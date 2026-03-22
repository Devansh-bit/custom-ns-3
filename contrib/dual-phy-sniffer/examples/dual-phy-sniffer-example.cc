/**
 * @file
 * @brief Basic DualPhySnifferHelper example using YansWifiChannel
 *
 * This example demonstrates passive WiFi beacon monitoring across multiple channels
 * using the DualPhySnifferHelper with YansWifiChannel.
 *
 * Scenario:
 * - 3 APs on different 2.4 GHz channels (1, 6, 11)
 * - 1 monitoring node with scanning radio
 * - Scanning radio hops across channels {1, 6, 11} every 500ms
 * - Beacons are collected in a cache and queried periodically
 *
 * This demonstrates:
 * - Channel-agnostic API with YansWifiChannel
 * - Multi-channel beacon detection via channel hopping
 * - Beacon cache querying
 * - RSSI/RCPI measurements
 */

#include "ns3/core-module.h"
#include "ns3/dual-phy-sniffer-helper.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DualPhySnifferExample");

// Global sniffer helper for beacon cache queries
DualPhySnifferHelper g_dualPhySniffer;

/**
 * @brief Print all beacons discovered by the scanning radio
 */
void
PrintBeaconCache()
{
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║  Beacon Cache Status (t=" << Simulator::Now().GetSeconds() << "s)\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";

    auto allBeacons = g_dualPhySniffer.GetAllBeacons();

    if (allBeacons.empty())
    {
        std::cout << "  No beacons detected yet.\n";
        return;
    }

    std::cout << "  Total beacon entries: " << allBeacons.size() << "\n\n";

    for (const auto& beaconEntry : allBeacons)
    {
        const BeaconInfo& info = beaconEntry.second;

        std::cout << "  TX BSSID: " << info.bssid << "\n";
        std::cout << "    Received by: " << info.receivedBy << "\n";
        std::cout << "    RSSI: " << info.rssi << " dBm\n";
        std::cout << "    RCPI: " << info.rcpi << "\n";
        std::cout << "    SNR: " << info.snr << " dB\n";
        std::cout << "    Channel: " << (int)info.channel << "\n";
        std::cout << "    Beacon count: " << info.beaconCount << "\n";
        std::cout << "    Last seen: " << info.timestamp.GetSeconds() << "s\n";
        std::cout << "\n";
    }
}

