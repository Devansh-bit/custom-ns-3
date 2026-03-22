/*
 * This example demonstrates automatic multi-channel roaming for multiple STAs
 * using StaChannelHoppingHelper with DualPhySnifferHelper.
 *
 * Network topology:
 *   - 3 APs: AP1 (channel 1→6), AP2 (channel 6), AP3 (channel 11)
 *   - 2 STAs with DualPhySniffer tracking all channels (1, 6, 11)
 *   - Both STAs initially associate with AP1 on channel 1 at position (0, 0, 0)
 *   - At t=10s, AP1 changes to channel 6 (causing disassociation)
 *   - At t=10s, STA1 moves to (45m, 0, 0) close to AP2
 *   - At t=10s, STA2 moves to (95m, 0, 0) close to AP3
 *   - Both STAs use DualPhySniffer to find available APs on all channels
 *   - STA1 automatically roams to AP2 (channel 6)
 *   - STA2 automatically roams to AP3 (channel 11)
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/propagation-module.h"
#include "ns3/sta-channel-hopping-helper.h"
#include "ns3/dual-phy-sniffer-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MultiStaChannelHoppingExample");

// Global map to track AP names
std::map<Mac48Address, std::string> g_apNames;

// Callback for association events
void
AssocCallback(std::string context, Mac48Address bssid)
{
    std::string apName = "Unknown";
    if (g_apNames.find(bssid) != g_apNames.end())
    {
        apName = g_apNames[bssid];
    }

    // Extract STA ID from context (e.g., "/NodeList/3/..." -> node 3)
    std::string staId = "STA";
    size_t nodeListPos = context.find("/NodeList/");
    if (nodeListPos != std::string::npos)
    {
        size_t startPos = nodeListPos + 10; // length of "/NodeList/"
        size_t endPos = context.find("/", startPos);
        if (endPos != std::string::npos)
        {
            std::string nodeNum = context.substr(startPos, endPos - startPos);
            staId = "STA" + std::to_string(std::stoi(nodeNum) - 2); // Nodes 0-2 are APs, 3+ are STAs
        }
    }

    std::cout << "\n+++ ASSOCIATION EVENT +++" << std::endl;
    std::cout << Simulator::Now().As(Time::S) << " " << staId << " Associated with " << apName
              << " (BSSID: " << bssid << ")" << std::endl;
    std::cout << "++++++++++++++++++++++++++\n" << std::endl;
}

// Callback for disassociation events
void
DeAssocCallback(std::string context, Mac48Address bssid)
{
    std::string apName = "Unknown";
    if (g_apNames.find(bssid) != g_apNames.end())
    {
        apName = g_apNames[bssid];
    }

    // Extract STA ID from context
    std::string staId = "STA";
    size_t nodeListPos = context.find("/NodeList/");
    if (nodeListPos != std::string::npos)
    {
        size_t startPos = nodeListPos + 10;
        size_t endPos = context.find("/", startPos);
        if (endPos != std::string::npos)
        {
            std::string nodeNum = context.substr(startPos, endPos - startPos);
            staId = "STA" + std::to_string(std::stoi(nodeNum) - 2);
        }
    }

    std::cout << "\n--- DISASSOCIATION EVENT ---" << std::endl;
    std::cout << Simulator::Now().As(Time::S) << " " << staId << " Disassociated from " << apName
              << " (BSSID: " << bssid << ")" << std::endl;
    std::cout << "----------------------------\n" << std::endl;
}

// Callback for roaming triggered events
void
RoamingTriggeredCallback(Time time,
                         Mac48Address staAddress,
                         Mac48Address oldBssid,
                         Mac48Address newBssid,
                         double snr)
{
    std::string oldApName = "None";
    std::string newApName = "Unknown";

    if (g_apNames.find(oldBssid) != g_apNames.end())
    {
        oldApName = g_apNames[oldBssid];
    }
    if (g_apNames.find(newBssid) != g_apNames.end())
    {
        newApName = g_apNames[newBssid];
    }

    std::cout << "\n>>> ROAMING TRIGGERED <<<" << std::endl;
    std::cout << time.As(Time::S) << " STA (" << staAddress << ") initiating roaming:" << std::endl;
    std::cout << "  From: " << oldApName << " (" << oldBssid << ")" << std::endl;
    std::cout << "  To:   " << newApName << " (" << newBssid << ")" << std::endl;
    std::cout << "  Target SNR: " << snr << " dB" << std::endl;
    std::cout << ">>>>>>>>>>>>>>>>>>>>>>>>>>\n" << std::endl;
}

// Change AP channel at runtime
void
ChangeApChannel(Ptr<WifiNetDevice> apDevice, uint8_t newChannel, std::string apNumber)
{
    Mac48Address apMac = apDevice->GetMac()->GetAddress();
    std::cout << "\n*** CHANNEL CHANGE ***" << std::endl;
    std::cout << Simulator::Now().As(Time::S) << " Changing " << g_apNames[apMac]
              << " to channel " << (int)newChannel << "..." << std::endl;

    apDevice->GetMac()->SwitchChannel(newChannel);

    // Update AP name in global map
    std::ostringstream newApName;
    newApName << apNumber << " (ch" << (int)newChannel << ")";
    g_apNames[apMac] = newApName.str();

    std::cout << Simulator::Now().As(Time::S) << " Channel changed successfully - now "
              << g_apNames[apMac] << std::endl;
    std::cout << "**********************\n" << std::endl;
}

// Move STA to a new position
void
MoveStaPosition(Ptr<Node> staNode, Vector newPosition, std::string staName)
{
    std::cout << Simulator::Now().As(Time::S) << " Moving " << staName << " to position ("
              << newPosition.x << ", " << newPosition.y << ", " << newPosition.z << ")..." << std::endl;

    Ptr<MobilityModel> mobility = staNode->GetObject<MobilityModel>();
    if (mobility)
    {
        mobility->SetPosition(newPosition);
        std::cout << Simulator::Now().As(Time::S) << " " << staName << " position updated" << std::endl;
    }
}

int
main(int argc, char* argv[])
{
    // Simulation parameters
    double simTime = 30.0;           // seconds
    double scanningDelay = 5;      // seconds
    double minimumSnr = 0.0;         // dB
    Time hopInterval = Seconds(1.0); // channel hopping interval
    bool verbose = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Total simulation time (s)", simTime);
    cmd.AddValue("scanningDelay", "Delay before reconnection attempt (s)", scanningDelay);
    cmd.AddValue("minimumSnr", "Minimum SNR threshold for AP selection (dB)", minimumSnr);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("StaChannelHoppingManager", LOG_LEVEL_INFO);
        LogComponentEnable("StaChannelHoppingHelper", LOG_LEVEL_INFO);
        LogComponentEnable("DualPhySnifferHelper", LOG_LEVEL_INFO);
    }

    // Create nodes
    NodeContainer apNodes;
    apNodes.Create(3);
    NodeContainer staNodes;
    staNodes.Create(2);

    // Create spectrum channel
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
    Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(lossModel);
    Ptr<ConstantSpeedPropagationDelayModel> delayModel =
        CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);

    // Configure WiFi PHY and MAC
    SpectrumWifiPhyHelper phyHelper;
    phyHelper.SetChannel(spectrumChannel);
    phyHelper.Set("ChannelSettings", StringValue("{0, 20, BAND_2_4GHZ, 0}"));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                  "DataMode",
                                  StringValue("HeMcs7"),
                                  "ControlMode",
                                  StringValue("HeMcs0"));

    WifiMacHelper macHelper;
    Ssid ssid = Ssid("multi-sta-test");

    // Create APs on different channels
    NetDeviceContainer apDevices;
    std::vector<uint8_t> channels = {1, 6, 11};
    std::vector<Mac48Address> apMacs;

    for (size_t i = 0; i < apNodes.GetN(); ++i)
    {
        // Set channel for this AP
        std::ostringstream oss;
        oss << "{" << (int)channels[i] << ", 20, BAND_2_4GHZ, 0}";
        phyHelper.Set("ChannelSettings", StringValue(oss.str()));

        // Configure AP MAC
        macHelper.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));

        // Install on AP node
        NetDeviceContainer dev = wifi.Install(phyHelper, macHelper, apNodes.Get(i));
        apDevices.Add(dev);

        // Store MAC address
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(dev.Get(0));
        Mac48Address apMac = wifiDev->GetMac()->GetAddress();
        apMacs.push_back(apMac);

        // Store AP name for callbacks
        std::ostringstream apName;
        apName << "AP" << (i + 1) << " (ch" << (int)channels[i] << ")";
        g_apNames[apMac] = apName.str();

        NS_LOG_INFO("AP" << i << " on channel " << (int)channels[i] << " BSSID: " << apMac);
    }

    // Create STA devices (both initially on channel 1)
    NetDeviceContainer staDevices;
    std::vector<Mac48Address> staMacs;

    for (size_t i = 0; i < staNodes.GetN(); ++i)
    {
        phyHelper.Set("ChannelSettings", StringValue("{1, 20, BAND_2_4GHZ, 0}"));
        macHelper.SetType("ns3::StaWifiMac",
                          "Ssid",
                          SsidValue(ssid),
                          "ActiveProbing",
                          BooleanValue(true));
        NetDeviceContainer dev = wifi.Install(phyHelper, macHelper, staNodes.Get(i));
        staDevices.Add(dev);

        // Store MAC address
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(dev.Get(0));
        Mac48Address staMac = wifiDev->GetMac()->GetAddress();
        staMacs.push_back(staMac);

        NS_LOG_INFO("STA" << i << " MAC: " << staMac);
    }

    // Setup mobility - APs in a line, STAs initially at origin
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));   // AP1 at 0m
    positionAlloc->Add(Vector(50.0, 0.0, 0.0));  // AP2 at 50m
    positionAlloc->Add(Vector(100.0, 0.0, 0.0)); // AP3 at 100m
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));   // STA1 at 0m (initially close to AP1)
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));   // STA2 at 0m (initially close to AP1)
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNodes);
    mobility.Install(staNodes);

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(apNodes);
    internet.Install(staNodes);

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apInterfaces = address.Assign(apDevices);
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);

    // Setup DualPhySniffer for each STA
    std::vector<DualPhySnifferHelper*> dualPhySniffers;
    std::vector<Ptr<StaChannelHoppingManager>> managers;

    for (size_t i = 0; i < staNodes.GetN(); ++i)
    {
        // Create DualPhySniffer for this STA
        DualPhySnifferHelper* sniffer = new DualPhySnifferHelper();
        sniffer->SetChannel(Ptr<SpectrumChannel>(spectrumChannel));
        sniffer->SetScanningChannels({1, 6, 11}); // Scan all three channels
        sniffer->SetHopInterval(hopInterval);
        sniffer->SetSsid(ssid);

        // Install DualPhySniffer on STA
        Ptr<WifiNetDevice> staWifiDev = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        sniffer->Install(staNodes.Get(i), 1, staMacs[i]); // Operating channel 1
        sniffer->StartChannelHopping();

        dualPhySniffers.push_back(sniffer);

        NS_LOG_INFO("DualPhySniffer installed on STA" << i);

        // Setup StaChannelHoppingHelper for this STA
        StaChannelHoppingHelper channelHoppingHelper;
        channelHoppingHelper.SetDualPhySniffer(sniffer);
        channelHoppingHelper.SetAttribute("ScanningDelay", TimeValue(Seconds(scanningDelay)));
        channelHoppingHelper.SetAttribute("MinimumSnr", DoubleValue(minimumSnr));
        channelHoppingHelper.SetAttribute("Enabled", BooleanValue(true));

        // Install on STA
        Ptr<StaChannelHoppingManager> manager = channelHoppingHelper.Install(staWifiDev);
        managers.push_back(manager);

        NS_LOG_INFO("StaChannelHoppingManager installed on STA" << i);

        // Connect roaming trace for this STA
        manager->TraceConnectWithoutContext("RoamingTriggered",
                                            MakeCallback(&RoamingTriggeredCallback));
    }

    // Connect trace callbacks for association/disassociation
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/Assoc",
                    MakeCallback(&AssocCallback));
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/DeAssoc",
                    MakeCallback(&DeAssocCallback));

    // At t=10s: Change AP1 channel AND move STAs to different positions
    // STA1 moves close to AP2 (channel 6), STA2 moves far away to -100m
    Ptr<WifiNetDevice> ap1Device = DynamicCast<WifiNetDevice>(apDevices.Get(0));
    Simulator::Schedule(Seconds(10.0), &ChangeApChannel, ap1Device, 6, "AP1");
    Simulator::Schedule(Seconds(10.0), &MoveStaPosition, staNodes.Get(0), Vector(45.0, 0.0, 0.0), "STA1");
    Simulator::Schedule(Seconds(10.0), &MoveStaPosition, staNodes.Get(1), Vector(-1000.0, 0.0, 0.0), "STA2");

    // At t=15s: Move STA2 back close to AP3
    Simulator::Schedule(Seconds(15.0), &MoveStaPosition, staNodes.Get(1), Vector(95.0, 0.0, 0.0), "STA2");

    // Run simulation
    std::cout << "Starting simulation..." << std::endl;
    std::cout << "Network topology:" << std::endl;
    std::cout << "  AP1 (channel 1 → 6 at t=10s): " << apMacs[0] << " at (0m, 0m, 0m)" << std::endl;
    std::cout << "  AP2 (channel 6): " << apMacs[1] << " at (50m, 0m, 0m)" << std::endl;
    std::cout << "  AP3 (channel 11): " << apMacs[2] << " at (100m, 0m, 0m)" << std::endl;
    std::cout << "  STA1: " << staMacs[0] << std::endl;
    std::cout << "    Initial position: (0m, 0m, 0m) - close to AP1" << std::endl;
    std::cout << "    At t=10s, moves to: (45m, 0m, 0m) - close to AP2" << std::endl;
    std::cout << "  STA2: " << staMacs[1] << std::endl;
    std::cout << "    Initial position: (0m, 0m, 0m) - close to AP1" << std::endl;
    std::cout << "    At t=10s, moves to: (-100m, 0m, 0m) - far from all APs" << std::endl;
    std::cout << "    At t=15s, moves to: (95m, 0m, 0m) - close to AP3" << std::endl;
    std::cout << "Both STAs scanning channels: 1, 6, 11" << std::endl;
    std::cout << "Scanning delay: " << scanningDelay << " seconds" << std::endl;
    std::cout << std::endl;

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    // Cleanup
    for (auto* sniffer : dualPhySniffers)
    {
        delete sniffer;
    }

    std::cout << std::endl << "Simulation finished." << std::endl;

    return 0;
}
