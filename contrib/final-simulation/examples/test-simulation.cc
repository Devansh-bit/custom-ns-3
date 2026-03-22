/**
 * Test Simulation - Simplified WiFi Network with Waypoint Mobility
 *
 * A stripped-down version of config-simulation that focuses on:
 * - Waypoint-based mobility for realistic STA movement
 * - Basic 802.11ac WiFi setup (5GHz)
 * - TCP traffic with realistic office patterns
 * - FlowMonitor for basic performance metrics
 *
 * This simulation does NOT include:
 * - Roaming protocols (BSS TM, auto-roaming, beacon/neighbor protocols)
 * - External integrations (Kafka producer/consumer, LeverAPI)
 * - Advanced monitoring (CCA monitor, dual-phy sniffer, MCS tracking)
 * - ACI simulation
 *
 * Usage:
 *   ./ns3 run "test-simulation"
 *   ./ns3 run "test-simulation --configFile=my-scenario.json"
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/propagation-module.h"
#include "ns3/applications-module.h"
#include "ns3/csma-helper.h"
#include "ns3/bridge-helper.h"

// Custom modules - simulation-helper
#include "ns3/simulation-helper.h"

// Custom modules - waypoint simulation
#include "ns3/simulation-config-parser.h"
#include "ns3/waypoint-mobility-helper.h"
#include "ns3/waypoint-grid.h"

// Flow monitoring
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"

// TCP
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/on-off-helper.h"

#include <cmath>
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TestSimulation");

// ============================================================================
// GLOBAL FLOW MONITOR REFERENCES
// ============================================================================

Ptr<FlowMonitor> g_flowMonitor;
Ptr<Ipv4FlowClassifier> g_flowClassifier;

// ============================================================================
// PERIODIC STATS COLLECTION
// ============================================================================

void CollectAndPrintStats()
{
    if (!g_flowMonitor || !g_flowClassifier) {
        Simulator::Schedule(Seconds(1.0), &CollectAndPrintStats);
        return;
    }

    g_flowMonitor->CheckForLostPackets();
    FlowMonitor::FlowStatsContainer stats = g_flowMonitor->GetFlowStats();

    double totalThroughput = 0.0;
    uint64_t totalRxPackets = 0;
    uint64_t totalTxPackets = 0;
    uint64_t totalLostPackets = 0;
    double totalDelay = 0.0;
    uint32_t flowCount = 0;

    for (auto it = stats.begin(); it != stats.end(); ++it) {
        FlowMonitor::FlowStats& fs = it->second;

        if (fs.rxPackets > 0) {
            double duration = (fs.timeLastRxPacket - fs.timeFirstTxPacket).GetSeconds();
            double throughput = (duration > 0) ? (fs.rxBytes * 8.0 / duration / 1e6) : 0;
            double avgDelay = (fs.delaySum.GetSeconds() / fs.rxPackets) * 1000.0;

            totalThroughput += throughput;
            totalRxPackets += fs.rxPackets;
            totalTxPackets += fs.txPackets;
            totalLostPackets += fs.lostPackets;
            totalDelay += avgDelay;
            flowCount++;
        }
    }

    double avgDelay = (flowCount > 0) ? (totalDelay / flowCount) : 0;
    double lossRate = (totalTxPackets > 0) ? ((double)totalLostPackets / totalTxPackets * 100.0) : 0;

    std::cout << "[t=" << std::fixed << std::setprecision(1) << Simulator::Now().GetSeconds() << "s] "
              << "Flows: " << flowCount
              << " | Throughput: " << std::setprecision(2) << totalThroughput << " Mbps"
              << " | Delay: " << avgDelay << " ms"
              << " | Packets: " << totalRxPackets
              << " | Loss: " << std::setprecision(1) << lossRate << "%"
              << std::endl;

    // Reschedule
    Simulator::Schedule(Seconds(1.0), &CollectAndPrintStats);
}

// ============================================================================
// MAIN SIMULATION
// ============================================================================

int main(int argc, char* argv[])
{
    // ========================================================================
    // COMMAND-LINE PARAMETERS
    // ========================================================================

    std::string configFile = "config-simulation.json";
    bool verbose = false;
    bool enablePcap = false;
    double simulationTimeOverride = 0.0;  // 0 = use config file value

    CommandLine cmd(__FILE__);
    cmd.AddValue("configFile", "Path to JSON configuration file", configFile);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.AddValue("enablePcap", "Enable PCAP tracing", enablePcap);
    cmd.AddValue("simulationTime", "Override simulation time from config (seconds)", simulationTimeOverride);

    cmd.Parse(argc, argv);

    // Enable logging
    if (verbose) {
        LogComponentEnable("TestSimulation", LOG_LEVEL_INFO);
        LogComponentEnable("WaypointMobilityHelper", LOG_LEVEL_INFO);
    }

    // ========================================================================
    // TCP CONFIGURATION - Optimized for Wireless
    // ========================================================================

    Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(100));
    Config::SetDefault("ns3::TcpSocket::ConnCount", UintegerValue(100));
    Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(Seconds(1.0)));
    Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(Seconds(3.0)));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(262144));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(262144));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(2));
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpCubic::GetTypeId()));

    NS_LOG_INFO("\n========================================================");
    NS_LOG_INFO("  Test Simulation - Waypoint Mobility + TCP Traffic");
    NS_LOG_INFO("========================================================\n");

    // ========================================================================
    // PARSE CONFIG FILE
    // ========================================================================

    NS_LOG_INFO("[Config] Parsing configuration file: " << configFile);
    SimulationConfigData config = SimulationConfigParser::ParseFile(configFile);

    double simulationTime = (simulationTimeOverride > 0) ? simulationTimeOverride : config.simulationTime;

    NS_LOG_INFO("[Config] Configuration loaded:");
    NS_LOG_INFO("  Simulation Time: " << simulationTime << " s");
    NS_LOG_INFO("  APs: " << config.aps.size());
    NS_LOG_INFO("  STAs: " << config.stas.size());
    NS_LOG_INFO("  Waypoints: " << config.waypoints.size() << "\n");

    // ========================================================================
    // CREATE NODES
    // ========================================================================

    NodeContainer apNodes;
    NodeContainer staNodes;
    NodeContainer dsNode;
    apNodes.Create(config.aps.size());
    staNodes.Create(config.stas.size());
    dsNode.Create(1);

    NS_LOG_INFO("[Setup] Created " << config.aps.size() << " AP nodes, "
                  << config.stas.size() << " STA nodes, 1 DS node");

    // ========================================================================
    // MOBILITY SETUP
    // ========================================================================

    // APs: ConstantPosition from config
    MobilityHelper mobilityAp;
    mobilityAp.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityAp.Install(apNodes);

    for (size_t i = 0; i < config.aps.size(); i++) {
        apNodes.Get(i)->GetObject<MobilityModel>()->SetPosition(config.aps[i].position);
    }

    NS_LOG_INFO("[Setup] AP positions from config:");
    for (size_t i = 0; i < config.aps.size(); i++) {
        Vector pos = config.aps[i].position;
        NS_LOG_INFO("        AP" << i << ": (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
    }

    // STAs: Waypoint-based mobility from config
    Ptr<WaypointGrid> waypointGrid = CreateObject<WaypointGrid>();
    for (const auto& wp : config.waypoints) {
        waypointGrid->AddWaypoint(wp.id, wp.position);
    }

    WaypointMobilityHelper mobilityHelper;
    mobilityHelper.SetWaypointGrid(waypointGrid);
    mobilityHelper.Install(staNodes, config.stas);

    NS_LOG_INFO("[Setup] STAs configured with waypoint mobility:");
    for (size_t i = 0; i < config.stas.size(); i++) {
        NS_LOG_INFO("        STA" << i << ": starts at waypoint " << config.stas[i].initialWaypointId
                      << ", velocity [" << config.stas[i].transferVelocityMin << ", "
                      << config.stas[i].transferVelocityMax << "] m/s");
    }

    // DS node: Center of AP positions
    Vector dsPosition(0, 0, 0);
    for (const auto& ap : config.aps) {
        dsPosition.x += ap.position.x;
        dsPosition.y += ap.position.y;
        dsPosition.z += ap.position.z;
    }
    dsPosition.x /= config.aps.size();
    dsPosition.y /= config.aps.size();
    dsPosition.z /= config.aps.size();

    MobilityHelper mobilityDs;
    mobilityDs.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityDs.Install(dsNode);
    dsNode.Get(0)->GetObject<MobilityModel>()->SetPosition(dsPosition);

    // ========================================================================
    // YANS WIFI CHANNEL SETUP
    // ========================================================================

    YansWifiChannelHelper channelHelper;
    channelHelper.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                     "Exponent", DoubleValue(3.0),
                                     "ReferenceDistance", DoubleValue(1.0),
                                     "ReferenceLoss", DoubleValue(46.6777));
    channelHelper.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");

    Ptr<YansWifiChannel> sharedChannel = channelHelper.Create();

    NS_LOG_INFO("[Setup] WiFi channel created with log-distance path loss");

    // ========================================================================
    // WIFI SETUP - 5 GHZ BAND
    // ========================================================================

    // Maximize A-MPDU aggregation
    Config::SetDefault("ns3::WifiMac::BE_MaxAmpduSize", UintegerValue(1048575));
    Config::SetDefault("ns3::WifiMac::BK_MaxAmpduSize", UintegerValue(1048575));
    Config::SetDefault("ns3::WifiMac::VI_MaxAmpduSize", UintegerValue(1048575));
    Config::SetDefault("ns3::WifiMac::VO_MaxAmpduSize", UintegerValue(1048575));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ac);
    wifi.SetRemoteStationManager("ns3::IdealWifiManager");

    YansWifiPhyHelper yansPhy;
    yansPhy.SetChannel(sharedChannel);

    WifiMacHelper wifiMac;
    Ssid ssid = Ssid("TestSimulation-Network");

    // Extract AP channels from config
    std::vector<uint8_t> apChannels;
    for (const auto& ap : config.aps) {
        apChannels.push_back(ap.channel);
    }

    // Install APs using SimulationHelper
    NetDeviceContainer apDevices = SimulationHelper::InstallApDevices(
        wifi, yansPhy, wifiMac, apNodes, apChannels, ssid,
        20.0, 20, WIFI_PHY_BAND_5GHZ);

    NS_LOG_INFO("[Setup] APs installed on 5GHz:");
    for (size_t i = 0; i < config.aps.size(); i++) {
        NS_LOG_INFO("        AP" << i << " (CH" << (int)apChannels[i] << ")");
    }

    // Distribute STAs uniformly across APs for balanced load
    std::vector<uint8_t> staChannels;
    for (size_t i = 0; i < config.stas.size(); i++) {
        staChannels.push_back(apChannels[i % apChannels.size()]);
    }

    NetDeviceContainer staDevices = SimulationHelper::InstallStaDevices(
        wifi, yansPhy, wifiMac, staNodes, staChannels, ssid,
        20, WIFI_PHY_BAND_5GHZ);

    NS_LOG_INFO("[Setup] STAs distributed across " << apChannels.size() << " APs");

    // ========================================================================
    // DISTRIBUTION SYSTEM (CSMA + BRIDGE)
    // ========================================================================

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("1Gbps"));
    csma.SetChannelAttribute("Delay", TimeValue(MicroSeconds(10)));

    NodeContainer csmaNodes;
    csmaNodes.Add(apNodes);
    csmaNodes.Add(dsNode);

    NetDeviceContainer csmaDevices = csma.Install(csmaNodes);

    NetDeviceContainer apCsmaDevices;
    for (uint32_t i = 0; i < apNodes.GetN(); i++) {
        apCsmaDevices.Add(csmaDevices.Get(i));
    }

    NetDeviceContainer dsDevices;
    dsDevices.Add(csmaDevices.Get(apNodes.GetN()));

    // Bridge WiFi and CSMA on each AP
    BridgeHelper bridge;
    for (uint32_t i = 0; i < apNodes.GetN(); i++) {
        NetDeviceContainer bridgePair;
        bridgePair.Add(apDevices.Get(i));
        bridgePair.Add(apCsmaDevices.Get(i));
        bridge.Install(apNodes.Get(i), bridgePair);
    }

    NS_LOG_INFO("[Setup] CSMA backbone created with bridges");

    // ========================================================================
    // INTERNET STACK
    // ========================================================================

    InternetStackHelper internet;
    internet.Install(staNodes);
    internet.Install(dsNode);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer staInterfaces = ipv4.Assign(staDevices);
    Ipv4InterfaceContainer dsInterfaces = ipv4.Assign(dsDevices);

    NS_LOG_INFO("[Setup] IP addresses assigned (10.1.1.0/24)");

    // ========================================================================
    // GET MAC ADDRESSES (for logging)
    // ========================================================================

    std::vector<Ptr<WifiNetDevice>> apNetDevices = SimulationHelper::GetWifiNetDevices(apDevices);
    std::vector<Ptr<WifiNetDevice>> staNetDevices = SimulationHelper::GetWifiNetDevices(staDevices);

    NS_LOG_INFO("\nMAC Addresses:");
    for (size_t i = 0; i < apNetDevices.size(); i++) {
        NS_LOG_INFO("  AP" << i << ": " << apNetDevices[i]->GetMac()->GetAddress()
                    << " (CH" << (int)apChannels[i] << ")");
    }
    for (size_t i = 0; i < staNetDevices.size(); i++) {
        NS_LOG_INFO("  STA" << i << ": " << staNetDevices[i]->GetMac()->GetAddress());
    }

    // ========================================================================
    // SETUP FLOW MONITOR
    // ========================================================================

    FlowMonitorHelper flowMonHelper;
    g_flowMonitor = flowMonHelper.InstallAll();
    g_flowMonitor->Start(Seconds(5.0));
    g_flowClassifier = DynamicCast<Ipv4FlowClassifier>(flowMonHelper.GetClassifier());

    // Schedule periodic stats collection (every 1 second, starting at t=5s)
    Simulator::Schedule(Seconds(5.0), &CollectAndPrintStats);

    // ========================================================================
    // SETUP TCP TRAFFIC
    // ========================================================================

    NS_LOG_INFO("\n[Traffic] Setting up TCP applications...");

    uint16_t uplinkPort = 50000;
    uint16_t downlinkPort = 60000;
    Ipv4Address dsAddress = dsInterfaces.GetAddress(0);

    NS_LOG_INFO("[Traffic] DS Address: " << dsAddress);

    // TCP Server on DS node (for UPLINK traffic from STAs)
    PacketSinkHelper uplinkSinkHelper("ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), uplinkPort));
    ApplicationContainer uplinkServerApps = uplinkSinkHelper.Install(dsNode.Get(0));
    uplinkServerApps.Start(Seconds(1.0));
    uplinkServerApps.Stop(Seconds(simulationTime));

    // Traffic pattern distribution
    uint32_t numWebEmail = static_cast<uint32_t>(staNodes.GetN() * 0.4);
    uint32_t numVideoConf = static_cast<uint32_t>(staNodes.GetN() * 0.35);

    // UPLINK: TCP Clients on STAs
    for (uint32_t i = 0; i < staNodes.GetN(); i++) {
        OnOffHelper onoff("ns3::TcpSocketFactory",
                         InetSocketAddress(dsAddress, uplinkPort));

        std::string trafficType;
        if (i < numWebEmail) {
            onoff.SetAttribute("DataRate", DataRateValue(DataRate("1Mbps")));
            onoff.SetAttribute("PacketSize", UintegerValue(1460));
            onoff.SetAttribute("OnTime",
                StringValue("ns3::ParetoRandomVariable[Scale=2.67|Shape=1.5|Bound=25.0]"));
            onoff.SetAttribute("OffTime",
                StringValue("ns3::ParetoRandomVariable[Scale=1.0|Shape=1.5|Bound=10.0]"));
            trafficType = "Web/Email";
        } else if (i < numWebEmail + numVideoConf) {
            onoff.SetAttribute("DataRate", DataRateValue(DataRate("3Mbps")));
            onoff.SetAttribute("PacketSize", UintegerValue(1460));
            onoff.SetAttribute("OnTime",
                StringValue("ns3::ParetoRandomVariable[Scale=10.0|Shape=1.5|Bound=60.0]"));
            onoff.SetAttribute("OffTime",
                StringValue("ns3::ParetoRandomVariable[Scale=3.33|Shape=1.5|Bound=30.0]"));
            trafficType = "VideoConf";
        } else {
            onoff.SetAttribute("DataRate", DataRateValue(DataRate("2Mbps")));
            onoff.SetAttribute("PacketSize", UintegerValue(1460));
            onoff.SetAttribute("OnTime",
                StringValue("ns3::ParetoRandomVariable[Scale=3.33|Shape=1.5|Bound=30.0]"));
            onoff.SetAttribute("OffTime",
                StringValue("ns3::ParetoRandomVariable[Scale=0.667|Shape=1.5|Bound=8.0]"));
            trafficType = "CloudApps";
        }

        ApplicationContainer clientApp = onoff.Install(staNodes.Get(i));
        clientApp.Start(Seconds(2.0 + i * 0.1));
        clientApp.Stop(Seconds(simulationTime - 1.0));

        NS_LOG_INFO("[Traffic] STA " << i << ": " << trafficType << " (uplink)");
    }

    // DOWNLINK: PacketSink on each STA + OnOff from DS
    for (uint32_t i = 0; i < staNodes.GetN(); i++) {
        PacketSinkHelper downlinkSinkHelper("ns3::TcpSocketFactory",
            InetSocketAddress(Ipv4Address::GetAny(), downlinkPort));
        ApplicationContainer staSinkApp = downlinkSinkHelper.Install(staNodes.Get(i));
        staSinkApp.Start(Seconds(1.0));
        staSinkApp.Stop(Seconds(simulationTime));
    }

    for (uint32_t i = 0; i < staNodes.GetN(); i++) {
        Ipv4Address staAddress = staInterfaces.GetAddress(i);

        OnOffHelper downlinkOnoff("ns3::TcpSocketFactory",
                                 InetSocketAddress(staAddress, downlinkPort));

        if (i < numWebEmail) {
            downlinkOnoff.SetAttribute("DataRate", DataRateValue(DataRate("3Mbps")));
            downlinkOnoff.SetAttribute("PacketSize", UintegerValue(1460));
            downlinkOnoff.SetAttribute("OnTime",
                StringValue("ns3::ParetoRandomVariable[Scale=2.67|Shape=1.5|Bound=25.0]"));
            downlinkOnoff.SetAttribute("OffTime",
                StringValue("ns3::ParetoRandomVariable[Scale=1.0|Shape=1.5|Bound=10.0]"));
        } else if (i < numWebEmail + numVideoConf) {
            downlinkOnoff.SetAttribute("DataRate", DataRateValue(DataRate("3Mbps")));
            downlinkOnoff.SetAttribute("PacketSize", UintegerValue(1460));
            downlinkOnoff.SetAttribute("OnTime",
                StringValue("ns3::ParetoRandomVariable[Scale=10.0|Shape=1.5|Bound=60.0]"));
            downlinkOnoff.SetAttribute("OffTime",
                StringValue("ns3::ParetoRandomVariable[Scale=3.33|Shape=1.5|Bound=30.0]"));
        } else {
            downlinkOnoff.SetAttribute("DataRate", DataRateValue(DataRate("8Mbps")));
            downlinkOnoff.SetAttribute("PacketSize", UintegerValue(1460));
            downlinkOnoff.SetAttribute("OnTime",
                StringValue("ns3::ParetoRandomVariable[Scale=3.33|Shape=1.5|Bound=30.0]"));
            downlinkOnoff.SetAttribute("OffTime",
                StringValue("ns3::ParetoRandomVariable[Scale=0.667|Shape=1.5|Bound=8.0]"));
        }

        ApplicationContainer downlinkApp = downlinkOnoff.Install(dsNode.Get(0));
        downlinkApp.Start(Seconds(2.05 + i * 0.1));
        downlinkApp.Stop(Seconds(simulationTime - 1.0));
    }

    NS_LOG_INFO("[Traffic] Configured " << staNodes.GetN() << " uplink + "
                  << staNodes.GetN() << " downlink flows");

    // ========================================================================
    // ENABLE PCAP (OPTIONAL)
    // ========================================================================

    if (enablePcap) {
        yansPhy.EnablePcap("test-simulation-wifi", staDevices);
        csma.EnablePcap("test-simulation-csma", dsDevices, false);
    }

    // ========================================================================
    // START WAYPOINT MOBILITY
    // ========================================================================

    NS_LOG_INFO("\n[Mobility] Starting waypoint-based mobility...");
    mobilityHelper.StartMobility();

    // ========================================================================
    // RUN SIMULATION
    // ========================================================================

    NS_LOG_INFO("\n========================================================");
    NS_LOG_INFO("  Starting Simulation (" << simulationTime << " seconds)");
    NS_LOG_INFO("========================================================\n");

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // ========================================================================
    // PRINT FLOW MONITOR STATISTICS
    // ========================================================================

    NS_LOG_INFO("\n========================================================");
    NS_LOG_INFO("  FlowMonitor Statistics");
    NS_LOG_INFO("========================================================\n");

    FlowMonitor::FlowStatsContainer stats = g_flowMonitor->GetFlowStats();

    double totalThroughput = 0.0;
    uint64_t totalPackets = 0;
    double totalDelay = 0.0;
    uint32_t flowCount = 0;

    for (auto it = stats.begin(); it != stats.end(); ++it) {
        Ipv4FlowClassifier::FiveTuple tuple = g_flowClassifier->FindFlow(it->first);
        FlowMonitor::FlowStats& fs = it->second;

        if (fs.rxPackets > 0) {
            double duration = (fs.timeLastRxPacket - fs.timeFirstTxPacket).GetSeconds();
            double throughput = (duration > 0) ? (fs.rxBytes * 8.0 / duration / 1e6) : 0;
            double avgDelay = (fs.delaySum.GetSeconds() / fs.rxPackets) * 1000.0;

            totalThroughput += throughput;
            totalPackets += fs.rxPackets;
            totalDelay += avgDelay;
            flowCount++;

            if (verbose) {
                std::cout << "Flow " << it->first << ": "
                          << tuple.sourceAddress << " -> " << tuple.destinationAddress
                          << " | Throughput: " << std::fixed << std::setprecision(2) << throughput << " Mbps"
                          << " | Delay: " << avgDelay << " ms"
                          << " | Packets: " << fs.rxPackets << std::endl;
            }
        }
    }

    std::cout << "\n--- Summary ---" << std::endl;
    std::cout << "Total Flows: " << flowCount << std::endl;
    std::cout << "Total Throughput: " << std::fixed << std::setprecision(2) << totalThroughput << " Mbps" << std::endl;
    std::cout << "Total Packets: " << totalPackets << std::endl;
    std::cout << "Average Delay: " << std::fixed << std::setprecision(2)
              << (flowCount > 0 ? totalDelay / flowCount : 0) << " ms" << std::endl;

    Simulator::Destroy();

    NS_LOG_INFO("\n========================================================");
    NS_LOG_INFO("  Simulation Complete!");
    NS_LOG_INFO("========================================================\n");

    return 0;
}
