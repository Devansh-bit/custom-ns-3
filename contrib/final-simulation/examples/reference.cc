/*
 * Basic WiFi AX Simulation with FlowMonitor and Kafka
 *
 * A simple ns-3 simulation that:
 * - Loads topology from config-simulation.json
 * - Uses 802.11ax (WiFi 6) with YANS channel
 * - Implements waypoint mobility for STAs
 * - Runs bidirectional TCP traffic
 * - Groups flows by connection (like ConnectionMetrics)
 * - Sends metrics to Kafka producer
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/csma-module.h"
#include "ns3/bridge-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/kafka-producer.h"
#include "ns3/kafka-producer-helper.h"
#include "ns3/heap-scheduler.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/unified-phy-sniffer-helper.h"
#include "ns3/dual-phy-sniffer-helper.h"
#include "ns3/beacon-protocol-11k-helper.h"
#include "ns3/neighbor-protocol-11k-helper.h"
#include "ns3/bss_tm_11v-helper.h"
#include "ns3/wifi-cca-monitor-helper.h"
#include "ns3/kafka-consumer.h"
#include "ns3/kafka-consumer-helper.h"
#include "ns3/aci-simulation.h"
#include "ns3/ofdma-simulation.h"
#include "ns3/channel-scoring-helper.h"
#include "ns3/power-scoring-helper.h"
#include "ns3/sta-channel-hopping-helper.h"
#include "ns3/arp-cache.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-interface.h"
#include "ns3/simulation-event-producer.h"
#include "ns3/simulation-event-helper.h"
#include "ns3/load-balance-helper.h"
#include "ns3/adaptive-udp-application.h"
#include "ns3/adaptive-udp-helper.h"

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/error/en.h>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <unordered_map>

using namespace ns3;
using namespace rapidjson;

NS_LOG_COMPONENT_DEFINE("BasicSimulation");

// Global FlowMonitor for stats collection
Ptr<FlowMonitor> g_flowMonitor;
FlowMonitorHelper g_flowMonitorHelper;
double g_statsInterval = 1.0;

// Throughput scaling factor (compensate for reduced PHY/traffic rates)
// Must match divisor in HePhy::GetDataRate (/10)
double g_throughputScaleFactor = 10.0;

// Global Kafka producer
Ptr<KafkaProducer> g_kafkaProducer;
bool g_enableKafka = true;

// Global Simulation Event Producer (for simulator-events topic)
Ptr<SimulationEventProducer> g_simEventProducer;

// Global DS address for flow direction detection
Ipv4Address g_dsAddress;

// AP metrics storage
std::map<uint32_t, ApMetrics> g_apMetrics;

// Node ID mappings (simulation node ID -> config node ID)
std::map<uint32_t, uint32_t> g_simNodeIdToConfigNodeId;      // For STAs
std::map<uint32_t, uint32_t> g_apSimNodeIdToConfigNodeId;    // For APs

// IP to STA index mapping (for identifying STAs)
std::map<Ipv4Address, uint32_t> g_ipToStaIndex;

// IP to STA MAC address mapping
std::map<Ipv4Address, Mac48Address> g_ipToStaMac;

// MAC address string to Node pointer cache (for O(1) lookups in CollectAndSendMetrics)
std::unordered_map<std::string, Ptr<Node>> g_macToNode;

// STA to AP assignment (STA index -> AP index)
std::map<uint32_t, uint32_t> g_staToApIndex;

// ===== MAC Address Storage for Sniffer Filtering =====
// AP BSSIDs (operating radio MAC addresses)
std::set<Mac48Address> g_apBssids;

// STA MAC addresses
std::set<Mac48Address> g_staMacs;

// Scanning radio MAC addresses (created by DualPhySnifferHelper)
std::set<Mac48Address> g_scanningRadioMacs;

// Global DualPhySnifferHelper for beacon monitoring
DualPhySnifferHelper g_dualPhySniffer;

// 802.11k Protocol Helpers
Ptr<NeighborProtocolHelper> g_neighborHelper;
Ptr<BeaconProtocolHelper> g_beaconHelper;

// BSS TM 11v Helpers - one per device (each helper owns its sniffers)
std::vector<Ptr<BssTm11vHelper>> g_apBssTmHelpers;
std::vector<Ptr<BssTm11vHelper>> g_staBssTmHelpers;

// Global device containers for BSS TM triggers
NetDeviceContainer g_apWifiDevices;
NetDeviceContainer g_staWifiDevices;

// STA Channel Hopping Managers (for orphaned client recovery)
std::vector<Ptr<StaChannelHoppingManager>> g_staChannelHoppingManagers;

// ===== Traffic Reinstallation After Roaming =====
// Map STA index -> AdaptiveUdpApplication pointer for traffic restart after roaming
std::map<uint32_t, Ptr<AdaptiveUdpApplication>> g_staAdaptiveApps;
// Track STAs that need app restart (only after disassociation, not initial association)
std::set<uint32_t> g_stasPendingAppRestart;
// Store simulation end time for app stop scheduling
double g_simulationEndTime = 0.0;
// Reference to STA nodes for reinstalling apps
NodeContainer g_staNodes;

// Load Balance Helper (for load-based STA offloading)
Ptr<LoadBalanceHelper> g_loadBalanceHelper;

// ===== CCA Monitoring for BSS Load IE =====
// CCA monitors for channel utilization tracking
std::vector<Ptr<WifiCcaMonitor>> g_ccaMonitors;

// Scanning radio devices (Operating BSSID -> Scanning NetDevice)
std::map<Mac48Address, Ptr<WifiNetDevice>> g_apScanningDevices;

// Scanning channel utilization: AP BSSID -> (Channel -> Utilization data)
struct ScanningChannelUtil {
    double totalUtil = 0.0;
    double wifiUtil = 0.0;
    double nonWifiUtil = 0.0;
};
std::map<Mac48Address, std::map<uint8_t, ScanningChannelUtil>> g_scanningChannelUtilization;

// Scanning channels list from config (for populating empty channels)
std::vector<uint8_t> g_scanningChannels;

// Kafka consumer (enabled by default when Kafka is enabled)
Ptr<KafkaConsumer> g_kafkaConsumer;

// ===== Channel Scoring (ACS/DCS Support) =====
ChannelScoringConfig g_channelScoringConfig;
ChannelScoringHelper g_channelScoringHelper;
bool g_channelScoringEnabled = true;
double g_channelScoringInterval = 10.0;  // Default 10 seconds

// ===== RSSI-based BSS TM Triggering =====
// STA RSSI tracking structure is defined in load-balance-helper.h

// STA RSSI tracking: AP BSSID -> (STA MAC -> RSSI data)
std::map<Mac48Address, std::map<Mac48Address, StaRssiTracker>> g_apStaRssi;

// RSSI threshold from config
double g_rssiThreshold = -70.0;

// EWMA alpha for RSSI smoothing (same as existing: span=3, alpha=0.5)
const double RSSI_EWMA_ALPHA = 0.5;

// Cooldown tracking: (AP BSSID, STA MAC) -> last trigger time
std::map<std::pair<Mac48Address, Mac48Address>, Time> g_rssiTriggerCooldown;
const Time RSSI_TRIGGER_COOLDOWN = Seconds(10);  // 10 second cooldown between triggers

// ===== Power Scoring (Dynamic TX Power Control) =====
PowerScoringConfig g_powerScoringConfig;
PowerScoringHelper g_powerScoringHelper;
bool g_powerScoringEnabled = false;

// Configuration structures
struct ApConfig
{
    uint32_t nodeId;
    double x, y, z;
    double txPower;
    double ccaThreshold;    // CCA Energy Detection threshold (dBm)
    double rxSensitivity;   // RX sensitivity (dBm)
    uint16_t channel;
};

struct WaypointConfig
{
    uint32_t id;
    double x, y, z;
};

struct StaConfig
{
    uint32_t nodeId;
    uint32_t initialWaypointId;
    double waypointSwitchTimeMin;
    double waypointSwitchTimeMax;
    double transferVelocityMin;
    double transferVelocityMax;
};

struct SimulationConfig
{
    double simulationTime;
    std::vector<ApConfig> aps;
    std::vector<WaypointConfig> waypoints;
    std::vector<StaConfig> stas;
    double bssOrchestrationRssiThreshold;  // RSSI threshold for BSS TM triggering

    // ACI (Adjacent Channel Interference) simulation config
    struct {
        bool enabled = false;
        double pathLossExponent = 3.0;
        double maxInterferenceDistanceM = 50.0;
        double clientWeightFactor = 0.1;
        double throughputFactor = 0.3;
        double packetLossFactor = 5.0;
        double latencyFactor = 0.5;
        double jitterFactor = 0.4;
        double channelUtilFactor = 0.15;
    } aci;

    // OFDMA effects simulation config
    struct {
        bool enabled = false;
        uint8_t minStasForBenefit = 2;
        uint8_t saturationStaCount = 9;
        double throughputFactor = 0.35;
        double latencyFactor = 0.45;
        double jitterFactor = 0.50;
        double packetLossFactor = 0.20;
        double channelUtilFactor = 0.25;
    } ofdma;

    // Channel Scoring configuration
    struct {
        bool enabled = false;
        double weightBssid = 0.5;
        double weightRssi = 0.5;
        double weightNonWifi = 0.5;
        double weightOverlap = 0.5;
        double nonWifiDiscardThreshold = 40.0;
        double interval = 10.0;  // Scoring interval in seconds
    } channelScoring;

    // Power Scoring configuration
    struct {
        bool enabled = false;
        double updateInterval = 1.0;           // Update interval in seconds
        double margin = 5.0;                   // M parameter (dBm)
        double gamma = 0.7;                    // MCS change threshold
        double alpha = 0.3;                    // EWMA smoothing factor
        double ofcThreshold = 5.0;             // Frame count threshold
        double obsspdMinDbm = -82.0;           // Min OBSS/PD
        double obsspdMaxDbm = -62.0;           // Max OBSS/PD
        double txPowerRefDbm = 33.0;           // Reference/Maximum TX power
        double txPowerMinDbm = 10.0;            // Min TX power
        double nonWifiThresholdPercent = 40.0; // Non-WiFi mode threshold
        double nonWifiHysteresis = 5.0;        // Hysteresis for mode transitions
    } powerScoring;

    // STA Channel Hopping configuration (for orphaned client recovery via health checks)
    struct {
        bool enabled = true;
    } staChannelHopping;

    // Load Balancing configuration (for load-based STA offloading)
    struct {
        bool enabled = true;
        double channelUtilThreshold = 70.0;  // Utilization threshold (%)
        double intervalSec = 60.0;           // Check interval in seconds
        double cooldownSec = 120.0;          // Cooldown between triggers (seconds)
    } loadBalancing;
};

// Aggregated flow data per connection
struct ConnectionFlowData
{
    std::string staIp;
    uint32_t staIndex;
    uint32_t apIndex;

    // Uplink (STA -> DS)
    uint64_t uplinkBytes = 0;
    uint32_t uplinkTxPackets = 0;
    uint32_t uplinkRxPackets = 0;
    double uplinkDelaySum = 0.0;
    double uplinkJitterSum = 0.0;
    double uplinkDuration = 0.0;

    // Downlink (DS -> STA)
    uint64_t downlinkBytes = 0;
    uint32_t downlinkTxPackets = 0;
    uint32_t downlinkRxPackets = 0;
    double downlinkDelaySum = 0.0;
    double downlinkJitterSum = 0.0;
    double downlinkDuration = 0.0;
};

// EWMA history for each connection (delta-based smoothing)
struct ConnectionHistory
{
    // Flow tracking - reset history when flow ID changes (e.g., after roaming)
    uint32_t lastFlowId = 0;
    bool hasFlowId = false;

    // Previous FlowMonitor stats for delta calculation
    uint64_t prevUplinkBytes = 0;
    uint64_t prevDownlinkBytes = 0;
    uint32_t prevUplinkTxPackets = 0;
    uint32_t prevUplinkRxPackets = 0;
    uint32_t prevDownlinkTxPackets = 0;
    uint32_t prevDownlinkRxPackets = 0;
    double prevUplinkDelaySum = 0.0;  // in ms
    double prevDownlinkDelaySum = 0.0;  // in ms
    double prevUplinkJitterSum = 0.0;  // in ms
    double prevDownlinkJitterSum = 0.0;  // in ms

    // EWMA smoothed values
    double ewmaRtt = 0.0;
    double ewmaJitter = 0.0;
    double ewmaUplinkThroughput = 0.0;
    double ewmaDownlinkThroughput = 0.0;
    double ewmaPacketLoss = 0.0;

    // Tracking
    double lastUpdateTime = 0.0;
    bool initialized = false;

    // Reset prev values when flow changes (roaming creates new flow)
    void resetPrevValues()
    {
        prevUplinkBytes = 0;
        prevDownlinkBytes = 0;
        prevUplinkTxPackets = 0;
        prevUplinkRxPackets = 0;
        prevDownlinkTxPackets = 0;
        prevDownlinkRxPackets = 0;
        prevUplinkDelaySum = 0.0;
        prevDownlinkDelaySum = 0.0;
        prevUplinkJitterSum = 0.0;
        prevDownlinkJitterSum = 0.0;
    }
};

// EWMA alpha: using span=3 formula: α = 2/(span+1) = 2/4 = 0.5
// This gives ~3 window effective memory (older samples have <13% weight)
const double EWMA_ALPHA = 2.0 / 4.0;

// Start EWMA collection after this time (seconds)
const double EWMA_START_TIME = 10.0;

// Global history storage for delta + EWMA calculations
std::map<std::string, ConnectionHistory> g_connectionHistory;

// Forward declarations
void CollectAndSendMetrics();
SimulationConfig LoadConfig(const std::string& filename);
void ProcessBeaconMeasurements();
void PerformChannelScoring();
void PerformPowerScoring();
void OnApHeSigA(uint32_t nodeId, HeSigAParameters params);
void OnApPhyRxPayloadBegin(uint32_t nodeId, WifiTxVector txVector, Time psduDuration);
double GetNonWifiForAp(uint32_t nodeId);

// ============================================================================
// TRAFFIC REINSTALLATION AFTER ROAMING
// ============================================================================

/**
 * Reinstall AdaptiveUdpApplication for a STA after roaming
 * Creates fresh socket with correct routing
 * Called after STA successfully re-associates following disassociation
 */
void ReinstallAdaptiveApp(uint32_t staIndex)
{
    if (staIndex >= g_staNodes.GetN())
    {
        std::cerr << "[TRAFFIC] ReinstallAdaptiveApp: Invalid staIndex " << staIndex << std::endl;
        return;
    }

    Ptr<Node> staNode = g_staNodes.Get(staIndex);
    if (!staNode)
    {
        std::cerr << "[TRAFFIC] ReinstallAdaptiveApp: No node for staIndex " << staIndex << std::endl;
        return;
    }

    // Stop old application if exists
    auto oldAppIt = g_staAdaptiveApps.find(staIndex);
    if (oldAppIt != g_staAdaptiveApps.end() && oldAppIt->second)
    {
        oldAppIt->second->SetStopTime(Simulator::Now());
    }

    // Create new AdaptiveUdp application with fresh socket and AIMD congestion control
    uint16_t port = 9;
    AdaptiveUdpHelper adaptive("ns3::UdpSocketFactory",
                               InetSocketAddress(g_dsAddress, port));
    adaptive.SetAttribute("InitialDataRate", DataRateValue(DataRate("3Mbps")));
    adaptive.SetAttribute("PacketSize", UintegerValue(1400));
    adaptive.SetAttribute("MinDataRate", DataRateValue(DataRate("25Kbps")));
    adaptive.SetAttribute("MaxDataRate", DataRateValue(DataRate("5Mbps")));

    ApplicationContainer newApp = adaptive.Install(staNode);
    newApp.Start(Simulator::Now() + MilliSeconds(50));  // Brief delay for routing
    newApp.Stop(Seconds(g_simulationEndTime));

    g_staAdaptiveApps[staIndex] = DynamicCast<AdaptiveUdpApplication>(newApp.Get(0));

    std::cout << "[TRAFFIC] Reinstalled AdaptiveUdp app for STA " << staIndex
              << " at t=" << Simulator::Now().GetSeconds() << "s" << std::endl;
}

