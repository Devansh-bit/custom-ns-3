#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/log.h"
#include <iomanip>

#include "ns3/link-measurement-protocol.h"
#include "ns3/link-measurement-report.h"

/**
 * @file
 *
 * Explain here what the example does.
 */

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LinkMeasurementTest");

struct MeasurementStats
{
    uint32_t requestsSent = 0;
    uint32_t reportsReceived = 0;
    std::vector<double> rcpiValues;
    std::vector<double> rsniValues;
    std::vector<double> linkMarginValues;
    std::vector<double> txPowerValues;
};

MeasurementStats g_stats;

/**
 * \brief Callback when Link Measurement Request is received
 * \param from source MAC address
 * \param request the request object
 */
void OnLinkMeasurementRequestReceived(Mac48Address from,
                                       LinkMeasurementRequest request)
{
    int8_t txPowerDbm = request.GetTransmitPowerUsedDbm();
    int8_t maxTxPowerDbm = request.GetMaxTransmitPowerDbm();

    NS_LOG_UNCOND("["
                  << Simulator::Now().GetSeconds() << "s] Request RX from "
                  << from << " | Dialog Token: " << (uint32_t)request.GetDialogToken()
                  << " | TX Power: " << (int32_t)txPowerDbm << " dBm"
                  << " | Max TX Power: " << (int32_t)maxTxPowerDbm << " dBm");
}

// Global node pointers for distance calculation
Ptr<Node> g_apNode;
Ptr<Node> g_staNode;

/**
 * \brief Calculate distance between AP and STA
 * \return distance in meters
 */
double GetDistance()
{
    Ptr<MobilityModel> apMob = g_apNode->GetObject<MobilityModel>();
    Ptr<MobilityModel> staMob = g_staNode->GetObject<MobilityModel>();
    return apMob->GetDistanceFrom(staMob);
}

/**
 * \brief Callback when Link Measurement Report is received
 * \param from source MAC address
 * \param report the measurement report
 */
void OnLinkMeasurementReportReceived(Mac48Address from,
                                      LinkMeasurementReport report)
{
    // Get values in proper units using the conversion methods
    double rcpiDbm = report.GetRcpiDbm();
    double rsniDb = report.GetRsniDb();
    int8_t txPowerDbm = report.GetTransmitPowerDbm();
    uint8_t linkMarginDb = report.GetLinkMarginDb();
    double distance = GetDistance();

    NS_LOG_UNCOND("["
                  << Simulator::Now().GetSeconds() << "s] Report RX from " << from
                  << " | Distance: " << std::fixed << std::setprecision(1) << distance << " m"
                  << " | RCPI: " << rcpiDbm << " dBm"
                  << " | RSNI: " << rsniDb << " dB"
                  << " | TX Power: " << (int32_t)txPowerDbm << " dBm"
                  << " | Link Margin: " << (uint32_t)linkMarginDb << " dB");

    // Collect statistics in proper units
    g_stats.reportsReceived++;
    g_stats.rcpiValues.push_back(rcpiDbm);
    g_stats.rsniValues.push_back(rsniDb);
    g_stats.linkMarginValues.push_back(linkMarginDb);
    g_stats.txPowerValues.push_back(txPowerDbm);
}

/**
 * \brief Initiate Link Measurement Request from STA to AP
 * \param staProtocol STA's protocol instance
 * \param apMac AP's MAC address
 * \param txPower transmit power for measurement
 */
void SendMeasurementRequest(Ptr<LinkMeasurementProtocol> staProtocol,
                            Mac48Address apMac,
                            int8_t txPower = 20)
{
    NS_LOG_UNCOND("[" << Simulator::Now().GetSeconds() << "s] STA sending "
                       "Link Measurement Request to AP");
    staProtocol->SendLinkMeasurementRequest(apMac, txPower, 30);
    g_stats.requestsSent++;
}

/**
 * \brief Print statistics summary
 */
