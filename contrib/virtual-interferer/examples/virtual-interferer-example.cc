/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Virtual Interferer Example
 *
 * Demonstrates usage of the virtual interferer system with YansWifiPhy
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"

#include "ns3/virtual-interferer.h"
#include "ns3/virtual-interferer-environment.h"
#include "ns3/virtual-interferer-helper.h"
#include "ns3/microwave-interferer.h"
#include "ns3/bluetooth-interferer.h"
#include "ns3/zigbee-interferer.h"
#include "ns3/radar-interferer.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("VirtualInterfererExample");

int
main(int argc, char* argv[])
{
    // Parameters
    double simTime = 10.0;  // seconds
    bool verbose = false;
    std::string scenario = "home";  // home, office, or dfs

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.AddValue("scenario", "Scenario: home, office, or dfs", scenario);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("VirtualInterfererExample", LOG_LEVEL_INFO);
        LogComponentEnable("VirtualInterferer", LOG_LEVEL_DEBUG);
        LogComponentEnable("VirtualInterfererEnvironment", LOG_LEVEL_DEBUG);
        LogComponentEnable("MicrowaveInterferer", LOG_LEVEL_DEBUG);
        LogComponentEnable("BluetoothInterferer", LOG_LEVEL_DEBUG);
    }

    // Create nodes
    NodeContainer wifiApNode;
    wifiApNode.Create(1);
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create(2);
    NodeContainer allWifiNodes;
    allWifiNodes.Add(wifiApNode);
    allWifiNodes.Add(wifiStaNodes);

    // Set up mobility - all nodes in a 20m x 20m room
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // AP at center
    mobility.Install(wifiApNode);
    wifiApNode.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(10, 10, 2));

    // STAs at corners
    mobility.Install(wifiStaNodes);
    wifiStaNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(5, 5, 1));
    wifiStaNodes.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector(15, 15, 1));

    // Set up WiFi using YansWifiPhy (the target use case)
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);

    YansWifiPhyHelper phy;
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    Ssid ssid = Ssid("virtual-interferer-test");

    // Configure AP
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, wifiApNode);

    // Configure STAs
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, wifiStaNodes);

    NetDeviceContainer allWifiDevices;
    allWifiDevices.Add(apDevice);
    allWifiDevices.Add(staDevices);

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(allWifiNodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(allWifiDevices);

    // ===============================================================
    // VIRTUAL INTERFERER SETUP
    // ===============================================================

    // Configure environment
    VirtualInterfererEnvironmentConfig envConfig;
    envConfig.updateInterval = MilliSeconds(100);
    envConfig.injectionInterval = MilliSeconds(10);
    envConfig.ccaSensitivityDbm = -82.0;  // Standard 802.11 CCA threshold

    VirtualInterfererHelper interfererHelper;
    interfererHelper.SetEnvironmentConfig(envConfig);

    // Register WiFi devices to receive interference effects
    interfererHelper.RegisterWifiDevices(allWifiDevices);

    // Create interferers based on scenario
    if (scenario == "home")
    {
        NS_LOG_INFO("Creating home scenario...");

        // Microwave in kitchen (near STA 0)
        auto microwave = interfererHelper.CreateMicrowave(
            Vector(3, 3, 1),
            MicrowaveInterferer::MEDIUM
        );

        // Set microwave schedule: on for 30 seconds, off for 30 seconds
        interfererHelper.SetSchedule(microwave, Seconds(2), Seconds(3));

        // Bluetooth devices around the room
        interfererHelper.CreateBluetooth(
            Vector(6, 8, 1),
            BluetoothInterferer::CLASS_2,
            BluetoothInterferer::AUDIO_STREAMING  // Bluetooth headphones
        );

        interfererHelper.CreateBluetooth(
            Vector(14, 12, 1),
            BluetoothInterferer::CLASS_2,
            BluetoothInterferer::HID  // Bluetooth keyboard/mouse
        );

        // ZigBee smart home hub
        interfererHelper.CreateZigbee(
            Vector(10, 10, 1),
            15,  // Channel 15 overlaps with WiFi channel 1-6
            ZigbeeInterferer::CONTROL
        );
    }
    else if (scenario == "office")
    {
        NS_LOG_INFO("Creating office scenario...");

        // Many Bluetooth devices (keyboards, mice, headsets)
        interfererHelper.CreateBluetoothsRandom(
            8,  // 8 Bluetooth devices
            Vector(2, 2, 0.7),
            Vector(18, 18, 1.5)
        );
    }
    else if (scenario == "dfs")
    {
        NS_LOG_INFO("Creating DFS test scenario...");

        // Note: DFS only affects 5 GHz, our example uses 2.4 GHz
        // This is for demonstration - in real use, configure 5 GHz channel

        // Create radar interferer
        auto radar = interfererHelper.CreateRadar(
            Vector(100, 100, 10),  // Radar far away but powerful
            52,  // DFS channel 52
            RadarInterferer::WEATHER
        );

        // Radar is always on in this test
        NS_LOG_INFO("Radar installed on DFS channel 52");
    }

    // Print summary of installed interferers
    auto allInterferers = interfererHelper.GetAllInterferers();
    NS_LOG_INFO("Installed " << allInterferers.size() << " virtual interferers:");
    for (const auto& intf : allInterferers)
    {
        NS_LOG_INFO("  - " << intf->GetInterfererType() << " at " << intf->GetPosition()
                   << " (ID " << intf->GetId() << ")");
    }

    // ===============================================================
    // APPLICATION SETUP (simple UDP echo for traffic)
    // ===============================================================

    uint16_t port = 9;

    // UDP Echo Server on AP
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApp = echoServer.Install(wifiApNode.Get(0));
    serverApp.Start(Seconds(1.0));
    serverApp.Stop(Seconds(simTime));

    // UDP Echo Clients on STAs
    UdpEchoClientHelper echoClient(interfaces.GetAddress(0), port);
    echoClient.SetAttribute("MaxPackets", UintegerValue(1000));
    echoClient.SetAttribute("Interval", TimeValue(MilliSeconds(100)));
    echoClient.SetAttribute("PacketSize", UintegerValue(512));

    ApplicationContainer clientApps = echoClient.Install(wifiStaNodes);
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(simTime));

    // ===============================================================
    // RUN SIMULATION
    // ===============================================================

    NS_LOG_INFO("Starting simulation for " << simTime << " seconds...");

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    NS_LOG_INFO("Simulation complete.");

    // Cleanup
    interfererHelper.UninstallAll();
    VirtualInterfererEnvironment::Destroy();
    Simulator::Destroy();

    return 0;
}