// ============================================================================
// ASSOCIATION EVENT CALLBACKS
// ============================================================================

/**
 * Callback when a STA successfully associates with an AP
 * Logs the association event with STA MAC and AP BSSID
 */
void OnStaAssociated(uint32_t staIndex, Mac48Address apBssid)
{
    Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(g_staWifiDevices.Get(staIndex));
    if (!staDevice) return;

    Mac48Address staMac = staDevice->GetMac()->GetAddress();

    // Find AP index from BSSID
    uint32_t newApIndex = 0;
    for (const auto& [nodeId, metrics] : g_apMetrics)
    {
        if (metrics.bssid == apBssid)
        {
            newApIndex = nodeId;
            break;
        }
    }

    std::cout << "\n[ASSOC] " << std::fixed << std::setprecision(2)
              << Simulator::Now().GetSeconds() << "s: "
              << "STA " << staMac << " (idx=" << staIndex << ") "
              << "ASSOCIATED with AP " << apBssid << " (node=" << newApIndex << ")"
              << std::endl;

    // Note: associatedClients count is now updated in CollectAndSendMetrics()
    // by querying actual STA list from ApWifiMac to avoid callback sync issues

    // Update STA to AP mapping
    g_staToApIndex[staIndex] = newApIndex;

    // Record association event to Kafka
    if (g_simEventProducer)
    {
        // Get config node IDs
        uint32_t staConfigNodeId = g_simNodeIdToConfigNodeId.count(staIndex)
                                       ? g_simNodeIdToConfigNodeId[staIndex]
                                       : staIndex;
        uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(newApIndex)
                                      ? g_apSimNodeIdToConfigNodeId[newApIndex]
                                      : newApIndex;

        std::ostringstream bssidStr;
        bssidStr << apBssid;
        g_simEventProducer->RecordAssociation(staConfigNodeId, apConfigNodeId, bssidStr.str());
    }

    // Reinstall AdaptiveUdp application if this STA was previously disassociated (roaming scenario)
    // This creates a fresh socket to restore traffic after roaming
    if (g_stasPendingAppRestart.count(staIndex) > 0)
    {
        g_stasPendingAppRestart.erase(staIndex);
        // Schedule reinstall with delay to allow routing table to stabilize
        Simulator::Schedule(MilliSeconds(200), &ReinstallAdaptiveApp, staIndex);
    }
}

/**
 * Callback when a STA deassociates from an AP
 */
void OnStaDeassociated(uint32_t staIndex, Mac48Address apBssid)
{
    Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(g_staWifiDevices.Get(staIndex));
    if (!staDevice) return;

    Mac48Address staMac = staDevice->GetMac()->GetAddress();

    // Find AP index from BSSID
    uint32_t oldApIndex = 0;
    for (const auto& [nodeId, metrics] : g_apMetrics)
    {
        if (metrics.bssid == apBssid)
        {
            oldApIndex = nodeId;
            break;
        }
    }

    std::cout << "\n[DEASSOC] " << std::fixed << std::setprecision(2)
              << Simulator::Now().GetSeconds() << "s: "
              << "STA " << staMac << " (idx=" << staIndex << ") "
              << "DEASSOCIATED from AP " << apBssid << " (node=" << oldApIndex << ")"
              << std::endl;

    // Note: associatedClients count is now updated in CollectAndSendMetrics()
    // by querying actual STA list from ApWifiMac to avoid callback sync issues

    // Record deassociation event to Kafka
    if (g_simEventProducer)
    {
        uint32_t staConfigNodeId = g_simNodeIdToConfigNodeId.count(staIndex)
                                       ? g_simNodeIdToConfigNodeId[staIndex]
                                       : staIndex;
        uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(oldApIndex)
                                      ? g_apSimNodeIdToConfigNodeId[oldApIndex]
                                      : oldApIndex;

        std::ostringstream bssidStr;
        bssidStr << apBssid;
        g_simEventProducer->RecordDeassociation(staConfigNodeId, apConfigNodeId, bssidStr.str());
    }

    // Mark this STA for app restart when it re-associates
    // This ensures traffic resumes after roaming
    g_stasPendingAppRestart.insert(staIndex);
}

// ============================================================================
// CHANNEL SCORING (ACS/DCS)
// ============================================================================

/**
 * Process beacon measurements from dual-phy-sniffer
 * Populates ApMetrics.scanningChannelData for channel scoring
 */
void
ProcessBeaconMeasurements()
{
    // Build set of known AP BSSIDs for filtering
    std::set<Mac48Address> knownApBssids;
    for (const auto& [nid, metrics] : g_apMetrics) {
        knownApBssids.insert(metrics.bssid);
    }

    // Process each AP
    for (auto& [nodeId, apMetrics] : g_apMetrics) {
        Mac48Address receiverBssid = apMetrics.bssid;

        // Get beacons from scanning radio
        std::vector<BeaconInfo> beacons = g_dualPhySniffer.GetBeaconsReceivedBy(receiverBssid);

        // Organize beacons by channel: Map<Channel, Map<NeighborBSSID, BeaconInfo>>
        std::map<uint8_t, std::map<Mac48Address, BeaconInfo>> channelBeacons;
        for (const auto& beacon : beacons) {
            if (receiverBssid == beacon.bssid) continue;  // Skip self
            if (knownApBssids.find(beacon.bssid) == knownApBssids.end()) continue;  // Skip unknown
            channelBeacons[beacon.channel][beacon.bssid] = beacon;
        }

        // Start with ALL scanning channels from config
        std::set<uint8_t> allChannels(g_scanningChannels.begin(), g_scanningChannels.end());
        allChannels.insert(apMetrics.channel);  // Always include operating channel

        // Create random number generator for empty channels (0-5%)
        static Ptr<UniformRandomVariable> randWifi;
        if (!randWifi) {
            randWifi = CreateObject<UniformRandomVariable>();
            randWifi->SetAttribute("Min", DoubleValue(0.0));
            randWifi->SetAttribute("Max", DoubleValue(0.05));  // 0-5% normalized to 0-0.05
        }

        // Populate scanningChannelData for all channels
        for (uint8_t channel : allChannels) {
            ChannelScanData& scanData = apMetrics.scanningChannelData[channel];

            // Check if this channel has beacon data (APs exist on this channel)
            auto beaconIt = channelBeacons.find(channel);
            bool hasApOnChannel = (beaconIt != channelBeacons.end());

            // Set CCA utilization based on channel type
            if (channel == apMetrics.channel) {
                // Operating channel - use live CCA data from operating radio (normalize to 0-1)
                scanData.channelUtilization = apMetrics.channelUtilization / 100.0;
                scanData.wifiChannelUtilization = apMetrics.wifiChannelUtilization / 100.0;
                scanData.nonWifiChannelUtilization = apMetrics.nonWifiChannelUtilization / 100.0;
            } else if (hasApOnChannel) {
                // Channel with APs - take MAX of all neighbors' utilization from beacon data
                double maxChannelUtil = 0.0;
                double maxWifiUtil = 0.0;
                double maxNonWifiUtil = 0.0;

                for (const auto& [neighborBssid, beaconInfo] : beaconIt->second) {
                    maxChannelUtil = std::max(maxChannelUtil, static_cast<double>(beaconInfo.channelUtilization));
                    maxWifiUtil = std::max(maxWifiUtil, static_cast<double>(beaconInfo.wifiUtilization));
                    maxNonWifiUtil = std::max(maxNonWifiUtil, static_cast<double>(beaconInfo.nonWifiUtilization));
                }

                // Normalize to 0-1 (beacon data is in 0-255 scale per IEEE 802.11)
                scanData.channelUtilization = maxChannelUtil / 255.0;
                scanData.wifiChannelUtilization = maxWifiUtil / 255.0;
                scanData.nonWifiChannelUtilization = maxNonWifiUtil / 255.0;
            } else {
                // Empty channel - wifi=0, non_wifi=channel=random(0, 0.05)
                double randomUtil = randWifi->GetValue();  // 0-0.05
                scanData.wifiChannelUtilization = 0.0;
                scanData.nonWifiChannelUtilization = randomUtil;
                scanData.channelUtilization = randomUtil;
            }

            // Populate neighbor data from beacons (normalize values to 0-1)
            if (hasApOnChannel) {
                scanData.bssidCount = beaconIt->second.size();
                scanData.neighbors.clear();
                for (const auto& [neighborBssid, beaconInfo] : beaconIt->second) {
                    ChannelNeighborInfo neighborInfo{};
                    std::ostringstream oss;
                    oss << neighborBssid;
                    neighborInfo.bssid = oss.str();
                    neighborInfo.rssi = beaconInfo.rssi;
                    neighborInfo.channel = beaconInfo.channel;
                    neighborInfo.channelWidth = beaconInfo.channelWidth;
                    neighborInfo.staCount = beaconInfo.staCount;
                    // Normalize neighbor utilization values to 0-1 (beacon data is 0-255 scale)
                    neighborInfo.channelUtil = beaconInfo.channelUtilization / 255.0;
                    neighborInfo.wifiUtil = beaconInfo.wifiUtilization / 255.0;
                    neighborInfo.nonWifiUtil = beaconInfo.nonWifiUtilization / 255.0;
                    scanData.neighbors.push_back(neighborInfo);
                }
            } else {
                scanData.bssidCount = 0;
                scanData.neighbors.clear();
            }
        }
    }
}

/**
 * Periodic channel scoring callback
 * Scores all channels and performs sequential allocation
 */
void
PerformChannelScoring()
{
    // First, update scan data
    ProcessBeaconMeasurements();

    std::cout << "\n========== CHANNEL SCORING (t=" << Simulator::Now().GetSeconds()
              << "s) ==========" << std::endl;

    // Sequential allocation: track channels already allocated
    std::set<uint8_t> allocatedChannels;

    for (const auto& [nodeId, apMetrics] : g_apMetrics) {
        // Calculate scores
        auto scores = g_channelScoringHelper.CalculateScores(apMetrics.scanningChannelData);

        if (scores.empty()) {
            std::cout << "AP " << nodeId << ": No scan data" << std::endl;
            continue;
        }

        // Find best available channel
        uint8_t bestCh = 0;
        for (const auto& score : scores) {
            if (score.discarded) continue;
            if (!ChannelScoringHelper::IsChannelInBand(score.channel, apMetrics.band)) continue;
            if (allocatedChannels.count(score.channel) > 0) continue;
            bestCh = score.channel;
            break;
        }

        if (bestCh != 0) {
            allocatedChannels.insert(bestCh);

            if (bestCh != apMetrics.channel) {
                std::cout << "AP " << nodeId << ": Switching from Ch " << (int)apMetrics.channel
                          << " to Ch " << (int)bestCh << std::endl;

                // Schedule channel switch directly via ApWifiMac
                Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(nodeId));
                if (apDev) {
                    uint32_t capturedNodeId = nodeId;
                    uint8_t capturedBestCh = bestCh;
                    uint32_t delayMs = 100 + (nodeId * 50);  // Stagger switches
                    Ptr<WifiNetDevice> capturedDev = apDev;

                    Simulator::Schedule(MilliSeconds(delayMs), [capturedDev, capturedNodeId, capturedBestCh]() {
                        Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(capturedDev->GetMac());
                        if (apMac) {
                            // Enable coordinated channel switch to preserve client associations
                            apMac->SetCoordinatedChannelSwitch(true);

                            apMac->SwitchChannel(capturedBestCh);

                            // Reset flag after 500ms
                            Simulator::Schedule(MilliSeconds(500), [apMac]() {
                                apMac->SetCoordinatedChannelSwitch(false);
                            });

                            // Update g_apMetrics
                            auto metricsIt = g_apMetrics.find(capturedNodeId);
                            if (metricsIt != g_apMetrics.end()) {
                                metricsIt->second.channel = capturedBestCh;
                            }
                        }
                    });
                }
            } else {
                std::cout << "AP " << nodeId << ": Keeping Ch " << (int)bestCh << std::endl;
            }
        }
    }

    std::cout << "==========================================================" << std::endl;

    // Schedule next scoring using configurable interval
    Simulator::Schedule(Seconds(g_channelScoringInterval), &PerformChannelScoring);
}