void PrintStatistics()
{
    NS_LOG_UNCOND("\n" << std::string(70, '='));
    NS_LOG_UNCOND("LINK MEASUREMENT PROTOCOL TEST RESULTS");
    NS_LOG_UNCOND(std::string(70, '='));
    NS_LOG_UNCOND("Requests Sent: " << g_stats.requestsSent);
    NS_LOG_UNCOND("Reports Received: " << g_stats.reportsReceived);

    if (g_stats.rcpiValues.size() > 0)
    {
        double avgRcpi = 0, avgRsni = 0, avgMargin = 0, avgTxPower = 0;

        for (size_t i = 0; i < g_stats.rcpiValues.size(); i++)
        {
            avgRcpi += g_stats.rcpiValues[i];
            avgRsni += g_stats.rsniValues[i];
            avgMargin += g_stats.linkMarginValues[i];
            avgTxPower += g_stats.txPowerValues[i];
        }

        avgRcpi /= g_stats.rcpiValues.size();
        avgRsni /= g_stats.rsniValues.size();
        avgMargin /= g_stats.linkMarginValues.size();
        avgTxPower /= g_stats.txPowerValues.size();

        NS_LOG_UNCOND("\nAverage Measurements:");
        NS_LOG_UNCOND("  RCPI: " << std::fixed << std::setprecision(2) << avgRcpi);
        NS_LOG_UNCOND("  RSNI: " << std::fixed << std::setprecision(2) << avgRsni);
        NS_LOG_UNCOND("  Link Margin: " << std::fixed << std::setprecision(2) << avgMargin
                                        << " dB");
        NS_LOG_UNCOND("  TX Power: " << std::fixed << std::setprecision(2) << avgTxPower
                                     << " dBm");
    }

    NS_LOG_UNCOND(std::string(70, '=') << "\n");
}


