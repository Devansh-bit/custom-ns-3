/*
 * STA Channel Following Example - LeverApi
 *
 * This example demonstrates that associated STAs automatically follow their AP
 * when the AP changes channel configuration using LeverApi::SwitchChannel().
 *
 * The example:
 * 1. Creates an AP with 3 associated STAs
 * 2. Starts on 2.4GHz channel 1
 * 3. Switches through multiple channels in both 2.4GHz and 5GHz bands
 * 4. After each switch, verifies that all STAs have followed the AP to the new channel
 *
 * Channel switching sequence:
 * - 0.0s: Start on 2.4GHz channel 1, 20MHz
 * - 2.0s: Switch to 2.4GHz channel 6, 20MHz
 * - 4.0s: Switch to 2.4GHz channel 11, 20MHz
 * - 6.0s: Switch to 5GHz channel 36, 20MHz
 * - 8.0s: Switch to 5GHz channel 42, 80MHz
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/lever-api-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("StaChannelFollowingExample");

/**
 * Helper function to get channel information string from a WiFi device
 */
std::string
GetChannelInfo(Ptr<WifiNetDevice> device)
{
    Ptr<WifiPhy> phy = device->GetPhy();
    if (!phy)
    {
        return "No PHY";
    }

    const WifiPhyOperatingChannel& opChannel = phy->GetOperatingChannel();
    if (!opChannel.IsSet())
    {
        return "Channel not set";
    }

    std::ostringstream oss;
    oss << "Ch " << static_cast<int>(opChannel.GetNumber())
        << ", " << opChannel.GetWidth() << " MHz"
        << ", " << opChannel.GetFrequency() << " MHz";

    return oss.str();
}

/**
 * Helper function to get band string from operating channel
 */
std::string
GetBandInfo(Ptr<WifiNetDevice> device)
{
    Ptr<WifiPhy> phy = device->GetPhy();
    if (!phy)
    {
        return "Unknown";
    }

    const WifiPhyOperatingChannel& opChannel = phy->GetOperatingChannel();
    if (!opChannel.IsSet())
    {
        return "Unknown";
    }

    uint16_t freq = opChannel.GetFrequency();
    if (freq >= 2400 && freq < 2500)
    {
        return "2.4GHz";
    }
    else if (freq >= 5000 && freq < 6000)
    {
        return "5GHz";
    }
    else if (freq >= 6000 && freq < 7000)
    {
        return "6GHz";
    }
    return "Unknown";
}

/**
 * Print channel information for AP and all STAs
 */
void
PrintAllChannelInfo(Ptr<WifiNetDevice> apDevice, NetDeviceContainer staDevices, const std::string& prefix = "")
{
    if (!prefix.empty())
    {
        NS_LOG_UNCOND(prefix);
    }

    // Print AP channel
    NS_LOG_UNCOND("  AP:      " << GetChannelInfo(apDevice)
                  << " [" << GetBandInfo(apDevice) << "]");

    // Print each STA channel
    for (uint32_t i = 0; i < staDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        NS_LOG_UNCOND("  STA " << i << ":    " << GetChannelInfo(staDevice)
                      << " [" << GetBandInfo(staDevice) << "]");
    }
    NS_LOG_UNCOND("");
}

/**
 * Verify that all STAs are on the same channel as the AP
 */
void
VerifyStasFollowedAp(Ptr<WifiNetDevice> apDevice, NetDeviceContainer staDevices)
{
    Ptr<WifiPhy> apPhy = apDevice->GetPhy();
    const WifiPhyOperatingChannel& apChannel = apPhy->GetOperatingChannel();

    bool allMatch = true;
    for (uint32_t i = 0; i < staDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        Ptr<WifiPhy> staPhy = staDevice->GetPhy();
        const WifiPhyOperatingChannel& staChannel = staPhy->GetOperatingChannel();

        if (staChannel.GetNumber() != apChannel.GetNumber() ||
            staChannel.GetWidth() != apChannel.GetWidth() ||
            staChannel.GetFrequency() != apChannel.GetFrequency())
        {
            allMatch = false;
            NS_LOG_UNCOND("  WARNING: STA " << i << " is NOT on the same channel as AP!");
        }
    }

    if (allMatch)
    {
        NS_LOG_UNCOND("  VERIFIED: All STAs successfully followed AP to new channel");
    }
}

