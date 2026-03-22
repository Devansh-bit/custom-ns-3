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

// Virtual Interferer module
#include "ns3/virtual-interferer.h"
#include "ns3/virtual-interferer-environment.h"
#include "ns3/virtual-interferer-helper.h"
#include "ns3/microwave-interferer.h"
#include "ns3/bluetooth-interferer.h"
#include "ns3/cordless-interferer.h"
#include "ns3/zigbee-interferer.h"
#include "ns3/radar-interferer.h"

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/error/en.h>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <queue>
#include <chrono>
#include <unordered_map>

// AdaptiveUdp application for traffic with AIMD congestion control
#include "ns3/adaptive-udp-application.h"
#include "ns3/adaptive-udp-helper.h"

using namespace ns3;
using namespace rapidjson;

NS_LOG_COMPONENT_DEFINE("BasicSimulation");

// Global FlowMonitor for stats collection
Ptr<FlowMonitor> g_flowMonitor;
FlowMonitorHelper g_flowMonitorHelper;
double g_statsInterval = 1.0;

// Global Kafka producer
Ptr<KafkaProducer> g_kafkaProducer;
bool g_enableKafka = true;

// Verbose logging flag (disabled by default - only AP/STA metrics shown)
bool g_verboseLogging = false;

// Display time scaling (for showing simulation time as wall clock time)
double g_displayTimeScaleFactor = 1.0;      // e.g., 10.0 means 1 sim second = 10 display seconds
double g_displayTimeOffsetSeconds = 0.0;    // e.g., 61200 = 17:00:00 (5 PM)

/**
 * Convert simulation time to display time (scaled and offset)
 * Used for showing simulation time as wall clock time in logs
 */
double GetDisplayTimeSeconds()
{
    return Simulator::Now().GetSeconds() * g_displayTimeScaleFactor + g_displayTimeOffsetSeconds;
}

/**
 * Format display time as HH:MM:SS string
 */
std::string FormatDisplayTime()
{
    double displaySec = GetDisplayTimeSeconds();
    int totalSeconds = static_cast<int>(displaySec) % 86400;  // Wrap at 24 hours
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ":"
        << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setfill('0') << std::setw(2) << seconds;
    return oss.str();
}

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

// ===== Traffic Reinstallation After Roaming =====
// Map STA index -> AdaptiveUdpApplication pointer for traffic restart after roaming
std::map<uint32_t, Ptr<AdaptiveUdpApplication>> g_staAdaptiveApps;
// Track STAs that need app restart (only after disassociation, not initial association)
std::set<uint32_t> g_stasPendingAppRestart;
// Store simulation end time for app stop scheduling
double g_simulationEndTime = 0.0;
// Reference to STA nodes for reinstalling apps
NodeContainer g_staNodes;

// ===== HIGH_THROUGHPUT Stress Test State =====
bool g_highThroughputActive = false;
std::map<uint32_t, DataRate> g_originalMaxRates;      // Store original max rates for restore
std::map<uint32_t, DataRate> g_originalCurrentRates;  // Store original current rates

// ===== HIGH_INTERFERENCE Stress Test State =====
bool g_highInterferenceActive = false;
std::map<uint32_t, uint8_t> g_originalChannels;       // Store original channels for restore
std::map<uint32_t, WifiPhyBand> g_originalBands;      // Store original bands for restore
double g_highInterferenceDuration = 30.0;             // Duration in seconds before auto-restore

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

// Log file output stream with tee (writes to both terminal AND file)
std::ofstream g_logFile;

class TeeStreambuf : public std::streambuf {
public:
    TeeStreambuf(std::streambuf* terminal, std::streambuf* file)
        : m_terminal(terminal), m_file(file) {}
protected:
    int overflow(int c) override {
        if (c != EOF) {
            if (m_terminal) m_terminal->sputc(c);
            if (m_file) m_file->sputc(c);
        }
        return c;
    }
    int sync() override {
        if (m_terminal) m_terminal->pubsync();
        if (m_file) m_file->pubsync();
        return 0;
    }
private:
    std::streambuf* m_terminal;
    std::streambuf* m_file;
};

TeeStreambuf* g_teeCout = nullptr;
TeeStreambuf* g_teeCerr = nullptr;
TeeStreambuf* g_teeClog = nullptr;
std::streambuf* g_origCoutBuf = nullptr;
std::streambuf* g_origCerrBuf = nullptr;
std::streambuf* g_origClogBuf = nullptr;

// ===== Channel Scoring (ACS/DCS Support) =====
ChannelScoringConfig g_channelScoringConfig;
ChannelScoringHelper g_channelScoringHelper;
bool g_channelScoringEnabled = true;
double g_channelScoringInterval = 10.0;  // Default 10 seconds

// ===== Global Channel Ranking (for reactive channel management) =====
/**
 * Global channel ranking structure - updated by PerformChannelScoring
 * Used for quick backup channel selection during DFS or high interference
 */
struct GlobalChannelRanking {
    std::vector<uint8_t> ranked5GHz;              // Best to worst channels for 5GHz
    std::vector<uint8_t> ranked24GHz;             // Best to worst channels for 2.4GHz
    std::set<uint8_t> dfsBlacklist;               // Channels with recent radar detection
    std::map<uint8_t, Time> blacklistExpiry;      // When each blacklisted channel expires
    Time lastUpdate;                              // When ranking was last updated
};

GlobalChannelRanking g_globalRanking;
double g_nonWifiThreshold = 65.0;                 // Trigger channel scoring when non-WiFi > this %
Time g_dfsBlacklistDuration = Seconds(30);        // How long to blacklist DFS channel after radar

// DFS detection tracking
std::map<uint8_t, Time> g_lastDfsDetection;       // Last detection time per channel
Time g_dfsDetectionCooldown = Seconds(5);         // Cooldown between detections for same channel
Ptr<RadarInterferer> g_radarInterferer = nullptr; // For querying affected channels

// Recently used backup channels (to avoid ping-pong during rapid DFS events)
std::set<uint8_t> g_recentlyUsedBackups;          // Channels used as backup recently
Time g_backupUsageCooldown = Seconds(30);         // How long to avoid reusing a backup
std::map<uint8_t, Time> g_backupUsageTime;        // When each backup was used

// Per-AP channel switch cooldown (wait for beacon cache to refresh after switch)
std::map<uint32_t, Time> g_lastChannelSwitch;     // When each AP last switched channels
Time g_channelSwitchCooldown = Seconds(15);       // Wait 15s for beacon cache refresh

// Global debounce for channel scoring (prevent multiple rapid scoring runs)
Time g_lastScoringRun = Seconds(0);
Time g_scoringDebounce = Seconds(1);              // Minimum 1s between scoring runs

// Per-AP scoring queue (process one at a time to avoid conflicts)
std::queue<uint32_t> g_apScoringQueue;            // Queue of AP nodeIds waiting for scoring
std::set<uint8_t> g_pendingAllocations;           // Channels already allocated in current batch
bool g_scoringQueueProcessing = false;            // Flag to prevent concurrent queue processing

// ===== Power Scoring (RACEBOT-based TX Power Control) =====
PowerScoringConfig g_powerScoringConfig;
PowerScoringHelper g_powerScoringHelper;
bool g_powerScoringEnabled = false;               // Disabled by default, enabled from JSON

// ===== Throughput Metrics Scaling =====
double g_throughputScaleFactor = 10.0;  // Must match divisor in HePhy::GetDataRate (/10)

// Default jitter values for interferer start times (seconds)
// Determines random offset range: actualStart = baseStart + uniform(0, jitter)
const double JITTER_ZIGBEE = 3.0;      // Most common: [0-3s]
const double JITTER_BLUETOOTH = 5.0;   // Common: [0-5s]
const double JITTER_CORDLESS = 8.0;    // Moderate: [0-8s]
const double JITTER_MICROWAVE = 12.0;  // Less common: [0-12s]
const double JITTER_RADAR = 25.0;      // Rarest: base 25s + [0-5s] jitter

// Fallback non-DFS channels (UNII-1 and UNII-3 bands - no DFS required)
const std::vector<uint8_t> g_nonDfsChannels5GHz = {36, 40, 44, 48, 149, 153, 157, 161, 165};
const std::vector<uint8_t> g_nonDfsChannels24GHz = {1, 6, 11};  // 2.4GHz has no DFS

// DFS channels in 5GHz (UNII-2A and UNII-2C)
const std::set<uint8_t> g_dfsChannels5GHz = {
    52, 56, 60, 64,           // UNII-2A
    100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144  // UNII-2C
};

/**
 * Check if a channel requires DFS (is in UNII-2 bands)
 */
bool IsDfsChannel(uint8_t channel)
{
    return g_dfsChannels5GHz.count(channel) > 0;
}

// ===== Virtual Interferer Support =====
// Global channel utilization map: Channel -> (channelUtil, wifiUtil, nonWifiUtil)
struct ChannelUtilData {
    double channelUtil = 0.0;    // Total channel utilization (0-100%)
    double wifiUtil = 0.0;       // WiFi-only utilization (0-100%)
    double nonWifiUtil = 0.0;    // Non-WiFi utilization from VI (0-100%)
};
std::map<uint8_t, ChannelUtilData> g_channelUtilMap;