int
main(int argc, char* argv[])
{
    bool verbose = false;
    uint32_t numMeasurements = 10;
    double measurementInterval = 1.5; // seconds
    double simTime = 20.0;            // seconds
    double distance = 10.0;           // meters initial distance between AP and STA

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "Tell application to log if true", verbose);
    cmd.AddValue("numMeasurements", "Number of measurements to perform",
                 numMeasurements);
    cmd.AddValue("measurementInterval", "Interval between measurements (seconds)",
                 measurementInterval);
    cmd.AddValue("simTime", "Total simulation time (seconds)", simTime);
    cmd.AddValue("distance", "Distance between AP and STA (meters)", distance);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("LinkMeasurementProtocol", LOG_ALL);
        LogComponentEnable("LinkMeasurementReport", LOG_ALL);
        LogComponentEnable("LinkMeasurementRequest", LOG_ALL);
        LogComponentEnable("LinkMeasurementTest", LOG_ALL);
        // LogComponentEnable("WifiMac", LOG_ALL);
        // LogComponentEnable("WifiPhy", LOG_ALL);

    }


    NS_LOG_UNCOND("\n=== Link Measurement Protocol Simulation ===");
    NS_LOG_UNCOND("Configuration:");
    NS_LOG_UNCOND("  Measurements: " << numMeasurements);
    NS_LOG_UNCOND("  Interval: " << measurementInterval << " s");
    NS_LOG_UNCOND("  Distance: " << distance << " m");
    NS_LOG_UNCOND("  Simulation Time: " << simTime << " s\n");

    // Create WiFi nodes
    NodeContainer apNodes;
    NodeContainer staNodes;
    apNodes.Create(1);
    staNodes.Create(1);

    // Set global node pointers for distance calculation
    g_apNode = apNodes.Get(0);
    g_staNode = staNodes.Get(0);

    NodeContainer allNodes;
    allNodes.Add(apNodes); // apNode = 0
    allNodes.Add(staNodes); // staNode = 1

    // Setup mobility - AP is stationary, STA moves around
    MobilityHelper mobilityAp;
    mobilityAp.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityAp.Install(apNodes);
    Ptr<MobilityModel> apMobility = apNodes.Get(0)->GetObject<MobilityModel>();
    apMobility->SetPosition(Vector(0.0, 0.0, 0.0));

    // STA uses RandomWalk2d to move around
    MobilityHelper mobilitySta;
    mobilitySta.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                                 "Bounds", RectangleValue(Rectangle(-50, 50, -50, 50)),
                                 "Speed", StringValue("ns3::ConstantRandomVariable[Constant=2.0]"),
                                 "Distance", DoubleValue(10.0));
    mobilitySta.Install(staNodes);
    Ptr<MobilityModel> staMobility = staNodes.Get(0)->GetObject<MobilityModel>();
    staMobility->SetPosition(Vector(distance, 0.0, 0.0));

    // Setup SpectrumChannel with propagation models
    SpectrumWifiPhyHelper spectrumPhyHelper;
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();

    Ptr<LogDistancePropagationLossModel> lossModel =
        CreateObject<LogDistancePropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(lossModel);

    Ptr<ConstantSpeedPropagationDelayModel> delayModel =
        CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);

    // Configure PHY with modern API
    spectrumPhyHelper.SetChannel(spectrumChannel);
    spectrumPhyHelper.Set("ChannelSettings", StringValue("{1, 20, BAND_2_4GHZ, 0}"));
    spectrumPhyHelper.Set("TxPowerStart", DoubleValue(20.0));
    spectrumPhyHelper.Set("TxPowerEnd", DoubleValue(20.0));

    WifiMacHelper wifiMacHelper;
    Ssid ssid = Ssid("rrm-test-network");

    WifiHelper wifiHelper;
    wifiHelper.SetStandard(WIFI_STANDARD_80211n);
    wifiHelper.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                    "DataMode", StringValue("HtMcs7"),
                                    "ControlMode", StringValue("HtMcs0"));

    wifiMacHelper.SetType("ns3::ApWifiMac",
                    "Ssid", SsidValue(ssid),
                    "BeaconInterval", TimeValue(MicroSeconds(102400)));
    auto apDevices = wifiHelper.Install(spectrumPhyHelper, wifiMacHelper, apNodes);

    wifiMacHelper.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(false));
    auto staDevices = wifiHelper.Install(spectrumPhyHelper, wifiMacHelper, staNodes);

    InternetStackHelper internet;
    internet.Install(staNodes);
    internet.Install(apNodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    auto apInterface = ipv4.Assign(apDevices);
    auto staInterface = ipv4.Assign(staDevices);

    spectrumPhyHelper.EnablePcap("rrm-ap", apDevices.Get(0));
    spectrumPhyHelper.EnablePcap("rrm-sta", staDevices.Get(0));

    // Install Link Measurement Protocol
    Ptr<LinkMeasurementProtocol> staProtocol =
        CreateObject<LinkMeasurementProtocol>();
    Ptr<LinkMeasurementProtocol> apProtocol =
        CreateObject<LinkMeasurementProtocol>();

    staProtocol->Install(
        DynamicCast<WifiNetDevice>(staDevices.Get(0)));
    apProtocol->Install(
        DynamicCast<WifiNetDevice>(apDevices.Get(0)));

    // Connect trace sources
    staProtocol
        ->TraceConnectWithoutContext(
            "LinkMeasurementReportReceived",
            MakeCallback(&OnLinkMeasurementReportReceived));

    apProtocol
        ->TraceConnectWithoutContext(
            "LinkMeasurementRequestReceived",
            MakeCallback(&OnLinkMeasurementRequestReceived));

    // Get MAC addresses
    Ptr<WifiNetDevice> staNetDevice = DynamicCast<WifiNetDevice>(staDevices.Get(0));
    Ptr<WifiNetDevice> apNetDevice = DynamicCast<WifiNetDevice>(apDevices.Get(0));

    Mac48Address staMac = staNetDevice->GetMac()->GetAddress();
    Mac48Address apMac = apNetDevice->GetMac()->GetAddress();

    NS_LOG_UNCOND("STA MAC: " << staMac);
    NS_LOG_UNCOND("AP MAC: " << apMac << "\n");

    // Schedule measurement requests
    for (uint32_t i = 0; i < numMeasurements; i++)
    {
        Time requestTime = Seconds(1.0 + i * measurementInterval);
        Simulator::Schedule(requestTime, &SendMeasurementRequest,
                           staProtocol, apMac, 20);
    }

    // Print statistics at end
    Simulator::Schedule(Seconds(simTime - 0.1), &PrintStatistics);

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
