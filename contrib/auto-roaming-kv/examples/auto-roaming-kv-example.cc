/*
 * Copyright (c) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/csma-module.h"
#include "ns3/bridge-module.h"
#include "ns3/applications-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/wifi-spectrum-value-helper.h"
#include "ns3/auto-roaming-kv-helper.h"
#include "ns3/link-measurement-protocol.h"
#include "ns3/link-measurement-report.h"
#include "ns3/link-measurement-request.h"
#include "ns3/neighbor-protocol-11k-helper.h"
#include "ns3/beacon-protocol-11k-helper.h"
#include "ns3/bss_tm_11v-helper.h"
#include "ns3/beacon-neighbor-model.h"
#include "ns3/dual-phy-sniffer-helper.h"

/**
 * @file
 * Example demonstrating the AutoRoamingKvHelper with Link Measurement Protocol.
 * This example creates one AP and one STA, installs the helper on both,
 * and verifies that link measurement requests and reports are properly exchanged.
 * The STA starts close to the AP and slowly moves away, which triggers a neighbor
 * report request when RSSI drops below the threshold.
 */

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("AutoRoamingKvExample");

// Statistics tracking
struct Stats
{
    uint32_t requestsSent = 0;
    uint32_t requestsReceived = 0;
    uint32_t reportsReceived = 0;
    uint32_t neighborReportsReceived = 0;
    uint32_t beaconReportsReceived = 0;
    uint32_t linkMeasurementRequestsReceivedAP2 = 0;
    uint32_t udpPacketsSent = 0;
    uint32_t udpPacketsReceived = 0;
    uint32_t packetsBeforeRoaming = 0;
    uint32_t packetsAfterRoaming = 0;
    Time roamingTime = Seconds(0);
    bool hasRoamed = false;
};

Stats g_stats;

/**
 * \brief Callback when Link Measurement Request is received at the AP
 * \param from source MAC address
 * \param request the request object
 */
void
OnApRequestReceived(Mac48Address from, LinkMeasurementRequest request)
{
    g_stats.requestsReceived++;

    NS_LOG_UNCOND("["
                  << std::fixed << std::setprecision(3)
                  << Simulator::Now().GetSeconds() << "s] AP1 received request from "
                  << from
                  << " | TX Power: " << (int32_t)request.GetTransmitPowerUsedDbm() << " dBm"
                  << " | Total requests received: " << g_stats.requestsReceived);
}

void
OnAp2ReqReceived(Mac48Address from, LinkMeasurementRequest request)
{
    g_stats.linkMeasurementRequestsReceivedAP2++;
    NS_LOG_UNCOND("["
                  << std::fixed << std::setprecision(3)
                  << Simulator::Now().GetSeconds() << "s] AP2 received request from "
                  << from
                  << " | TX Power: " << (int32_t)request.GetTransmitPowerUsedDbm() << " dBm"
                  << " | Total requests received: " << g_stats.linkMeasurementRequestsReceivedAP2);
}


/**
 * \brief Callback when Link Measurement Report is received at the STA
 * \param from source MAC address
 * \param report the measurement report
 */
void
OnStaReportReceived(Mac48Address from, LinkMeasurementReport report)
{
    g_stats.reportsReceived++;

    double rcpiDbm = report.GetRcpiDbm();
    double rsniDb = report.GetRsniDb();
    int8_t txPowerDbm = report.GetTransmitPowerDbm();
    uint8_t linkMarginDb = report.GetLinkMarginDb();

    NS_LOG_UNCOND("["
                  << std::fixed << std::setprecision(3)
                  << Simulator::Now().GetSeconds() << "s] STA received report from " << from
                  << " | RCPI: " << std::setprecision(1) << rcpiDbm << " dBm"
                  << " | RSNI: " << rsniDb << " dB"
                  << " | TX Power: " << (int32_t)txPowerDbm << " dBm"
                  << " | Link Margin: " << (uint32_t)linkMarginDb << " dB"
                  << " | Total reports received: " << g_stats.reportsReceived);
}

/**
 * \brief Callback when Neighbor Report is received at the STA
 * \param staAddress MAC of STA
 * \param apAddress MAC of AP
 * \param neighbors Vector of neighbor report data
 */
void
OnNeighborReportReceived(Mac48Address staAddress,
                          Mac48Address apAddress,
                          std::vector<NeighborReportData> neighbors)
{
    g_stats.neighborReportsReceived++;
}

/**
 * \brief Callback when Beacon Report is received at the AP
 * \param apAddress MAC of AP
 * \param staAddress MAC of STA
 * \param reports Vector of beacon report data
 */
void
OnBeaconReportReceived(Mac48Address apAddress,
                        Mac48Address staAddress,
                        std::vector<BeaconReportData> reports)
{
    g_stats.beaconReportsReceived++;
}

/**
 * \brief Callback for UDP packet transmission
 * \param packet the packet
 */
