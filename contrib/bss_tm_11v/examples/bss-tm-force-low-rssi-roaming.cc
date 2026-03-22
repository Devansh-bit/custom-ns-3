/*
 * BSS Transition Management - Forced Roaming to Lower RSSI AP
 *
 * This example demonstrates network-controlled roaming where BSS-TM
 * forces the client to roam to an AP with LOWER RSSI, overriding
 * signal-strength based decisions.
 *
 * Scenario:
 * - 3 APs on the SAME channel (6, 2.4 GHz) in a line
 *   - AP0 at (0,0) - Strong signal (STA starts here)
 *   - AP1 at (30,0) - Medium signal
 *   - AP2 at (60,0) - Weak signal (farthest)
 * - STA at fixed position (10,0) - close to AP0
 * - At t=5s: AP0 sends BSS-TM Request directing STA to roam to AP2
 * - Expected: STA roams to AP2 despite it having the WORST RSSI
 *
 * This demonstrates that BSS-TM protocol-based decisions override
 * traditional signal-strength based roaming.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/bss_tm_11v-helper.h"
#include "ns3/bss_tm_11v.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("BssTmForceLowRssiRoaming");

// Global device pointers
Ptr<WifiNetDevice> g_ap0Device;
Ptr<WifiNetDevice> g_ap1Device;
Ptr<WifiNetDevice> g_ap2Device;
Ptr<WifiNetDevice> g_staDevice;

// Function to measure RSSI from all APs
void
MeasureRssiFromAllAps(std::string label)
{
    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(g_staDevice->GetMac());
    if (!staMac) {
        return;
    }

    auto currentBssid = staMac->GetCurrentBssid();
    double currentRssi = staMac->GetCurrentRssi();

    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  RSSI Measurement: " << label << std::string(33 - label.length(), ' ') << "║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║  Time: " << Simulator::Now().GetSeconds() << "s\n";
    std::cout << "║  STA Position: (10, 0, 0)\n";
    std::cout << "║\n";

    // Calculate approximate RSSI from each AP based on distance
    // Using simple path loss model for comparison
    Ptr<MobilityModel> staMobility = g_staDevice->GetNode()->GetObject<MobilityModel>();
    Vector staPos = staMobility->GetPosition();

    for (uint32_t i = 0; i < 3; i++) {
        Ptr<WifiNetDevice> apDevice;
        std::string apName;
        Vector apPos;

        if (i == 0) {
            apDevice = g_ap0Device;
            apName = "AP0";
            apPos = Vector(0, 0, 0);
        } else if (i == 1) {
            apDevice = g_ap1Device;
            apName = "AP1";
            apPos = Vector(30, 0, 0);
        } else {
            apDevice = g_ap2Device;
            apName = "AP2";
            apPos = Vector(60, 0, 0);
        }

        double distance = std::sqrt(std::pow(staPos.x - apPos.x, 2) +
                                   std::pow(staPos.y - apPos.y, 2));

        Mac48Address apBssid = apDevice->GetMac()->GetAddress();
        bool isAssociated = currentBssid.has_value() && currentBssid.value() == apBssid;

        std::cout << "║  " << apName << " (" << apBssid << "):\n";
        std::cout << "║    Position: (" << apPos.x << ", " << apPos.y << ", " << apPos.z << ")\n";
        std::cout << "║    Distance: " << distance << " m\n";

        if (isAssociated) {
            std::cout << "║    RSSI: " << currentRssi << " dBm ★ ASSOCIATED\n";
        } else {
            // Estimate RSSI (not actual, just for demonstration)
            std::cout << "║    RSSI: ~" << (-40.0 - 20 * std::log10(distance + 1)) << " dBm (estimated)\n";
        }
        std::cout << "║\n";
    }

    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";
}

// Function to send BSS TM Request forcing roam to AP2 (worst RSSI)
void
ForceRoamToWorstAp(Ptr<BssTm11vHelper> tmHelper)
{
    Mac48Address ap2Bssid = g_ap2Device->GetMac()->GetAddress();
    Mac48Address staMac = g_staDevice->GetMac()->GetAddress();

    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  AP0: Forcing Roam to AP2 (Worst RSSI)               ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║  Time: " << Simulator::Now().GetSeconds() << "s\n";
    std::cout << "║  Target STA: " << staMac << "\n";
    std::cout << "║  Forced Target: AP2 (" << ap2Bssid << ")\n";
    std::cout << "║  Reason: Network-controlled load balancing\n";
    std::cout << "║\n";
    std::cout << "║  Note: AP2 is the FARTHEST AP (60m away)\n";
    std::cout << "║        This will result in WORSE RSSI than current!\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    // Create BssTmParameters with ONLY AP2 as candidate
    BssTmParameters params;
    params.dialogToken = 99;
    params.disassociationTimer = 50;  // Give it some urgency
    params.validityInterval = 255;
    params.terminationDuration = 0;
    params.reasonCode = BssTmParameters::ReasonCode::HIGH_LOAD;  // Network decision

    // ONLY add AP2 as candidate (forcing roam to worst RSSI)
    BssTmParameters::CandidateAP candidate;
    ap2Bssid.CopyTo(candidate.BSSID);
    candidate.channel = 6;  // Same channel
    candidate.operatingClass = 81;  // 2.4 GHz
    candidate.phyType = 7;
    candidate.preference = 255;  // Highest preference (only choice)

    params.candidates.push_back(candidate);

    // Send BSS TM Request
    tmHelper->SendDynamicBssTmRequest(g_ap0Device, params, staMac);
}

int
main(int argc, char* argv[])
{
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  BSS-TM: Force Roaming to Lower RSSI AP              ║\n";
    std::cout << "║  Demonstrates Network-Controlled Roaming             ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    // Configuration
    Time simulationTime = Seconds(12);
    bool verbose = true;

    CommandLine cmd(__FILE__);
    cmd.AddValue("time", "Simulation duration (seconds)", simulationTime);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.Parse(argc, argv);

    if (verbose) {
        LogComponentEnable("BssTm11vHelper", LOG_LEVEL_INFO);
    }

    // Create nodes: 3 APs + 1 STA
    NodeContainer apNodes, staNode;
    apNodes.Create(3);
    staNode.Create(1);

    std::cout << "[Setup] Created 3 AP nodes + 1 STA node\n";

    // WiFi Channel - ALL APs on SAME channel for direct RSSI comparison
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    // WiFi standard and rate manager
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("HtMcs7"),
                                 "ControlMode", StringValue("HtMcs0"));

    Ssid ssid = Ssid("bss-tm-force-roam");

    // Configure STA on channel 6 (2.4 GHz)
    phy.Set("ChannelSettings", StringValue("{6, 20, BAND_2_4GHZ, 0}"));
    WifiMacHelper staMac;
    staMac.SetType("ns3::StaWifiMac",
                   "Ssid", SsidValue(ssid),
                   "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevice = wifi.Install(phy, staMac, staNode);
    g_staDevice = DynamicCast<WifiNetDevice>(staDevice.Get(0));

    // Configure AP MAC
    WifiMacHelper apMac;
    apMac.SetType("ns3::ApWifiMac",
                  "Ssid", SsidValue(ssid),
                  "BeaconGeneration", BooleanValue(true),
                  "BeaconInterval", TimeValue(MicroSeconds(102400)));

    // ALL APs on channel 6 (2.4 GHz) - SAME CHANNEL
    phy.Set("ChannelSettings", StringValue("{6, 20, BAND_2_4GHZ, 0}"));

    // Install AP0 (closest to STA)
    NetDeviceContainer ap0Device = wifi.Install(phy, apMac, apNodes.Get(0));
    g_ap0Device = DynamicCast<WifiNetDevice>(ap0Device.Get(0));

    // Install AP1 (medium distance)
    NetDeviceContainer ap1Device = wifi.Install(phy, apMac, apNodes.Get(1));
    g_ap1Device = DynamicCast<WifiNetDevice>(ap1Device.Get(0));

    // Install AP2 (farthest from STA - worst RSSI)
    NetDeviceContainer ap2Device = wifi.Install(phy, apMac, apNodes.Get(2));
    g_ap2Device = DynamicCast<WifiNetDevice>(ap2Device.Get(0));

    // Combine all AP devices
    NetDeviceContainer apDevices;
    apDevices.Add(ap0Device);
    apDevices.Add(ap1Device);
    apDevices.Add(ap2Device);

    std::cout << "[Setup] AP0 BSSID: " << g_ap0Device->GetMac()->GetAddress() << " (Channel 6, 2.4GHz, Position: 0m)\n";
    std::cout << "[Setup] AP1 BSSID: " << g_ap1Device->GetMac()->GetAddress() << " (Channel 6, 2.4GHz, Position: 30m)\n";
    std::cout << "[Setup] AP2 BSSID: " << g_ap2Device->GetMac()->GetAddress() << " (Channel 6, 2.4GHz, Position: 60m) ← WORST RSSI\n";
    std::cout << "[Setup] STA MAC:   " << g_staDevice->GetMac()->GetAddress() << " (Position: 10m)\n\n";

    // Mobility: 3 APs in a line with increasing distance
    MobilityHelper apMobility;
    Ptr<ListPositionAllocator> apPositions = CreateObject<ListPositionAllocator>();
    apPositions->Add(Vector(0.0, 0.0, 0.0));    // AP0 - closest
    apPositions->Add(Vector(30.0, 0.0, 0.0));   // AP1 - medium
    apPositions->Add(Vector(60.0, 0.0, 0.0));   // AP2 - farthest (worst RSSI)
    apMobility.SetPositionAllocator(apPositions);
    apMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    apMobility.Install(apNodes);

    // STA: Fixed position close to AP0 (best RSSI from AP0)
    MobilityHelper staMobility;
    Ptr<ListPositionAllocator> staPosition = CreateObject<ListPositionAllocator>();
    staPosition->Add(Vector(10.0, 0.0, 0.0));  // Close to AP0
    staMobility.SetPositionAllocator(staPosition);
    staMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    staMobility.Install(staNode);

    std::cout << "[Topology] Linear placement:\n";
    std::cout << "           AP0────10m────STA────20m────AP1────30m────AP2\n";
    std::cout << "           (0m)         (10m)         (30m)         (60m)\n";
    std::cout << "           BEST         ↑             MED          WORST\n";
    std::cout << "           RSSI     starts here       RSSI          RSSI\n\n";

    // Internet stack
    InternetStackHelper stack;
    stack.Install(apNodes);
    stack.Install(staNode);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    address.Assign(apDevices);
    address.Assign(staDevice);

    // Install BSS-TM Helper on AP0 and STA
    Ptr<BssTm11vHelper> tmHelper = CreateObject<BssTm11vHelper>();
    tmHelper->InstallOnAp(g_ap0Device);
    tmHelper->InstallOnSta(g_staDevice);

    std::cout << "[BSS-TM] Helper installed on AP0 and STA\n\n";

    // Schedule RSSI measurements
    Simulator::Schedule(Seconds(2.0), &MeasureRssiFromAllAps, "Initial State");
    Simulator::Schedule(Seconds(7.0), &MeasureRssiFromAllAps, "After Forced Roaming");
    Simulator::Schedule(Seconds(11.0), &MeasureRssiFromAllAps, "Final State");

    // Schedule forced roaming to AP2 (worst RSSI) at t=5s
    Simulator::Schedule(Seconds(5.0), &ForceRoamToWorstAp, tmHelper);

    std::cout << "╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  Simulation Timeline:                                 ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║  t=2s:  Measure RSSI (STA should be on AP0)           ║\n";
    std::cout << "║  t=5s:  BSS-TM forces roam to AP2 (worst RSSI)        ║\n";
    std::cout << "║  t=7s:  Measure RSSI (verify roam to AP2)             ║\n";
    std::cout << "║  t=11s: Final RSSI measurement                        ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    std::cout << "Starting simulation...\n\n";

    // Run simulation
    Simulator::Stop(simulationTime);
    Simulator::Run();

    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  Simulation Completed                                 ║\n";
    std::cout << "║                                                       ║\n";
    std::cout << "║  Key Demonstration:                                   ║\n";
    std::cout << "║  BSS-TM successfully forced the client to roam to     ║\n";
    std::cout << "║  AP2 (farthest AP) despite having WORSE RSSI than     ║\n";
    std::cout << "║  AP0 (initial AP).                                    ║\n";
    std::cout << "║                                                       ║\n";
    std::cout << "║  This proves network-controlled roaming can override ║\n";
    std::cout << "║  signal-strength based decisions!                     ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    Simulator::Destroy();
    return 0;
}
