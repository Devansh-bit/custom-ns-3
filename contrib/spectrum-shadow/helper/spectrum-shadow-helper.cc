/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "spectrum-shadow-helper.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/ssid.h"
#include "ns3/wifi-net-device.h"
#include "ns3/spectrum-wifi-phy.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/random-variable-stream.h"
#include "ns3/non-communicating-net-device.h"
#include "ns3/config.h"

#include <stdexcept>
#include <iostream>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace ns3 {

// Global Kafka producer pointer for PSD callback (set during SetupKafkaStreaming)
static Ptr<SpectrumKafkaProducer> g_kafkaProducer = nullptr;
static std::map<std::string, uint32_t> g_tracePathToNodeId;

// Static callback for PSD data forwarding to Kafka
static void
KafkaPsdCallback(std::string context, Ptr<const SpectrumValue> psd)
{
    if (g_kafkaProducer && psd)
    {
        // Extract node ID from context path
        auto it = g_tracePathToNodeId.find(context);
        uint32_t nodeId = (it != g_tracePathToNodeId.end()) ? it->second : 0;
        g_kafkaProducer->ReceivePsd(nodeId, psd);
    }
}

// Generate timestamp string for unique filenames
static std::string GetTimestampString()
{
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y%m%d-%H%M%S");
    return oss.str();
}

NS_LOG_COMPONENT_DEFINE("SpectrumShadowHelper");

SpectrumShadowHelper::SpectrumShadowHelper()
    : m_configLoaded(false)
{
    NS_LOG_FUNCTION(this);

    // Initialize setup tracking
    m_setupComplete["nodes"] = false;
    m_setupComplete["channel"] = false;
    m_setupComplete["wifi"] = false;
    m_setupComplete["mobility"] = false;
    m_setupComplete["interferers"] = false;
    m_setupComplete["analyzers"] = false;

    // Create core manager
    m_spectrumShadow = CreateObject<SpectrumShadow>();
}

SpectrumShadowHelper::~SpectrumShadowHelper()
{
    NS_LOG_FUNCTION(this);
}

// ==================== CONFIGURATION ====================

bool
SpectrumShadowHelper::LoadConfig(const std::string& configFile)
{
    NS_LOG_FUNCTION(this << configFile);

    try {
        m_simConfig = SimulationConfigParser::ParseFile(configFile);
        m_configLoaded = true;
        NS_LOG_INFO("Configuration loaded from: " << configFile);
        NS_LOG_INFO("  APs: " << m_simConfig.aps.size());
        NS_LOG_INFO("  STAs: " << m_simConfig.stas.size());
        NS_LOG_INFO("  Waypoints: " << m_simConfig.waypoints.size());
        NS_LOG_INFO("  Simulation time: " << m_simConfig.simulationTime << "s");
        return true;
    }
    catch (const std::exception& e) {
        NS_LOG_ERROR("Failed to load config: " << e.what());
        return false;
    }
}

void
SpectrumShadowHelper::SetSpectrumConfig(const SpectrumShadowConfig& config)
{
    NS_LOG_FUNCTION(this);
    m_spectrumConfig = config;
    m_spectrumShadow->SetConfig(config);
}

SimulationConfigData
SpectrumShadowHelper::GetSimulationConfig() const
{
    return m_simConfig;
}

// ==================== SETUP METHODS ====================

void
SpectrumShadowHelper::SetupNodes()
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(m_configLoaded, "Configuration must be loaded before SetupNodes()");

    // Create AP nodes
    m_apNodes.Create(m_simConfig.aps.size());
    NS_LOG_INFO("Created " << m_apNodes.GetN() << " AP nodes");

    // Create STA nodes
    m_staNodes.Create(m_simConfig.stas.size());
    NS_LOG_INFO("Created " << m_staNodes.GetN() << " STA nodes");

    MarkSetupComplete("nodes");
}

void
SpectrumShadowHelper::SetupSpectrumChannel()
{
    NS_LOG_FUNCTION(this);
    ValidatePrerequisites("channel", {});

    // Create spectrum channel
    m_channel = CreateObject<MultiModelSpectrumChannel>();

    // Add propagation loss model (same as main simulation)
    Ptr<LogDistancePropagationLossModel> lossModel =
        CreateObject<LogDistancePropagationLossModel>();
    lossModel->SetAttribute("Exponent", DoubleValue(3.0));
    lossModel->SetAttribute("ReferenceDistance", DoubleValue(1.0));
    lossModel->SetAttribute("ReferenceLoss", DoubleValue(46.6777));
    m_channel->AddPropagationLossModel(lossModel);

    // Add propagation delay model
    Ptr<ConstantSpeedPropagationDelayModel> delayModel =
        CreateObject<ConstantSpeedPropagationDelayModel>();
    m_channel->SetPropagationDelayModel(delayModel);

    // Set channel on core manager
    m_spectrumShadow->SetChannel(m_channel);

    NS_LOG_INFO("Spectrum channel created with LogDistance propagation (exponent=3.0)");

    MarkSetupComplete("channel");
}

