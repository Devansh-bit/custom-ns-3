#include "ns3/beacon-protocol-11k-helper.h"
#include "ns3/core-module.h"
#include "ns3/dual-phy-sniffer-helper.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/neighbor-protocol-11k-helper.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/propagation-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("BeaconNeighborExample");

// Global variables
Ptr<WifiNetDevice> g_apDevice;
Ptr<WifiNetDevice> g_staDevice;
Ptr<NeighborProtocolHelper> g_neighborHelper;
Ptr<BeaconProtocolHelper> g_beaconHelper;
bool g_neighborReportTriggered = false;
double g_rssiThreshold = -65.0;

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
    std::cout << "  Threshold: " << g_rssiThreshold << " dBm\n";

    if (rssi < g_rssiThreshold && !g_neighborReportTriggered)
    {
        std::cout << "  Status:  THRESHOLD CROSSED!\n";
        std::cout << "╔═══════════════════════════════════════════╗\n";
        std::cout << "║ Initiating Neighbor Report Protocol      ║\n";
        std::cout << "╚═══════════════════════════════════════════╝\n\n";

        g_neighborReportTriggered = true;

        //  Trigger neighbor protocol
        g_neighborHelper->SendNeighborReportRequest(g_staDevice,
                                                    g_apDevice->GetMac()->GetAddress());
    }
    else
    {
        std::cout << "  Status: ✓ Connection OK\n\n";
    }
}

// ===== TRACE CALLBACK: Neighbor Report Received =====
void
OnNeighborReportReceived(Mac48Address staAddr, Mac48Address apAddr, std::vector<NeighborReportData> neighbors)
{
    std::cout << "\n╔═══════════════════════════════════════════╗\n";
    std::cout << "║  TRACE: Neighbor Report Received       ║\n";
    std::cout << "╚═══════════════════════════════════════════╝\n";
    std::cout << "  STA: " << staAddr << "\n";
    std::cout << "  From AP: " << apAddr << "\n";
    std::cout << "  Number of Neighbors: " << neighbors.size() << "\n";
    std::cout << "--------------------------------------\n";

    // Log each neighbor report to console
    for (size_t i = 0; i < neighbors.size(); i++)
    {
        const NeighborReportData& neighbor = neighbors[i];

        std::cout << "Neighbor #" << (i + 1) << ":\n";
        std::cout << "  BSSID: " << neighbor.bssid << "\n";
        std::cout << "  Channel: " << (int)neighbor.channel << "\n";
        std::cout << "  Regulatory Class: " << (int)neighbor.regulatoryClass << "\n";
        std::cout << "  PHY Type: " << (int)neighbor.phyType << "\n";
        std::cout << "  BSSID Info: 0x" << std::hex << neighbor.bssidInfo << std::dec << "\n";
        std::cout << "\n";
    }

    // Update beacon helper with neighbor list
    g_beaconHelper->SetNeighborList(g_neighborHelper->GetNeighborList());

    //  CORRECTED: Use PeekPointer() to get raw pointer
    std::vector<uint8_t> channels = {36};
    Simulator::Schedule(MilliSeconds(50),
                        &BeaconProtocolHelper::SendBeaconRequest,
                        PeekPointer(g_beaconHelper),
                        g_apDevice,
                        staAddr);
}

// ===== TRACE CALLBACK: Beacon Report Received =====
void
OnBeaconReportReceived(Mac48Address apAddr,
                       Mac48Address staAddr,
                       std::vector<BeaconReportData> reports)
{
    std::cout << "\n╔═══════════════════════════════════════════╗\n";
    std::cout << "║  TRACE: Beacon Report Received         ║\n";
    std::cout << "╚═══════════════════════════════════════════╝\n";
    std::cout << "  AP: " << apAddr << "\n";
    std::cout << "  From STA: " << staAddr << "\n";
    std::cout << "  Number of Reports: " << reports.size() << "\n";
    std::cout << "--------------------------------------\n";

    // Log each raw beacon report to console
    for (size_t i = 0; i < reports.size(); i++)
    {
        const BeaconReportData& report = reports[i];

        std::cout << "Report #" << (i + 1) << ":\n";
        std::cout << "  BSSID: " << report.bssid << "\n";
        std::cout << "  Channel: " << (int)report.channel << "\n";
        std::cout << "  Regulatory Class: " << (int)report.regulatoryClass << "\n";
        std::cout << "  RCPI (raw): " << (int)report.rcpi << "\n";
        std::cout << "  RSNI (raw): " << (int)report.rsni << "\n";
        std::cout << "  Reported Frame Info: 0x" << std::hex << (int)report.reportedFrameInfo
                  << std::dec << "\n";
        std::cout << "  Measurement Duration: " << report.measurementDuration << " TUs\n";
        std::cout << "  Measurement Start Time: " << report.actualMeasurementStartTime << "\n";
        std::cout << "  Antenna ID: " << (int)report.antennaID << "\n";
        std::cout << "  Parent TSF: " << report.parentTSF << "\n";
        std::cout << "\n";
    }

    std::cout << "\n 802.11k Protocol Sequence Complete!\n\n";
}