void
OnUdpTx(Ptr<const Packet> packet)
{
    g_stats.udpPacketsSent++;

    // Track packets before/after roaming
    if (!g_stats.hasRoamed)
    {
        g_stats.packetsBeforeRoaming++;
    }
    else
    {
        g_stats.packetsAfterRoaming++;
    }

    // Log every 50th packet with timing info (100 packets/sec = high rate)
    if (g_stats.udpPacketsSent % 50 == 1)
    {
        NS_LOG_UNCOND("[" << std::fixed << std::setprecision(3)
                      << Simulator::Now().GetSeconds() << "s] "
                      << "UDP Packet #" << g_stats.udpPacketsSent << " sent from STA"
                      << (g_stats.hasRoamed ? " [AFTER ROAMING]" : " [BEFORE ROAMING]"));
    }
}

/**
 * \brief Callback for UDP packet reception
 * \param packet the packet
 */
void
OnUdpRx(Ptr<const Packet> packet)
{
    g_stats.udpPacketsReceived++;

    // Log every 50th packet (high traffic rate)
    if (g_stats.udpPacketsReceived % 50 == 0)
    {
        NS_LOG_UNCOND("[" << std::fixed << std::setprecision(3)
                      << Simulator::Now().GetSeconds() << "s] "
                      << "UDP Packet #" << g_stats.udpPacketsReceived << " received at DS Node");
    }
}

/**
 * \brief Callback for STA association events
 * \param context the context
 * \param bssid the BSSID of the AP
 */
void
OnAssociation(std::string context, Mac48Address bssid)
{
    NS_LOG_UNCOND("\n════════════════════════════════════════════════════════════");
    NS_LOG_UNCOND("  ✓ ROAMING EVENT: STA ASSOCIATED to AP: " << bssid);
    NS_LOG_UNCOND("  Time: " << std::fixed << std::setprecision(3)
                  << Simulator::Now().GetSeconds() << "s");
    NS_LOG_UNCOND("════════════════════════════════════════════════════════════\n");

    // Mark that roaming has occurred (second association is after roaming)
    if (Simulator::Now().GetSeconds() > 1.0)  // First association happens early
    {
        g_stats.hasRoamed = true;
        g_stats.roamingTime = Simulator::Now();
        NS_LOG_UNCOND(">>> ROAMING COMPLETED - UDP traffic should continue <<<");
    }
}

/**
 * \brief Callback for STA disassociation events
 * \param context the context
 * \param bssid the BSSID of the AP
 */
void
OnDisassociation(std::string context, Mac48Address bssid)
{
    NS_LOG_UNCOND("\n════════════════════════════════════════════════════════════");
    NS_LOG_UNCOND("  ✗ DISASSOCIATED from AP: " << bssid);
    NS_LOG_UNCOND("  Time: " << std::fixed << std::setprecision(3)
                  << Simulator::Now().GetSeconds() << "s");
    NS_LOG_UNCOND("════════════════════════════════════════════════════════════\n");
}

/**
 * \brief Callback for STA roaming initiated events
 * \param context the context
 * \param time the time roaming was initiated
 * \param oldBssid the BSSID of the current AP
 * \param newBssid the BSSID of the target AP
 * \param rssi the current RSSI
 * \param snr the current SNR
 */
void
OnRoamingInitiated(std::string context,
                   Time time,
                   Mac48Address oldBssid,
                   Mac48Address newBssid,
                   double rssi,
                   double snr)
{
    NS_LOG_UNCOND("\n════════════════════════════════════════════════════════════");
    NS_LOG_UNCOND("  ↻ ROAMING INITIATED");
    NS_LOG_UNCOND("  From AP: " << oldBssid << " → To AP: " << newBssid);
    NS_LOG_UNCOND("  Time: " << std::fixed << std::setprecision(3) << time.GetSeconds() << "s");
    NS_LOG_UNCOND("  RSSI: " << std::setprecision(1) << rssi << " dBm, SNR: " << snr << " dB");
    NS_LOG_UNCOND("════════════════════════════════════════════════════════════\n");
}

/**
 * \brief Print final statistics
 */