void
SpectrumShadowHelper::SetupWifiDevices()
{
    NS_LOG_FUNCTION(this);
    ValidatePrerequisites("wifi", {"nodes", "channel"});

    // WiFi helper setup
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::IdealWifiManager");

    // Spectrum PHY helper
    SpectrumWifiPhyHelper specPhy;
    specPhy.SetChannel(m_channel);

    // MAC helper
    WifiMacHelper mac;
    Ssid ssid = Ssid("SpectrumShadow-Network");

    // ========== Install AP devices ==========
    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid),
                "BeaconGeneration", BooleanValue(true));

    for (size_t i = 0; i < m_simConfig.aps.size(); i++) {
        uint8_t ch = m_simConfig.aps[i].channel;
        WifiPhyBand band = GetBandForChannel(ch);

        std::string bandStr = (band == WIFI_PHY_BAND_5GHZ) ? "BAND_5GHZ" : "BAND_2_4GHZ";
        std::string channelSettings = "{" + std::to_string(ch) + ", 20, " + bandStr + ", 0}";

        specPhy.Set("ChannelSettings", StringValue(channelSettings));
        specPhy.Set("TxPowerStart", DoubleValue(m_simConfig.aps[i].txPower));
        specPhy.Set("TxPowerEnd", DoubleValue(m_simConfig.aps[i].txPower));

        NetDeviceContainer dev = wifi.Install(specPhy, mac, m_apNodes.Get(i));
        m_apDevices.Add(dev);

        NS_LOG_INFO("AP" << i << " installed on channel " << (int)ch
                    << " (" << bandStr << "), TX power: " << m_simConfig.aps[i].txPower << " dBm");
    }

    // ========== Install STA devices ==========
    // STAs start on first AP's channel
    uint8_t staChannel = m_simConfig.aps[0].channel;
    WifiPhyBand staBand = GetBandForChannel(staChannel);
    std::string staBandStr = (staBand == WIFI_PHY_BAND_5GHZ) ? "BAND_5GHZ" : "BAND_2_4GHZ";
    std::string staChannelSettings = "{" + std::to_string(staChannel) + ", 20, " + staBandStr + ", 0}";

    specPhy.Set("ChannelSettings", StringValue(staChannelSettings));
    specPhy.Set("TxPowerStart", DoubleValue(20.0));  // Default STA TX power
    specPhy.Set("TxPowerEnd", DoubleValue(20.0));

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(false));

    m_staDevices = wifi.Install(specPhy, mac, m_staNodes);

    NS_LOG_INFO("Installed " << m_staDevices.GetN() << " STA devices on channel "
                << (int)staChannel << " (" << staBandStr << ")");

    MarkSetupComplete("wifi");
}

void
SpectrumShadowHelper::SetupMobility()
{
    NS_LOG_FUNCTION(this);
    ValidatePrerequisites("mobility", {"nodes"});

    // ========== AP Mobility (static positions) ==========
    MobilityHelper apMobility;
    apMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    apMobility.Install(m_apNodes);

    for (size_t i = 0; i < m_simConfig.aps.size(); i++) {
        Ptr<MobilityModel> mob = m_apNodes.Get(i)->GetObject<MobilityModel>();
        mob->SetPosition(m_simConfig.aps[i].position);
        NS_LOG_INFO("AP" << i << " position: " << m_simConfig.aps[i].position);
    }

    // ========== STA Mobility (waypoint-based) ==========
    // Create waypoint grid from config
    m_waypointGrid = CreateObject<WaypointGrid>();
    for (const auto& wp : m_simConfig.waypoints) {
        m_waypointGrid->AddWaypoint(wp.id, wp.position);
    }
    NS_LOG_INFO("Created waypoint grid with " << m_waypointGrid->GetWaypointCount() << " waypoints");

    // Setup waypoint mobility helper
    m_waypointHelper.SetWaypointGrid(m_waypointGrid);

    // Install on each STA
    for (size_t i = 0; i < m_simConfig.stas.size(); i++) {
        m_waypointHelper.Install(m_staNodes.Get(i), m_simConfig.stas[i]);
        NS_LOG_INFO("STA" << i << " configured with waypoint mobility");
    }

    // Start mobility for all STAs
    m_waypointHelper.StartMobility();

    NS_LOG_INFO("Mobility setup complete: " << m_apNodes.GetN() << " APs (static), "
                << m_staNodes.GetN() << " STAs (waypoint)");

    MarkSetupComplete("mobility");
}

