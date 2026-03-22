/*
 * This example demonstrates multi-AP roaming using the LeverApi module.
 *
 * Network setup:
 * - 5 Access Points on the same SSID and same channel (Channel 42 - 5 GHz, 80 MHz)
 * - 1 mobile Station that moves through the coverage areas
 * - 1 Spectrum Analyzer monitoring the 5 GHz band
 * - UDP traffic to test connectivity during roaming
 *
 * The mobile STA will naturally roam between APs based on beacon RSSI as it moves.
 * Each AP is configured via the Lever API with separate LeverConfig objects.
 *
 * NOTE: All APs are on the same channel (42 = 36+40+44+48 bonded, 80 MHz) to enable seamless roaming.
 * This is common in enterprise WiFi where same SSID + same channel allows clients to roam
 * based on signal strength. The Lever API can be used to dynamically adjust AP
 * power or channel to influence roaming behavior.
 *
 * Expected behavior:
 * - STA starts near AP0 and associates with it
 * - As STA moves, it will roam to AP1, AP2, AP3, AP4 based on which has strongest signal
 * - Spectrum analyzer shows all 5 AP beacons on channel 42 (5 GHz, 80 MHz)
 * - Association/disassociation events are logged with timestamps
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/spectrum-analyzer-helper.h"
#include "ns3/lever-api-helper.h"
#include "ns3/propagation-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MultiApRoamingExample5GHz");

// Global variables to track roaming
uint32_t g_associationCount = 0;
uint32_t g_disassociationCount = 0;
std::string g_currentBssid = "none";
Ptr<WifiNetDevice> g_staDevice; // Global reference to STA device for channel updates
NetDeviceContainer g_apDevices; // Global reference to AP devices for channel updates

// Throughput tracking variables
uint64_t g_rxBytes = 0;
uint64_t g_lastRxBytes = 0;
double g_lastThroughputTime = 0.0;

// Callback for association events
void
OnAssociation(std::string context, Mac48Address bssid)
{
    g_associationCount++;
    std::ostringstream oss;
    oss << bssid;
    g_currentBssid = oss.str();

    NS_LOG_UNCOND("Time " << Simulator::Now().GetSeconds() << "s: "
                  << "STA ASSOCIATED with AP " << bssid
                  << " (Roaming event #" << g_associationCount << ")");
}

// Callback for disassociation events
void
OnDisassociation(std::string context, Mac48Address bssid)
{
    g_disassociationCount++;

    NS_LOG_UNCOND("Time " << Simulator::Now().GetSeconds() << "s: "
                  << "STA DISASSOCIATED from AP " << bssid);
}

// Callback to log STA position
void
OnPositionChange(Ptr<const MobilityModel> mobility)
{
    Vector pos = mobility->GetPosition();
    NS_LOG_INFO("Time " << Simulator::Now().GetSeconds() << "s: "
                << "STA position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
}

// Callback for packet reception to track throughput
void
OnPacketReceived(Ptr<const Packet> packet)
{
    g_rxBytes += packet->GetSize();
}

// Calculate and log throughput periodically
void
CalculateThroughput()
{
    double currentTime = Simulator::Now().GetSeconds();
    double timeDiff = currentTime - g_lastThroughputTime;
    
    if (timeDiff > 0)
    {
        uint64_t bytesDiff = g_rxBytes - g_lastRxBytes;
        double throughput = (bytesDiff * 8.0) / (timeDiff * 1e6); // Mbps
        
        NS_LOG_UNCOND("Time " << currentTime << "s: "
                      << "Throughput = " << throughput << " Mbps "
                      << "(Associated with: " << g_currentBssid << ")");
        
        g_lastRxBytes = g_rxBytes;
        g_lastThroughputTime = currentTime;
    }
    
    // Schedule next throughput calculation
    Simulator::Schedule(Seconds(1.0), &CalculateThroughput);
}

// Callback for TxPower changes via Lever API
void
OnTxPowerChanged(std::string apName, double oldValue, double newValue)
{
    NS_LOG_UNCOND("Time " << Simulator::Now().GetSeconds() << "s: "
                  << apName << " TxPower changed from " << oldValue
                  << " to " << newValue << " dBm");
}

// Callback for Channel changes via Lever API - FIXED signature
void
OnChannelChanged(std::string apName, uint8_t channelNumber, uint16_t widthMhz,
                 WifiPhyBandType band, uint8_t primary20Index)
{
    NS_LOG_UNCOND("Time " << Simulator::Now().GetSeconds() << "s: "
                  << apName << " channel changed to " << +channelNumber
                  << ", width: " << widthMhz << " MHz");
}

// Helper function to change channel width for both AP and STA to prevent disassociation
void
ChangeChannelWidth(Ptr<LeverConfig> apConfig, uint8_t channel, uint16_t widthMhz, std::string apName)
{
    NS_LOG_UNCOND("Time " << Simulator::Now().GetSeconds() << "s: "
                  << "Changing " << apName << " and STA to " << widthMhz << " MHz on 5 GHz");

    // Change AP channel width via LeverConfig
    apConfig->SwitchChannel(channel);

    // Also change STA channel width to match
    if (g_staDevice)
    {
        Ptr<WifiPhy> staPhy = g_staDevice->GetPhy();
        if (staPhy)
        {
            std::string channelStr = "{" + std::to_string(channel) + ", " +
                                    std::to_string(widthMhz) + ", BAND_5GHZ, 0}";
            staPhy->SetAttribute("ChannelSettings", StringValue(channelStr));
            NS_LOG_UNCOND("  -> STA also updated to " << widthMhz << " MHz to prevent disassociation");
        }
    }
}

void ChangeTxPower(Ptr<LeverConfig> config, double power, std::string apName)
{
    config->SetTxPower(power);
}

void ChangeClientTxPower(double power)
{
    if (g_staDevice)
    {
        Ptr<WifiPhy> staPhy = g_staDevice->GetPhy();
        if (staPhy)
        {
            std::cout << "Setting STA TxPowerStart to " << power << " dBm" << std::endl;
            std::cout << "Setting STA TxPowerEnd to " << power << " dBm" << std::endl;
            staPhy->SetTxPowerStart(power);
            staPhy->SetTxPowerEnd(power);
        }
    }
}

int
main(int argc, char* argv[])
{
    // Enable logging
    LogComponentEnable("MultiApRoamingExample5GHz", LOG_LEVEL_INFO);

    // Simulation parameters
    uint32_t nAPs = 1;
    double simulationTime = 10.0; // seconds
    double staSpeed = 2.0; // m/s
    double apSpacing = 25.0; // meters between APs
    uint16_t channelWidth = 40; // Channel width in MHz (20, 40, 80, or 160)

    // Parse command line
    CommandLine cmd(__FILE__);
    cmd.AddValue("nAPs", "Number of Access Points", nAPs);
    cmd.AddValue("simulationTime", "Total simulation time (s)", simulationTime);
    cmd.AddValue("staSpeed", "STA movement speed (m/s)", staSpeed);
    cmd.AddValue("apSpacing", "Distance between APs (m)", apSpacing);
    cmd.AddValue("channelWidth", "Channel width in MHz (20, 40, 80, 160)", channelWidth);
    cmd.Parse(argc, argv);

    NS_LOG_INFO("Creating multi-AP roaming scenario with " << nAPs << " APs on 5 GHz");
    NS_LOG_UNCOND("=== Multi-AP Roaming Example (5 GHz Band) ===");
    NS_LOG_UNCOND("Network: " << nAPs << " APs on same SSID, same channel (Ch42 - 5 GHz, 80 MHz)");
    NS_LOG_UNCOND("Channel Width: " << channelWidth << " MHz");
    NS_LOG_UNCOND("Configuration: All APs on Channel 42 (5210 MHz center, 36+40+44+48 bonded) for seamless roaming");
    NS_LOG_UNCOND("STA speed: " << staSpeed << " m/s over " << simulationTime << "s");

    // Create nodes
    NodeContainer apNodes;
    apNodes.Create(nAPs);

    NodeContainer staNode;
    staNode.Create(1);

    NodeContainer spectrumAnalyzerNode;
    spectrumAnalyzerNode.Create(1);

    // Create spectrum channel for WiFi
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
    
    // Add propagation loss model
    Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(lossModel);
    
    // Add propagation delay model
    Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);

    // Configure WiFi PHY using Spectrum
    SpectrumWifiPhyHelper phy;
    phy.SetChannel(spectrumChannel);
    phy.Set("Antennas", UintegerValue(1));
    phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(1));
    phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(1));

    // Configure WiFi standard and rate manager
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n); // Use 802.11ac for 5 GHz
    wifi.SetRemoteStationManager("ns3::IdealWifiManager");

    // Configure MAC
    WifiMacHelper mac;
    Ssid ssid = Ssid("roaming-test-network-5ghz");

    // Channel assignments for APs
    // NOTE: For roaming to work, all APs must be on the same channel so STA can hear them all
    // Select appropriate channel number based on requested width
    uint8_t baseChannel;
    if (channelWidth == 20) {
        baseChannel = 36;  // 20 MHz channel
    } else if (channelWidth == 40) {
        baseChannel = 38;  // 40 MHz channel (bonds 36+40)
    } else if (channelWidth == 80) {
        baseChannel = 42;  // 80 MHz channel (bonds 36+40+44+48)
    } else if (channelWidth == 160) {
        baseChannel = 50;  // 160 MHz channel
    } else {
        NS_LOG_UNCOND("Invalid channel width " << channelWidth << ", defaulting to 20 MHz on channel 36");
        baseChannel = 36;
    }
    std::vector<uint8_t> apChannels = {baseChannel, baseChannel, baseChannel, baseChannel, baseChannel};
    std::vector<double> apTxPowers = {20.0, 20.0, 20.0, 20.0, 20.0}; // Typical 5 GHz power

    // Container for all AP devices and LeverConfigs
    NetDeviceContainer apDevices;
    std::vector<Ptr<LeverConfig>> apConfigs;
    std::vector<ApplicationContainer> leverApps;

    // Install APs
    for (uint32_t i = 0; i < nAPs; i++)
    {
        // Configure this AP's channel before installation
        uint8_t channel = apChannels[i % apChannels.size()];
        double txPower = apTxPowers[i % apTxPowers.size()];

        phy.Set("TxPowerStart", DoubleValue(txPower));
        phy.Set("TxPowerEnd", DoubleValue(txPower));
        // Use width=0 for auto-detection based on channel number
        std::string channelStr = "{" + std::to_string(channel) + ", 0, BAND_5GHZ, 0}";
        phy.Set("ChannelSettings", StringValue(channelStr));

        // Create AP MAC
        mac.SetType("ns3::ApWifiMac",
                    "Ssid", SsidValue(ssid),
                    "EnableBeaconJitter", BooleanValue(false),
                    "BeaconInterval", TimeValue(MicroSeconds(102400))); // 100 * 1024us

        NetDeviceContainer apDevice = wifi.Install(phy, mac, apNodes.Get(i));
        apDevices.Add(apDevice);

        // Create LeverConfig for this AP
        Ptr<LeverConfig> config = CreateObject<LeverConfig>();
        config->SetTxPower(txPower);
        config->SetCcaEdThreshold(-82.0);
        config->SetRxSensitivity(-93.0);
        config->SwitchChannel(channel);
        apConfigs.push_back(config);

        // Connect traces with AP name
        std::string apName = "AP" + std::to_string(i);
        config->TraceConnectWithoutContext("TxPowerStart",
            MakeBoundCallback(&OnTxPowerChanged, apName));
        config->TraceConnectWithoutContext("ChannelSettings",
            MakeBoundCallback(&OnChannelChanged, apName));

        // Install LeverApi on this AP
        LeverApiHelper leverHelper(config);
        ApplicationContainer app = leverHelper.Install(apNodes.Get(i));
        app.Start(Seconds(0.0));
        app.Stop(Seconds(simulationTime));
        leverApps.push_back(app);

        // CRITICAL: Force LeverConfig to apply settings after LeverApi starts
        // Schedule this slightly after app start to ensure LeverApi is ready
        Simulator::Schedule(Seconds(0.001), [config, channel, txPower]() {
            // Re-trigger the trace by setting the same values
            config->SwitchChannel(channel);
            config->SetTxPower(txPower);
        });

        NS_LOG_INFO("AP" << i << " configured: Channel " << +channel
                    << " (5 GHz), TxPower " << txPower << " dBm");
    }

    // Install STA with active probing enabled for roaming
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(true),
                "ProbeRequestTimeout", TimeValue(MilliSeconds(50)),
                "MaxMissedBeacons", UintegerValue(10));

    // STA starts on same channel as AP0 for initial association
    phy.Set("TxPowerStart", DoubleValue(20.0));
    phy.Set("TxPowerEnd", DoubleValue(20.0));
    // Use width=0 for auto-detection based on channel number
    std::string staChannelStr = "{" + std::to_string(apChannels[0]) + ", 0, BAND_5GHZ, 0}";
    phy.Set("ChannelSettings", StringValue(staChannelStr));

    NetDeviceContainer staDevice = wifi.Install(phy, mac, staNode);

    // Store STA device globally for channel width updates
    g_staDevice = DynamicCast<WifiNetDevice>(staDevice.Get(0));
    g_apDevices = apDevices;

    // Configure AP positions (linear layout)
    MobilityHelper apMobility;
    Ptr<ListPositionAllocator> apPositionAlloc = CreateObject<ListPositionAllocator>();

    for (uint32_t i = 0; i < nAPs; i++)
    {
        double x = i * apSpacing; // APs at 0, 25, 50, 75, 100m
        apPositionAlloc->Add(Vector(x, 0.0, 1.5)); // 1.5m height for APs
        NS_LOG_INFO("AP" << i << " position: (" << x << ", 0, 1.5)");
    }

    apMobility.SetPositionAllocator(apPositionAlloc);
    apMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    apMobility.Install(apNodes);

    // Configure STA mobility (moves through all AP coverage areas)
    MobilityHelper staMobility;
    Ptr<ListPositionAllocator> staPositionAlloc = CreateObject<ListPositionAllocator>();
    staPositionAlloc->Add(Vector(0.0, 0.0, 1.0));
    staMobility.SetPositionAllocator(staPositionAlloc);
    staMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    staMobility.Install(staNode);

    // Position spectrum analyzer at center
    MobilityHelper spectrumMobility;
    Ptr<ListPositionAllocator> spectrumPositionAlloc = CreateObject<ListPositionAllocator>();
    double centerX = ((nAPs - 1) * apSpacing) / 2.0;
    spectrumPositionAlloc->Add(Vector(centerX, 0.0, 1.0));
    spectrumMobility.SetPositionAllocator(spectrumPositionAlloc);
    spectrumMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    spectrumMobility.Install(spectrumAnalyzerNode);

    NS_LOG_INFO("Spectrum analyzer at center position: (" << centerX << ", 0, 1)");

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(apNodes);
    internet.Install(staNode);

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apInterfaces = address.Assign(apDevices);
    Ipv4InterfaceContainer staInterface = address.Assign(staDevice);

    // ========================================================================
    // Setup Spectrum Analyzer (5 GHz band)
    // ========================================================================

    double startFreqGHz = 5.15;  // Start of UNII-1 band
    double endFreqGHz = 5.35;    // Cover channels 36-64
    double freqResolutionMHz = 0.1; // 100 kHz

    std::vector<double> freqs;
    double currentFreqGHz = startFreqGHz;
    while (currentFreqGHz <= endFreqGHz)
    {
        freqs.push_back(currentFreqGHz * 1e9);
        currentFreqGHz += freqResolutionMHz / 1000.0;
    }
    Ptr<SpectrumModel> spectrumModel = Create<SpectrumModel>(freqs);

    SpectrumAnalyzerHelper spectrumAnalyzerHelper;
    spectrumAnalyzerHelper.SetChannel(spectrumChannel);
    spectrumAnalyzerHelper.SetRxSpectrumModel(spectrumModel);
    spectrumAnalyzerHelper.SetPhyAttribute("Resolution", TimeValue(MilliSeconds(10)));
    spectrumAnalyzerHelper.SetPhyAttribute("NoisePowerSpectralDensity", DoubleValue(1e-15));
    spectrumAnalyzerHelper.EnableAsciiAll("multi-ap-roaming-spectrum-5ghz");

    NetDeviceContainer spectrumAnalyzerDevices =
        spectrumAnalyzerHelper.Install(spectrumAnalyzerNode);

    NS_LOG_INFO("Spectrum analyzer configured: " << freqs.size() << " bins from "
                << startFreqGHz << " to " << endFreqGHz << " GHz (5 GHz band)");

    // ========================================================================
    // Setup UDP traffic - BIDIRECTIONAL to measure AP transmit power
    // ========================================================================

    uint16_t port = 9;

    // DOWNLINK: Install UDP server on STA, client on AP
    // This creates traffic FROM AP TO STA (measuring AP's TX power!)
    UdpServerHelper serverHelper(port);
    ApplicationContainer serverApps = serverHelper.Install(staNode.Get(0));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(simulationTime));

    UdpClientHelper clientHelper(staInterface.GetAddress(0), port);
    clientHelper.SetAttribute("MaxPackets", UintegerValue(1000000));
    clientHelper.SetAttribute("Interval", TimeValue(Time("0.001s"))); // 1ms = 1000 pps
    clientHelper.SetAttribute("PacketSize", UintegerValue(1472));

    ApplicationContainer clientApps = clientHelper.Install(apNodes.Get(0));
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(simulationTime));

    // ========================================================================
    // Connect to association/disassociation traces
    // ========================================================================

    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/Assoc",
                    MakeCallback(&OnAssociation));
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/DeAssoc",
                    MakeCallback(&OnDisassociation));

    // Connect to packet reception trace for throughput measurement
    // Config::ConnectWithoutContext("/NodeList/" + std::to_string(staNode.Get(0)->GetId()) +
    //                               "/ApplicationList/*/$ns3::UdpClient/Rx",
    //                               MakeCallback(&OnPacketReceived));


    // Periodic position logging for STA
    for (double t = 0.0; t < simulationTime; t += 2.0)
    {
        Simulator::Schedule(Seconds(t), &OnPositionChange,
                           staNode.Get(0)->GetObject<MobilityModel>());
    }

    // Start throughput calculation
    g_lastThroughputTime = 2.0; // Start at 2 seconds (when traffic begins)
    Simulator::Schedule(Seconds(3.0), &CalculateThroughput);

    if (nAPs > 0)
    {
        // Simulator::Schedule(Seconds(3.0), &ChangeChannelWidth, apConfigs[0], 42, 80, "AP0");
        Simulator::Schedule(Seconds(4.0), &ChangeTxPower, apConfigs[0], 10.0, "AP0");  // Change AP power to 10 dBm
        // Simulator::Schedule(Seconds(5.0), &ChangeClientTxPower, 10.0);
        // Simulator::Schedule(Seconds(6.0), &ChangeChannelWidth, apConfigs[0], 40, 20, "AP0");
        Simulator::Schedule(Seconds(7.0), &ChangeTxPower, apConfigs[0], 30.0, "AP0");  // Change AP power to 30 dBm
    }

    // ========================================================================
    // Run simulation
    // ========================================================================

    NS_LOG_INFO("Starting simulation for " << simulationTime << " seconds");
    NS_LOG_UNCOND("=== Simulation started ===");
    NS_LOG_UNCOND("Watch for association events as STA roams between APs on 5 GHz...");

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // Get final statistics
    Ptr<UdpServer> udpServer = DynamicCast<UdpServer>(serverApps.Get(0));
    if (udpServer)
    {
        g_rxBytes = udpServer->GetReceived() * 1024; // packets * packet size
    }

    NS_LOG_UNCOND("=== Simulation complete ===");
    NS_LOG_UNCOND("Total associations: " << g_associationCount);
    NS_LOG_UNCOND("Total disassociations: " << g_disassociationCount);
    NS_LOG_UNCOND("Total packets received: " << udpServer->GetReceived());
    NS_LOG_UNCOND("Average throughput: " 
                  << (udpServer->GetReceived() * 1024 * 8.0) / ((simulationTime - 2.0) * 1e6) << " Mbps");
    NS_LOG_UNCOND("Spectrum trace saved to: multi-ap-roaming-spectrum-5ghz-*");

    Simulator::Destroy();

    return 0;
}