// Virtual interferer helper instance
VirtualInterfererHelper g_viHelper;
bool g_virtualInterfererEnabled = false;

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
        // Trigger and cooldown settings
        double nonWifiTriggerThreshold = 65.0;  // Trigger scoring when non-WiFi > this %
        double dfsBlacklistDurationSec = 30.0;  // How long to blacklist DFS channel after radar
        double channelSwitchCooldownSec = 15.0;  // Wait for beacon cache refresh after switch
    } channelScoring;

    // Power Scoring configuration (RACEBOT algorithm)
    struct {
        bool enabled = false;
        double updateInterval = 1.0;           // Update interval in seconds
        double t1IntervalSec = 10.0;           // Slow loop: goal recalculation interval
        double t2IntervalSec = 2.0;            // Fast loop: power change cooldown
        double margin = 3.0;                   // M parameter (dBm)
        double gamma = 0.7;                    // MCS change threshold
        double alpha = 0.3;                    // EWMA smoothing factor
        double ofcThreshold = 500.0;           // OFC threshold (frame count)
        double obsspdMinDbm = -82.0;           // Min OBSS/PD
        double obsspdMaxDbm = -62.0;           // Max OBSS/PD
        double txPowerRefDbm = 33.0;           // Reference/Maximum TX power
        double txPowerMinDbm = 10.0;           // Min TX power
        double nonWifiThresholdPercent = 50.0; // Non-WiFi mode threshold
        double nonWifiHysteresis = 10.0;       // Hysteresis for mode transitions
        double rlPowerMarginDbm = 2.0;         // RL integration: margin around RL power center
    } powerScoring;

    // ===== Virtual Interferer Configuration =====
    struct ScheduleConfig {
        double onDuration = 0.0;
        double offDuration = 0.0;
        bool hasSchedule = false;
    };

    struct MicrowaveConfig {
        double x = 0.0, y = 0.0, z = 0.0;
        double txPowerDbm = -20.0;
        double dutyCycle = 0.5;
        double startTime = 0.0;
        bool active = true;
        ScheduleConfig schedule;
    };

    struct BluetoothConfig {
        double x = 0.0, y = 0.0, z = 0.0;
        double txPowerDbm = 4.0;
        std::string profile = "HID";
        double startTime = 0.0;
        bool active = true;
        ScheduleConfig schedule;
    };

    struct CordlessConfig {
        double x = 0.0, y = 0.0, z = 0.0;
        double txPowerDbm = 10.0;
        uint32_t numHops = 100;
        double hopInterval = 0.01;
        double bandwidthMhz = 1.728;
        double startTime = 0.0;
        bool active = true;
        ScheduleConfig schedule;
    };

    struct ZigbeeConfig {
        double x = 0.0, y = 0.0, z = 0.0;
        double txPowerDbm = 0.0;
        uint8_t zigbeeChannel = 11;
        std::string networkType = "SENSOR";
        double dutyCycle = 0.02;
        double startTime = 0.0;
        bool active = true;
        ScheduleConfig schedule;
    };

    struct RadarConfig {
        double x = 0.0, y = 0.0, z = 0.0;
        double txPowerDbm = 30.0;
        uint8_t dfsChannel = 52;
        std::string radarType = "WEATHER";
        std::vector<uint8_t> dfsChannels;
        double hopIntervalSec = 5.0;
        bool randomHopping = false;
        uint8_t spanLength = 2;
        uint8_t maxSpanLength = 4;
        bool randomSpan = false;
        double startTime = 0.0;
        double startTimeJitter = 5.0;
        bool active = true;
        ScheduleConfig schedule;
    };

    struct {
        bool enabled = false;
        double updateInterval = 0.1;
        std::vector<MicrowaveConfig> microwaves;
        std::vector<BluetoothConfig> bluetooths;
        std::vector<CordlessConfig> cordless;
        std::vector<ZigbeeConfig> zigbees;
        std::vector<RadarConfig> radars;
    } virtualInterferers;

    // STA Channel Hopping configuration (for orphaned client recovery)
    struct {
        bool enabled = true;  // Enabled by default for stable roaming
        double scanningDelaySec = 5.0;  // Delay before attempting reconnection
        double minimumSnrDb = 0.0;      // Minimum SNR threshold for AP selection
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
void PerformChannelScoring();  // Scores ALL APs (used for DFS events)
void PerformChannelScoringForAp(uint32_t nodeId);  // Scores ONLY specific AP
void ProcessApScoringQueue();  // Process scoring queue one AP at a time
void QueueApForScoring(uint32_t nodeId);  // Add AP to scoring queue
void PerformPowerScoring();
void OnApHeSigA(uint32_t nodeId, HeSigAParameters params);
void OnApPhyRxPayloadBegin(uint32_t nodeId, WifiTxVector txVector, Time psduDuration);
double GetNonWifiForAp(uint32_t nodeId);
void HandleDfsRadarDetection(uint32_t nodeId, uint8_t receiverChannel, uint8_t dfsChannel);
void CheckDfsBlacklistExpiry();
uint8_t GetBackupChannel(WifiPhyBand band, uint8_t excludeChannel);
void OnCommandReceived(std::string commandType, std::map<std::string, std::string> params);
void ForceRadarDetection();
void ForceHighInterference();
void RestoreNormalBands();
void ForceHighThroughput();
void RestoreNormalThroughput();

// ============================================================================
// DFS RADAR HANDLING FUNCTIONS
// ============================================================================

/**
 * Check if a channel is available as backup (not blacklisted, not recently used, not current)
 */
bool IsBackupChannelAvailable(uint8_t ch, uint8_t excludeChannel)
{
    if (ch == excludeChannel) return false;
    if (g_globalRanking.dfsBlacklist.count(ch) > 0) return false;

    // Check if recently used as backup (avoid ping-pong)
    auto usageIt = g_backupUsageTime.find(ch);
    if (usageIt != g_backupUsageTime.end()) {
        if (Simulator::Now() - usageIt->second < g_backupUsageCooldown) {
            return false;  // Recently used, skip
        }
    }
    return true;
}

/**
 * Get backup channel (excludes blacklisted/affected, recently-used, and current channel)
 * @param band WiFi band (5GHz or 2.4GHz)
 * @param excludeChannel Current channel to exclude
 *
 * Note: For DFS events, affected channels are already in dfsBlacklist,
 * so any channel NOT in blacklist is valid (DFS or non-DFS)
 */
uint8_t GetBackupChannel(WifiPhyBand band, uint8_t excludeChannel)
{
    // First, try the global ranking (if populated) - excludes blacklisted (affected) channels
    auto& ranked = (band == WIFI_PHY_BAND_5GHZ)
        ? g_globalRanking.ranked5GHz
        : g_globalRanking.ranked24GHz;

    for (uint8_t ch : ranked) {
        if (IsBackupChannelAvailable(ch, excludeChannel)) {
            return ch;
        }
    }

    // Fallback: try known non-DFS channels (guaranteed not affected by radar)
    const auto& fallback = (band == WIFI_PHY_BAND_5GHZ)
        ? g_nonDfsChannels5GHz
        : g_nonDfsChannels24GHz;

    for (uint8_t ch : fallback) {
        if (IsBackupChannelAvailable(ch, excludeChannel)) {
            return ch;
        }
    }

    // Last resort: any channel not blacklisted (even if recently used)
    for (uint8_t ch : fallback) {
        if (ch != excludeChannel && g_globalRanking.dfsBlacklist.count(ch) == 0) {
            return ch;
        }
    }

    return 0;  // No backup available
}

/**
 * Get optimal backup channel for a specific AP using its own scan data
 * This provides per-AP optimal channel selection during DFS events
 * @param apNodeId The AP node ID
 * @param blacklistedChannels Channels to exclude (DFS blacklist)
 * @return Best available channel for this AP, or 0 if none
 */
uint8_t GetOptimalBackupForAp(uint32_t apNodeId, const std::set<uint8_t>& blacklistedChannels)
{
    auto apIt = g_apMetrics.find(apNodeId);
    if (apIt == g_apMetrics.end()) {
        return 0;
    }

    ApMetrics& apMetrics = apIt->second;

    // Refresh scan data before scoring
    ProcessBeaconMeasurements();

    // Calculate scores using THIS AP's scan data (filtered by RL width bounds)
    auto scores = g_channelScoringHelper.CalculateScoresForRlWidth(
        apNodeId,
        apMetrics.scanningChannelData);

    if (scores.empty()) {
        // Fallback to non-DFS channels if no scan data
        const auto& fallback = (apMetrics.band == WIFI_PHY_BAND_5GHZ)
            ? g_nonDfsChannels5GHz
            : g_nonDfsChannels24GHz;

        for (uint8_t ch : fallback) {
            if (blacklistedChannels.count(ch) == 0 && ch != apMetrics.channel) {
                return ch;
            }
        }
        return 0;
    }

    // Find best channel not blacklisted for THIS AP
    for (const auto& score : scores) {
        if (score.discarded) continue;
        if (!ChannelScoringHelper::IsChannelInBand(score.channel, apMetrics.band)) continue;
        if (blacklistedChannels.count(score.channel) > 0) continue;
        if (score.channel == apMetrics.channel) continue;  // Skip current channel

        return score.channel;
    }

    // Fallback: any non-DFS channel not blacklisted
    const auto& fallback = (apMetrics.band == WIFI_PHY_BAND_5GHZ)
        ? g_nonDfsChannels5GHz
        : g_nonDfsChannels24GHz;

    for (uint8_t ch : fallback) {
        if (blacklistedChannels.count(ch) == 0 && ch != apMetrics.channel) {
            return ch;
        }
    }

    return 0;  // No backup available
}

/**
 * Check and expire old DFS blacklist entries
 */
void CheckDfsBlacklistExpiry()
{
    Time now = Simulator::Now();
    std::vector<uint8_t> expired;
    for (const auto& [channel, expiryTime] : g_globalRanking.blacklistExpiry) {
        if (now >= expiryTime) {
            expired.push_back(channel);
        }
    }
    for (uint8_t ch : expired) {
        g_globalRanking.dfsBlacklist.erase(ch);
        g_globalRanking.blacklistExpiry.erase(ch);
        if (g_verboseLogging) {
            std::cout << "[DFS-EXPIRY] Channel " << (int)ch << " removed from blacklist at t="
                         << GetDisplayTimeSeconds() << "s (was blacklisted for "
                         << g_dfsBlacklistDuration.GetSeconds() << "s)" << std::endl;
        }
    }
}

/**
 * Handle DFS radar detection from Virtual Interferer
 * Adds ALL affected channels to blacklist and triggers immediate channel switch
 */
void HandleDfsRadarDetection(uint32_t nodeId, uint8_t receiverChannel, uint8_t dfsChannel)
{
    Time now = Simulator::Now();

    // Check cooldown for this channel to avoid spam
    if (g_lastDfsDetection.count(receiverChannel) > 0) {
        if (now - g_lastDfsDetection[receiverChannel] < g_dfsDetectionCooldown) {
            return;  // Skip, already detected recently
        }
    }
    g_lastDfsDetection[receiverChannel] = now;

    // Get all channels affected by the wideband radar
    std::set<uint8_t> affectedChannels;
    if (g_radarInterferer) {
        affectedChannels = g_radarInterferer->GetCurrentlyAffectedChannels();
    } else {
        // Fallback: just blacklist the detected channel
        affectedChannels.insert(dfsChannel);
    }

    // Build affected channels string for logging
    std::ostringstream affectedStr;
    affectedStr << "[";
    bool first = true;
    for (uint8_t ch : affectedChannels) {
        if (!first) affectedStr << ",";
        affectedStr << (int)ch;
        first = false;
    }
    affectedStr << "]";

    // Count how many new channels we're blacklisting
    int newlyBlacklisted = 0;
    for (uint8_t ch : affectedChannels) {
        if (g_globalRanking.dfsBlacklist.count(ch) == 0) {
            g_globalRanking.dfsBlacklist.insert(ch);
            g_globalRanking.blacklistExpiry[ch] = now + g_dfsBlacklistDuration;
            newlyBlacklisted++;
        }
    }

    if (newlyBlacklisted > 0) {
        if (g_verboseLogging) {
            std::cout << "[DFS-BLACKLIST] Radar detected on DFS channel " << (int)dfsChannel
                         << " by node " << nodeId << " (on CH " << (int)receiverChannel << ") at t=" << GetDisplayTimeSeconds() << "s"
                         << ", Affected channels: " << affectedStr.str()
                         << " (" << newlyBlacklisted << " newly blacklisted)"
                         << ", Blacklist duration: " << g_dfsBlacklistDuration.GetSeconds() << " seconds" << std::endl;
        }

        // Record DFS radar detected event to Kafka
        if (g_simEventProducer)
        {
            g_simEventProducer->RecordDfsRadarDetected(0, dfsChannel, receiverChannel, affectedStr.str());
        }

        // Check ALL APs - any on affected channels must switch immediately
        // Use per-AP optimal channel selection (each AP picks best channel from its own perspective)
        int apsSwitched = 0;
        for (auto& [apNodeId, apMetrics] : g_apMetrics) {
            uint8_t currentCh = apMetrics.channel;
            if (affectedChannels.count(currentCh) > 0) {
                // This AP is on an affected channel - get OPTIMAL backup using THIS AP's scan data
                uint8_t backupCh = GetOptimalBackupForAp(apNodeId, g_globalRanking.dfsBlacklist);
                if (backupCh != 0) {
                    // Mark this backup as recently used (avoid ping-pong)
                    g_backupUsageTime[backupCh] = now;

                    if (g_verboseLogging) {
                        std::cout << "[DFS-SWITCH] Node " << apNodeId << " per-AP optimal switch from Ch "
                                     << (int)currentCh << " to Ch " << (int)backupCh << std::endl;
                    }

                    // Immediate channel switch via ApWifiMac
                    Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(apNodeId));
                    if (apDev) {
                        Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDev->GetMac());
                        if (apMac) {
                            apMac->SwitchChannel(backupCh);
                            // Update g_apMetrics
                            apMetrics.channel = backupCh;
                            apMetrics.band = (backupCh >= 36) ? WIFI_PHY_BAND_5GHZ : WIFI_PHY_BAND_2_4GHZ;
                            // Mark switch time for cooldown (wait for beacon cache refresh)
                            g_lastChannelSwitch[apNodeId] = now;
                            apsSwitched++;

                            // Record DFS channel switch event to Kafka
                            if (g_simEventProducer)
                            {
                                uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(apNodeId)
                                                              ? g_apSimNodeIdToConfigNodeId[apNodeId]
                                                              : apNodeId;
                                g_simEventProducer->RecordDfsChannelSwitch(apConfigNodeId, currentCh, backupCh, "DFS_RADAR");
                            }
                        }
                    }
                } else if (g_verboseLogging) {
                    std::cout << "[DFS-SWITCH] WARNING: No backup channel available for node " << apNodeId << std::endl;
                }
            }
        }

        if (apsSwitched > 0 && g_verboseLogging) {
            std::cout << "[DFS-SWITCH] Total " << apsSwitched << " AP(s) switched with per-AP optimal selection" << std::endl;
        }

        // Trigger channel scoring to update rankings (exclude blacklisted channels)
        Simulator::ScheduleNow(&PerformChannelScoring);
    }
}

// ============================================================================
// STRESS TEST COMMAND HANDLERS (Dashboard-triggered)
// ============================================================================

/**
 * Command router callback for Kafka consumer - handles stress test commands from dashboard
 * Routes to appropriate handler based on command type (FORCE_DFS, HIGH_INTERFERENCE, etc.)
 */
void OnCommandReceived(std::string commandType, std::map<std::string, std::string> params)
{
    std::cout << "[COMMAND] Received: " << commandType << " at t=" 
              << GetDisplayTimeSeconds() << "s" << std::endl;
    
    if (commandType == "FORCE_DFS") {
        ForceRadarDetection();
    } else if (commandType == "HIGH_INTERFERENCE") {
        ForceHighInterference();
    } else if (commandType == "HIGH_THROUGHPUT") {
        ForceHighThroughput();
    } else {
        std::cout << "[COMMAND] WARNING: Unknown command type: " << commandType << std::endl;
        NS_LOG_WARN("Unknown command: " << commandType);
    }
}

/**
 * Force DFS radar detection on-demand (user-triggered from dashboard)
 * This COEXISTS with random DFS - both can be active simultaneously
 * Temporarily activates the radar for 5 seconds to trigger DFS detection
 */
void ForceRadarDetection()
{
    if (!g_radarInterferer) {
        std::cout << "[FORCE-DFS] ERROR: No radar interferer configured" << std::endl;
        NS_LOG_WARN("[FORCE-DFS] No radar interferer available");
        return;
    }

    // Turn on radar temporarily to trigger detection
    // Duration: 5 seconds (ensures reliable detection across all APs)
    g_radarInterferer->TurnOn();

    std::cout << "[FORCE-DFS] Radar FORCED ON at t="
              << std::fixed << std::setprecision(1)
              << GetDisplayTimeSeconds() << "s (5s burst)" << std::endl;

    // Log to NS_LOG for debugging
    NS_LOG_INFO("[FORCE-DFS] Radar activated for 5 second burst");

    // Record stress test event to Kafka
    if (g_simEventProducer)
    {
        g_simEventProducer->RecordStressTestForceDfs(5.0);  // 5 second radar burst
    }
    
    // Schedule turn off after 5 seconds
    // 5 seconds = 50 VirtualInterferer update cycles (100ms each)
    // This guarantees detection across all APs even with Kafka latency
    Simulator::Schedule(Seconds(5.0), []() {
        if (g_radarInterferer) {
            g_radarInterferer->TurnOff();
            std::cout << "[FORCE-DFS] Radar burst complete at t=" 
                      << std::fixed << std::setprecision(1)
                      << GetDisplayTimeSeconds() << "s" << std::endl;
            NS_LOG_INFO("[FORCE-DFS] Radar deactivated after 5 second burst");
        }
    });

    // Event will appear in logs and DFS detection will trigger normal Kafka events
}

/**
 * Force high interference by switching all APs to 2.4GHz band
 * This makes virtual interferers (Bluetooth, ZigBee, Microwave) affect WiFi performance
 * RL/RRM system should detect poor conditions and auto-recover to 5GHz
 */
void ForceHighInterference()
{
    std::cout << "[HIGH-INTERFERENCE] Command received at t="
              << std::fixed << std::setprecision(1)
              << GetDisplayTimeSeconds() << "s" << std::endl;

    if (g_highInterferenceActive)
    {
        std::cout << "[HIGH-INTERFERENCE] Already active, ignoring duplicate command" << std::endl;
        return;
    }

    g_highInterferenceActive = true;

    // 2.4GHz non-overlapping channels
    std::vector<uint8_t> channels24GHz = {1, 6, 11};

    // Random number generator for channel selection
    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    rng->SetAttribute("Min", DoubleValue(0));
    rng->SetAttribute("Max", DoubleValue(channels24GHz.size() - 0.001));

    int apsSwitched = 0;

    // Switch each AP to random 2.4GHz channel
    for (auto& [apNodeId, apMetrics] : g_apMetrics) {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(apNodeId));
        if (apDev) {
            Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDev->GetMac());
            if (apMac) {
                // Store original channel and band for restore
                g_originalChannels[apNodeId] = apMetrics.channel;
                g_originalBands[apNodeId] = apMetrics.band;

                // Pick random 2.4GHz channel
                uint8_t newChannel = channels24GHz[static_cast<uint32_t>(rng->GetValue())];
                uint8_t originalChannel = apMetrics.channel;

                // Switch to 2.4GHz
                apMac->SwitchChannel(newChannel);

                // Update metrics
                apMetrics.channel = newChannel;
                apMetrics.band = WIFI_PHY_BAND_2_4GHZ;

                // Record channel switch event
                if (g_simEventProducer)
                {
                    uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(apNodeId)
                                                  ? g_apSimNodeIdToConfigNodeId[apNodeId]
                                                  : apNodeId;
                    g_simEventProducer->RecordChannelSwitch(apConfigNodeId, originalChannel, newChannel);
                }

                std::cout << "[HIGH-INTERFERENCE] AP " << apNodeId
                          << " switched from Ch " << +originalChannel
                          << " (5GHz) to Ch " << +newChannel
                          << " (2.4GHz)" << std::endl;

                apsSwitched++;
            }
        }
    }

    std::cout << "[HIGH-INTERFERENCE] " << apsSwitched
              << " AP(s) switched to 2.4GHz band for " << g_highInterferenceDuration << " seconds" << std::endl;

    NS_LOG_INFO("[HIGH-INTERFERENCE] Switched " << apsSwitched << " APs to 2.4GHz band");

    // Record stress test event to Kafka
    if (g_simEventProducer)
    {
        g_simEventProducer->RecordStressTestHighInterference(apsSwitched, "1,6,11");
    }

    // Schedule auto-restore after duration
    Simulator::Schedule(Seconds(g_highInterferenceDuration), &RestoreNormalBands);
}

/**
 * Restore normal bands after HIGH_INTERFERENCE stress test
 * Switches APs back to their original 5GHz channels
 */