void
SpectrumShadowHelper::SetupInterferers()
{
    NS_LOG_FUNCTION(this);
    ValidatePrerequisites("interferers", {"channel"});

    if (!m_simConfig.virtualInterferers.enabled) {
        NS_LOG_INFO("Virtual interferers disabled in config, skipping");
        MarkSetupComplete("interferers");
        return;
    }

    // Create spectrogram generation helper
    m_spectroHelper = std::make_unique<SpectrogramGenerationHelper>(
        m_spectrumShadow->GetAnnotationManager());

    // Create interferers
    CreateMicrowaveInterferers();
    CreateBluetoothInterferers();
    CreateZigbeeInterferers();
    CreateCordlessInterferers();
    CreateRadarInterferers();

    NS_LOG_INFO("Created " << m_interfererNodes.GetN() << " spectrum interferers");

    MarkSetupComplete("interferers");
}

void
SpectrumShadowHelper::SetupSpectrumAnalyzers()
{
    NS_LOG_FUNCTION(this);
    ValidatePrerequisites("analyzers", {"nodes", "channel", "wifi"});

    // Initialize core manager
    m_spectrumShadow->Initialize();

    // ========== Setup Spectrum Analyser Logger ==========
    if (m_spectrumConfig.enableFileLogging) {
        // Enable shared .tr file mode (all APs write to single file)
        // Filename includes timestamp for distinguishing simulation runs
        std::string trFilename = m_spectrumConfig.logPath + "/spectrum-2.4ghz-" + GetTimestampString() + ".tr";
        m_loggerHelper.EnableSharedFile("2.4GHz", trFilename);

        m_loggerHelper.SetChannel(m_channel);
        m_loggerHelper.SetFrequencyRange(
            m_spectrumConfig.startFrequency,
            m_spectrumConfig.frequencyResolution,
            m_spectrumConfig.numFrequencyBins
        );
        m_loggerHelper.SetNoiseFloor(m_spectrumConfig.noiseFloorDbm);
        m_loggerHelper.SetLoggingInterval(m_spectrumConfig.logInterval);
        m_loggerHelper.EnableConsoleOutput(m_spectrumConfig.enableConsoleOutput);

        m_loggerHelper.InstallOnNodes(m_apNodes, m_spectrumConfig.logPath + "/psd-2.4ghz");

        NS_LOG_INFO("2.4 GHz spectrum loggers installed on " << m_apNodes.GetN() << " APs");
        NS_LOG_INFO("2.4 GHz .tr file: " << trFilename);

        // ========== Setup 5 GHz Spectrum Analyser Logger ==========
        if (m_spectrumConfig.enable5GHz) {
            std::string trFilename5GHz = m_spectrumConfig.logPath + "/spectrum-5ghz-" + GetTimestampString() + ".tr";
            m_loggerHelper5GHz.EnableSharedFile("5GHz", trFilename5GHz);

            m_loggerHelper5GHz.SetChannel(m_channel);
            m_loggerHelper5GHz.SetFrequencyRange(
                m_spectrumConfig.startFrequency5GHz,
                m_spectrumConfig.frequencyResolution5GHz,
                m_spectrumConfig.numFrequencyBins5GHz
            );
            m_loggerHelper5GHz.SetNoiseFloor(m_spectrumConfig.noiseFloorDbm);
            m_loggerHelper5GHz.SetLoggingInterval(m_spectrumConfig.logInterval);
            m_loggerHelper5GHz.EnableConsoleOutput(m_spectrumConfig.enableConsoleOutput);

            m_loggerHelper5GHz.InstallOnNodes(m_apNodes, m_spectrumConfig.logPath + "/psd-5ghz");

            NS_LOG_INFO("5 GHz spectrum loggers installed on " << m_apNodes.GetN() << " APs");
            NS_LOG_INFO("5 GHz .tr file: " << trFilename5GHz);
        }

        // ========== Setup Kafka Streaming (if enabled) ==========
        if (m_spectrumConfig.enableKafkaStreaming) {
            SetupKafkaStreaming();
            NS_LOG_INFO("Kafka spectrum streaming enabled");
        }
        // Note: CNN windowing (cnn_1.tr, cnn_2.tr every 100ms) disabled by default
        // The dual-band .tr files (2.4GHz + 5GHz) above provide spectrum data
    }

    // ========== Setup Spectrum Pipe Streamer ==========
    if (m_spectrumConfig.enablePipeStreaming) {
        // Enable shared pipe mode (all nodes write to single pipe)
        m_streamerHelper.EnableSharedPipe(m_spectrumConfig.pipePath + "/spectrum.pipe");

        m_streamerHelper.SetBasePath(m_spectrumConfig.pipePath);
        m_streamerHelper.SetChannel(m_channel);
        m_streamerHelper.SetFrequencyRange(
            m_spectrumConfig.startFrequency,
            m_spectrumConfig.frequencyResolution,
            m_spectrumConfig.numFrequencyBins
        );
        m_streamerHelper.SetNoiseFloor(m_spectrumConfig.noiseFloorDbm);
        m_streamerHelper.SetStreamInterval(m_spectrumConfig.streamInterval);

        m_streamerHelper.InstallOnNodes(m_apNodes);

        NS_LOG_INFO("Spectrum pipe streamers installed, base path: " << m_spectrumConfig.pipePath);
    }

    MarkSetupComplete("analyzers");
}

