#include "simulation-helper.h"
#include "ns3/log.h"
#include "ns3/wifi-net-device.h"
#include "ns3/node.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"

#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("SimulationHelper");

DualPhySnifferHelper*
SimulationHelper::SetupAPDualPhySniffer(
    NetDeviceContainer apDevices,
    std::vector<Mac48Address> apMacs,
    Ptr<YansWifiChannel> channel,
    std::vector<uint8_t> operatingChannels,
    std::vector<uint8_t> scanningChannels,
    Time hopInterval)
{
    NS_LOG_FUNCTION_NOARGS();

    // Create dual-PHY sniffer for APs (used by Neighbor Protocol)
    DualPhySnifferHelper* dualPhySniffer = new DualPhySnifferHelper();
    dualPhySniffer->SetChannel(channel);
    dualPhySniffer->SetScanningChannels(scanningChannels);
    dualPhySniffer->SetHopInterval(hopInterval);

    // Install dual-PHY on all APs
    for (uint32_t i = 0; i < apDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(apDevices.Get(i));
        Ptr<Node> node = apDevice->GetNode();
        uint8_t operatingChannel = operatingChannels[i];
        Mac48Address apMac = apMacs[i];

        dualPhySniffer->Install(node, operatingChannel, apMac);

        NS_LOG_INFO("Installed AP dual-PHY sniffer on AP " << i
                    << " (MAC: " << apMac
                    << ", Channel: " << (int)operatingChannel << ")");
    }

    // Start channel hopping
    dualPhySniffer->StartChannelHopping();

    NS_LOG_INFO("Started channel hopping on all APs");
    NS_LOG_INFO("Scanning channels: " << scanningChannels.size()
                << ", Hop interval: " << hopInterval.GetMilliSeconds() << " ms");

    return dualPhySniffer;
}

DualPhySnifferHelper*
SimulationHelper::SetupSTADualPhySniffer(
    NetDeviceContainer staDevices,
    std::vector<Mac48Address> staMacs,
    Ptr<YansWifiChannel> channel,
    std::vector<uint8_t> operatingChannels,
    std::vector<uint8_t> scanningChannels,
    Time hopInterval)
{
    NS_LOG_FUNCTION_NOARGS();

    // Create dual-PHY sniffer for STAs (used by Beacon Protocol)
    DualPhySnifferHelper* dualPhySniffer = new DualPhySnifferHelper();
    dualPhySniffer->SetChannel(channel);
    dualPhySniffer->SetScanningChannels(scanningChannels);
    dualPhySniffer->SetHopInterval(hopInterval);

    // Install dual-PHY on all STAs
    for (uint32_t i = 0; i < staDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        Ptr<Node> node = staDevice->GetNode();
        uint8_t operatingChannel = operatingChannels[i];
        Mac48Address staMac = staMacs[i];

        dualPhySniffer->Install(node, operatingChannel, staMac);

        NS_LOG_INFO("Installed STA dual-PHY sniffer on STA " << i
                    << " (MAC: " << staMac
                    << ", Channel: " << (int)operatingChannel << ")");
    }

    // Start channel hopping
    dualPhySniffer->StartChannelHopping();

    NS_LOG_INFO("Started channel hopping on all STAs");
    NS_LOG_INFO("Scanning channels: " << scanningChannels.size()
                << ", Hop interval: " << hopInterval.GetMilliSeconds() << " ms");

    return dualPhySniffer;
}

