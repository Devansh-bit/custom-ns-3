/*
 * LeverApi Channel Switch Example
 *
 * This example demonstrates using LeverApi::SwitchChannel() method which
 * delegates to the underlying ApWifiMac/StaWifiMac implementation,
 * avoiding code duplication and leveraging the core WiFi implementation.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/lever-api-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LeverApiChannelSwitchExample");

int
main(int argc, char* argv[])
{
    double simTime = 10.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Total simulation time (s)", simTime);
    cmd.Parse(argc, argv);

    NS_LOG_UNCOND("\n==============================================");
    NS_LOG_UNCOND("  LeverApi Channel Switch Example");
    NS_LOG_UNCOND("==============================================\n");

    // Create AP and 2 STAs
    NodeContainer apNode;
    apNode.Create(1);
    NodeContainer staNodes;
    staNodes.Create(2);

    // Create spectrum channel
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
    Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(lossModel);
    Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);

    // Configure WiFi
    SpectrumWifiPhyHelper phyHelper;
    phyHelper.SetChannel(spectrumChannel);
    phyHelper.Set("ChannelSettings", StringValue("{1, 20, BAND_2_4GHZ, 0}"));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                  "DataMode", StringValue("HeMcs7"),
                                  "ControlMode", StringValue("HeMcs0"));

    WifiMacHelper macHelper;
    Ssid ssid = Ssid("lever-api-demo");

    // Create AP
    macHelper.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phyHelper, macHelper, apNode);

    // Create STAs
    macHelper.SetType("ns3::StaWifiMac",
                      "Ssid", SsidValue(ssid),
                      "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phyHelper, macHelper, staNodes);

    // Setup mobility
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));   // AP
    positionAlloc->Add(Vector(10.0, 0.0, 0.0));  // STA 1
    positionAlloc->Add(Vector(5.0, 8.0, 0.0));   // STA 2
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNode);
    mobility.Install(staNodes);

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(apNode);
    internet.Install(staNodes);

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    address.Assign(apDevice);
    address.Assign(staDevices);

    // Create and configure LeverConfig
    Ptr<LeverConfig> config = CreateObject<LeverConfig>();
    config->SetTxPower(20.0);
    config->SetCcaEdThreshold(-82.0);
    config->SetRxSensitivity(-91.0);

    // Install LeverApi on AP
    LeverApiHelper leverHelper(config);
    ApplicationContainer leverApp = leverHelper.Install(apNode.Get(0));
    leverApp.Start(Seconds(0.0));
    leverApp.Stop(Seconds(simTime));

    // Get the LeverApi application
    Ptr<LeverApi> leverApi = leverApp.Get(0)->GetObject<LeverApi>();

    NS_LOG_UNCOND("Starting simulation on 2.4GHz channel 1...\n");

    // Test 1: Switch to 2.4GHz channel 6 using LeverApi::SwitchChannel()
    Simulator::Schedule(Seconds(2.0), [leverApi]() {
        NS_LOG_UNCOND("\n[2.0s] Switching to 2.4GHz channel 6");
        NS_LOG_UNCOND("        Using: leverApi->SwitchChannel(6)");
        leverApi->SwitchChannel(6);
    });

    // Test 2: Switch to 2.4GHz channel 11
    Simulator::Schedule(Seconds(4.0), [leverApi]() {
        NS_LOG_UNCOND("\n[4.0s] Switching to 2.4GHz channel 11");
        NS_LOG_UNCOND("        Using: leverApi->SwitchChannel(11)");
        leverApi->SwitchChannel(11);
    });

    // Test 3: Switch to 5GHz channel 36, 20MHz
    Simulator::Schedule(Seconds(6.0), [leverApi]() {
        NS_LOG_UNCOND("\n[6.0s] Switching to 5GHz channel 36, 20MHz");
        NS_LOG_UNCOND("        Using: leverApi->SwitchChannel(36)");
        leverApi->SwitchChannel(36);
    });

    // Test 4: Switch to 5GHz channel 42, 80MHz
    Simulator::Schedule(Seconds(8.0), [leverApi]() {
        NS_LOG_UNCOND("\n[8.0s] Switching to 5GHz channel 42, 80MHz");
        NS_LOG_UNCOND("        Using: leverApi->SwitchChannel(42)");
        leverApi->SwitchChannel(42);
    });

    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    NS_LOG_UNCOND("\n==============================================");
    NS_LOG_UNCOND("  Demo Complete!");
    NS_LOG_UNCOND("==============================================");
    NS_LOG_UNCOND("\nKey Features:");
    NS_LOG_UNCOND("1. LeverApi::SwitchChannel() delegates to ApWifiMac::SwitchChannel()");
    NS_LOG_UNCOND("2. No code duplication - uses core WiFi implementation");
    NS_LOG_UNCOND("3. Automatic propagation to connected STAs (via ApWifiMac)");
    NS_LOG_UNCOND("==============================================\n");

    return 0;
}