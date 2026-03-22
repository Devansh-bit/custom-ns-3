/*
 * Test channel switching propagation from AP to STAs using LeverApi
 *
 * Topology:
 *   - 1 AP, 5 STAs (WiFi 6 / 802.11ax)
 *   - STAs send UDP traffic to AP
 *   - Channel switch from ch1 -> ch6 at t=5s
 *   - Verify traffic continues after switch
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/lever-api-helper.h"
#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TestPropagation");

// Global packet counter
uint32_t g_packetsReceived = 0;
uint32_t g_packetsBeforeSwitch = 0;
uint32_t g_packetsAfterSwitch = 0;
bool g_switchOccurred = false;

// Callback functions
void
OnStaAssociated(uint32_t staIndex, Mac48Address apBssid)
{
    std::cout << "[ASSOC] Time=" << Simulator::Now().GetSeconds()
              << "s STA_" << staIndex
              << " associated with AP BSSID=" << apBssid << std::endl;
}

void
OnStaDeassociated(uint32_t staIndex, Mac48Address apBssid)
{
    std::cout << "[DEASSOC] Time=" << Simulator::Now().GetSeconds()
              << "s STA_" << staIndex
              << " deassociated from AP BSSID=" << apBssid << std::endl;
}

void
OnPacketReceived(Ptr<const Packet> packet, const Address& from)
{
    g_packetsReceived++;
    if (!g_switchOccurred)
    {
        g_packetsBeforeSwitch++;
    }
    else
    {
        g_packetsAfterSwitch++;
    }
}

void
OnPacketReceivedSimple(Ptr<const Packet> packet)
{
    g_packetsReceived++;
    if (!g_switchOccurred)
    {
        g_packetsBeforeSwitch++;
    }
    else
    {
        g_packetsAfterSwitch++;
    }
}

void
DoChannelSwitch(Ptr<LeverApi> leverApi)
{
    std::cout << "\n========== CHANNEL SWITCH at t=" << Simulator::Now().GetSeconds()
              << "s ==========" << std::endl;
    std::cout << "Switching from channel 36 to channel 40..." << std::endl;
    leverApi->SwitchChannel(40);  // Switch to channel 40 (5GHz)
    g_switchOccurred = true;
    std::cout << "Channel switch command issued\n" << std::endl;
}

void
PrintStatistics()
{
    std::cout << "\n========== SIMULATION STATISTICS ==========" << std::endl;
    std::cout << "Total packets received: " << g_packetsReceived << std::endl;
    std::cout << "Packets before switch (0-5s): " << g_packetsBeforeSwitch << std::endl;
    std::cout << "Packets after switch (5-10s): " << g_packetsAfterSwitch << std::endl;

    if (g_packetsAfterSwitch > 0)
    {
        std::cout << "✓ SUCCESS: Traffic continued after channel switch!" << std::endl;
    }
    else
    {
        std::cout << "✗ FAILURE: No traffic after channel switch!" << std::endl;
    }
    std::cout << "===========================================\n" << std::endl;
}

int
main(int argc, char* argv[])
{
    double simTime = 10.0;
    double switchTime = 5.0;
    uint32_t nStas = 5;

    // Parse command line
    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
    cmd.AddValue("switchTime", "Time to perform channel switch (seconds)", switchTime);
    cmd.AddValue("nStas", "Number of STAs", nStas);
    cmd.Parse(argc, argv);

    // Enable logging
    LogComponentEnable("TestPropagation", LOG_LEVEL_INFO);

    std::cout << "\n========== TEST PROPAGATION SIMULATION ==========" << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  - AP nodes: 1" << std::endl;
    std::cout << "  - STA nodes: " << nStas << std::endl;
    std::cout << "  - Initial channel: 36 (5GHz)" << std::endl;
    std::cout << "  - Switch to channel: 40 (5GHz)" << std::endl;
    std::cout << "  - Switch time: " << switchTime << "s" << std::endl;
    std::cout << "  - Total simulation time: " << simTime << "s" << std::endl;
    std::cout << "================================================\n" << std::endl;

    // Create nodes
    NodeContainer apNode;
    apNode.Create(1);
    NodeContainer staNodes;
    staNodes.Create(nStas);

    // Create YANS WiFi channel
    YansWifiChannelHelper channelHelper = YansWifiChannelHelper::Default();

    // Configure WiFi PHY with channel 36 (5GHz)
    YansWifiPhyHelper phyHelper;
    phyHelper.SetChannel(channelHelper.Create());
    phyHelper.Set("ChannelSettings", StringValue("{36, 20, BAND_5GHZ, 0}"));

    // Configure WiFi with 802.11n standard
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                  "DataMode", StringValue("HtMcs7"),
                                  "ControlMode", StringValue("HtMcs0"));

    WifiMacHelper macHelper;
    Ssid ssid = Ssid("test-network");

    // Create AP device
    macHelper.SetType("ns3::ApWifiMac",
                      "Ssid", SsidValue(ssid),
                      "BeaconGeneration", BooleanValue(true),
                      "BeaconInterval", TimeValue(MicroSeconds(102400)));  // 100 * 1024us = 102.4ms
    NetDeviceContainer apDevice = wifi.Install(phyHelper, macHelper, apNode);

    // Create STA devices with same PHY settings
    macHelper.SetType("ns3::StaWifiMac",
                      "Ssid", SsidValue(ssid),
                      "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phyHelper, macHelper, staNodes);


    // Setup mobility - fixed positions
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));    // AP at origin
    positionAlloc->Add(Vector(5.0, 0.0, 0.0));    // STA 0
    positionAlloc->Add(Vector(0.0, 5.0, 0.0));    // STA 1
    positionAlloc->Add(Vector(-5.0, 0.0, 0.0));   // STA 2
    positionAlloc->Add(Vector(0.0, -5.0, 0.0));   // STA 3
    positionAlloc->Add(Vector(3.5, 3.5, 0.0));    // STA 4

    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNode);
    mobility.Install(staNodes);

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(apNode);
    internet.Install(staNodes);

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apInterface = address.Assign(apDevice);
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);

    // Populate ARP cache to avoid delays
    Ptr<ArpCache> arpCache = CreateObject<ArpCache>();
    arpCache->SetAliveTimeout(Seconds(3600));
    for (uint32_t i = 0; i < staNodes.GetN(); i++)
    {
        Ptr<Ipv4L3Protocol> ip = staNodes.Get(i)->GetObject<Ipv4L3Protocol>();
        NS_ASSERT(ip);
        int32_t interface = ip->GetInterfaceForDevice(staDevices.Get(i));
        NS_ASSERT(interface != -1);
        Ptr<Ipv4Interface> ipv4Interface = ip->GetInterface(interface);
        ipv4Interface->SetArpCache(arpCache);

        // Add AP to ARP cache
        ArpCache::Entry* entry = arpCache->Add(apInterface.GetAddress(0));
        Ptr<WifiNetDevice> apWifiDev = DynamicCast<WifiNetDevice>(apDevice.Get(0));
        entry->SetMacAddress(apWifiDev->GetAddress());
        entry->MarkPermanent();
    }

    std::cout << "Network setup complete" << std::endl;
    std::cout << "AP IP: " << apInterface.GetAddress(0) << std::endl;
    for (uint32_t i = 0; i < staInterfaces.GetN(); i++)
    {
        std::cout << "STA_" << i << " IP: " << staInterfaces.GetAddress(i) << std::endl;
    }
    std::cout << std::endl;

    // Register association/deassociation callbacks
    for (uint32_t i = 0; i < staDevices.GetN(); i++)
    {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(wifiDev->GetMac());
        if (staMac)
        {
            staMac->TraceConnectWithoutContext("Assoc",
                                                MakeBoundCallback(&OnStaAssociated, i));
            staMac->TraceConnectWithoutContext("DeAssoc",
                                                MakeBoundCallback(&OnStaDeassociated, i));
        }
    }

    // Install LeverApi on AP
    Ptr<LeverConfig> config = CreateObject<LeverConfig>();
    config->SetTxPower(20.0);
    config->SetCcaEdThreshold(-82.0);
    config->SetRxSensitivity(-91.0);

    LeverApiHelper leverHelper(config);
    ApplicationContainer leverApp = leverHelper.Install(apNode.Get(0));
    leverApp.Start(Seconds(0.0));
    leverApp.Stop(Seconds(simTime));

    Ptr<LeverApi> leverApi = leverApp.Get(0)->GetObject<LeverApi>();

    // Setup UDP traffic - PacketSink on AP
    uint16_t port = 9;
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sinkHelper.Install(apNode.Get(0));
    sinkApp.Start(Seconds(0.5));
    sinkApp.Stop(Seconds(simTime));

    // Connect packet received trace
    Ptr<PacketSink> packetSink = DynamicCast<PacketSink>(sinkApp.Get(0));
    packetSink->TraceConnectWithoutContext("Rx", MakeCallback(&OnPacketReceived));

    // Setup UDP traffic - Clients on STAs
    for (uint32_t i = 0; i < staNodes.GetN(); i++)
    {
        UdpClientHelper client(apInterface.GetAddress(0), port);
        client.SetAttribute("MaxPackets", UintegerValue(4294967295u));
        client.SetAttribute("Interval", TimeValue(MilliSeconds(10)));  // 100 pkt/s
        client.SetAttribute("PacketSize", UintegerValue(1024));

        ApplicationContainer clientApp = client.Install(staNodes.Get(i));
        clientApp.Start(Seconds(3.0 + i * 0.1));  // Stagger starts, wait for association
        clientApp.Stop(Seconds(simTime));
    }

    std::cout << "UDP traffic configured:" << std::endl;
    std::cout << "  - Server on AP: " << apInterface.GetAddress(0) << ":" << port << std::endl;
    std::cout << "  - " << nStas << " clients sending 1024-byte packets at 100 pkt/s" << std::endl;
    std::cout << "  - Expected rate: ~" << (nStas * 100) << " pkt/s total\n" << std::endl;

    // Schedule channel switch
    Simulator::Schedule(Seconds(switchTime), &DoChannelSwitch, leverApi);

    // Print statistics at the end
    Simulator::Schedule(Seconds(simTime - 0.001), &PrintStatistics);

    // Enable ASCII tracing for debugging
    AsciiTraceHelper ascii;
    phyHelper.EnableAsciiAll(ascii.CreateFileStream("test-propagation.tr"));

    // Run simulation
    std::cout << "Starting simulation...\n" << std::endl;
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "\nTrace file written to: test-propagation.tr" << std::endl;

    return 0;
}