std::vector<Ptr<NeighborProtocolHelper>>
SimulationHelper::SetupNeighborProtocol(
    NetDeviceContainer apDevices,
    NetDeviceContainer staDevices,
    std::vector<Mac48Address> apMacs,
    std::vector<ApInfo> neighborTable,
    DualPhySnifferHelper* apDualPhySniffer
)
{
    NS_LOG_FUNCTION_NOARGS();

    std::vector<Ptr<NeighborProtocolHelper>> neighborProtocols;

    NS_LOG_UNCOND("\n========== Setting up Neighbor Protocol (802.11k) ==========");
    NS_LOG_UNCOND("Both APs and STAs will have Neighbor Protocol capability");
    NS_LOG_UNCOND("Sniffer mode: " << (apDualPhySniffer != nullptr ? "ACTIVE on APs (apDualPhySniffer)" : "PASSIVE (no sniffer)"));

    // Setup neighbor protocol for each AP (WITH dual-PHY sniffer for passive neighbor discovery)
    NS_LOG_UNCOND("\nInstalling Neighbor Protocol on APs:");
    for (uint32_t i = 0; i < apDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(apDevices.Get(i));
        Ptr<Node> node = apDevice->GetNode();
        Ptr<NeighborProtocolHelper> neighborProtocol = CreateObject<NeighborProtocolHelper>();

        // Use AP dual-PHY sniffer if provided (for passive neighbor discovery via beacon cache)
        if (apDualPhySniffer != nullptr)
        {
            neighborProtocol->SetDualPhySniffer(apDualPhySniffer);
            NS_LOG_UNCOND("  ✓ AP " << i << " (Node " << node->GetId()
                          << ", MAC: " << apMacs[i]
                          << "): Neighbor Protocol installed with ACTIVE scanning (apDualPhySniffer)");
        }
        else
        {
            NS_LOG_UNCOND("  ✓ AP " << i << " (Node " << node->GetId()
                          << ", MAC: " << apMacs[i]
                          << "): Neighbor Protocol installed in PASSIVE mode (no sniffer)");
        }

        neighborProtocol->SetNeighborTable(neighborTable);
        neighborProtocol->InstallOnAp(apDevice);

        neighborProtocols.push_back(neighborProtocol);
    }

    // Setup neighbor protocol for each STA (WITHOUT sniffer - STAs just request neighbor reports)
    NS_LOG_UNCOND("\nInstalling Neighbor Protocol on STAs:");
    for (uint32_t i = 0; i < staDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        Ptr<Node> node = staDevice->GetNode();
        Ptr<WifiMac> mac = staDevice->GetMac();
        Mac48Address staMac = Mac48Address::ConvertFrom(mac->GetAddress());
        Ptr<NeighborProtocolHelper> neighborProtocol = CreateObject<NeighborProtocolHelper>();

        // STAs don't need sniffer for Neighbor Protocol - they just request reports from APs
        neighborProtocol->InstallOnSta(staDevice);

        neighborProtocols.push_back(neighborProtocol);

        NS_LOG_UNCOND("  ✓ STA " << i << " (Node " << node->GetId()
                      << ", MAC: " << staMac
                      << "): Neighbor Protocol installed (can request neighbor reports, no sniffer)");
    }

    NS_LOG_UNCOND("\n✓ Neighbor Protocol setup complete:");
    NS_LOG_UNCOND("  - Total instances: " << neighborProtocols.size());
    NS_LOG_UNCOND("  - APs configured: " << apDevices.GetN() << " (with apDualPhySniffer)");
    NS_LOG_UNCOND("  - STAs configured: " << staDevices.GetN() << " (without sniffer)");
    NS_LOG_UNCOND("  - Neighbor table entries: " << neighborTable.size());
    NS_LOG_UNCOND("============================================================\n");

    return neighborProtocols;
}