int
main(int argc, char* argv[])
{
    // Configuration
    bool verbose = false;
    double simTime = 10.0;
    double hopInterval = 0.5; // 500ms per channel

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.AddValue("simTime", "Simulation duration (seconds)", simTime);
    cmd.AddValue("hopInterval", "Channel hopping interval (seconds)", hopInterval);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("DualPhySnifferExample", LOG_LEVEL_INFO);
        LogComponentEnable("DualPhySnifferHelper", LOG_LEVEL_INFO);
    }

    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║  DualPhySnifferHelper Basic Example             ║\n";
    std::cout << "║  (YansWifiChannel - 2.4 GHz)                     ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";

    // ===== CREATE NODES =====
    NodeContainer apNodes;
    apNodes.Create(3); // 3 APs

    NodeContainer monitorNode;
    monitorNode.Create(1); // 1 monitoring node with scanning radio

    std::cout << "[Setup] Created 3 AP nodes + 1 monitoring node\n";

    // ===== SETUP YANS WIFI CHANNEL =====
    YansWifiChannelHelper channelHelper = YansWifiChannelHelper::Default();
    Ptr<YansWifiChannel> channel = channelHelper.Create();

    std::cout << "[Setup] Created YansWifiChannel with default propagation\n";

    // ===== WIFI HELPER SETUP =====
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("HtMcs7"),
                                 "ControlMode",
                                 StringValue("HtMcs0"));

    Ssid ssid = Ssid("DualPhySniffer-Demo");

    // ===== CREATE APS ON DIFFERENT CHANNELS =====
    std::vector<uint8_t> apChannels = {1, 6, 11}; // 2.4 GHz non-overlapping channels
    NetDeviceContainer apDevices;

    for (uint32_t i = 0; i < 3; i++)
    {
        YansWifiPhyHelper apPhy;
        apPhy.SetChannel(channel);

        // Configure channel: {channel, width, band, primary20}
        std::ostringstream chStr;
        chStr << "{" << (int)apChannels[i] << ", 20, BAND_2_4GHZ, 0}";
        apPhy.Set("ChannelSettings", StringValue(chStr.str()));

        WifiMacHelper apMac;
        apMac.SetType("ns3::ApWifiMac",
                     "Ssid",
                     SsidValue(ssid),
                     "BeaconGeneration",
                     BooleanValue(true),
                     "BeaconInterval",
                     TimeValue(MicroSeconds(102400))); // ~100ms

        NetDeviceContainer apDevice = wifi.Install(apPhy, apMac, apNodes.Get(i));
        apDevices.Add(apDevice);

        std::cout << "[Setup] AP" << i << " created on Channel " << (int)apChannels[i]
                  << " (BSSID will be assigned at runtime)\n";
    }

    // ===== SETUP MOBILITY (APS IN A LINE) =====
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));   // AP0
    positionAlloc->Add(Vector(50.0, 0.0, 0.0));  // AP1
    positionAlloc->Add(Vector(100.0, 0.0, 0.0)); // AP2
    positionAlloc->Add(Vector(50.0, 50.0, 0.0)); // Monitor (center, elevated)

    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNodes);
    mobility.Install(monitorNode);

    std::cout << "[Setup] AP positions: (0,0,0), (50,0,0), (100,0,0)\n";
    std::cout << "[Setup] Monitor position: (50,50,0) - center, elevated view\n\n";

    // ===== SETUP DUAL-PHY SNIFFER =====
    std::cout << "[Sniffer] Configuring DualPhySnifferHelper...\n";

    // Channel-agnostic: Set YansWifiChannel (could also use SpectrumChannel)
    g_dualPhySniffer.SetChannel(channel);

    // Set channels to scan (must match AP channels for beacon detection)
    g_dualPhySniffer.SetScanningChannels({1, 6, 11});

    // Set channel hopping interval
    g_dualPhySniffer.SetHopInterval(Seconds(hopInterval));

    std::cout << "[Sniffer] Scanning channels: {1, 6, 11}\n";
    std::cout << "[Sniffer] Hop interval: " << hopInterval << "s\n\n";

    // Install scanning radio on monitoring node
    // Parameters: node, initial channel, MAC address (any unique address)
    g_dualPhySniffer.Install(monitorNode.Get(0), 1, Mac48Address("AA:BB:CC:DD:EE:FF"));

    // Start channel hopping
    g_dualPhySniffer.StartChannelHopping();

    std::cout << "[Sniffer] ✓ Scanning radio installed on monitoring node\n";
    std::cout << "[Sniffer] ✓ Channel hopping started\n\n";

    // ===== SCHEDULE BEACON CACHE QUERIES =====
    std::cout << "[Schedule] Beacon cache will be printed at t=2s, t=5s, t=8s\n\n";

    Simulator::Schedule(Seconds(2.0), &PrintBeaconCache);
    Simulator::Schedule(Seconds(5.0), &PrintBeaconCache);
    Simulator::Schedule(Seconds(8.0), &PrintBeaconCache);

    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║  Starting Simulation (" << simTime << "s)                      ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";

    // ===== RUN SIMULATION =====
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // ===== FINAL SUMMARY =====
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║  Simulation Complete - Final Summary             ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";

    PrintBeaconCache();

    std::cout << "\n[Info] This example demonstrated:\n";
    std::cout << "  ✓ DualPhySnifferHelper with YansWifiChannel\n";
    std::cout << "  ✓ Multi-channel beacon monitoring (channels 1, 6, 11)\n";
    std::cout << "  ✓ Channel hopping every " << hopInterval << "s\n";
    std::cout << "  ✓ Beacon cache querying with GetAllBeacons()\n";
    std::cout << "  ✓ RSSI/RCPI measurements\n\n";

    std::cout << "[Tip] For advanced Spectrum-based example with RRM matrices,\n";
    std::cout << "      run: ./ns3 run spectrum-dual-radio-example\n\n";

    Simulator::Destroy();
    return 0;
}