void
CollectAndSendMetrics()
{
    // Health checks are now triggered after metrics collection to include packet loss data

    // Update scanning channel data before collecting metrics
    ProcessBeaconMeasurements();

    g_flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(g_flowMonitorHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = g_flowMonitor->GetFlowStats();

    // Group flows by STA (connection)
    std::map<std::string, ConnectionFlowData> connectionFlows;

    // Map to store packet loss per STA index for health checks
    std::map<uint32_t, double> staPacketLoss;

    // FIRST PASS: Find the highest (most recent) flow ID for each STA
    // FlowMonitor keeps ALL historical flows, we only want the latest
    // Also skip flows that have been inactive for too long (stale flows from old roaming)
    std::map<std::string, uint32_t> latestFlowIdPerSta;
    Time now = Simulator::Now();
    Time staleThreshold = Seconds(30);  // Skip flows with no activity in last 30 seconds

    for (auto& iter : stats)
    {
        uint32_t flowId = iter.first;
        FlowMonitor::FlowStats& fs = iter.second;
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flowId);

        if (t.destinationPort != 9) continue;  // Only uplink

        // Skip stale flows - no packets received recently
        if (fs.timeLastRxPacket.IsZero() || (now - fs.timeLastRxPacket) > staleThreshold)
        {
            continue;  // Flow has been inactive too long, skip it
        }

        Ipv4Address staIp = t.sourceAddress;
        std::ostringstream oss;
        staIp.Print(oss);
        std::string staIpStr = oss.str();

        // Keep track of highest flow ID per STA (higher = more recent)
        if (latestFlowIdPerSta.find(staIpStr) == latestFlowIdPerSta.end() ||
            flowId > latestFlowIdPerSta[staIpStr])
        {
            latestFlowIdPerSta[staIpStr] = flowId;
        }
    }

    // SECOND PASS: Only process the latest flow for each STA
    for (auto& iter : stats)
    {
        uint32_t flowId = iter.first;
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flowId);
        FlowMonitor::FlowStats& fs = iter.second;

        // Only track uplink data flows (STA -> DS, destination port 9)
        bool isUplinkData = (t.destinationPort == 9);
        if (!isUplinkData)
        {
            continue; // Skip non-uplink flows (ACKs, etc.)
        }

        // Get STA IP (source of uplink flow)
        Ipv4Address staIp = t.sourceAddress;
        std::ostringstream oss;
        staIp.Print(oss);
        std::string staIpStr = oss.str();

        // SKIP old/stale flows - only process the latest active flow for this STA
        auto latestIt = latestFlowIdPerSta.find(staIpStr);
        if (latestIt == latestFlowIdPerSta.end() || latestIt->second != flowId)
        {
            continue;  // This is an old or stale flow, skip it
        }

        // Get STA and AP indices
        auto staIt = g_ipToStaIndex.find(staIp);
        if (staIt == g_ipToStaIndex.end())
        {
            continue; // Unknown STA
        }
        uint32_t staIndex = staIt->second;
        uint32_t apIndex = g_staToApIndex[staIndex];

        // Initialize connection if needed
        if (connectionFlows.find(staIpStr) == connectionFlows.end())
        {
            ConnectionFlowData data;
            data.staIp = staIpStr;
            data.staIndex = staIndex;
            data.apIndex = apIndex;
            connectionFlows[staIpStr] = data;
        }

        ConnectionFlowData& conn = connectionFlows[staIpStr];

        // Always update apIndex (may have changed due to roaming)
        conn.apIndex = apIndex;

        // Store cumulative uplink stats (for packet count display)
        conn.uplinkBytes = fs.rxBytes;
        conn.uplinkTxPackets = fs.txPackets;
        conn.uplinkRxPackets = fs.rxPackets;

        // Get or create EWMA history for this connection
        ConnectionHistory& history = g_connectionHistory[staIpStr];

        // Detect flow ID changes (happens after roaming) and reset prev values
        if (history.hasFlowId && history.lastFlowId != flowId)
        {
            NS_LOG_DEBUG("Flow ID changed for " << staIpStr << ": "
                         << history.lastFlowId << " -> " << flowId << ", resetting history");
            history.resetPrevValues();
        }
        history.lastFlowId = flowId;
        history.hasFlowId = true;

        // Calculate DELTAS from previous FlowMonitor stats
        // Guard against unsigned underflow (can happen after roaming/flow reset)
        uint64_t deltaBytes = (fs.rxBytes >= history.prevUplinkBytes)
                              ? (fs.rxBytes - history.prevUplinkBytes) : 0;
        uint32_t deltaTxPackets = (fs.txPackets >= history.prevUplinkTxPackets)
                                  ? (fs.txPackets - history.prevUplinkTxPackets) : 0;
        uint32_t deltaRxPackets = (fs.rxPackets >= history.prevUplinkRxPackets)
                                  ? (fs.rxPackets - history.prevUplinkRxPackets) : 0;
        double deltaDelayMs = (fs.delaySum.GetSeconds() * 1000.0) - history.prevUplinkDelaySum;
        double deltaJitterMs = (fs.jitterSum.GetSeconds() * 1000.0) - history.prevUplinkJitterSum;

        // Update previous values for next iteration
        history.prevUplinkBytes = fs.rxBytes;
        history.prevUplinkTxPackets = fs.txPackets;
        history.prevUplinkRxPackets = fs.rxPackets;
        history.prevUplinkDelaySum = fs.delaySum.GetSeconds() * 1000.0;
        history.prevUplinkJitterSum = fs.jitterSum.GetSeconds() * 1000.0;

        // Compute INSTANTANEOUS metrics from deltas
        double instantRtt = 0.0;
        double instantJitter = 0.0;
        double instantThroughput = 0.0;
        double instantLoss = 0.0;

        if (deltaRxPackets > 0)
        {
            instantRtt = deltaDelayMs / deltaRxPackets;
            instantJitter = deltaJitterMs / deltaRxPackets;
        }

        if (g_statsInterval > 0.0)
        {
            instantThroughput = (deltaBytes * 8.0) / g_statsInterval / 1e6;

            // Safety cap at 50 Mbps per STA (well above 12 Mbps max configured rate)
            // This catches any remaining edge cases after flow ID tracking fix
            const double MAX_THROUGHPUT_MBPS = 50.0;
            if (instantThroughput > MAX_THROUGHPUT_MBPS)
            {
                instantThroughput = MAX_THROUGHPUT_MBPS;
            }
        }

        if (deltaTxPackets > 0 && deltaTxPackets >= deltaRxPackets)
        {
            instantLoss = static_cast<double>(deltaTxPackets - deltaRxPackets) / deltaTxPackets;
        }
        else
        {
            instantLoss = 0.0;  // No loss or more received than sent (edge case)
        }

        // Apply EWMA smoothing only after EWMA_START_TIME
        double nowSec = Simulator::Now().GetSeconds();
        if (nowSec >= EWMA_START_TIME)
        {
            if (!history.initialized)
            {
                // First sample after start time - use raw values
                history.ewmaRtt = instantRtt;
                history.ewmaJitter = instantJitter;
                history.ewmaUplinkThroughput = instantThroughput;
                history.ewmaPacketLoss = instantLoss;
                history.initialized = true;
            }
            else
            {
                // EWMA update: new = α * instant + (1 - α) * old
                // With span=3, after 3 windows oldest sample has ~12.5% weight
                history.ewmaRtt = EWMA_ALPHA * instantRtt + (1.0 - EWMA_ALPHA) * history.ewmaRtt;
                history.ewmaJitter = EWMA_ALPHA * instantJitter + (1.0 - EWMA_ALPHA) * history.ewmaJitter;
                history.ewmaUplinkThroughput = EWMA_ALPHA * instantThroughput + (1.0 - EWMA_ALPHA) * history.ewmaUplinkThroughput;
                history.ewmaPacketLoss = EWMA_ALPHA * instantLoss + (1.0 - EWMA_ALPHA) * history.ewmaPacketLoss;
            }

            // Safety cap on EWMA (matches instant cap)
            const double MAX_EWMA_THROUGHPUT_MBPS = 50.0;
            if (history.ewmaUplinkThroughput > MAX_EWMA_THROUGHPUT_MBPS)
            {
                history.ewmaUplinkThroughput = MAX_EWMA_THROUGHPUT_MBPS;
            }
        }

        history.lastUpdateTime = nowSec;

        // Store packet loss for this STA for health checks
        staPacketLoss[staIndex] = history.ewmaPacketLoss;
    }

    // Build ConnectionMetrics and update ApMetrics
    for (auto& apEntry : g_apMetrics)
    {
        apEntry.second.connectionMetrics.clear();
        apEntry.second.bytesSent = 0;
        apEntry.second.bytesReceived = 0;
        apEntry.second.throughputMbps = 0.0;
    }

    for (const auto& [staIpStr, flowData] : connectionFlows)
    {
        // Get EWMA history for this connection
        auto histIt = g_connectionHistory.find(staIpStr);
        if (histIt == g_connectionHistory.end() || !histIt->second.initialized)
        {
            continue;  // No history or EWMA not started yet (before EWMA_START_TIME)
        }
        const ConnectionHistory& history = histIt->second;

        // Build ConnectionMetrics using EWMA-smoothed values
        ConnectionMetrics conn;

        // Look up STA MAC from IP
        Ipv4Address staIp;
        std::istringstream iss(staIpStr);
        std::string ipStr;
        iss >> ipStr;
        staIp = Ipv4Address(ipStr.c_str());

        Mac48Address staMac;
        auto macIt = g_ipToStaMac.find(staIp);
        if (macIt != g_ipToStaMac.end())
        {
            staMac = macIt->second;
        }

        // Get AP BSSID
        Mac48Address apBssid = g_apMetrics[flowData.apIndex].bssid;

        // Build connection key as "staMac->apBssid" (matching config-simulation format)
        std::ostringstream connKeyStream;
        connKeyStream << staMac << "->" << apBssid;
        std::string connKey = connKeyStream.str();

        // Convert MAC addresses to strings
        std::ostringstream staMacStream, apBssidStream;
        staMacStream << staMac;
        apBssidStream << apBssid;

        conn.staAddress = (staMac == Mac48Address()) ? staIpStr : staMacStream.str();
        conn.APAddress = apBssidStream.str();

        // RTT from EWMA (delta-based, smoothed)
        conn.meanRTTLatency = history.ewmaRtt;

        // Jitter from EWMA (delta-based, smoothed)
        conn.jitterMs = history.ewmaJitter;

        // Packet count (cumulative - for informational purposes)
        conn.packetCount = flowData.uplinkRxPackets + flowData.downlinkRxPackets;

        // Throughput from EWMA (delta-based, smoothed)
        conn.uplinkThroughputMbps = history.ewmaUplinkThroughput;
        // Hardcode downlink to ±10% of uplink (no actual downlink traffic in this sim)
        static Ptr<UniformRandomVariable> dlVariationRv = CreateObject<UniformRandomVariable>();
        dlVariationRv->SetAttribute("Min", DoubleValue(-0.10));
        dlVariationRv->SetAttribute("Max", DoubleValue(0.10));
        double variation = dlVariationRv->GetValue();
        conn.downlinkThroughputMbps = history.ewmaUplinkThroughput * (1.0 + variation);

        // Packet loss from EWMA (delta-based, smoothed)
        conn.packetLossRate = history.ewmaPacketLoss;
        conn.MACRetryRate = 0.0;

        // Get RSSI from g_apStaRssi (populated by PeriodicRssiCheck)
        conn.apViewRSSI = 0.0;
        conn.apViewSNR = 0.0;
        conn.staViewRSSI = 0.0;
        conn.staViewSNR = 0.0;

        // Look up RSSI using STA MAC and AP BSSID
        if (flowData.staIndex < g_staWifiDevices.GetN())
        {
            Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(g_staWifiDevices.Get(flowData.staIndex));
            if (staDevice)
            {
                staMac = staDevice->GetMac()->GetAddress();
                // apBssid already set at line 770

                // Noise floor for SNR calculation (standard WiFi thermal noise at 20MHz)
                const double NOISE_FLOOR_DBM = -93.0;

                // Get EWMA-smoothed RSSI from roaming tracker (AP's view of STA)
                auto apIt = g_apStaRssi.find(apBssid);
                if (apIt != g_apStaRssi.end())
                {
                    auto staIt = apIt->second.find(staMac);
                    if (staIt != apIt->second.end() && staIt->second.initialized)
                    {
                        conn.apViewRSSI = staIt->second.ewmaRssi;
                        conn.apViewSNR = conn.apViewRSSI - NOISE_FLOOR_DBM;
                    }
                }

                // Get STA's view of AP RSSI (from STA's station manager)
                Ptr<WifiRemoteStationManager> staStationMgr = staDevice->GetRemoteStationManager();
                if (staStationMgr)
                {
                    auto staRssiOpt = staStationMgr->GetMostRecentRssi(apBssid);
                    if (staRssiOpt.has_value())
                    {
                        conn.staViewRSSI = static_cast<double>(staRssiOpt.value());
                        conn.staViewSNR = conn.staViewRSSI - NOISE_FLOOR_DBM;
                    }
                }
            }
        }

        conn.uplinkMCS = 0;
        conn.downlinkMCS = 0;
        conn.lastUpdate = Simulator::Now();

        // Add to AP metrics using connKey (staMac->apBssid format)
        uint32_t apNodeId = flowData.apIndex; // Using AP index as node ID
        if (g_apMetrics.find(apNodeId) != g_apMetrics.end())
        {
            // Scale throughput metrics to compensate for reduced PHY/traffic rates
            conn.uplinkThroughputMbps *= g_throughputScaleFactor;
            conn.downlinkThroughputMbps *= g_throughputScaleFactor;

            ApMetrics& ap = g_apMetrics[apNodeId];
            ap.connectionMetrics[connKey] = conn;
            ap.bytesSent += flowData.downlinkBytes;
            ap.bytesReceived += flowData.uplinkBytes;
            ap.throughputMbps += conn.uplinkThroughputMbps + conn.downlinkThroughputMbps;
        }
    }

    // ========== TRIGGER RSSI-BASED HEALTH CHECKS FOR ORPHANED STAs ==========
    // Only trigger connection recovery for STAs that cannot hear their AP (rssi_sta=0)
    // Pass 0.0 for packet loss to disable stale connection detection (which caused cycling)
    for (uint32_t i = 0; i < g_staChannelHoppingManagers.size(); i++) {
        auto manager = g_staChannelHoppingManagers[i];
        if (manager) {
            // Pass 0.0 for packet loss to disable stale connection detection
            // PerformHealthCheck will still check RSSI internally for orphan detection
            manager->PerformHealthCheck(0.0);
        }
    }

    // Update associated clients count and PHY ground truth (channel, power) from actual devices
    for (uint32_t apIndex = 0; apIndex < g_apWifiDevices.GetN(); apIndex++)
    {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(apIndex));
        if (!apDev) continue;

        Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDev->GetMac());
        if (!apMac) continue;

        // Get actual count from MAC layer (link 0 for single-link)
        const auto& staList = apMac->GetStaList(0);
        g_apMetrics[apIndex].associatedClients = staList.size();

        Mac48Address apBssid = apMac->GetAddress();

        // Ensure all associated STAs appear in connectionMetrics
        // This catches STAs with 100% packet loss whose flows became "stale"
        for (const auto& [aid, staMac] : staList)
        {
            std::ostringstream connKeyStream;
            connKeyStream << staMac << "->" << apBssid;
            std::string connKey = connKeyStream.str();

            // Check if this STA is already in connectionMetrics
            if (g_apMetrics[apIndex].connectionMetrics.find(connKey) == g_apMetrics[apIndex].connectionMetrics.end())
            {
                // STA is associated but has no flow data (stale/100% loss) - add with defaults
                ConnectionMetrics conn;
                std::ostringstream staMacStream, apBssidStream;
                staMacStream << staMac;
                apBssidStream << apBssid;
                conn.staAddress = staMacStream.str();
                conn.APAddress = apBssidStream.str();
                conn.meanRTTLatency = 0.0;
                conn.jitterMs = 0.0;
                conn.packetCount = 0;
                conn.uplinkThroughputMbps = 0.0;
                conn.downlinkThroughputMbps = 0.0;
                conn.packetLossRate = 1.0;  // 100% loss
                conn.MACRetryRate = 0.0;
                conn.apViewRSSI = 0.0;
                conn.apViewSNR = 0.0;
                conn.staViewRSSI = 0.0;
                conn.staViewSNR = 0.0;

                // Get RSSI if available
                Ptr<WifiRemoteStationManager> stationManager = apDev->GetRemoteStationManager();
                if (stationManager)
                {
                    auto rssiOpt = stationManager->GetMostRecentRssi(staMac);
                    if (rssiOpt.has_value())
                    {
                        conn.apViewRSSI = static_cast<double>(rssiOpt.value());
                        conn.apViewSNR = conn.apViewRSSI - (-93.0);  // NOISE_FLOOR_DBM
                    }
                }

                // Get STA's view of AP RSSI
                std::ostringstream staMacStr;
                staMacStr << staMac;
                auto nodeIt = g_macToNode.find(staMacStr.str());
                if (nodeIt != g_macToNode.end())
                {
                    Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(nodeIt->second->GetDevice(0));
                    if (staDevice)
                    {
                        Ptr<WifiRemoteStationManager> staStationMgr = staDevice->GetRemoteStationManager();
                        if (staStationMgr)
                        {
                            auto staRssiOpt = staStationMgr->GetMostRecentRssi(apBssid);
                            if (staRssiOpt.has_value())
                            {
                                conn.staViewRSSI = static_cast<double>(staRssiOpt.value());
                                conn.staViewSNR = conn.staViewRSSI - (-93.0);
                            }
                        }
                    }
                }

                conn.uplinkMCS = 0;
                conn.downlinkMCS = 0;
                conn.lastUpdate = Simulator::Now();

                g_apMetrics[apIndex].connectionMetrics[connKey] = conn;
                NS_LOG_DEBUG("Added missing associated STA " << staMac << " to AP " << apBssid << " metrics (stale/no-flow)");
            }
        }

        // Get PHY ground truth for channel and TX power
        Ptr<WifiPhy> phy = apDev->GetPhy();
        if (phy)
        {
            g_apMetrics[apIndex].channel = phy->GetChannelNumber();
            g_apMetrics[apIndex].txPowerDbm = phy->GetTxPowerStart();
        }
    }

    // Apply ACI (Adjacent Channel Interference) effects if enabled
    if (AciSimulation::IsEnabled())
    {
        AciSimulation::Apply(g_apMetrics);
    }

    // Apply OFDMA effects if enabled
    if (OfdmaSimulation::IsEnabled())
    {
        OfdmaSimulation::Apply(g_apMetrics);
    }

    // Update AP and connection metrics with proper node IDs and positions before sending
    for (auto& apEntry : g_apMetrics) {
        ApMetrics& ap = apEntry.second;
        uint32_t apSimNodeId = apEntry.first;

        // Set AP node IDs
        ap.simNodeId = apSimNodeId;
        auto apNodeIdIt = g_apSimNodeIdToConfigNodeId.find(apSimNodeId);
        ap.nodeId = (apNodeIdIt != g_apSimNodeIdToConfigNodeId.end()) ? apNodeIdIt->second : apSimNodeId;

        // Update AP position from mobility model
        Ptr<Node> apNode = NodeList::GetNode(apSimNodeId);
        if (apNode) {
            Ptr<MobilityModel> mobility = apNode->GetObject<MobilityModel>();
            if (mobility) {
                Vector pos = mobility->GetPosition();
                ap.positionX = pos.x;
                ap.positionY = pos.y;
                ap.positionZ = pos.z;
            }
        }

        // Update connection metrics using O(1) cache lookup instead of O(N) NodeList iteration
        for (auto& connEntry : ap.connectionMetrics) {
            ConnectionMetrics& conn = connEntry.second;

            // Use cached MAC-to-Node mapping for O(1) lookup
            auto nodeIt = g_macToNode.find(conn.staAddress);
            if (nodeIt != g_macToNode.end()) {
                Ptr<Node> node = nodeIt->second;
                uint32_t simNodeId = node->GetId();
                conn.simNodeId = simNodeId;

                // Look up config node ID
                auto it = g_simNodeIdToConfigNodeId.find(simNodeId);
                conn.nodeId = (it != g_simNodeIdToConfigNodeId.end()) ? it->second : simNodeId;

                // Get position from mobility model
                Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
                if (mobility) {
                    Vector pos = mobility->GetPosition();
                    conn.positionX = pos.x;
                    conn.positionY = pos.y;
                    conn.positionZ = pos.z;
                }
            }
        }
    }

    // Print and send metrics to Kafka (LLM-friendly key=value format)
    std::cout << "\n";
    auto wallClock = std::chrono::system_clock::now();
    auto wallTime = std::chrono::system_clock::to_time_t(wallClock);
    std::tm* tm = std::localtime(&wallTime);
    char wallStr[20];
    std::strftime(wallStr, sizeof(wallStr), "%H:%M:%S", tm);

    for (const auto& [nodeId, metrics] : g_apMetrics)
    {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "[AP] id=" << metrics.nodeId
                  << " bssid=" << metrics.bssid
                  << " sim=" << Simulator::Now().GetSeconds() << "s"
                  << " wall=" << wallStr
                  << " ch=" << +metrics.channel
                  << " band=" << (metrics.band == WifiPhyBand::WIFI_PHY_BAND_5GHZ ? "5GHz" : "2.4GHz")
                  << " util=" << (metrics.channelUtilization * 100) << "%"
                  << " clients=" << metrics.associatedClients
                  << " tp=" << metrics.throughputMbps << "Mbps"
                  << " tx=" << metrics.bytesSent
                  << " rx=" << metrics.bytesReceived
                  << std::endl;

        for (const auto& [connId, conn] : metrics.connectionMetrics)
        {
            std::string staMac = connId.substr(0, connId.find("->"));
            std::cout << "  [STA] mac=" << staMac
                      << " rtt=" << conn.meanRTTLatency << "ms"
                      << " jitter=" << conn.jitterMs << "ms"
                      << " ul=" << conn.uplinkThroughputMbps << "Mbps"
                      << " dl=" << conn.downlinkThroughputMbps << "Mbps"
                      << " loss=" << (conn.packetLossRate * 100.0) << "%"
                      << " rssi_ap=" << conn.apViewRSSI << "dBm"
                      << " rssi_sta=" << conn.staViewRSSI << "dBm"
                      << " snr_ap=" << conn.apViewSNR << "dB"
                      << " snr_sta=" << conn.staViewSNR << "dB"
                      << std::endl;
        }

        // Send to Kafka (only after 10 seconds simulation time)
        if (g_kafkaProducer && Simulator::Now().GetSeconds() >= 10.0)
        {
            g_kafkaProducer->UpdateApMetrics(metrics.bssid, metrics);
        }
    }

    // Schedule next collection
    Simulator::Schedule(Seconds(g_statsInterval), &CollectAndSendMetrics);
}