std::vector<Ptr<BeaconProtocolHelper>>
SimulationHelper::SetupBeaconProtocol(
    NetDeviceContainer apDevices,
    NetDeviceContainer staDevices,
    DualPhySnifferHelper* staDualPhySniffer)
{
    NS_LOG_FUNCTION_NOARGS();

    std::vector<Ptr<BeaconProtocolHelper>> beaconProtocols;

    NS_LOG_UNCOND("\n========== Setting up Beacon Protocol (802.11k) ==========");
    NS_LOG_UNCOND("Both APs and STAs will have Beacon Protocol capability");
    NS_LOG_UNCOND("Sniffer mode: " << (staDualPhySniffer != nullptr ? "ACTIVE on STAs (staDualPhySniffer)" : "PASSIVE (no sniffer)"));

    // Setup beacon protocol for each AP (WITHOUT sniffer - APs just request beacon reports from STAs)
    NS_LOG_UNCOND("\nInstalling Beacon Protocol on APs:");
    for (uint32_t i = 0; i < apDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(apDevices.Get(i));
        Ptr<Node> node = apDevice->GetNode();
        Ptr<WifiMac> mac = apDevice->GetMac();
        Mac48Address apMac = Mac48Address::ConvertFrom(mac->GetAddress());
        Ptr<BeaconProtocolHelper> beaconProtocol = CreateObject<BeaconProtocolHelper>();

        // APs don't need sniffer for Beacon Protocol - they request reports from STAs
        beaconProtocol->InstallOnAp(apDevice);

        beaconProtocols.push_back(beaconProtocol);

        NS_LOG_UNCOND("  ✓ AP " << i << " (Node " << node->GetId()
                      << ", MAC: " << apMac
                      << "): Beacon Protocol installed (can request beacon reports, no sniffer)");
    }

    // Setup beacon protocol for each STA (WITH dual-PHY sniffer for active multi-channel scanning)
    NS_LOG_UNCOND("\nInstalling Beacon Protocol on STAs:");
    for (uint32_t i = 0; i < staDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        Ptr<Node> node = staDevice->GetNode();
        Ptr<WifiMac> mac = staDevice->GetMac();
        Mac48Address staMac = Mac48Address::ConvertFrom(mac->GetAddress());
        Ptr<BeaconProtocolHelper> beaconProtocol = CreateObject<BeaconProtocolHelper>();

        // Use STA dual-PHY sniffer if provided (for active multi-channel beacon scanning)
        if (staDualPhySniffer != nullptr)
        {
            beaconProtocol->SetDualPhySniffer(staDualPhySniffer);
            NS_LOG_UNCOND("  ✓ STA " << i << " (Node " << node->GetId()
                          << ", MAC: " << staMac
                          << "): Beacon Protocol installed with ACTIVE scanning (staDualPhySniffer)");
        }
        else
        {
            NS_LOG_UNCOND("  ✓ STA " << i << " (Node " << node->GetId()
                          << ", MAC: " << staMac
                          << "): Beacon Protocol installed in PASSIVE mode (no sniffer)");
        }

        beaconProtocol->InstallOnSta(staDevice);

        beaconProtocols.push_back(beaconProtocol);
    }

    NS_LOG_UNCOND("\n✓ Beacon Protocol setup complete:");
    NS_LOG_UNCOND("  - Total instances: " << beaconProtocols.size());
    NS_LOG_UNCOND("  - APs configured: " << apDevices.GetN() << " (without sniffer)");
    NS_LOG_UNCOND("  - STAs configured: " << staDevices.GetN() << " (with staDualPhySniffer)");
    NS_LOG_UNCOND("============================================================\n");

    return beaconProtocols;
}

std::vector<Ptr<BssTm11vHelper>>
SimulationHelper::SetupBssTmHelper(
    NetDeviceContainer apDevices,
    NetDeviceContainer staDevices)
{
    NS_LOG_FUNCTION_NOARGS();

    std::vector<Ptr<BssTm11vHelper>> bssTmHelpers;

    NS_LOG_UNCOND("\n========== Setting up BSS Transition Management (802.11v) ==========");
    NS_LOG_UNCOND("Both APs and STAs will have BSS TM capability");

    // Setup BSS TM helper for each AP
    NS_LOG_UNCOND("\nInstalling BSS TM on APs:");
    for (uint32_t i = 0; i < apDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(apDevices.Get(i));
        Ptr<Node> node = apDevice->GetNode();
        Ptr<WifiMac> mac = apDevice->GetMac();
        Mac48Address apMac = Mac48Address::ConvertFrom(mac->GetAddress());
        Ptr<BssTm11vHelper> bssTmHelper = CreateObject<BssTm11vHelper>();

        bssTmHelper->InstallOnAp(apDevice);

        bssTmHelpers.push_back(bssTmHelper);

        NS_LOG_UNCOND("  ✓ AP " << i << " (Node " << node->GetId()
                      << ", MAC: " << apMac
                      << "): BSS TM installed (can send transition requests to STAs)");
    }

    // Setup BSS TM helper for each STA
    NS_LOG_UNCOND("\nInstalling BSS TM on STAs:");
    for (uint32_t i = 0; i < staDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        Ptr<Node> node = staDevice->GetNode();
        Ptr<WifiMac> mac = staDevice->GetMac();
        Mac48Address staMac = Mac48Address::ConvertFrom(mac->GetAddress());
        Ptr<BssTm11vHelper> bssTmHelper = CreateObject<BssTm11vHelper>();

        bssTmHelper->InstallOnSta(staDevice);

        bssTmHelpers.push_back(bssTmHelper);

        NS_LOG_UNCOND("  ✓ STA " << i << " (Node " << node->GetId()
                      << ", MAC: " << staMac
                      << "): BSS TM installed (can receive and process transition requests)");
    }

    NS_LOG_UNCOND("\n✓ BSS TM setup complete:");
    NS_LOG_UNCOND("  - Total instances: " << bssTmHelpers.size());
    NS_LOG_UNCOND("  - APs configured: " << apDevices.GetN());
    NS_LOG_UNCOND("  - STAs configured: " << staDevices.GetN());
    NS_LOG_UNCOND("====================================================================\n");

    return bssTmHelpers;
}

