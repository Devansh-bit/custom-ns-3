/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * WiFi CCA Monitor Example with Per-Device Channel Utilization
 *
 * This example demonstrates per-device channel utilization monitoring using
 * sliding time windows.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-cca-monitor-helper.h"
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiCcaMonitorExample");

// =================================================================
// TRACE SINK FUNCTION
// =================================================================

/**
 * \brief Callback for channel utilization trace
 */
void
ChannelUtilizationTrace(uint32_t nodeId, double timestamp, double totalUtil,
                        double txUtil, double rxUtil, double ccaUtil,
                        double txTime, double rxTime, double ccaTime, double idleTime,
                        uint64_t bytesSent, uint64_t bytesReceived, double throughput)
{
    std::cout << std::fixed << std::setprecision(3)
              << "t=" << timestamp << "s | "
              << "Node " << nodeId << " | "
              << "ChanUtil=" << std::setw(5) << totalUtil << "% ("
              << "TX:" << std::setw(5) << txUtil << "% "
              << "RX:" << std::setw(5) << rxUtil << "% "
              << "CCA:" << std::setw(5) << ccaUtil << "%) | "
              << "Throughput=" << std::setw(7) << throughput << " Mbps | "
              << "Bytes TX:" << std::setw(8) << bytesSent << " RX:" << std::setw(8) << bytesReceived
              << std::endl;
}

