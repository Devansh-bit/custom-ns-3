/**
 * Spectrum Shadow Simulation
 *
 * This simulation runs alongside the main config-simulation to provide
 * spectral data (PSD) collection. It reads the same JSON configuration
 * and replicates the exact node positions and STA movements, but uses
 * SpectrumWifiPhy instead of YansWifiPhy to enable spectrum analysis.
 *
 * Key features:
 * - Uses same config.json as main simulation
 * - Replicates exact STA waypoint movements
 * - Generates PSD data via SpectrumAnalyserLogger
 * - Streams PSD to dashboard via SpectrumPipeStreamer (named pipes)
 * - Converts VirtualInterferers to spectrum-based interferers
 *
 * Usage:
 *   ./ns3 run "spectrum-shadow-sim --configFile=config.json"
 *   ./ns3 run "spectrum-shadow-sim --configFile=config.json --pipePath=/tmp/spectrum"
 *
 * The simulation outputs:
 * - Shared pipe at pipePath/spectrum.pipe (for real-time CNN/dashboard)
 * - Log files at logPath/psd-X.csv (for offline analysis)
 * - Annotations at annotationFile (JSON format)
 */

#include "ns3/core-module.h"
#include "ns3/spectrum-shadow-helper.h"

#include <iostream>
#include <signal.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SpectrumShadowSim");

// Global helper for signal handling
SpectrumShadowHelper* g_helper = nullptr;

void
SignalHandler(int signum)
{
    std::cout << "\n[SpectrumShadow] Received signal " << signum << ", exporting results..." << std::endl;
    if (g_helper) {
        g_helper->ExportResults();
    }
    Simulator::Stop();
}