// ============================================================================
// CCA CHANNEL UTILIZATION CALLBACKS
// ============================================================================

/**
 * Channel utilization callback for operating radio - updates BSS Load IE
 * Called periodically by WifiCcaMonitor attached to AP's main WiFi device
 */
void OnChannelUtilization(uint32_t nodeId, double timestamp, double totalUtil,
                          double wifiUtil, double nonWifiUtil,
                          double txUtil, double rxUtil,
                          double wifiCcaUtil, double nonWifiCcaUtil,
                          double txTime, double rxTime,
                          double wifiCcaTime, double nonWifiCcaTime,
                          double idleTime, uint64_t bytesSent, uint64_t bytesReceived,
                          double throughput)
{
    auto it = g_apMetrics.find(nodeId);
    if (it != g_apMetrics.end())
    {
        ApMetrics& ap = it->second;
        ap.channelUtilization = totalUtil / 100.0;  // Store as 0-1 scale
        ap.wifiChannelUtilization = wifiUtil / 100.0;  // Store wifi component
        ap.nonWifiChannelUtilization = nonWifiUtil / 100.0;  // Store non-wifi component

        // Convert 0-100% to 0-255 scale for BSS Load IE (IEEE 802.11)
        uint8_t channelUtilScaled = static_cast<uint8_t>((totalUtil * 255.0) / 100.0);
        uint8_t wifiUtilScaled = static_cast<uint8_t>((wifiUtil * 255.0) / 100.0);
        uint8_t nonWifiUtilScaled = static_cast<uint8_t>((nonWifiUtil * 255.0) / 100.0);

        // Set BSS Load IE values via ApWifiMac static methods
        ApWifiMac::SetChannelUtilization(ap.bssid, channelUtilScaled);
        ApWifiMac::SetWifiChannelUtilization(ap.bssid, wifiUtilScaled);
        ApWifiMac::SetNonWifiChannelUtilization(ap.bssid, nonWifiUtilScaled);
    }
}

/**
 * Scanning radio channel utilization callback - tracks utilization per channel
 * Called periodically by WifiCcaMonitor attached to AP's scanning radio (DualPhySniffer)
 */
void OnScanningChannelUtilization(uint32_t nodeId, double timestamp, double totalUtil,
                                   double wifiUtil, double nonWifiUtil,
                                   double txUtil, double rxUtil,
                                   double wifiCcaUtil, double nonWifiCcaUtil,
                                   double txTime, double rxTime,
                                   double wifiCcaTime, double nonWifiCcaTime,
                                   double idleTime, uint64_t bytesSent, uint64_t bytesReceived,
                                   double throughput)
{
    auto it = g_apMetrics.find(nodeId);
    if (it != g_apMetrics.end())
    {
        Mac48Address bssid = it->second.bssid;
        auto deviceIt = g_apScanningDevices.find(bssid);
        if (deviceIt != g_apScanningDevices.end())
        {
            Ptr<WifiPhy> scanningPhy = deviceIt->second->GetPhy();
            uint8_t currentChannel = scanningPhy->GetChannelNumber();

            // Store utilization data for this channel
            ScanningChannelUtil& scanUtil = g_scanningChannelUtilization[bssid][currentChannel];
            scanUtil.totalUtil = totalUtil;
            scanUtil.wifiUtil = wifiUtil;
            scanUtil.nonWifiUtil = nonWifiUtil;
        }
    }
}

// ============================================================================
// RSSI-BASED BSS TM TRIGGERING
// ============================================================================

// Forward declarations
void TriggerBssTmForSta(uint32_t apIndex, Mac48Address staMac, Mac48Address apBssid);
void PeriodicRssiCheck();
void PeriodicMapCleanup();

// RSSI check interval (every 1 second)
const double RSSI_CHECK_INTERVAL = 1.0;

/**
 * Periodic RSSI check for all APs and their associated STAs
 * Runs every RSSI_CHECK_INTERVAL seconds
 * Triggers BSS TM when RSSI drops below threshold
 */
void PeriodicRssiCheck()
{
    // Iterate through all APs
    for (uint32_t apIndex = 0; apIndex < g_apWifiDevices.GetN(); apIndex++)
    {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(apIndex));
        if (!apDev) continue;

        Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDev->GetMac());
        if (!apMac) continue;

        Mac48Address apBssid = apMac->GetAddress();
        Ptr<WifiRemoteStationManager> stationManager = apDev->GetRemoteStationManager();

        // Get list of associated STAs (link 0 for single-link)
        const auto& staList = apMac->GetStaList(0);

        for (const auto& [aid, staMac] : staList)
        {
            // Get most recent RSSI for this STA
            auto rssiOpt = stationManager->GetMostRecentRssi(staMac);
            if (!rssiOpt.has_value()) continue;

            double rssi = static_cast<double>(rssiOpt.value());

            // Get or create RSSI tracker for this STA
            StaRssiTracker& tracker = g_apStaRssi[apBssid][staMac];

            // Apply EWMA smoothing
            if (!tracker.initialized)
            {
                tracker.ewmaRssi = rssi;
                tracker.initialized = true;
            }
            else
            {
                tracker.ewmaRssi = RSSI_EWMA_ALPHA * rssi +
                                  (1.0 - RSSI_EWMA_ALPHA) * tracker.ewmaRssi;
            }
            tracker.currentRssi = rssi;
            tracker.lastUpdate = Simulator::Now();

            // Check threshold
            if (tracker.ewmaRssi < g_rssiThreshold)
            {
                // Check cooldown (periodic re-check with rate limiting)
                auto key = std::make_pair(apBssid, staMac);
                auto cooldownIt = g_rssiTriggerCooldown.find(key);
                if (cooldownIt != g_rssiTriggerCooldown.end())
                {
                    Time elapsed = Simulator::Now() - cooldownIt->second;
                    if (elapsed < RSSI_TRIGGER_COOLDOWN)
                    {
                        continue;  // Still in cooldown
                    }
                }

                // Update cooldown timestamp
                g_rssiTriggerCooldown[key] = Simulator::Now();

                std::cout << "\n[RSSI-ROAM] " << Simulator::Now().GetSeconds() << "s: "
                          << "STA " << staMac << " RSSI=" << std::fixed << std::setprecision(1)
                          << tracker.ewmaRssi << " dBm < threshold=" << g_rssiThreshold
                          << " dBm on AP " << apBssid << std::endl;

                // Stagger BSS TM triggers with random delay (0-100ms)
                // This prevents thundering herd where all STAs roam to same AP
                Ptr<UniformRandomVariable> randDelay = CreateObject<UniformRandomVariable>();
                randDelay->SetAttribute("Min", DoubleValue(0.0));
                randDelay->SetAttribute("Max", DoubleValue(0.1));
                double delaySeconds = randDelay->GetValue();

                std::cout << "[RSSI-ROAM]   └─ Scheduling BSS TM in " << std::fixed
                          << std::setprecision(2) << delaySeconds << "s" << std::endl;

                Simulator::Schedule(Seconds(delaySeconds), &TriggerBssTmForSta,
                                    apIndex, staMac, apBssid);
            }
        }
    }

    // Schedule next check
    Simulator::Schedule(Seconds(RSSI_CHECK_INTERVAL), &PeriodicRssiCheck);
}

/**
 * Periodic cleanup of global maps to prevent unbounded growth
 * Runs every 60 seconds
 */
void PeriodicMapCleanup()
{
    Time now = Simulator::Now();
    Time maxAge = Minutes(2);  // Expire entries older than 2 minutes

    // Cleanup g_rssiTriggerCooldown - remove expired cooldown entries
    for (auto it = g_rssiTriggerCooldown.begin(); it != g_rssiTriggerCooldown.end(); )
    {
        if (now - it->second > maxAge)
        {
            it = g_rssiTriggerCooldown.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Cleanup g_apStaRssi - remove entries for STAs not associated with any AP
    // Build set of currently associated STAs across all APs
    std::set<Mac48Address> associatedStas;
    for (uint32_t apIndex = 0; apIndex < g_apWifiDevices.GetN(); apIndex++)
    {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(apIndex));
        if (!apDev) continue;

        Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDev->GetMac());
        if (!apMac) continue;

        const auto& staList = apMac->GetStaList(0);
        for (const auto& [aid, staMac] : staList)
        {
            associatedStas.insert(staMac);
        }
    }

    // Prune g_apStaRssi for STAs no longer associated with ANY AP
    for (auto& [apBssid, staMap] : g_apStaRssi)
    {
        for (auto it = staMap.begin(); it != staMap.end(); )
        {
            // Remove if STA not associated anywhere AND entry is stale
            if (associatedStas.find(it->first) == associatedStas.end() &&
                now - it->second.lastUpdate > maxAge)
            {
                it = staMap.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Schedule next cleanup
    Simulator::Schedule(Seconds(60), &PeriodicMapCleanup);
}

/**
 * Trigger BSS TM Request for a STA that dropped below RSSI threshold
 * Checks if better candidates exist before sending
 */
void TriggerBssTmForSta(uint32_t apIndex, Mac48Address staMac, Mac48Address apBssid)
{
    if (apIndex >= g_apBssTmHelpers.size()) return;

    // Find STA device and get position
    Ptr<WifiNetDevice> staDevice;
    for (uint32_t i = 0; i < g_staWifiDevices.GetN(); i++)
    {
        Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice>(g_staWifiDevices.Get(i));
        if (dev && dev->GetMac()->GetAddress() == staMac)
        {
            staDevice = dev;
            break;
        }
    }
    if (!staDevice) return;

    Ptr<MobilityModel> staMobility = staDevice->GetNode()->GetObject<MobilityModel>();
    if (!staMobility) return;
    Vector staPos = staMobility->GetPosition();

    // Find closest AP by querying each AP's mobility model
    uint32_t closestApIndex = apIndex;
    double minDist = std::numeric_limits<double>::max();

    for (uint32_t i = 0; i < g_apWifiDevices.GetN(); i++)
    {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(i));
        if (!apDev) continue;

        Ptr<MobilityModel> apMobility = apDev->GetNode()->GetObject<MobilityModel>();
        if (!apMobility) continue;

        Vector apPos = apMobility->GetPosition();
        double dist = CalculateDistance(staPos, apPos);
        if (dist < minDist)
        {
            minDist = dist;
            closestApIndex = i;
        }
    }

    // Skip if closest AP is current AP
    if (closestApIndex == apIndex)
    {
        std::cout << "[RSSI-ROAM]   └─ SKIPPED: Closest AP is current AP" << std::endl;
        return;
    }

    // Get target AP info
    Ptr<WifiNetDevice> targetApDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(closestApIndex));
    if (!targetApDev) return;
    Mac48Address targetBssid = targetApDev->GetMac()->GetAddress();
    uint8_t targetChannel = targetApDev->GetPhy()->GetChannelNumber();

    std::cout << "[RSSI-ROAM]   └─ SENDING BSS TM to closest AP " << targetBssid
              << " (ch " << +targetChannel << ", dist=" << minDist << "m)" << std::endl;

    // Create single beacon entry for target AP
    std::vector<BeaconInfo> beacons;
    BeaconInfo targetBeacon{};
    targetBeacon.bssid = targetBssid;
    targetBeacon.channel = targetChannel;
    targetBeacon.rssi = 0;
    Ptr<ApWifiMac> targetApMac = DynamicCast<ApWifiMac>(targetApDev->GetMac());
    targetBeacon.staCount = targetApMac ? targetApMac->GetStaList(0).size() : 0;
    beacons.push_back(targetBeacon);

    Ptr<BssTm11vHelper> helper = g_apBssTmHelpers[apIndex];
    Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(apIndex));

    // Configure helper for this STA's beacon cache
    helper->SetStaMac(staMac);

    // Pass beacons with real-time STA counts to helper
    helper->SetBeaconCache(beacons);

    // Use empty beacon reports - sendRankedCandidates will use injected beacon cache
    std::vector<BeaconReportData> emptyReports;

    // Record BSS TM Request Sent event
    if (g_simEventProducer)
    {
        uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(apIndex)
                                      ? g_apSimNodeIdToConfigNodeId[apIndex]
                                      : apIndex;

        // Find STA config node ID from MAC address
        uint32_t staConfigNodeId = 0;
        for (uint32_t i = 0; i < g_staWifiDevices.GetN(); i++)
        {
            Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(g_staWifiDevices.Get(i));
            if (staDevice && staDevice->GetMac()->GetAddress() == staMac)
            {
                staConfigNodeId = g_simNodeIdToConfigNodeId.count(i)
                                      ? g_simNodeIdToConfigNodeId[i]
                                      : i;
                break;
            }
        }

        // Get target BSSID string
        std::ostringstream oss;
        oss << targetBssid;
        std::string targetBssidStr = oss.str();

        // Get current RSSI from tracker
        double rssiDbm = 0.0;
        auto apTrackerIt = g_apStaRssi.find(apBssid);
        if (apTrackerIt != g_apStaRssi.end())
        {
            auto staTrackerIt = apTrackerIt->second.find(staMac);
            if (staTrackerIt != apTrackerIt->second.end())
            {
                rssiDbm = staTrackerIt->second.ewmaRssi;
            }
        }

        g_simEventProducer->RecordBssTmRequestSent(apConfigNodeId, staConfigNodeId,
                                                    "LOW_RSSI", rssiDbm, targetBssidStr);
    }

    // Schedule the BSS TM request (allows current callback to complete)
    Simulator::ScheduleNow(&BssTm11vHelper::sendRankedCandidates,
                           helper,
                           apDevice,
                           apBssid,
                           staMac,
                           emptyReports);
}