// std::tuple<AutoRoamingKvHelper*,
//            std::vector<Ptr<LinkMeasurementProtocol>>,
//            std::vector<Ptr<LinkMeasurementProtocol>>>
// SimulationHelper::SetupAutoRoamingKvHelper(
//     NetDeviceContainer apDevices,
//     NetDeviceContainer staDevices,
//     Ptr<NeighborProtocolHelper> neighborProtocolSta,
//     Ptr<BeaconProtocolHelper> beaconProtocolSta,
//     std::vector<Ptr<BeaconProtocolHelper>> beaconProtocolAps,
//     Ptr<BssTm11vHelper> bssTmHelperSta,
//     Time measurementInterval,
//     double rssiThreshold)
// {
//     NS_LOG_FUNCTION_NOARGS();

//     // Create helper on heap so it stays alive for the entire simulation
//     AutoRoamingKvHelper* helper = new AutoRoamingKvHelper();
//     helper->SetMeasurementInterval(measurementInterval);
//     helper->SetRssiThreshold(rssiThreshold);

//     // Install on APs
//     std::vector<Ptr<LinkMeasurementProtocol>> apProtocols = helper->InstallAp(apDevices);

//     NS_LOG_INFO("Installed Link Measurement Protocol on " << apDevices.GetN() << " APs");

//     // Install on STAs (with neighbor, beacon, and BSS TM protocols)
//     // The helper will auto-detect the currently associated AP
//     std::vector<Ptr<LinkMeasurementProtocol>> staProtocols =
//         helper->InstallSta(staDevices, neighborProtocolSta,
//                           beaconProtocolSta, bssTmHelperSta);
    
//     // Connect helper's beacon callback to AP instances
//     // The helper connects to the STA instance, but beacon reports are received by APs
//     for (auto& beaconProtocolAp : beaconProtocolAps)
//     {
//         beaconProtocolAp->m_beaconReportReceivedTrace.ConnectWithoutContext(
//             MakeCallback(&AutoRoamingKvHelper::OnBeaconReportReceived, helper));
//     }
    
//     NS_LOG_INFO("Installed Link Measurement Protocol on " << staDevices.GetN() << " STAs");
//     NS_LOG_INFO("  - Measurement interval: " << measurementInterval.GetSeconds() << " s");
//     NS_LOG_INFO("  - RSSI threshold: " << rssiThreshold << " dBm");
//     NS_LOG_INFO("  - AP detection: automatic (will detect current association)");
    
//     return std::make_tuple(helper, apProtocols, staProtocols);
// }