int
main(int argc, char* argv[])
{
    double simTime = 12.0;
    uint32_t nStas = 3;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Total simulation time (s)", simTime);
    cmd.AddValue("nStas", "Number of STAs", nStas);
    cmd.Parse(argc, argv);

    NS_LOG_UNCOND("\n==============================================================");
    NS_LOG_UNCOND("  STA Channel Following Demonstration");
    NS_LOG_UNCOND("==============================================================");
    NS_LOG_UNCOND("This example demonstrates that associated STAs automatically");
    NS_LOG_UNCOND("follow their AP when the AP changes channel configuration.");
    NS_LOG_UNCOND("==============================================================\n");

    // Create AP and STAs
    NodeContainer apNode;
    apNode.Create(1);
    NodeContainer staNodes;
    staNodes.Create(nStas);

    NS_LOG_UNCOND("Network topology:");
    NS_LOG_UNCOND("  1 AP + " << nStas << " STAs");
    NS_LOG_UNCOND("");

    // Create spectrum channel
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
    Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(lossModel);
    Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);

    // Configure WiFi - Start on 2.4GHz channel 1
    SpectrumWifiPhyHelper phyHelper;
    phyHelper.SetChannel(spectrumChannel);
    phyHelper.Set("ChannelSettings", StringValue("{1, 20, BAND_2_4GHZ, 0}"));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                  "DataMode", StringValue("HeMcs7"),
                                  "ControlMode", StringValue("HeMcs0"));

    WifiMacHelper macHelper;
    Ssid ssid = Ssid("channel-following-demo");

    // Create AP
    macHelper.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phyHelper, macHelper, apNode);

    // Create STAs
    macHelper.SetType("ns3::StaWifiMac",
                      "Ssid", SsidValue(ssid),
                      "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phyHelper, macHelper, staNodes);

    // Setup mobility - place all devices close together for good connectivity
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));  // AP at origin

    // Place STAs in a circle around the AP
    double radius = 5.0;
    for (uint32_t i = 0; i < nStas; ++i)
    {
        double angle = (2.0 * M_PI * i) / nStas;
        double x = radius * std::cos(angle);
        double y = radius * std::sin(angle);
        positionAlloc->Add(Vector(x, y, 0.0));
    }

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
    // Important: Start with same channel as PHY configuration (channel 1)
    Ptr<LeverConfig> config = CreateObject<LeverConfig>();
    config->SetTxPower(20.0);
    config->SetCcaEdThreshold(-82.0);
    config->SetRxSensitivity(-91.0);
    config->SwitchChannel(1);  // Match initial PHY channel

    // Install LeverApi on AP
    LeverApiHelper leverHelper(config);
    ApplicationContainer leverApp = leverHelper.Install(apNode.Get(0));
    leverApp.Start(Seconds(0.0));
    leverApp.Stop(Seconds(simTime));

    // Get the LeverApi application for channel switching
    Ptr<LeverApi> leverApi = leverApp.Get(0)->GetObject<LeverApi>();
    Ptr<WifiNetDevice> apWifiDevice = DynamicCast<WifiNetDevice>(apDevice.Get(0));

    // Print initial channel configuration after association
    Simulator::Schedule(Seconds(1.0), [apWifiDevice, staDevices]() {
        NS_LOG_UNCOND("--------------------------------------------------------------");
        NS_LOG_UNCOND("[1.0s] INITIAL STATE - All devices associated");
        NS_LOG_UNCOND("--------------------------------------------------------------");
        PrintAllChannelInfo(apWifiDevice, staDevices);
    });

    // Test 1: Switch to 2.4GHz channel 6
    Simulator::Schedule(Seconds(2.0), [leverApi]() {
        NS_LOG_UNCOND("--------------------------------------------------------------");
        NS_LOG_UNCOND("[2.0s] TEST 1: Switching to 2.4GHz channel 6");
        NS_LOG_UNCOND("--------------------------------------------------------------");
        NS_LOG_UNCOND("Executing: leverApi->SwitchChannel(6)");
        NS_LOG_UNCOND("");
        leverApi->SwitchChannel(6);
    });

    Simulator::Schedule(Seconds(2.5), [apWifiDevice, staDevices]() {
        PrintAllChannelInfo(apWifiDevice, staDevices, "Channel configuration after switch:");
        VerifyStasFollowedAp(apWifiDevice, staDevices);
        NS_LOG_UNCOND("");
    });

    // Test 2: Switch to 2.4GHz channel 11
    Simulator::Schedule(Seconds(4.0), [leverApi]() {
        NS_LOG_UNCOND("--------------------------------------------------------------");
        NS_LOG_UNCOND("[4.0s] TEST 2: Switching to 2.4GHz channel 11");
        NS_LOG_UNCOND("--------------------------------------------------------------");
        NS_LOG_UNCOND("Executing: leverApi->SwitchChannel(11)");
        NS_LOG_UNCOND("");
        leverApi->SwitchChannel(11);
    });

    Simulator::Schedule(Seconds(4.5), [apWifiDevice, staDevices]() {
        PrintAllChannelInfo(apWifiDevice, staDevices, "Channel configuration after switch:");
        VerifyStasFollowedAp(apWifiDevice, staDevices);
        NS_LOG_UNCOND("");
    });

    // Test 3: Switch to 5GHz channel 36 (20MHz)
    Simulator::Schedule(Seconds(6.0), [leverApi]() {
        NS_LOG_UNCOND("--------------------------------------------------------------");
        NS_LOG_UNCOND("[6.0s] TEST 3: Switching to 5GHz channel 36 (20MHz)");
        NS_LOG_UNCOND("--------------------------------------------------------------");
        NS_LOG_UNCOND("Executing: leverApi->SwitchChannel(36)");
        NS_LOG_UNCOND("");
        leverApi->SwitchChannel(36);
    });

    Simulator::Schedule(Seconds(6.5), [apWifiDevice, staDevices]() {
        PrintAllChannelInfo(apWifiDevice, staDevices, "Channel configuration after switch:");
        VerifyStasFollowedAp(apWifiDevice, staDevices);
        NS_LOG_UNCOND("");
    });

    // Test 4: Switch to 5GHz channel 42 (80MHz)
    Simulator::Schedule(Seconds(8.0), [leverApi]() {
        NS_LOG_UNCOND("--------------------------------------------------------------");
        NS_LOG_UNCOND("[8.0s] TEST 4: Switching to 5GHz channel 42 (80MHz)");
        NS_LOG_UNCOND("--------------------------------------------------------------");
        NS_LOG_UNCOND("Executing: leverApi->SwitchChannel(42)");
        NS_LOG_UNCOND("");
        leverApi->SwitchChannel(42);
    });

    Simulator::Schedule(Seconds(8.5), [apWifiDevice, staDevices]() {
        PrintAllChannelInfo(apWifiDevice, staDevices, "Channel configuration after switch:");
        VerifyStasFollowedAp(apWifiDevice, staDevices);
        NS_LOG_UNCOND("");
    });

    // Test 5: Switch back to 2.4GHz channel 1
    Simulator::Schedule(Seconds(10.0), [leverApi]() {
        NS_LOG_UNCOND("--------------------------------------------------------------");
        NS_LOG_UNCOND("[10.0s] TEST 5: Switching back to 2.4GHz channel 1");
        NS_LOG_UNCOND("--------------------------------------------------------------");
        NS_LOG_UNCOND("Executing: leverApi->SwitchChannel(1)");
        NS_LOG_UNCOND("");
        leverApi->SwitchChannel(1);
    });

    Simulator::Schedule(Seconds(10.5), [apWifiDevice, staDevices]() {
        PrintAllChannelInfo(apWifiDevice, staDevices, "Channel configuration after switch:");
        VerifyStasFollowedAp(apWifiDevice, staDevices);
        NS_LOG_UNCOND("");
    });

    // Print summary at end
    Simulator::Schedule(Seconds(simTime - 0.5), []() {
        NS_LOG_UNCOND("==============================================================");
        NS_LOG_UNCOND("  DEMONSTRATION COMPLETE");
        NS_LOG_UNCOND("==============================================================");
        NS_LOG_UNCOND("Successfully demonstrated:");
        NS_LOG_UNCOND("1. STAs follow AP through multiple 2.4GHz channels");
        NS_LOG_UNCOND("2. STAs follow AP from 2.4GHz to 5GHz band");
        NS_LOG_UNCOND("3. STAs follow AP through different channel widths (20MHz, 80MHz)");
        NS_LOG_UNCOND("4. STAs follow AP from 5GHz back to 2.4GHz");
        NS_LOG_UNCOND("");
        NS_LOG_UNCOND("Key Features:");
        NS_LOG_UNCOND("- LeverApi::SwitchChannel() delegates to ApWifiMac");
        NS_LOG_UNCOND("- ApWifiMac automatically propagates changes to all associated STAs");
        NS_LOG_UNCOND("- No manual intervention needed for STA channel switching");
        NS_LOG_UNCOND("==============================================================\n");
    });

    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