// ============================================================================
// KAFKA CONSUMER CALLBACK - APPLIES OPTIMIZATION PARAMETERS DIRECTLY TO PHY
// ============================================================================

/**
 * Callback for Kafka consumer - applies optimization parameters from external optimizer
 * Directly modifies WiFi PHY parameters (TX power, CCA, RX sensitivity, channel)
 */
void OnParametersReceived(std::string bssidStr, ApParameters params)
{
    Mac48Address bssid(bssidStr.c_str());

    // Find AP device by BSSID and get node index
    Ptr<WifiNetDevice> apDevice = nullptr;
    uint32_t apIndex = 0;
    for (uint32_t i = 0; i < g_apWifiDevices.GetN(); i++)
    {
        Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(i));
        if (dev && dev->GetMac()->GetAddress() == bssid)
        {
            apDevice = dev;
            apIndex = i;
            break;
        }
    }

    if (!apDevice)
    {
        NS_LOG_WARN("OnParametersReceived: Unknown BSSID " << bssid);
        return;
    }

    // Get current channel and power before applying changes
    Ptr<WifiPhy> phy = apDevice->GetPhy();
    uint8_t oldChannel = phy ? phy->GetChannelNumber() : 0;
    double oldPower = phy ? phy->GetTxPowerStart() : 0.0;

    // Apply PHY parameters directly
    if (phy)
    {
        phy->SetTxPowerStart(dBm_u{params.txPowerStartDbm});
        phy->SetTxPowerEnd(dBm_u{params.txPowerStartDbm});
        phy->SetCcaEdThreshold(dBm_u{params.ccaEdThresholdDbm});
        phy->SetRxSensitivity(dBm_u{params.rxSensitivityDbm});
    }

    NS_LOG_INFO("Applied params to " << bssid
                << ": TxPower=" << params.txPowerStartDbm
                << " CCA=" << params.ccaEdThresholdDbm
                << " RxSens=" << params.rxSensitivityDbm);

    // Record config received event
    if (g_simEventProducer)
    {
        uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(apIndex)
                                      ? g_apSimNodeIdToConfigNodeId[apIndex]
                                      : apIndex;
        g_simEventProducer->RecordConfigReceived(apConfigNodeId, "KAFKA_PARAMS");

        // Record power switch event if power changed
        if (std::abs(oldPower - params.txPowerStartDbm) > 0.01)
        {
            g_simEventProducer->RecordPowerSwitch(apConfigNodeId, oldPower, params.txPowerStartDbm);
        }
    }

    // Deferred channel switch (100ms delay to avoid PHY state conflicts)
    if (params.channelNumber > 0)
    {
        uint8_t newChannel = params.channelNumber;
        Ptr<WifiNetDevice> capturedDevice = apDevice;

        // Record channel switch event (before the actual switch)
        if (g_simEventProducer && oldChannel != newChannel)
        {
            uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(apIndex)
                                          ? g_apSimNodeIdToConfigNodeId[apIndex]
                                          : apIndex;
            g_simEventProducer->RecordChannelSwitch(apConfigNodeId, oldChannel, newChannel);
        }

        Simulator::Schedule(MilliSeconds(100), [capturedDevice, newChannel, bssid]() {
            Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(capturedDevice->GetMac());
            if (apMac)
            {
                // Enable coordinated channel switch to preserve client associations
                // This prevents the AP from clearing its staList and STAs from disassociating
                apMac->SetCoordinatedChannelSwitch(true);

                apMac->SwitchChannel(newChannel);
                NS_LOG_INFO("Channel switched to " << (uint32_t)newChannel << " for " << bssid);

                // Reset flag after 500ms to ensure all clients complete the transition
                Simulator::Schedule(MilliSeconds(500), [apMac]() {
                    apMac->SetCoordinatedChannelSwitch(false);
                });
            }
        });
    }
}

// ============================================================================
// POWER SCORING CALLBACKS
// ============================================================================

/**
 * HE-SIG-A callback for receiving RSSI and BSS Color from AP's perspective
 */
void OnApHeSigA(uint32_t nodeId, HeSigAParameters params)
{
    g_powerScoringHelper.ProcessHeSigA(nodeId, params.rssi, params.bssColor);
}

/**
 * PHY RX Payload Begin callback for receiving actual MCS from AP's perspective
 */
void OnApPhyRxPayloadBegin(uint32_t nodeId, WifiTxVector txVector, Time psduDuration)
{
    if (txVector.GetModulationClass() == WIFI_MOD_CLASS_HE)
    {
        uint8_t mcs = txVector.GetMode().GetMcsValue();
        g_powerScoringHelper.UpdateMcs(nodeId, mcs);
    }
}

/**
 * Get non-WiFi percentage for an AP from its metrics
 */
double GetNonWifiForAp(uint32_t nodeId)
{
    auto it = g_apMetrics.find(nodeId);
    if (it == g_apMetrics.end())
    {
        return 0.0;
    }
    return it->second.nonWifiChannelUtilization;
}

/**
 * Periodic power scoring callback - calculates and applies TX power for all APs
 */
void PerformPowerScoring()
{
    if (!g_powerScoringEnabled)
    {
        Simulator::Schedule(Seconds(g_powerScoringConfig.updateIntervalSec), &PerformPowerScoring);
        return;
    }

    std::cout << "\n========== POWER SCORING (t=" << Simulator::Now().GetSeconds() << "s) ==========" << std::endl;

    for (const auto& [nodeId, apMetrics] : g_apMetrics)
    {
        auto& state = g_powerScoringHelper.GetOrCreateApState(nodeId, apMetrics.bssColor);
        double nonWifiPercent = GetNonWifiForAp(nodeId);
        double prevPower = state.currentTxPowerDbm;

        std::cout << "\n--- AP Node " << nodeId << " ---" << std::endl;
        std::cout << "  Inputs: BSS_RSSI=" << state.tracker.bssRssiEwma
                  << "dBm, MCS=" << state.tracker.mcsEwma
                  << ", NonWifi=" << nonWifiPercent << "%" << std::endl;

        PowerResult result = g_powerScoringHelper.CalculatePower(nodeId, nonWifiPercent);

        if (result.powerChanged)
        {
            std::cout << "  -> POWER CHANGE: " << prevPower << " -> " << result.txPowerDbm
                      << " dBm (" << result.reason << ")" << std::endl;

            // Apply TX power directly to PHY
            Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(nodeId));
            if (apDev)
            {
                Ptr<WifiPhy> phy = apDev->GetPhy();
                if (phy)
                {
                    phy->SetTxPowerStart(dBm_u{result.txPowerDbm});
                    phy->SetTxPowerEnd(dBm_u{result.txPowerDbm});
                }
            }
            auto metricsIt = g_apMetrics.find(nodeId);
            if (metricsIt != g_apMetrics.end())
            {
                metricsIt->second.txPowerDbm = result.txPowerDbm;
                metricsIt->second.obsspdLevelDbm = result.obsspdLevelDbm;
            }
        }
        else
        {
            std::cout << "  -> No change (TxPower=" << result.txPowerDbm << "dBm)" << std::endl;
        }
    }

    Simulator::Schedule(Seconds(g_powerScoringConfig.updateIntervalSec), &PerformPowerScoring);
}

