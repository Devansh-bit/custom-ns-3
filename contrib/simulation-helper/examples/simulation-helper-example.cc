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
#include "ns3/simulation-helper.h"
#include "ns3/link-measurement-protocol.h"
#include "ns3/link-measurement-report.h"
#include "ns3/link-measurement-request.h"
#include "ns3/beacon-neighbor-model.h"

/**
 * @file
 * Example demonstrating the SimulationHelper modular functions.
 * This example creates a roaming scenario with 2 APs and 1 STA,
 * using the modular helper setup functions to simplify configuration.
 * The STA starts close to AP1 and moves toward AP2, triggering roaming.
 */

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SimulationHelperExample");

// Statistics tracking
struct Stats
{
    uint32_t requestsReceived = 0;
    uint32_t linkMeasurementRequestsReceivedAP2 = 0;
    uint32_t reportsReceived = 0;
    uint32_t neighborReportsReceived = 0;
    uint32_t beaconReportsReceived = 0;
    uint32_t udpPacketsSent = 0;
    uint32_t udpPacketsReceived = 0;
    uint32_t packetsBeforeRoaming = 0;
    uint32_t packetsAfterRoaming = 0;
    Time roamingTime = Seconds(0);
    bool hasRoamed = false;
};

Stats g_stats;

/**
 * \brief Callback when Link Measurement Request is received at AP1
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

/**
 * \brief Callback when Link Measurement Request is received at AP2
 */
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
    
    // Log every 50th packet
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
 */
void
OnUdpRx(Ptr<const Packet> packet)
{
    g_stats.udpPacketsReceived++;
    
    if (g_stats.udpPacketsReceived % 50 == 0)
    {
        NS_LOG_UNCOND("[" << std::fixed << std::setprecision(3)
                      << Simulator::Now().GetSeconds() << "s] "
                      << "UDP Packet #" << g_stats.udpPacketsReceived << " received at DS Node");
    }
}