int
main(int argc, char* argv[])
{
    // ========================================================================
    // COMMAND LINE ARGUMENTS
    // ========================================================================
    std::string configFile = "config.json";
    std::string pipePath = "/tmp/ns3-spectrum";
    std::string logPath = "./spectrum-logs";
    std::string annotationFile = "spectrum-annotations.json";
    double streamIntervalMs = 10.0;
    double logIntervalMs = 100.0;
    double startFreqGHz = 2.4;
    double freqResolutionKHz = 100.0;
    uint32_t numBins = 1000;
    bool enablePipes = true;
    bool enableLogs = true;
    bool verbose = false;
    
    // Kafka streaming options (replaces file-based CNN windows)
    bool enableKafka = false;
    std::string kafkaBrokers = "localhost:9092";
    std::string kafkaTopic = "spectrum-data";
    std::string simulationId = "sim-001";

    CommandLine cmd;
    cmd.AddValue("configFile", "JSON configuration file (same as main sim)", configFile);
    cmd.AddValue("pipePath", "Base path for named pipes", pipePath);
    cmd.AddValue("logPath", "Path for log files", logPath);
    cmd.AddValue("annotationFile", "Output file for interference annotations", annotationFile);
    cmd.AddValue("streamInterval", "Pipe streaming interval (ms)", streamIntervalMs);
    cmd.AddValue("logInterval", "File logging interval (ms)", logIntervalMs);
    cmd.AddValue("startFreq", "Start frequency (GHz)", startFreqGHz);
    cmd.AddValue("freqResolution", "Frequency resolution (kHz)", freqResolutionKHz);
    cmd.AddValue("numBins", "Number of frequency bins", numBins);
    cmd.AddValue("enablePipes", "Enable named pipe streaming", enablePipes);
    cmd.AddValue("enableLogs", "Enable file logging", enableLogs);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    // Kafka options
    cmd.AddValue("enableKafka", "Enable Kafka spectrum streaming (replaces file-based)", enableKafka);
    cmd.AddValue("kafkaBrokers", "Kafka broker addresses", kafkaBrokers);
    cmd.AddValue("kafkaTopic", "Kafka topic for spectrum data", kafkaTopic);
    cmd.AddValue("simulationId", "Simulation ID for Kafka messages", simulationId);
    cmd.Parse(argc, argv);

    if (verbose) {
        LogComponentEnable("SpectrumShadowSim", LOG_LEVEL_ALL);
        LogComponentEnable("SpectrumShadow", LOG_LEVEL_INFO);
        LogComponentEnable("SpectrumShadowHelper", LOG_LEVEL_INFO);
    }

    // ========================================================================
    // SETUP SIGNAL HANDLER
    // ========================================================================
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE to prevent crash on reader disconnect

    std::cout << "========================================" << std::endl;
    std::cout << "   SPECTRUM SHADOW SIMULATION" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Config file: " << configFile << std::endl;
    std::cout << "Pipe path: " << pipePath << std::endl;
    std::cout << "Log path: " << logPath << std::endl;
    std::cout << "Frequency range: " << startFreqGHz << " GHz, "
              << freqResolutionKHz << " kHz resolution, "
              << numBins << " bins" << std::endl;

    // ========================================================================
    // CREATE AND CONFIGURE HELPER
    // ========================================================================
    SpectrumShadowHelper helper;
    g_helper = &helper;

    // Load configuration (same as main simulation)
    if (!helper.LoadConfig(configFile)) {
        std::cerr << "ERROR: Failed to load config file: " << configFile << std::endl;
        return 1;
    }

    // Configure spectrum-specific settings
    SpectrumShadowConfig specConfig;
    specConfig.startFrequency = startFreqGHz * 1e9;
    specConfig.frequencyResolution = freqResolutionKHz * 1e3;
    specConfig.numFrequencyBins = numBins;
    specConfig.noiseFloorDbm = -174.0;
    specConfig.streamInterval = MilliSeconds(streamIntervalMs);

    // Synchronized logging interval: use config's updateInterval for timestamp alignment
    {
        SimulationConfigData cfg = helper.GetSimulationConfig();
        if (logIntervalMs == 100.0 && cfg.virtualInterferers.updateInterval > 0) {
            // Use config's updateInterval (seconds -> Time)
            specConfig.logInterval = Seconds(cfg.virtualInterferers.updateInterval);
            std::cout << "Synchronized logging interval: "
                      << (cfg.virtualInterferers.updateInterval * 1000) << " ms "
                      << "(from config.virtualInterferers.updateInterval)" << std::endl;
        } else {
            specConfig.logInterval = MilliSeconds(logIntervalMs);
        }
    }

    specConfig.pipePath = pipePath;
    specConfig.logPath = logPath;
    specConfig.annotationFile = annotationFile;
    specConfig.enablePipeStreaming = enablePipes;
    specConfig.enableFileLogging = enableLogs;
    specConfig.enableConsoleOutput = verbose;
    
    // Kafka streaming configuration
    specConfig.enableKafkaStreaming = enableKafka;
    specConfig.kafkaBrokers = kafkaBrokers;
    specConfig.kafkaTopic = kafkaTopic;
    specConfig.simulationId = simulationId;
    
    if (enableKafka) {
        std::cout << "Kafka streaming: ENABLED" << std::endl;
        std::cout << "  Brokers: " << kafkaBrokers << std::endl;
        std::cout << "  Topic: " << kafkaTopic << std::endl;
    }

    helper.SetSpectrumConfig(specConfig);

    // ========================================================================
    // SETUP SIMULATION
    // ========================================================================
    std::cout << "\n[Setup] Creating nodes..." << std::endl;
    helper.SetupNodes();

    std::cout << "[Setup] Creating spectrum channel..." << std::endl;
    helper.SetupSpectrumChannel();

    std::cout << "[Setup] Installing WiFi devices (SpectrumWifiPhy)..." << std::endl;
    helper.SetupWifiDevices();

    std::cout << "[Setup] Configuring mobility (waypoints)..." << std::endl;
    helper.SetupMobility();

    std::cout << "[Setup] Creating spectrum interferers..." << std::endl;
    helper.SetupInterferers();

    std::cout << "[Setup] Installing spectrum analyzers..." << std::endl;
    helper.SetupSpectrumAnalyzers();

    // ========================================================================
    // PRINT SUMMARY
    // ========================================================================
    SimulationConfigData simConfig = helper.GetSimulationConfig();
    std::cout << "\n========================================" << std::endl;
    std::cout << "   SIMULATION READY" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "APs: " << helper.GetApNodes().GetN() << std::endl;
    std::cout << "STAs: " << helper.GetStaNodes().GetN() << std::endl;
    std::cout << "Interferers: " << helper.GetInterfererNodes().GetN() << std::endl;
    std::cout << "Duration: " << simConfig.simulationTime << " seconds" << std::endl;

    if (enablePipes) {
        std::cout << "\nShared spectrum pipe:" << std::endl;
        std::cout << "  " << pipePath << "/spectrum.pipe" << std::endl;
        std::cout << "\nTo read: python3 read-spectrum-pipe.py " << pipePath << "/spectrum.pipe" << std::endl;
    }

    // ========================================================================
    // RUN SIMULATION
    // ========================================================================
    std::cout << "\n[Run] Starting simulation..." << std::endl;
    helper.Run();

    // ========================================================================
    // EXPORT RESULTS
    // ========================================================================
    std::cout << "\n[Done] Exporting results..." << std::endl;
    helper.ExportResults();

    // Print statistics
    SpectrumShadowStats stats = helper.GetStats();
    std::cout << "\n========================================" << std::endl;
    std::cout << "   SIMULATION COMPLETE" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Duration: " << stats.simulationDuration.GetSeconds() << " seconds" << std::endl;
    std::cout << "Total PSD samples: " << stats.totalPsdSamples << std::endl;
    std::cout << "Packets sent: " << stats.psdPacketsSent << std::endl;
    std::cout << "Bytes written: " << stats.psdBytesWritten << std::endl;
    std::cout << "Annotations: " << annotationFile << std::endl;

    Simulator::Destroy();
    g_helper = nullptr;

    return 0;
}
