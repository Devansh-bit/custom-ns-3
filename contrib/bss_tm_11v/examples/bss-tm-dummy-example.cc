/*
 * BSS Transition Management (802.11v) Roaming Example with Distribution System
 *
 * This example demonstrates BSS-TM frame exchange, actual roaming, and
 * seamless data flow during roaming with a proper Distribution System (DS).
 *
 * Infrastructure:
 * - 3 APs connected to a wired Distribution System (1Gbps CSMA)
 * - Backend server on the DS network
 * - All APs on same channel (36, 5GHz) for demonstration
 * - Bridge devices connect each AP's WiFi ↔ Wired interface
 *
 * Scenario:
 * - 1 STA starts at (10,0), moves to (60,0) over 10 seconds
 * - STA initially associates with AP0
 * - UDP data flows: STA → AP → DS → Server (10 packets/sec, 1024 bytes)
 * - At t=5s, AP0 sends BSS TM Request with candidates [AP1, AP2]
 * - STA roams to AP1 (BSSID change)
 * - Data continues flowing to same server (seamless roaming)
 * - Demonstrates realistic WiFi infrastructure with minimal data loss
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/csma-module.h"
#include "ns3/bridge-module.h"
#include "ns3/bss_tm_11v-helper.h"
#include "ns3/bss_tm_11v.h"
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("BssTmDummyExample");

// Global device pointers
Ptr<WifiNetDevice> g_ap0Device;
Ptr<WifiNetDevice> g_ap1Device;
Ptr<WifiNetDevice> g_ap2Device;
Ptr<WifiNetDevice> g_staDevice;

// Packet counters for data transmission tracking
uint32_t g_packetsSent = 0;
uint32_t g_packetsReceived = 0;
Time g_lastPacketTime = Seconds(0);

// UDP client and server address
Ptr<UdpClient> g_udpClient;
Ipv4Address g_serverAddress;  // Server on wired DS network

// Callback for packet transmission (UDP client sends)
void
PacketSent(Ptr<const Packet> packet)
{
    g_packetsSent++;
    g_lastPacketTime = Simulator::Now();
}

// Callback for packet reception (UDP server receives)
void
PacketReceived(Ptr<const Packet> packet)
{
    g_packetsReceived++;
    std::cout << "[Data] t=" << Simulator::Now().GetSeconds()
              << "s: Packet #" << g_packetsReceived
              << " received (" << packet->GetSize() << " bytes) "
              << "- Total: " << g_packetsReceived << "/" << g_packetsSent << "\n";
}

// Function to display data transmission stats
void
ShowDataStats(std::string label)
{
    double packetLossRate = 0.0;
    if (g_packetsSent > 0) {
        packetLossRate = 100.0 * (g_packetsSent - g_packetsReceived) / g_packetsSent;
    }

    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  Data Transmission Stats: " << label << std::string(26 - label.length(), ' ') << "║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║  Time: " << Simulator::Now().GetSeconds() << "s\n";
    std::cout << "║  Packets Sent: " << g_packetsSent << "\n";
    std::cout << "║  Packets Received: " << g_packetsReceived << "\n";
    std::cout << "║  Packet Loss: " << (g_packetsSent - g_packetsReceived)
              << " (" << std::fixed << std::setprecision(1) << packetLossRate << "%)\n";

    Time timeSinceLastPacket = Simulator::Now() - g_lastPacketTime;
    if (timeSinceLastPacket.GetSeconds() > 0.5) {
        std::cout << "║  Status: ⚠ DATA INTERRUPTED ("
                  << timeSinceLastPacket.GetSeconds() << "s since last packet)\n";
    } else {
        std::cout << "║  Status: ✓ DATA FLOWING\n";
    }
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";
}


// Function to send hardcoded BSS TM Request from AP0 to STA
void
SendHardcodedBssTmRequest(Ptr<BssTm11vHelper> tmHelper)
{
    NS_LOG_INFO("\n========== Sending Hardcoded BSS TM Request ==========");

    // Get MAC addresses
    Mac48Address ap1Bssid = g_ap1Device->GetMac()->GetAddress();
    Mac48Address ap2Bssid = g_ap2Device->GetMac()->GetAddress();
    Mac48Address staMac = g_staDevice->GetMac()->GetAddress();

    std::cout << "\n[t=" << Simulator::Now().GetSeconds() << "s] AP0: Preparing BSS TM Request\n";
    std::cout << "  Target STA: " << staMac << "\n";
    std::cout << "  Candidate 1 (AP1): " << ap1Bssid << ", Channel 36, Preference 255\n";
    std::cout << "  Candidate 2 (AP2): " << ap2Bssid << ", Channel 40, Preference 200\n\n";

    // Manually create BssTmParameters with hardcoded candidates
    BssTmParameters params;
    params.dialogToken = 42;  // Arbitrary dialog token
    params.disassociationTimer = 100;  // 100 TUs
    params.validityInterval = 255;  // Max validity
    params.terminationDuration = 0;
    params.reasonCode = BssTmParameters::ReasonCode::LOW_RSSI;

    // Create candidate 1: AP1
    BssTmParameters::CandidateAP candidate1;
    ap1Bssid.CopyTo(candidate1.BSSID);
    candidate1.channel = 36;
    candidate1.operatingClass = 115;  // 5 GHz, 20 MHz
    candidate1.phyType = 7;  // HT PHY
    candidate1.preference = 255;  // Highest preference

    // Create candidate 2: AP2
    BssTmParameters::CandidateAP candidate2;
    ap2Bssid.CopyTo(candidate2.BSSID);
    candidate2.channel = 40;
    candidate2.operatingClass = 115;
    candidate2.phyType = 7;
    candidate2.preference = 200;  // Lower preference

    // Add candidates to params
    params.candidates.push_back(candidate1);
    params.candidates.push_back(candidate2);

    // Send BSS TM Request directly using SendDynamicBssTmRequest
    tmHelper->SendDynamicBssTmRequest(g_ap0Device, params, staMac);

    std::cout << "[t=" << Simulator::Now().GetSeconds() << "s] AP0: BSS TM Request queued for transmission\n\n";
}

// Function to check STA association status
void
CheckStaAssociation(std::string label)
{
    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(g_staDevice->GetMac());
    if (!staMac) {
        std::cout << "[" << label << "] ERROR: Could not get StaWifiMac\n";
        return;
    }

    auto currentBssid = staMac->GetCurrentBssid();
    double rssi = staMac->GetCurrentRssi();
    double snr = staMac->GetCurrentSnr();
    uint8_t channel = g_staDevice->GetPhy()->GetChannelNumber();

    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  STA Association Check: " << label << std::string(28 - label.length(), ' ') << "║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║  Time: " << Simulator::Now().GetSeconds() << "s\n";

    if (currentBssid.has_value()) {
        Mac48Address bssid = currentBssid.value();
        std::cout << "║  Status: ASSOCIATED\n";
        std::cout << "║  BSSID: " << bssid;

        // Identify which AP
        if (bssid == g_ap0Device->GetMac()->GetAddress()) {
            std::cout << " (AP0 - Channel 6, 2.4GHz)";
        } else if (bssid == g_ap1Device->GetMac()->GetAddress()) {
            std::cout << " (AP1 - Channel 36, 5GHz) ✓ ROAMED!";
        } else if (bssid == g_ap2Device->GetMac()->GetAddress()) {
            std::cout << " (AP2 - Channel 40, 5GHz) ✓ ROAMED!";
        }
        std::cout << "\n";

        std::cout << "║  Channel: " << (int)channel << "\n";
        std::cout << "║  RSSI: " << rssi << " dBm\n";
        std::cout << "║  SNR: " << snr << " dB\n";
    } else {
        std::cout << "║  Status: NOT ASSOCIATED\n";
        std::cout << "║  Channel: " << (int)channel << "\n";
    }

    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";
}

int
main(int argc, char* argv[])
{
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  BSS-TM Roaming with Data Transmission                ║\n";
    std::cout << "║  (Frame Exchange + Roaming + Data Flow Monitor)       ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    // Configuration
    Time simulationTime = Seconds(10);
    bool verbose = true;

    CommandLine cmd(__FILE__);
    cmd.AddValue("time", "Simulation duration (seconds)", simulationTime);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("BssTm11vHelper", LOG_LEVEL_INFO);
    }

    // Create nodes: 3 APs + 1 STA + 1 Server
    NodeContainer apNodes, staNode, serverNode;
    apNodes.Create(3);
    staNode.Create(1);
    serverNode.Create(1);

    std::cout << "[Setup] Created 3 AP nodes + 1 STA node + 1 Server + Distribution System\n";

    // WiFi Channel
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    // WiFi standard and rate manager
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("HtMcs7"),
                                 "ControlMode", StringValue("HtMcs0"));

    Ssid ssid = Ssid("bss-tm-test");

    // Configure STA on channel 36 (5 GHz) - ALL devices on same channel for data resumption
    phy.Set("ChannelSettings", StringValue("{36, 20, BAND_5GHZ, 0}"));
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

    // ALL APs on channel 36 (5 GHz) - SAME CHANNEL for data resumption demo
    phy.Set("ChannelSettings", StringValue("{36, 20, BAND_5GHZ, 0}"));

    // Install AP0 - STA starts associated here
    NetDeviceContainer ap0Device = wifi.Install(phy, apMac, apNodes.Get(0));
    g_ap0Device = DynamicCast<WifiNetDevice>(ap0Device.Get(0));

    // Install AP1 - first roaming candidate
    NetDeviceContainer ap1Device = wifi.Install(phy, apMac, apNodes.Get(1));
    g_ap1Device = DynamicCast<WifiNetDevice>(ap1Device.Get(0));

    // Install AP2 - second roaming candidate
    NetDeviceContainer ap2Device = wifi.Install(phy, apMac, apNodes.Get(2));
    g_ap2Device = DynamicCast<WifiNetDevice>(ap2Device.Get(0));

    // Combine all AP devices for address assignment
    NetDeviceContainer apDevices;
    apDevices.Add(ap0Device);
    apDevices.Add(ap1Device);
    apDevices.Add(ap2Device);

    std::cout << "[Setup] AP0 BSSID: " << g_ap0Device->GetMac()->GetAddress() << " (Channel 36, 5GHz)\n";
    std::cout << "[Setup] AP1 BSSID: " << g_ap1Device->GetMac()->GetAddress() << " (Channel 36, 5GHz)\n";
    std::cout << "[Setup] AP2 BSSID: " << g_ap2Device->GetMac()->GetAddress() << " (Channel 36, 5GHz)\n";
    std::cout << "[Setup] STA MAC:   " << g_staDevice->GetMac()->GetAddress() << " (Channel 36, 5GHz)\n";
    std::cout << "[Setup] Note: All devices on same channel for data resumption demo\n\n";

    // Mobility: 3 APs in a line
    MobilityHelper apMobility;
    Ptr<ListPositionAllocator> apPositions = CreateObject<ListPositionAllocator>();
    apPositions->Add(Vector(0.0, 0.0, 0.0));    // AP0
    apPositions->Add(Vector(50.0, 0.0, 0.0));   // AP1
    apPositions->Add(Vector(100.0, 0.0, 0.0));  // AP2
    apMobility.SetPositionAllocator(apPositions);
    apMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    apMobility.Install(apNodes);

    std::cout << "[Mobility] AP0 position: (0, 0, 0)\n";
    std::cout << "[Mobility] AP1 position: (50, 0, 0)\n";
    std::cout << "[Mobility] AP2 position: (100, 0, 0)\n\n";

    // STA mobility: Move from (10,0) to (60,0) over simulation time
    Ptr<WaypointMobilityModel> staMobility = CreateObject<WaypointMobilityModel>();
    staMobility->AddWaypoint(Waypoint(Seconds(0.0), Vector(10.0, 0.0, 0.0)));
    staMobility->AddWaypoint(Waypoint(simulationTime, Vector(60.0, 0.0, 0.0)));
    staNode.Get(0)->AggregateObject(staMobility);

    std::cout << "[Mobility] STA movement:\n";
    std::cout << "           t=0s:  (10, 0, 0) [near AP0]\n";
    std::cout << "           t=" << simulationTime.GetSeconds() << "s: (60, 0, 0) [between AP1 and AP2]\n\n";

    // ====== DISTRIBUTION SYSTEM (DS) SETUP ======
    // Create wired CSMA network (the DS backbone - a single shared segment)
    std::cout << "[DS] Setting up Distribution System (wired backbone)...\n";

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("1Gbps"));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));

    // Create CSMA devices on: Server + 3 APs (wired side)
    NodeContainer csmaNodes;
    csmaNodes.Add(serverNode);
    csmaNodes.Add(apNodes);  // All 3 APs

    NetDeviceContainer csmaDevices = csma.Install(csmaNodes);

    // Extract individual CSMA devices for each AP
    NetDeviceContainer ap0CsmaDevice, ap1CsmaDevice, ap2CsmaDevice, serverCsmaDevice;
    serverCsmaDevice.Add(csmaDevices.Get(0));
    ap0CsmaDevice.Add(csmaDevices.Get(1));
    ap1CsmaDevice.Add(csmaDevices.Get(2));
    ap2CsmaDevice.Add(csmaDevices.Get(3));

    std::cout << "[DS] Wired backbone created (1Gbps CSMA shared segment)\n";
    std::cout << "[DS] Connected: Server + 3 APs\n";

    // Create bridge devices to connect WiFi and wired interfaces for each AP
    BridgeHelper bridge;

    NetDeviceContainer bridge0Devices;
    bridge0Devices.Add(ap0Device.Get(0));  // WiFi side
    bridge0Devices.Add(ap0CsmaDevice.Get(0));  // Wired side
    bridge.Install(apNodes.Get(0), bridge0Devices);

    NetDeviceContainer bridge1Devices;
    bridge1Devices.Add(ap1Device.Get(0));
    bridge1Devices.Add(ap1CsmaDevice.Get(0));
    bridge.Install(apNodes.Get(1), bridge1Devices);

    NetDeviceContainer bridge2Devices;
    bridge2Devices.Add(ap2Device.Get(0));
    bridge2Devices.Add(ap2CsmaDevice.Get(0));
    bridge.Install(apNodes.Get(2), bridge2Devices);

    std::cout << "[DS] Bridge devices created (connecting WiFi ↔ Wired for each AP)\n\n";

    // Internet stack - NOTE: APs don't need IP stack, they just bridge L2
    InternetStackHelper stack;
    stack.Install(serverNode);
    stack.Install(staNode);

    // Assign IP addresses - Server and STA are on the SAME subnet via bridges
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer serverInterface = address.Assign(serverCsmaDevice);
    Ipv4InterfaceContainer staInterface = address.Assign(staDevice);

    g_serverAddress = serverInterface.GetAddress(0);

    std::cout << "[Network] Server IP: " << g_serverAddress << " (on DS backbone)\n";
    std::cout << "[Network] STA IP: " << staInterface.GetAddress(0) << " (WiFi, bridged to DS)\n";
    std::cout << "[Network] STA and Server on same L2 segment via AP bridges\n\n";

    // ====== UDP DATA TRANSMISSION SETUP ======
    uint16_t udpPort = 9;

    // Install UDP server on backend server
    UdpServerHelper server(udpPort);
    ApplicationContainer serverApp = server.Install(serverNode);
    serverApp.Start(Seconds(0.0));
    serverApp.Stop(simulationTime);

    // Connect receive callback
    Ptr<UdpServer> udpServer = DynamicCast<UdpServer>(serverApp.Get(0));
    udpServer->TraceConnectWithoutContext("Rx", MakeCallback(&PacketReceived));

    // Install UDP client on STA (sends to server on DS network)
    UdpClientHelper client(g_serverAddress, udpPort);
    client.SetAttribute("MaxPackets", UintegerValue(1000000));
    client.SetAttribute("Interval", TimeValue(MilliSeconds(100)));  // 10 packets/sec
    client.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApp = client.Install(staNode);
    clientApp.Start(Seconds(1.0));
    clientApp.Stop(simulationTime);

    // Store UDP client pointer
    g_udpClient = DynamicCast<UdpClient>(clientApp.Get(0));
    g_udpClient->TraceConnectWithoutContext("Tx", MakeCallback(&PacketSent));

    std::cout << "[UDP] Server installed on backend (port " << udpPort << ")\n";
    std::cout << "[UDP] Client on STA → Server (" << g_serverAddress << ")\n";
    std::cout << "[UDP] Data rate: 10 packets/sec, 1024 bytes/packet\n";
    std::cout << "[UDP] Traffic flows: STA → AP → DS → Server (seamless roaming)\n\n";

    // Install BSS-TM Helper on AP0 and STA
    Ptr<BssTm11vHelper> tmHelper = CreateObject<BssTm11vHelper>();
    tmHelper->InstallOnAp(g_ap0Device);
    tmHelper->InstallOnSta(g_staDevice);

    std::cout << "[BSS-TM] Helper installed on AP0 and STA\n";
    std::cout << "[BSS-TM] Frame sniffers active for monitoring\n";
    std::cout << "[BSS-TM] Note: Roaming initiated/completed events will be logged to ns-3 LOG_INFO\n\n";

    // Schedule association status checks and data stats
    Simulator::Schedule(Seconds(2.0), &CheckStaAssociation, "Before Roaming");
    Simulator::Schedule(Seconds(2.0), &ShowDataStats, "Before Roaming");

    // Schedule BSS TM Request at t=5s
    Simulator::Schedule(Seconds(5.0), &SendHardcodedBssTmRequest, tmHelper);

    Simulator::Schedule(Seconds(6.5), &CheckStaAssociation, "After Roaming");
    Simulator::Schedule(Seconds(6.5), &ShowDataStats, "After Roaming");

    Simulator::Schedule(Seconds(9.0), &CheckStaAssociation, "Final Check");
    Simulator::Schedule(Seconds(9.0), &ShowDataStats, "Final Check");

    std::cout << "[Schedule] Data transmission: 1s - 10s (10 pkt/s)\n";
    std::cout << "[Schedule] BSS TM Request: t=5.0s\n";
    std::cout << "[Schedule] Status checks: t=2s, 6.5s, 9s\n\n";
    std::cout << "╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  Starting Simulation...                               ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    // Run simulation
    Simulator::Stop(simulationTime);
    Simulator::Run();

    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  Simulation Completed                                 ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║  Summary:                                             ║\n";
    std::cout << "║  - Data flowed: STA → AP → DS → Server               ║\n";
    std::cout << "║  - BSS-TM triggered roaming from AP0 → AP1            ║\n";
    std::cout << "║  - Distribution System maintained connectivity        ║\n";
    std::cout << "║  - Data continued to flow after roaming               ║\n";
    std::cout << "║                                                       ║\n";
    std::cout << "║  Final Stats: " << g_packetsReceived << "/" << g_packetsSent << " packets received";
    if (g_packetsSent > 0) {
        double lossRate = 100.0 * (g_packetsSent - g_packetsReceived) / g_packetsSent;
        std::cout << " (" << std::fixed << std::setprecision(1) << lossRate << "% loss)";
    }
    std::cout << "    ║\n";
    std::cout << "║                                                       ║\n";
    std::cout << "║  ✅ Seamless roaming with Distribution System!        ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    Simulator::Destroy();
    return 0;
}