SimulationHelper::AutoRoamingKvHelperContainer
SimulationHelper::SetupAutoRoamingKvHelperMulti(
    NetDeviceContainer apDevices,
    NetDeviceContainer staDevices,
    std::vector<Ptr<NeighborProtocolHelper>> neighborProtocolStas,
    std::vector<Ptr<BeaconProtocolHelper>> beaconProtocolStas,
    std::vector<Ptr<BeaconProtocolHelper>> beaconProtocolAps,
    std::vector<Ptr<BssTm11vHelper>> bssTmHelperStas,
    Time measurementInterval,
    double rssiThreshold)
{
    NS_LOG_FUNCTION_NOARGS();

    uint32_t numStas = staDevices.GetN();

    // Validate input sizes - ensures each STA has all required protocol instances
    NS_ASSERT_MSG(neighborProtocolStas.size() == numStas,
                  "Number of neighbor protocol instances must match number of STAs");
    NS_ASSERT_MSG(beaconProtocolStas.size() == numStas,
                  "Number of beacon protocol instances must match number of STAs");
    NS_ASSERT_MSG(bssTmHelperStas.size() == numStas,
                  "Number of BSS TM helper instances must match number of STAs");

    AutoRoamingKvHelperContainer container;

    NS_LOG_UNCOND("\n========== Setting up AutoRoamingKvHelper (Link Measurement Protocol) ==========");
    NS_LOG_UNCOND("Creating " << numStas << " roaming orchestration instances (one per STA)");
    NS_LOG_UNCOND("Architecture: Link Measurement → Neighbor Protocol → Beacon Protocol → BSS TM");
    NS_LOG_UNCOND("\nConfiguration:");
    NS_LOG_UNCOND("  - Measurement interval: " << measurementInterval.GetSeconds() << " s");
    NS_LOG_UNCOND("  - RSSI threshold: " << rssiThreshold << " dBm");
    NS_LOG_UNCOND("  - APs monitored: " << apDevices.GetN());
    NS_LOG_UNCOND("  - STAs managed: " << numStas);

    // Create one AutoRoamingKvHelper instance per STA
    for (uint32_t i = 0; i < numStas; i++)
    {
        // Create helper instance on heap (stays alive for entire simulation)
        AutoRoamingKvHelper* helper = new AutoRoamingKvHelper();
        helper->SetMeasurementInterval(measurementInterval);
        helper->SetRssiThreshold(rssiThreshold);

        NS_LOG_UNCOND("\n[STA " << i << " Roaming Setup]");

        // Install Link Measurement Protocol on all APs
        // (Each helper installs its own AP protocol instances)
        std::vector<Ptr<LinkMeasurementProtocol>> apProtocols = helper->InstallAp(apDevices);

        // Log each AP's Link Measurement installation
        for (uint32_t j = 0; j < apDevices.GetN(); ++j)
        {
            Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(apDevices.Get(j));
            Ptr<Node> apNode = apDevice->GetNode();
            Ptr<WifiMac> apMac = apDevice->GetMac();
            Mac48Address apMacAddr = Mac48Address::ConvertFrom(apMac->GetAddress());

            NS_LOG_UNCOND("  ✓ AP " << j << " (Node " << apNode->GetId()
                          << ", MAC: " << apMacAddr
                          << "): Link Measurement Protocol installed");
        }

        // Install Link Measurement Protocol on this specific STA
        NetDeviceContainer singleStaDev(staDevices.Get(i));
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        Ptr<Node> staNode = staDevice->GetNode();
        Ptr<WifiMac> staMac = staDevice->GetMac();
        Mac48Address staMacAddr = Mac48Address::ConvertFrom(staMac->GetAddress());

        std::vector<Ptr<LinkMeasurementProtocol>> staProtocols =
            helper->InstallSta(singleStaDev,
                              neighborProtocolStas[i],
                              beaconProtocolStas[i],
                              bssTmHelperStas[i]);

        NS_LOG_UNCOND("  ✓ STA " << i << " (Node " << staNode->GetId()
                      << ", MAC: " << staMacAddr
                      << "): Link Measurement Protocol installed");
        NS_LOG_UNCOND("      - Connected to Neighbor Protocol (passive - requests from AP)");
        NS_LOG_UNCOND("      - Connected to Beacon Protocol (active - scans with staDualPhySniffer)");
        NS_LOG_UNCOND("      - Connected to BSS TM (processes transition requests)");

        // Connect helper's beacon callback to ALL AP beacon protocol instances
        // This is critical: the helper needs to receive beacon reports from all APs
        for (auto& beaconProtocolAp : beaconProtocolAps)
        {
            beaconProtocolAp->m_beaconReportReceivedTrace.ConnectWithoutContext(
                MakeCallback(&AutoRoamingKvHelper::OnBeaconReportReceived, helper));
        }

        NS_LOG_UNCOND("  ✓ Connected beacon report callbacks from " << beaconProtocolAps.size() << " APs");

        // Store in container
        container.helpers.push_back(helper);
        container.apProtocols.push_back(apProtocols);
        container.staProtocols.push_back(staProtocols);

        NS_LOG_UNCOND("  ✓ Roaming orchestration for STA " << i << " complete");
    }

    NS_LOG_UNCOND("\n✓ AutoRoamingKvHelper setup complete for all " << numStas << " STAs");
    NS_LOG_UNCOND("  Each STA can now measure link quality and trigger roaming when RSSI drops");
    NS_LOG_UNCOND("=============================================================================\n");

    return container;
}

std::vector<Ptr<WifiNetDevice>>
SimulationHelper::GetWifiNetDevices(NetDeviceContainer devices)
{
    NS_LOG_FUNCTION_NOARGS();

    std::vector<Ptr<WifiNetDevice>> wifiDevices;

    for (uint32_t i = 0; i < devices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(devices.Get(i));
        wifiDevices.push_back(wifiDevice);
    }

    NS_LOG_INFO("Converted " << devices.GetN() << " devices to WifiNetDevice pointers");

    return wifiDevices;
}

