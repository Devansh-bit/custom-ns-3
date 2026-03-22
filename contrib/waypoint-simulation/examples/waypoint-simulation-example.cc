/*
 * Waypoint Simulation Example
 *
 * Demonstrates the use of waypoint-simulation module helpers:
 * - WaypointGrid for waypoint management
 * - WaypointMobilityHelper for STA waypoint-based mobility
 * - SimulationConfigParser for JSON configuration loading
 *
 * STAs move randomly between waypoints with configurable timing and velocities.
 * APs are configured with LeverAPI for dynamic PHY parameters.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/lever-api.h"
#include "ns3/lever-api-helper.h"
#include "ns3/simulation-helper.h"

// Waypoint simulation module
#include "ns3/waypoint-grid.h"
#include "ns3/waypoint-mobility-helper.h"
#include "ns3/simulation-config-parser.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WaypointSimulationExample");

int
main(int argc, char* argv[])
{
    // ========================================================================
    // Command Line Arguments
    // ========================================================================

    std::string configFile = "waypoint-sim-config.json";
    bool verbose = false;

    CommandLine cmd;
    cmd.AddValue("configFile", "Path to JSON configuration file", configFile);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("WaypointSimulationExample", LOG_LEVEL_INFO);
        LogComponentEnable("WaypointGrid", LOG_LEVEL_INFO);
        LogComponentEnable("WaypointMobilityHelper", LOG_LEVEL_INFO);
        LogComponentEnable("SimulationConfigParser", LOG_LEVEL_INFO);
    }

    // ========================================================================
    // Load Configuration
    // ========================================================================

    NS_LOG_INFO("Loading configuration from: " << configFile);
    SimulationConfigData config = SimulationConfigParser::ParseFile(configFile);

    // ========================================================================
    // Build Waypoint Grid
    // ========================================================================

    Ptr<WaypointGrid> waypointGrid = CreateObject<WaypointGrid>();

    for (const auto& wp : config.waypoints)
    {
        waypointGrid->AddWaypoint(wp.id, wp.position);
    }

    NS_LOG_INFO("Built waypoint grid with " << waypointGrid->GetWaypointCount() << " waypoints");

    // ========================================================================
    // Create Nodes
    // ========================================================================

    NodeContainer apNodes;
    apNodes.Create(config.aps.size());

    NodeContainer staNodes;
    staNodes.Create(config.stas.size());

    NS_LOG_INFO("Created " << apNodes.GetN() << " AP nodes and "
                << staNodes.GetN() << " STA nodes");

    // ========================================================================
    // Apply AP Mobility (Constant Position)
    // ========================================================================

    for (size_t i = 0; i < config.aps.size(); i++)
    {
        Ptr<ConstantPositionMobilityModel> apMobility =
            CreateObject<ConstantPositionMobilityModel>();
        apMobility->SetPosition(config.aps[i].position);
        apNodes.Get(i)->AggregateObject(apMobility);

        NS_LOG_INFO("Set AP Node " << apNodes.Get(i)->GetId()
                    << " position to " << config.aps[i].position);
    }

    // ========================================================================
    // WiFi Configuration
    // ========================================================================

    // Spectrum channel
    SpectrumWifiPhyHelper spectrumPhy;
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
    Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(lossModel);
    Ptr<ConstantSpeedPropagationDelayModel> delayModel =
        CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);
    spectrumPhy.SetChannel(spectrumChannel);

    // WiFi Helper
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ac);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                  "DataMode",
                                  StringValue("VhtMcs8"),
                                  "ControlMode",
                                  StringValue("VhtMcs0"));

    // MAC Helper
    WifiMacHelper wifiMac;
    Ssid ssid = Ssid("WaypointNetwork");

    // Extract AP channels from config
    std::vector<uint8_t> apChannels;
    for (const auto& ap : config.aps)
    {
        apChannels.push_back(ap.channel);
    }

    // STAs start on first AP's channel
    std::vector<uint8_t> staChannels(config.stas.size(), apChannels[0]);

    // Install AP devices
    NetDeviceContainer apDevices;
    for (size_t i = 0; i < apNodes.GetN(); ++i)
    {
        uint8_t channel = apChannels[i];
        std::ostringstream oss;
        oss << "{" << (int)channel << ", 20, BAND_5GHZ, 0}";
        spectrumPhy.Set("ChannelSettings", StringValue(oss.str()));

        wifiMac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));

        NetDeviceContainer dev = wifi.Install(spectrumPhy, wifiMac, apNodes.Get(i));
        apDevices.Add(dev);
    }

    NS_LOG_INFO("Installed AP WiFi devices");

    // Install STA devices
    NetDeviceContainer staDevices;
    for (size_t i = 0; i < staNodes.GetN(); ++i)
    {
        uint8_t channel = staChannels[i];
        std::ostringstream oss;
        oss << "{" << (int)channel << ", 20, BAND_5GHZ, 0}";
        spectrumPhy.Set("ChannelSettings", StringValue(oss.str()));

        wifiMac.SetType("ns3::StaWifiMac",
                        "Ssid",
                        SsidValue(ssid),
                        "ActiveProbing",
                        BooleanValue(true));

        NetDeviceContainer dev = wifi.Install(spectrumPhy, wifiMac, staNodes.Get(i));
        staDevices.Add(dev);
    }

    NS_LOG_INFO("Installed STA WiFi devices");

    // ========================================================================
    // Apply LeverAPI Configuration to APs
    // ========================================================================

    for (size_t i = 0; i < config.aps.size(); i++)
    {
        const ApConfigData& apConfig = config.aps[i];
        Ptr<Node> apNode = apNodes.Get(i);

        // Create LeverConfig
        Ptr<LeverConfig> leverConfig = CreateObject<LeverConfig>();

        // Set TX power
        leverConfig->SetTxPower(apConfig.txPower);

        // Set CCA threshold
        leverConfig->SetCcaEdThreshold(apConfig.ccaThreshold);

        // Set RX sensitivity
        leverConfig->SetRxSensitivity(apConfig.rxSensitivity);

        // Set channel
        leverConfig->SwitchChannel(apConfig.channel);

        // Install LeverApi application
        LeverApiHelper leverHelper(leverConfig);
        ApplicationContainer leverApp = leverHelper.Install(apNode);
        leverApp.Start(Seconds(0.0));
        leverApp.Stop(Seconds(config.simulationTime));

        NS_LOG_INFO("Applied Lever config to AP Node " << apNode->GetId()
                    << ": channel=" << (int)apConfig.channel
                    << ", txPower=" << apConfig.txPower
                    << ", ccaThreshold=" << apConfig.ccaThreshold
                    << ", rxSensitivity=" << apConfig.rxSensitivity);
    }

    // ========================================================================
    // Setup Waypoint-Based Mobility for STAs
    // ========================================================================

    WaypointMobilityHelper mobilityHelper;
    mobilityHelper.SetWaypointGrid(waypointGrid);
    mobilityHelper.Install(staNodes, config.stas);

    NS_LOG_INFO("Installed waypoint-based mobility on " << staNodes.GetN() << " STAs");

    // ========================================================================
    // Internet Stack
    // ========================================================================

    InternetStackHelper internet;
    internet.Install(apNodes);
    internet.Install(staNodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer apInterfaces = address.Assign(apDevices);
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);

    NS_LOG_INFO("Assigned IP addresses");

    // ========================================================================
    // Applications (Simple UDP Echo)
    // ========================================================================

    // Echo server on first AP
    UdpEchoServerHelper echoServer(9);
    ApplicationContainer serverApps = echoServer.Install(apNodes.Get(0));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(config.simulationTime));

    // Echo clients on all STAs
    for (size_t i = 0; i < staNodes.GetN(); i++)
    {
        UdpEchoClientHelper echoClient(apInterfaces.GetAddress(0), 9);
        echoClient.SetAttribute("MaxPackets", UintegerValue(1000));
        echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
        echoClient.SetAttribute("PacketSize", UintegerValue(1024));

        ApplicationContainer clientApp = echoClient.Install(staNodes.Get(i));
        clientApp.Start(Seconds(2.0 + i * 0.1));
        clientApp.Stop(Seconds(config.simulationTime));
    }

    NS_LOG_INFO("Installed UDP echo applications");

    // ========================================================================
    // Start Waypoint-Based Mobility
    // ========================================================================

    mobilityHelper.StartMobility();

    NS_LOG_INFO("Started waypoint-based mobility for " << mobilityHelper.GetStaCount() << " STAs");

    // ========================================================================
    // Run Simulation
    // ========================================================================

    NS_LOG_INFO("Starting waypoint-based simulation for " << config.simulationTime << " seconds...");
    NS_LOG_INFO("STAs will move randomly between " << waypointGrid->GetWaypointCount()
                                                    << " waypoints");

    Simulator::Stop(Seconds(config.simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    NS_LOG_INFO("Simulation completed successfully");

    return 0;
}
