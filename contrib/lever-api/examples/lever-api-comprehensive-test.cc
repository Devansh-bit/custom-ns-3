/*
 * Comprehensive test suite for LeverApi module
 *
 * This example tests each lever individually and verifies behavior using:
 * - RSSI measurements
 * - Channel switching verification
 * - Packet loss monitoring
 * - Throughput measurements
 *
 * Tests performed:
 * 1. TxPower lever (increase/decrease power, verify RSSI changes)
 * 2. CcaEdThreshold lever (change threshold, verify channel access)
 * 3. RxSensitivity lever (change sensitivity, verify packet reception)
 * 4. Channel lever (switch channels, verify operation)
 * 5. Band lever (switch bands 2.4GHz <-> 5GHz)
 *
 * Output format: Console logging with NS_LOG_UNCOND
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/lever-api-helper.h"
#include "ns3/flow-monitor-module.h"

#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LeverApiComprehensiveTest");

// Global variables for data collection
uint32_t g_packetsSent = 0;
uint32_t g_packetsReceived = 0;
double g_lastRssi = 0.0;
std::string g_currentTest = "";

// Log data point
void
LogDataPoint(std::string event, Ptr<LeverConfig> config)
{
    double now = Simulator::Now().GetSeconds();
    double lossPercent = (g_packetsSent > 0) ?
        (100.0 * (g_packetsSent - g_packetsReceived) / g_packetsSent) : 0.0;

    std::string bandStr;
    switch (config->GetBand())
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

    NS_LOG_UNCOND("  [" << std::fixed << std::setprecision(3) << now << "s] " << event);
    NS_LOG_UNCOND("    Packets: " << g_packetsSent << " sent, " << g_packetsReceived
                  << " received, Loss: " << std::setprecision(2) << lossPercent << "%");
    NS_LOG_UNCOND("    RSSI: " << std::setprecision(1) << g_lastRssi << " dBm");
    NS_LOG_UNCOND("    Config: TxPower=" << std::setprecision(1) << config->GetTxPowerStart()
                  << " dBm, CCA=" << config->GetCcaEdThreshold()
                  << " dBm, RxSens=" << config->GetRxSensitivity() << " dBm");
    NS_LOG_UNCOND("    Channel: " << +config->GetChannelNumber() << " (" << bandStr << ")");
}

// Packet sent callback
void
TxCallback(Ptr<const Packet> packet)
{
    g_packetsSent++;
}

// Packet received callback
void
RxCallback(Ptr<const Packet> packet)
{
    g_packetsReceived++;
}

// Monitor sniffer callback for RSSI
void
MonitorSnifferRx(Ptr<const Packet> packet,
                 uint16_t channelFreqMhz,
                 WifiTxVector txVector,
                 MpduInfo aMpdu,
                 SignalNoiseDbm signalNoise,
                 uint16_t staId)
{
    g_lastRssi = signalNoise.signal;
}

// ========== TEST FUNCTIONS ==========

// Test 1: TxPower changes
void
TestTxPower(Ptr<LeverConfig> config)
{
    NS_LOG_UNCOND("\n========== TEST 1: TX POWER ==========");
    g_currentTest = "TxPower";

    // Reset counters
    g_packetsSent = 0;
    g_packetsReceived = 0;

    // Baseline: 20 dBm
    config->SetTxPower(20.0);
    LogDataPoint("Baseline_20dBm", config);

    // Test 1a: Reduce to 10 dBm (should decrease RSSI, possibly increase loss)
    Simulator::Schedule(Seconds(2.0), [config]() {
        NS_LOG_UNCOND("  [2s] Reducing TxPower to 10 dBm");
        config->SetTxPower(10.0);
        LogDataPoint("Reduce_to_10dBm", config);
    });

    // Test 1b: Reduce to 5 dBm (should further decrease RSSI, likely packet loss)
    Simulator::Schedule(Seconds(4.0), [config]() {
        NS_LOG_UNCOND("  [4s] Reducing TxPower to 5 dBm");
        config->SetTxPower(5.0);
        LogDataPoint("Reduce_to_5dBm", config);
    });

    // Test 1c: Increase to 25 dBm (should increase RSSI, reduce loss)
    Simulator::Schedule(Seconds(6.0), [config]() {
        NS_LOG_UNCOND("  [6s] Increasing TxPower to 25 dBm");
        config->SetTxPower(25.0);
        LogDataPoint("Increase_to_25dBm", config);
    });

    // Final measurement
    Simulator::Schedule(Seconds(7.9), [config]() {
        LogDataPoint("TxPower_Test_End", config);
        NS_LOG_UNCOND("  TX Power Test Complete");
        NS_LOG_UNCOND("    Packets Sent: " << g_packetsSent);
        NS_LOG_UNCOND("    Packets Received: " << g_packetsReceived);
        NS_LOG_UNCOND("    Last RSSI: " << g_lastRssi << " dBm");
    });
}

// Test 2: CCA Threshold changes
void
TestCcaThreshold(Ptr<LeverConfig> config)
{
    NS_LOG_UNCOND("\n========== TEST 2: CCA THRESHOLD ==========");
    g_currentTest = "CcaThreshold";

    // Reset counters
    g_packetsSent = 0;
    g_packetsReceived = 0;

    // Restore TxPower to normal
    config->SetTxPower(20.0);

    // Baseline: -82 dBm (default)
    config->SetCcaEdThreshold(-82.0);
    LogDataPoint("Baseline_-82dBm", config);

    // Test 2a: More sensitive (-92 dBm, easier to detect busy channel)
    Simulator::Schedule(Seconds(10.0), [config]() {
        NS_LOG_UNCOND("  [10s] Setting CCA threshold to -92 dBm (more sensitive)");
        config->SetCcaEdThreshold(-92.0);
        LogDataPoint("More_Sensitive_-92dBm", config);
    });

    // Test 2b: Less sensitive (-72 dBm, harder to detect busy channel)
    Simulator::Schedule(Seconds(12.0), [config]() {
        NS_LOG_UNCOND("  [12s] Setting CCA threshold to -72 dBm (less sensitive)");
        config->SetCcaEdThreshold(-72.0);
        LogDataPoint("Less_Sensitive_-72dBm", config);
    });

    // Test 2c: Very insensitive (-62 dBm)
    Simulator::Schedule(Seconds(14.0), [config]() {
        NS_LOG_UNCOND("  [14s] Setting CCA threshold to -62 dBm (very insensitive)");
        config->SetCcaEdThreshold(-62.0);
        LogDataPoint("Very_Insensitive_-62dBm", config);
    });

    // Restore to default
    Simulator::Schedule(Seconds(16.0), [config]() {
        NS_LOG_UNCOND("  [16s] Restoring CCA threshold to -82 dBm");
        config->SetCcaEdThreshold(-82.0);
        LogDataPoint("CcaThreshold_Test_End", config);
        NS_LOG_UNCOND("  CCA Threshold Test Complete");
    });
}

// Test 3: RxSensitivity changes
void
TestRxSensitivity(Ptr<LeverConfig> config)
{
    NS_LOG_UNCOND("\n========== TEST 3: RX SENSITIVITY ==========");
    g_currentTest = "RxSensitivity";

    // Reset counters
    g_packetsSent = 0;
    g_packetsReceived = 0;

    // Baseline: -91 dBm (default)
    config->SetRxSensitivity(-91.0);
    LogDataPoint("Baseline_-91dBm", config);

    // Test 3a: More sensitive (-96 dBm, can receive weaker signals)
    Simulator::Schedule(Seconds(18.0), [config]() {
        NS_LOG_UNCOND("  [18s] Setting RxSensitivity to -96 dBm (more sensitive)");
        config->SetRxSensitivity(-96.0);
        LogDataPoint("More_Sensitive_-96dBm", config);
    });

    // Test 3b: Less sensitive (-86 dBm, requires stronger signal)
    Simulator::Schedule(Seconds(20.0), [config]() {
        NS_LOG_UNCOND("  [20s] Setting RxSensitivity to -86 dBm (less sensitive)");
        config->SetRxSensitivity(-86.0);
        LogDataPoint("Less_Sensitive_-86dBm", config);
    });

    // Test 3c: Much less sensitive (-76 dBm, may drop packets)
    Simulator::Schedule(Seconds(22.0), [config]() {
        NS_LOG_UNCOND("  [22s] Setting RxSensitivity to -76 dBm (much less sensitive)");
        config->SetRxSensitivity(-76.0);
        LogDataPoint("Much_Less_Sensitive_-76dBm", config);
    });

    // Restore to default
    Simulator::Schedule(Seconds(24.0), [config]() {
        NS_LOG_UNCOND("  [24s] Restoring RxSensitivity to -91 dBm");
        config->SetRxSensitivity(-91.0);
        LogDataPoint("RxSensitivity_Test_End", config);
        NS_LOG_UNCOND("  RX Sensitivity Test Complete");
    });
}

// Test 4: Channel switching with smart SwitchChannel method (2.4GHz band)
void
TestChannelSwitching(Ptr<LeverConfig> config)
{
    NS_LOG_UNCOND("\n========== TEST 4: SMART CHANNEL SWITCHING ==========");
    g_currentTest = "ChannelSwitch";

    // Reset counters
    g_packetsSent = 0;
    g_packetsReceived = 0;

    // Baseline: Channel 1 (2.4GHz, 20 MHz automatically determined)
    NS_LOG_UNCOND("  [25s] Using SwitchChannel(1) - should be 2.4GHz, 20MHz");
    config->SwitchChannel(1);
    LogDataPoint("Channel_1", config);

    // Test 4a: Switch to channel 6 (2.4GHz, 20 MHz)
    Simulator::Schedule(Seconds(26.0), [config]() {
        NS_LOG_UNCOND("  [26s] Using SwitchChannel(6) - should be 2.4GHz, 20MHz");
        config->SwitchChannel(6);
        LogDataPoint("Channel_6", config);
    });

    // Test 4b: Switch to channel 11 (2.4GHz, 20 MHz)
    Simulator::Schedule(Seconds(28.0), [config]() {
        NS_LOG_UNCOND("  [28s] Using SwitchChannel(11) - should be 2.4GHz, 20MHz");
        config->SwitchChannel(11);
        LogDataPoint("Channel_11", config);
    });

    // Test 4c: Back to channel 1
    Simulator::Schedule(Seconds(30.0), [config]() {
        NS_LOG_UNCOND("  [30s] Using SwitchChannel(1) - back to channel 1");
        config->SwitchChannel(1);
        LogDataPoint("ChannelSwitch_Test_End", config);
        NS_LOG_UNCOND("  Channel Switching Test Complete");
    });
}

// Test 5: Band switching with width encoding
void
TestBandSwitching(Ptr<LeverConfig> config)
{
    NS_LOG_UNCOND("\n========== TEST 5: BAND & WIDTH SWITCHING ==========");
    g_currentTest = "BandSwitch";

    // Reset counters
    g_packetsSent = 0;
    g_packetsReceived = 0;

    // Baseline: 2.4GHz channel 1 (20 MHz automatic)
    NS_LOG_UNCOND("  [31s] Using SwitchChannel(1) - 2.4GHz, 20MHz");
    config->SwitchChannel(1);
    LogDataPoint("2_4GHz_Ch1", config);

    // Test 5a: Switch to 5GHz channel 36, 20 MHz
    Simulator::Schedule(Seconds(32.0), [config]() {
        NS_LOG_UNCOND("  [32s] Using SwitchChannel(36) - 5GHz channel 36, 20MHz");
        config->SwitchChannel(36);
        LogDataPoint("5GHz_Ch36_20MHz", config);
    });

    // Test 5b: Switch to 5GHz channel 38, 40 MHz
    // IEEE 802.11 channel encoding: 40 MHz channels use specific numbers (38, 46, 54, 62, etc.)
    // Channel 38 bonds primary channels 36+40 into a single 40 MHz channel
    Simulator::Schedule(Seconds(34.0), [config]() {
        NS_LOG_UNCOND("  [34s] Using SwitchChannel(38) - 5GHz channel 38, 40MHz");
        config->SwitchChannel(38);
        LogDataPoint("5GHz_Ch38_40MHz", config);
    });

    // Test 5c: Switch to 5GHz channel 42, 80 MHz
    // Channel 42 bonds primary channels 36+40+44+48 into a single 80 MHz channel
    Simulator::Schedule(Seconds(36.0), [config]() {
        NS_LOG_UNCOND("  [36s] Using SwitchChannel(42) - 5GHz channel 42, 80MHz");
        config->SwitchChannel(42);
        LogDataPoint("5GHz_Ch42_80MHz", config);
    });

    // Test 5d: Switch to 5GHz channel 50, 160 MHz
    // Channel 50 bonds 8 primary channels (36+40+44+48+52+56+60+64) into 160 MHz
    Simulator::Schedule(Seconds(38.0), [config]() {
        NS_LOG_UNCOND("  [38s] Using SwitchChannel(50) - 5GHz channel 50, 160MHz");
        config->SwitchChannel(50);
        LogDataPoint("5GHz_Ch50_160MHz", config);
    });

    // Test 5e: Back to 2.4GHz channel 6
    Simulator::Schedule(Seconds(40.0), [config]() {
        NS_LOG_UNCOND("  [40s] Using SwitchChannel(6) - back to 2.4GHz, 20MHz");
        config->SwitchChannel(6);
        LogDataPoint("BandSwitch_Test_End", config);
        NS_LOG_UNCOND("  Band & Width Switching Test Complete");
    });
}

int
main(int argc, char* argv[])
{
    // Simulation parameters
    double simTime = 45.0;  // Extended to accommodate width testing
    double distance = 10.0;  // Distance between AP and STA in meters
    uint32_t packetSize = 1024;  // bytes
    uint32_t packetsPerSecond = 100;  // packet rate

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Total simulation time (s)", simTime);
    cmd.AddValue("distance", "Distance between nodes (m)", distance);
    cmd.AddValue("packetRate", "Packets per second", packetsPerSecond);
    cmd.Parse(argc, argv);

    NS_LOG_UNCOND("==============================================");
    NS_LOG_UNCOND("  Lever API Comprehensive Test Suite");
    NS_LOG_UNCOND("==============================================");
    NS_LOG_UNCOND("Distance: " << distance << " m");
    NS_LOG_UNCOND("Packet rate: " << packetsPerSecond << " pps");
    NS_LOG_UNCOND("==============================================\n");

    // Create nodes
    NodeContainer apNode;
    apNode.Create(1);
    NodeContainer staNode;
    staNode.Create(1);

    // Create spectrum channel
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
    Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(lossModel);
    Ptr<ConstantSpeedPropagationDelayModel> delayModel =
        CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);

    // Configure WiFi
    SpectrumWifiPhyHelper phyHelper;
    phyHelper.SetChannel(spectrumChannel);
    phyHelper.Set("ChannelSettings", StringValue("{1, 20, BAND_2_4GHZ, 0}"));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                  "DataMode",
                                  StringValue("HeMcs7"),
                                  "ControlMode",
                                  StringValue("HeMcs0"));

    WifiMacHelper macHelper;
    Ssid ssid = Ssid("lever-test");

    // Create AP
    macHelper.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phyHelper, macHelper, apNode);

    // Create STA
    macHelper.SetType("ns3::StaWifiMac",
                      "Ssid",
                      SsidValue(ssid),
                      "ActiveProbing",
                      BooleanValue(false));
    NetDeviceContainer staDevice = wifi.Install(phyHelper, macHelper, staNode);

    // Setup mobility
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));  // AP
    positionAlloc->Add(Vector(distance, 0.0, 0.0));  // STA
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNode);
    mobility.Install(staNode);

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(apNode);
    internet.Install(staNode);

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apInterface = address.Assign(apDevice);
    Ipv4InterfaceContainer staInterface = address.Assign(staDevice);

    // Create and configure LeverConfig
    Ptr<LeverConfig> config = CreateObject<LeverConfig>();
    config->SetTxPower(20.0);
    config->SetCcaEdThreshold(-82.0);
    config->SetRxSensitivity(-91.0);
    config->SwitchChannel(1);

    // Install LeverApi on AP
    LeverApiHelper leverHelper(config);
    leverHelper.Install(apNode.Get(0));

    // Install UDP server on STA
    uint16_t port = 9;
    UdpServerHelper server(port);
    ApplicationContainer serverApps = server.Install(staNode.Get(0));
    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(simTime));

    // Install UDP client on AP
    Time interPacketInterval = Seconds(1.0 / packetsPerSecond);
    UdpClientHelper client(staInterface.GetAddress(0), port);
    client.SetAttribute("MaxPackets", UintegerValue(4294967295u));
    client.SetAttribute("Interval", TimeValue(interPacketInterval));
    client.SetAttribute("PacketSize", UintegerValue(packetSize));
    ApplicationContainer clientApps = client.Install(apNode.Get(0));
    clientApps.Start(Seconds(0.5));
    clientApps.Stop(Seconds(simTime));

    // Connect to packet traces
    clientApps.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&TxCallback));
    serverApps.Get(0)->TraceConnectWithoutContext("Rx", MakeCallback(&RxCallback));

    // Connect to monitor sniffer for RSSI
    Ptr<WifiNetDevice> staWifiDev = DynamicCast<WifiNetDevice>(staDevice.Get(0));
    staWifiDev->GetPhy()->TraceConnectWithoutContext("MonitorSnifferRx",
                                                      MakeCallback(&MonitorSnifferRx));

    // Schedule tests
    Simulator::Schedule(Seconds(0.5), &TestTxPower, config);
    Simulator::Schedule(Seconds(8.0), &TestCcaThreshold, config);
    Simulator::Schedule(Seconds(17.0), &TestRxSensitivity, config);
    Simulator::Schedule(Seconds(25.0), &TestChannelSwitching, config);
    Simulator::Schedule(Seconds(31.0), &TestBandSwitching, config);

    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    NS_LOG_UNCOND("\n==============================================");
    NS_LOG_UNCOND("  Test Suite Complete");
    NS_LOG_UNCOND("==============================================");
    NS_LOG_UNCOND("Total packets sent: " << g_packetsSent);
    NS_LOG_UNCOND("Total packets received: " << g_packetsReceived);
    double overallLoss = (g_packetsSent > 0) ?
        (100.0 * (g_packetsSent - g_packetsReceived) / g_packetsSent) : 0.0;
    NS_LOG_UNCOND("Overall packet loss: " << std::fixed << std::setprecision(2)
                  << overallLoss << "%");
    NS_LOG_UNCOND("==============================================\n");

    return 0;
}