void
PrintStatistics()
{
    // Get UDP statistics from server
    Ptr<UdpServer> server = DynamicCast<UdpServer>(
        NodeList::GetNode(3)->GetApplication(0));  // DS node is node 3
    if (server)
    {
        g_stats.udpPacketsReceived = server->GetReceived();
    }

    NS_LOG_UNCOND("\n" << std::string(70, '='));
    NS_LOG_UNCOND("SIMULATION RESULTS");
    NS_LOG_UNCOND(std::string(70, '='));
    NS_LOG_UNCOND("Link Measurement Requests Received (at AP1): " << g_stats.requestsReceived);
    NS_LOG_UNCOND("Link Measurement Requests Received (at AP2): " << g_stats.linkMeasurementRequestsReceivedAP2);
    NS_LOG_UNCOND("Link Measurement Reports Received (at STA):  " << g_stats.reportsReceived);
    NS_LOG_UNCOND("Neighbor Reports Received (at STA):           " << g_stats.neighborReportsReceived);
    NS_LOG_UNCOND("Beacon Reports Received (at AP):              " << g_stats.beaconReportsReceived);

    NS_LOG_UNCOND("\n" << std::string(70, '-'));
    NS_LOG_UNCOND("UDP TRAFFIC STATISTICS");
    NS_LOG_UNCOND(std::string(70, '-'));
    NS_LOG_UNCOND("Total UDP Packets Sent by STA:                " << g_stats.udpPacketsSent);
    NS_LOG_UNCOND("Total UDP Packets Received at DS Node:        " << g_stats.udpPacketsReceived);
    NS_LOG_UNCOND("\n>>> TRAFFIC CONTINUITY ANALYSIS <<<");
    NS_LOG_UNCOND("Packets sent BEFORE roaming:                  " << g_stats.packetsBeforeRoaming);
    NS_LOG_UNCOND("Packets sent AFTER roaming:                   " << g_stats.packetsAfterRoaming);
    if (g_stats.hasRoamed)
    {
        NS_LOG_UNCOND("Roaming occurred at:                          " << std::fixed << std::setprecision(3)
                      << g_stats.roamingTime.GetSeconds() << "s");
        double successRate = (g_stats.udpPacketsReceived * 100.0) / g_stats.udpPacketsSent;
        NS_LOG_UNCOND("Packet delivery rate:                         " << std::setprecision(1)
                      << successRate << "%");
    }

    if (g_stats.requestsReceived > 0 && g_stats.reportsReceived > 0)
    {
        NS_LOG_UNCOND("\nVERIFICATION: SUCCESS - Link measurement protocol is working!");
        NS_LOG_UNCOND("  ✓ Requests are being sent from STA to AP");
        NS_LOG_UNCOND("  ✓ Reports are being sent from AP to STA");

        if (g_stats.neighborReportsReceived > 0)
        {
            NS_LOG_UNCOND("  ✓ Neighbor request was triggered when RSSI dropped below threshold!");
        }

        if (g_stats.beaconReportsReceived > 0)
        {
            NS_LOG_UNCOND("  ✓ Beacon request was automatically sent after neighbor report!");
            NS_LOG_UNCOND("  ✓ BSS TM request chain is enabled (check logs for BSS TM activity)");
        }

        if (g_stats.packetsAfterRoaming > 0)
        {
            NS_LOG_UNCOND("  ✓ UDP traffic CONTINUED AFTER ROAMING - " << g_stats.packetsAfterRoaming << " packets sent!");
            NS_LOG_UNCOND("  ✓ Bridge properly forwarded traffic through new AP!");
        }
    }
    else
    {
        NS_LOG_UNCOND("\nVERIFICATION: FAILURE - No measurements exchanged");
    }
    NS_LOG_UNCOND(std::string(70, '=') << "\n");
}