// ==================== EXECUTION ====================

void
SpectrumShadowHelper::Run(Time duration)
{
    NS_LOG_FUNCTION(this << duration);

    // Validate all setup complete
    for (const auto& step : m_setupComplete) {
        if (!step.second && step.first != "interferers") {  // Interferers are optional
            NS_LOG_WARN("Setup step '" << step.first << "' not complete before Run()");
        }
    }

    // Determine simulation duration
    Time simDuration = duration.IsZero() ? Seconds(m_simConfig.simulationTime) : duration;

    NS_LOG_INFO("Starting spectrum shadow simulation for " << simDuration.GetSeconds() << "s");

    Simulator::Stop(simDuration);
    Simulator::Run();

    NS_LOG_INFO("Simulation complete");
}

void
SpectrumShadowHelper::ExportResults()
{
    NS_LOG_FUNCTION(this);

    // Close shared .tr files if enabled
    if (m_loggerHelper.IsSharedFileEnabled()) {
        m_loggerHelper.CloseSharedFile();
        NS_LOG_INFO("Closed 2.4 GHz .tr file");
    }
    if (m_loggerHelper5GHz.IsSharedFileEnabled()) {
        m_loggerHelper5GHz.CloseSharedFile();
        NS_LOG_INFO("Closed 5 GHz .tr file");
    }

    // Export annotations
    m_spectrumShadow->ExportAnnotations();

    // Log statistics
    SpectrumShadowStats stats = m_spectrumShadow->GetStats();
    NS_LOG_INFO("=== Spectrum Shadow Statistics ===");
    NS_LOG_INFO("  Total PSD samples: " << stats.totalPsdSamples);
    NS_LOG_INFO("  Packets sent: " << stats.psdPacketsSent);
    NS_LOG_INFO("  Bytes written: " << stats.psdBytesWritten);
    NS_LOG_INFO("  Active interferers: " << stats.interferersActive);
    NS_LOG_INFO("  Duration: " << stats.simulationDuration.GetSeconds() << "s");
}

// ==================== ACCESSORS ====================

NodeContainer
SpectrumShadowHelper::GetApNodes() const
{
    return m_apNodes;
}

NodeContainer
SpectrumShadowHelper::GetStaNodes() const
{
    return m_staNodes;
}

NodeContainer
SpectrumShadowHelper::GetInterfererNodes() const
{
    return m_interfererNodes;
}

Ptr<MultiModelSpectrumChannel>
SpectrumShadowHelper::GetSpectrumChannel() const
{
    return m_channel;
}

NetDeviceContainer
SpectrumShadowHelper::GetApDevices() const
{
    return m_apDevices;
}

NetDeviceContainer
SpectrumShadowHelper::GetStaDevices() const
{
    return m_staDevices;
}

Ptr<SpectrumShadow>
SpectrumShadowHelper::GetSpectrumShadow() const
{
    return m_spectrumShadow;
}

SpectrumShadowStats
SpectrumShadowHelper::GetStats() const
{
    return m_spectrumShadow->GetStats();
}

bool
SpectrumShadowHelper::IsSetupComplete(const std::string& step) const
{
    auto it = m_setupComplete.find(step);
    return it != m_setupComplete.end() && it->second;
}