void RestoreNormalBands()
{
    std::cout << "[HIGH-INTERFERENCE] Restoring original bands at t="
              << std::fixed << std::setprecision(1)
              << GetDisplayTimeSeconds() << "s" << std::endl;

    int apsRestored = 0;

    for (auto& [apNodeId, apMetrics] : g_apMetrics) {
        if (g_originalChannels.count(apNodeId) == 0) continue;

        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(apNodeId));
        if (apDev) {
            Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDev->GetMac());
            if (apMac) {
                uint8_t originalChannel = g_originalChannels[apNodeId];
                WifiPhyBand originalBand = g_originalBands[apNodeId];
                uint8_t currentChannel = apMetrics.channel;

                // Switch back to original channel
                apMac->SwitchChannel(originalChannel);

                // Update metrics
                apMetrics.channel = originalChannel;
                apMetrics.band = originalBand;

                // Record channel switch event
                if (g_simEventProducer)
                {
                    uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(apNodeId)
                                                  ? g_apSimNodeIdToConfigNodeId[apNodeId]
                                                  : apNodeId;
                    g_simEventProducer->RecordChannelSwitch(apConfigNodeId, currentChannel, originalChannel);
                }

                std::cout << "[HIGH-INTERFERENCE] AP " << apNodeId
                          << " restored from Ch " << +currentChannel
                          << " (2.4GHz) to Ch " << +originalChannel
                          << " (5GHz)" << std::endl;

                apsRestored++;
            }
        }
    }

    // Clear stored channels
    g_originalChannels.clear();
    g_originalBands.clear();
    g_highInterferenceActive = false;

    std::cout << "[HIGH-INTERFERENCE] " << apsRestored
              << " AP(s) restored to 5GHz band" << std::endl;

    NS_LOG_INFO("[HIGH-INTERFERENCE] Restored " << apsRestored << " APs to 5GHz band");
}

// ============================================================================
// HIGH THROUGHPUT STRESS TEST FUNCTIONS
// ============================================================================

/**
 * Restore normal throughput after HIGH_THROUGHPUT stress test
 */
void RestoreNormalThroughput()
{
    std::cout << "[HIGH-THROUGHPUT] Restoring normal rates at t="
              << std::fixed << std::setprecision(1)
              << GetDisplayTimeSeconds() << "s" << std::endl;

    int stasRestored = 0;

    for (auto& [staIndex, app] : g_staAdaptiveApps)
    {
        if (app && g_originalMaxRates.count(staIndex) > 0)
        {
            // Restore original max rate first
            app->SetMaxRate(g_originalMaxRates[staIndex]);

            // Restore original current rate (clamped to max)
            app->SetCurrentRate(g_originalCurrentRates[staIndex]);

            std::cout << "[HIGH-THROUGHPUT] STA " << staIndex
                      << " restored to " << app->GetCurrentRate()
                      << " (max: " << app->GetMaxRate() << ")" << std::endl;

            stasRestored++;
        }
    }

    // Clear stored rates
    g_originalMaxRates.clear();
    g_originalCurrentRates.clear();
    g_highThroughputActive = false;

    std::cout << "[HIGH-THROUGHPUT] " << stasRestored
              << " STA(s) restored to normal rates" << std::endl;

    NS_LOG_INFO("[HIGH-THROUGHPUT] Restored " << stasRestored << " STAs to normal rates");
}

/**
 * Force high throughput by boosting all STA traffic rates
 * Effect lasts for 15 seconds, then auto-restores
 */
void ForceHighThroughput()
{
    std::cout << "[HIGH-THROUGHPUT] Command received at t="
              << std::fixed << std::setprecision(1)
              << GetDisplayTimeSeconds() << "s" << std::endl;

    if (g_highThroughputActive)
    {
        std::cout << "[HIGH-THROUGHPUT] Already active, ignoring duplicate command" << std::endl;
        return;
    }

    g_highThroughputActive = true;
    double boostFactor = 3.0;  // Triple the rates
    int stasBoosted = 0;

    // Store original rates and boost each STA
    for (auto& [staIndex, app] : g_staAdaptiveApps)
    {
        if (app)
        {
            // Store original rates for restore
            g_originalMaxRates[staIndex] = app->GetMaxRate();
            g_originalCurrentRates[staIndex] = app->GetCurrentRate();

            // Boost throughput
            app->BoostThroughput(boostFactor);

            std::cout << "[HIGH-THROUGHPUT] STA " << staIndex
                      << " boosted: " << g_originalCurrentRates[staIndex]
                      << " -> " << app->GetCurrentRate()
                      << " (max: " << app->GetMaxRate() << ")" << std::endl;

            stasBoosted++;
        }
    }

    std::cout << "[HIGH-THROUGHPUT] " << stasBoosted
              << " STA(s) boosted by " << boostFactor << "x for 15 seconds" << std::endl;

    NS_LOG_INFO("[HIGH-THROUGHPUT] Boosted " << stasBoosted << " STAs by " << boostFactor << "x");

    // Record stress test event to Kafka
    if (g_simEventProducer)
    {
        g_simEventProducer->RecordStressTestHighThroughput(stasBoosted, boostFactor, 15.0);
    }

    // Schedule auto-restore after 15 seconds
    Simulator::Schedule(Seconds(15.0), &RestoreNormalThroughput);
}

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
    adaptive.SetAttribute("InitialDataRate", DataRateValue(DataRate("3Mbps")));  // 0.3 Mbps effective -> 3 Mbps displayed
    adaptive.SetAttribute("PacketSize", UintegerValue(1400));
    adaptive.SetAttribute("MinDataRate", DataRateValue(DataRate("500Kbps")));
    adaptive.SetAttribute("MaxDataRate", DataRateValue(DataRate("8Mbps")));  // 0.3 Mbps effective max -> 3 Mbps displayed

    ApplicationContainer newApp = adaptive.Install(staNode);
    newApp.Start(Simulator::Now() + MilliSeconds(50));  // Brief delay for routing
    newApp.Stop(Seconds(g_simulationEndTime));

    g_staAdaptiveApps[staIndex] = DynamicCast<AdaptiveUdpApplication>(newApp.Get(0));

    if (g_verboseLogging) {
        std::cout << "[TRAFFIC] Reinstalled AdaptiveUdp app for STA " << staIndex
                  << " at t=" << GetDisplayTimeSeconds() << "s" << std::endl;
    }
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

    std::cout << "[ASSOC] " << std::fixed << std::setprecision(2)
                 << GetDisplayTimeSeconds() << "s: "
                 << "STA " << staMac << " (idx=" << staIndex << ") "
                 << "ASSOCIATED with AP " << apBssid << " (node=" << newApIndex << ")" << std::endl;

    // Note: associatedClients count is now updated in CollectAndSendMetrics()
    // by querying actual STA list from ApWifiMac to avoid callback sync issues

    // Check if this is a roaming event (STA was previously associated with a different AP)
    uint32_t oldApIndex = UINT32_MAX;
    bool isRoaming = false;
    auto it = g_staToApIndex.find(staIndex);
    if (it != g_staToApIndex.end() && it->second != newApIndex)
    {
        oldApIndex = it->second;
        isRoaming = true;
    }

    // Update STA to AP mapping
    g_staToApIndex[staIndex] = newApIndex;

    // Record events to Kafka
    if (g_simEventProducer)
    {
        // Get config node IDs
        uint32_t staConfigNodeId = g_simNodeIdToConfigNodeId.count(staIndex)
                                       ? g_simNodeIdToConfigNodeId[staIndex]
                                       : staIndex;
        uint32_t newApConfigNodeId = g_apSimNodeIdToConfigNodeId.count(newApIndex)
                                         ? g_apSimNodeIdToConfigNodeId[newApIndex]
                                         : newApIndex;

        std::ostringstream bssidStr;
        bssidStr << apBssid;
        g_simEventProducer->RecordAssociation(staConfigNodeId, newApConfigNodeId, bssidStr.str());

        // If roaming, also record the roaming event
        if (isRoaming)
        {
            uint32_t oldApConfigNodeId = g_apSimNodeIdToConfigNodeId.count(oldApIndex)
                                             ? g_apSimNodeIdToConfigNodeId[oldApIndex]
                                             : oldApIndex;
            g_simEventProducer->RecordClientRoamed(staConfigNodeId, oldApConfigNodeId, newApConfigNodeId);
        }
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

    std::cout << "[DEASSOC] " << std::fixed << std::setprecision(2)
                 << GetDisplayTimeSeconds() << "s: "
                 << "STA " << staMac << " (idx=" << staIndex << ") "
                 << "DEASSOCIATED from AP " << apBssid << " (node=" << oldApIndex << ")" << std::endl;

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

    // Mark STA for app restart after it re-associates
    // This ensures traffic is reinstalled with fresh socket after roaming
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

        // Get VI environment for non-WiFi queries
        auto viEnv = g_virtualInterfererEnabled ? VirtualInterfererEnvironment::Get() : nullptr;
        bool viActive = viEnv && !VirtualInterfererEnvironment::IsBeingDestroyed();

        // Get AP position for VI queries
        Vector apPos(0, 0, 0);
        if (viActive) {
            Ptr<Node> node = NodeList::GetNode(nodeId);
            if (node) {
                Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
                if (mobility) {
                    apPos = mobility->GetPosition();
                }
            }
        }

        // Populate scanningChannelData for all channels
        for (uint8_t channel : allChannels) {
            ChannelScanData& scanData = apMetrics.scanningChannelData[channel];

            // Check if this channel has beacon data (APs exist on this channel)
            auto beaconIt = channelBeacons.find(channel);
            bool hasApOnChannel = (beaconIt != channelBeacons.end());

            // Get VI non-WiFi for this channel
            double viNonWifi = 0.0;
            if (viActive) {
                InterferenceEffect effect = viEnv->GetAggregateEffect(apPos, channel);
                viNonWifi = effect.nonWifiCcaPercent / 100.0;  // Convert to 0-1 scale
            }

            // Set CCA utilization based on channel type
            if (channel == apMetrics.channel) {
                // Operating channel - use live CCA data (already 0-1 scale, already has VI injected)
                scanData.channelUtilization = apMetrics.channelUtilization;
                scanData.wifiChannelUtilization = apMetrics.wifiChannelUtilization;
                scanData.nonWifiChannelUtilization = apMetrics.nonWifiChannelUtilization;
            } else if (hasApOnChannel) {
                // Channel with APs - MUST use g_channelUtilMap (populated from operating channel CCA)
                // Every AP on this channel updates g_channelUtilMap via OnChannelUtilization callback
                // This data already includes VI effects and proportional scaling
                auto utilIt = g_channelUtilMap.find(channel);
                if (utilIt != g_channelUtilMap.end()) {
                    // Use global map (0-100% scale, already has VI + proportional scaling), convert to 0-1
                    scanData.channelUtilization = utilIt->second.channelUtil / 100.0;
                    scanData.wifiChannelUtilization = utilIt->second.wifiUtil / 100.0;
                    scanData.nonWifiChannelUtilization = utilIt->second.nonWifiUtil / 100.0;
                } else {
                    // This should NEVER happen - if hasApOnChannel is true, the channel MUST be in g_channelUtilMap
                    // Log error and use zero values as fallback
                    std::cerr << "[ERROR] Channel " << (int)channel << " has APs but not in g_channelUtilMap!" << std::endl;
                    scanData.channelUtilization = 0.0;
                    scanData.wifiChannelUtilization = 0.0;
                    scanData.nonWifiChannelUtilization = viNonWifi;  // At least use VI if available
                }
            } else {
                // Empty channel - random small wifi util (0-5%), VI for non-wifi
                // Apply proportional scaling here too
                double randomWifiUtil = randWifi->GetValue();  // 0-0.05
                double effectiveWifi = randomWifiUtil;
                double effectiveNonWifi = viNonWifi;

                if (viNonWifi > 0.0 && randomWifiUtil > 0.0) {
                    double availableSpace = std::max(0.0, 1.0 - viNonWifi);
                    if (availableSpace < randomWifiUtil) {
                        effectiveWifi = availableSpace;
                    }
                }

                scanData.wifiChannelUtilization = effectiveWifi;
                scanData.nonWifiChannelUtilization = effectiveNonWifi;
                scanData.channelUtilization = std::min(1.0, effectiveWifi + effectiveNonWifi);
            }

            // Populate neighbor data from beacons
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

                    // Use g_channelUtilMap for utilization (more accurate than beacon)
                    // Note: ChannelNeighborInfo uses uint8_t (0-255 scale), g_channelUtilMap uses 0-100%
                    auto neighborUtilIt = g_channelUtilMap.find(beaconInfo.channel);
                    if (neighborUtilIt != g_channelUtilMap.end()) {
                        // Convert 0-100% to 0-255 scale for uint8_t storage
                        neighborInfo.channelUtil = static_cast<uint8_t>(std::min(255.0, neighborUtilIt->second.channelUtil * 2.55));
                        neighborInfo.wifiUtil = static_cast<uint8_t>(std::min(255.0, neighborUtilIt->second.wifiUtil * 2.55));
                        neighborInfo.nonWifiUtil = static_cast<uint8_t>(std::min(255.0, neighborUtilIt->second.nonWifiUtil * 2.55));
                    } else {
                        // Fallback: beacon data (already 0-255 scale) + VI
                        neighborInfo.channelUtil = beaconInfo.channelUtilization;
                        neighborInfo.wifiUtil = beaconInfo.wifiUtilization;
                        // Query VI for neighbor's channel (VI returns 0-100%, convert to 0-255)
                        double neighborViNonWifi = 0.0;
                        if (viActive) {
                            InterferenceEffect effect = viEnv->GetAggregateEffect(apPos, beaconInfo.channel);
                            neighborViNonWifi = effect.nonWifiCcaPercent;  // 0-100%
                        }
                        neighborInfo.nonWifiUtil = static_cast<uint8_t>(std::min(255.0, neighborViNonWifi * 2.55));
                    }
                    scanData.neighbors.push_back(neighborInfo);
                }
            } else {
                scanData.bssidCount = 0;
                scanData.neighbors.clear();
            }
        }

        // Debug: Print scan data for this AP (channels with activity)
        if (g_verboseLogging && nodeId <= 2) {  // Only first few APs for brevity
            std::cout << "[ScanData] AP node=" << nodeId << " ch=" << (int)apMetrics.channel
                         << " | Scanned: " << apMetrics.scanningChannelData.size() << " channels" << std::endl;
            for (const auto& [scanCh, scanData] : apMetrics.scanningChannelData) {
                // Show: operating channel, channels with neighbors, channels with any utilization
                if (scanCh == apMetrics.channel || scanData.bssidCount > 0 || scanData.channelUtilization > 0.001) {
                    std::cout << "  [Ch " << (int)scanCh << "] util=" << std::fixed << std::setprecision(1)
                              << (scanData.channelUtilization * 100.0) << "% wifi="
                              << (scanData.wifiChannelUtilization * 100.0) << "% nonWifi="
                              << (scanData.nonWifiChannelUtilization * 100.0) << "% neighbors="
                              << scanData.bssidCount << std::endl;
                }
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
    Time now = Simulator::Now();

    // Debounce: prevent multiple rapid scoring runs
    if (now - g_lastScoringRun < g_scoringDebounce && g_lastScoringRun > Seconds(0)) {
        // Skip this run, too soon after last scoring
        return;
    }
    g_lastScoringRun = now;

    // First, update scan data
    ProcessBeaconMeasurements();

    if (g_verboseLogging) {
        std::cout << "========== CHANNEL SCORING (t=" << GetDisplayTimeSeconds()
                     << "s) ==========" << std::endl;
        std::cout << "Mode: SEQUENTIAL ALLOCATION (reactive trigger)" << std::endl;
    }

    // Sequential allocation: track channels already allocated
    std::set<uint8_t> allocatedChannels;

    for (const auto& [nodeId, apMetrics] : g_apMetrics) {
        if (g_verboseLogging) {
            std::cout << "[AP " << nodeId << "] " << apMetrics.bssid
                         << " (Current Ch " << (int)apMetrics.channel << "):" << std::endl;
        }

        // Check if this AP is in cooldown (recently switched, beacon cache not yet refreshed)
        auto lastSwitchIt = g_lastChannelSwitch.find(nodeId);
        if (lastSwitchIt != g_lastChannelSwitch.end()) {
            Time timeSinceSwitch = now - lastSwitchIt->second;
            if (timeSinceSwitch < g_channelSwitchCooldown) {
                if (g_verboseLogging) {
                    double remaining = (g_channelSwitchCooldown - timeSinceSwitch).GetSeconds();
                    std::cout << "  [COOLDOWN] Recently switched, waiting " << std::fixed
                                 << std::setprecision(1) << remaining << "s for beacon cache refresh - skipping" << std::endl;
                }
                allocatedChannels.insert(apMetrics.channel);  // Reserve current channel
                continue;
            }
        }

        // Calculate scores (filtered by RL width bounds)
        auto scores = g_channelScoringHelper.CalculateScoresForRlWidth(nodeId, apMetrics.scanningChannelData);

        if (scores.empty()) {
            if (g_verboseLogging) {
                std::cout << "  No scan data available - skipping" << std::endl;
            }
            continue;
        }

        if (g_verboseLogging) {
            // Print ALL scores for channels in AP's band (compact format)
            std::string bandName = (apMetrics.band == WIFI_PHY_BAND_2_4GHZ) ? "2.4GHz" : "5GHz";
            std::cout << "  " << bandName << " Channel Scores (sorted best->worst):" << std::endl;

            int bandRank = 0;
            for (const auto& score : scores) {
                // Only show channels in this AP's band
                if (!ChannelScoringHelper::IsChannelInBand(score.channel, apMetrics.band)) continue;
                bandRank++;

                std::ostringstream scoreLog;
                scoreLog << std::fixed << std::setprecision(1);
                scoreLog << "    Ch " << std::setw(3) << (int)score.channel
                    << " | Score:" << std::setw(6) << score.totalScore
                    << " (B:" << std::setw(5) << score.bssidScore
                    << " R:" << std::setw(5) << score.rssiScore
                    << " N:" << std::setw(5) << score.nonWifiScore
                    << " O:" << std::setw(5) << score.overlapScore << ")";

                if (score.discarded) scoreLog << " [DISC]";
                if (score.channel == apMetrics.channel) scoreLog << " [CUR]";
                if (allocatedChannels.count(score.channel) > 0) scoreLog << " [ALLOC]";
                if (g_globalRanking.dfsBlacklist.count(score.channel) > 0) scoreLog << " [DFS-BL]";

                std::cout << scoreLog.str() << std::endl;
            }
            std::cout << "  Total " << bandName << " channels: " << bandRank << std::endl;
        }

        // Find best available channel (excluding blacklist and already allocated)
        uint8_t bestCh = 0;
        for (const auto& score : scores) {
            if (score.discarded) continue;
            if (!ChannelScoringHelper::IsChannelInBand(score.channel, apMetrics.band)) continue;
            if (allocatedChannels.count(score.channel) > 0) continue;
            if (g_globalRanking.dfsBlacklist.count(score.channel) > 0) continue;  // Skip blacklisted
            bestCh = score.channel;
            break;
        }

        if (bestCh != 0) {
            allocatedChannels.insert(bestCh);

            if (bestCh != apMetrics.channel) {
                if (g_verboseLogging) {
                    std::cout << "  -> SWITCHING: Ch " << (int)apMetrics.channel << " -> Ch " << (int)bestCh << std::endl;
                }

                // Schedule channel switch directly via ApWifiMac (staggered)
                Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(nodeId));
                if (apDev) {
                    uint32_t capturedNodeId = nodeId;
                    uint8_t capturedBestCh = bestCh;
                    uint32_t delayMs = 100 + (nodeId * 50);  // Stagger switches
                    Ptr<WifiNetDevice> capturedDev = apDev;

                    Simulator::Schedule(MilliSeconds(delayMs), [capturedDev, capturedNodeId, capturedBestCh]() {
                        Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(capturedDev->GetMac());
                        if (apMac) {
                            // Get old channel before switching
                            uint8_t oldChannel = 0;
                            auto metricsIt = g_apMetrics.find(capturedNodeId);
                            if (metricsIt != g_apMetrics.end()) {
                                oldChannel = metricsIt->second.channel;
                            }

                            apMac->SwitchChannel(capturedBestCh);

                            // Record channel switch event
                            if (g_simEventProducer && oldChannel != 0) {
                                uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(capturedNodeId)
                                                              ? g_apSimNodeIdToConfigNodeId[capturedNodeId]
                                                              : capturedNodeId;
                                g_simEventProducer->RecordChannelSwitch(apConfigNodeId, oldChannel, capturedBestCh);
                            }

                            // Update g_apMetrics
                            if (metricsIt != g_apMetrics.end()) {
                                metricsIt->second.channel = capturedBestCh;
                                metricsIt->second.band = (capturedBestCh >= 36) ? WIFI_PHY_BAND_5GHZ : WIFI_PHY_BAND_2_4GHZ;
                                // Mark switch time for cooldown (wait for beacon cache refresh)
                                g_lastChannelSwitch[capturedNodeId] = Simulator::Now();
                                if (g_verboseLogging) {
                                    std::cout << "[CHANNEL-SWITCH] Node " << capturedNodeId
                                                 << " switched to Ch " << (int)capturedBestCh
                                                 << " (cooldown: " << g_channelSwitchCooldown.GetSeconds() << "s)" << std::endl;
                                }
                            }
                        }
                    });
                }
            } else if (g_verboseLogging) {
                std::cout << "  -> KEEPING: Ch " << (int)bestCh << " (already optimal)" << std::endl;
            }
        } else if (g_verboseLogging) {
            std::cout << "  -> WARNING: No suitable channel found!" << std::endl;
        }
    }

    // Update global channel ranking for backup selection
    g_globalRanking.ranked5GHz.clear();
    g_globalRanking.ranked24GHz.clear();

    for (const auto& [nodeId, apMetrics] : g_apMetrics) {
        if (apMetrics.scanningChannelData.empty()) continue;

        auto scores = g_channelScoringHelper.CalculateScoresForRlWidth(nodeId, apMetrics.scanningChannelData);
        for (const auto& score : scores) {
            if (score.discarded) continue;
            if (g_globalRanking.dfsBlacklist.count(score.channel) > 0) continue;

            if (ChannelScoringHelper::IsChannelInBand(score.channel, WIFI_PHY_BAND_5GHZ)) {
                if (std::find(g_globalRanking.ranked5GHz.begin(), g_globalRanking.ranked5GHz.end(), score.channel)
                    == g_globalRanking.ranked5GHz.end()) {
                    g_globalRanking.ranked5GHz.push_back(score.channel);
                }
            } else if (ChannelScoringHelper::IsChannelInBand(score.channel, WIFI_PHY_BAND_2_4GHZ)) {
                if (std::find(g_globalRanking.ranked24GHz.begin(), g_globalRanking.ranked24GHz.end(), score.channel)
                    == g_globalRanking.ranked24GHz.end()) {
                    g_globalRanking.ranked24GHz.push_back(score.channel);
                }
            }
        }
        break;  // Use first AP's scoring for global ranking
    }

    g_globalRanking.lastUpdate = Simulator::Now();

    // Check for expired DFS blacklist entries
    CheckDfsBlacklistExpiry();

    // Summary
    if (g_verboseLogging) {
        {
            std::ostringstream allocLog;
            allocLog << "---------- ALLOCATION SUMMARY ---------- Allocated channels: ";
            for (uint8_t ch : allocatedChannels) allocLog << (int)ch << " ";
            std::cout << allocLog.str() << std::endl;
        }

        {
            std::ostringstream rankLog;
            rankLog << "Global Ranking: 5GHz: ";
            for (uint8_t ch : g_globalRanking.ranked5GHz) rankLog << (int)ch << " ";
            rankLog << "| 2.4GHz: ";
            for (uint8_t ch : g_globalRanking.ranked24GHz) rankLog << (int)ch << " ";
            if (!g_globalRanking.dfsBlacklist.empty()) {
                rankLog << "| DFS Blacklist: ";
                for (uint8_t ch : g_globalRanking.dfsBlacklist) rankLog << (int)ch << " ";
            }
            std::cout << rankLog.str() << std::endl;
        }

        std::cout << "==========================================================" << std::endl;
    }

    // NOTE: No periodic rescheduling - reactive only (triggered by non-WiFi threshold or DFS)
}