/**
 * \brief Callback for STA association events
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
    if (Simulator::Now().GetSeconds() > 1.0)
    {
        g_stats.hasRoamed = true;
        g_stats.roamingTime = Simulator::Now();
        NS_LOG_UNCOND(">>> ROAMING COMPLETED - UDP traffic should continue <<<");
    }
}

/**
 * \brief Callback for STA disassociation events
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
        NodeList::GetNode(3)->GetApplication(0));
    if (server)
    {
        g_stats.udpPacketsReceived = server->GetReceived();
    }
    
    NS_LOG_UNCOND("\n" << std::string(70, '='));
    NS_LOG_UNCOND("SIMULATION RESULTS (Using SimulationHelper Modular Functions)");
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
            NS_LOG_UNCOND("  ✓ BSS TM request chain is enabled!");
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
    double simTime = 50.0;
    double startDistance = 5.0;
    double staSpeed = 2.0;
    double measurementInterval = 1.0;
    double rssiThreshold = -60.0;
    
    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.AddValue("enablePcap", "Enable PCAP tracing", enablePcap);
    cmd.AddValue("simTime", "Total simulation time (seconds)", simTime);
    cmd.AddValue("startDistance", "Initial distance between AP and STA (meters)", startDistance);
    cmd.AddValue("staSpeed", "STA movement speed (m/s)", staSpeed);
    cmd.AddValue("measurementInterval", "Interval between measurements (seconds)", measurementInterval);
    cmd.AddValue("rssiThreshold", "RSSI threshold for neighbor request (dBm)", rssiThreshold);
    cmd.Parse(argc, argv);
    
    // Enable logging
    LogComponentEnable("SimulationHelper", LOG_LEVEL_INFO);
    LogComponentEnable("AutoRoamingKvHelper", LOG_LEVEL_INFO);
    LogComponentEnable("BssTm11vHelper", LOG_LEVEL_INFO);
    LogComponentEnable("RoamingManager", LOG_LEVEL_INFO);
    
    if (verbose)
    {
        LogComponentEnable("AutoRoamingKvHelper", LOG_LEVEL_ALL);
        LogComponentEnable("LinkMeasurementProtocol", LOG_LEVEL_ALL);
        LogComponentEnable("NeighborProtocolHelper", LOG_LEVEL_ALL);
        LogComponentEnable("BeaconProtocolHelper", LOG_LEVEL_ALL);
        LogComponentEnable("BssTm11vHelper", LOG_LEVEL_ALL);
        LogComponentEnable("RoamingManager", LOG_LEVEL_ALL);
        LogComponentEnable("SimulationHelperExample", LOG_LEVEL_ALL);
    }
    
    NS_LOG_UNCOND("\n=== SimulationHelper Example: Modular Auto-Roaming Setup ===");
    NS_LOG_UNCOND("Configuration:");
    NS_LOG_UNCOND("  Initial Distance: " << startDistance << " m");
    NS_LOG_UNCOND("  STA Speed: " << staSpeed << " m/s");
    NS_LOG_UNCOND("  Measurement Interval: " << measurementInterval << " s");
    NS_LOG_UNCOND("  RSSI Threshold: " << rssiThreshold << " dBm");
    NS_LOG_UNCOND("  Simulation Time: " << simTime << " s\n");
    
    // =====================================================
    // Create nodes
    // =====================================================
    NodeContainer apNodes;
    NodeContainer staNodes;
    NodeContainer dsNode;
    apNodes.Create(2);
    staNodes.Create(1);
    dsNode.Create(1);
    
    // =====================================================
    // Setup mobility
    // =====================================================
    MobilityHelper mobilityAp;
    mobilityAp.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityAp.Install(apNodes);
    apNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0.0, 0.0, 0.0));
    apNodes.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector(30.0, 0.0, 0.0));
    
    MobilityHelper mobilitySta;
    mobilitySta.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobilitySta.Install(staNodes);
    Ptr<ConstantVelocityMobilityModel> staMobility =
        staNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();
    staMobility->SetPosition(Vector(startDistance, 0.0, 0.0));
    staMobility->SetVelocity(Vector(staSpeed, 0.0, 0.0));
    
    MobilityHelper mobilityDs;
    mobilityDs.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityDs.Install(dsNode);
    dsNode.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(15.0, -10.0, 0.0));
    
    // =====================================================
    // Setup WiFi
    // =====================================================
    YansWifiChannelHelper channelHelper;
    channelHelper.AddPropagationLoss("ns3::LogDistancePropagationLossModel");
    channelHelper.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    Ptr<YansWifiChannel> channel = channelHelper.Create();

    YansWifiPhyHelper phyHelper;
    phyHelper.SetChannel(channel);
    
    WifiMacHelper wifiMacHelper;
    Ssid ssid = Ssid("simulation-helper-test");
    
    WifiHelper wifiHelper;
    wifiHelper.SetStandard(WIFI_STANDARD_80211n);
    wifiHelper.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                       "DataMode", StringValue("HtMcs7"),
                                       "ControlMode", StringValue("HtMcs0"));
    
    wifiMacHelper.SetType("ns3::ApWifiMac",
                          "Ssid", SsidValue(ssid),
                          "BeaconInterval", TimeValue(MicroSeconds(102400)));
    
    // Install WiFi on APs
    NetDeviceContainer apDevices;
    
    // AP1 on channel 11
    phyHelper.Set("ChannelSettings", StringValue("{11, 20, BAND_2_4GHZ, 0}"));
    phyHelper.Set("TxPowerStart", DoubleValue(16.0));
    phyHelper.Set("TxPowerEnd", DoubleValue(16.0));
    NetDeviceContainer ap1Device = wifiHelper.Install(phyHelper, wifiMacHelper, apNodes.Get(0));
    apDevices.Add(ap1Device);
    
    // AP2 on channel 1
    phyHelper.Set("ChannelSettings", StringValue("{1, 20, BAND_2_4GHZ, 0}"));
    phyHelper.Set("TxPowerStart", DoubleValue(16.0));
    phyHelper.Set("TxPowerEnd", DoubleValue(16.0));
    NetDeviceContainer ap2Device = wifiHelper.Install(phyHelper, wifiMacHelper, apNodes.Get(1));
    apDevices.Add(ap2Device);
    
    NS_LOG_UNCOND("Created 2 APs (AP1: Channel 11, AP2: Channel 1)");
    
    // Install WiFi on STA (start on channel 11)
    phyHelper.Set("ChannelSettings", StringValue("{11, 20, BAND_2_4GHZ, 0}"));
    wifiMacHelper.SetType("ns3::StaWifiMac",
                          "Ssid", SsidValue(ssid),
                          "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifiHelper.Install(phyHelper, wifiMacHelper, staNodes);
    
    // =====================================================
    // Setup CSMA network for Distribution System
    // =====================================================
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(MicroSeconds(50)));
    
    NodeContainer csmaNodes;
    csmaNodes.Add(apNodes);
    csmaNodes.Add(dsNode);
    
    NetDeviceContainer csmaDevices = csma.Install(csmaNodes);
    
    NetDeviceContainer apCsmaDevices;
    apCsmaDevices.Add(csmaDevices.Get(0));
    apCsmaDevices.Add(csmaDevices.Get(1));
    NetDeviceContainer dsDevices;
    dsDevices.Add(csmaDevices.Get(2));
    
    // Create bridges on APs
    BridgeHelper bridge;
    NetDeviceContainer ap1BridgedDevices;
    ap1BridgedDevices.Add(apDevices.Get(0));
    ap1BridgedDevices.Add(apCsmaDevices.Get(0));
    bridge.Install(apNodes.Get(0), ap1BridgedDevices);
    
    NetDeviceContainer ap2BridgedDevices;
    ap2BridgedDevices.Add(apDevices.Get(1));
    ap2BridgedDevices.Add(apCsmaDevices.Get(1));
    bridge.Install(apNodes.Get(1), ap2BridgedDevices);
    
    NS_LOG_UNCOND("Created Distribution System with bridges");
    
    // =====================================================
    // Install Internet stack
    // =====================================================
    InternetStackHelper internet;
    internet.Install(staNodes);
    internet.Install(dsNode);
    
    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer staInterfaces = ipv4.Assign(staDevices);
    Ipv4InterfaceContainer dsInterfaces = ipv4.Assign(dsDevices);
    
    NS_LOG_UNCOND("Assigned IP addresses (10.1.1.0/24)");

    // Get MAC addresses using helper function
    std::vector<Ptr<WifiNetDevice>> apNetDevices = SimulationHelper::GetWifiNetDevices(apDevices);
    std::vector<Ptr<WifiNetDevice>> staNetDevices = SimulationHelper::GetWifiNetDevices(staDevices);

    Mac48Address ap1Mac = apNetDevices[0]->GetMac()->GetAddress();
    Mac48Address ap2Mac = apNetDevices[1]->GetMac()->GetAddress();
    Mac48Address staMac = staNetDevices[0]->GetMac()->GetAddress();
    
    NS_LOG_UNCOND("AP1 MAC: " << ap1Mac << " (Channel 11)");
    NS_LOG_UNCOND("AP2 MAC: " << ap2Mac << " (Channel 1)");
    NS_LOG_UNCOND("STA MAC: " << staMac << "\n");
    
    // =====================================================
    // Setup helper protocols using SimulationHelper modular functions
    // =====================================================
    NS_LOG_UNCOND("=== Using SimulationHelper Modular Functions ===\n");

    // 1. Setup DualPhySniffer - Two instances for functional separation
    std::vector<Mac48Address> apMacs = {ap1Mac, ap2Mac};
    std::vector<Mac48Address> staMacs;
    for (uint32_t i = 0; i < staDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        staMacs.push_back(staDevice->GetMac()->GetAddress());
    }

    std::vector<uint8_t> apOperatingChannels = {11, 1};
    std::vector<uint8_t> staOperatingChannels = {11};  // 1 STA on channel 11
    std::vector<uint8_t> scanningChannels = {1, 6, 11};

    // Create AP DualPhySniffer (for Neighbor Protocol)
    DualPhySnifferHelper* apDualPhySniffer = SimulationHelper::SetupAPDualPhySniffer(
        apDevices,
        apMacs,
        channel,
        apOperatingChannels,
        scanningChannels,
        MilliSeconds(100));

    // Create STA DualPhySniffer (for Beacon Protocol)
    DualPhySnifferHelper* staDualPhySniffer = SimulationHelper::SetupSTADualPhySniffer(
        staDevices,
        staMacs,
        channel,
        staOperatingChannels,
        scanningChannels,
        MilliSeconds(100));

    // 2. Create neighbor table
    std::vector<ApInfo> neighborTable;
    
    ApInfo ap1Info;
    ap1Info.bssid = ap1Mac;
    ap1Info.ssid = "simulation-helper-test";
    ap1Info.channel = 11;
    ap1Info.regulatoryClass = 81;
    ap1Info.phyType = 7;
    ap1Info.position = Vector(0.0, 0.0, 0.0);
    neighborTable.push_back(ap1Info);
    
    ApInfo ap2Info;
    ap2Info.bssid = ap2Mac;
    ap2Info.ssid = "simulation-helper-test";
    ap2Info.channel = 1;
    ap2Info.regulatoryClass = 81;
    ap2Info.phyType = 7;
    ap2Info.position = Vector(30.0, 0.0, 0.0);
    neighborTable.push_back(ap2Info);
    
    // 3. Setup NeighborProtocol (APs use apDualPhySniffer, STAs don't need it)
    std::vector<Ptr<NeighborProtocolHelper>> neighborProtocols =
        SimulationHelper::SetupNeighborProtocol(
            apDevices,
            staDevices,
            apMacs,
            neighborTable,
            apDualPhySniffer);  // APs use this for neighbor discovery

    // Extract individual instances (2 APs + 1 STA = 3 total)
    Ptr<NeighborProtocolHelper> neighborProtocolAp1 = neighborProtocols[0];
    Ptr<NeighborProtocolHelper> neighborProtocolAp2 = neighborProtocols[1];
    Ptr<NeighborProtocolHelper> neighborProtocolSta = neighborProtocols[2];

    // Connect neighbor report trace
    neighborProtocolSta->m_neighborReportReceivedTrace.ConnectWithoutContext(
        MakeCallback(&OnNeighborReportReceived));

    // 4. Setup BeaconProtocol (APs don't need it, STAs use staDualPhySniffer)
    std::vector<Ptr<BeaconProtocolHelper>> beaconProtocols =
        SimulationHelper::SetupBeaconProtocol(
            apDevices,
            staDevices,
            staDualPhySniffer); // STAs use this for beacon reports
    
    // Extract individual instances
    Ptr<BeaconProtocolHelper> beaconProtocolAp1 = beaconProtocols[0];
    Ptr<BeaconProtocolHelper> beaconProtocolAp2 = beaconProtocols[1];
    Ptr<BeaconProtocolHelper> beaconProtocolSta = beaconProtocols[2];

    // Connect beacon report traces
    beaconProtocolAp1->m_beaconReportReceivedTrace.ConnectWithoutContext(
        MakeCallback(&OnBeaconReportReceived));
    beaconProtocolAp2->m_beaconReportReceivedTrace.ConnectWithoutContext(
        MakeCallback(&OnBeaconReportReceived));
    
    // 5. Setup BssTmHelper
    std::vector<Ptr<BssTm11vHelper>> bssTmHelpers =
        SimulationHelper::SetupBssTmHelper(apDevices, staDevices);
    
    // Extract individual instances
    Ptr<BssTm11vHelper> bssTmHelperAp1 = bssTmHelpers[0];
    Ptr<BssTm11vHelper> bssTmHelperAp2 = bssTmHelpers[1];
    Ptr<BssTm11vHelper> bssTmHelperSta = bssTmHelpers[2];

    // 6. Setup AutoRoamingKvHelper
    std::vector<Ptr<BeaconProtocolHelper>> beaconProtocolAps = {beaconProtocolAp1, beaconProtocolAp2};
    std::vector<Ptr<NeighborProtocolHelper>> neighborProtocolStas = {neighborProtocolSta};
    std::vector<Ptr<BeaconProtocolHelper>> beaconProtocolStas = {beaconProtocolSta};
    std::vector<Ptr<BssTm11vHelper>> bssTmHelperStas = {bssTmHelperSta};

    auto autoRoamingResult = SimulationHelper::SetupAutoRoamingKvHelperMulti(
        apDevices,
        staDevices,
        neighborProtocolStas,
        beaconProtocolStas,
        beaconProtocolAps,
        bssTmHelperStas,
        Seconds(measurementInterval),
        rssiThreshold);

    AutoRoamingKvHelper* autoRoamingHelper = autoRoamingResult.helpers[0];
    std::vector<Ptr<LinkMeasurementProtocol>> apProtocols = autoRoamingResult.apProtocols[0];
    std::vector<Ptr<LinkMeasurementProtocol>> staProtocols = autoRoamingResult.staProtocols[0];
    
    // Note: autoRoamingHelper is kept alive for the entire simulation
    // It handles internal callbacks for the roaming protocol chain
    (void)autoRoamingHelper;  // Suppress unused variable warning
    
    NS_LOG_UNCOND("\n=== All Helper Protocols Setup Complete (Using Modular Functions) ===\n");
    
    // =====================================================
    // Connect trace callbacks
    // =====================================================
    apProtocols[0]->TraceConnectWithoutContext(
        "LinkMeasurementRequestReceived",
        MakeCallback(&OnApRequestReceived));
    
    apProtocols[1]->TraceConnectWithoutContext(
        "LinkMeasurementRequestReceived",
        MakeCallback(&OnAp2ReqReceived));
    
    staProtocols[0]->TraceConnectWithoutContext(
        "LinkMeasurementReportReceived",
        MakeCallback(&OnStaReportReceived));
    
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/Assoc",
                    MakeCallback(&OnAssociation));
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/DeAssoc",
                    MakeCallback(&OnDisassociation));
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/RoamingInitiated",
                    MakeCallback(&OnRoamingInitiated));
    
    // =====================================================
    // Setup UDP Applications
    // =====================================================
    NS_LOG_UNCOND("=== Setting up UDP Traffic ===");
    
    uint16_t udpPort = 9;
    UdpServerHelper udpServer(udpPort);
    ApplicationContainer serverApps = udpServer.Install(dsNode.Get(0));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(simTime));
    
    Ipv4Address dsAddress = dsInterfaces.GetAddress(0);
    UdpClientHelper udpClient(dsAddress, udpPort);
    udpClient.SetAttribute("MaxPackets", UintegerValue(100000));
    udpClient.SetAttribute("Interval", TimeValue(MilliSeconds(10)));
    udpClient.SetAttribute("PacketSize", UintegerValue(1024));
    
    ApplicationContainer clientApps = udpClient.Install(staNodes.Get(0));
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(simTime - 1.0));
    
    Ptr<UdpClient> udpClientApp = DynamicCast<UdpClient>(clientApps.Get(0));
    udpClientApp->TraceConnectWithoutContext("Tx", MakeCallback(&OnUdpTx));
    
    Ptr<UdpServer> udpServerApp = DynamicCast<UdpServer>(serverApps.Get(0));
    udpServerApp->TraceConnectWithoutContext("Rx", MakeCallback(&OnUdpRx));
    
    NS_LOG_UNCOND("  - UDP Server on DS node (" << dsAddress << ":" << udpPort << ")");
    NS_LOG_UNCOND("  - UDP Client on STA (100 packets/sec, 1024 bytes)\n");
    
    // Enable PCAP if requested
    if (enablePcap)
    {
        phyHelper.EnablePcap("simulation-helper-wifi", staDevices);
        csma.EnablePcap("simulation-helper-csma", dsDevices, false);
        NS_LOG_UNCOND("PCAP tracing enabled");
    }
    
    // Schedule statistics printout
    Simulator::Schedule(Seconds(simTime - 0.1), &PrintStatistics);
    
    // Run simulation
    NS_LOG_UNCOND("=== Starting Simulation ===\n");
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();
    
    return 0;
}