SimulationConfig
LoadConfig(const std::string& filename)
{
    SimulationConfig config;

    FILE* fp = fopen(filename.c_str(), "r");
    if (!fp)
    {
        NS_FATAL_ERROR("Cannot open config file: " << filename);
    }

    char readBuffer[65536];
    FileReadStream is(fp, readBuffer, sizeof(readBuffer));

    Document doc;
    doc.ParseStream(is);
    fclose(fp);

    if (doc.HasParseError())
    {
        NS_FATAL_ERROR("JSON parse error: " << GetParseError_En(doc.GetParseError())
                                            << " at offset " << doc.GetErrorOffset());
    }

    config.simulationTime =
        doc.HasMember("simulationTime") ? doc["simulationTime"].GetDouble() : 100.0;

    if (doc.HasMember("aps") && doc["aps"].IsArray())
    {
        const Value& apsArray = doc["aps"];
        for (SizeType i = 0; i < apsArray.Size(); i++)
        {
            const Value& ap = apsArray[i];
            ApConfig apConfig;
            apConfig.nodeId = ap["nodeId"].GetUint();
            apConfig.x = ap["position"]["x"].GetDouble();
            apConfig.y = ap["position"]["y"].GetDouble();
            apConfig.z = ap["position"]["z"].GetDouble();
            apConfig.txPower = ap["leverConfig"]["txPower"].GetDouble();
            // Parse CCA threshold with default fallback
            apConfig.ccaThreshold = ap["leverConfig"].HasMember("ccaThreshold")
                ? ap["leverConfig"]["ccaThreshold"].GetDouble() : -82.0;
            // Parse RX sensitivity with default fallback
            apConfig.rxSensitivity = ap["leverConfig"].HasMember("rxSensitivity")
                ? ap["leverConfig"]["rxSensitivity"].GetDouble() : -93.0;
            apConfig.channel = static_cast<uint16_t>(ap["leverConfig"]["channel"].GetUint());
            config.aps.push_back(apConfig);
        }
    }

    if (doc.HasMember("waypoints") && doc["waypoints"].IsArray())
    {
        const Value& wpArray = doc["waypoints"];
        for (SizeType i = 0; i < wpArray.Size(); i++)
        {
            const Value& wp = wpArray[i];
            WaypointConfig wpConfig;
            wpConfig.id = wp["id"].GetUint();
            wpConfig.x = wp["x"].GetDouble();
            wpConfig.y = wp["y"].GetDouble();
            wpConfig.z = wp["z"].GetDouble();
            config.waypoints.push_back(wpConfig);
        }
    }

    if (doc.HasMember("stas") && doc["stas"].IsArray())
    {
        const Value& stasArray = doc["stas"];
        for (SizeType i = 0; i < stasArray.Size(); i++)
        {
            const Value& sta = stasArray[i];
            StaConfig staConfig;
            staConfig.nodeId = sta["nodeId"].GetUint();
            staConfig.initialWaypointId = sta["initialWaypointId"].GetUint();
            staConfig.waypointSwitchTimeMin = sta["waypointSwitchTimeMin"].GetDouble();
            staConfig.waypointSwitchTimeMax = sta["waypointSwitchTimeMax"].GetDouble();
            staConfig.transferVelocityMin = sta["transferVelocityMin"].GetDouble();
            staConfig.transferVelocityMax = sta["transferVelocityMax"].GetDouble();
            config.stas.push_back(staConfig);
        }
    }

    // Parse BSS orchestration RSSI threshold from system_config
    config.bssOrchestrationRssiThreshold = -70.0;  // Default
    if (doc.HasMember("system_config") && doc["system_config"].IsObject())
    {
        const Value& sysConfig = doc["system_config"];
        if (sysConfig.HasMember("bss_orchestration_rssi_threshold"))
        {
            config.bssOrchestrationRssiThreshold =
                sysConfig["bss_orchestration_rssi_threshold"].GetDouble();
        }

        // Parse scanning channels for populating empty channel data
        if (sysConfig.HasMember("scanning_channels") && sysConfig["scanning_channels"].IsArray())
        {
            const Value& scanChannelsArray = sysConfig["scanning_channels"];
            g_scanningChannels.clear();
            for (SizeType i = 0; i < scanChannelsArray.Size(); i++)
            {
                g_scanningChannels.push_back(static_cast<uint8_t>(scanChannelsArray[i].GetUint()));
            }
            NS_LOG_INFO("Loaded " << g_scanningChannels.size() << " scanning channels from config");
        }
    }

    // Parse ACI configuration
    if (doc.HasMember("aci") && doc["aci"].IsObject())
    {
        const Value& a = doc["aci"];
        config.aci.enabled = a.HasMember("enabled") && a["enabled"].GetBool();
        if (a.HasMember("pathLossExponent"))
            config.aci.pathLossExponent = a["pathLossExponent"].GetDouble();
        if (a.HasMember("maxInterferenceDistanceM"))
            config.aci.maxInterferenceDistanceM = a["maxInterferenceDistanceM"].GetDouble();
        if (a.HasMember("clientWeightFactor"))
            config.aci.clientWeightFactor = a["clientWeightFactor"].GetDouble();
        if (a.HasMember("degradation") && a["degradation"].IsObject())
        {
            const Value& d = a["degradation"];
            if (d.HasMember("throughputFactor"))
                config.aci.throughputFactor = d["throughputFactor"].GetDouble();
            if (d.HasMember("packetLossFactor"))
                config.aci.packetLossFactor = d["packetLossFactor"].GetDouble();
            if (d.HasMember("latencyFactor"))
                config.aci.latencyFactor = d["latencyFactor"].GetDouble();
            if (d.HasMember("jitterFactor"))
                config.aci.jitterFactor = d["jitterFactor"].GetDouble();
            if (d.HasMember("channelUtilFactor"))
                config.aci.channelUtilFactor = d["channelUtilFactor"].GetDouble();
        }
    }

    // Parse OFDMA configuration
    if (doc.HasMember("ofdma") && doc["ofdma"].IsObject())
    {
        const Value& o = doc["ofdma"];
        config.ofdma.enabled = o.HasMember("enabled") && o["enabled"].GetBool();
        if (o.HasMember("minStasForBenefit"))
            config.ofdma.minStasForBenefit = static_cast<uint8_t>(o["minStasForBenefit"].GetUint());
        if (o.HasMember("saturationStaCount"))
            config.ofdma.saturationStaCount = static_cast<uint8_t>(o["saturationStaCount"].GetUint());
        if (o.HasMember("improvement") && o["improvement"].IsObject())
        {
            const Value& imp = o["improvement"];
            if (imp.HasMember("throughputFactor"))
                config.ofdma.throughputFactor = imp["throughputFactor"].GetDouble();
            if (imp.HasMember("latencyFactor"))
                config.ofdma.latencyFactor = imp["latencyFactor"].GetDouble();
            if (imp.HasMember("jitterFactor"))
                config.ofdma.jitterFactor = imp["jitterFactor"].GetDouble();
            if (imp.HasMember("packetLossFactor"))
                config.ofdma.packetLossFactor = imp["packetLossFactor"].GetDouble();
            if (imp.HasMember("channelUtilFactor"))
                config.ofdma.channelUtilFactor = imp["channelUtilFactor"].GetDouble();
        }
    }

    // Parse channelScoring configuration
    if (doc.HasMember("channelScoring") && doc["channelScoring"].IsObject())
    {
        const Value& cs = doc["channelScoring"];
        config.channelScoring.enabled = cs.HasMember("enabled")
            ? cs["enabled"].GetBool() : true;

        if (cs.HasMember("weights") && cs["weights"].IsObject())
        {
            const Value& w = cs["weights"];
            config.channelScoring.weightBssid = w.HasMember("bssid")
                ? w["bssid"].GetDouble() : 0.5;
            config.channelScoring.weightRssi = w.HasMember("rssi")
                ? w["rssi"].GetDouble() : 0.5;
            config.channelScoring.weightNonWifi = w.HasMember("nonWifi")
                ? w["nonWifi"].GetDouble() : 0.5;
            config.channelScoring.weightOverlap = w.HasMember("overlap")
                ? w["overlap"].GetDouble() : 0.5;
        }

        config.channelScoring.nonWifiDiscardThreshold = cs.HasMember("nonWifiDiscardThreshold")
            ? cs["nonWifiDiscardThreshold"].GetDouble() : 40.0;
        config.channelScoring.interval = cs.HasMember("interval")
            ? cs["interval"].GetDouble() : 10.0;
    }

    // Parse powerScoring configuration
    if (doc.HasMember("powerScoring") && doc["powerScoring"].IsObject())
    {
        const Value& ps = doc["powerScoring"];

        if (ps.HasMember("enabled"))
            config.powerScoring.enabled = ps["enabled"].GetBool();
        if (ps.HasMember("updateInterval"))
            config.powerScoring.updateInterval = ps["updateInterval"].GetDouble();
        if (ps.HasMember("margin"))
            config.powerScoring.margin = ps["margin"].GetDouble();
        if (ps.HasMember("gamma"))
            config.powerScoring.gamma = ps["gamma"].GetDouble();
        if (ps.HasMember("alpha"))
            config.powerScoring.alpha = ps["alpha"].GetDouble();
        if (ps.HasMember("ofcThreshold"))
            config.powerScoring.ofcThreshold = ps["ofcThreshold"].GetDouble();
        if (ps.HasMember("obsspdMinDbm"))
            config.powerScoring.obsspdMinDbm = ps["obsspdMinDbm"].GetDouble();
        if (ps.HasMember("obsspdMaxDbm"))
            config.powerScoring.obsspdMaxDbm = ps["obsspdMaxDbm"].GetDouble();
        if (ps.HasMember("txPowerRefDbm"))
            config.powerScoring.txPowerRefDbm = ps["txPowerRefDbm"].GetDouble();
        if (ps.HasMember("txPowerMinDbm"))
            config.powerScoring.txPowerMinDbm = ps["txPowerMinDbm"].GetDouble();
        if (ps.HasMember("nonWifiThresholdPercent"))
            config.powerScoring.nonWifiThresholdPercent = ps["nonWifiThresholdPercent"].GetDouble();
        if (ps.HasMember("nonWifiHysteresis"))
            config.powerScoring.nonWifiHysteresis = ps["nonWifiHysteresis"].GetDouble();

        std::cout << "[Config] Power Scoring: " << (config.powerScoring.enabled ? "enabled" : "disabled")
                  << ", interval=" << config.powerScoring.updateInterval << "s" << std::endl;
    }

    // Parse staChannelHopping configuration
    if (doc.HasMember("staChannelHopping") && doc["staChannelHopping"].IsObject())
    {
        const Value& sch = doc["staChannelHopping"];

        if (sch.HasMember("enabled"))
            config.staChannelHopping.enabled = sch["enabled"].GetBool();

        std::cout << "[Config] STA Channel Hopping: " << (config.staChannelHopping.enabled ? "enabled" : "disabled") << std::endl;
    }

    // Parse loadBalancing configuration
    if (doc.HasMember("loadBalancing") && doc["loadBalancing"].IsObject())
    {
        const Value& lb = doc["loadBalancing"];

        if (lb.HasMember("enabled"))
            config.loadBalancing.enabled = lb["enabled"].GetBool();
        if (lb.HasMember("channelUtilThreshold"))
            config.loadBalancing.channelUtilThreshold = lb["channelUtilThreshold"].GetDouble();
        if (lb.HasMember("intervalSec"))
            config.loadBalancing.intervalSec = lb["intervalSec"].GetDouble();
        if (lb.HasMember("cooldownSec"))
            config.loadBalancing.cooldownSec = lb["cooldownSec"].GetDouble();

        std::cout << "[Config] Load Balancing: " << (config.loadBalancing.enabled ? "enabled" : "disabled")
                  << ", threshold=" << config.loadBalancing.channelUtilThreshold << "%"
                  << ", interval=" << config.loadBalancing.intervalSec << "s"
                  << ", cooldown=" << config.loadBalancing.cooldownSec << "s" << std::endl;
    }

    return config;
}

/**
 * Find the index of the closest AP to a given position
 */
uint32_t
FindClosestAp(const Vector& staPos, const std::vector<ApConfig>& aps)
{
    uint32_t closestIdx = 0;
    double minDist = std::numeric_limits<double>::max();
    for (uint32_t i = 0; i < aps.size(); i++)
    {
        double dx = staPos.x - aps[i].x;
        double dy = staPos.y - aps[i].y;
        double dz = staPos.z - aps[i].z;
        double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < minDist)
        {
            minDist = dist;
            closestIdx = i;
        }
    }
    return closestIdx;
}