// ==================== PRIVATE HELPERS ====================

void
SpectrumShadowHelper::ValidatePrerequisites(const std::string& step,
                                             const std::vector<std::string>& prerequisites) const
{
    for (const auto& prereq : prerequisites) {
        auto it = m_setupComplete.find(prereq);
        if (it == m_setupComplete.end() || !it->second) {
            NS_FATAL_ERROR("Setup step '" << prereq << "' must be completed before '" << step << "'");
        }
    }
}

void
SpectrumShadowHelper::MarkSetupComplete(const std::string& step)
{
    m_setupComplete[step] = true;
    NS_LOG_INFO("Setup step '" << step << "' complete");
}

WifiPhyBand
SpectrumShadowHelper::GetBandForChannel(uint8_t channel) const
{
    // Channels 1-14 are 2.4 GHz, 36+ are 5 GHz
    return (channel >= 36) ? WIFI_PHY_BAND_5GHZ : WIFI_PHY_BAND_2_4GHZ;
}

void
SpectrumShadowHelper::CreateMicrowaveInterferers()
{
    for (const auto& mw : m_simConfig.virtualInterferers.microwaves) {
        if (!mw.active) continue;

        Ptr<Node> node = CreateObject<Node>();
        m_interfererNodes.Add(node);

        // Set position
        Ptr<ConstantPositionMobilityModel> mob = CreateObject<ConstantPositionMobilityModel>();
        mob->SetPosition(mw.position);
        node->AggregateObject(mob);

        // Convert to spectrum interferer using LAZY scheduling
        double durationSec = m_simConfig.simulationTime - mw.startTime;
        if (durationSec <= 0) continue;

        Ptr<LazyMicrowaveInterferer> interferer = CreateObject<LazyMicrowaveInterferer>();
        interferer->Configure(
            m_channel,
            node,
            Seconds(mw.startTime),
            Seconds(durationSec),
            mw.centerFrequencyGHz * 1e9,  // Hz
            mw.bandwidthMHz * 1e6,         // Hz
            Seconds(1.0 / 60.0),           // 60 Hz duty period
            mw.dutyCycle,
            mw.txPowerDbm - 30             // Approximate PSD conversion
        );

        // Set schedule for synchronized ON/OFF cycling with VirtualInterferers
        if (mw.schedule.hasSchedule)
        {
            interferer->SetSchedule(Seconds(mw.schedule.onDuration),
                                   Seconds(mw.schedule.offDuration));
            NS_LOG_INFO("  Schedule: " << mw.schedule.onDuration << "s ON / "
                        << mw.schedule.offDuration << "s OFF");
        }

        interferer->Start();
        m_lazyInterferers.push_back(interferer);

        NS_LOG_INFO("Created LAZY microwave interferer at " << mw.position
                    << ", freq=" << mw.centerFrequencyGHz << " GHz");
    }
}

void
SpectrumShadowHelper::CreateBluetoothInterferers()
{
    for (const auto& bt : m_simConfig.virtualInterferers.bluetooths) {
        if (!bt.active) continue;

        Ptr<Node> node = CreateObject<Node>();
        m_interfererNodes.Add(node);

        // Set position
        Ptr<ConstantPositionMobilityModel> mob = CreateObject<ConstantPositionMobilityModel>();
        mob->SetPosition(bt.position);
        node->AggregateObject(mob);

        // Convert to spectrum interferer using LAZY scheduling
        double durationSec = m_simConfig.simulationTime - bt.startTime;
        if (durationSec <= 0) continue;

        Ptr<LazyBluetoothInterferer> interferer = CreateObject<LazyBluetoothInterferer>();
        interferer->Configure(
            m_channel,
            node,
            Seconds(bt.startTime),
            Seconds(durationSec),
            Seconds(bt.hopInterval),
            bt.txPowerDbm - 30,           // Approximate PSD conversion
            bt.hoppingSeed
        );

        // Set schedule for synchronized ON/OFF cycling with VirtualInterferers
        if (bt.schedule.hasSchedule)
        {
            interferer->SetSchedule(Seconds(bt.schedule.onDuration),
                                   Seconds(bt.schedule.offDuration));
            NS_LOG_INFO("  Schedule: " << bt.schedule.onDuration << "s ON / "
                        << bt.schedule.offDuration << "s OFF");
        }

        interferer->Start();
        m_lazyInterferers.push_back(interferer);

        NS_LOG_INFO("Created LAZY Bluetooth interferer at " << bt.position
                    << ", profile=" << bt.profile);
    }
}