/**
 * Per-AP channel scoring - triggered when a SPECIFIC AP's non-WiFi exceeds threshold
 * Only scores and potentially switches the triggered AP, leaving other APs undisturbed
 */
void
PerformChannelScoringForAp(uint32_t triggeredNodeId)
{
    Time now = Simulator::Now();

    // Note: Non-WiFi trigger cooldown is already checked by the caller (OnChannelUtilization)
    // We only check channel switch cooldown here

    // Check if AP is in channel switch cooldown
    auto lastSwitchIt = g_lastChannelSwitch.find(triggeredNodeId);
    if (lastSwitchIt != g_lastChannelSwitch.end()) {
        Time timeSinceSwitch = now - lastSwitchIt->second;
        if (timeSinceSwitch < g_channelSwitchCooldown) {
            if (g_verboseLogging) {
                std::cout << "[AP-SCORING] AP " << triggeredNodeId
                             << " in cooldown (beacon cache refresh) - skipping" << std::endl;
            }
            return;
        }
    }

    // Find the AP in g_apMetrics
    auto apIt = g_apMetrics.find(triggeredNodeId);
    if (apIt == g_apMetrics.end()) {
        if (g_verboseLogging) {
            std::cout << "[AP-SCORING] AP " << triggeredNodeId << " not found - skipping" << std::endl;
        }
        return;
    }

    // Update scan data for this AP
    ProcessBeaconMeasurements();

    ApMetrics& apMetrics = apIt->second;
    std::string bandName = (apMetrics.band == WIFI_PHY_BAND_2_4GHZ) ? "2.4GHz" : "5GHz";

    if (g_verboseLogging) {
        std::cout << "========== PER-AP CHANNEL SCORING (t=" << GetDisplayTimeSeconds()
                     << "s) ==========" << std::endl;
        std::cout << "[AP " << triggeredNodeId << "] " << apMetrics.bssid
                     << " (Current Ch " << (int)apMetrics.channel << " - " << bandName << ")" << std::endl;
        std::cout << "Trigger: Non-WiFi threshold exceeded - scoring ONLY this AP" << std::endl;
    }

    // Calculate scores for this AP (filtered by RL width bounds)
    auto scores = g_channelScoringHelper.CalculateScoresForRlWidth(triggeredNodeId, apMetrics.scanningChannelData);

    if (scores.empty()) {
        if (g_verboseLogging) {
            std::cout << "  No scan data available - skipping" << std::endl;
        }
        return;
    }

    if (g_verboseLogging) {
        // Print ALL scores for channels in AP's band
        std::cout << "  " << bandName << " Channel Scores (sorted best->worst):" << std::endl;

        int bandRank = 0;
        for (const auto& score : scores) {
            if (!ChannelScoringHelper::IsChannelInBand(score.channel, apMetrics.band)) continue;
            bandRank++;

            std::ostringstream scoreLog;
            scoreLog << std::fixed << std::setprecision(1);
            scoreLog << "    Ch " << std::setw(3) << (int)score.channel
                << " | Score:" << std::setw(6) << score.totalScore
                << " (B:" << std::setw(5) << score.bssidScore
                << " R:" << std::setw(5) << score.rssiScore
                << " N:" << std::setw(5) << score.nonWifiScore
                << " O:" << std::setw(5) << score.overlapScore << ")";

            if (score.discarded) scoreLog << " [DISC]";
            if (score.channel == apMetrics.channel) scoreLog << " [CUR]";
            if (g_globalRanking.dfsBlacklist.count(score.channel) > 0) scoreLog << " [DFS-BL]";
            if (g_pendingAllocations.count(score.channel) > 0) scoreLog << " [ALLOC]";

            std::cout << scoreLog.str() << std::endl;
        }
        std::cout << "  Total " << bandName << " channels: " << bandRank << std::endl;
    }

    // Find best available channel for THIS AP
    // Note: We allow selecting a channel that's already allocated (another AP may be there)
    // The [ALLOC] tag is informational - APs CAN share channels if that's the best option
    // We only hard-block: discarded channels and DFS blacklisted channels
    uint8_t bestCh = 0;
    for (const auto& score : scores) {
        if (score.discarded) continue;
        if (!ChannelScoringHelper::IsChannelInBand(score.channel, apMetrics.band)) continue;
        if (g_globalRanking.dfsBlacklist.count(score.channel) > 0) continue;
        bestCh = score.channel;
        break;
    }

    if (bestCh != 0 && bestCh != apMetrics.channel) {
        if (g_verboseLogging) {
            std::cout << "  -> SWITCHING: Ch " << (int)apMetrics.channel << " -> Ch " << (int)bestCh << std::endl;
        }

        // Mark this channel as allocated for subsequent APs in the queue
        g_pendingAllocations.insert(bestCh);

        // Schedule channel switch
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(triggeredNodeId));
        if (apDev) {
            uint32_t capturedNodeId = triggeredNodeId;
            uint8_t capturedBestCh = bestCh;
            Ptr<WifiNetDevice> capturedDev = apDev;

            Simulator::Schedule(MilliSeconds(100), [capturedDev, capturedNodeId, capturedBestCh]() {
                Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(capturedDev->GetMac());
                if (apMac) {
                    // Get old channel before switching
                    uint8_t oldChannel = 0;
                    auto metricsIt = g_apMetrics.find(capturedNodeId);
                    if (metricsIt != g_apMetrics.end()) {
                        oldChannel = metricsIt->second.channel;
                    }

                    apMac->SwitchChannel(capturedBestCh);

                    // Record channel switch event
                    if (g_simEventProducer && oldChannel != 0) {
                        uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(capturedNodeId)
                                                      ? g_apSimNodeIdToConfigNodeId[capturedNodeId]
                                                      : capturedNodeId;
                        g_simEventProducer->RecordChannelSwitch(apConfigNodeId, oldChannel, capturedBestCh);
                    }

                    if (metricsIt != g_apMetrics.end()) {
                        metricsIt->second.channel = capturedBestCh;
                        metricsIt->second.band = (capturedBestCh >= 36) ? WIFI_PHY_BAND_5GHZ : WIFI_PHY_BAND_2_4GHZ;
                        g_lastChannelSwitch[capturedNodeId] = Simulator::Now();
                        if (g_verboseLogging) {
                            std::cout << "[CHANNEL-SWITCH] Node " << capturedNodeId
                                         << " switched to Ch " << (int)capturedBestCh
                                         << " (cooldown: " << g_channelSwitchCooldown.GetSeconds() << "s)" << std::endl;
                        }
                    }
                }
            });
        }
    } else if (bestCh == apMetrics.channel) {
        if (g_verboseLogging) {
            std::cout << "  -> STAYING on current channel (already best)" << std::endl;
        }
        // Also mark current channel as allocated
        g_pendingAllocations.insert(apMetrics.channel);
    } else if (g_verboseLogging) {
        std::cout << "  -> NO suitable channel found" << std::endl;
    }

    if (g_verboseLogging) {
        std::cout << "==========================================================" << std::endl;
    }
}

