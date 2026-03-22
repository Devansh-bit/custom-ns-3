/*
 * This example demonstrates automatic multi-band, multi-width channel roaming using the
 * StaChannelHoppingHelper with DualPhySnifferHelper.
 *
 * Network topology:
 *   - 5 APs on mixed 2.4 GHz and 5 GHz bands with different channel widths:
 *     • AP1 (channel 1, 2.4 GHz, 20 MHz)
 *     • AP2 (channel 36, 5 GHz, 20 MHz)
 *     • AP3 (channel 38, 5 GHz, 40 MHz)
 *     • AP4 (channel 42, 5 GHz, 80 MHz)
 *     • AP5 (channel 50, 5 GHz, 160 MHz)
 *   - 1 STA with DualPhySniffer tracking all channels
 *   - STA initially associates with AP1 on channel 1 (2.4 GHz, 20 MHz)
 *   - At t=15s, forced disassociation triggers roaming
 *   - STA uses DualPhySniffer to scan both 2.4 GHz and 5 GHz bands
 *   - STA automatically roams to best SNR AP after scanning delay
 *   - Tests roaming across different bands and channel widths (2.4GHz-20MHz to 5GHz-20/40/80/160MHz)
 *   - Demonstrates explicit band specification with auto-width detection
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

NS_LOG_COMPONENT_DEFINE("StaChannelHoppingExample");

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

    std::cout << "\n+++ ASSOCIATION EVENT +++" << std::endl;
    std::cout << Simulator::Now().As(Time::S) << " STA Associated with " << apName
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

    std::cout << "\n--- DISASSOCIATION EVENT ---" << std::endl;
    std::cout << Simulator::Now().As(Time::S) << " STA Disassociated from " << apName
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
    std::cout << time.As(Time::S) << " STA initiating roaming:" << std::endl;
    std::cout << "  From: " << oldApName << " (" << oldBssid << ")" << std::endl;
    std::cout << "  To:   " << newApName << " (" << newBssid << ")" << std::endl;
    std::cout << "  Target SNR: " << snr << " dB" << std::endl;
    std::cout << ">>>>>>>>>>>>>>>>>>>>>>>>>\n" << std::endl;
}

// Force disassociation at specified time
void
ForceDisassociation(Ptr<WifiNetDevice> staDevice)
{
    std::cout << Simulator::Now().As(Time::S) << " Forcing disassociation..." << std::endl;
    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(staDevice->GetMac());
    if (staMac)
    {
        staMac->ForcedDisassociate();
    }
}

// Change AP channel at runtime
void
ChangeApChannel(Ptr<WifiNetDevice> apDevice, uint8_t newChannel, std::string apNumber)
{
    Mac48Address apMac = apDevice->GetMac()->GetAddress();
    std::cout << "\n*** CHANNEL CHANGE ***" << std::endl;
    std::cout << Simulator::Now().As(Time::S) << " Changing " << g_apNames[apMac]
              << " to channel " << (int)newChannel << "..." << std::endl;

    Ptr<WifiPhy> phy = apDevice->GetPhy();
    if (phy)
    {
        // Create channel settings string: {channel, width, band, primary20Index}
        // For 2.4 GHz: must specify width=20 explicitly (channels have multiple definitions: DSSS 22MHz, OFDM 20MHz, OFDM 40MHz)
        // For 5 GHz: can use width=0 for auto-width detection
        std::string bandStr;
        uint16_t width;
        if (newChannel >= 1 && newChannel <= 14)
        {
            bandStr = "BAND_2_4GHZ";
            width = 20; // Must be explicit for 2.4 GHz to avoid ambiguity
        }
        else
        {
            bandStr = "BAND_5GHZ";
            width = 0; // Auto-width for 5 GHz
        }

        std::ostringstream channelSettings;
        channelSettings << "{" << (int)newChannel << ", " << width << ", " << bandStr << ", 0}";

        // Switch to new channel
        phy->SetAttribute("ChannelSettings", StringValue(channelSettings.str()));

        // Update AP name in global map
        std::ostringstream newApName;
        newApName << apNumber << " (ch" << (int)newChannel << ")";
        g_apNames[apMac] = newApName.str();

        std::cout << Simulator::Now().As(Time::S) << " Channel changed successfully - now "
                  << g_apNames[apMac] << std::endl;
        std::cout << "**********************\n" << std::endl;
    }
}

// Move STA to a new position
void
MoveStaPosition(Ptr<Node> staNode, Vector newPosition)
{
    std::cout << Simulator::Now().As(Time::S) << " Moving STA to position ("
              << newPosition.x << ", " << newPosition.y << ", " << newPosition.z << ")..." << std::endl;

    Ptr<MobilityModel> mobility = staNode->GetObject<MobilityModel>();
    if (mobility)
    {
        mobility->SetPosition(newPosition);
        std::cout << Simulator::Now().As(Time::S) << " STA position updated" << std::endl;
    }
}

int
main(int argc, char* argv[])
{
    // Simulation parameters
    double simTime = 50.0;           // seconds
    double scanningDelay = 5.0;      // seconds
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

    // Create nodes: 5 APs (1 on 2.4 GHz, 4 on 5 GHz with different widths) + 1 STA
    NodeContainer apNodes;
    apNodes.Create(5);
    NodeContainer staNode;
    staNode.Create(1);

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

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                  "DataMode",
                                  StringValue("HeMcs7"),
                                  "ControlMode",
                                  StringValue("HeMcs0"));

    WifiMacHelper macHelper;
    Ssid ssid = Ssid("test-ssid");

    // Create APs on mixed 2.4 GHz and 5 GHz channels with different widths
    NetDeviceContainer apDevices;
    std::vector<uint8_t> channels = {1, 36, 38, 42, 50};  // 1x 2.4 GHz (20MHz) + 4x 5 GHz (20/40/80/160MHz)
    std::vector<std::string> bands = {"2.4GHz-20MHz", "5GHz-20MHz", "5GHz-40MHz", "5GHz-80MHz", "5GHz-160MHz"};
    std::vector<Mac48Address> apMacs;

    for (size_t i = 0; i < apNodes.GetN(); ++i)
    {
        // Determine band from channel number (ch 1-14: 2.4 GHz, others: 5 GHz)
        std::string bandStr = (channels[i] >= 1 && channels[i] <= 14) ? "BAND_2_4GHZ" : "BAND_5GHZ";

        // Set channel for this AP with explicit band, width=0 for auto-width
        std::ostringstream oss;
        oss << "{" << (int)channels[i] << ", 0, " << bandStr << ", 0}";
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
        apName << "AP" << (i + 1) << " (ch" << (int)channels[i] << " " << bands[i] << ")";
        g_apNames[apMac] = apName.str();

        NS_LOG_INFO("AP" << (i + 1) << " on channel " << (int)channels[i]
                         << " (" << bands[i] << ") BSSID: " << apMac);
    }

    // Create STA device (initially on channel 1, 2.4 GHz) with explicit band, width=0 for auto-width
    phyHelper.Set("ChannelSettings", StringValue("{1, 0, BAND_2_4GHZ, 0}"));
    macHelper.SetType("ns3::StaWifiMac",
                      "Ssid",
                      SsidValue(ssid),
                      "ActiveProbing",
                      BooleanValue(true));
    NetDeviceContainer staDevice = wifi.Install(phyHelper, macHelper, staNode);

    // Setup mobility - place all nodes close together to ensure good signal
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));    // AP1 (ch1, 2.4GHz) at center
    positionAlloc->Add(Vector(5.0, 0.0, 0.0));    // AP2 (ch6, 2.4GHz) nearby
    positionAlloc->Add(Vector(0.0, 5.0, 0.0));    // AP3 (ch11, 2.4GHz) nearby
    positionAlloc->Add(Vector(5.0, 5.0, 0.0));    // AP4 (ch36, 5GHz) nearby
    positionAlloc->Add(Vector(2.5, 2.5, 0.0));    // AP5 (ch44, 5GHz) at center
    positionAlloc->Add(Vector(1.0, 1.0, 0.0));    // STA initially close to AP1
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNodes);
    mobility.Install(staNode);

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(apNodes);
    internet.Install(staNode);

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apInterfaces = address.Assign(apDevices);
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevice);

    // Setup DualPhySniffer for multi-channel, multi-band scanning
    DualPhySnifferHelper* dualPhySniffer = new DualPhySnifferHelper();
    dualPhySniffer->SetChannel(Ptr<SpectrumChannel>(spectrumChannel));
    dualPhySniffer->SetScanningChannels({1, 36, 38, 42, 50}); // Scan both 2.4 GHz and 5 GHz channels with various widths
    dualPhySniffer->SetHopInterval(hopInterval);
    dualPhySniffer->SetSsid(ssid);

    // Install DualPhySniffer on STA
    Ptr<WifiNetDevice> staWifiDev = DynamicCast<WifiNetDevice>(staDevice.Get(0));
    Mac48Address staBssid = staWifiDev->GetMac()->GetAddress();
    dualPhySniffer->Install(staNode.Get(0), 1, staBssid); // Operating channel 1 (2.4 GHz)
    dualPhySniffer->StartChannelHopping();

    NS_LOG_INFO("DualPhySniffer installed on STA, scanning 2.4 GHz (1) and 5 GHz (36,38,42,50) with multiple widths");

    // Setup StaChannelHoppingHelper
    StaChannelHoppingHelper channelHoppingHelper;
    channelHoppingHelper.SetDualPhySniffer(dualPhySniffer);
    channelHoppingHelper.SetAttribute("ScanningDelay", TimeValue(Seconds(scanningDelay)));
    channelHoppingHelper.SetAttribute("MinimumSnr", DoubleValue(minimumSnr));
    channelHoppingHelper.SetAttribute("Enabled", BooleanValue(true));

    // Install on STA
    Ptr<StaChannelHoppingManager> manager = channelHoppingHelper.Install(staWifiDev);

    NS_LOG_INFO("StaChannelHoppingManager installed on STA");

    // Connect trace callbacks
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/Assoc",
                    MakeCallback(&AssocCallback));
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/DeAssoc",
                    MakeCallback(&DeAssocCallback));

    manager->TraceConnectWithoutContext("RoamingTriggered",
                                        MakeCallback(&RoamingTriggeredCallback));

    // Scenario: Test cross-band roaming
    // At t=15s: Force disassociation to trigger roaming
    Simulator::Schedule(Seconds(15.0), &ForceDisassociation, staWifiDev);

    // At t=30s: Change AP2 channel to test dynamic cross-band channel tracking
    Ptr<WifiNetDevice> ap2Device = DynamicCast<WifiNetDevice>(apDevices.Get(1));
    Simulator::Schedule(Seconds(30.0), &ChangeApChannel, ap2Device, 11, "AP2");  // ch36 (5GHz) → ch11 (2.4GHz)

    // Run simulation
    std::cout << "\n╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║   Multi-Band Multi-Width Roaming Test                    ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝\n" << std::endl;

    std::cout << "Network Configuration:" << std::endl;
    std::cout << "  AP1 (channel 1, 2.4 GHz, 20 MHz):    " << apMacs[0] << " at (0.0, 0.0, 0.0)m" << std::endl;
    std::cout << "  AP2 (channel 36, 5 GHz, 20 MHz):     " << apMacs[1] << " at (5.0, 0.0, 0.0)m" << std::endl;
    std::cout << "  AP3 (channel 38, 5 GHz, 40 MHz):     " << apMacs[2] << " at (0.0, 5.0, 0.0)m" << std::endl;
    std::cout << "  AP4 (channel 42, 5 GHz, 80 MHz):     " << apMacs[3] << " at (5.0, 5.0, 0.0)m" << std::endl;
    std::cout << "  AP5 (channel 50, 5 GHz, 160 MHz):    " << apMacs[4] << " at (2.5, 2.5, 0.0)m" << std::endl;
    std::cout << "\n  STA: " << staBssid << " at (1.0, 1.0, 0.0)m" << std::endl;
    std::cout << "       Initially associates with AP1 (channel 1, 2.4 GHz, 20 MHz)" << std::endl;

    std::cout << "\nSTA Configuration:" << std::endl;
    std::cout << "  Scanning channels: 1 (2.4 GHz, 20MHz) + 36, 38, 42, 50 (5 GHz, 20/40/80/160 MHz)" << std::endl;
    std::cout << "  Hop interval: " << hopInterval.As(Time::S) << std::endl;
    std::cout << "  Scanning delay: " << scanningDelay << " seconds" << std::endl;
    std::cout << "  Minimum SNR: " << minimumSnr << " dB" << std::endl;

    std::cout << "\nTest Scenario:" << std::endl;
    std::cout << "  t=0s:   STA associates with AP1 (ch1, 2.4 GHz, 20 MHz)" << std::endl;
    std::cout << "  t=15s:  Force disassociation → triggers roaming" << std::endl;
    std::cout << "          STA scans all channels (2.4 GHz + 5 GHz with various widths)" << std::endl;
    std::cout << "          and selects best AP (may switch to wider 5 GHz channel)" << std::endl;
    std::cout << "  t=30s:  AP2 switches bands: ch36 (5 GHz) → ch11 (2.4 GHz)" << std::endl;
    std::cout << "          Tests dynamic cross-band channel tracking" << std::endl;
    std::cout << std::endl;

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    delete dualPhySniffer;

    std::cout << std::endl << "Simulation finished." << std::endl;

    return 0;
}