void
SpectrumShadowHelper::CreateZigbeeInterferers()
{
    for (const auto& zb : m_simConfig.virtualInterferers.zigbees) {
        if (!zb.active) continue;

        Ptr<Node> node = CreateObject<Node>();
        m_interfererNodes.Add(node);

        // Set position
        Ptr<ConstantPositionMobilityModel> mob = CreateObject<ConstantPositionMobilityModel>();
        mob->SetPosition(zb.position);
        node->AggregateObject(mob);

        // Convert to spectrum interferer using LAZY scheduling
        double durationSec = m_simConfig.simulationTime - zb.startTime;
        if (durationSec <= 0) continue;

        Ptr<LazyZigbeeInterferer> interferer = CreateObject<LazyZigbeeInterferer>();
        interferer->Configure(
            m_channel,
            node,
            Seconds(zb.startTime),
            Seconds(durationSec),
            zb.zigbeeChannel,
            MilliSeconds(100),            // Transmission interval
            zb.txPowerDbm - 30            // Approximate PSD conversion
        );

        // Set schedule for synchronized ON/OFF cycling with VirtualInterferers
        if (zb.schedule.hasSchedule)
        {
            interferer->SetSchedule(Seconds(zb.schedule.onDuration),
                                   Seconds(zb.schedule.offDuration));
            NS_LOG_INFO("  Schedule: " << zb.schedule.onDuration << "s ON / "
                        << zb.schedule.offDuration << "s OFF");
        }

        interferer->Start();
        m_lazyInterferers.push_back(interferer);

        NS_LOG_INFO("Created LAZY ZigBee interferer at " << zb.position
                    << ", channel=" << (int)zb.zigbeeChannel);
    }
}

void
SpectrumShadowHelper::CreateCordlessInterferers()
{
    for (const auto& cl : m_simConfig.virtualInterferers.cordless) {
        if (!cl.active) continue;

        Ptr<Node> node = CreateObject<Node>();
        m_interfererNodes.Add(node);

        // Set position
        Ptr<ConstantPositionMobilityModel> mob = CreateObject<ConstantPositionMobilityModel>();
        mob->SetPosition(cl.position);
        node->AggregateObject(mob);

        // Convert to spectrum interferer using LAZY scheduling
        double durationSec = m_simConfig.simulationTime - cl.startTime;
        if (durationSec <= 0) continue;

        Ptr<LazyCordlessInterferer> interferer = CreateObject<LazyCordlessInterferer>();
        interferer->Configure(
            m_channel,
            node,
            Seconds(cl.startTime),
            Seconds(durationSec),
            Seconds(cl.hopInterval),
            Seconds(cl.hopInterval * 0.8),  // Transmission duration per hop
            cl.bandwidthMhz * 1e6,           // Hz
            cl.txPowerDbm - 30               // Approximate PSD conversion
        );

        // Set schedule for synchronized ON/OFF cycling with VirtualInterferers
        if (cl.schedule.hasSchedule)
        {
            interferer->SetSchedule(Seconds(cl.schedule.onDuration),
                                   Seconds(cl.schedule.offDuration));
            NS_LOG_INFO("  Schedule: " << cl.schedule.onDuration << "s ON / "
                        << cl.schedule.offDuration << "s OFF");
        }

        interferer->Start();
        m_lazyInterferers.push_back(interferer);

        NS_LOG_INFO("Created LAZY cordless interferer at " << cl.position);
    }
}

