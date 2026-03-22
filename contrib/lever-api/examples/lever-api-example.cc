/*
 * This example demonstrates the LeverApi module for dynamic WiFi PHY configuration.
 *
 * The example creates a simple WiFi network with two nodes and shows how to:
 * 1. Create a LeverConfig object with traced parameters
 * 2. Install LeverApi application on WiFi nodes
 * 3. Dynamically change PHY parameters (TxPower, CcaEdThreshold, RxSensitivity)
 * 4. Dynamically change channel settings
 *
 * The configuration changes are scheduled at different simulation times to
 * demonstrate the trace-driven configuration system.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/spectrum-analyzer-helper.h"
#include "ns3/lever-api-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LeverApiExample");

// Callback to demonstrate when PHY parameters change
void
OnTxPowerChanged(double oldValue, double newValue)
{
    NS_LOG_UNCOND("Time " << Simulator::Now().GetSeconds()
                  << "s: TxPower changed from " << oldValue
                  << " to " << newValue << " dBm");
}

void
OnChannelChanged(uint8_t channelNumber, uint16_t widthMhz, WifiPhyBandType band, uint8_t primary20Index)
{
    std::string bandStr;
    switch (band)
    {
        case BAND_2_4GHZ:
            bandStr = "2.4GHz";
            break;
        case BAND_5GHZ:
            bandStr = "5GHz";
            break;
        case BAND_6GHZ:
            bandStr = "6GHz";
            break;
        default:
            bandStr = "Unknown";
    }

    NS_LOG_UNCOND("Time " << Simulator::Now().GetSeconds()
                  << "s: Channel changed to {" << +channelNumber
                  << ", " << widthMhz << " MHz, " << bandStr
                  << ", P20=" << +primary20Index << "}");
}

// Function to change TxPower at runtime
void
ChangeTxPower(Ptr<LeverConfig> config, double newPower)
{
    NS_LOG_UNCOND("Time " << Simulator::Now().GetSeconds()
                  << "s: Changing TxPower to " << newPower << " dBm");
    config->SetTxPower(newPower);
}

// Function to change CCA threshold at runtime
void
ChangeCcaThreshold(Ptr<LeverConfig> config, double newThreshold)
{
    NS_LOG_UNCOND("Time " << Simulator::Now().GetSeconds()
                  << "s: Changing CcaEdThreshold to " << newThreshold << " dBm");
    config->SetCcaEdThreshold(newThreshold);
}

// Function to change channel settings at runtime
void
ChangeChannel(Ptr<LeverConfig> config, uint8_t channel, uint16_t width, WifiPhyBandType band)
{
    NS_LOG_UNCOND("Time " << Simulator::Now().GetSeconds()
                  << "s: Changing channel settings to {" << +channel
                  << ", " << width << " MHz (using SwitchChannel)}");
    // Note: width and band parameters are ignored - SwitchChannel auto-determines them
    config->SwitchChannel(channel);
}

int
main(int argc, char* argv[])
{
    // Enable logging
    LogComponentEnable("LeverApiExample", LOG_LEVEL_INFO);
    LogComponentEnable("LeverApi", LOG_LEVEL_INFO);
    LogComponentEnable("LeverApiHelper", LOG_LEVEL_INFO);

    // Simulation parameters
    uint32_t nWifiNodes = 2;
    double simulationTime = 10.0; // seconds

    // Parse command line arguments
    CommandLine cmd(__FILE__);
    cmd.AddValue("nWifiNodes", "Number of WiFi nodes", nWifiNodes);
    cmd.AddValue("simulationTime", "Total simulation time (s)", simulationTime);
    cmd.Parse(argc, argv);

    NS_LOG_INFO("Creating " << nWifiNodes << " WiFi nodes");

    // Create nodes
    NodeContainer wifiNodes;
    wifiNodes.Create(nWifiNodes);

    // Create spectrum analyzer node
    NodeContainer spectrumAnalyzerNode;
    spectrumAnalyzerNode.Create(1);

    // Create spectrum channel for WiFi (needed for spectrum analyzer)
    SpectrumChannelHelper channelHelper = SpectrumChannelHelper::Default();
    channelHelper.SetChannel("ns3::MultiModelSpectrumChannel");
    Ptr<SpectrumChannel> channel = channelHelper.Create();

    // Configure WiFi PHY using Spectrum
    SpectrumWifiPhyHelper phy;
    phy.SetChannel(channel);
    phy.Set("TxPowerStart", DoubleValue(16.0));
    phy.Set("TxPowerEnd", DoubleValue(16.0));
    phy.Set("ChannelSettings", StringValue("{6, 20, BAND_2_4GHZ, 0}"));  // Channel 6 in 2.4 GHz

    // Configure WiFi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);  // 802.11n for 2.4 GHz
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("HtMcs7"),
                                 "ControlMode", StringValue("HtMcs0"));

    // Configure MAC - Create AP and STA
    WifiMacHelper mac;
    Ssid ssid = Ssid("lever-api-network");

    // Install AP on node 0
    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid),
                "EnableBeaconJitter", BooleanValue(false));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, wifiNodes.Get(0));

    // Install STA on node 1
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevice = wifi.Install(phy, mac, wifiNodes.Get(1));

    // Combine into one container
    NetDeviceContainer wifiDevices;
    wifiDevices.Add(apDevice);
    wifiDevices.Add(staDevice);

    // Configure mobility (simple positions)
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                 "MinX", DoubleValue(0.0),
                                 "MinY", DoubleValue(0.0),
                                 "DeltaX", DoubleValue(5.0),
                                 "DeltaY", DoubleValue(0.0),
                                 "GridWidth", UintegerValue(nWifiNodes),
                                 "LayoutType", StringValue("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiNodes);

    // Place spectrum analyzer at center
    MobilityHelper spectrumMobility;
    spectrumMobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                         "MinX", DoubleValue(2.5),
                                         "MinY", DoubleValue(0.0),
                                         "DeltaX", DoubleValue(0.0),
                                         "DeltaY", DoubleValue(0.0),
                                         "GridWidth", UintegerValue(1),
                                         "LayoutType", StringValue("RowFirst"));
    spectrumMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    spectrumMobility.Install(spectrumAnalyzerNode);

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(wifiNodes);

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(wifiDevices);

    // ========================================================================
    // LeverApi Configuration - This is the key part!
    // ========================================================================

    // Create separate LeverConfig objects for AP and STA
    Ptr<LeverConfig> apConfig = CreateObject<LeverConfig>();
    Ptr<LeverConfig> staConfig = CreateObject<LeverConfig>();

    // Set initial values for AP (node 0)
    apConfig->SetTxPower(16.0);  // 16 dBm
    apConfig->SetCcaEdThreshold(-82.0);  // -82 dBm
    apConfig->SetRxSensitivity(-93.0);   // -93 dBm
    apConfig->SwitchChannel(6);  // Channel 6, auto-width (20 MHz for 2.4 GHz)

    // Set initial values for STA (node 1)
    staConfig->SetTxPower(16.0);  // 16 dBm
    staConfig->SetCcaEdThreshold(-82.0);  // -82 dBm
    staConfig->SetRxSensitivity(-93.0);   // -93 dBm
    staConfig->SwitchChannel(6);  // Channel 6, auto-width (20 MHz for 2.4 GHz)

    // Connect to traces to see when configuration changes
    apConfig->TraceConnectWithoutContext("TxPowerStart", MakeCallback(&OnTxPowerChanged));
    apConfig->TraceConnectWithoutContext("ChannelSettings", MakeCallback(&OnChannelChanged));
    staConfig->TraceConnectWithoutContext("TxPowerStart", MakeCallback(&OnTxPowerChanged));
    staConfig->TraceConnectWithoutContext("ChannelSettings", MakeCallback(&OnChannelChanged));

    // Create LeverApi helper and install on AP
    LeverApiHelper apLeverHelper(apConfig);
    ApplicationContainer apLeverApp = apLeverHelper.Install(wifiNodes.Get(0));
    apLeverApp.Start(Seconds(0.0));
    apLeverApp.Stop(Seconds(simulationTime));

    // Create LeverApi helper and install on STA
    LeverApiHelper staLeverHelper(staConfig);
    ApplicationContainer staLeverApp = staLeverHelper.Install(wifiNodes.Get(1));
    staLeverApp.Start(Seconds(0.0));
    staLeverApp.Stop(Seconds(simulationTime));

    // ========================================================================
    // Setup Spectrum Analyzer to monitor the WiFi spectrum
    // ========================================================================

    // Create frequency model for 2.4 GHz WiFi (channels 1-14, ~2.4-2.5 GHz)
    double startFreqGHz = 2.4;   // Start frequency (GHz)
    double endFreqGHz = 2.5;     // End frequency (GHz)
    double freqResolutionMHz = 0.1; // 100 kHz resolution

    std::vector<double> freqs;
    double currentFreqGHz = startFreqGHz;
    while (currentFreqGHz <= endFreqGHz) {
        freqs.push_back(currentFreqGHz * 1e9); // Convert to Hz
        currentFreqGHz += freqResolutionMHz / 1000.0;
    }
    Ptr<SpectrumModel> spectrumModel = Create<SpectrumModel>(freqs);

    // Configure and install spectrum analyzer
    SpectrumAnalyzerHelper spectrumAnalyzerHelper;
    spectrumAnalyzerHelper.SetChannel(channel);
    spectrumAnalyzerHelper.SetRxSpectrumModel(spectrumModel);
    spectrumAnalyzerHelper.SetPhyAttribute("Resolution", TimeValue(MilliSeconds(10))); // 10ms resolution
    spectrumAnalyzerHelper.SetPhyAttribute("NoisePowerSpectralDensity", DoubleValue(1e-15)); // Low noise floor
    spectrumAnalyzerHelper.EnableAsciiAll("lever-api-spectrum");

    NetDeviceContainer spectrumAnalyzerDevices = spectrumAnalyzerHelper.Install(spectrumAnalyzerNode);

    NS_LOG_INFO("Spectrum analyzer configured: " << freqs.size()
                << " frequency bins from " << startFreqGHz << " to " << endFreqGHz << " GHz");
    NS_LOG_INFO("Spectrum data will be saved to: lever-api-spectrum-2-0.tr");

    // ========================================================================
    // Add UDP traffic to generate spectrum activity
    // ========================================================================

    uint16_t port = 9;

    // Install UDP echo server on node 1
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApps = echoServer.Install(wifiNodes.Get(1));
    serverApps.Start(Seconds(0.5));
    serverApps.Stop(Seconds(simulationTime));

    // Install UDP echo client on node 0
    UdpEchoClientHelper echoClient(interfaces.GetAddress(1), port);
    echoClient.SetAttribute("MaxPackets", UintegerValue(1000000));
    echoClient.SetAttribute("Interval", TimeValue(MilliSeconds(50))); // Send every 50ms
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApps = echoClient.Install(wifiNodes.Get(0));
    clientApps.Start(Seconds(0.5));
    clientApps.Stop(Seconds(simulationTime));

    // ========================================================================
    // Schedule dynamic configuration changes during simulation
    // ========================================================================

    NS_LOG_UNCOND("=== Channel Hopping Schedule ===");
    NS_LOG_UNCOND("t=0s: Both AP and STA on Channel 6 (2.437 GHz) - CONNECTED");
    NS_LOG_UNCOND("t=2s: STA moves to Channel 1 (2.412 GHz) - DISCONNECTED");
    NS_LOG_UNCOND("t=5s: AP moves to Channel 1 (2.412 GHz) - RECONNECTING");
    NS_LOG_UNCOND("t=8s: Both move to Channel 11 (2.462 GHz) - CONNECTED");

    // At t=2s, move STA to channel 1 (AP stays on channel 6 - they disconnect)
    Simulator::Schedule(Seconds(2.0), &ChangeChannel, staConfig, 1, 20, BAND_2_4GHZ);

    // At t=5s, move AP to channel 1 (both on channel 1 - they can reconnect)
    Simulator::Schedule(Seconds(5.0), &ChangeChannel, apConfig, 1, 20, BAND_2_4GHZ);

    // At t=8s, move both to channel 11
    Simulator::Schedule(Seconds(7.0), &ChangeChannel, apConfig, 11, 20, BAND_2_4GHZ);
    Simulator::Schedule(Seconds(7.0), &ChangeChannel, staConfig, 11, 20, BAND_2_4GHZ);

    Simulator::Schedule(Seconds(9.0), &ChangeChannel, staConfig, 11, 40, BAND_2_4GHZ);

    // ========================================================================
    // Run simulation
    // ========================================================================

    NS_LOG_INFO("Starting simulation for " << simulationTime << " seconds");
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    NS_LOG_INFO("Simulation complete!");

    return 0;
}
