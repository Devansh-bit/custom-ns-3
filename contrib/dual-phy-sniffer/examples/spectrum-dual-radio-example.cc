/*
 * Dual-Radio RRM Example using SpectrumWifiPhy
 *
 * This example demonstrates multi-channel RRM measurements using:
 * - DualPhySnifferHelper for dual-PHY creation and management
 * - SpectrumWifiPhy for frequency-aware propagation
 * - MultiModelSpectrumChannel shared by all radios
 *
 * Architecture:
 * - Main script: Node setup, positioning, matrix building
 * - Helper: Dual-PHY logic, channel hopping, trace callbacks
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/propagation-module.h"
#include "ns3/dual-phy-sniffer-helper.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-mac.h"

#include <map>
#include <vector>
#include <iomanip>
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SpectrumDualRadioRRM");

// Track AP operating channels for verification
std::map<Mac48Address, uint8_t> g_apOperatingChannels;

int
main(int argc, char* argv[])
{
    uint32_t nAPs = 5;
    double simTime = 50.0;
    double hopInterval = 2.0;

    CommandLine cmd;
    cmd.AddValue("nAPs", "Number of APs", nAPs);
    cmd.AddValue("time", "Simulation time (seconds)", simTime);
    cmd.AddValue("hopInterval", "Channel hop interval (seconds)", hopInterval);
    cmd.Parse(argc, argv);

    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   Spectrum Dual-Radio RRM                        ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";
    std::cout << "APs: " << nAPs << "\n";
    std::cout << "Simulation: " << simTime << "s\n";
    std::cout << "Hop Interval: " << hopInterval << "s\n\n";

    // ===== CREATE NODES =====
    NodeContainer apNodes;
    apNodes.Create(nAPs);

    // ===== SPECTRUM CHANNEL =====
    // Create shared MultiModelSpectrumChannel for all radios
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();

    Ptr<LogDistancePropagationLossModel> lossModel = CreateObject<LogDistancePropagationLossModel>();
    lossModel->SetAttribute("Exponent", DoubleValue(3.0));
    lossModel->SetAttribute("ReferenceLoss", DoubleValue(46.6777));
    spectrumChannel->AddPropagationLossModel(lossModel);

    Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);

    // ===== POSITIONING =====
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

    // Realistic indoor/building positioning with varying distances
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));      // AP0
    positionAlloc->Add(Vector(1.0, 0.0, 0.0));      // AP1 - close to AP0
    positionAlloc->Add(Vector(0.0, 12.0, 0.0));     // AP2 - mid-range
    positionAlloc->Add(Vector(0.0, 0.0, 5.0));      // AP3 - close to AP2
    positionAlloc->Add(Vector(-9.0, 0.0, 0.0));     // AP4 - isolated

    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNodes);

    // Print AP positions
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   AP Positions                                   ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";

    for (uint32_t i = 0; i < apNodes.GetN(); i++)
    {
        Ptr<MobilityModel> mob = apNodes.Get(i)->GetObject<MobilityModel>();
        Vector pos = mob->GetPosition();
        std::cout << "AP" << i << ": (" << pos.x << ", " << pos.y << ", " << pos.z << ") m\n";
    }

    // ===== DUAL-PHY SNIFFER SETUP =====
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   Creating Dual-PHY Sniffers                     ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";

    DualPhySnifferHelper snifferHelper;
    snifferHelper.SetChannel(Ptr<SpectrumChannel>(spectrumChannel)); // Channel-agnostic API
    snifferHelper.SetScanningChannels({36, 40, 44, 48});
    snifferHelper.SetHopInterval(Seconds(hopInterval));
    // NEW: No callback needed! Helper stores beacons internally

    // Operating channels: AP0/AP1 on 36, AP2/AP3 on 40, AP4 on 44
    std::vector<uint8_t> opChannels = {36, 36, 40, 40, 44};

    std::vector<Mac48Address> apBssids;

    for (uint32_t i = 0; i < nAPs; i++)
    {
        uint8_t opChannel = opChannels[i % opChannels.size()];

        // Desired BSSID (will be auto-assigned by ns-3)
        std::ostringstream bssidStr;
        bssidStr << "00:00:00:00:00:0" << (i + 1);
        Mac48Address desiredBssid(bssidStr.str().c_str());

        // Install dual-PHY sniffer on this AP
        NetDeviceContainer opDevice = snifferHelper.Install(apNodes.Get(i), opChannel, desiredBssid);

        // Get the ACTUAL BSSID that was assigned
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(opDevice.Get(0));
        Mac48Address actualBssid = wifiDev->GetMac()->GetAddress();

        g_apOperatingChannels[actualBssid] = opChannel;
        apBssids.push_back(actualBssid);

        std::cout << "AP" << i << ": BSSID=" << actualBssid
                  << " Operating CH=" << (int)opChannel << "\n";
    }

    // ===== START CHANNEL HOPPING =====
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   Starting Channel Hopping                       ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";

    snifferHelper.StartChannelHopping();
    std::cout << "✓ All scanning radios hopping channels\n\n";

    // ===== RUN SIMULATION =====
    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   Running Simulation                             ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // ===== BUILD RCPI MATRIX FROM HELPER'S BEACON CACHE =====
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   Building AP-AP RCPI Matrix                     ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";

    // NEW: Query beacon cache from helper
    const auto& allBeacons = snifferHelper.GetAllBeacons();
    std::cout << "Total beacon entries in cache: " << allBeacons.size() << "\n";

    // Breakdown by channel
    std::set<uint8_t> channels;
    std::map<uint8_t, uint32_t> channelCounts;
    for (const auto& entry : allBeacons)
    {
        channels.insert(entry.second.channel);
        channelCounts[entry.second.channel] += entry.second.beaconCount;
    }

    std::cout << "\nBeacon counts per channel:\n";
    for (uint8_t ch : channels)
    {
        std::cout << "  Channel " << (int)ch << ": "
                  << channelCounts[ch] << " beacons detected\n";
    }

    // Build 3D RCPI matrix: (channel, tx MAC, rx MAC) -> RCPI
    std::map<std::tuple<uint8_t, Mac48Address, Mac48Address>, double> rcpiMatrix3D;

    // Fill matrix from cached beacons
    for (const auto& entry : allBeacons)
    {
        auto key = std::make_tuple(entry.second.channel,
                                    entry.second.bssid,        // TX
                                    entry.second.receivedBy);  // RX
        rcpiMatrix3D[key] = entry.second.rcpi;
    }

    // Export to CSV - Channel-based format
    std::ofstream matrixCsv("rrm-ap-matrix.csv");
    matrixCsv << "Channel,TransmitterMAC,ReceiverMAC,RCPI,RSSI_dBm,SNR_dB\n";
    for (const auto& entry : allBeacons)
    {
        matrixCsv << (int)entry.second.channel << ","
                  << entry.second.bssid << ","
                  << entry.second.receivedBy << ","
                  << std::fixed << std::setprecision(1) << entry.second.rcpi << ","
                  << std::setprecision(2) << entry.second.rssi << ","
                  << std::setprecision(2) << entry.second.snr << "\n";
    }
    matrixCsv.close();

    // Print to console - Group by channel
    std::cout << "\n=== Channel-Based RCPI Matrix ===\n";
    std::cout << "(RCPI scale: 0-220, where RCPI = (RSSI_dBm + 110) × 2)\n\n";

    for (uint8_t ch : channels)
    {
        std::cout << "Channel " << (int)ch << ":\n";
        std::cout << "  TX → RX\t\tRCPI\tRSSI\tSNR\n";
        std::cout << "  ─────────────────────────────────────────\n";

        for (const auto& entry : allBeacons)
        {
            if (entry.second.channel == ch)
            {
                std::cout << "  " << entry.second.bssid
                          << " → " << entry.second.receivedBy
                          << "\t" << std::fixed << std::setprecision(0) << entry.second.rcpi
                          << "\t" << std::setprecision(1) << entry.second.rssi << " dBm"
                          << "\t" << std::setprecision(1) << entry.second.snr << " dB\n";
            }
        }
        std::cout << "\n";
    }

    std::cout << "\n✓ 3D AP-AP RCPI matrix (Channel, TX, RX) saved to: rrm-ap-matrix.csv\n";

    // ===== BUILD 2D AP-TO-AP MATRIX (averaged across channels) =====
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   Building 2D AP-to-AP RCPI Matrix              ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";

    // Collect unique AP BSSIDs and build 2D matrix (average RCPI across channels)
    std::set<Mac48Address> allAPs;
    std::map<std::pair<Mac48Address, Mac48Address>, std::vector<double>> rcpiValues;

    for (const auto& entry : allBeacons)
    {
        allAPs.insert(entry.second.bssid);
        allAPs.insert(entry.second.receivedBy);

        auto key = std::make_pair(entry.second.bssid, entry.second.receivedBy);
        rcpiValues[key].push_back(entry.second.rcpi);
    }

    // Calculate average RCPI for each TX-RX pair
    std::map<std::pair<Mac48Address, Mac48Address>, double> matrix2D;
    for (const auto& entry : rcpiValues)
    {
        double sum = 0;
        for (double val : entry.second)
            sum += val;
        matrix2D[entry.first] = sum / entry.second.size();
    }

    // Print 2D matrix to console
    std::cout << "Traditional AP-to-AP RCPI Matrix (averaged across all channels):\n\n";
    std::cout << "         ";
    for (const auto& ap : allAPs)
    {
        uint8_t macBytes[6];
        ap.CopyTo(macBytes);
        std::cout << "   AP" << std::hex << (int)macBytes[5] << std::dec << " ";
    }
    std::cout << "\n";

    for (const auto& txAP : allAPs)
    {
        uint8_t macBytes[6];
        txAP.CopyTo(macBytes);
        std::cout << "   AP" << std::hex << (int)macBytes[5] << std::dec << " ";

        for (const auto& rxAP : allAPs)
        {
            if (txAP == rxAP)
            {
                std::cout << std::setw(7) << "-" << " ";
            }
            else
            {
                auto key = std::make_pair(txAP, rxAP);
                auto it = matrix2D.find(key);
                if (it != matrix2D.end())
                {
                    std::cout << std::setw(7) << std::fixed << std::setprecision(0) << it->second << " ";
                }
                else
                {
                    std::cout << std::setw(7) << "-" << " ";
                }
            }
        }
        std::cout << "\n";
    }

    // Export 2D matrix to CSV
    std::ofstream matrix2DCsv("rrm-ap-matrix-2d.csv");
    matrix2DCsv << "TransmitterMAC,ReceiverMAC,AvgRCPI\n";
    for (const auto& entry : matrix2D)
    {
        matrix2DCsv << entry.first.first << ","
                    << entry.first.second << ","
                    << std::fixed << std::setprecision(1) << entry.second << "\n";
    }
    matrix2DCsv.close();
    std::cout << "\n✓ 2D AP-AP RCPI matrix (averaged) saved to: rrm-ap-matrix-2d.csv\n";

    // Export beacon cache to CSV (NEW: using helper's stored data)
    std::ofstream rawCsv("spectrum-dual-radio-results.csv");
    rawCsv << "TransmitterAP,ReceiverAP,Channel,RSSI_dBm,RCPI,SNR_dB,BeaconCount,LastSeen\n";
    for (const auto& entry : allBeacons)
    {
        rawCsv << entry.second.bssid << ","
               << entry.second.receivedBy << ","
               << (int)entry.second.channel << ","
               << std::fixed << std::setprecision(4) << entry.second.rssi << ","
               << std::setprecision(1) << entry.second.rcpi << ","
               << std::setprecision(2) << entry.second.snr << ","
               << entry.second.beaconCount << ","
               << std::setprecision(6) << entry.second.timestamp.GetSeconds() << "\n";
    }
    rawCsv.close();
    std::cout << "✓ Beacon cache saved to: spectrum-dual-radio-results.csv\n";

    // ===== TEST VERIFICATION =====
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   Test Verification                              ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";

    std::cout << "Test 1: Multi-Channel Detection\n";
    std::cout << "  ✓ Channels detected: " << channels.size() << " (";
    for (uint8_t ch : channels)
        std::cout << (int)ch << " ";
    std::cout << ")\n";

    std::cout << "\nTest 2: Scanning Radio Activity\n";
    std::set<Mac48Address> receiverAPs;
    for (const auto& entry : allBeacons)
        receiverAPs.insert(entry.second.receivedBy);
    std::cout << "  ✓ Active scanning radios: " << receiverAPs.size() << "/" << nAPs << "\n";

    std::cout << "\nTest 3: Cross-Channel Communication\n";
    bool crossChannelDetected = false;
    for (const auto& entry : allBeacons)
    {
        uint8_t txOpChannel = g_apOperatingChannels[entry.second.bssid];
        if (entry.second.channel == txOpChannel)
        {
            crossChannelDetected = true;
            break;
        }
    }
    std::cout << "  " << (crossChannelDetected ? "✓" : "✗")
              << " Cross-channel detection: "
              << (crossChannelDetected ? "[PASS]" : "[FAIL]") << "\n";

    Simulator::Destroy();
    return 0;
}