void
SpectrumShadowHelper::CreateRadarInterferers()
{
    for (const auto& radar : m_simConfig.virtualInterferers.radars) {
        if (!radar.active) continue;

        Ptr<Node> node = CreateObject<Node>();
        m_interfererNodes.Add(node);

        // Set position
        Ptr<ConstantPositionMobilityModel> mob = CreateObject<ConstantPositionMobilityModel>();
        mob->SetPosition(radar.position);
        node->AggregateObject(mob);

        // Determine radar parameters from type
        double pulseWidth;
        double prf;
        int pulsesPerBurst;

        if (radar.radarType == "MILITARY" || radar.radarType == "MILITARY_RADAR") {
            pulseWidth = 0.5e-6;
            prf = 3000;
            pulsesPerBurst = 10;
        } else if (radar.radarType == "AIRPORT" || radar.radarType == "AIRPORT_RADAR" || radar.radarType == "AVIATION") {
            pulseWidth = 5e-6;
            prf = 500;
            pulsesPerBurst = 18;
        } else {
            // Default: WEATHER_RADAR
            pulseWidth = 1.5e-6;
            prf = 1000;
            pulsesPerBurst = 15;
        }

        // Calculate duration and center frequency
        double durationSec = m_simConfig.simulationTime - radar.startTime;
        if (durationSec <= 0) continue;

        double centerFreq = SpectrogramGenerationHelper::ChannelToFrequency(radar.dfsChannel);

        // Create LAZY radar interferer
        Ptr<LazyRadarInterferer> interferer = CreateObject<LazyRadarInterferer>();
        interferer->Configure(
            m_channel,
            node,
            Seconds(radar.startTime),
            Seconds(durationSec),
            centerFreq,
            pulseWidth,
            prf,
            pulsesPerBurst,
            Seconds(1.0),                 // Burst interval (antenna rotation)
            radar.txPowerDbm - 30         // Approximate PSD conversion
        );

        // Set schedule for synchronized ON/OFF cycling with VirtualInterferers
        if (radar.schedule.hasSchedule)
        {
            interferer->SetSchedule(Seconds(radar.schedule.onDuration),
                                   Seconds(radar.schedule.offDuration));
            NS_LOG_INFO("  Schedule: " << radar.schedule.onDuration << "s ON / "
                        << radar.schedule.offDuration << "s OFF");
        }

        // Configure channel hopping for synchronized behavior with VirtualRadarInterferer
        if (!radar.dfsChannels.empty() && radar.hopIntervalSec > 0)
        {
            // Use shared seed for synchronized random hopping between simulations
            // Seed 42 is arbitrary but must match VirtualRadarInterferer
            uint32_t sharedSeed = 42;
            interferer->SetChannelHopping(radar.dfsChannels,
                                          Seconds(radar.hopIntervalSec),
                                          radar.randomHopping,
                                          sharedSeed);
            NS_LOG_INFO("  Channel hopping: " << radar.dfsChannels.size() << " channels, "
                        << radar.hopIntervalSec << "s interval, "
                        << (radar.randomHopping ? "random" : "sequential"));
        }

        // Configure wideband span
        if (radar.spanLength > 0 || radar.randomSpan)
        {
            interferer->SetWidebandSpan(radar.spanLength,
                                        radar.maxSpanLength,
                                        radar.randomSpan);
            NS_LOG_INFO("  Wideband span: ±" << (int)radar.spanLength
                        << " (max=" << (int)radar.maxSpanLength
                        << ", random=" << (radar.randomSpan ? "yes" : "no") << ")");
        }

        interferer->Start();
        m_lazyInterferers.push_back(interferer);

        NS_LOG_INFO("Created LAZY DFS radar interferer at " << radar.position
                    << ", channel=" << (int)radar.dfsChannel
                    << ", type=" << radar.radarType
                    << ", CAC=" << SpectrogramGenerationHelper::GetCacTime(radar.dfsChannel) << "s");
    }
}

void
SpectrumShadowHelper::StartCnnWindow()
{
    m_cnnWindowIndex++;
    
    // Create a new logger helper for this window
    m_cnnLoggerHelpers.emplace_back();
    auto& logger = m_cnnLoggerHelpers.back();
    
    // Create filename: cnn_1.tr, cnn_2.tr, etc.
    std::string windowName = "CNN_" + std::to_string(m_cnnWindowIndex);
    std::string cnnFilename = m_spectrumConfig.logPath + "/cnn_" + std::to_string(m_cnnWindowIndex) + ".tr";
    
    logger.EnableSharedFile(windowName, cnnFilename);
    logger.SetChannel(m_channel);
    logger.SetFrequencyRange(
        m_spectrumConfig.startFrequency,
        m_spectrumConfig.frequencyResolution,
        m_spectrumConfig.numFrequencyBins
    );
    logger.SetNoiseFloor(m_spectrumConfig.noiseFloorDbm);
    logger.SetLoggingInterval(MilliSeconds(10));  // 10ms resolution
    logger.EnableConsoleOutput(false);
    logger.InstallOnNodes(m_apNodes, m_spectrumConfig.logPath + "/cnn-psd-" + std::to_string(m_cnnWindowIndex));
    
    NS_LOG_INFO("[CNN] Window " << m_cnnWindowIndex << " started: " << cnnFilename);
    std::cout << "[CNN] Window " << m_cnnWindowIndex << " started: " << cnnFilename << std::endl;
    
    // Schedule close of this window after 100ms
    uint32_t currentWindow = m_cnnWindowIndex;
    Simulator::Schedule(MilliSeconds(100), &SpectrumShadowHelper::CloseCnnWindow, this, currentWindow);
    
    // Schedule next window start after 100ms (overlapping at the boundary)
    // Check if simulation will still be running
    Time currentTime = Simulator::Now();
    Time simEndTime = Seconds(m_simConfig.simulationTime);
    if (currentTime + MilliSeconds(100) < simEndTime) {
        Simulator::Schedule(MilliSeconds(100), &SpectrumShadowHelper::StartCnnWindow, this);
    }
}

