#include "ns3/core-module.h"
#include "ns3/bss_tm_11v-helper.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/beacon-neighbor-model.h"

using namespace ns3;

std::vector<BeaconReportData> 
GetDummyBeaconReports()
{
    std::vector<BeaconReportData> reports;

    BeaconReportData br1 = {
        Mac48Address("00:11:22:33:44:55"),
        36,    // channel
        1,     // regulatoryClass
        180,   // rcpi
        40,    // rsni
        0x01,  // reportedFrameInfo
        50,    // measurementDuration
        1000000, // TSF
        1,     // antennaID
        123456 // parentTSF
    };

    BeaconReportData br2 = {
        Mac48Address("66:77:88:99:AA:BB"),
        40,
        1,
        210,
        45,
        0x01,
        55,
        2000000,
        1,
        234567
    };

    BeaconReportData br3 = {
        Mac48Address("CC:DD:EE:FF:00:11"),
        44,
        1,
        160,
        35,
        0x01,
        60,
        3000000,
        1,
        345678
    };

    reports.push_back(br1);
    reports.push_back(br2);
    reports.push_back(br3);

    return reports;
}

NS_LOG_COMPONENT_DEFINE("BSSTransitionManagementExample");

//Global Variables
Ptr<WifiNetDevice> g_apDevice;
Ptr<WifiNetDevice> g_staDevice;

// Add this callback function:
void
PhyTxCallback(Ptr<const Packet> packet, double txPowerW)
{
    NS_LOG_INFO("[PHY-TX] AP transmitting packet, size=" << packet->GetSize() 
                << " bytes, power=" << txPowerW << "W at t=" 
                << Simulator::Now().GetSeconds() << "s");
}

// ===== SNIFFER #1: Connection Quality Monitor (Main Script) =====
void
ConnectionQualitySniffer(std::string context,
                         Ptr<const Packet> packet,
                         uint16_t channelFreq,
                         WifiTxVector txVector,
                         MpduInfo mpdu,
                         SignalNoiseDbm signalNoise,
                         uint16_t staId)
{
    WifiMacHeader hdr;
    Ptr<Packet> copy = packet->Copy();
    copy->RemoveHeader(hdr);

    Mac48Address src = hdr.GetAddr2();

    if (!hdr.IsBeacon())
    {
        return;
    }

    Mac48Address bssid = src;
    if (bssid != g_apDevice->GetMac()->GetAddress())
    {
        return;
    }

    double rssi = signalNoise.signal;
    double snr = rssi - signalNoise.noise;

    Ptr<MobilityModel> staMobility = g_staDevice->GetNode()->GetObject<MobilityModel>();
    Ptr<MobilityModel> apMobility = g_apDevice->GetNode()->GetObject<MobilityModel>();

    double distance = 0.0;
    if (staMobility && apMobility)
    {
        distance = staMobility->GetDistanceFrom(apMobility);
    }

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   Connected AP Link Quality          ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";
    std::cout << "  Time: " << Simulator::Now().GetSeconds() << "s\n";
    std::cout << "  Distance: " << distance << " m\n";
    std::cout << "  RSSI: " << rssi << " dBm\n";
    std::cout << "  SNR: " << snr << " dB\n";
}


int
main(int argc, char* argv[])
{
    // In your main function, after creating the AP device:

    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║  BSS TM Example       ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n\n";

    // Configuration
    uint32_t nStations = 1;
    uint32_t nAPs = 1;
    double apDistance = 5.0;
    double rssiThreshold = -60.0;
    Time simulationTime = Seconds(10);
    bool verbose = true;

    double startDistance = 10.0;
    double distanceStep = 10.0;
    double timeInterval = 1.0;
    double maxDistance = 70.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nAPs", "Number of Access Points", nAPs);
    cmd.AddValue("apDistance", "Distance between APs (m)", apDistance);
    cmd.AddValue("rssi", "RSSI threshold (dBm)", rssiThreshold);
    cmd.AddValue("time", "Simulation duration (s)", simulationTime);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("BssTm11vHelper", LOG_LEVEL_INFO);
        LogComponentEnable("rankListManager", LOG_LEVEL_INFO);
    }

    // Create nodes
    NodeContainer wifiStaNodes, wifiApNodes;
    wifiStaNodes.Create(nStations);
    wifiApNodes.Create(nAPs);

    // WiFi setup
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("ChannelSettings", StringValue("{36, 20, BAND_5GHZ, 0}"));

    WifiMacHelper mac;
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("HtMcs7"),
                                 "ControlMode",
                                 StringValue("HtMcs0"));

    Ssid ssid = Ssid("ns3-80211k");

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, wifiStaNodes);

    mac.SetType("ns3::ApWifiMac",
                "Ssid",
                SsidValue(ssid),
                "BeaconGeneration",
                BooleanValue(true),
                "BeaconInterval",
                TimeValue(MicroSeconds(102400)));
    NetDeviceContainer apDevices = wifi.Install(phy, mac, wifiApNodes);

    g_staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(0));
    g_apDevice = DynamicCast<WifiNetDevice>(apDevices.Get(0));

    Ptr<WifiPhy> apPhy = g_apDevice->GetPhy();
    apPhy->TraceConnectWithoutContext("PhyTxBegin", MakeCallback(&PhyTxCallback));

    std::cout << "[Main] AP: " << g_apDevice->GetMac()->GetAddress() << "\n";
    std::cout << "[Main] STA: " << g_staDevice->GetMac()->GetAddress() << "\n\n";

    // Mobility
    MobilityHelper apMobility;
    Ptr<ListPositionAllocator> apPos = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < nAPs; i++)
    {
        apPos->Add(Vector(i * apDistance, 0.0, 0.0));
    }
    apMobility.SetPositionAllocator(apPos);
    apMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    apMobility.Install(wifiApNodes);

    Ptr<WaypointMobilityModel> staWaypoint = CreateObject<WaypointMobilityModel>();
    double currentDistance = startDistance;
    double currentTime = 1.0;

    std::cout << "[Main] STA movement schedule:\n";
    while (currentDistance <= maxDistance)
    {
        staWaypoint->AddWaypoint(Waypoint(Seconds(currentTime), Vector(currentDistance, 0.0, 0.0)));
        std::cout << "       t=" << currentTime << "s → " << currentDistance << "m\n";
        currentDistance += distanceStep;
        currentTime += timeInterval;
    }
    wifiStaNodes.Get(0)->AggregateObject(staWaypoint);
    std::cout << "\n";

    // Internet stack
    InternetStackHelper stack;
    stack.Install(wifiApNodes);
    stack.Install(wifiStaNodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    address.Assign(staDevices);
    address.Assign(apDevices);

    Ptr<BssTm11vHelper> g_TMHelper = CreateObject<BssTm11vHelper>();
    g_TMHelper->InstallOnAp(g_apDevice);
    g_TMHelper->InstallOnSta(g_staDevice);

    std::cout << "[Main] ✓ Trace callbacks connected\n\n";

    // Run simulation
    std::cout << "╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║         Starting Simulation (5 Sniffers Total)      ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n\n";

    Simulator::Schedule(Seconds(5.0), 
                   &BssTm11vHelper::sendRankedCandidates, 
                   g_TMHelper, 
                   g_apDevice, 
                   g_apDevice->GetMac()->GetAddress(), 
                   g_staDevice->GetMac()->GetAddress(), 
                   GetDummyBeaconReports());

    Simulator::Stop(simulationTime);
    Simulator::Run();

    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║         Simulation Completed                         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n\n";

    Simulator::Destroy();
    return 0;
}