// Test function to verify continuous background scanning
void
TestContinuousScanning()
{
    std::cout << "\n╔════════════════════════════════════════════╗\n";
    std::cout << "║ TEST: Triggering 2nd Neighbor Request     ║\n";
    std::cout << "║ (to verify continuous scanning)            ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n";
    std::cout << "  Time: " << Simulator::Now().GetSeconds() << "s\n";
    std::cout << "  Cache should have beacons by now...\n\n";

    g_neighborHelper->SendNeighborReportRequest(g_staDevice,
                                                g_apDevice->GetMac()->GetAddress());
}

int
main(int argc, char* argv[])
{
    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║  802.11k Neighbor & Beacon Reporting Example        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n\n";

    // Configuration
    uint32_t nStations = 1;
    uint32_t nAPs = 5;
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

    g_rssiThreshold = rssiThreshold;

    if (verbose)
    {
        LogComponentEnable("BeaconNeighborExample", LOG_LEVEL_INFO);
        LogComponentEnable("NeighborProtocolHelper", LOG_LEVEL_INFO);
        LogComponentEnable("BeaconProtocolHelper", LOG_LEVEL_INFO);
    }

    // Create nodes
    NodeContainer wifiStaNodes, wifiApNodes;
    wifiStaNodes.Create(nStations);
    wifiApNodes.Create(nAPs);

    // ===== SPECTRUM CHANNEL SETUP =====
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();

    Ptr<LogDistancePropagationLossModel> lossModel = CreateObject<LogDistancePropagationLossModel>();
    lossModel->SetAttribute("Exponent", DoubleValue(3.0));
    lossModel->SetAttribute("ReferenceLoss", DoubleValue(46.6777));
    spectrumChannel->AddPropagationLossModel(lossModel);

    Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);

    std::cout << "[Main] ✓ Spectrum channel created with propagation models\n";

    // ===== WIFI SETUP =====
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("HtMcs7"),
                                 "ControlMode",
                                 StringValue("HtMcs0"));

    Ssid ssid = Ssid("ns3-80211k");

    // ===== STA DEVICE (Channel 36) =====
    SpectrumWifiPhyHelper staPhy;
    staPhy.SetChannel(spectrumChannel);
    staPhy.Set("ChannelSettings", StringValue("{36, 20, BAND_5GHZ, 0}"));

    WifiMacHelper staMac;
    staMac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(staPhy, staMac, wifiStaNodes);

    std::cout << "[Main] ✓ STA device installed on Channel 36\n";

    // ===== AP DEVICES (Multi-channel) =====
    NetDeviceContainer apDevices;
    std::vector<uint8_t> apChannels = {36, 40, 44, 36, 40}; // Channels for APs 0-4

    for (uint32_t i = 0; i < nAPs; i++)
    {
        uint8_t channel = apChannels[i % apChannels.size()];

        SpectrumWifiPhyHelper apPhy;
        apPhy.SetChannel(spectrumChannel);

        std::ostringstream chStr;
        chStr << "{" << (int)channel << ", 20, BAND_5GHZ, 0}";
        apPhy.Set("ChannelSettings", StringValue(chStr.str()));

        WifiMacHelper apMac;
        apMac.SetType("ns3::ApWifiMac",
                     "Ssid",
                     SsidValue(ssid),
                     "BeaconGeneration",
                     BooleanValue(true),
                     "BeaconInterval",
                     TimeValue(MicroSeconds(102400)));

        NetDeviceContainer apDevice = wifi.Install(apPhy, apMac, wifiApNodes.Get(i));
        apDevices.Add(apDevice);

        std::cout << "[Main] ✓ AP" << i << " installed on Channel " << (int)channel << "\n";
    }

    g_staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(0));
    g_apDevice = DynamicCast<WifiNetDevice>(apDevices.Get(0));

    std::cout << "[Main] AP: " << g_apDevice->GetMac()->GetAddress() << "\n";
    std::cout << "[Main] STA: " << g_staDevice->GetMac()->GetAddress() << "\n\n";

    // // CORRECT: Access the apDevices container directly
    // std::vector<ApInfo> neighborTable;

    // for (uint32_t i = 1; i < apDevices.GetN(); i++)
    // { // GetN() gets container size
    //     // Skip index 0 (the connected AP)

    //     Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(apDevices.Get(i));

    //     ApInfo ap;
    //     ap.bssid = apDevice->GetMac()->GetAddress(); // Get actual MAC!
    //     ap.ssid = "ns3-80211k";
    //     ap.channel = 36;
    //     ap.regulatoryClass = 115;
    //     ap.phyType = 7;
    //     ap.position = Vector(i * apDistance, 0, 0);
    //     ap.load = 30 + (i * 15);
    //     neighborTable.push_back(ap);

    //     std::cout << "  Added neighbor: " << ap.bssid << "\n";
    // }

    // std::cout << "[Main] Neighbor table: " << neighborTable.size() << " APs\n\n";

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

    // Create SHARED DualPhySnifferHelper for multi-channel scanning
    DualPhySnifferHelper dualPhySniffer;
    dualPhySniffer.SetChannel(Ptr<SpectrumChannel>(spectrumChannel));
    dualPhySniffer.SetScanningChannels({36, 40, 44, 48});
    dualPhySniffer.SetHopInterval(Seconds(0.5));  // Fast hopping for quick discovery

    std::cout << "[Main] ✓ DualPhySnifferHelper configured\n";
    std::cout << "[Main]   Scanning channels: 36, 40, 44, 48\n";
    std::cout << "[Main]   Hop interval: 0.5s\n\n";

    // Install dual-PHY sniffer on AP (for neighbor discovery)
    dualPhySniffer.Install(g_apDevice->GetNode(), 36, g_apDevice->GetMac()->GetAddress());
    dualPhySniffer.StartChannelHopping();

    std::cout << "[Main] ✓ Dual-PHY sniffer installed on AP and started\n\n";

    // Install SEPARATE protocol helpers
    g_neighborHelper = CreateObject<NeighborProtocolHelper>();
    g_neighborHelper->SetDualPhySniffer(&dualPhySniffer);
    g_neighborHelper->InstallOnAp(g_apDevice);
    g_neighborHelper->InstallOnSta(g_staDevice);

    std::cout << "[Main] ✓ Neighbor helper installed\n\n";

    // Configure beacon helper with same dual-PHY sniffer
    g_beaconHelper = CreateObject<BeaconProtocolHelper>();
    g_beaconHelper->SetDualPhySniffer(&dualPhySniffer);
    g_beaconHelper->InstallOnAp(g_apDevice);
    g_beaconHelper->InstallOnSta(g_staDevice);

    std::cout << "[Main] ✓ Beacon helper installed\n";
    std::cout << "[Main] ✓ 802.11k protocols configured (sharing dual-PHY sniffer)\n\n";

    // Connect trace callbacks
    g_neighborHelper->TraceConnectWithoutContext("NeighborReportReceived",
                                                 MakeCallback(&OnNeighborReportReceived));
    g_beaconHelper->TraceConnectWithoutContext("BeaconReportReceived",
                                               MakeCallback(&OnBeaconReportReceived));

    std::cout << "[Main] ✓ Trace callbacks connected\n\n";

    //  Install connection monitor (5th sniffer)
    uint32_t staNodeId = g_staDevice->GetNode()->GetId();
    std::stringstream ss;
    ss << "/NodeList/" << staNodeId << "/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx";
    Config::Connect(ss.str(), MakeCallback(&ConnectionQualitySniffer));

    std::cout << "[Main] ✓ Connection monitor installed (5th sniffer)\n\n";

    // Schedule test of continuous scanning at t=2s
    Simulator::Schedule(Seconds(2.0), &TestContinuousScanning);
    std::cout << "[Main] ✓ Scheduled 2nd neighbor request at t=2s (to test continuous scanning)\n\n";

    // Run simulation
    std::cout << "╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║         Starting Simulation (5 Sniffers Total)      ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n\n";

    Simulator::Stop(simulationTime);
    Simulator::Run();

    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║         Simulation Completed                         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n\n";

    Simulator::Destroy();
    return 0;
}