void
SpectrumShadowHelper::CloseCnnWindow(uint32_t windowIndex)
{
    std::string windowName = "CNN_" + std::to_string(windowIndex);
    SpectrumAnalyserLogger::CloseSharedFile(windowName);
    
    NS_LOG_INFO("[CNN] Window " << windowIndex << " closed (cnn_" << windowIndex << ".tr ready)");
    std::cout << "[CNN] Window " << windowIndex << " closed (cnn_" << windowIndex << ".tr ready)" << std::endl;
}

void
SpectrumShadowHelper::SetupKafkaStreaming()
{
    NS_LOG_FUNCTION(this);
    
    // Create Kafka producer for spectrum data
    m_kafkaProducer = CreateObject<SpectrumKafkaProducer>();
    
    // Configure
    SpectrumKafkaConfig kafkaConfig;
    kafkaConfig.brokers = m_spectrumConfig.kafkaBrokers;
    kafkaConfig.topic = m_spectrumConfig.kafkaTopic;
    kafkaConfig.simulationId = m_spectrumConfig.simulationId;
    kafkaConfig.windowDuration = MilliSeconds(100);  // 100ms windows
    kafkaConfig.sampleInterval = m_spectrumConfig.analyzerInterval;
    kafkaConfig.startFrequency = m_spectrumConfig.startFrequency;
    kafkaConfig.frequencyResolution = m_spectrumConfig.frequencyResolution;
    kafkaConfig.numFrequencyBins = m_spectrumConfig.numFrequencyBins;
    kafkaConfig.bandId = "2.4GHz";
    
    m_kafkaProducer->SetConfig(kafkaConfig);
    
    // Initialize Kafka connection
    if (!m_kafkaProducer->Initialize())
    {
        NS_LOG_ERROR("Failed to initialize Kafka producer");
        std::cout << "[SpectrumKafka] ERROR: Failed to initialize - continuing without Kafka streaming" << std::endl;
        // Note: NOT falling back to CNN windowing (cnn_*.tr every 100ms) as it causes slowdown
        // The dual-band .tr files (2.4GHz + 5GHz) are still available for spectrum data
        return;
    }
    
    // Set global pointer for static callback
    g_kafkaProducer = m_kafkaProducer;
    
    // Connect to spectrum analyzer trace sources on each AP
    for (uint32_t i = 0; i < m_apNodes.GetN(); i++)
    {
        Ptr<Node> node = m_apNodes.Get(i);
        uint32_t nodeId = node->GetId();
        
        // Find the NonCommunicatingNetDevice (spectrum analyzer)
        for (uint32_t d = 0; d < node->GetNDevices(); d++)
        {
            Ptr<NetDevice> dev = node->GetDevice(d);
            Ptr<NonCommunicatingNetDevice> nonCommDev = DynamicCast<NonCommunicatingNetDevice>(dev);
            if (nonCommDev)
            {
                std::ostringstream oss;
                oss << "/NodeList/" << nodeId
                    << "/DeviceList/" << d
                    << "/$ns3::NonCommunicatingNetDevice/Phy/AveragePowerSpectralDensityReport";
                
                std::string tracePath = oss.str();
                g_tracePathToNodeId[tracePath] = nodeId;
                
                Config::Connect(tracePath, MakeCallback(&KafkaPsdCallback));
                
                NS_LOG_INFO("Connected Kafka producer to node " << nodeId << " spectrum analyzer");
                std::cout << "[SpectrumKafka] Connected to node " << nodeId << " spectrum analyzer" << std::endl;
                break;
            }
        }
    }
    
    // Start the producer (schedules 100ms windows)
    m_kafkaProducer->Start();
    
    std::cout << "[SpectrumKafka] Kafka streaming enabled: " << m_spectrumConfig.kafkaBrokers
              << " topic=" << m_spectrumConfig.kafkaTopic << std::endl;
    
    NS_LOG_INFO("Kafka spectrum producer started");
}

} // namespace ns3