int main(int argc, char *argv[])
{
    // ============================================================
    // SIMULATION PARAMETERS
    // ============================================================

    uint32_t nAps = 2;
    uint32_t nStaPerAp = 2;
    double dataRate = 5000.0;  // kbps
    uint32_t packetSize = 1472;
    double windowSize = 0.1;  // 100ms window
    double updateInterval = 0.1;  // Update every 100ms

    CommandLine cmd;
    cmd.AddValue("nAps", "Number of APs", nAps);
    cmd.AddValue("nStaPerAp", "Number of stations per AP", nStaPerAp);
    cmd.AddValue("dataRate", "Data rate per station in kbps", dataRate);
    cmd.AddValue("packetSize", "UDP packet size in bytes", packetSize);
    cmd.AddValue("windowSize", "Sliding window size in seconds", windowSize);
    cmd.AddValue("updateInterval", "Update interval in seconds", updateInterval);
    cmd.Parse(argc, argv);

    // ============================================================
    // CREATE NODES
    // ============================================================

    NodeContainer apNodes;
    apNodes.Create(nAps);

    uint32_t totalStations = nAps * nStaPerAp;
    NodeContainer staNodes;
    staNodes.Create(totalStations);

    std::cout << "\n=== WiFi CCA Monitor Example ===" << std::endl;
    std::cout << "APs: " << nAps << ", Stations per AP: " << nStaPerAp << std::endl;
    std::cout << "Window size: " << (windowSize * 1000) << " ms" << std::endl;
    std::cout << "Update interval: " << (updateInterval * 1000) << " ms\n" << std::endl;

    // ============================================================
    // CONFIGURE WIFI
    // ============================================================

    std::vector<std::string> channelSettings;
    std::vector<uint8_t> channelNumbers;

    for(uint32_t i = 0; i < nAps; i++)
    {
        uint8_t channel = 36 + (i * 4);
        channelNumbers.push_back(channel);
        channelSettings.push_back("{" + std::to_string(channel) + ", 0, BAND_5GHZ, 0}");
    }

    for(uint32_t i = 0; i < apNodes.GetN(); i++)
    {
        YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
        YansWifiPhyHelper phy;
        phy.SetErrorRateModel("ns3::NistErrorRateModel");
        phy.SetChannel(channel.Create());
        phy.Set("CcaEdThreshold", DoubleValue(-62.0));
        phy.Set("CcaSensitivity", DoubleValue(-82.0));
        phy.Set("ChannelSettings", StringValue(channelSettings[i]));

        WifiHelper wifi;
        wifi.SetStandard(WIFI_STANDARD_80211ax);
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode", StringValue("HeMcs5"),
                                     "ControlMode", StringValue("HeMcs0"));

        WifiMacHelper mac;
        Ssid ssid = Ssid("ns3-ap-" + std::to_string(i));
        mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));

        NetDeviceContainer apDevice = wifi.Install(phy, mac, apNodes.Get(i));

        mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));

        NodeContainer currentStations;
        for(uint32_t j = 0; j < nStaPerAp; j++)
        {
            currentStations.Add(staNodes.Get(i*nStaPerAp + j));
        }

        NetDeviceContainer staDevices = wifi.Install(phy, mac, currentStations);
    }

    // ============================================================
    // CONFIGURE MOBILITY
    // ============================================================

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                 "MinX", DoubleValue(0.0),
                                 "MinY", DoubleValue(0.0),
                                 "DeltaX", DoubleValue(30.0),
                                 "DeltaY", DoubleValue(0.0),
                                 "GridWidth", UintegerValue(nAps),
                                 "LayoutType", StringValue("RowFirst"));
    mobility.Install(apNodes);

    for(uint32_t i = 0; i < nAps; i++)
    {
        double apX = i * 30.0;
        uint32_t gridWidth = (uint32_t)std::ceil(std::sqrt(nStaPerAp));
        double spacing = 3.0;
        double gridXSize = gridWidth * spacing;

        mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                    "MinX", DoubleValue(apX - gridXSize/2.0),
                                    "MinY", DoubleValue(10.0),
                                    "DeltaX", DoubleValue(spacing),
                                    "DeltaY", DoubleValue(spacing),
                                    "GridWidth", UintegerValue(gridWidth),
                                    "LayoutType", StringValue("RowFirst"));
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

        NodeContainer bssStations;
        for(uint32_t j = 0; j < nStaPerAp; j++)
        {
            bssStations.Add(staNodes.Get(i*nStaPerAp + j));
        }
        mobility.Install(bssStations);
    }

    // ============================================================
    // INSTALL INTERNET STACK
    // ============================================================

    InternetStackHelper stack;
    stack.Install(apNodes);
    stack.Install(staNodes);

    Ipv4AddressHelper address;
    for(uint32_t i = 0; i < apNodes.GetN(); i++)
    {
        std::string subnet = "10.1." + std::to_string(i+1) + ".0";
        address.SetBase(subnet.c_str(), "255.255.255.0");

        Ptr<NetDevice> apDev = apNodes.Get(i)->GetDevice(0);
        NetDeviceContainer bssDevices;
        bssDevices.Add(apDev);

        for(uint32_t j = 0; j < nStaPerAp; j++)
        {
            Ptr<NetDevice> staDev = staNodes.Get(i*nStaPerAp + j)->GetDevice(0);
            bssDevices.Add(staDev);
        }

        address.Assign(bssDevices);
    }

    // ============================================================
    // CONFIGURE TRAFFIC (UPLINK)
    // ============================================================

    uint16_t port = 8080;
    double bitsPerPacket = packetSize * 8.0;
    double packetsPerSecond = (dataRate * 1000.0) / bitsPerPacket;
    uint32_t intervalMicroseconds = (uint32_t)(1000000.0 / packetsPerSecond);

    for(uint32_t i = 0; i < apNodes.GetN(); i++)
    {
        UdpServerHelper server(port);
        ApplicationContainer serverApp = server.Install(apNodes.Get(i));
        serverApp.Start(Seconds(1.0));
        serverApp.Stop(Seconds(10.0));

        for(uint32_t j = 0; j < nStaPerAp; j++)
        {
            uint32_t staIndex = i*nStaPerAp + j;
            Ptr<Node> staNode = staNodes.Get(staIndex);

            Ptr<Ipv4> apIpv4 = apNodes.Get(i)->GetObject<Ipv4>();
            Ipv4Address apAddr = apIpv4->GetAddress(1, 0).GetLocal();

            UdpClientHelper client(apAddr, port);
            client.SetAttribute("MaxPackets", UintegerValue(10000000));
            client.SetAttribute("Interval", TimeValue(MicroSeconds(intervalMicroseconds)));
            client.SetAttribute("PacketSize", UintegerValue(packetSize));

            ApplicationContainer clientApp = client.Install(staNode);
            clientApp.Start(Seconds(2.0));
            clientApp.Stop(Seconds(10.0));
        }
    }

    // ============================================================
    // INSTALL CHANNEL UTILIZATION MONITORS
    // ============================================================

    WifiCcaMonitorHelper monitorHelper;
    monitorHelper.SetWindowSize(Seconds(windowSize));
    monitorHelper.SetUpdateInterval(Seconds(updateInterval));

    std::cout << "=== Installing monitors on selected devices ===" << std::endl;

    // Keep monitors alive (store in vector)
    std::vector<Ptr<WifiCcaMonitor>> monitors;

    // Install on all APs
    for(uint32_t i = 0; i < apNodes.GetN(); i++)
    {
        Ptr<NetDevice> apDev = apNodes.Get(i)->GetDevice(0);
        Ptr<WifiCcaMonitor> monitor = monitorHelper.Install(apDev);
        monitor->TraceConnectWithoutContext("ChannelUtilization",
                                            MakeCallback(&ChannelUtilizationTrace));
        monitors.push_back(monitor);
        std::cout << "  - Monitoring AP " << i << " (Node " << apNodes.Get(i)->GetId() << ")" << std::endl;
    }

    // Install on first station of each BSS
    for(uint32_t i = 0; i < nAps; i++)
    {
        Ptr<NetDevice> staDev = staNodes.Get(i*nStaPerAp)->GetDevice(0);
        Ptr<WifiCcaMonitor> monitor = monitorHelper.Install(staDev);
        monitor->TraceConnectWithoutContext("ChannelUtilization",
                                            MakeCallback(&ChannelUtilizationTrace));
        monitors.push_back(monitor);
        std::cout << "  - Monitoring STA " << (i*nStaPerAp) << " (Node " << staNodes.Get(i*nStaPerAp)->GetId() << ")" << std::endl;
    }

    std::cout << "\n=== Starting Simulation ===" << std::endl;
    std::cout << "t=time | Node ID | Channel Utilization (total, TX, RX, CCA) | Throughput | Bytes\n" << std::endl;

    // ============================================================
    // RUN SIMULATION
    // ============================================================

    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "\n=== Simulation Complete ===" << std::endl;

    return 0;
}