int
main(int argc, char* argv[])
{
    // Use nanosecond time resolution (required for HE guard intervals)
    Time::SetResolution(Time::NS);
    // Note: Time::ClearMarkedTimes() is called internally by Simulator::Run()

    // SPEEDUP: Use MapScheduler for O(log n) event removal (HeapScheduler is O(n))
    ObjectFactory schedulerFactory;
    schedulerFactory.SetTypeId("ns3::MapScheduler");
    Simulator::SetScheduler(schedulerFactory);

    // Configure propagation models (chained: Range cutoff + LogDistance RSSI)
    // 2) LogDistancePropagationLossModel: Realistic RSSI for in-range packets
    Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss", DoubleValue(47.0));  // 47 dB at 1m (5 GHz)
    Config::SetDefault("ns3::LogDistancePropagationLossModel::Exponent", DoubleValue(3.0));  // Indoor

    // ===== FIX: ARP Cache Timeout Issue (ns-3 bug #2057) =====
    // Default AliveTimeout of 120s causes silent packet drops after ~300s
    // Set to 24 hours to ensure ARP entries never expire during simulation
    Config::SetDefault("ns3::ArpCache::AliveTimeout", TimeValue(Hours(24)));         // Never expire
    Config::SetDefault("ns3::ArpCache::DeadTimeout", TimeValue(Hours(24)));          // Never give up
    Config::SetDefault("ns3::ArpCache::WaitReplyTimeout", TimeValue(Seconds(5)));    // More patient ARP
    Config::SetDefault("ns3::ArpCache::PendingQueueSize", UintegerValue(1000));      // Handle burst traffic
    Config::SetDefault("ns3::ArpCache::MaxRetries", UintegerValue(50));              // Many retries before giving up

    std::string configFile = "config-simulation.json";
    bool verbose = false;
    std::string kafkaBroker = "localhost:9092";
    std::string kafkaTopic = "ns3-metrics";
    std::string simulationId = "basic-sim";
    double simTime = 0.0;  // 0 = use config file value

    // Kafka consumer settings (enabled by default when Kafka is enabled)
    std::string kafkaDownstreamTopic = "optimization-commands";
    std::string kafkaConsumerGroupId = "ns3-consumer";

    CommandLine cmd(__FILE__);
    cmd.AddValue("config", "Path to configuration JSON file", configFile);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.AddValue("simTime", "Simulation time in seconds (overrides config file)", simTime);
    cmd.AddValue("statsInterval", "Interval for printing flow stats (seconds)", g_statsInterval);
    cmd.AddValue("enableKafka", "Enable Kafka producer and consumer", g_enableKafka);
    cmd.AddValue("kafkaBroker", "Kafka broker address", kafkaBroker);
    cmd.AddValue("kafkaTopic", "Kafka topic name for metrics", kafkaTopic);
    cmd.AddValue("simulationId", "Simulation ID for Kafka", simulationId);
    cmd.AddValue("kafkaDownstreamTopic", "Kafka topic for receiving optimization commands", kafkaDownstreamTopic);
    cmd.AddValue("kafkaConsumerGroupId", "Kafka consumer group ID", kafkaConsumerGroupId);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("BasicSimulation", LOG_LEVEL_INFO);
    }

    // Load configuration
    SimulationConfig config = LoadConfig(configFile);

    // Override simulation time if specified on command line
    if (simTime > 0.0)
    {
        config.simulationTime = simTime;
    }

    uint32_t numAps = config.aps.size();
    uint32_t numStas = config.stas.size();

    // Create nodes
    NodeContainer apNodes;
    // Create AP nodes one by one and build sim node ID -> config nodeId mapping
    for (size_t i = 0; i < config.aps.size(); i++) {
        Ptr<Node> apNode = CreateObject<Node>();
        apNodes.Add(apNode);
        uint32_t simNodeId = apNode->GetId();
        uint32_t configNodeId = config.aps[i].nodeId;
        g_apSimNodeIdToConfigNodeId[simNodeId] = configNodeId;
    }

    NodeContainer staNodes;
    // Create STA nodes one by one and build sim node ID -> config nodeId mapping
    for (size_t i = 0; i < config.stas.size(); i++) {
        Ptr<Node> staNode = CreateObject<Node>();
        staNodes.Add(staNode);
        uint32_t simNodeId = staNode->GetId();
        uint32_t configNodeId = config.stas[i].nodeId;
        g_simNodeIdToConfigNodeId[simNodeId] = configNodeId;
    }

    NodeContainer dsNode;
    dsNode.Create(1);

    // Setup mobility for APs (static positions)
    MobilityHelper apMobility;
    apMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    apMobility.Install(apNodes);

    for (uint32_t i = 0; i < numAps; i++)
    {
        Ptr<ConstantPositionMobilityModel> mob =
            apNodes.Get(i)->GetObject<ConstantPositionMobilityModel>();
        mob->SetPosition(Vector(config.aps[i].x, config.aps[i].y, config.aps[i].z));
    }

    // Setup mobility for STAs (waypoint mobility)
    MobilityHelper staMobility;
    staMobility.SetMobilityModel("ns3::WaypointMobilityModel");
    staMobility.Install(staNodes);

    Ptr<UniformRandomVariable> switchTimeRv = CreateObject<UniformRandomVariable>();
    Ptr<UniformRandomVariable> velocityRv = CreateObject<UniformRandomVariable>();

    for (uint32_t i = 0; i < numStas; i++)
    {
        Ptr<WaypointMobilityModel> mob = staNodes.Get(i)->GetObject<WaypointMobilityModel>();
        uint32_t initWpId = config.stas[i].initialWaypointId;
        Vector currentPos(config.waypoints[initWpId].x,
                          config.waypoints[initWpId].y,
                          config.waypoints[initWpId].z);

        switchTimeRv->SetAttribute("Min", DoubleValue(config.stas[i].waypointSwitchTimeMin));
        switchTimeRv->SetAttribute("Max", DoubleValue(config.stas[i].waypointSwitchTimeMax));
        velocityRv->SetAttribute("Min", DoubleValue(config.stas[i].transferVelocityMin));
        velocityRv->SetAttribute("Max", DoubleValue(config.stas[i].transferVelocityMax));

        double currentTime = 0.0;
        mob->AddWaypoint(Waypoint(Seconds(currentTime), currentPos));

        // Add initial wait time before first movement (random between min and max)
        double initialWaitTime = switchTimeRv->GetValue();
        currentTime += initialWaitTime;
        mob->AddWaypoint(Waypoint(Seconds(currentTime), currentPos));  // Stay at initial position

        uint32_t wpIndex = initWpId;
        while (currentTime < config.simulationTime)
        {
            wpIndex = (wpIndex + 1) % config.waypoints.size();
            Vector nextPos(config.waypoints[wpIndex].x,
                           config.waypoints[wpIndex].y,
                           config.waypoints[wpIndex].z);

            double distance = CalculateDistance(currentPos, nextPos);
            double velocity = velocityRv->GetValue();
            double travelTime = distance / velocity;

            currentTime += travelTime;
            mob->AddWaypoint(Waypoint(Seconds(currentTime), nextPos));
            currentPos = nextPos;

            // Wait at waypoint before moving to next
            double waitTime = switchTimeRv->GetValue();
            currentTime += waitTime;
            mob->AddWaypoint(Waypoint(Seconds(currentTime), currentPos));
        }
    }

    // Setup DS node mobility
    MobilityHelper dsMobility;
    dsMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    dsMobility.Install(dsNode);
    Ptr<ConstantPositionMobilityModel> dsMob =
        dsNode.Get(0)->GetObject<ConstantPositionMobilityModel>();
    double dsX = 0, dsY = 0, dsZ = 0;
    for (const auto& ap : config.aps)
    {
        dsX += ap.x;
        dsY += ap.y;
        dsZ += ap.z;
    }
    dsMob->SetPosition(Vector(dsX / numAps, dsY / numAps, dsZ / numAps));

    // Create WiFi channel with chained propagation models:
    // 1) Range cutoff (drops packets beyond MaxRange - configured above)
    // 2) LogDistance for realistic RSSI within range
    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::RangePropagationLossModel");  // Hard cutoff first
    wifiChannel.AddPropagationLoss("ns3::LogDistancePropagationLossModel");  // Realistic RSSI
    Ptr<YansWifiChannel> channel = wifiChannel.Create();

    // Setup WiFi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue("HeMcs2"),
                               "ControlMode", StringValue("HeMcs0"));

    // Increase WiFi MAC queue size from default 500p to 10000p
    // Helps prevent queue overflow under heavy contention (20 STAs × 17Mbps)
    // Limitations: Increases bufferbloat (latency), doesn't fix overload
    Config::SetDefault("ns3::WifiMacQueue::MaxSize", QueueSizeValue(QueueSize("10000p")));

    // ===== Queue Delay Tolerance =====
    // Use default 500ms - longer delays increase expiry processing overhead
    Config::SetDefault("ns3::WifiMacQueue::MaxDelay", TimeValue(MilliSeconds(500)));

    // ===== FIX: Beacon Watchdog Tolerance =====
    // Increase tolerance for missed beacons to prevent spurious disassociations
    // Default is 10 missed beacons -> increased to 100 for long simulations
    // Config::SetDefault("ns3::StaWifiMac::MaxMissedBeacons", UintegerValue(100));

    YansWifiPhyHelper wifiPhy;
    wifiPhy.SetChannel(channel);

    WifiMacHelper wifiMac;
    Ssid ssid = Ssid("BasicSimNetwork");

    // Install AP devices and initialize ApMetrics
    NetDeviceContainer apWifiDevices;
    for (uint32_t i = 0; i < numAps; i++)
    {
        wifiPhy.Set("TxPowerStart", DoubleValue(config.aps[i].txPower));
        wifiPhy.Set("TxPowerEnd", DoubleValue(config.aps[i].txPower));
        wifiPhy.Set(
            "ChannelSettings",
            StringValue("{" + std::to_string(config.aps[i].channel) + ", 0, BAND_5GHZ, 0}"));

        wifiMac.SetType("ns3::ApWifiMac",
                        "Ssid", SsidValue(ssid),
                        "BE_MaxAmpduSize", UintegerValue(6500631));  // HE maximum for better aggregation

        NetDeviceContainer apDev = wifi.Install(wifiPhy, wifiMac, apNodes.Get(i));
        apWifiDevices.Add(apDev);

        // Initialize ApMetrics
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(apDev.Get(0));
        ApMetrics apMetrics;
        apMetrics.nodeId = i;
        apMetrics.bssid = wifiDev->GetMac()->GetAddress();
        apMetrics.channel = config.aps[i].channel;
        apMetrics.band = WIFI_PHY_BAND_5GHZ;
        apMetrics.channelUtilization = 0.0;
        apMetrics.associatedClients = 0;
        apMetrics.bytesSent = 0;
        apMetrics.bytesReceived = 0;
        apMetrics.throughputMbps = 0.0;
        g_apMetrics[i] = apMetrics;
    }

    // ===== Initialize ACI Simulation =====
    if (config.aci.enabled)
    {
        AciConfig aciCfg;
        aciCfg.enabled = true;
        aciCfg.pathLossExponent = config.aci.pathLossExponent;
        aciCfg.maxInterferenceDistanceM = config.aci.maxInterferenceDistanceM;
        aciCfg.clientWeightFactor = config.aci.clientWeightFactor;
        aciCfg.degradation.throughput = config.aci.throughputFactor;
        aciCfg.degradation.packetLoss = config.aci.packetLossFactor;
        aciCfg.degradation.latency = config.aci.latencyFactor;
        aciCfg.degradation.jitter = config.aci.jitterFactor;
        aciCfg.degradation.channelUtil = config.aci.channelUtilFactor;
        AciSimulation::SetConfig(aciCfg);

        std::vector<ApConfigData> apConfigs;
        for (uint32_t i = 0; i < numAps; i++)
        {
            ApConfigData ap;
            ap.nodeId = i;
            ap.position = Vector(config.aps[i].x, config.aps[i].y, config.aps[i].z);
            ap.txPower = config.aps[i].txPower;
            ap.channel = static_cast<uint8_t>(config.aps[i].channel);
            apConfigs.push_back(ap);
        }
        AciSimulation::Initialize(apConfigs, g_apMetrics);
        std::cout << "[ACI] Adjacent Channel Interference simulation ENABLED" << std::endl;
    }

    // ===== Initialize OFDMA Simulation =====
    if (config.ofdma.enabled)
    {
        OfdmaConfig ofdmaCfg;
        ofdmaCfg.enabled = true;
        ofdmaCfg.minStasForBenefit = config.ofdma.minStasForBenefit;
        ofdmaCfg.saturationStaCount = config.ofdma.saturationStaCount;
        ofdmaCfg.improvement.throughput = config.ofdma.throughputFactor;
        ofdmaCfg.improvement.latency = config.ofdma.latencyFactor;
        ofdmaCfg.improvement.jitter = config.ofdma.jitterFactor;
        ofdmaCfg.improvement.packetLoss = config.ofdma.packetLossFactor;
        ofdmaCfg.improvement.channelUtil = config.ofdma.channelUtilFactor;
        OfdmaSimulation::SetConfig(ofdmaCfg);
        std::cout << "[OFDMA] Effects simulation ENABLED" << std::endl;
    }

    // ===== Initialize Channel Scoring Helper =====
    if (config.channelScoring.enabled)
    {
        g_channelScoringEnabled = true;
        g_channelScoringInterval = config.channelScoring.interval;

        g_channelScoringConfig.weightBssid = config.channelScoring.weightBssid;
        g_channelScoringConfig.weightRssi = config.channelScoring.weightRssi;
        g_channelScoringConfig.weightNonWifi = config.channelScoring.weightNonWifi;
        g_channelScoringConfig.weightOverlap = config.channelScoring.weightOverlap;
        g_channelScoringConfig.nonWifiDiscardThreshold = config.channelScoring.nonWifiDiscardThreshold;
        g_channelScoringHelper.SetConfig(g_channelScoringConfig);

        std::cout << "[CHANNEL SCORING] Enabled with interval=" << g_channelScoringInterval << "s" << std::endl;
    }
    else
    {
        g_channelScoringEnabled = false;
    }

    // ===== Install CCA Monitors on Operating Radios (for BSS Load IE) =====
    WifiCcaMonitorHelper ccaHelper;
    ccaHelper.SetWindowSize(MilliSeconds(200));
    ccaHelper.SetUpdateInterval(MilliSeconds(200));

    for (uint32_t i = 0; i < apWifiDevices.GetN(); i++)
    {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(apWifiDevices.Get(i));
        Ptr<WifiCcaMonitor> monitor = ccaHelper.Install(apDev);
        monitor->TraceConnectWithoutContext("ChannelUtilization",
            MakeCallback(&OnChannelUtilization));
        monitor->Start();
        g_ccaMonitors.push_back(monitor);
    }

    // Install STA devices
    NetDeviceContainer staWifiDevices;

    // Configure association manager to allow all channel widths
    // This prevents NS_ABORT during connection recovery when STA switches channels
    // The channel width compatibility check can fail during roaming between APs
    // on different 80MHz channels due to auto-width detection timing issues
    wifiMac.SetAssocManager("ns3::WifiDefaultAssocManager",
                            "AllowAssocAllChannelWidths", BooleanValue(true));

    for (uint32_t i = 0; i < numStas; i++)
    {
        // Find closest AP based on STA's initial position
        Ptr<MobilityModel> staMob = staNodes.Get(i)->GetObject<MobilityModel>();
        Vector staPos = staMob->GetPosition();
        uint32_t apIndex = FindClosestAp(staPos, config.aps);
        g_staToApIndex[i] = apIndex;
        // Note: associatedClients count is now managed by OnStaAssociated/OnStaDeassociated callbacks

        wifiPhy.Set("TxPowerStart", DoubleValue(16.0));
        wifiPhy.Set("TxPowerEnd", DoubleValue(16.0));
        wifiPhy.Set(
            "ChannelSettings",
            StringValue("{" + std::to_string(config.aps[apIndex].channel) + ", 0, BAND_5GHZ, 0}"));

        wifiMac.SetType("ns3::StaWifiMac",
                        "Ssid",
                        SsidValue(ssid),
                        "ActiveProbing",
                        BooleanValue(false),
                        "BE_MaxAmpduSize",
                        UintegerValue(6500631));  // HE maximum for better aggregation

        NetDeviceContainer staDev = wifi.Install(wifiPhy, wifiMac, staNodes.Get(i));
        staWifiDevices.Add(staDev);
    }

    // Copy to global containers for BSS TM trigger access
    g_apWifiDevices = apWifiDevices;
    g_staWifiDevices = staWifiDevices;

    // ===== Collect MAC Addresses =====
    // Collect AP BSSIDs
    for (uint32_t i = 0; i < numAps; i++)
    {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(apWifiDevices.Get(i));
        Mac48Address apBssid = wifiDev->GetMac()->GetAddress();
        g_apBssids.insert(apBssid);
    }

    // Collect STA MAC addresses and connect association trace sources
    for (uint32_t i = 0; i < numStas; i++)
    {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(staWifiDevices.Get(i));
        Mac48Address staMac = wifiDev->GetMac()->GetAddress();
        g_staMacs.insert(staMac);

        // Populate MAC-to-Node cache for O(1) lookups in CollectAndSendMetrics
        std::ostringstream macOss;
        macOss << staMac;
        g_macToNode[macOss.str()] = staNodes.Get(i);

        // Connect association/deassociation trace sources
        Ptr<StaWifiMac> staMac_ptr = DynamicCast<StaWifiMac>(wifiDev->GetMac());
        if (staMac_ptr)
        {
            uint32_t staIdx = i;  // Capture by value for lambda
            staMac_ptr->TraceConnectWithoutContext("Assoc",
                MakeBoundCallback(&OnStaAssociated, staIdx));
            staMac_ptr->TraceConnectWithoutContext("DeAssoc",
                MakeBoundCallback(&OnStaDeassociated, staIdx));
        }
    }

    // ===== Install DualPhySniffer for multi-channel beacon monitoring =====
    g_dualPhySniffer.SetChannel(channel);

    // Use AP channels as scanning channels
    std::vector<uint8_t> scanChannels;
    if (!g_scanningChannels.empty())
    {
        scanChannels = g_scanningChannels;
    }
    else
    {
        for (const auto& ap : config.aps)
        {
            uint8_t ch = static_cast<uint8_t>(ap.channel);
            if (std::find(scanChannels.begin(), scanChannels.end(), ch) == scanChannels.end())
            {
                scanChannels.push_back(ch);
            }
        }
    }
    g_dualPhySniffer.SetScanningChannels(scanChannels);
    g_dualPhySniffer.SetHopInterval(Seconds(0.3));
    g_dualPhySniffer.SetSsid(ssid);
    g_dualPhySniffer.SetValidApBssids(g_apBssids);  // Filter to only accept beacons from APs
    g_dualPhySniffer.SetBeaconMaxAge(Seconds(30));  // Expire beacons older than 30 seconds
    g_dualPhySniffer.SetBeaconMaxEntries(50);       // Limit to 50 beacon entries per receiver

    // Install scanning radio on each AP
    for (uint32_t i = 0; i < numAps; i++)
    {
        Ptr<WifiNetDevice> apWifiDev = DynamicCast<WifiNetDevice>(apWifiDevices.Get(i));
        Mac48Address apBssid = apWifiDev->GetMac()->GetAddress();
        uint8_t apChannel = static_cast<uint8_t>(config.aps[i].channel);

        g_dualPhySniffer.Install(apNodes.Get(i), apChannel, apBssid);

        // Track scanning radio MAC
        Mac48Address scanningMac = g_dualPhySniffer.GetScanningMac(apNodes.Get(i)->GetId());
        g_scanningRadioMacs.insert(scanningMac);
    }

    // Install scanning radio on each STA
    for (uint32_t i = 0; i < numStas; i++)
    {
        Ptr<WifiNetDevice> staWifiDev = DynamicCast<WifiNetDevice>(staWifiDevices.Get(i));
        Mac48Address staMac = staWifiDev->GetMac()->GetAddress();
        uint32_t apIndex = g_staToApIndex[i];
        uint8_t staChannel = static_cast<uint8_t>(config.aps[apIndex].channel);

        g_dualPhySniffer.Install(staNodes.Get(i), staChannel, staMac);

        // Track scanning radio MAC
        Mac48Address scanningMac = g_dualPhySniffer.GetScanningMac(staNodes.Get(i)->GetId());
        g_scanningRadioMacs.insert(scanningMac);
    }

    // Start channel hopping on all scanning radios
    g_dualPhySniffer.StartChannelHopping();

    // ===== Install CCA Monitors on Scanning Radios (for multi-channel tracking) =====
    WifiCcaMonitorHelper scanningCcaHelper;
    scanningCcaHelper.SetWindowSize(Seconds(0.3));     // Match hop interval
    scanningCcaHelper.SetUpdateInterval(Seconds(0.3));

    for (uint32_t i = 0; i < numAps; i++)
    {
        Ptr<Node> node = apNodes.Get(i);
        Mac48Address operatingMac = g_dualPhySniffer.GetOperatingMac(node->GetId());
        Mac48Address scanningMac = g_dualPhySniffer.GetScanningMac(node->GetId());

        // Find scanning device by MAC
        Ptr<WifiNetDevice> scanningDevice = nullptr;
        for (uint32_t d = 0; d < node->GetNDevices(); d++)
        {
            Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(d));
            if (wifiDev && wifiDev->GetMac()->GetAddress() == scanningMac)
            {
                scanningDevice = wifiDev;
                break;
            }
        }

        if (scanningDevice)
        {
            g_apScanningDevices[operatingMac] = scanningDevice;
            Ptr<WifiCcaMonitor> monitor = scanningCcaHelper.Install(scanningDevice);
            monitor->TraceConnectWithoutContext("ChannelUtilization",
                MakeCallback(&OnScanningChannelUtilization));
            monitor->Start();
            g_ccaMonitors.push_back(monitor);
        }
    }

    // ===== Install 802.11k Beacon/Neighbor Protocol =====
    g_neighborHelper = CreateObject<NeighborProtocolHelper>();
    g_beaconHelper = CreateObject<BeaconProtocolHelper>();

    // Connect to shared DualPhySniffer - enables instant responses from beacon cache
    g_neighborHelper->SetDualPhySniffer(&g_dualPhySniffer);
    g_beaconHelper->SetDualPhySniffer(&g_dualPhySniffer);

    // Install on all APs
    for (uint32_t i = 0; i < numAps; i++)
    {
        Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(apWifiDevices.Get(i));
        g_neighborHelper->InstallOnAp(apDevice);
        g_beaconHelper->InstallOnAp(apDevice);
    }

    // Install on all STAs
    for (uint32_t i = 0; i < numStas; i++)
    {
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staWifiDevices.Get(i));
        g_neighborHelper->InstallOnSta(staDevice);
        g_beaconHelper->InstallOnSta(staDevice);
    }

    // ===== Install BSS TM 11v Protocol =====
    // Per documentation: Create SEPARATE BssTm11vHelper instances per device (avoid pointer overwrites)

    // Install on all APs - one helper per AP
    for (uint32_t i = 0; i < numAps; i++)
    {
        Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(apWifiDevices.Get(i));
        Ptr<BssTm11vHelper> apHelper = CreateObject<BssTm11vHelper>();
        apHelper->SetBeaconSniffer(&g_dualPhySniffer);
        apHelper->SetCooldown(Minutes(1));  // 1 minute cooldown for testing
        apHelper->InstallOnAp(apDevice);
        g_apBssTmHelpers.push_back(apHelper);
    }

    // Install on all STAs - one helper per STA
    for (uint32_t i = 0; i < numStas; i++)
    {
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staWifiDevices.Get(i));
        Ptr<BssTm11vHelper> staHelper = CreateObject<BssTm11vHelper>();
        staHelper->SetBeaconSniffer(&g_dualPhySniffer);
        staHelper->SetCooldown(Minutes(1));
        staHelper->InstallOnSta(staDevice);
        g_staBssTmHelpers.push_back(staHelper);
    }

    // ===== Install STA Channel Hopping for Orphan Recovery =====
    if (config.staChannelHopping.enabled)
    {
        StaChannelHoppingHelper channelHoppingHelper;
        channelHoppingHelper.SetDualPhySniffer(&g_dualPhySniffer);
        channelHoppingHelper.SetApDevices(&g_apWifiDevices);
        channelHoppingHelper.SetAttribute("Enabled", BooleanValue(true));

        for (uint32_t i = 0; i < numStas; i++)
        {
            Ptr<WifiNetDevice> staDev = DynamicCast<WifiNetDevice>(staWifiDevices.Get(i));
            if (staDev)
            {
                Ptr<StaChannelHoppingManager> manager = channelHoppingHelper.Install(staDev);
                g_staChannelHoppingManagers.push_back(manager);

                NS_LOG_INFO("[STA Channel Hopping] Installed on STA " << i);
            }
        }

        std::cout << "[STA CHANNEL HOPPING] Enabled for " << g_staChannelHoppingManagers.size()
                  << " STAs" << std::endl;
    }

    // ===== Configure RSSI-based BSS TM Triggering =====
    g_rssiThreshold = config.bssOrchestrationRssiThreshold;

    // ===== Setup Load Balance Helper =====
    if (config.loadBalancing.enabled)
    {
        g_loadBalanceHelper = CreateObject<LoadBalanceHelper>();

        LoadBalanceConfig lbConfig;
        lbConfig.enabled = config.loadBalancing.enabled;
        lbConfig.channelUtilThreshold = config.loadBalancing.channelUtilThreshold / 100.0;  // Convert % to 0-1
        lbConfig.intervalSec = config.loadBalancing.intervalSec;
        lbConfig.cooldownSec = config.loadBalancing.cooldownSec;
        lbConfig.rssiThreshold = g_rssiThreshold;  // Use same threshold as RSSI roaming

        g_loadBalanceHelper->SetConfig(lbConfig);
        g_loadBalanceHelper->SetDualPhySniffer(&g_dualPhySniffer);
        g_loadBalanceHelper->SetBssTmHelpers(&g_apBssTmHelpers);
        g_loadBalanceHelper->SetApDevices(&g_apWifiDevices);
        g_loadBalanceHelper->SetStaDevices(&g_staWifiDevices);
        g_loadBalanceHelper->SetApMetrics(&g_apMetrics);
        g_loadBalanceHelper->SetStaRssiTracker(&g_apStaRssi);
        g_loadBalanceHelper->SetSimEventProducer(g_simEventProducer);
        g_loadBalanceHelper->SetNodeIdMappings(&g_simNodeIdToConfigNodeId, &g_apSimNodeIdToConfigNodeId);

        // Start after 30s to allow CCA data to populate
        g_loadBalanceHelper->Start(Seconds(30));
    }

    // ========================================================================
    // POWER SCORING SETUP - Connect HeSigA callbacks and initialize
    // ========================================================================

    // Initialize power scoring from config
    g_powerScoringEnabled = config.powerScoring.enabled;
    if (g_powerScoringEnabled)
    {
        g_powerScoringConfig.margin = config.powerScoring.margin;
        g_powerScoringConfig.gamma = config.powerScoring.gamma;
        g_powerScoringConfig.alpha = config.powerScoring.alpha;
        g_powerScoringConfig.ofcThreshold = config.powerScoring.ofcThreshold;
        g_powerScoringConfig.obsspdMinDbm = config.powerScoring.obsspdMinDbm;
        g_powerScoringConfig.obsspdMaxDbm = config.powerScoring.obsspdMaxDbm;
        g_powerScoringConfig.txPowerRefDbm = config.powerScoring.txPowerRefDbm;
        g_powerScoringConfig.txPowerMinDbm = config.powerScoring.txPowerMinDbm;
        g_powerScoringConfig.nonWifiThresholdPercent = config.powerScoring.nonWifiThresholdPercent;
        g_powerScoringConfig.nonWifiHysteresis = config.powerScoring.nonWifiHysteresis;
        g_powerScoringConfig.updateIntervalSec = config.powerScoring.updateInterval;

        std::cout << "\n========== POWER SCORING SETUP ==========" << std::endl;
        g_powerScoringHelper.SetConfig(g_powerScoringConfig);

        for (size_t i = 0; i < config.aps.size(); i++)
        {
            uint32_t nodeId = apNodes.Get(i)->GetId();
            Ptr<Node> apNode = apNodes.Get(i);

            Ptr<WifiNetDevice> wifiDevice = nullptr;
            for (uint32_t j = 0; j < apNode->GetNDevices(); j++)
            {
                wifiDevice = DynamicCast<WifiNetDevice>(apNode->GetDevice(j));
                if (wifiDevice) break;
            }

            if (!wifiDevice) continue;

            // Get BSS Color from HeConfiguration
            uint8_t bssColor = 0;
            bool hasHePhy = false;
            Ptr<HeConfiguration> heConfig = wifiDevice->GetHeConfiguration();
            if (heConfig)
            {
                bssColor = heConfig->m_bssColor;
                hasHePhy = true;
            }

            // Update g_apMetrics with BSS Color
            auto metricsIt = g_apMetrics.find(nodeId);
            if (metricsIt != g_apMetrics.end())
            {
                metricsIt->second.bssColor = bssColor;
                metricsIt->second.txPowerDbm = config.aps[i].txPower;
            }

            // Initialize power scoring state
            g_powerScoringHelper.GetOrCreateApState(nodeId, bssColor);

            // Connect HeSigA callback
            Ptr<WifiPhy> phy = wifiDevice->GetPhy();
            if (phy && hasHePhy)
            {
                auto hePhy = std::dynamic_pointer_cast<HePhy>(phy->GetPhyEntity(WIFI_MOD_CLASS_HE));
                if (hePhy)
                {
                    uint32_t capturedNodeId = nodeId;
                    hePhy->SetEndOfHeSigACallback(
                        [capturedNodeId](HeSigAParameters params) {
                            OnApHeSigA(capturedNodeId, params);
                        });
                }

                // Connect PhyRxPayloadBegin trace
                uint32_t capturedNodeId = nodeId;
                phy->TraceConnectWithoutContext("PhyRxPayloadBegin",
                    MakeBoundCallback(&OnApPhyRxPayloadBegin, capturedNodeId));
            }

            std::cout << "[Power Scoring] AP" << i << " (Node " << nodeId
                      << "): BSS Color=" << (int)bssColor << std::endl;
        }
        std::cout << "==========================================\n" << std::endl;
    }

    // Create CSMA backbone
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    csma.SetChannelAttribute("Delay", TimeValue(MicroSeconds(10)));

    NodeContainer backboneNodes;
    backboneNodes.Add(dsNode);
    backboneNodes.Add(apNodes);

    NetDeviceContainer backboneDevices = csma.Install(backboneNodes);

    NetDeviceContainer dsDevices;
    dsDevices.Add(backboneDevices.Get(0));

    // Create bridges
    for (uint32_t i = 0; i < numAps; i++)
    {
        BridgeHelper bridge;
        NetDeviceContainer bridgeDevices;
        bridgeDevices.Add(apWifiDevices.Get(i));
        bridgeDevices.Add(backboneDevices.Get(i + 1));
        bridge.Install(apNodes.Get(i), bridgeDevices);
    }

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(staNodes);
    internet.Install(dsNode);

    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer staInterfaces = ipv4.Assign(staWifiDevices);
    Ipv4InterfaceContainer dsInterfaces = ipv4.Assign(dsDevices);

    // Uninstall traffic control layer on ALL devices
    // Allows WiFi MAC queue to provide proper backpressure to applications
    // Without this, QueueDisc intercepts all packets and WiFi queue never drops,
    // preventing AdaptiveUdpApplication from detecting congestion via Send() failures
    TrafficControlHelper tch;
    tch.Uninstall(staWifiDevices);  // Remove from STA WiFi interfaces
    tch.Uninstall(dsDevices);       // Remove from destination server

    g_dsAddress = dsInterfaces.GetAddress(0);

    // Build IP to STA mapping and IP to MAC mapping
    for (uint32_t i = 0; i < numStas; i++)
    {
        Ipv4Address staIp = staInterfaces.GetAddress(i);
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staWifiDevices.Get(i));
        Mac48Address staMac = staDevice->GetMac()->GetAddress();

        g_ipToStaIndex[staIp] = i;
        g_ipToStaMac[staIp] = staMac;
    }

    // ===== FIX: Pre-populate ARP cache to avoid resolution failures =====
    // Get DS MAC address from the CSMA device (bridges are L2 transparent)
    Mac48Address dsMacAddress = Mac48Address::ConvertFrom(dsDevices.Get(0)->GetAddress());

    // Pre-populate each STA's ARP cache with DS entry marked as PERMANENT
    for (uint32_t i = 0; i < numStas; i++)
    {
        Ptr<Node> staNode = staNodes.Get(i);
        Ptr<Ipv4L3Protocol> staIpv4 = staNode->GetObject<Ipv4L3Protocol>();
        if (staIpv4)
        {
            // STA has only one IPv4 interface (WiFi)
            Ptr<Ipv4Interface> iface = staIpv4->GetInterface(1);  // Interface 1 is WiFi (0 is loopback)
            if (iface)
            {
                Ptr<ArpCache> arpCache = iface->GetArpCache();
                if (arpCache)
                {
                    ArpCache::Entry* entry = arpCache->Add(g_dsAddress);
                    entry->SetMacAddress(dsMacAddress);
                    entry->MarkPermanent();  // NEVER expires
                }
            }
        }
    }
    NS_LOG_INFO("Pre-populated ARP cache on " << numStas << " STAs with DS address " << g_dsAddress << " -> " << dsMacAddress);

    // Setup traffic
    uint16_t port = 9;

    // Uplink: STA -> DS (UDP for faster simulation, MAC contention still applies)
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = sinkHelper.Install(dsNode.Get(0));
    sinkApps.Start(Seconds(1.0));
    sinkApps.Stop(Seconds(config.simulationTime));

    // Store references for traffic reinstallation after roaming
    g_staNodes = staNodes;
    g_simulationEndTime = config.simulationTime;

    for (uint32_t i = 0; i < numStas; i++)
    {
        // Use AdaptiveUdpApplication with TCP-like AIMD congestion control
        AdaptiveUdpHelper adaptive("ns3::UdpSocketFactory",
                                   InetSocketAddress(dsInterfaces.GetAddress(0), port));
        adaptive.SetAttribute("InitialDataRate", DataRateValue(DataRate("3Mbps")));
        adaptive.SetAttribute("PacketSize", UintegerValue(1400));
        adaptive.SetAttribute("MinDataRate", DataRateValue(DataRate("25Kbps")));
        adaptive.SetAttribute("MaxDataRate", DataRateValue(DataRate("10Mbps")));
        // AIMD: backoff to 50% on failure, increase 500Kbps after 3 successes (faster ramp-up)
        adaptive.SetAttribute("BackoffMultiplier", DoubleValue(0.5));
        adaptive.SetAttribute("AdditiveIncrease", UintegerValue(500000));
        adaptive.SetAttribute("SuccessThreshold", UintegerValue(3));

        ApplicationContainer clientApp = adaptive.Install(staNodes.Get(i));
        clientApp.Start(Seconds(2.0 + i * 0.1));
        clientApp.Stop(Seconds(config.simulationTime));

        // Store reference for roaming restart
        g_staAdaptiveApps[i] = DynamicCast<AdaptiveUdpApplication>(clientApp.Get(0));
    }

    // Downlink traffic removed - uplink only

    // Setup Kafka producer
    if (g_enableKafka)
    {
        NodeContainer kafkaNode;
        kafkaNode.Create(1);

        KafkaProducerHelper kafkaHelper(kafkaBroker, kafkaTopic, simulationId);
        kafkaHelper.SetUpdateInterval(Seconds(g_statsInterval));
        ApplicationContainer kafkaApp = kafkaHelper.Install(kafkaNode.Get(0));
        kafkaApp.Start(Seconds(10.0));  // Start sending after network stabilizes
        kafkaApp.Stop(Seconds(config.simulationTime + 5.0));

        g_kafkaProducer = KafkaProducerHelper::GetKafkaProducer(kafkaNode.Get(0));

        // Setup Kafka consumer for receiving optimization commands
        KafkaConsumerHelper consumerHelper(
            kafkaBroker,
            kafkaDownstreamTopic,
            kafkaConsumerGroupId,
            simulationId
        );
        consumerHelper.SetPollInterval(MilliSeconds(100));

        NodeContainer consumerNode;
        consumerNode.Create(1);

        ApplicationContainer consumerApp = consumerHelper.Install(consumerNode.Get(0));
        consumerApp.Start(Seconds(0.0));
        consumerApp.Stop(Seconds(config.simulationTime + 5.0));

        g_kafkaConsumer = KafkaConsumerHelper::GetKafkaConsumer(consumerNode.Get(0));
        g_kafkaConsumer->SetParameterCallback(MakeCallback(&OnParametersReceived));

        NS_LOG_INFO("Kafka consumer enabled: broker=" << kafkaBroker
                    << " topic=" << kafkaDownstreamTopic);

        // Setup Simulation Event Producer (for simulator-events topic)
        SimulationEventHelper eventHelper;
        eventHelper.SetBrokers(kafkaBroker);
        eventHelper.SetTopic("simulator-events");
        eventHelper.SetSimulationId(simulationId);
        eventHelper.SetFlushInterval(MilliSeconds(100));
        eventHelper.SetNodeIdMappings(g_simNodeIdToConfigNodeId, g_apSimNodeIdToConfigNodeId);

        // Install on same node as Kafka producer
        g_simEventProducer = eventHelper.Install(kafkaNode.Get(0));

        if (g_simEventProducer)
        {
            NS_LOG_INFO("Simulation Event Producer enabled: topic=simulator-events");
        }
    }

    // Install FlowMonitor
    g_flowMonitor = g_flowMonitorHelper.InstallAll();

    // Schedule metrics collection
    Simulator::Schedule(Seconds(g_statsInterval), &CollectAndSendMetrics);

    // Schedule periodic RSSI check for BSS TM triggering
    Simulator::Schedule(Seconds(RSSI_CHECK_INTERVAL), &PeriodicRssiCheck);

    // Schedule periodic cleanup of global maps to prevent unbounded growth
    Simulator::Schedule(Seconds(60), &PeriodicMapCleanup);

    // Schedule periodic channel scoring (if enabled)
    if (g_channelScoringEnabled)
    {
        // Start after 15s to allow scan data collection
        Simulator::Schedule(Seconds(15), &PerformChannelScoring);
    }

    // Schedule periodic power scoring (if enabled)
    if (g_powerScoringEnabled)
    {
        // Start after 5 seconds to allow network to stabilize
        Simulator::Schedule(Seconds(5.0), &PerformPowerScoring);
        std::cout << "[Power Scoring] First scoring scheduled at t=5.0s" << std::endl;
    }

    // Run simulation
    Simulator::Stop(Seconds(config.simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