/**
 * Process the AP scoring queue one AP at a time
 * This ensures channels are allocated sequentially to avoid conflicts
 */
void
ProcessApScoringQueue()
{
    if (g_scoringQueueProcessing) {
        return;  // Already processing
    }

    if (g_apScoringQueue.empty()) {
        // Queue empty - clear pending allocations for next batch
        g_pendingAllocations.clear();
        return;
    }

    g_scoringQueueProcessing = true;

    // Process one AP
    uint32_t nodeId = g_apScoringQueue.front();
    g_apScoringQueue.pop();

    // Score this AP (will use g_pendingAllocations to avoid conflicts)
    PerformChannelScoringForAp(nodeId);

    g_scoringQueueProcessing = false;

    // Process next AP in queue (if any) after a small delay
    if (!g_apScoringQueue.empty()) {
        Simulator::Schedule(MilliSeconds(50), &ProcessApScoringQueue);
    } else {
        // Queue empty - clear pending allocations
        if (g_verboseLogging) {
            std::cout << "[QUEUE] All APs processed, clearing pending allocations" << std::endl;
        }
        g_pendingAllocations.clear();
    }
}

/**
 * Add an AP to the scoring queue
 * If queue was empty, start processing
 */
void
QueueApForScoring(uint32_t nodeId)
{
    // Check if this AP is already in the queue
    std::queue<uint32_t> tempQueue = g_apScoringQueue;
    while (!tempQueue.empty()) {
        if (tempQueue.front() == nodeId) {
            if (g_verboseLogging) {
                std::cout << "[QUEUE] AP " << nodeId << " already in queue - skipping" << std::endl;
            }
            return;  // Already queued
        }
        tempQueue.pop();
    }

    bool wasEmpty = g_apScoringQueue.empty();
    g_apScoringQueue.push(nodeId);
    if (g_verboseLogging) {
        std::cout << "[QUEUE] AP " << nodeId << " added to scoring queue (size: "
                     << g_apScoringQueue.size() << ")" << std::endl;
    }

    // If queue was empty, start processing
    if (wasEmpty && !g_scoringQueueProcessing) {
        Simulator::ScheduleNow(&ProcessApScoringQueue);
    }
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

        // Check for flow ID change (happens when STA roams to different AP)
        bool flowIdChanged = false;
        if (history.hasFlowId && history.lastFlowId != flowId)
        {
            // Flow ID changed - reset prev values to avoid delta spikes
            NS_LOG_DEBUG("Flow ID changed for " << staIpStr << ": "
                         << history.lastFlowId << " -> " << flowId << ", resetting history");
            history.prevUplinkBytes = fs.rxBytes;
            history.prevUplinkTxPackets = fs.txPackets;
            history.prevUplinkRxPackets = fs.rxPackets;
            history.prevUplinkDelaySum = fs.delaySum.GetSeconds() * 1000.0;
            history.prevUplinkJitterSum = fs.jitterSum.GetSeconds() * 1000.0;
            flowIdChanged = true;  // Skip EWMA this interval, but continue processing
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

        // Apply EWMA smoothing only after EWMA_START_TIME and if flow ID didn't change
        double nowSec = Simulator::Now().GetSeconds();
        if (nowSec >= EWMA_START_TIME && !flowIdChanged)
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
    // NOTE: bytesSent/bytesReceived are set from PHY-level traces in OnChannelUtilization callback
    // Do NOT reset them here - they contain accurate over-the-air byte counts
    for (auto& apEntry : g_apMetrics)
    {
        apEntry.second.connectionMetrics.clear();
        // Throughput is recalculated from connection data below
        apEntry.second.throughputMbps = 0.0;
    }

    // First pass: Accumulate bytes and throughput for ALL flows (even before EWMA init)
    for (const auto& [staIpStr, flowData] : connectionFlows)
    {
        uint32_t apNodeId = flowData.apIndex;
        if (g_apMetrics.find(apNodeId) != g_apMetrics.end())
        {
            ApMetrics& ap = g_apMetrics[apNodeId];

            // Calculate instantaneous throughput for this connection
            auto histIt = g_connectionHistory.find(staIpStr);
            if (histIt != g_connectionHistory.end() && histIt->second.initialized)
            {
                const ConnectionHistory& history = histIt->second;
                // Use delta bytes (not cumulative) for AP bytesReceived
                uint64_t deltaBytes = (flowData.uplinkBytes >= history.prevUplinkBytes)
                    ? (flowData.uplinkBytes - history.prevUplinkBytes)
                    : flowData.uplinkBytes;
                ap.bytesReceived += deltaBytes;
                ap.throughputMbps += (history.ewmaUplinkThroughput + history.ewmaUplinkThroughput * 1.1) * g_throughputScaleFactor; // uplink + approx downlink, scaled
            }
        }
    }

    // Second pass: Build ConnectionMetrics for flows with initialized EWMA
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

        // Throughput from EWMA (delta-based, smoothed), scaled by g_throughputScaleFactor
        conn.uplinkThroughputMbps = history.ewmaUplinkThroughput * g_throughputScaleFactor;
        // Hardcode downlink to ±10% of uplink (no actual downlink traffic in this sim)
        static Ptr<UniformRandomVariable> dlVariationRv = CreateObject<UniformRandomVariable>();
        dlVariationRv->SetAttribute("Min", DoubleValue(-0.10));
        dlVariationRv->SetAttribute("Max", DoubleValue(0.10));
        double variation = dlVariationRv->GetValue();
        conn.downlinkThroughputMbps = history.ewmaUplinkThroughput * (1.0 + variation) * g_throughputScaleFactor;

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
                // apBssid already set earlier in this function

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
        uint32_t apNodeId = flowData.apIndex;
        if (g_apMetrics.find(apNodeId) != g_apMetrics.end())
        {
            ApMetrics& ap = g_apMetrics[apNodeId];
            ap.connectionMetrics[connKey] = conn;
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
    // Also ensure all associated STAs appear in connectionMetrics (even with 100% loss / stale flows)
    for (uint32_t apIndex = 0; apIndex < g_apWifiDevices.GetN(); apIndex++)
    {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(apIndex));
        if (!apDev) continue;

        Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDev->GetMac());
        if (!apMac) continue;

        Mac48Address apBssid = apMac->GetAddress();

        // Get actual count from MAC layer (link 0 for single-link)
        const auto& staList = apMac->GetStaList(0);
        g_apMetrics[apIndex].associatedClients = staList.size();

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

    // Sync scanning data with ACI/OFDMA-adjusted channel utilization values
    // This ensures scanning data for operating channel matches top-level AP metrics
    for (auto& [nodeId, ap] : g_apMetrics)
    {
        uint8_t opChannel = ap.channel;
        if (ap.scanningChannelData.find(opChannel) != ap.scanningChannelData.end())
        {
            ap.scanningChannelData[opChannel].channelUtilization = ap.channelUtilization;
            ap.scanningChannelData[opChannel].wifiChannelUtilization = ap.wifiChannelUtilization;
            ap.scanningChannelData[opChannel].nonWifiChannelUtilization = ap.nonWifiChannelUtilization;
        }
        // Also update g_channelUtilMap for cross-AP consistency
        g_channelUtilMap[opChannel].channelUtil = ap.channelUtilization * 100.0;
        g_channelUtilMap[opChannel].wifiUtil = ap.wifiChannelUtilization * 100.0;
        g_channelUtilMap[opChannel].nonWifiUtil = ap.nonWifiChannelUtilization * 100.0;
    }

    // Update lastUpdate timestamp for all APs before sending to Kafka
    for (auto& [nodeId, ap] : g_apMetrics)
    {
        ap.lastUpdate = Simulator::Now();
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

    for (const auto& [nodeId, metrics] : g_apMetrics)
    {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "[AP] id=" << metrics.nodeId
                  << " bssid=" << metrics.bssid
                  << " sim=" << GetDisplayTimeSeconds() << "s"
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
 * Injects VI non-WiFi effects into channel utilization
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

        // Values from WifiCcaMonitor are already normalized with proportional scaling
        // (VI effects included, capped at 100%, wifi + nonWifi = total)

        // Store in metrics (as 0-1 scale for ApMetrics)
        ap.channelUtilization = totalUtil / 100.0;
        ap.wifiChannelUtilization = wifiUtil / 100.0;
        ap.nonWifiChannelUtilization = nonWifiUtil / 100.0;
        ap.bytesSent = bytesSent;
        ap.bytesReceived = bytesReceived;

        // Update global channel util map
        ChannelUtilData& utilData = g_channelUtilMap[ap.channel];
        utilData.channelUtil = totalUtil;
        utilData.wifiUtil = wifiUtil;
        utilData.nonWifiUtil = nonWifiUtil;

        // Convert 0-100% to 0-255 scale for BSS Load IE (IEEE 802.11)
        uint8_t channelUtilScaled = static_cast<uint8_t>(std::min(255.0, (totalUtil * 255.0) / 100.0));
        uint8_t wifiUtilScaled = static_cast<uint8_t>(std::min(255.0, (wifiUtil * 255.0) / 100.0));
        uint8_t nonWifiUtilScaled = static_cast<uint8_t>(std::min(255.0, (nonWifiUtil * 255.0) / 100.0));

        // Set BSS Load IE values via ApWifiMac static methods
        ApWifiMac::SetChannelUtilization(ap.bssid, channelUtilScaled);
        ApWifiMac::SetWifiChannelUtilization(ap.bssid, wifiUtilScaled);
        ApWifiMac::SetNonWifiChannelUtilization(ap.bssid, nonWifiUtilScaled);

        // Non-WiFi threshold trigger: initiate channel scoring if threshold exceeded
        if (g_channelScoringEnabled && nonWifiUtil > g_nonWifiThreshold)
        {
            Time now = Simulator::Now();
            bool shouldTrigger = true;

            // Check channel switch cooldown - AP must wait for beacon cache refresh after switch
            auto lastSwitchIt = g_lastChannelSwitch.find(nodeId);
            if (lastSwitchIt != g_lastChannelSwitch.end())
            {
                Time timeSinceSwitch = now - lastSwitchIt->second;
                if (timeSinceSwitch < g_channelSwitchCooldown)
                {
                    if (g_verboseLogging) {
                        double remaining = (g_channelSwitchCooldown - timeSinceSwitch).GetSeconds();
                        std::cout << "[NON-WIFI-TRIGGER] AP " << nodeId << " (Ch " << (int)ap.channel
                                     << ") non-WiFi=" << std::fixed << std::setprecision(1) << nonWifiUtil
                                     << "% > threshold, but in cooldown (switched " << std::setprecision(1)
                                     << timeSinceSwitch.GetSeconds() << "s ago, " << remaining
                                     << "s remaining) - skipping" << std::endl;
                    }
                    shouldTrigger = false;
                }
            }

            if (shouldTrigger)
            {
                if (g_verboseLogging) {
                    std::cout << "[NON-WIFI-TRIGGER] AP " << nodeId << " (Ch " << (int)ap.channel
                                 << ") non-WiFi=" << std::fixed << std::setprecision(1) << nonWifiUtil
                                 << "% > threshold=" << g_nonWifiThreshold << "% -> Queuing for scoring" << std::endl;
                }
                QueueApForScoring(nodeId);
            }
        }
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

// RSSI check interval (every 1 second - faster detection for stable roaming)
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

                if (g_verboseLogging) {
                    std::cout << "[RSSI-ROAM] " << GetDisplayTimeSeconds() << "s: "
                                 << "STA " << staMac << " RSSI=" << std::fixed << std::setprecision(1)
                                 << tracker.ewmaRssi << " dBm < threshold=" << g_rssiThreshold
                                 << " dBm on AP " << apBssid << std::endl;
                }

                // Stagger BSS TM triggers with random delay (0-2 seconds)
                // This prevents thundering herd where all STAs roam to same AP
                Ptr<UniformRandomVariable> randDelay = CreateObject<UniformRandomVariable>();
                randDelay->SetAttribute("Min", DoubleValue(0.0));
                randDelay->SetAttribute("Max", DoubleValue(0.1));  // 0-100ms delay for faster roaming
                double delaySeconds = randDelay->GetValue();

                if (g_verboseLogging) {
                    std::cout << "[RSSI-ROAM]   └─ Scheduling BSS TM in " << std::fixed
                                 << std::setprecision(2) << delaySeconds << "s" << std::endl;
                }

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

    // Get beacons from cache
    std::vector<BeaconInfo> beacons = g_dualPhySniffer.GetBeaconsReceivedBy(staMac);

    // Inject real-time STA counts from actual AP staLists
    // This ensures ranking uses current load, not stale beacon data
    for (auto& beacon : beacons)
    {
        // Find the AP device for this BSSID
        for (uint32_t i = 0; i < g_apWifiDevices.GetN(); i++)
        {
            Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(i));
            if (!apDev) continue;

            Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDev->GetMac());
            if (apMac && apMac->GetAddress() == beacon.bssid)
            {
                // Update staCount and channel with real-time data from AP
                // Ensures beacon cache reflects current AP state after channel switches
                beacon.staCount = apMac->GetStaList(0).size();
                beacon.channel = apDev->GetPhy()->GetChannelNumber();
                break;
            }
        }
    }

    // Filter out current AP and check if any candidates remain
    bool hasBetterCandidate = false;
    for (const auto& beacon : beacons)
    {
        if (beacon.bssid != apBssid)
        {
            hasBetterCandidate = true;
            break;
        }
    }

    if (!hasBetterCandidate)
    {
        if (g_verboseLogging) {
            std::cout << "[RSSI-ROAM]   └─ SKIPPED: No candidate APs in beacon cache" << std::endl;
        }
        return;  // Skip - no better candidates available
    }

    if (g_verboseLogging) {
        std::cout << "[RSSI-ROAM]   └─ SENDING BSS TM Request with " << beacons.size()
                     << " candidate(s)" << std::endl;
    }

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

        // Get target BSSID (first candidate AP)
        std::string targetBssidStr = "";
        for (const auto& beacon : beacons)
        {
            if (beacon.bssid != apBssid)
            {
                std::ostringstream oss;
                oss << beacon.bssid;
                targetBssidStr = oss.str();
                break;
            }
        }

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

    if (g_verboseLogging) {
        std::cout << "[RL COMMAND] Applied params to " << bssid
                    << ": TxPower=" << params.txPowerStartDbm
                    << " CCA=" << params.ccaEdThresholdDbm
                    << " RxSens=" << params.rxSensitivityDbm
                    << " ChannelWidth=" << params.channelWidthMhz << "MHz" << std::endl;
    }

    // Update RACEBOT bounds based on RL command
    // This sets the power and OBSS/PD range that RACEBOT will operate within
    if (g_powerScoringEnabled)
    {
        g_powerScoringHelper.UpdateRlBounds(
            apIndex,
            params.txPowerStartDbm,
            params.channelWidthMhz
        );
    }

    // Update channel scoring bounds based on RL command
    // This sets the channel width constraint that fast loop channel scoring will use
    if (g_channelScoringEnabled)
    {
        g_channelScoringHelper.UpdateRlChannelBounds(
            apIndex,
            params.channelNumber,
            params.channelWidthMhz
        );
    }

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
            g_simEventProducer->RecordPowerSwitch(apConfigNodeId, oldPower, params.txPowerStartDbm, "kafka");
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

        Simulator::Schedule(MilliSeconds(100), [capturedDevice, newChannel, bssid, apIndex]() {
            Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(capturedDevice->GetMac());
            if (apMac)
            {
                apMac->SwitchChannel(newChannel);

                // Update g_apMetrics with new channel and band
                auto it = g_apMetrics.find(apIndex);
                if (it != g_apMetrics.end())
                {
                    it->second.channel = newChannel;
                    it->second.band = (newChannel >= 36) ? WIFI_PHY_BAND_5GHZ : WIFI_PHY_BAND_2_4GHZ;
                }

                if (g_verboseLogging) {
                    std::cout << "Channel switched to " << (uint32_t)newChannel << " for " << bssid
                              << " (band=" << ((newChannel >= 36) ? "5GHz" : "2.4GHz") << ")" << std::endl;
                }
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
    // Return as percentage (0-100) for RACEBOT, matching Kafka output
    return it->second.nonWifiChannelUtilization * 100.0;
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

    if (g_verboseLogging) {
        std::cout << "========== POWER SCORING (t=" << GetDisplayTimeSeconds() << "s) ==========" << std::endl;
    }

    for (const auto& [nodeId, apMetrics] : g_apMetrics)
    {
        auto& state = g_powerScoringHelper.GetOrCreateApState(nodeId, apMetrics.bssColor);
        double nonWifiPercent = GetNonWifiForAp(nodeId);
        double prevPower = state.currentTxPowerDbm;
        double prevGoal = state.goalObsspdDbm;  // Capture old goal before recalculation

        if (g_verboseLogging) {
            std::cout << "--- AP Node " << nodeId << " --- Inputs: BSS_RSSI=" << state.tracker.bssRssiEwma
                         << "dBm, MCS=" << state.tracker.mcsEwma
                         << ", NonWifi=" << std::fixed << std::setprecision(1) << nonWifiPercent << "%" << std::endl;
        }

        PowerResult result = g_powerScoringHelper.CalculatePower(nodeId, nonWifiPercent);

        // Record OBSS-PD goal change event if goal was recalculated
        if (g_simEventProducer && result.goalRecalculated && std::abs(result.goalObsspdDbm - prevGoal) > 0.01)
        {
            uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(nodeId)
                                          ? g_apSimNodeIdToConfigNodeId[nodeId]
                                          : nodeId;
            g_simEventProducer->RecordObssPdGoalChange(apConfigNodeId,
                                                        prevGoal,
                                                        result.goalObsspdDbm,
                                                        "timer1_recalc");
        }

        // Calculate actual delta for logging
        double actualDelta = result.txPowerDbm - prevPower;

        if (g_verboseLogging) {
            // Build timing info string
            std::ostringstream timingInfo;
            timingInfo << std::fixed << std::setprecision(1);
            timingInfo << "  [Timers: t1=";
            if (result.goalRecalculated)
            {
                timingInfo << "RECALC";
            }
            else
            {
                timingInfo << result.timeToNextGoalRecalc << "s";
            }
            timingInfo << ", t2=";
            if (result.inT2Cooldown)
            {
                timingInfo << result.timeRemainingT2Cooldown << "s";
            }
            else
            {
                timingInfo << "ready";
            }
            timingInfo << ", Goal=" << result.goalObsspdDbm << "dBm]";
            std::cout << timingInfo.str() << std::endl;

            if (result.powerChanged)
            {
                std::cout << "  -> POWER CHANGE: " << std::fixed << std::setprecision(1)
                             << prevPower << " -> " << result.txPowerDbm
                             << " dBm (delta=" << std::showpos << actualDelta << std::noshowpos
                             << "dBm, " << result.reason << ")" << std::endl;
            }
            else
            {
                // Show reason even when no significant change (helps debug gradual adjustments)
                std::ostringstream noChangeLog;
                noChangeLog << std::fixed << std::setprecision(1);
                noChangeLog << "  -> No change (TxPower=" << result.txPowerDbm << "dBm";
                if (std::abs(actualDelta) > 0.01)
                {
                    noChangeLog << ", micro-delta=" << std::showpos << actualDelta << std::noshowpos << "dBm";
                }
                noChangeLog << ", " << result.reason << ")";
                std::cout << noChangeLog.str() << std::endl;
            }
        }

        // Always apply TX power to PHY (even small changes) to keep state in sync
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(nodeId));
        if (apDev)
        {
            Ptr<WifiPhy> phy = apDev->GetPhy();
            if (phy)
            {
                phy->SetTxPowerStart(dBm_u{result.txPowerDbm});
                phy->SetTxPowerEnd(dBm_u{result.txPowerDbm});

                // Record power switch event (only for significant changes)
                if (g_simEventProducer && result.powerChanged)
                {
                    uint32_t apConfigNodeId = g_apSimNodeIdToConfigNodeId.count(nodeId)
                                                  ? g_apSimNodeIdToConfigNodeId[nodeId]
                                                  : nodeId;
                    g_simEventProducer->RecordPowerSwitch(apConfigNodeId,
                                                          prevPower,
                                                          result.txPowerDbm,
                                                          result.reason,
                                                          result.obsspdLevelDbm,
                                                          result.goalObsspdDbm,
                                                          result.inNonWifiMode,
                                                          result.goalRecalculated);
                }
            }
        }
        auto metricsIt = g_apMetrics.find(nodeId);
        if (metricsIt != g_apMetrics.end())
        {
            metricsIt->second.txPowerDbm = result.txPowerDbm;
            metricsIt->second.obsspdLevelDbm = result.obsspdLevelDbm;
            metricsIt->second.mcsEwma = state.tracker.mcsEwma;
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

    // Parse display time scaling (for showing sim time as wall clock time)
    if (doc.HasMember("displayTimeScaleFactor"))
    {
        g_displayTimeScaleFactor = doc["displayTimeScaleFactor"].GetDouble();
    }
    if (doc.HasMember("displayTimeOffsetSeconds"))
    {
        g_displayTimeOffsetSeconds = doc["displayTimeOffsetSeconds"].GetDouble();
    }
    // Set global display time factors for simulation events module
    SetEventDisplayTimeScaleFactor(g_displayTimeScaleFactor);
    SetEventDisplayTimeOffsetSeconds(g_displayTimeOffsetSeconds);

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
            std::cout << "Loaded " << g_scanningChannels.size() << " scanning channels from config" << std::endl;
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
        // Trigger and cooldown settings
        config.channelScoring.nonWifiTriggerThreshold = cs.HasMember("nonWifiTriggerThreshold")
            ? cs["nonWifiTriggerThreshold"].GetDouble() : 65.0;
        config.channelScoring.dfsBlacklistDurationSec = cs.HasMember("dfsBlacklistDurationSec")
            ? cs["dfsBlacklistDurationSec"].GetDouble() : 30.0;
        config.channelScoring.channelSwitchCooldownSec = cs.HasMember("channelSwitchCooldownSec")
            ? cs["channelSwitchCooldownSec"].GetDouble() : 15.0;
    }

    // Parse powerScoring configuration
    if (doc.HasMember("powerScoring") && doc["powerScoring"].IsObject())
    {
        const Value& ps = doc["powerScoring"];

        if (ps.HasMember("enabled"))
            config.powerScoring.enabled = ps["enabled"].GetBool();
        if (ps.HasMember("updateInterval"))
            config.powerScoring.updateInterval = ps["updateInterval"].GetDouble();
        if (ps.HasMember("t1IntervalSec"))
        {
            config.powerScoring.t1IntervalSec = ps["t1IntervalSec"].GetDouble();
        }
        if (ps.HasMember("t2IntervalSec"))
        {
            config.powerScoring.t2IntervalSec = ps["t2IntervalSec"].GetDouble();
        }
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
        if (ps.HasMember("rlPowerMarginDbm"))
            config.powerScoring.rlPowerMarginDbm = ps["rlPowerMarginDbm"].GetDouble();

        if (g_verboseLogging) {
            std::cout << "[Config] Power Scoring: " << (config.powerScoring.enabled ? "enabled" : "disabled")
                         << ", interval=" << config.powerScoring.updateInterval << "s"
                         << ", t1=" << config.powerScoring.t1IntervalSec << "s"
                         << ", t2=" << config.powerScoring.t2IntervalSec << "s"
                         << ", rlMargin=" << config.powerScoring.rlPowerMarginDbm << "dBm" << std::endl;
        }
    }

    // Parse virtualInterferers configuration
    if (doc.HasMember("virtualInterferers") && doc["virtualInterferers"].IsObject())
    {
        const Value& vi = doc["virtualInterferers"];
        config.virtualInterferers.enabled = vi.HasMember("enabled") && vi["enabled"].GetBool();

        if (vi.HasMember("updateInterval"))
            config.virtualInterferers.updateInterval = vi["updateInterval"].GetDouble();

        // Helper lambda to parse schedule config
        auto parseSchedule = [](const Value& obj) -> SimulationConfig::ScheduleConfig {
            SimulationConfig::ScheduleConfig sched;
            if (obj.HasMember("schedule") && obj["schedule"].IsObject())
            {
                const Value& s = obj["schedule"];
                sched.hasSchedule = true;
                if (s.HasMember("onDuration"))
                    sched.onDuration = s["onDuration"].GetDouble();
                if (s.HasMember("offDuration"))
                    sched.offDuration = s["offDuration"].GetDouble();
            }
            return sched;
        };

        // Parse generic "interferers" array (type-based format)
        if (vi.HasMember("interferers") && vi["interferers"].IsArray())
        {
            const Value& intArray = vi["interferers"];
            for (SizeType i = 0; i < intArray.Size(); i++)
            {
                const Value& intObj = intArray[i];
                if (!intObj.HasMember("type")) continue;

                std::string type = intObj["type"].GetString();

                if (type == "microwave")
                {
                    SimulationConfig::MicrowaveConfig mwConfig;
                    if (intObj.HasMember("position") && intObj["position"].IsObject())
                    {
                        mwConfig.x = intObj["position"]["x"].GetDouble();
                        mwConfig.y = intObj["position"]["y"].GetDouble();
                        mwConfig.z = intObj["position"].HasMember("z") ? intObj["position"]["z"].GetDouble() : 1.0;
                    }
                    if (intObj.HasMember("txPowerDbm"))
                        mwConfig.txPowerDbm = intObj["txPowerDbm"].GetDouble();
                    if (intObj.HasMember("dutyCycle"))
                        mwConfig.dutyCycle = intObj["dutyCycle"].GetDouble();
                    if (intObj.HasMember("startTime"))
                        mwConfig.startTime = intObj["startTime"].GetDouble();
                    mwConfig.active = !intObj.HasMember("active") || intObj["active"].GetBool();
                    mwConfig.schedule = parseSchedule(intObj);
                    config.virtualInterferers.microwaves.push_back(mwConfig);
                }
                else if (type == "bluetooth")
                {
                    SimulationConfig::BluetoothConfig btConfig;
                    if (intObj.HasMember("position") && intObj["position"].IsObject())
                    {
                        btConfig.x = intObj["position"]["x"].GetDouble();
                        btConfig.y = intObj["position"]["y"].GetDouble();
                        btConfig.z = intObj["position"].HasMember("z") ? intObj["position"]["z"].GetDouble() : 1.0;
                    }
                    if (intObj.HasMember("txPowerDbm"))
                        btConfig.txPowerDbm = intObj["txPowerDbm"].GetDouble();
                    if (intObj.HasMember("profile"))
                        btConfig.profile = intObj["profile"].GetString();
                    if (intObj.HasMember("startTime"))
                        btConfig.startTime = intObj["startTime"].GetDouble();
                    btConfig.active = !intObj.HasMember("active") || intObj["active"].GetBool();
                    btConfig.schedule = parseSchedule(intObj);
                    config.virtualInterferers.bluetooths.push_back(btConfig);
                }
                else if (type == "cordless")
                {
                    SimulationConfig::CordlessConfig cordConfig;
                    if (intObj.HasMember("position") && intObj["position"].IsObject())
                    {
                        cordConfig.x = intObj["position"]["x"].GetDouble();
                        cordConfig.y = intObj["position"]["y"].GetDouble();
                        cordConfig.z = intObj["position"].HasMember("z") ? intObj["position"]["z"].GetDouble() : 1.0;
                    }
                    if (intObj.HasMember("txPowerDbm"))
                        cordConfig.txPowerDbm = intObj["txPowerDbm"].GetDouble();
                    if (intObj.HasMember("numHops"))
                        cordConfig.numHops = intObj["numHops"].GetUint();
                    if (intObj.HasMember("hopInterval"))
                        cordConfig.hopInterval = intObj["hopInterval"].GetDouble();
                    if (intObj.HasMember("bandwidthMhz"))
                        cordConfig.bandwidthMhz = intObj["bandwidthMhz"].GetDouble();
                    if (intObj.HasMember("startTime"))
                        cordConfig.startTime = intObj["startTime"].GetDouble();
                    cordConfig.active = !intObj.HasMember("active") || intObj["active"].GetBool();
                    cordConfig.schedule = parseSchedule(intObj);
                    config.virtualInterferers.cordless.push_back(cordConfig);
                }
                else if (type == "zigbee")
                {
                    SimulationConfig::ZigbeeConfig zbConfig;
                    if (intObj.HasMember("position") && intObj["position"].IsObject())
                    {
                        zbConfig.x = intObj["position"]["x"].GetDouble();
                        zbConfig.y = intObj["position"]["y"].GetDouble();
                        zbConfig.z = intObj["position"].HasMember("z") ? intObj["position"]["z"].GetDouble() : 1.0;
                    }
                    if (intObj.HasMember("txPowerDbm"))
                        zbConfig.txPowerDbm = intObj["txPowerDbm"].GetDouble();
                    if (intObj.HasMember("zigbeeChannel"))
                        zbConfig.zigbeeChannel = static_cast<uint8_t>(intObj["zigbeeChannel"].GetUint());
                    if (intObj.HasMember("networkType"))
                        zbConfig.networkType = intObj["networkType"].GetString();
                    if (intObj.HasMember("dutyCycle"))
                        zbConfig.dutyCycle = intObj["dutyCycle"].GetDouble();
                    if (intObj.HasMember("startTime"))
                        zbConfig.startTime = intObj["startTime"].GetDouble();
                    zbConfig.active = !intObj.HasMember("active") || intObj["active"].GetBool();
                    zbConfig.schedule = parseSchedule(intObj);
                    config.virtualInterferers.zigbees.push_back(zbConfig);
                }
                else if (type == "radar")
                {
                    SimulationConfig::RadarConfig radarConfig;
                    if (intObj.HasMember("position") && intObj["position"].IsObject())
                    {
                        radarConfig.x = intObj["position"]["x"].GetDouble();
                        radarConfig.y = intObj["position"]["y"].GetDouble();
                        radarConfig.z = intObj["position"].HasMember("z") ? intObj["position"]["z"].GetDouble() : 1.0;
                    }
                    if (intObj.HasMember("txPowerDbm"))
                        radarConfig.txPowerDbm = intObj["txPowerDbm"].GetDouble();
                    if (intObj.HasMember("dfsChannel"))
                        radarConfig.dfsChannel = static_cast<uint8_t>(intObj["dfsChannel"].GetUint());
                    if (intObj.HasMember("radarType"))
                        radarConfig.radarType = intObj["radarType"].GetString();
                    if (intObj.HasMember("channelHopping") && intObj["channelHopping"].IsObject())
                    {
                        const Value& ch = intObj["channelHopping"];
                        if (ch.HasMember("dfsChannels") && ch["dfsChannels"].IsArray())
                        {
                            for (SizeType j = 0; j < ch["dfsChannels"].Size(); j++)
                                radarConfig.dfsChannels.push_back(static_cast<uint8_t>(ch["dfsChannels"][j].GetUint()));
                        }
                        if (ch.HasMember("hopIntervalSec"))
                            radarConfig.hopIntervalSec = ch["hopIntervalSec"].GetDouble();
                        if (ch.HasMember("randomHopping"))
                            radarConfig.randomHopping = ch["randomHopping"].GetBool();
                    }
                    if (intObj.HasMember("widebandSpan") && intObj["widebandSpan"].IsObject())
                    {
                        const Value& ws = intObj["widebandSpan"];
                        if (ws.HasMember("spanLength"))
                            radarConfig.spanLength = static_cast<uint8_t>(ws["spanLength"].GetUint());
                        if (ws.HasMember("maxSpanLength"))
                            radarConfig.maxSpanLength = static_cast<uint8_t>(ws["maxSpanLength"].GetUint());
                        if (ws.HasMember("randomSpan"))
                            radarConfig.randomSpan = ws["randomSpan"].GetBool();
                    }
                    if (intObj.HasMember("startTime"))
                        radarConfig.startTime = intObj["startTime"].GetDouble();
                    if (intObj.HasMember("startTimeJitter"))
                        radarConfig.startTimeJitter = intObj["startTimeJitter"].GetDouble();
                    radarConfig.active = !intObj.HasMember("active") || intObj["active"].GetBool();
                    radarConfig.schedule = parseSchedule(intObj);
                    config.virtualInterferers.radars.push_back(radarConfig);
                }
            }
        }

        if (g_verboseLogging && config.virtualInterferers.enabled)
        {
            std::cout << "[Config] Virtual Interferers: enabled with "
                         << config.virtualInterferers.microwaves.size() << " microwave, "
                         << config.virtualInterferers.bluetooths.size() << " Bluetooth, "
                         << config.virtualInterferers.cordless.size() << " cordless, "
                         << config.virtualInterferers.zigbees.size() << " ZigBee, "
                         << config.virtualInterferers.radars.size() << " radar interferers" << std::endl;
        }
    }

    // Parse staChannelHopping configuration
    if (doc.HasMember("staChannelHopping") && doc["staChannelHopping"].IsObject())
    {
        const Value& sch = doc["staChannelHopping"];

        if (sch.HasMember("enabled"))
            config.staChannelHopping.enabled = sch["enabled"].GetBool();
        if (sch.HasMember("scanningDelaySec"))
            config.staChannelHopping.scanningDelaySec = sch["scanningDelaySec"].GetDouble();
        if (sch.HasMember("minimumSnrDb"))
            config.staChannelHopping.minimumSnrDb = sch["minimumSnrDb"].GetDouble();

        if (g_verboseLogging) {
            std::cout << "[Config] STA Channel Hopping: " << (config.staChannelHopping.enabled ? "enabled" : "disabled")
                      << ", delay=" << config.staChannelHopping.scanningDelaySec << "s"
                      << ", minSnr=" << config.staChannelHopping.minimumSnrDb << "dB" << std::endl;
        }
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

        if (g_verboseLogging) {
            std::cout << "[Config] Load Balancing: " << (config.loadBalancing.enabled ? "enabled" : "disabled")
                      << ", threshold=" << config.loadBalancing.channelUtilThreshold << "%"
                      << ", interval=" << config.loadBalancing.intervalSec << "s"
                      << ", cooldown=" << config.loadBalancing.cooldownSec << "s" << std::endl;
        }
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
    // 1) RangePropagationLossModel: Hard cutoff - drops packets beyond MaxRange
    // 2) LogDistancePropagationLossModel: Realistic RSSI for in-range packets
    Config::SetDefault("ns3::RangePropagationLossModel::MaxRange", DoubleValue(15.0));  // 15m cutoff
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
    std::string kafkaDownstreamTopic = "simulator-commands";
    std::string kafkaConsumerGroupId = "ns3-consumer";

    // Setup automatic log file with timestamp
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);
        std::ostringstream filename;
        filename << "basic-sim-logs-" << std::put_time(&tm, "%Y%m%d-%H%M%S") << ".txt";
        g_logFile.open(filename.str(), std::ios::out | std::ios::trunc);
        if (g_logFile.is_open())
        {
            g_origCoutBuf = std::cout.rdbuf();
            g_origCerrBuf = std::cerr.rdbuf();
            g_origClogBuf = std::clog.rdbuf();
            g_teeCout = new TeeStreambuf(g_origCoutBuf, g_logFile.rdbuf());
            g_teeCerr = new TeeStreambuf(g_origCerrBuf, g_logFile.rdbuf());
            g_teeClog = new TeeStreambuf(g_origClogBuf, g_logFile.rdbuf());
            std::cout.rdbuf(g_teeCout);
            std::cerr.rdbuf(g_teeCerr);
            std::clog.rdbuf(g_teeClog);
            std::cout << "=== Logging to: " << filename.str() << " ===" << std::endl;
        }
    }

    CommandLine cmd(__FILE__);
    cmd.AddValue("config", "Path to configuration JSON file", configFile);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.AddValue("logging", "Enable verbose simulation logging (roaming, scoring, etc)", g_verboseLogging);
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
    // 1) Range cutoff (drops packets beyond 20m)
    // 2) LogDistance for realistic RSSI within range
    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::RangePropagationLossModel");  // Hard cutoff first
    wifiChannel.AddPropagationLoss("ns3::LogDistancePropagationLossModel");  // Realistic RSSI
    Ptr<YansWifiChannel> channel = wifiChannel.Create();

    // Setup WiFi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::IdealWifiManager");

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
        wifiPhy.Set("CcaEdThreshold", DoubleValue(config.aps[i].ccaThreshold));

        // Detect band based on channel number (1-14 = 2.4GHz, 36+ = 5GHz)
        bool is2_4GHz = (config.aps[i].channel >= 1 && config.aps[i].channel <= 14);
        WifiPhyBand phyBand = is2_4GHz ? WIFI_PHY_BAND_2_4GHZ : WIFI_PHY_BAND_5GHZ;

        // Set channel using programmatic API
        // IMPORTANT: 2.4GHz requires explicit width=20 to avoid ambiguity (DSSS 22MHz vs OFDM 20/40MHz)
        // 5GHz can use width=0 for auto-detection
        int width = is2_4GHz ? 20 : 0;
        wifiPhy.Set("ChannelSettings",
            StringValue("{" + std::to_string(config.aps[i].channel) + ", " +
                        std::to_string(width) + ", " +
                        (is2_4GHz ? "BAND_2_4GHZ" : "BAND_5GHZ") + ", 0}"));

        wifiMac.SetType("ns3::ApWifiMac",
                        "Ssid", SsidValue(ssid),
                        "BE_MaxAmpduSize", UintegerValue(6500631));  // HE maximum for better aggregation

        NetDeviceContainer apDev = wifi.Install(wifiPhy, wifiMac, apNodes.Get(i));
        apWifiDevices.Add(apDev);

        // Set unique BSS Color for each AP (1-63 valid range, 0 means disabled)
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(apDev.Get(0));
        if (wifiDev)
        {
            Ptr<HeConfiguration> heConfig = wifiDev->GetHeConfiguration();
            if (heConfig)
            {
                // Assign BSS Color: AP0=1, AP1=2, ..., AP6=7, etc. (wrap at 63)
                uint8_t bssColor = static_cast<uint8_t>((i % 63) + 1);
                heConfig->m_bssColor = bssColor;
            }
        }

        // Initialize ApMetrics
        ApMetrics apMetrics;
        apMetrics.nodeId = i;
        apMetrics.bssid = wifiDev->GetMac()->GetAddress();
        apMetrics.channel = config.aps[i].channel;
        apMetrics.band = phyBand;
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
        if (g_verboseLogging) {
            std::cout << "[ACI] Adjacent Channel Interference simulation ENABLED" << std::endl;
        }
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
        if (g_verboseLogging) {
            std::cout << "[OFDMA] Effects simulation ENABLED" << std::endl;
        }
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

        // Apply trigger threshold and cooldown settings from config
        g_nonWifiThreshold = config.channelScoring.nonWifiTriggerThreshold;
        g_dfsBlacklistDuration = Seconds(config.channelScoring.dfsBlacklistDurationSec);
        g_channelSwitchCooldown = Seconds(config.channelScoring.channelSwitchCooldownSec);

        if (g_verboseLogging) {
            std::cout << "[CHANNEL SCORING] Enabled: interval=" << g_channelScoringInterval << "s"
                      << ", nonWifiThreshold=" << g_nonWifiThreshold << "%"
                      << ", dfsBlacklist=" << g_dfsBlacklistDuration.GetSeconds() << "s"
                      << ", switchCooldown=" << g_channelSwitchCooldown.GetSeconds() << "s" << std::endl;
        }
    }
    else
    {
        g_channelScoringEnabled = false;
    }

    // ===== Initialize Virtual Interferers =====
    if (config.virtualInterferers.enabled)
    {
        if (g_verboseLogging) {
            std::cout << "[VirtualInterferer] Setting up virtual interferers..." << std::endl;
        }

        VirtualInterfererEnvironmentConfig envConfig;
        envConfig.updateInterval = Seconds(config.virtualInterferers.updateInterval);
        envConfig.enableNonWifiCca = false;  // Disabled: WifiCcaMonitor handles VI integration
        envConfig.enablePacketLoss = true;
        envConfig.enableDfs = true;
        g_viHelper.SetEnvironmentConfig(envConfig);

        // Random variable for jittering start times
        Ptr<UniformRandomVariable> startJitter = CreateObject<UniformRandomVariable>();

        // Create microwave interferers
        for (const auto& mwConfig : config.virtualInterferers.microwaves)
        {
            auto microwave = CreateObject<MicrowaveInterferer>();
            microwave->SetPosition(Vector(mwConfig.x, mwConfig.y, mwConfig.z));
            microwave->SetTxPowerDbm(mwConfig.txPowerDbm);
            microwave->SetDutyCycle(mwConfig.dutyCycle);
            microwave->TurnOff();  // Start OFF
            microwave->Install();
            // Apply random start jitter
            double jitteredStart = mwConfig.startTime + startJitter->GetValue(0.0, JITTER_MICROWAVE);
            // Schedule TurnOn AND SetSchedule at jittered start time
            bool hasSchedule = mwConfig.schedule.hasSchedule;
            double onDur = mwConfig.schedule.onDuration;
            double offDur = mwConfig.schedule.offDuration;
            Simulator::Schedule(Seconds(jitteredStart), [microwave, hasSchedule, onDur, offDur]() {
                microwave->TurnOn();
                if (hasSchedule) microwave->SetSchedule(Seconds(onDur), Seconds(offDur));
            });
            if (g_verboseLogging) {
                std::cout << "  [VI] Microwave @ (" << mwConfig.x << "," << mwConfig.y << "), txPower="
                             << mwConfig.txPowerDbm << "dBm, startTime=" << std::fixed << std::setprecision(1)
                             << jitteredStart << "s" << std::endl;
            }
        }

        // Create Bluetooth interferers
        for (const auto& btConfig : config.virtualInterferers.bluetooths)
        {
            auto bluetooth = CreateObject<BluetoothInterferer>();
            bluetooth->SetPosition(Vector(btConfig.x, btConfig.y, btConfig.z));
            bluetooth->SetTxPowerDbm(btConfig.txPowerDbm);
            if (btConfig.profile == "HID") bluetooth->SetProfile(BluetoothInterferer::HID);
            else if (btConfig.profile == "AUDIO_STREAMING") bluetooth->SetProfile(BluetoothInterferer::AUDIO_STREAMING);
            else if (btConfig.profile == "DATA_TRANSFER") bluetooth->SetProfile(BluetoothInterferer::DATA_TRANSFER);
            bluetooth->TurnOff();  // Start OFF
            bluetooth->Install();
            // Apply random start jitter
            double jitteredStart = btConfig.startTime + startJitter->GetValue(0.0, JITTER_BLUETOOTH);
            // Schedule TurnOn AND SetSchedule at jittered start time
            bool hasSchedule = btConfig.schedule.hasSchedule;
            double onDur = btConfig.schedule.onDuration;
            double offDur = btConfig.schedule.offDuration;
            Simulator::Schedule(Seconds(jitteredStart), [bluetooth, hasSchedule, onDur, offDur]() {
                bluetooth->TurnOn();
                if (hasSchedule) bluetooth->SetSchedule(Seconds(onDur), Seconds(offDur));
            });
            if (g_verboseLogging) {
                std::cout << "  [VI] Bluetooth @ (" << btConfig.x << "," << btConfig.y << "), profile="
                             << btConfig.profile << ", startTime=" << std::fixed << std::setprecision(1)
                             << jitteredStart << "s" << std::endl;
            }
        }

        // Create cordless phone interferers
        for (const auto& cordConfig : config.virtualInterferers.cordless)
        {
            auto cordless = CreateObject<CordlessInterferer>();
            cordless->SetPosition(Vector(cordConfig.x, cordConfig.y, cordConfig.z));
            cordless->SetTxPowerDbm(cordConfig.txPowerDbm);
            cordless->SetNumHops(cordConfig.numHops);
            cordless->SetHopInterval(cordConfig.hopInterval);
            cordless->SetBandwidthMhz(cordConfig.bandwidthMhz);
            cordless->TurnOff();  // Start OFF
            cordless->Install();
            // Apply random start jitter
            double jitteredStart = cordConfig.startTime + startJitter->GetValue(0.0, JITTER_CORDLESS);
            // Schedule TurnOn AND SetSchedule at jittered start time
            bool hasSchedule = cordConfig.schedule.hasSchedule;
            double onDur = cordConfig.schedule.onDuration;
            double offDur = cordConfig.schedule.offDuration;
            Simulator::Schedule(Seconds(jitteredStart), [cordless, hasSchedule, onDur, offDur]() {
                cordless->TurnOn();
                if (hasSchedule) cordless->SetSchedule(Seconds(onDur), Seconds(offDur));
            });
            if (g_verboseLogging) {
                std::cout << "  [VI] Cordless @ (" << cordConfig.x << "," << cordConfig.y << "), numHops="
                             << cordConfig.numHops << ", startTime=" << std::fixed << std::setprecision(1)
                             << jitteredStart << "s" << std::endl;
            }
        }

        // Create ZigBee interferers
        for (const auto& zbConfig : config.virtualInterferers.zigbees)
        {
            auto zigbee = CreateObject<ZigbeeInterferer>();
            zigbee->SetPosition(Vector(zbConfig.x, zbConfig.y, zbConfig.z));
            zigbee->SetTxPowerDbm(zbConfig.txPowerDbm);
            zigbee->SetZigbeeChannel(zbConfig.zigbeeChannel);
            zigbee->SetDutyCycle(zbConfig.dutyCycle);
            if (zbConfig.networkType == "SENSOR") zigbee->SetNetworkType(ZigbeeInterferer::SENSOR);
            else if (zbConfig.networkType == "CONTROL") zigbee->SetNetworkType(ZigbeeInterferer::CONTROL);
            else if (zbConfig.networkType == "LIGHTING") zigbee->SetNetworkType(ZigbeeInterferer::LIGHTING);
            zigbee->TurnOff();  // Start OFF
            zigbee->Install();
            // Apply random start jitter (ZigBee is most common, starts earliest)
            double jitteredStart = zbConfig.startTime + startJitter->GetValue(0.0, JITTER_ZIGBEE);
            // Schedule TurnOn AND SetSchedule at jittered start time
            bool hasSchedule = zbConfig.schedule.hasSchedule;
            double onDur = zbConfig.schedule.onDuration;
            double offDur = zbConfig.schedule.offDuration;
            Simulator::Schedule(Seconds(jitteredStart), [zigbee, hasSchedule, onDur, offDur]() {
                zigbee->TurnOn();
                if (hasSchedule) zigbee->SetSchedule(Seconds(onDur), Seconds(offDur));
            });
            if (g_verboseLogging) {
                std::cout << "  [VI] ZigBee @ (" << zbConfig.x << "," << zbConfig.y << "), channel="
                             << (int)zbConfig.zigbeeChannel << ", startTime=" << std::fixed << std::setprecision(1)
                             << jitteredStart << "s" << std::endl;
            }
        }

        // Create Radar interferers (RAREST - starts latest with base 25s + small jitter)
        for (const auto& radarConfig : config.virtualInterferers.radars)
        {
            auto radar = CreateObject<RadarInterferer>();
            radar->SetPosition(Vector(radarConfig.x, radarConfig.y, radarConfig.z));
            radar->SetTxPowerDbm(radarConfig.txPowerDbm);
            radar->SetDfsChannel(radarConfig.dfsChannel);
            if (radarConfig.radarType == "WEATHER") radar->SetRadarType(RadarInterferer::WEATHER);
            else if (radarConfig.radarType == "MILITARY") radar->SetRadarType(RadarInterferer::MILITARY);
            else if (radarConfig.radarType == "AVIATION") radar->SetRadarType(RadarInterferer::AVIATION);
            if (!radarConfig.dfsChannels.empty())
            {
                radar->SetDfsChannels(radarConfig.dfsChannels);
                radar->SetHopInterval(Seconds(radarConfig.hopIntervalSec));
                radar->SetRandomHopping(radarConfig.randomHopping);
            }
            radar->SetSpanLength(radarConfig.spanLength);
            radar->SetMaxSpanLength(radarConfig.maxSpanLength);
            radar->SetRandomSpan(radarConfig.randomSpan);
            radar->TurnOff();  // Start OFF (RAREST interferer)
            radar->Install();
            // Save first radar for GetCurrentlyAffectedChannels queries
            if (!g_radarInterferer)
            {
                g_radarInterferer = radar;
            }
            // Apply random start jitter from config (like other interferers)
            double jitteredStart = radarConfig.startTime + startJitter->GetValue(0.0, radarConfig.startTimeJitter);
            // Schedule TurnOn AND SetSchedule at jittered start time
            bool hasSchedule = radarConfig.schedule.hasSchedule;
            double onDur = radarConfig.schedule.onDuration;
            double offDur = radarConfig.schedule.offDuration;
            Simulator::Schedule(Seconds(jitteredStart), [radar, hasSchedule, onDur, offDur]() {
                radar->TurnOn();
                if (hasSchedule) radar->SetSchedule(Seconds(onDur), Seconds(offDur));
            });
            if (g_verboseLogging) {
                std::cout << "  [VI] Radar @ (" << radarConfig.x << "," << radarConfig.y << "), dfsChannel="
                             << (int)radarConfig.dfsChannel << ", startTime=" << std::fixed << std::setprecision(1)
                             << jitteredStart << "s (RAREST)" << std::endl;
            }
        }

        g_virtualInterfererEnabled = true;
        if (g_verboseLogging) {
            std::cout << "[VirtualInterferer] Created interferers" << std::endl;
        }

        // Connect DFS trigger trace to HandleDfsRadarDetection
        auto viEnv = VirtualInterfererEnvironment::Get();
        if (viEnv)
        {
            viEnv->TraceConnectWithoutContext("DfsTrigger",
                MakeCallback(&HandleDfsRadarDetection));
            if (g_verboseLogging) {
                std::cout << "[VirtualInterferer] DFS trigger callback connected" << std::endl;
            }
        }
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

    // IEEE 802.11 association: allow all channel widths for roaming compatibility
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

        // Detect band based on channel number (1-14 = 2.4GHz, 36+ = 5GHz)
        bool staIs2_4GHz = (config.aps[apIndex].channel >= 1 && config.aps[apIndex].channel <= 14);

        // IMPORTANT: 2.4GHz requires explicit width=20 to avoid ambiguity
        int staWidth = staIs2_4GHz ? 20 : 0;
        wifiPhy.Set("ChannelSettings",
            StringValue("{" + std::to_string(config.aps[apIndex].channel) + ", " +
                        std::to_string(staWidth) + ", " +
                        (staIs2_4GHz ? "BAND_2_4GHZ" : "BAND_5GHZ") + ", 0}"));

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

    // ===== Register WiFi devices with Virtual Interferer Environment =====
    if (g_virtualInterfererEnabled)
    {
        NetDeviceContainer allWifiDevices;
        allWifiDevices.Add(apWifiDevices);
        allWifiDevices.Add(staWifiDevices);
        g_viHelper.RegisterWifiDevices(allWifiDevices);

        auto viEnv = VirtualInterfererEnvironment::Get();
        viEnv->Start();

        if (g_verboseLogging) {
            std::cout << "[VirtualInterferer] Environment started with "
                         << viEnv->GetInterfererCount() << " interferers, "
                         << viEnv->GetReceiverCount() << " receivers" << std::endl;
        }
    }

    // ===== Collect MAC Addresses =====
    // Collect AP BSSIDs and populate g_macToNode for O(1) lookups
    for (uint32_t i = 0; i < numAps; i++)
    {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(apWifiDevices.Get(i));
        Mac48Address apBssid = wifiDev->GetMac()->GetAddress();
        g_apBssids.insert(apBssid);

        // Add to g_macToNode cache for O(1) lookups in CollectAndSendMetrics
        std::ostringstream oss;
        oss << apBssid;
        g_macToNode[oss.str()] = apNodes.Get(i);
    }

    // Collect STA MAC addresses and connect association trace sources
    for (uint32_t i = 0; i < numStas; i++)
    {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(staWifiDevices.Get(i));
        Mac48Address staMac = wifiDev->GetMac()->GetAddress();
        g_staMacs.insert(staMac);

        // Add to g_macToNode cache for O(1) lookups in CollectAndSendMetrics
        std::ostringstream oss;
        oss << staMac;
        g_macToNode[oss.str()] = staNodes.Get(i);

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
        channelHoppingHelper.SetAttribute("ScanningDelay",
            TimeValue(Seconds(config.staChannelHopping.scanningDelaySec)));
        channelHoppingHelper.SetAttribute("MinimumSnr",
            DoubleValue(config.staChannelHopping.minimumSnrDb));
        channelHoppingHelper.SetAttribute("Enabled", BooleanValue(true));

        for (uint32_t i = 0; i < numStas; i++)
        {
            Ptr<WifiNetDevice> staDev = DynamicCast<WifiNetDevice>(staWifiDevices.Get(i));
            if (staDev)
            {
                Ptr<StaChannelHoppingManager> manager = channelHoppingHelper.Install(staDev);
                g_staChannelHoppingManagers.push_back(manager);

                if (g_verboseLogging) {
                    std::cout << "[STA Channel Hopping] Installed on STA " << i << std::endl;
                }
            }
        }

        if (g_verboseLogging) {
            std::cout << "[STA CHANNEL HOPPING] Enabled for " << g_staChannelHoppingManagers.size()
                      << " STAs (delay=" << config.staChannelHopping.scanningDelaySec << "s)" << std::endl;
        }
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
        g_powerScoringConfig.t1IntervalSec = config.powerScoring.t1IntervalSec;
        g_powerScoringConfig.t2IntervalSec = config.powerScoring.t2IntervalSec;
        g_powerScoringConfig.rlPowerMarginDbm = config.powerScoring.rlPowerMarginDbm;

        if (g_verboseLogging) {
            std::cout << "========== POWER SCORING SETUP ==========" << std::endl;
            std::cout << "  RL Power Margin: ±" << g_powerScoringConfig.rlPowerMarginDbm << " dBm" << std::endl;
        }
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

            // Initialize power scoring state with actual configured TX power
            auto& powerState = g_powerScoringHelper.GetOrCreateApState(nodeId, bssColor);
            powerState.currentTxPowerDbm = config.aps[i].txPower;  // Use config value, not txPowerRefDbm

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

            if (g_verboseLogging) {
                std::cout << "[Power Scoring] AP" << i << " (Node " << nodeId
                             << "): BSS Color=" << (int)bssColor << std::endl;
            }
        }
        if (g_verboseLogging) {
            std::cout << "==========================================" << std::endl;
        }
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

    // Uninstall traffic control layer to enable WiFi MAC queue backpressure
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
    if (g_verboseLogging) {
        std::cout << "Pre-populated ARP cache on " << numStas << " STAs with DS address " << g_dsAddress << " -> " << dsMacAddress << std::endl;
    }

    // Setup traffic
    uint16_t port = 9;

    // Uplink: STA -> DS (UDP for faster simulation, MAC contention still applies)
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = sinkHelper.Install(dsNode.Get(0));
    sinkApps.Start(Seconds(1.0));
    sinkApps.Stop(Seconds(config.simulationTime));

    // Store reference to STA nodes for traffic reinstallation after roaming
    g_staNodes = staNodes;
    g_simulationEndTime = config.simulationTime;

    for (uint32_t i = 0; i < numStas; i++)
    {
        // AdaptiveUdp with AIMD congestion control (replaces OnOff)
        AdaptiveUdpHelper adaptiveUdp("ns3::UdpSocketFactory",
                                       InetSocketAddress(dsInterfaces.GetAddress(0), port));
        adaptiveUdp.SetInitialRate(DataRate("3Mbps"), 1400);  // 0.3 Mbps effective -> 3 Mbps displayed
        adaptiveUdp.SetRateLimits(DataRate("500Kbps"), DataRate("8Mbps"));  // 0.3 Mbps effective max -> 3 Mbps displayed
        adaptiveUdp.SetAimdParameters(0.5, 500000, 3);  // backoff=0.5, additive=500Kbps, threshold=3 (faster ramp-up)

        ApplicationContainer clientApp = adaptiveUdp.Install(staNodes.Get(i));
        clientApp.Start(Seconds(2.0 + i * 0.1));
        clientApp.Stop(Seconds(config.simulationTime));

        // Store app reference for reinstallation after roaming
        Ptr<AdaptiveUdpApplication> app = DynamicCast<AdaptiveUdpApplication>(clientApp.Get(0));
        if (app)
        {
            g_staAdaptiveApps[i] = app;
        }
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

        // Set display time scale factor and offset for Kafka producer
        if (g_kafkaProducer)
        {
            g_kafkaProducer->SetDisplayTimeScaleFactor(g_displayTimeScaleFactor);
            g_kafkaProducer->SetDisplayTimeOffsetSeconds(g_displayTimeOffsetSeconds);
        }

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
        g_kafkaConsumer->SetCommandCallback(MakeCallback(&OnCommandReceived));

        NS_LOG_INFO("Kafka consumer enabled: broker=" << kafkaBroker
                    << " topic=" << kafkaDownstreamTopic);

        // Setup Simulation Event Producer (for simulator-events topic)
        g_simEventProducer = CreateObject<SimulationEventProducer>();
        g_simEventProducer->SetBrokers(kafkaBroker);
        g_simEventProducer->SetTopic("simulator-events");
        g_simEventProducer->SetSimulationId(simulationId);
        g_simEventProducer->SetFlushInterval(MilliSeconds(100));
        g_simEventProducer->SetNodeIdMappings(g_simNodeIdToConfigNodeId, g_apSimNodeIdToConfigNodeId);

        // Install on same node as Kafka producer
        kafkaNode.Get(0)->AddApplication(g_simEventProducer);
        g_simEventProducer->SetStartTime(Seconds(0.0));
        g_simEventProducer->SetStopTime(Seconds(config.simulationTime + 5.0));

        NS_LOG_INFO("Simulation Event Producer enabled: topic=simulator-events");
    }

    // Install FlowMonitor
    g_flowMonitor = g_flowMonitorHelper.InstallAll();

    // Schedule metrics collection
    Simulator::Schedule(Seconds(g_statsInterval), &CollectAndSendMetrics);

    // Schedule periodic RSSI check for BSS TM triggering
    Simulator::Schedule(Seconds(RSSI_CHECK_INTERVAL), &PeriodicRssiCheck);

    // Schedule periodic cleanup of global maps to prevent unbounded growth
    Simulator::Schedule(Seconds(60), &PeriodicMapCleanup);

    // Channel scoring is now REACTIVE ONLY:
    // - Per-AP scoring triggered when specific AP's non-WiFi exceeds threshold
    // - Global scoring triggered by DFS radar events affecting multiple APs
    // No initial global scoring at startup - APs stay on their configured channels
    // unless interference forces them to switch
    if (g_channelScoringEnabled && g_verboseLogging)
    {
        std::cout << "[CHANNEL SCORING] Reactive mode - no initial scoring, triggered by non-WiFi threshold or DFS" << std::endl;
    }

    // Schedule periodic power scoring (if enabled)
    if (g_powerScoringEnabled)
    {
        // Start after 5 seconds to allow network to stabilize
        Simulator::Schedule(Seconds(5.0), &PerformPowerScoring);
        if (g_verboseLogging) {
            std::cout << "[Power Scoring] First scoring scheduled at t=5.0s" << std::endl;
        }
    }

    // Run simulation
    Simulator::Stop(Seconds(config.simulationTime));
    Simulator::Run();

    // Cleanup virtual interferer environment before destroying simulator
    if (g_virtualInterfererEnabled)
    {
        if (g_verboseLogging) {
            std::cout << "[VirtualInterferer] Cleaning up environment..." << std::endl;
        }
        VirtualInterfererEnvironment::Destroy();
    }

    Simulator::Destroy();

    // Restore original stream buffers and cleanup
    if (g_logFile.is_open())
    {
        std::cout << "=== Simulation logs saved ===" << std::endl;
        if (g_origCoutBuf) std::cout.rdbuf(g_origCoutBuf);
        if (g_origCerrBuf) std::cerr.rdbuf(g_origCerrBuf);
        if (g_origClogBuf) std::clog.rdbuf(g_origClogBuf);
        delete g_teeCout;
        delete g_teeCerr;
        delete g_teeClog;
        g_logFile.close();
    }

    return 0;
}
