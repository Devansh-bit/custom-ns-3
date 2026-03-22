/*
 * Smart Channel Switching Demo - LeverApi
 *
 * This example demonstrates the new SwitchChannel() method that:
 * 1. Automatically determines band and width from channel number (IEEE 802.11 standard)
 * 2. Propagates channel changes from AP to all connected STAs
 *
 * IEEE 802.11 Channel numbering:
 * - 2.4GHz: channels 1-14 (always 20MHz)
 * - 5GHz 20MHz: 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, etc.
 * - 5GHz 40MHz: 38, 46, 54, 62, 102, 110, 118, 126, 134, 142, 151, 159
 * - 5GHz 80MHz: 42, 58, 106, 122, 138, 155
 * - 5GHz 160MHz: 50, 114
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/lever-api-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ChannelSwitchDemo");

int
main(int argc, char* argv[])
{
    double simTime = 12.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Total simulation time (s)", simTime);
    cmd.Parse(argc, argv);

    NS_LOG_UNCOND("\n==============================================");
    NS_LOG_UNCOND("  Smart Channel Switching Demo");
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
    Ssid ssid = Ssid("channel-switch-demo");

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
    config->SwitchChannel(1);

    // Install LeverApi on AP
    LeverApiHelper leverHelper(config);
    ApplicationContainer leverApp = leverHelper.Install(apNode.Get(0));
    leverApp.Start(Seconds(0.0));
    leverApp.Stop(Seconds(simTime + 2));

    // Get the LeverApi application for channel switching
    Ptr<LeverApi> leverApi = leverApp.Get(0)->GetObject<LeverApi>();

    NS_LOG_UNCOND("Starting simulation with AP and 2 STAs on 2.4GHz channel 1...\n");

    // Test 1: Switch to 2.4GHz channel 6
    Simulator::Schedule(Seconds(2.0), [leverApi]() {
        NS_LOG_UNCOND("\n[2.0s] TEST 1: Switch to 2.4GHz channel 6");
        NS_LOG_UNCOND("        Using: leverApi->SwitchChannel(6)");
        leverApi->SwitchChannel(6);
    });

    // Test 2: Switch to 2.4GHz channel 11
    Simulator::Schedule(Seconds(4.0), [leverApi]() {
        NS_LOG_UNCOND("\n[4.0s] TEST 2: Switch to 2.4GHz channel 11");
        NS_LOG_UNCOND("        Using: leverApi->SwitchChannel(11)");
        leverApi->SwitchChannel(11);
    });

    // Test 3: Switch to 5GHz channel 36, 20MHz
    Simulator::Schedule(Seconds(6.0), [leverApi]() {
        NS_LOG_UNCOND("\n[6.0s] TEST 3: Switch to 5GHz channel 36, 20MHz");
        NS_LOG_UNCOND("        Using: leverApi->SwitchChannel(36)");
        leverApi->SwitchChannel(36);
    });

    // Test 4: Switch to 5GHz channel 38, 40MHz (bonds 36+40)
    Simulator::Schedule(Seconds(8.0), [leverApi]() {
        NS_LOG_UNCOND("\n[8.0s] TEST 4: Switch to 5GHz channel 38, 40MHz");
        NS_LOG_UNCOND("        Using: leverApi->SwitchChannel(38)  [40MHz channel, bonds 36+40]");
        leverApi->SwitchChannel(38);
    });

    // Test 5: Switch to 5GHz channel 42, 80MHz (bonds 36+40+44+48)
    Simulator::Schedule(Seconds(10.0), [leverApi]() {
        NS_LOG_UNCOND("\n[10.0s] TEST 5: Switch to 5GHz channel 42, 80MHz");
        NS_LOG_UNCOND("         Using: leverApi->SwitchChannel(42)  [80MHz channel, bonds 36+40+44+48]");
        leverApi->SwitchChannel(42);
    });

    // Run simulation
    Simulator::Stop(Seconds(simTime + 2));
    Simulator::Run();
    Simulator::Destroy();

    NS_LOG_UNCOND("\n==============================================");
    NS_LOG_UNCOND("  Demo Complete!");
    NS_LOG_UNCOND("==============================================");
    NS_LOG_UNCOND("\nKey Features Demonstrated:");
    NS_LOG_UNCOND("1. Smart channel switching with single method call");
    NS_LOG_UNCOND("2. Automatic band/width detection from channel number");
    NS_LOG_UNCOND("3. Automatic propagation to all connected STAs");
    NS_LOG_UNCOND("==============================================\n");

    return 0;
}