NetDeviceContainer
SimulationHelper::InstallApDevices(WifiHelper& wifi,
                                    YansWifiPhyHelper& phy,
                                    WifiMacHelper& mac,
                                    NodeContainer nodes,
                                    const std::vector<uint8_t>& channels,
                                    Ssid ssid,
                                    double txPower,
                                    uint16_t channelWidth,
                                    WifiPhyBand band)
{
    NS_LOG_FUNCTION_NOARGS();

    NS_ASSERT_MSG(nodes.GetN() == channels.size(),
                  "Number of AP nodes must match number of channels");

    NetDeviceContainer apDevices;

    // Convert band enum to string
    std::string bandStr;
    if (band == WIFI_PHY_BAND_2_4GHZ)
        bandStr = "BAND_2_4GHZ";
    else if (band == WIFI_PHY_BAND_5GHZ)
        bandStr = "BAND_5GHZ";
    else if (band == WIFI_PHY_BAND_6GHZ)
        bandStr = "BAND_6GHZ";
    else
        bandStr = "BAND_5GHZ"; // Default

    // Configure AP MAC type once (common settings for all APs)
    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid),
                "BeaconInterval", TimeValue(MicroSeconds(102400)),
                "BE_MaxAmpduSize", UintegerValue(0),  // Disable aggregation for bridge compatibility
                "BK_MaxAmpduSize", UintegerValue(0),
                "VI_MaxAmpduSize", UintegerValue(0),
                "VO_MaxAmpduSize", UintegerValue(0));

    // Install each AP with its specific channel
    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        // Set channel settings for this AP
        std::ostringstream channelStr;
        channelStr << "{" << (int)channels[i] << ", " << channelWidth << ", " << bandStr << ", 0}";
        phy.Set("ChannelSettings", StringValue(channelStr.str()));

        // Set TX power
        phy.Set("TxPowerStart", DoubleValue(txPower));
        phy.Set("TxPowerEnd", DoubleValue(txPower));

        // Install on this AP node
        NetDeviceContainer apDevice = wifi.Install(phy, mac, nodes.Get(i));
        apDevices.Add(apDevice);

        NS_LOG_INFO("[SimulationHelper] Installed AP " << i << " on channel " << (int)channels[i]
                    << " (" << channelWidth << " MHz, " << bandStr << ")");
    }

    NS_LOG_INFO("[SimulationHelper] Installed " << apDevices.GetN() << " AP devices");
    return apDevices;
}

NetDeviceContainer
SimulationHelper::InstallStaDevices(WifiHelper& wifi,
                                     YansWifiPhyHelper& phy,
                                     WifiMacHelper& mac,
                                     NodeContainer nodes,
                                     const std::vector<uint8_t>& channels,
                                     Ssid ssid,
                                     uint16_t channelWidth,
                                     WifiPhyBand band)
{
    NS_LOG_FUNCTION_NOARGS();

    NS_ASSERT_MSG(nodes.GetN() == channels.size(),
                  "Number of STA nodes must match number of channels");

    NetDeviceContainer staDevices;

    // Convert band enum to string
    std::string bandStr;
    if (band == WIFI_PHY_BAND_2_4GHZ)
        bandStr = "BAND_2_4GHZ";
    else if (band == WIFI_PHY_BAND_5GHZ)
        bandStr = "BAND_5GHZ";
    else if (band == WIFI_PHY_BAND_6GHZ)
        bandStr = "BAND_6GHZ";
    else
        bandStr = "BAND_5GHZ"; // Default

    // Configure STA MAC type once (common settings for all STAs)
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(false),                      // Keep passive scanning (beacon listening)
                "MaxMissedBeacons", UintegerValue(30),                     // Increased for 24hr resilience
                "AssocRequestTimeout", TimeValue(MilliSeconds(500)),
                "BE_MaxAmpduSize", UintegerValue(0),  // Disable aggregation to prevent Block Ack race during roaming
                "BK_MaxAmpduSize", UintegerValue(0),
                "VI_MaxAmpduSize", UintegerValue(0),
                "VO_MaxAmpduSize", UintegerValue(0));

    // Install each STA with its specific channel
    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        // Set channel settings for this STA
        std::ostringstream channelStr;
        channelStr << "{" << (int)channels[i] << ", " << channelWidth << ", " << bandStr << ", 0}";
        phy.Set("ChannelSettings", StringValue(channelStr.str()));

        // Install on this STA node
        NetDeviceContainer staDevice = wifi.Install(phy, mac, nodes.Get(i));
        staDevices.Add(staDevice);

        NS_LOG_INFO("[SimulationHelper] Installed STA " << i << " on channel " << (int)channels[i]
                    << " (" << channelWidth << " MHz, " << bandStr << ")");
    }

    NS_LOG_INFO("[SimulationHelper] Installed " << staDevices.GetN() << " STA devices");
    return staDevices;
}

} // namespace ns3