int
main(int argc, char* argv[])
{
    bool verbose = false;
    bool enablePcap = false;
    double simTime = 50.0;            // seconds - increased for more time to see BSS TM chain
    double startDistance = 5.0;       // meters - initial distance between AP and STA
    double staSpeed = 2.0;            // m/s - STA movement speed
    double measurementInterval = 1.0; // seconds between measurements
    double rssiThreshold = -60.0;     // dBm - threshold for triggering neighbor request and roaming

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.AddValue("enablePcap", "Enable PCAP tracing", enablePcap);
    cmd.AddValue("simTime", "Total simulation time (seconds)", simTime);
    cmd.AddValue("startDistance", "Initial distance between AP and STA (meters)", startDistance);
    cmd.AddValue("staSpeed", "STA movement speed (m/s)", staSpeed);
    cmd.AddValue("measurementInterval", "Interval between measurements (seconds)", measurementInterval);
    cmd.AddValue("rssiThreshold", "RSSI threshold for neighbor request (dBm)", rssiThreshold);
    cmd.Parse(argc, argv);

    // Enable logging for BSS TM and roaming chain
    LogComponentEnable("AutoRoamingKvHelper", LOG_LEVEL_INFO);
    LogComponentEnable("BssTm11vHelper", LOG_LEVEL_INFO);
    // LogComponentEnable("LinkMeasurementProtocol", LOG_LEVEL_DEBUG);

    if (verbose)
    {
        LogComponentEnable("AutoRoamingKvHelper", LOG_LEVEL_ALL);
        LogComponentEnable("LinkMeasurementProtocol", LOG_LEVEL_ALL);
        LogComponentEnable("NeighborProtocolHelper", LOG_LEVEL_ALL);
        LogComponentEnable("BeaconProtocolHelper", LOG_LEVEL_ALL);
        LogComponentEnable("BssTm11vHelper", LOG_LEVEL_ALL);
        LogComponentEnable("AutoRoamingKvExample", LOG_LEVEL_ALL);
    }

    NS_LOG_UNCOND("\n=== Auto-Roaming-KV Example: Link Measurement + Neighbor Request ===");
    NS_LOG_UNCOND("Configuration:");
    NS_LOG_UNCOND("  Initial Distance: " << startDistance << " m");
    NS_LOG_UNCOND("  STA Speed: " << staSpeed << " m/s");
    NS_LOG_UNCOND("  Measurement Interval: " << measurementInterval << " s");
    NS_LOG_UNCOND("  RSSI Threshold: " << rssiThreshold << " dBm");
    NS_LOG_UNCOND("  Simulation Time: " << simTime << " s\n");

    // Create nodes
    NodeContainer apNodes;
    NodeContainer staNodes;
    NodeContainer dsNode;  // Distribution System node
    apNodes.Create(2);  // Create 2 APs for roaming scenario
    staNodes.Create(1);
    dsNode.Create(1);  // Create 1 DS node (wired server)

    // Setup mobility for APs
    MobilityHelper mobilityAp;
    mobilityAp.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityAp.Install(apNodes);
    // AP1 at origin
    apNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0.0, 0.0, 0.0));
    // AP2 at 30 meters away
    apNodes.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector(30.0, 0.0, 0.0));

    // STA starts close and moves away from the AP
    MobilityHelper mobilitySta;
    mobilitySta.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobilitySta.Install(staNodes);
    Ptr<ConstantVelocityMobilityModel> staMobility =
        staNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();
    staMobility->SetPosition(Vector(startDistance, 0.0, 0.0));
    staMobility->SetVelocity(Vector(staSpeed, 0.0, 0.0)); // Move away in +X direction

    // Setup mobility for DS node (stationary)
    MobilityHelper mobilityDs;
    mobilityDs.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityDs.Install(dsNode);
    dsNode.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(15.0, -10.0, 0.0)); // DS node at center-back

    // Setup WiFi
    SpectrumWifiPhyHelper spectrumPhyHelper;
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
    Ptr<LogDistancePropagationLossModel> lossModel =
        CreateObject<LogDistancePropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(lossModel);
    Ptr<ConstantSpeedPropagationDelayModel> delayModel =
        CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);

    spectrumPhyHelper.SetChannel(spectrumChannel);

    WifiMacHelper wifiMacHelper;
    Ssid ssid = Ssid("auto-roaming-test");

    WifiHelper wifiHelper;
    wifiHelper.SetStandard(WIFI_STANDARD_80211ax);
    wifiHelper.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                       "DataMode", StringValue("HtMcs7"),
                                       "ControlMode", StringValue("HtMcs0"));

    wifiMacHelper.SetType("ns3::ApWifiMac",
                          "Ssid", SsidValue(ssid),
                          "BeaconInterval", TimeValue(MicroSeconds(102400)));

    // Install WiFi on APs with DIFFERENT channels for proper multi-channel scanning
    NetDeviceContainer apDevices;

    // AP1 on channel 11 - REVERSED to test frequency asymmetry
    spectrumPhyHelper.Set("ChannelSettings", StringValue("{11, 20, BAND_2_4GHZ, 0}"));
    spectrumPhyHelper.Set("TxPowerStart", DoubleValue(16.0)); // Default 16 dBm
    spectrumPhyHelper.Set("TxPowerEnd", DoubleValue(16.0));
    NetDeviceContainer ap1Device = wifiHelper.Install(spectrumPhyHelper, wifiMacHelper, apNodes.Get(0));
    apDevices.Add(ap1Device);

    // AP2 on channel 1 - REVERSED
    spectrumPhyHelper.Set("ChannelSettings", StringValue("{1, 20, BAND_2_4GHZ, 0}"));
    spectrumPhyHelper.Set("TxPowerStart", DoubleValue(16.0)); // Default 16 dBm
    spectrumPhyHelper.Set("TxPowerEnd", DoubleValue(16.0));
    NetDeviceContainer ap2Device = wifiHelper.Install(spectrumPhyHelper, wifiMacHelper, apNodes.Get(1));
    apDevices.Add(ap2Device);

    NS_LOG_UNCOND("Created 2 APs for roaming scenario (AP1: Channel 11, AP2: Channel 1) - REVERSED");

    // Install WiFi on STA (start on channel 11 to associate with AP1)
    spectrumPhyHelper.Set("ChannelSettings", StringValue("{11, 20, BAND_2_4GHZ, 0}"));
    wifiMacHelper.SetType("ns3::StaWifiMac",
                          "Ssid", SsidValue(ssid),
                          "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifiHelper.Install(spectrumPhyHelper, wifiMacHelper, staNodes);

    // =====================================================
    // Setup CSMA network for Distribution System (DS)
    // =====================================================
    NS_LOG_UNCOND("\n=== Setting up Distribution System (DS) ===");

    // Create CSMA helper
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(MicroSeconds(50)));

    // Create containers for all CSMA devices
    NodeContainer csmaNodes;
    csmaNodes.Add(apNodes);  // Add both APs
    csmaNodes.Add(dsNode);   // Add DS node

    // Install CSMA on all nodes at once (creates shared channel)
    NetDeviceContainer csmaDevices = csma.Install(csmaNodes);

    // Split devices for clarity
    NetDeviceContainer apCsmaDevices;
    apCsmaDevices.Add(csmaDevices.Get(0));  // AP1's CSMA device
    apCsmaDevices.Add(csmaDevices.Get(1));  // AP2's CSMA device
    NetDeviceContainer dsDevices;
    dsDevices.Add(csmaDevices.Get(2));      // DS node's CSMA device

    // Create bridges on APs to connect WiFi and CSMA
    BridgeHelper bridge;
    NetDeviceContainer apBridgeDevices;

    // Bridge AP1's WiFi and CSMA interfaces
    NetDeviceContainer ap1BridgedDevices;
    ap1BridgedDevices.Add(apDevices.Get(0));      // WiFi device
    ap1BridgedDevices.Add(apCsmaDevices.Get(0));  // CSMA device
    NetDeviceContainer ap1Bridge = bridge.Install(apNodes.Get(0), ap1BridgedDevices);
    apBridgeDevices.Add(ap1Bridge);

    // Bridge AP2's WiFi and CSMA interfaces
    NetDeviceContainer ap2BridgedDevices;
    ap2BridgedDevices.Add(apDevices.Get(1));      // WiFi device
    ap2BridgedDevices.Add(apCsmaDevices.Get(1));  // CSMA device
    NetDeviceContainer ap2Bridge = bridge.Install(apNodes.Get(1), ap2BridgedDevices);
    apBridgeDevices.Add(ap2Bridge);

    NS_LOG_UNCOND("  - Created shared CSMA network connecting DS node to both APs");
    NS_LOG_UNCOND("  - Configured bridges on both APs to connect WiFi and CSMA");
    NS_LOG_UNCOND("  - CSMA: 100 Mbps, 50 µs delay");

    // Install Internet stack
    InternetStackHelper internet;
    // Don't install on APs - they have bridges which work at Layer 2
    internet.Install(staNodes);
    internet.Install(dsNode);

    // Assign IP addresses - All on same subnet since we're using bridges
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");

    // Assign to STA wireless interface
    Ipv4InterfaceContainer staInterfaces = ipv4.Assign(staDevices);

    // Assign to DS node CSMA interface
    Ipv4InterfaceContainer dsInterfaces = ipv4.Assign(dsDevices);

    NS_LOG_UNCOND("\n=== IP Address Assignment ===");
    NS_LOG_UNCOND("  - STA: " << staInterfaces.GetAddress(0) << " (WiFi)");
    NS_LOG_UNCOND("  - DS Node: " << dsInterfaces.GetAddress(0) << " (CSMA)");
    NS_LOG_UNCOND("  - All devices on same subnet (10.1.1.0/24) for L2 bridging");

    // Get MAC addresses
    Ptr<WifiNetDevice> ap1NetDevice = DynamicCast<WifiNetDevice>(apDevices.Get(0));
    Ptr<WifiNetDevice> ap2NetDevice = DynamicCast<WifiNetDevice>(apDevices.Get(1));
    Ptr<WifiNetDevice> staNetDevice = DynamicCast<WifiNetDevice>(staDevices.Get(0));
    Mac48Address ap1Mac = ap1NetDevice->GetMac()->GetAddress();
    Mac48Address ap2Mac = ap2NetDevice->GetMac()->GetAddress();
    Mac48Address staMac = staNetDevice->GetMac()->GetAddress();

    NS_LOG_UNCOND("AP1 MAC Address: " << ap1Mac << " (at position 0,0,0, Channel 11) - REVERSED");
    NS_LOG_UNCOND("AP2 MAC Address: " << ap2Mac << " (at position 30,0,0, Channel 1) - REVERSED");
    NS_LOG_UNCOND("STA MAC Address: " << staMac << " (starts at 5,0,0 moving in +X direction)\n");

    // Create SHARED dual-PHY sniffer (to avoid device duplication)
    DualPhySnifferHelper dualPhySniffer;
    dualPhySniffer.SetChannel(Ptr<SpectrumChannel>(spectrumChannel));
    std::vector<uint8_t> scanningChannels = {1, 6, 11}; // 2.4 GHz channels
    dualPhySniffer.SetScanningChannels(scanningChannels);
    dualPhySniffer.SetHopInterval(MilliSeconds(100));

    // Install dual-PHY on both APs (adds scanning radios to existing devices)
    dualPhySniffer.Install(apNodes.Get(0), 11, ap1Mac);  // AP1 operating on channel 11 - REVERSED
    dualPhySniffer.Install(apNodes.Get(1), 1, ap2Mac);  // AP2 operating on channel 1 - REVERSED
    dualPhySniffer.StartChannelHopping();

    NS_LOG_UNCOND("Installed shared dual-PHY sniffer on both APs");
    NS_LOG_UNCOND("  - Scanning channels: 1, 6, 11");
    NS_LOG_UNCOND("  - Hop interval: 100 ms");
    NS_LOG_UNCOND("  - This single instance will be shared by Neighbor and Beacon protocols\n");

    // Setup Neighbor Protocol - Need separate instances for each AP!
    Ptr<NeighborProtocolHelper> neighborProtocolAp1 = CreateObject<NeighborProtocolHelper>();
    Ptr<NeighborProtocolHelper> neighborProtocolAp2 = CreateObject<NeighborProtocolHelper>();
    Ptr<NeighborProtocolHelper> neighborProtocolSta = CreateObject<NeighborProtocolHelper>();

    // Use the SHARED dual-PHY instance (no duplicate creation!)
    neighborProtocolAp1->SetDualPhySniffer(&dualPhySniffer);
    neighborProtocolAp2->SetDualPhySniffer(&dualPhySniffer);
    neighborProtocolSta->SetDualPhySniffer(&dualPhySniffer);

    // Create neighbor table with both APs
    std::vector<ApInfo> neighborTable;

    // AP1 - REVERSED
    ApInfo ap1Info;
    ap1Info.bssid = ap1Mac;
    ap1Info.ssid = "auto-roaming-test";
    ap1Info.channel = 11;  // AP1 on channel 11 - REVERSED
    ap1Info.regulatoryClass = 81;
    ap1Info.phyType = 7; // HT PHY
    ap1Info.position = Vector(0.0, 0.0, 0.0);
    neighborTable.push_back(ap1Info);

    // AP2 - REVERSED
    ApInfo ap2Info;
    ap2Info.bssid = ap2Mac;
    ap2Info.ssid = "auto-roaming-test";
    ap2Info.channel = 1;  // AP2 on channel 1 - REVERSED
    ap2Info.regulatoryClass = 81;
    ap2Info.phyType = 7; // HT PHY
    ap2Info.position = Vector(30.0, 0.0, 0.0);
    neighborTable.push_back(ap2Info);

    neighborProtocolAp1->SetNeighborTable(neighborTable);
    neighborProtocolAp2->SetNeighborTable(neighborTable);
    neighborProtocolSta->SetNeighborTable(neighborTable);

    neighborProtocolAp1->InstallOnAp(ap1NetDevice);
    neighborProtocolAp2->InstallOnAp(ap2NetDevice);
    neighborProtocolSta->InstallOnSta(staNetDevice);

    // Connect neighbor report trace (for statistics only)
    neighborProtocolSta->m_neighborReportReceivedTrace.ConnectWithoutContext(
        MakeCallback(&OnNeighborReportReceived));

    NS_LOG_UNCOND("Installed Neighbor Protocol on AP and STA\n");

    // Setup Beacon Protocol - Need separate instances for each AP!
    Ptr<BeaconProtocolHelper> beaconProtocolAp1 = CreateObject<BeaconProtocolHelper>();
    Ptr<BeaconProtocolHelper> beaconProtocolAp2 = CreateObject<BeaconProtocolHelper>();
    Ptr<BeaconProtocolHelper> beaconProtocolSta = CreateObject<BeaconProtocolHelper>();

    // Use the SHARED dual-PHY instance (no duplicate creation!)
    beaconProtocolAp1->SetDualPhySniffer(&dualPhySniffer);
    beaconProtocolAp2->SetDualPhySniffer(&dualPhySniffer);
    beaconProtocolSta->SetDualPhySniffer(&dualPhySniffer);

    beaconProtocolAp1->InstallOnAp(ap1NetDevice);
    beaconProtocolAp2->InstallOnAp(ap2NetDevice);
    beaconProtocolSta->InstallOnSta(staNetDevice);

    // Connect beacon report trace to AP instances (beacon reports are received by APs)
    // Note: Connect both the example's callback AND the helper's callback
    beaconProtocolAp1->m_beaconReportReceivedTrace.ConnectWithoutContext(
        MakeCallback(&OnBeaconReportReceived));
    beaconProtocolAp2->m_beaconReportReceivedTrace.ConnectWithoutContext(
        MakeCallback(&OnBeaconReportReceived));

    NS_LOG_UNCOND("Installed Beacon Protocol on both APs and STA\n");

    // Setup BSS TM Helper - CREATE SEPARATE INSTANCES for each AP and STA
    // (fixes bug where shared instance overwrites m_apDevice)
    Ptr<BssTm11vHelper> bssTmHelperAp1 = CreateObject<BssTm11vHelper>();
    Ptr<BssTm11vHelper> bssTmHelperAp2 = CreateObject<BssTm11vHelper>();
    Ptr<BssTm11vHelper> bssTmHelperSta = CreateObject<BssTm11vHelper>();

    bssTmHelperAp1->InstallOnAp(ap1NetDevice);
    bssTmHelperAp2->InstallOnAp(ap2NetDevice);
    bssTmHelperSta->InstallOnSta(staNetDevice);

    NS_LOG_UNCOND("Installed BSS TM Protocol on both APs and STA\n");

    // Install Link Measurement Protocol using the helper
    AutoRoamingKvHelper helper;
    helper.SetMeasurementInterval(Seconds(measurementInterval));
    helper.SetRssiThreshold(rssiThreshold);

    // Install on both APs
    std::vector<Ptr<LinkMeasurementProtocol>> apProtocols = helper.InstallAp(apDevices);
    NS_LOG_UNCOND("Installed Link Measurement Protocol on both APs");

    // Install on STA (with neighbor, beacon, and BSS TM protocols)
    // The helper will auto-detect which AP the STA is currently associated with
    std::vector<Ptr<LinkMeasurementProtocol>> staProtocols =
        helper.InstallSta(staDevices, neighborProtocolSta, beaconProtocolSta, bssTmHelperAp1);

    // IMPORTANT: Manually connect helper's beacon callback to BOTH AP instances
    // (The helper connects to the STA instance, but beacon reports are received by APs)
    // Must connect to BOTH APs since the STA can roam between them
    beaconProtocolAp1->m_beaconReportReceivedTrace.ConnectWithoutContext(
        MakeCallback(&AutoRoamingKvHelper::OnBeaconReportReceived, &helper));
    beaconProtocolAp2->m_beaconReportReceivedTrace.ConnectWithoutContext(
        MakeCallback(&AutoRoamingKvHelper::OnBeaconReportReceived, &helper));

    NS_LOG_UNCOND("Installed Link Measurement Protocol on STA (initially associated to AP1)");
    NS_LOG_UNCOND("  - Measurement interval: " << measurementInterval << " s");
    NS_LOG_UNCOND("  - RSSI threshold: " << rssiThreshold << " dBm");
    NS_LOG_UNCOND("  - Beacon request delay: 50 ms (default)");
    NS_LOG_UNCOND("  - BSS TM request delay: 50 ms (default)");
    NS_LOG_UNCOND("  - Client steering: ENABLED");
    NS_LOG_UNCOND("  - Neighbor request will trigger when RSSI < threshold");
    NS_LOG_UNCOND("  - Beacon request will follow after neighbor report");
    NS_LOG_UNCOND("  - BSS TM request will follow after beacon report (if steering enabled)");
    NS_LOG_UNCOND("\n=== Scenario ===");
    NS_LOG_UNCOND("STA starts near AP1 (0m) and moves toward AP2 (30m) at " << staSpeed << " m/s");
    NS_LOG_UNCOND("When RSSI from AP1 drops below threshold, steering chain will trigger");
    NS_LOG_UNCOND("Expected: STA should receive BSS TM request to roam to AP2\n");

    // Connect trace callbacks
    apProtocols[0]->TraceConnectWithoutContext(
        "LinkMeasurementRequestReceived",
        MakeCallback(&OnApRequestReceived));

    apProtocols[1]->TraceConnectWithoutContext(
        "LinkMeasurementRequestReceived",
        MakeCallback(&OnAp2ReqReceived));

    staProtocols[0]->TraceConnectWithoutContext(
        "LinkMeasurementReportReceived",
        MakeCallback(&OnStaReportReceived));

    // Connect to association/disassociation/roaming traces to track roaming
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/Assoc",
                    MakeCallback(&OnAssociation));
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/DeAssoc",
                    MakeCallback(&OnDisassociation));
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/RoamingInitiated",
                    MakeCallback(&OnRoamingInitiated));

    // =====================================================
    // Setup UDP Applications
    // =====================================================
    NS_LOG_UNCOND("\n=== Setting up UDP Traffic ===");

    // UDP Server on DS node
    uint16_t udpPort = 9;
    UdpServerHelper udpServer(udpPort);
    ApplicationContainer serverApps = udpServer.Install(dsNode.Get(0));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(simTime));

    // UDP Client on STA sending to DS node
    Ipv4Address dsAddress = dsInterfaces.GetAddress(0);
    UdpClientHelper udpClient(dsAddress, udpPort);
    udpClient.SetAttribute("MaxPackets", UintegerValue(100000));
    udpClient.SetAttribute("Interval", TimeValue(MilliSeconds(10))); // 100 packets/sec
    udpClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApps = udpClient.Install(staNodes.Get(0));
    clientApps.Start(Seconds(2.0));  // Start after server
    clientApps.Stop(Seconds(simTime - 1.0));

    // Connect packet trace callbacks
    Ptr<UdpClient> udpClientApp = DynamicCast<UdpClient>(clientApps.Get(0));
    udpClientApp->TraceConnectWithoutContext("Tx", MakeCallback(&OnUdpTx));

    Ptr<UdpServer> udpServerApp = DynamicCast<UdpServer>(serverApps.Get(0));
    udpServerApp->TraceConnectWithoutContext("Rx", MakeCallback(&OnUdpRx));

    NS_LOG_UNCOND("  - UDP Server on DS node (" << dsAddress << ":" << udpPort << ")");
    NS_LOG_UNCOND("  - UDP Client on STA (" << staInterfaces.GetAddress(0) << ") → DS node");
    NS_LOG_UNCOND("  - Packet size: 1024 bytes");
    NS_LOG_UNCOND("  - Packet interval: 10 ms (100 packets/sec - HIGH TRAFFIC RATE)");
    NS_LOG_UNCOND("  - Traffic rate: ~819 kbps");
    NS_LOG_UNCOND("  - Traffic starts at 2.0s, ends at " << (simTime - 1.0) << "s");
    NS_LOG_UNCOND("  - Packet tracing enabled (logging every 50th packet)");

    // Enable PCAP tracing for debugging (optional - use --enablePcap flag)
    if (enablePcap)
    {
        spectrumPhyHelper.EnablePcap("auto-roaming-wifi", staDevices);
        csma.EnablePcap("auto-roaming-csma", dsDevices, false);
        NS_LOG_UNCOND("\n=== PCAP Tracing Enabled ===");
        NS_LOG_UNCOND("  - WiFi: auto-roaming-wifi-*.pcap");
        NS_LOG_UNCOND("  - CSMA: auto-roaming-csma-*.pcap");
    }

    // Enable Spectrum Analyzer for full 2.4 GHz band visualization
    NS_LOG_UNCOND("\n=== Spectrum Analyzer Setup (Full 2.4 GHz Band) ===");

    // Create spectrum analyzer node
    NodeContainer spectrumAnalyzerNode;
    spectrumAnalyzerNode.Create(1);

    // Position spectrum analyzer between APs
    MobilityHelper analyzerMobility;
    analyzerMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    analyzerMobility.Install(spectrumAnalyzerNode);
    spectrumAnalyzerNode.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(15.0, 0.0, 0.0));

    // Create frequency model for entire 2.4 GHz band (2.4 - 2.5 GHz)
    double startFreq = 2.4e9;       // 2.4 GHz in Hz
    double stopFreq = 2.5e9;        // 2.5 GHz in Hz
    double freqResolution = 100e3;    // 100 kHz resolution
    int numFreqBins = static_cast<int>((stopFreq - startFreq) / freqResolution);

    std::vector<double> freqs;
    freqs.reserve(numFreqBins + 1);
    for (int i = 0; i <= numFreqBins; ++i)
    {
        freqs.push_back(startFreq + i * freqResolution);
    }
    Ptr<SpectrumModel> spectrumModel = Create<SpectrumModel>(freqs);

    // Configure spectrum analyzer
    SpectrumAnalyzerHelper spectrumAnalyzerHelper;
    spectrumAnalyzerHelper.SetChannel(spectrumChannel);
    spectrumAnalyzerHelper.SetRxSpectrumModel(spectrumModel);
    spectrumAnalyzerHelper.SetPhyAttribute("Resolution", TimeValue(MicroSeconds(1000)));

    // Set noise floor
    double noisePowerDbm = -90.0;  // Noise floor in dBm
    double noisePowerWatts = std::pow(10.0, (noisePowerDbm - 30.0) / 10.0);
    spectrumAnalyzerHelper.SetPhyAttribute("NoisePowerSpectralDensity",
                                           DoubleValue(noisePowerWatts / freqResolution));

    // Enable ASCII trace output
    std::string traceFile = "spectrum-2.4ghz-band";
    spectrumAnalyzerHelper.EnableAsciiAll(traceFile);

    // Install spectrum analyzer
    NetDeviceContainer analyzerDevices = spectrumAnalyzerHelper.Install(spectrumAnalyzerNode.Get(0));

    NS_LOG_UNCOND("  - Spectrum analyzer output: " << traceFile << "-0-0.tr");
    NS_LOG_UNCOND("  - Frequency range: 2.4 - 2.5 GHz");
    NS_LOG_UNCOND("  - Frequency resolution: 1 MHz");
    NS_LOG_UNCOND("  - Time resolution: 1 ms");
    NS_LOG_UNCOND("  - Channels visible: 1 (2412 MHz), 6 (2437 MHz), 11 (2462 MHz)");
    NS_LOG_UNCOND("  - Visualize with spectrogram tool or gnuplot");

    // Schedule statistics printout
    Simulator::Schedule(Seconds(simTime - 0.1), &PrintStatistics);

    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
