/**
 * Config-Based Simulation - WiFi Roaming with Waypoint Mobility
 *
 * This simulation demonstrates a complete WiFi roaming orchestration system
 * configured from a JSON file, combining:
 * - Waypoint-based mobility for realistic STA movement
 * - IEEE 802.11k/v protocols for intelligent roaming
 * - Comprehensive metrics collection for Kafka integration
 * - TCP traffic with performance tracking
 * - Dynamic PHY parameter control via LeverAPI
 *
 * Network Setup (from config file):
 * - N APs at configured positions (5GHz with configurable channels)
 * - M STAs moving between configured waypoints
 * - 1 DS node (distribution system)
 *
 * Roaming Chain:
 * Link Measurement → Neighbor Request → Beacon Request → BSS TM → Roaming
 *
 * Monitoring:
 * - TCP performance (RTT, retransmissions, packet loss)
 * - WiFi CCA channel utilization
 * - Link quality via 802.11k measurements
 * - FlowMonitor for per-flow latency/jitter
 *
 * Usage:
 *   ./ns3 run "config-simulation"
 *   ./ns3 run "config-simulation --configFile=my-scenario.json"
 *   ./ns3 run "config-simulation --configFile=my-scenario.json --enableKafkaProducer=1"
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/propagation-module.h"
#include "ns3/applications-module.h"
#include "ns3/csma-helper.h"
#include "ns3/bridge-helper.h"

// Custom modules - simulation-helper
#include "ns3/simulation-helper.h"
#include "ns3/metrics-helper.h"
#include "ns3/roaming-trace-helper.h"

// Custom modules - roaming protocols
#include "ns3/auto-roaming-kv-helper.h"
#include "ns3/ap-link-measurement-helper.h"
#include "ns3/beacon-protocol-11k-helper.h"
#include "ns3/neighbor-protocol-11k-helper.h"
#include "ns3/bss_tm_11v-helper.h"
#include "ns3/dual-phy-sniffer-helper.h"
#include "ns3/link-measurement-protocol.h"
#include "ns3/sta-channel-hopping-helper.h"

// Custom modules - waypoint simulation
#include "ns3/simulation-config-parser.h"
#include "ns3/waypoint-mobility-helper.h"
#include "ns3/waypoint-grid.h"

// Custom modules - monitoring
#include "ns3/wifi-cca-monitor-helper.h"
#include "ns3/wifi-cca-monitor.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"

// Custom modules - Kafka & LeverAPI
#include "ns3/kafka-producer-helper.h"
#include "ns3/kafka-consumer-helper.h"
#include "ns3/lever-api-helper.h"
#include "ns3/optimization-command.h"
#include "ns3/channel-scoring-helper.h"
#include "ns3/power-scoring-helper.h"

// Virtual Interferer module
#include "ns3/virtual-interferer.h"
#include "ns3/virtual-interferer-environment.h"
#include "ns3/virtual-interferer-helper.h"
#include "ns3/microwave-interferer.h"
#include "ns3/bluetooth-interferer.h"
#include "ns3/cordless-interferer.h"
#include "ns3/zigbee-interferer.h"
#include "ns3/radar-interferer.h"

// HE PHY for BSS Color callback
#include "ns3/he-phy.h"
#include "ns3/he-configuration.h"
#include "ns3/wifi-tx-vector.h"
#include "ns3/wifi-mode.h"

// TCP
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/on-off-helper.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/tcp-socket-factory.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <set>
#include <tuple>
#include <thread>
#include <chrono>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ConfigSimulation");


// Global Kafka configuration
double g_kafkaInterval = 0.1;
Ptr<KafkaProducer> g_kafkaProducer;  // Global Kafka producer for metrics

// Global FlowMonitor references
Ptr<FlowMonitor> g_flowMonitor;
Ptr<Ipv4FlowClassifier> g_flowClassifier;
DualPhySnifferHelper* g_apDualPhySniffer = nullptr;

// Global DS (Destination Server) address for flow direction detection
Ipv4Address g_dsAddress;

std::map<Ptr<const TcpSocketBase>, uint32_t> g_socketToNodeId;

// ============================================================================
// MAC RETRY TRACKING
// ============================================================================

std::map<uint32_t, uint32_t> g_macRetryCount;  // nodeId -> retry count

// ============================================================================
// NODE ID MAPPING (sim node ID -> config nodeId)
// ============================================================================

std::map<uint32_t, uint32_t> g_simNodeIdToConfigNodeId;      // For STAs
std::map<uint32_t, uint32_t> g_apSimNodeIdToConfigNodeId;    // For APs

// ============================================================================
// METRIC STRUCTURES
// ============================================================================

std::map<uint32_t, ApMetrics> g_apMetrics;

// Scanning radio CCA monitors for multi-channel utilization tracking
// Struct to hold all utilization values for a scanning channel
struct ScanningChannelUtil {
    double totalUtil = 0.0;
    double wifiUtil = 0.0;
    double nonWifiUtil = 0.0;
};
// Maps: AP Operating BSSID -> (Channel Number -> Utilization data)
std::map<Mac48Address, std::map<uint8_t, ScanningChannelUtil>> g_scanningChannelUtilization;

// Track scanning devices for each AP (Operating BSSID -> Scanning NetDevice)
std::map<Mac48Address, Ptr<WifiNetDevice>> g_apScanningDevices;

// Store CCA monitors to keep them alive for the simulation duration
std::vector<Ptr<WifiCcaMonitor>> g_ccaMonitors;

// ============================================================================
// BEACON MEASUREMENT STORAGE (Part B Attributes 2-4)
// ============================================================================

// Attribute 2: Per-channel RSSI of neighbor APs
// Maps: Receiver AP BSSID -> Channel -> Transmitter AP BSSID -> Latest RSSI (dBm)
std::map<Mac48Address, std::map<uint8_t, std::map<Mac48Address, double>>> g_beaconRssiMap;

// Attribute 3: Number of unique BSSIDs per channel
// Maps: Receiver AP BSSID -> Channel -> Set of detected Transmitter BSSIDs
std::map<Mac48Address, std::map<uint8_t, std::set<Mac48Address>>> g_bssidCountMap;

// Attribute 4: Neighbor AP configuration from beacons
struct NeighborApConfig {
    uint8_t channel = 0;
    uint16_t channelWidth = 20;  // MHz (default 20 MHz)
    uint16_t staCount = 0;       // Number of associated STAs from BSS Load IE
    double lastRssi = 0.0;       // Latest RSSI
    Time lastUpdate;
};
// Maps: Receiver AP BSSID -> Transmitter AP BSSID -> Config
std::map<Mac48Address, std::map<Mac48Address, NeighborApConfig>> g_neighborConfigMap;

// Pending connection data storage (for callbacks that fire before FlowMonitor creates entries)
struct PendingConnectionData {
    double apViewRSSI = 0.0;
    double apViewSNR = 0.0;
    double staViewRSSI = 0.0;
    double staViewSNR = 0.0;
    uint8_t uplinkMCS = 0;
    uint8_t downlinkMCS = 0;
    bool hasApView = false;
    bool hasStaView = false;
    bool hasUplinkMCS = false;
    bool hasDownlinkMCS = false;
    Time lastUpdate;
};
std::map<std::string, PendingConnectionData> g_pendingConnectionData;  // connId -> pending data

// Global LeverConfig objects for APs
std::map<uint32_t, Ptr<LeverConfig>> g_apLeverConfigs;
std::map<std::string, Ptr<LeverConfig>> g_bssidToLeverConfig;

// Global LeverApi instances for APs (for channel switch propagation)
std::map<uint32_t, Ptr<LeverApi>> g_apLeverApis;

// ============================================================================
// CHANNEL SCORING (ACS/DCS Support) - Using ChannelScoringHelper module
// ============================================================================

// Global channel scoring helper and config (can be set from JSON config)
ChannelScoringConfig g_channelScoringConfig;
ChannelScoringHelper g_channelScoringHelper;

// ============================================================================
// GLOBAL CHANNEL RANKING (for reactive channel management)
// ============================================================================

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
Time g_dfsBlacklistDuration = Minutes(1);         // How long to blacklist DFS channel after radar
std::map<uint32_t, Time> g_lastNonWifiTrigger;    // Per-AP: last time non-WiFi trigger fired
Time g_nonWifiTriggerCooldown = Seconds(10);      // Cooldown between triggers for same AP

/**
 * Get backup channel from global ranking (excludes blacklisted and current channel)
 */
uint8_t GetBackupChannel(WifiPhyBand band, uint8_t excludeChannel)
{
    auto& ranked = (band == WIFI_PHY_BAND_5GHZ)
        ? g_globalRanking.ranked5GHz
        : g_globalRanking.ranked24GHz;

    for (uint8_t ch : ranked) {
        if (ch == excludeChannel) continue;
        if (g_globalRanking.dfsBlacklist.count(ch) > 0) continue;
        return ch;  // First valid backup
    }
    return 0;  // No backup available
}

/**
 * Check and expire old DFS blacklist entries
 */
void CheckDfsBlacklistExpiry()
{
    std::vector<uint8_t> expired;
    for (const auto& [channel, expiryTime] : g_globalRanking.blacklistExpiry) {
        if (Simulator::Now() >= expiryTime) {
            expired.push_back(channel);
        }
    }
    for (uint8_t ch : expired) {
        g_globalRanking.dfsBlacklist.erase(ch);
        g_globalRanking.blacklistExpiry.erase(ch);
        std::cout << "[DFS] Channel " << (int)ch << " removed from blacklist at t="
                  << Simulator::Now().GetSeconds() << "s" << std::endl;
    }
}

// Track last DFS detection time per channel to avoid spam
static std::map<uint8_t, Time> g_lastDfsDetection;
static Time g_dfsDetectionCooldown = Seconds(5);  // Cooldown between detections for same channel

// Global radar interferer pointer (for querying affected channels during DFS detection)
static Ptr<RadarInterferer> g_radarInterferer = nullptr;

// Forward declaration
void PerformChannelScoring();

/**
 * Handle DFS radar detection from scanning radio
 * Adds ALL affected channels to blacklist and triggers channel scoring
 */
void HandleDfsRadarDetection(uint32_t nodeId, uint8_t channel)
{
    Time now = Simulator::Now();

    // Check cooldown for this specific detected channel to avoid spam
    if (g_lastDfsDetection.count(channel) > 0) {
        if (now - g_lastDfsDetection[channel] < g_dfsDetectionCooldown) {
            return;  // Skip, already detected recently
        }
    }
    g_lastDfsDetection[channel] = now;

    // Get all channels affected by the wideband radar
    std::set<uint8_t> affectedChannels;
    if (g_radarInterferer) {
        affectedChannels = g_radarInterferer->GetCurrentlyAffectedChannels();
    } else {
        // Fallback: just blacklist the detected channel
        affectedChannels.insert(channel);
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
        std::cout << "[DFS-RADAR] Wideband radar detected on channel " << (int)channel
                  << " by node " << nodeId << " at t=" << now.GetSeconds() << "s"
                  << "\n            Affected channels: " << affectedStr.str()
                  << " (" << newlyBlacklisted << " newly blacklisted)"
                  << "\n            Blacklist duration: " << g_dfsBlacklistDuration.GetMinutes() << " minutes"
                  << std::endl;

        // Trigger channel scoring to update rankings (exclude blacklisted channels)
        Simulator::ScheduleNow(&PerformChannelScoring);
    }
}

/**
 * Periodic channel scoring callback - logs scores for all APs
 */
void PerformChannelScoring()
{
    std::cout << "\n========== CHANNEL SCORING (t=" << Simulator::Now().GetSeconds() << "s) ==========" << std::endl;
    std::cout << "Mode: SEQUENTIAL ALLOCATION (coordinated channel selection)" << std::endl;

    auto config = g_channelScoringHelper.GetConfig();

    // SEQUENTIAL ALLOCATION: Track channels already allocated to prevent all APs picking same channel
    std::set<uint8_t> allocatedChannels;

    // First pass: Print raw scan data for all APs
    for (const auto& [nodeId, apMetrics] : g_apMetrics) {
        std::cout << "\nAP " << apMetrics.bssid << " (Operating Ch " << (int)apMetrics.channel << ") - Raw Scan Data:" << std::endl;
        for (const auto& [ch, scanData] : apMetrics.scanningChannelData) {
            std::cout << "  Scanned Ch " << (int)ch
                      << ": BSSIDs=" << scanData.bssidCount
                      << ", WiFiUtil=" << scanData.wifiChannelUtilization << "%"
                      << ", NonWiFiUtil=" << scanData.nonWifiChannelUtilization << "%"
                      << ", Neighbors=" << scanData.neighbors.size() << std::endl;
            for (const auto& neighbor : scanData.neighbors) {
                std::cout << "    -> " << neighbor.bssid
                          << " (ch=" << (int)neighbor.channel
                          << ", rssi=" << neighbor.rssi
                          << ", wifiUtil=" << (int)neighbor.wifiUtil
                          << ", nonWifiUtil=" << (int)neighbor.nonWifiUtil << ")" << std::endl;
            }
        }
    }

    std::cout << "\n---------- SEQUENTIAL CHANNEL ALLOCATION ----------" << std::endl;

    // Second pass: Sequential allocation - process APs one by one
    for (const auto& [nodeId, apMetrics] : g_apMetrics) {
        std::cout << "\n[AP " << nodeId << "] " << apMetrics.bssid << " (Current Ch " << (int)apMetrics.channel << "):" << std::endl;

        // Calculate scores for this AP (pass actual channel width for overlap calculation)
        auto scores = g_channelScoringHelper.CalculateScores(apMetrics.scanningChannelData, apMetrics.channelWidth);

        if (scores.empty()) {
            std::cout << "  No scan data available - skipping" << std::endl;
            continue;
        }

        // Print scores with allocation status
        std::cout << "  Scoring Results:" << std::endl;
        for (const auto& score : scores) {
            std::cout << std::fixed << std::setprecision(1);
            std::cout << "    Ch " << std::setw(3) << (int)score.channel
                << " | Total: " << std::setw(6) << score.totalScore
                << " | Rank: " << score.rank;

            if (score.discarded) {
                std::cout << " [DISCARDED]";
            }
            if (score.channel == apMetrics.channel) {
                std::cout << " [CURRENT]";
            }
            if (allocatedChannels.count(score.channel) > 0) {
                std::cout << " [ALREADY ALLOCATED]";
            }

            std::cout << std::endl;
        }

        // Find best channel for this AP's band, EXCLUDING already allocated channels
        uint8_t bestCh = 0;
        for (const auto& score : scores) {
            if (score.discarded) {
                continue;
            }
            // Check if channel is in correct band
            if (!ChannelScoringHelper::IsChannelInBand(score.channel, apMetrics.band)) {
                continue;
            }
            // Skip channels already allocated to other APs in this round
            if (allocatedChannels.count(score.channel) > 0) {
                continue;
            }
            // Found best available channel
            bestCh = score.channel;
            break;
        }

        if (bestCh != 0) {
            // Mark this channel as allocated for subsequent APs
            allocatedChannels.insert(bestCh);

            if (bestCh != apMetrics.channel) {
                std::cout << "  -> ALLOCATED: Channel " << (int)bestCh << " (best available in "
                          << (apMetrics.band == WIFI_PHY_BAND_5GHZ ? "5GHz" : "2.4GHz") << " band)" << std::endl;

                // Schedule channel switch via LeverApi
                auto leverApiIter = g_apLeverApis.find(nodeId);
                if (leverApiIter != g_apLeverApis.end() && leverApiIter->second) {
                    uint32_t capturedNodeId = nodeId;
                    uint8_t capturedBestCh = bestCh;
                    // Stagger switches: 100ms + (nodeId * 50ms) to avoid simultaneous switches
                    uint32_t delayMs = 100 + (nodeId * 50);
                    Simulator::Schedule(MilliSeconds(delayMs), [capturedNodeId, capturedBestCh]() {
                        auto it = g_apLeverApis.find(capturedNodeId);
                        if (it != g_apLeverApis.end() && it->second) {
                            std::cout << "\n[CHANNEL SCORING → PHY] Switching Node " << capturedNodeId
                                      << " to Channel " << (int)capturedBestCh << std::endl;
                            it->second->SwitchChannel(capturedBestCh);

                            // UPDATE g_apMetrics to reflect the new channel, band, AND width
                            auto metricsIt = g_apMetrics.find(capturedNodeId);
                            if (metricsIt != g_apMetrics.end()) {
                                WifiPhyBand newBand = (capturedBestCh >= 36) ? WIFI_PHY_BAND_5GHZ : WIFI_PHY_BAND_2_4GHZ;

                                // Get updated channel width from LeverApi
                                uint16_t newWidth = it->second->GetChannelWidth();

                                metricsIt->second.channel = capturedBestCh;
                                metricsIt->second.band = newBand;
                                metricsIt->second.channelWidth = newWidth;
                                std::cout << "[CHANNEL SCORING → PHY] ✓ Updated g_apMetrics[" << capturedNodeId
                                          << "].channel = " << (int)capturedBestCh << " @ " << newWidth << "MHz ("
                                          << (newBand == WIFI_PHY_BAND_5GHZ ? "5GHz" : "2.4GHz") << ")" << std::endl;
                            }
                            std::cout << "[CHANNEL SCORING → PHY] ✓ Channel switch triggered (STAs will follow)" << std::endl;
                        }
                    });
                    std::cout << "  -> Scheduling switch to CH" << (int)bestCh << " in " << delayMs << "ms" << std::endl;
                } else {
                    std::cout << "  -> WARNING: No LeverApi for Node " << nodeId << std::endl;
                }
            } else {
                std::cout << "  -> KEEPING: Channel " << (int)bestCh << " (already optimal)" << std::endl;
            }
        } else {
            std::cout << "  -> WARNING: No suitable channel found!" << std::endl;
        }
    }

    // Summary
    std::cout << "\n---------- ALLOCATION SUMMARY ----------" << std::endl;
    std::cout << "Allocated channels: ";
    for (uint8_t ch : allocatedChannels) {
        std::cout << (int)ch << " ";
    }
    std::cout << std::endl;

    // Update global channel ranking for backup selection
    // Use first AP with scan data to build global ranking per band
    g_globalRanking.ranked5GHz.clear();
    g_globalRanking.ranked24GHz.clear();

    for (const auto& [nodeId, apMetrics] : g_apMetrics) {
        if (apMetrics.scanningChannelData.empty()) continue;

        auto scores = g_channelScoringHelper.CalculateScores(apMetrics.scanningChannelData, apMetrics.channelWidth);
        for (const auto& score : scores) {
            if (score.discarded) continue;
            if (g_globalRanking.dfsBlacklist.count(score.channel) > 0) continue;

            if (ChannelScoringHelper::IsChannelInBand(score.channel, WIFI_PHY_BAND_5GHZ)) {
                // Add to 5GHz ranking if not already present
                if (std::find(g_globalRanking.ranked5GHz.begin(), g_globalRanking.ranked5GHz.end(), score.channel)
                    == g_globalRanking.ranked5GHz.end()) {
                    g_globalRanking.ranked5GHz.push_back(score.channel);
                }
            } else if (ChannelScoringHelper::IsChannelInBand(score.channel, WIFI_PHY_BAND_2_4GHZ)) {
                // Add to 2.4GHz ranking if not already present
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

    std::cout << "Global Ranking Updated:" << std::endl;
    std::cout << "  5GHz: ";
    for (uint8_t ch : g_globalRanking.ranked5GHz) {
        std::cout << (int)ch << " ";
    }
    std::cout << std::endl;
    std::cout << "  2.4GHz: ";
    for (uint8_t ch : g_globalRanking.ranked24GHz) {
        std::cout << (int)ch << " ";
    }
    std::cout << std::endl;
    if (!g_globalRanking.dfsBlacklist.empty()) {
        std::cout << "  DFS Blacklist: ";
        for (uint8_t ch : g_globalRanking.dfsBlacklist) {
            std::cout << (int)ch << " ";
        }
        std::cout << std::endl;
    }

    std::cout << "==========================================================" << std::endl;

    // Schedule next scoring (every 10 seconds)
    Simulator::Schedule(Seconds(10), &PerformChannelScoring);
}

// ============================================================================
// POWER SCORING (TX Power Control) - Using PowerScoringHelper module
// ============================================================================

// Global power scoring helper and config
PowerScoringConfig g_powerScoringConfig;
PowerScoringHelper g_powerScoringHelper;
bool g_powerScoringEnabled = true;  // Can be set from JSON config

/**
 * HE-SIG-A callback for receiving RSSI and BSS Color from AP's perspective
 */
void OnApHeSigA(uint32_t nodeId, HeSigAParameters params)
{
    // Forward to power scoring helper
    g_powerScoringHelper.ProcessHeSigA(nodeId, params.rssi, params.bssColor);
}

/**
 * PHY RX Payload Begin callback for receiving actual MCS from AP's perspective
 * Called when payload reception begins - gives us WifiTxVector with actual MCS
 */
void OnApPhyRxPayloadBegin(uint32_t nodeId, WifiTxVector txVector, Time psduDuration)
{
    // Only process HE (802.11ax) frames
    if (txVector.GetModulationClass() == WIFI_MOD_CLASS_HE)
    {
        uint8_t mcs = txVector.GetMode().GetMcsValue();
        // Update power scoring helper with actual MCS (0-11 for 802.11ax)
        g_powerScoringHelper.UpdateMcs(nodeId, mcs);
    }
}

/**
 * Get non-WiFi percentage for an AP from its scan data
 * Uses BSS Load IE if neighbors exist, otherwise scanning radio CCA
 */
double GetNonWifiForAp(uint32_t nodeId)
{
    auto it = g_apMetrics.find(nodeId);
    if (it == g_apMetrics.end())
    {
        return 0.0;
    }

    const auto& apMetrics = it->second;

    // Use operating channel CCA monitor data (continuously updated, includes virtual interferers)
    // This is more accurate than scanning radio data which is only periodically updated
    return apMetrics.nonWifiChannelUtilization;
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

    auto config = g_powerScoringHelper.GetConfig();

    for (const auto& [nodeId, apMetrics] : g_apMetrics)
    {
        // Get or create AP state with BSS Color
        auto& state = g_powerScoringHelper.GetOrCreateApState(nodeId, apMetrics.bssColor);

        // Get non-WiFi percentage
        double nonWifiPercent = GetNonWifiForAp(nodeId);

        // Get current state for logging
        double prevPower = state.currentTxPowerDbm;
        bool wasInNonWifiMode = state.inNonWifiMode;

        std::cout << "\n--- AP Node " << nodeId << " (BSSID: " << apMetrics.bssid
                  << ", Ch: " << (int)apMetrics.channel << ") ---" << std::endl;
        std::cout << "  Current: TxPower=" << apMetrics.txPowerDbm << "dBm"
                  << ", OBSS/PD=" << apMetrics.obsspdLevelDbm << "dBm" << std::endl;
        std::cout << "  Inputs:" << std::endl;
        std::cout << "    - BSS RSSI (EWMA):  " << state.tracker.bssRssiEwma << " dBm" << std::endl;
        std::cout << "    - OBSS Frames:      " << state.tracker.obssFrameCount
                  << " / " << state.tracker.frameCount << " total" << std::endl;
        std::cout << "    - MCS (EWMA):       " << state.tracker.mcsEwma << std::endl;
        std::cout << "    - Non-WiFi:         " << nonWifiPercent << "%" << std::endl;
        std::cout << "    - BSS Color:        " << (int)apMetrics.bssColor << std::endl;
        std::cout << "  Mode: " << (state.inNonWifiMode ? "NON_WIFI" : "RACEBOT") << std::endl;

        // Calculate new power
        PowerResult result = g_powerScoringHelper.CalculatePower(nodeId, nonWifiPercent);

        if (result.powerChanged)
        {
            std::cout << "  -> POWER CHANGE: " << prevPower << " -> " << result.txPowerDbm
                      << " dBm (" << result.reason << ")" << std::endl;

            // Apply via LeverConfig (like Kafka consumer does)
            auto configIt = g_apLeverConfigs.find(nodeId);
            if (configIt != g_apLeverConfigs.end() && configIt->second)
            {
                std::cout << "\n[POWER SCORING -> PHY] Changing Node " << nodeId
                          << " TxPower: " << prevPower << " -> " << result.txPowerDbm << " dBm" << std::endl;
                configIt->second->SetTxPower(result.txPowerDbm);

                // Update g_apMetrics
                auto metricsIt = g_apMetrics.find(nodeId);
                if (metricsIt != g_apMetrics.end())
                {
                    metricsIt->second.txPowerDbm = result.txPowerDbm;
                    metricsIt->second.obsspdLevelDbm = result.obsspdLevelDbm;
                    std::cout << "[POWER SCORING -> PHY] Updated g_apMetrics[" << nodeId
                              << "].txPowerDbm = " << result.txPowerDbm << std::endl;
                }
                std::cout << "[POWER SCORING -> PHY] TX power change applied" << std::endl;
            }
            else
            {
                std::cout << "  -> WARNING: No LeverConfig for Node " << nodeId << std::endl;
            }
        }
        else
        {
            std::cout << "  -> KEEPING: TxPower=" << result.txPowerDbm
                      << "dBm (no significant change)" << std::endl;
        }
    }

    // Summary
    std::cout << "\n---------- POWER ALLOCATION SUMMARY ----------" << std::endl;
    for (const auto& [nodeId, metrics] : g_apMetrics)
    {
        auto stateIt = g_powerScoringHelper.GetAllApStates().find(nodeId);
        std::string mode = "N/A";
        if (stateIt != g_powerScoringHelper.GetAllApStates().end())
        {
            mode = stateIt->second.inNonWifiMode ? "NON_WIFI" : "RACEBOT";
        }
        std::cout << "Node " << nodeId << ": " << metrics.txPowerDbm << " dBm"
                  << " (Mode: " << mode << ")" << std::endl;
    }
    std::cout << "==========================================================" << std::endl;

    // Schedule next scoring
    Simulator::Schedule(Seconds(g_powerScoringConfig.updateIntervalSec), &PerformPowerScoring);
}

// ============================================================================
// VIRTUAL INTERFERER DETECTION LOGGING
// ============================================================================

// Global config for virtual interferer logging
bool g_virtualInterfererLoggingEnabled = true;  // Can be set from JSON config
double g_virtualInterfererLoggingInterval = 10.0;  // seconds

/**
 * Periodic virtual interferer detection logging
 * Shows when and where virtual interferers are detected on each AP's channel
 */
void PerformVirtualInterfererLogging()
{
    if (!g_virtualInterfererLoggingEnabled)
    {
        Simulator::Schedule(Seconds(g_virtualInterfererLoggingInterval), &PerformVirtualInterfererLogging);
        return;
    }

    auto viEnv = VirtualInterfererEnvironment::Get();
    if (!viEnv || VirtualInterfererEnvironment::IsBeingDestroyed())
    {
        // Virtual interferer environment not available
        Simulator::Schedule(Seconds(g_virtualInterfererLoggingInterval), &PerformVirtualInterfererLogging);
        return;
    }

    std::cout << "\n========== VIRTUAL INTERFERER DETECTION (t="
              << std::fixed << std::setprecision(2) << Simulator::Now().GetSeconds()
              << "s) ==========" << std::endl;

    bool anyInterferenceDetected = false;

    // Check each AP
    for (const auto& [nodeId, apMetrics] : g_apMetrics)
    {
        // Get AP position
        Ptr<Node> apNode = NodeList::GetNode(nodeId);
        if (!apNode) continue;

        Ptr<MobilityModel> mobility = apNode->GetObject<MobilityModel>();
        if (!mobility) continue;

        Vector apPosition = mobility->GetPosition();
        uint8_t channel = apMetrics.channel;

        // Query interference effect at this AP's position/channel
        InterferenceEffect effect = viEnv->GetAggregateEffect(apPosition, channel);

        // Only log if there's significant interference
        if (effect.nonWifiCcaPercent > 0.1 || effect.packetLossProbability > 0.001)
        {
            anyInterferenceDetected = true;

            std::cout << "\n--- AP Node " << nodeId << " (BSSID: " << apMetrics.bssid
                      << ", Ch: " << (int)channel << ") ---" << std::endl;
            std::cout << "  Position: (" << std::fixed << std::setprecision(1)
                      << apPosition.x << ", " << apPosition.y << ", " << apPosition.z << ")" << std::endl;
            std::cout << "  Detected Interference Effects:" << std::endl;
            std::cout << "    ├─ Non-WiFi CCA:    " << std::fixed << std::setprecision(2)
                      << effect.nonWifiCcaPercent << "%" << std::endl;
            std::cout << "    ├─ Packet Loss:     " << std::fixed << std::setprecision(4)
                      << (effect.packetLossProbability * 100.0) << "%" << std::endl;
            std::cout << "    ├─ Signal Power:    " << std::fixed << std::setprecision(1)
                      << effect.signalPowerDbm << " dBm" << std::endl;
            std::cout << "    └─ DFS Trigger:     " << (effect.triggersDfs ? "YES" : "NO") << std::endl;

            // List all active interferers in the environment (for context)
            std::vector<Ptr<VirtualInterferer>> allInterferers = viEnv->GetInterferers();
            std::cout << "  Active Interferers in Environment:" << std::endl;

            int activeCount = 0;
            for (const auto& interferer : allInterferers)
            {
                if (!interferer->IsActive() || !interferer->IsInstalled())
                    continue;

                activeCount++;
                Vector interfererPos = interferer->GetPosition();
                double distance = CalculateDistance(apPosition, interfererPos);

                std::cout << "    " << activeCount << ". " << interferer->GetInterfererType()
                          << " @ (" << std::fixed << std::setprecision(1)
                          << interfererPos.x << ", " << interfererPos.y << ", " << interfererPos.z << ")"
                          << " | Distance: " << std::setprecision(1) << distance << "m"
                          << " | TxPower: " << std::setprecision(1) << interferer->GetTxPowerDbm() << "dBm"
                          << std::endl;
            }

            if (activeCount == 0)
            {
                std::cout << "    (No active interferers in environment)" << std::endl;
            }
        }
    }

    if (!anyInterferenceDetected)
    {
        std::cout << "\n  No significant virtual interference detected on any AP channel." << std::endl;
    }

    // Summary of all active interferers
    std::vector<Ptr<VirtualInterferer>> allInterferers = viEnv->GetInterferers();
    int activeCount = 0;
    for (const auto& interferer : allInterferers)
    {
        if (interferer->IsActive() && interferer->IsInstalled())
            activeCount++;
    }

    std::cout << "\n---------- VIRTUAL INTERFERER SUMMARY ----------" << std::endl;
    std::cout << "Total Active Interferers: " << activeCount << " / " << allInterferers.size() << std::endl;
    std::cout << "====================================================" << std::endl;

    // Schedule next logging
    Simulator::Schedule(Seconds(g_virtualInterfererLoggingInterval), &PerformVirtualInterfererLogging);
}

// ============================================================================
// TRACE CALLBACKS
// ============================================================================

/**
 * WiFi MAC Data Transmission Failed (Retry) event
 */
void OnMacTxDataFailed(std::string context, Mac48Address address)
{
    uint32_t nodeId = MetricsHelper::ExtractNodeIdFromContext(context);

    NS_LOG_DEBUG("[MAC RETRY] Node " << nodeId << " failed to transmit to " << address
                 << " at t=" << Simulator::Now().GetSeconds() << "s (will retry)");

    g_macRetryCount[nodeId]++;

    NS_LOG_DEBUG("[MAC RETRY] Updated STA Node " << nodeId
                 << ": retry_count=" << g_macRetryCount[nodeId]);
}

/**
 * STA Association event
 * Called when a STA successfully associates with an AP
 */
void OnStaAssociation(std::string context, Mac48Address bssid)
{
    uint32_t nodeId = MetricsHelper::ExtractNodeIdFromContext(context);

    NS_LOG_INFO("[Association] STA Node " << nodeId << " associated with AP " << bssid
                << " at t=" << Simulator::Now().GetSeconds() << "s");

    // Get STA MAC address
    Ptr<Node> staNode = NodeList::GetNode(nodeId);
    if (!staNode) {
        NS_LOG_WARN("[Association] Could not find STA Node " << nodeId);
        return;
    }

    Ptr<WifiNetDevice> staDev = DynamicCast<WifiNetDevice>(staNode->GetDevice(0));
    if (!staDev) {
        NS_LOG_WARN("[Association] Could not find WiFi device for STA Node " << nodeId);
        return;
    }

    Mac48Address staMac = Mac48Address::ConvertFrom(staDev->GetAddress());

    // Remove STA from ALL other APs' client lists first (handles roaming)
    // A STA can only be associated with ONE AP at a time
    for (auto& entry : g_apMetrics) {
        ApMetrics& otherAp = entry.second;
        if (otherAp.bssid != bssid) {  // Not the new AP
            auto it = otherAp.clientList.find(staMac);
            if (it != otherAp.clientList.end()) {
                otherAp.clientList.erase(it);
                otherAp.associatedClients = otherAp.clientList.size();
                NS_LOG_INFO("[Association] Removed STA " << staMac << " from old AP " << otherAp.bssid);

                // Update AP's internal STA list for BSS Load IE sta_count
                Ptr<Node> oldApNode = NodeList::GetNode(otherAp.nodeId);
                if (oldApNode) {
                    Ptr<WifiNetDevice> oldApDev = DynamicCast<WifiNetDevice>(oldApNode->GetDevice(0));
                    if (oldApDev) {
                        Ptr<ApWifiMac> oldApMac = DynamicCast<ApWifiMac>(oldApDev->GetMac());
                        if (oldApMac) {
                            oldApMac->DeassociateSta(staMac, 0);
                            NS_LOG_INFO("[Association] Deassociated STA from old AP " << otherAp.bssid);
                        }
                    }
                }
            }
        }
    }

    // Now add STA to the new AP's client list
    ApMetrics* ap = MetricsHelper::FindApByBssid(g_apMetrics, bssid);
    if (ap) {
        ap->clientList.insert(staMac);
        ap->associatedClients = ap->clientList.size();
        ap->lastUpdate = Simulator::Now();

        NS_LOG_INFO("[Association] AP " << bssid << " now has "
                    << ap->associatedClients << " associated clients");

        NS_LOG_INFO("\n[WiFi Association] ✓ STA Node " << nodeId
                      << " (" << staMac << ") → AP " << bssid
                      << " | Total clients: " << ap->associatedClients);
    } else {
        NS_LOG_WARN("[Association] Could not find AP metrics for BSSID " << bssid);
    }
}

/**
 * Channel utilization callback (for APs)
 * Now includes WiFi vs non-WiFi utilization breakdown based on preamble detection.
 * WiFi util = TX + RX + WiFi CCA busy (signals with preamble detected)
 * Non-WiFi util = Non-WiFi CCA busy (energy without preamble)
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
    NS_LOG_DEBUG("[CALLBACK] OnChannelUtilization triggered for Node " << nodeId
                << " at t=" << timestamp << "s"
                << " | TotalUtil: " << totalUtil << "%"
                << " | WiFiUtil: " << wifiUtil << "%"
                << " | NonWiFiUtil: " << nonWifiUtil << "%"
                << " | TxUtil: " << txUtil << "%"
                << " | RxUtil: " << rxUtil << "%");

    // Inject VirtualInterferer effects into nonWifiUtil for channel allocation trigger
    double viNonWifiCca = 0.0;
    auto viEnv = VirtualInterfererEnvironment::Get();
    if (viEnv && !VirtualInterfererEnvironment::IsBeingDestroyed()) {
        ApMetrics* apCheck = MetricsHelper::FindApByNodeId(g_apMetrics, nodeId);
        if (apCheck) {
            Ptr<Node> node = NodeList::GetNode(nodeId);
            if (node) {
                Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
                if (mobility) {
                    Vector pos = mobility->GetPosition();
                    InterferenceEffect effect = viEnv->GetAggregateEffect(pos, apCheck->channel);
                    viNonWifiCca = effect.nonWifiCcaPercent;
                }
            }
        }
    }
    // Combine measured nonWifiUtil with VI-injected CCA
    double effectiveNonWifiUtil = nonWifiUtil + viNonWifiCca;

    // Update AP metrics with MAC layer statistics
    ApMetrics* ap = MetricsHelper::FindApByNodeId(g_apMetrics, nodeId);
    if (ap) {
        ap->channelUtilization = totalUtil;  // Already 0-100%, don't divide again
        ap->wifiChannelUtilization = wifiUtil;
        ap->nonWifiChannelUtilization = effectiveNonWifiUtil;
        ap->bytesSent = bytesSent;
        ap->bytesReceived = bytesReceived;
        ap->lastUpdate = Simulator::Now();

        // Check non-WiFi threshold for reactive channel scoring trigger (uses VI-injected CCA)
        if (effectiveNonWifiUtil > g_nonWifiThreshold) {
            Time now = Simulator::Now();
            // Initialize lastTrigger to a time that allows first trigger to fire
            Time lastTrigger = Seconds(-100);  // Default: allow trigger
            if (g_lastNonWifiTrigger.count(nodeId) > 0) {
                lastTrigger = g_lastNonWifiTrigger[nodeId];
            }
            if (now - lastTrigger >= g_nonWifiTriggerCooldown) {
                g_lastNonWifiTrigger[nodeId] = now;
                std::cout << "[REACTIVE] Non-WiFi threshold exceeded for AP " << nodeId
                          << " (" << effectiveNonWifiUtil << "% [RF:" << nonWifiUtil
                          << "% + VI:" << viNonWifiCca << "%] > " << g_nonWifiThreshold << "%) at t="
                          << now.GetSeconds() << "s - triggering channel scoring" << std::endl;
                // Schedule immediately (0 delay) to run after current event processing
                Simulator::ScheduleNow(&PerformChannelScoring);
            }
        }

        // Update BSS Load IE channel utilization (convert 0-100% to 0-255 scale for IEEE 802.11)
        uint8_t channelUtilScaled = static_cast<uint8_t>((totalUtil * 255.0) / 100.0);
        uint8_t wifiUtilScaled = static_cast<uint8_t>((wifiUtil * 255.0) / 100.0);
        uint8_t nonWifiUtilScaled = static_cast<uint8_t>((effectiveNonWifiUtil * 255.0) / 100.0);

        ApWifiMac::SetChannelUtilization(ap->bssid, channelUtilScaled);
        ApWifiMac::SetWifiChannelUtilization(ap->bssid, wifiUtilScaled);
        ApWifiMac::SetNonWifiChannelUtilization(ap->bssid, nonWifiUtilScaled);

        NS_LOG_DEBUG("[CCA] Node " << nodeId << " (" << ap->bssid
                     << "): total=" << totalUtil << "% (" << (int)channelUtilScaled << "/255)"
                     << ", wifi=" << wifiUtil << "% (" << (int)wifiUtilScaled << "/255)"
                     << ", nonWifi=" << nonWifiUtil << "% (" << (int)nonWifiUtilScaled << "/255)");
    } else {
        NS_LOG_WARN("[CALLBACK] OnChannelUtilization: AP Node " << nodeId << " not found in g_apMetrics");
    }
}

/**
 * Scanning radio channel utilization callback (for multi-channel monitoring)
 * Tracks channel utilization across all scanning channels
 * Now includes WiFi vs non-WiFi utilization breakdown
 */
// DFS channels in 5 GHz band (UNII-2, UNII-2e)
static const std::set<uint8_t> g_dfsChannels = {52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144};
static const double DFS_RADAR_NONWIFI_THRESHOLD = 30.0;  // Radar typically shows ~50% nonWifi when active

// Forward declaration for DFS handler
void HandleDfsRadarDetection(uint32_t nodeId, uint8_t channel);

void OnScanningChannelUtilization(uint32_t nodeId, double timestamp, double totalUtil,
                                   double wifiUtil, double nonWifiUtil,
                                   double txUtil, double rxUtil,
                                   double wifiCcaUtil, double nonWifiCcaUtil,
                                   double txTime, double rxTime,
                                   double wifiCcaTime, double nonWifiCcaTime,
                                   double idleTime, uint64_t bytesSent, uint64_t bytesReceived,
                                   double throughput)
{
    // Find the AP by node ID to get its operating BSSID
    ApMetrics* ap = MetricsHelper::FindApByNodeId(g_apMetrics, nodeId);
    if (ap) {
        Mac48Address bssid = ap->bssid;

        // Get the scanning device to query current channel
        auto deviceIt = g_apScanningDevices.find(bssid);
        if (deviceIt != g_apScanningDevices.end()) {
            Ptr<WifiNetDevice> scanningDevice = deviceIt->second;
            Ptr<WifiPhy> scanningPhy = scanningDevice->GetPhy();

            // Get current channel number from PHY
            uint8_t currentChannel = scanningPhy->GetChannelNumber();

            // Store all utilization values for this channel
            ScanningChannelUtil& scanUtil = g_scanningChannelUtilization[bssid][currentChannel];
            scanUtil.totalUtil = totalUtil;
            scanUtil.wifiUtil = wifiUtil;

            // Inject VirtualInterferer effects into nonWifiUtil at the source
            // This ensures all downstream consumers (channel scoring, etc.) see VI effects
            double effectiveNonWifiUtil = nonWifiUtil;
            auto viEnv = VirtualInterfererEnvironment::Get();
            if (viEnv && !VirtualInterfererEnvironment::IsBeingDestroyed()) {
                Ptr<Node> node = NodeList::GetNode(nodeId);
                if (node) {
                    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
                    if (mobility) {
                        Vector pos = mobility->GetPosition();
                        InterferenceEffect effect = viEnv->GetAggregateEffect(pos, currentChannel);
                        if (effect.nonWifiCcaPercent > 0) {
                            effectiveNonWifiUtil += effect.nonWifiCcaPercent;
                            // Cap at 100%
                            if (effectiveNonWifiUtil > 100.0) {
                                effectiveNonWifiUtil = 100.0;
                            }
                        }
                    }
                }
            }
            scanUtil.nonWifiUtil = effectiveNonWifiUtil;

            NS_LOG_DEBUG("[Scanning CCA] AP " << bssid << " Channel " << (int)currentChannel
                         << ": total=" << totalUtil << "% (WiFi=" << wifiUtil
                         << "%, NonWiFi=" << effectiveNonWifiUtil << "% [RF:" << nonWifiUtil << "%])");

            // DFS RADAR DETECTION: Check if scanning a DFS channel with high non-WiFi utilization
            if (g_dfsChannels.count(currentChannel) > 0 && effectiveNonWifiUtil > DFS_RADAR_NONWIFI_THRESHOLD) {
                HandleDfsRadarDetection(nodeId, currentChannel);
            }
        }
    }
}

// ============================================================================
// LINK MEASUREMENT REPORT CALLBACK
// ============================================================================

/**
 * @brief Callback for link measurement reports (802.11k)
 *
 * This callback is triggered when a STA receives a Link Measurement Report
 * from an AP in response to a Link Measurement Request.
 *
 * @param from MAC address of the device that sent this report (typically an AP)
 * @param report Link measurement report containing RSSI, SNR, and other metrics
 */
void
OnLinkMeasurementReport(Mac48Address staMac, Mac48Address from, LinkMeasurementReport report)
{
    // Deduplication: only log once per unique (timestamp, AP, STA) triplet
    // This prevents log spam when multiple STAs receive reports from the same AP simultaneously
    static std::set<std::tuple<uint64_t, std::string, std::string>> loggedReports;

    double rssiDbm = report.GetRcpiDbm();
    double snrDb = report.GetRsniDb();
    uint64_t timestampMs = Simulator::Now().GetMilliSeconds();

    // Convert MAC addresses to strings for use in set (ensures proper comparison)
    std::ostringstream apOss, staOss;
    apOss << from;
    staOss << staMac;
    std::string apStr = apOss.str();
    std::string staStr = staOss.str();

    // Create unique key: (timestamp in ms, AP MAC string, STA MAC string)
    auto key = std::make_tuple(timestampMs, apStr, staStr);

    // Only log if we haven't seen this exact report yet
    auto insertResult = loggedReports.insert(key);
    if (insertResult.second)
    {
        NS_LOG_INFO("[LinkMeasurement] t=" << std::fixed << std::setprecision(1)
                      << Simulator::Now().GetSeconds() << "s"
                      << " | From AP: " << from
                      << " → To STA: " << staMac
                      << " | RSSI: " << std::fixed << std::setprecision(1) << rssiDbm << " dBm"
                      << " | SNR: " << std::fixed << std::setprecision(1) << snrDb << " dB");
    }

    // Update AP metrics with RSSI/SNR data (similar to OnChannelUtilization)
    ApMetrics* ap = MetricsHelper::FindApByBssid(g_apMetrics, from);
    if (ap) {
        // Create connection ID: "staMac->apBssid"
        std::ostringstream connIdOss;
        connIdOss << staMac << "->" << from;
        std::string connId = connIdOss.str();

        // Update existing entry or store in pending if entry doesn't exist yet
        auto it = ap->connectionMetrics.find(connId);
        if (it != ap->connectionMetrics.end()) {
            // Entry exists, update apView RSSI/SNR
            it->second.apViewRSSI = rssiDbm;
            it->second.apViewSNR = snrDb;
            it->second.lastUpdate = Simulator::Now();

            NS_LOG_DEBUG("[LinkMeasurement] Updated connection metrics for " << connId
                         << " | RSSI: " << rssiDbm << " dBm, SNR: " << snrDb << " dB");
        } else {
            // Entry doesn't exist yet - store in pending
            g_pendingConnectionData[connId].apViewRSSI = rssiDbm;
            g_pendingConnectionData[connId].apViewSNR = snrDb;
            g_pendingConnectionData[connId].hasApView = true;
            g_pendingConnectionData[connId].lastUpdate = Simulator::Now();

            NS_LOG_DEBUG("[LinkMeasurement] Stored pending apView data for " << connId
                         << " | RSSI: " << rssiDbm << " dBm, SNR: " << snrDb << " dB");
        }

        ap->lastUpdate = Simulator::Now();
    } else {
        NS_LOG_WARN("[LinkMeasurement] AP not found for BSSID: " << from);
    }

    // Periodic cleanup to prevent memory growth (keep last 10 seconds of data)
    if (loggedReports.size() > 1000)
    {
        uint64_t oldestToKeep = (Simulator::Now() - Seconds(10.0)).GetMilliSeconds();
        auto it = loggedReports.begin();
        while (it != loggedReports.end())
        {
            if (std::get<0>(*it) < oldestToKeep)  // First element is timestamp
            {
                it = loggedReports.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

// ============================================================================
// AP LINK MEASUREMENT REPORT CALLBACK (for staViewRSSI/SNR)
// ============================================================================

/**
 * AP Link Measurement Report Callback
 * Called when AP receives Link Measurement Report FROM a STA
 * Contains STA's view of the AP signal (staViewRSSI/SNR)
 *
 * @param apMac MAC address of the AP receiving this report
 * @param from MAC address of the STA that sent this report
 * @param report Link measurement report containing RSSI, SNR from STA's perspective
 */
void
OnApLinkMeasurementReport(Mac48Address apMac, Mac48Address from, LinkMeasurementReport report)
{
    // Deduplication: only log once per unique (timestamp, AP, STA) triplet
    static std::set<std::tuple<uint64_t, std::string, std::string>> loggedReports;

    double rssiDbm = report.GetRcpiDbm();  // STA's measurement of AP's signal
    double snrDb = report.GetRsniDb();     // STA's SNR when receiving from AP
    uint64_t timestampMs = Simulator::Now().GetMilliSeconds();

    // Convert MAC addresses to strings
    std::ostringstream apOss, staOss;
    apOss << apMac;
    staOss << from;
    std::string apStr = apOss.str();
    std::string staStr = staOss.str();

    // Create unique key: (timestamp in ms, AP MAC string, STA MAC string)
    auto key = std::make_tuple(timestampMs, apStr, staStr);

    // Only log if we haven't seen this exact report yet
    auto insertResult = loggedReports.insert(key);
    if (insertResult.second)
    {
        NS_LOG_INFO("[AP-LinkMeasurement] t=" << std::fixed << std::setprecision(1)
                      << Simulator::Now().GetSeconds() << "s"
                      << " | AP: " << apMac
                      << " ← STA: " << from
                      << " | STA's view - RSSI: " << std::fixed << std::setprecision(1) << rssiDbm << " dBm"
                      << " | SNR: " << std::fixed << std::setprecision(1) << snrDb << " dB");
    }

    // Update AP metrics with staView RSSI/SNR
    ApMetrics* ap = MetricsHelper::FindApByBssid(g_apMetrics, apMac);
    if (ap) {
        // Create connection ID: "staMac->apBssid"
        std::ostringstream connIdOss;
        connIdOss << from << "->" << apMac;
        std::string connId = connIdOss.str();

        // Update existing entry or store in pending if entry doesn't exist yet
        auto it = ap->connectionMetrics.find(connId);
        if (it != ap->connectionMetrics.end()) {
            // Entry exists, update staView RSSI/SNR
            it->second.staViewRSSI = rssiDbm;
            it->second.staViewSNR = snrDb;
            it->second.lastUpdate = Simulator::Now();

            NS_LOG_DEBUG("[AP-LinkMeasurement] Updated connection metrics for " << connId
                         << " | staView RSSI: " << rssiDbm << " dBm, SNR: " << snrDb << " dB");
        } else {
            // Entry doesn't exist yet - store in pending
            g_pendingConnectionData[connId].staViewRSSI = rssiDbm;
            g_pendingConnectionData[connId].staViewSNR = snrDb;
            g_pendingConnectionData[connId].hasStaView = true;
            g_pendingConnectionData[connId].lastUpdate = Simulator::Now();

            NS_LOG_DEBUG("[AP-LinkMeasurement] Stored pending staView data for " << connId
                         << " | RSSI: " << rssiDbm << " dBm, SNR: " << snrDb << " dB");
        }

        ap->lastUpdate = Simulator::Now();
    } else {
        NS_LOG_WARN("[AP-LinkMeasurement] AP not found for BSSID: " << apMac);
    }

    // Periodic cleanup to prevent memory growth (keep last 10 seconds of data)
    if (loggedReports.size() > 1000)
    {
        uint64_t oldestToKeep = (Simulator::Now() - Seconds(10.0)).GetMilliSeconds();
        auto it = loggedReports.begin();
        while (it != loggedReports.end())
        {
            if (std::get<0>(*it) < oldestToKeep)
            {
                it = loggedReports.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

// ============================================================================
// MCS TRACKING (OPERATING PHY MONITOR)
// ============================================================================

/**
 * @brief MonitorSnifferRx callback to capture MCS values from data packets
 *
 * This callback is connected to the operating PHY (not the scanning PHY) of both APs and STAs.
 * It captures MCS values from actual data transmissions and stores them directly in g_apMetrics.
 *
 * @param context NS-3 trace context (contains node/device info)
 * @param packet Received packet
 * @param channelFreq Channel frequency
 * @param txVector Contains MCS and transmission parameters
 * @param mpdu MPDU information
 * @param signalNoise Signal and noise levels
 * @param staId Station ID
 */
void
OnMcsMonitorSnifferRx(std::string context,
                      Ptr<const Packet> packet,
                      uint16_t channelFreq,
                      WifiTxVector txVector,
                      MpduInfo mpdu,
                      SignalNoiseDbm signalNoise,
                      uint16_t staId)
{
    // Get the WiFi mode
    WifiMode mode = txVector.GetMode();

    // Check if this is an HT/VHT/HE mode (MCS only exists for these)
    WifiModulationClass modClass = mode.GetModulationClass();
    if (modClass != WIFI_MOD_CLASS_HT &&
        modClass != WIFI_MOD_CLASS_VHT &&
        modClass != WIFI_MOD_CLASS_HE) {
        // Legacy mode (802.11a/b/g) - no MCS value, skip
        return;
    }

    // Extract MCS value from transmission vector
    uint8_t mcs = mode.GetMcsValue();

    // Parse WiFi MAC header to get source and destination addresses
    WifiMacHeader hdr;
    Ptr<Packet> copy = packet->Copy();
    uint32_t headerSize = copy->RemoveHeader(hdr);

    if (headerSize == 0) {
        return; // Failed to parse header
    }

    // Only process data frames
    if (!hdr.IsData()) {
        return;
    }

    Mac48Address src = hdr.GetAddr2();  // Transmitter address
    Mac48Address dst = hdr.GetAddr1();  // Receiver address

    // Determine if this is uplink (STA→AP) or downlink (AP→STA)
    // We need to identify which is the AP and which is the STA
    Mac48Address apMac;
    Mac48Address staMac;
    bool isUplink = false;

    // Check if destination is an AP (exists in g_apMetrics)
    ApMetrics* dstAp = MetricsHelper::FindApByBssid(g_apMetrics, dst);
    if (dstAp) {
        // Destination is AP → this is uplink (STA→AP)
        isUplink = true;
        apMac = dst;
        staMac = src;
    } else {
        // Check if source is an AP
        ApMetrics* srcAp = MetricsHelper::FindApByBssid(g_apMetrics, src);
        if (srcAp) {
            // Source is AP → this is downlink (AP→STA)
            isUplink = false;
            apMac = src;
            staMac = dst;
        } else {
            // Neither source nor destination is a known AP, skip
            return;
        }
    }

    // Create connection ID: "staMac->apMac"
    std::ostringstream connIdOss;
    connIdOss << staMac << "->" << apMac;
    std::string connId = connIdOss.str();

    // Find AP in metrics
    ApMetrics* ap = MetricsHelper::FindApByBssid(g_apMetrics, apMac);
    if (!ap) {
        return;
    }

    // Update existing entry or store in pending if entry doesn't exist yet
    auto it = ap->connectionMetrics.find(connId);
    if (it != ap->connectionMetrics.end()) {
        // Entry exists, update MCS
        if (isUplink) {
            it->second.uplinkMCS = mcs;
        } else {
            it->second.downlinkMCS = mcs;
        }
        it->second.lastUpdate = Simulator::Now();

        NS_LOG_DEBUG("[MCS] Updated " << connId
                     << " | " << (isUplink ? "Uplink" : "Downlink") << " MCS: " << (int)mcs);
    } else {
        // Entry doesn't exist yet - store in pending
        if (isUplink) {
            g_pendingConnectionData[connId].uplinkMCS = mcs;
            g_pendingConnectionData[connId].hasUplinkMCS = true;
        } else {
            g_pendingConnectionData[connId].downlinkMCS = mcs;
            g_pendingConnectionData[connId].hasDownlinkMCS = true;
        }
        g_pendingConnectionData[connId].lastUpdate = Simulator::Now();

        NS_LOG_DEBUG("[MCS] Stored pending " << (isUplink ? "uplink" : "downlink") << " MCS data for " << connId
                     << " | MCS: " << (int)mcs);
    }
}


/**
 * @brief Process beacon measurements from dual-phy-sniffer
 *
 * Periodically queries beacon data from apDualPhySniffer and updates:
 * - Attribute 2: Per-channel RSSI of neighbor APs
 * - Attribute 3: Number of BSSIDs per channel
 * - Attribute 4: Neighbor AP configuration
 */
void
ProcessBeaconMeasurements(DualPhySnifferHelper* apDualPhySniffer)
{
    // Build a set of known AP BSSIDs for filtering (only include real APs as neighbors)
    std::set<Mac48Address> knownApBssids;
    for (const auto& [nid, metrics] : g_apMetrics) {
        knownApBssids.insert(metrics.bssid);
    }

    // Process beacon data for each AP
    for (auto& [nodeId, apMetrics] : g_apMetrics) {
        Mac48Address receiverBssid = apMetrics.bssid;

        // Get all beacons received by this AP's scanning radio
        std::vector<BeaconInfo> beacons = apDualPhySniffer->GetBeaconsReceivedBy(receiverBssid);

        // Build per-channel data: Map<Channel, Map<NeighborBSSID, BeaconInfo>>
        std::map<uint8_t, std::map<Mac48Address, BeaconInfo>> channelBeacons;

        // Organize beacons by channel
        for (const auto& beacon : beacons) {
            Mac48Address transmitterBssid = beacon.bssid;

            // Skip self-beacons
            if (receiverBssid == transmitterBssid) {
                continue;
            }

            // FILTER: Only include beacons from known APs (skip bogus/unknown BSSIDs)
            if (knownApBssids.find(transmitterBssid) == knownApBssids.end()) {
                continue;
            }

            channelBeacons[beacon.channel][transmitterBssid] = beacon;
        }

        // Get all channels that have CCA data for this AP
        std::set<uint8_t> allChannels;
        auto utilIt = g_scanningChannelUtilization.find(receiverBssid);
        if (utilIt != g_scanningChannelUtilization.end()) {
            for (const auto& [ch, util] : utilIt->second) {
                allChannels.insert(ch);
            }
        }
        // Also add channels where beacons were detected
        for (const auto& [ch, neighbors] : channelBeacons) {
            allChannels.insert(ch);
        }

        // Always include the AP's own operating channel
        allChannels.insert(apMetrics.channel);

        // Pre-create all channel entries to avoid map reallocation issues
        for (uint8_t channel : allChannels) {
            if (apMetrics.scanningChannelData.find(channel) == apMetrics.scanningChannelData.end()) {
                apMetrics.scanningChannelData[channel] = ChannelScanData();
            }
        }

        // Populate ApMetrics.scanningChannelData for ALL channels (with or without beacons)
        for (uint8_t channel : allChannels) {
            ChannelScanData& scanData = apMetrics.scanningChannelData[channel];

            // When scanning channel == operating channel, use operating PHY's CCA
            scanData.channelUtilization = 0.0;
            scanData.wifiChannelUtilization = 0.0;
            scanData.nonWifiChannelUtilization = 0.0;

            if (channel == apMetrics.channel) {
                // For operating channel, use operating PHY's values
                // apMetrics.nonWifiChannelUtilization already includes VI injection from OnChannelUtilization
                scanData.channelUtilization = apMetrics.channelUtilization;
                scanData.wifiChannelUtilization = apMetrics.wifiChannelUtilization;
                scanData.nonWifiChannelUtilization = apMetrics.nonWifiChannelUtilization;  // VI-injected
            } else {
                if (utilIt != g_scanningChannelUtilization.end()) {
                    auto channelUtilIt = utilIt->second.find(channel);
                    if (channelUtilIt != utilIt->second.end()) {
                        const ScanningChannelUtil& scanUtil = channelUtilIt->second;
                        scanData.channelUtilization = scanUtil.totalUtil;
                        scanData.wifiChannelUtilization = scanUtil.wifiUtil;
                        scanData.nonWifiChannelUtilization = scanUtil.nonWifiUtil;
                    }
                }
            }

            // Get beacon data (may be empty)
            auto beaconIt = channelBeacons.find(channel);
            if (beaconIt != channelBeacons.end()) {
                const auto& neighbors = beaconIt->second;
                scanData.bssidCount = neighbors.size();

                // Build a new neighbors list (avoids potential memory issues with clear/realloc)
                std::vector<ChannelNeighborInfo> newNeighbors;
                newNeighbors.reserve(neighbors.size());

                for (const auto& [neighborBssid, beaconInfo] : neighbors) {
                    ChannelNeighborInfo neighborInfo{};  // Zero-initialize

                    // Convert Mac48Address to string using ostringstream (SAME as connectionMetrics)
                    std::ostringstream bssidOss;
                    bssidOss << neighborBssid;
                    neighborInfo.bssid = bssidOss.str();

                    neighborInfo.rssi = beaconInfo.rssi;
                    neighborInfo.channel = beaconInfo.channel;

                    // Use parsed IE values from BeaconInfo
                    neighborInfo.channelWidth = beaconInfo.channelWidth;         // From HT/VHT/HE IE
                    neighborInfo.staCount = beaconInfo.staCount;                 // From BSS Load IE
                    neighborInfo.channelUtil = beaconInfo.channelUtilization;    // From BSS Load IE
                    neighborInfo.wifiUtil = beaconInfo.wifiUtilization;          // From BSS Load IE AAC high byte
                    neighborInfo.nonWifiUtil = beaconInfo.nonWifiUtilization;    // From BSS Load IE AAC low byte

                    newNeighbors.push_back(neighborInfo);
                }

                // Assign the new vector (move semantics, no double-free)
                scanData.neighbors = std::move(newNeighbors);
            } else {
                // No beacons detected on this channel
                scanData.bssidCount = 0;
                scanData.neighbors.clear();
            }
        }
    }


    // Note: Reschedule removed - this function is now called directly from CollectFlowMonitorStats
    // to avoid pointer lifetime issues with apDualPhySniffer
}


// ============================================================================
// FLOW MONITOR STATISTICS COLLECTION
// ============================================================================

/**
 * Helper struct to aggregate flow metrics by connection
 */
struct FlowData {
    double uplinkDelay = 0.0;
    double downlinkDelay = 0.0;
    double uplinkJitter = 0.0;
    double downlinkJitter = 0.0;
    uint64_t uplinkPackets = 0;
    uint64_t downlinkPackets = 0;
    uint64_t uplinkBytes = 0;
    uint64_t downlinkBytes = 0;
    double uplinkDuration = 0.0;
    double downlinkDuration = 0.0;
    uint32_t uplinkLost = 0;
    uint32_t downlinkLost = 0;
    uint32_t uplinkTx = 0;
    uint32_t downlinkTx = 0;
    std::string staMac;
    std::string apBssid;
    uint32_t staNodeId = 0;    // Config node ID
    uint32_t staSimNodeId = 0; // Simulation node ID (ns-3 internal)
};

// Forward declaration
void SendMetricsToKafka(Ptr<KafkaProducer> producer);

/**
 * Periodic FlowMonitor statistics collection
 */
void CollectFlowMonitorStats()
{

    // Call ProcessBeaconMeasurements first to ensure scanningChannelData is up-to-date
    if (g_apDualPhySniffer) {
        ProcessBeaconMeasurements(g_apDualPhySniffer);
    }

    if (!g_flowMonitor || !g_flowClassifier) {
        NS_LOG_WARN("[FlowMonitor] Monitor or classifier not initialized");
        return;
    }

    g_flowMonitor->CheckForLostPackets();

    FlowMonitor::FlowStatsContainer stats = g_flowMonitor->GetFlowStats();

    Time now = Simulator::Now();
    double timestamp = now.GetSeconds();


    NS_LOG_DEBUG("[FlowMonitor] Collecting stats at t=" << timestamp
                 << "s, " << stats.size() << " flows");

    // Group flows by connection ID (staMac->apBssid)
    std::map<std::string, FlowData> connectionFlows;

    // Process all flows
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        FlowId flowId = it->first;
        FlowMonitor::FlowStats& flowStats = it->second;

        Ipv4FlowClassifier::FiveTuple tuple = g_flowClassifier->FindFlow(flowId);

        if (flowStats.rxPackets == 0) {
            continue;
        }

        // Determine flow direction using g_dsAddress
        bool isUplink = (tuple.destinationAddress == g_dsAddress);
        bool isDownlink = (tuple.sourceAddress == g_dsAddress);

        if (!isUplink && !isDownlink) {
            // Flow doesn't involve DS node, skip
            continue;
        }

        // Get STA IP address
        Ipv4Address staIp = isUplink ? tuple.sourceAddress : tuple.destinationAddress;

        // Find STA node by IP
        Ptr<Node> staNode = nullptr;
        uint32_t staNodeId = 0;
        Mac48Address staMac;
        Mac48Address apBssid;

        for (uint32_t i = 0; i < NodeList::GetNNodes(); i++) {
            Ptr<Node> node = NodeList::GetNode(i);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            if (!ipv4 || ipv4->GetNInterfaces() < 2) {
                continue;
            }

            Ipv4Address nodeIp = ipv4->GetAddress(1, 0).GetAddress();
            if (nodeIp == staIp) {
                staNode = node;
                staNodeId = node->GetId();

                // Get STA MAC address
                Ptr<NetDevice> staDevice = node->GetDevice(0);
                Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(staDevice);
                if (wifiDev) {
                    staMac = wifiDev->GetMac()->GetAddress();

                    // Get associated AP BSSID
                    Ptr<StaWifiMac> staMacLayer = DynamicCast<StaWifiMac>(wifiDev->GetMac());
                    if (staMacLayer && staMacLayer->IsAssociated()) {
                        apBssid = staMacLayer->GetBssid(0);  // linkId=0 for single-link
                    }
                }
                break;
            }
        }

        if (!staNode || apBssid.IsBroadcast()) {
            NS_LOG_DEBUG("[FlowMonitor] STA not found or not associated for IP " << staIp);
            continue;
        }

        // Create connection ID
        std::ostringstream ossConnId;
        ossConnId << staMac << "->" << apBssid;
        std::string connId = ossConnId.str();

        // Initialize FlowData if needed
        if (connectionFlows.find(connId) == connectionFlows.end()) {
            connectionFlows[connId] = FlowData();
            std::ostringstream staOss;
            staOss << staMac;
            connectionFlows[connId].staMac = staOss.str();
            std::ostringstream apOss;
            apOss << apBssid;
            connectionFlows[connId].apBssid = apOss.str();
            // Store both simulation node ID and config node ID
            connectionFlows[connId].staSimNodeId = staNodeId;  // ns-3 internal ID
            auto nodeIdIt = g_simNodeIdToConfigNodeId.find(staNodeId);
            connectionFlows[connId].staNodeId = (nodeIdIt != g_simNodeIdToConfigNodeId.end())
                ? nodeIdIt->second
                : staNodeId;  // Config ID (fallback to sim ID if not found)
        }

        FlowData& flowData = connectionFlows[connId];

        // Calculate metrics for this flow
        double delayMs = (flowStats.delaySum.GetSeconds() / flowStats.rxPackets) * 1000.0;
        double jitterMs = 0.0;
        if (flowStats.rxPackets > 1) {
            jitterMs = (flowStats.jitterSum.GetSeconds() / (flowStats.rxPackets - 1)) * 1000.0;
        }
        double duration = (flowStats.timeLastRxPacket - flowStats.timeFirstTxPacket).GetSeconds();

        // Aggregate based on direction
        if (isUplink) {
            flowData.uplinkDelay = delayMs;
            flowData.uplinkJitter = jitterMs;
            flowData.uplinkPackets += flowStats.rxPackets;
            flowData.uplinkBytes += flowStats.rxBytes;
            flowData.uplinkDuration = duration;
            flowData.uplinkLost += flowStats.lostPackets;
            flowData.uplinkTx += flowStats.txPackets;
        } else { // downlink
            flowData.downlinkDelay = delayMs;
            flowData.downlinkJitter = jitterMs;
            flowData.downlinkPackets += flowStats.rxPackets;
            flowData.downlinkBytes += flowStats.rxBytes;
            flowData.downlinkDuration = duration;
            flowData.downlinkLost += flowStats.lostPackets;
            flowData.downlinkTx += flowStats.txPackets;
        }

        NS_LOG_DEBUG("[FlowMonitor] Flow " << flowId << " (" << (isUplink ? "UPLINK" : "DOWNLINK") << "): "
                     << tuple.sourceAddress << " -> " << tuple.destinationAddress
                     << " | ConnId: " << connId
                     << " | Delay: " << delayMs << "ms, Jitter: " << jitterMs << "ms");
    }


    // Now create ConnectionMetrics for each connection
    for (const auto& [connId, flowData] : connectionFlows) {
        // Build ConnectionMetrics
        ConnectionMetrics conn;
        conn.staAddress = flowData.staMac;
        conn.APAddress = flowData.apBssid;
        // Note: nodeId, simNodeId, and positions are updated in SendMetricsToKafka

        // RTT Latency = uplink delay + downlink delay
        conn.meanRTTLatency = flowData.uplinkDelay + flowData.downlinkDelay;

        // Jitter = average of both directions
        double jitterSum = flowData.uplinkJitter + flowData.downlinkJitter;
        double jitterCount = (flowData.uplinkPackets > 0 ? 1 : 0) + (flowData.downlinkPackets > 0 ? 1 : 0);
        conn.jitterMs = jitterCount > 0 ? (jitterSum / jitterCount) : 0.0;

        // Packet count = sum of both directions
        conn.packetCount = flowData.uplinkPackets + flowData.downlinkPackets;

        // Throughput calculations
        conn.uplinkThroughputMbps = 0.0;
        if (flowData.uplinkDuration > 0) {
            conn.uplinkThroughputMbps = (flowData.uplinkBytes * 8.0) / flowData.uplinkDuration / 1000000.0;
        }

        conn.downlinkThroughputMbps = 0.0;
        if (flowData.downlinkDuration > 0) {
            conn.downlinkThroughputMbps = (flowData.downlinkBytes * 8.0) / flowData.downlinkDuration / 1000000.0;
        }

        // Packet loss rate = combined from both directions
        uint32_t totalLost = flowData.uplinkLost + flowData.downlinkLost;
        uint32_t totalTx = flowData.uplinkTx + flowData.downlinkTx;
        uint32_t totalRx = flowData.uplinkPackets + flowData.downlinkPackets;

        conn.packetLossRate = 0.0;
        if (totalTx > 0) {
            // Use rxPackets to calculate loss: lostPackets = txPackets - rxPackets
            // This avoids FlowMonitor's potentially incorrect lostPackets field
            uint32_t actualLost = (totalTx > totalRx) ? (totalTx - totalRx) : 0;
            conn.packetLossRate = ((double)actualLost / totalTx) * 100.0;

            // Debug output if values seem wrong
            if (totalLost > totalTx) {
                NS_LOG_WARN("[FlowMonitor] Suspicious packet loss for " << connId
                           << ": reported lost=" << totalLost << " > tx=" << totalTx
                           << " | Recalculated: actualLost=" << actualLost
                           << " (tx=" << totalTx << " - rx=" << totalRx << ")");
            }
        }

        // MAC Retry Rate from MAC layer traces
        conn.MACRetryRate = 0.0;
        if (g_macRetryCount.find(flowData.staNodeId) != g_macRetryCount.end()) {
            uint32_t retryCount = g_macRetryCount[flowData.staNodeId];
            if (totalTx > 0) {
                conn.MACRetryRate = ((double)retryCount / totalTx) * 100.0;
            }
        }

        conn.lastUpdate = now;

        // Update AP metrics
        ApMetrics* ap = MetricsHelper::FindApByBssid(g_apMetrics, Mac48Address(flowData.apBssid.c_str()));
        if (ap) {
            // Check if entry already exists (may have RSSI/SNR/MCS from callbacks)
            auto existingEntry = ap->connectionMetrics.find(connId);
            if (existingEntry != ap->connectionMetrics.end()) {
                // Preserve existing values set by callbacks
                conn.apViewRSSI = existingEntry->second.apViewRSSI;
                conn.apViewSNR = existingEntry->second.apViewSNR;
                conn.staViewRSSI = existingEntry->second.staViewRSSI;
                conn.staViewSNR = existingEntry->second.staViewSNR;
                conn.uplinkMCS = existingEntry->second.uplinkMCS;
                conn.downlinkMCS = existingEntry->second.downlinkMCS;
            } else {
                // New entry - check for pending data from callbacks that fired before FlowMonitor
                auto pendingIt = g_pendingConnectionData.find(connId);
                if (pendingIt != g_pendingConnectionData.end()) {
                    // Apply pending data
                    if (pendingIt->second.hasApView) {
                        conn.apViewRSSI = pendingIt->second.apViewRSSI;
                        conn.apViewSNR = pendingIt->second.apViewSNR;
                    } else {
                        conn.apViewRSSI = 0.0;
                        conn.apViewSNR = 0.0;
                    }
                    if (pendingIt->second.hasStaView) {
                        conn.staViewRSSI = pendingIt->second.staViewRSSI;
                        conn.staViewSNR = pendingIt->second.staViewSNR;
                    } else {
                        conn.staViewRSSI = 0.0;
                        conn.staViewSNR = 0.0;
                    }
                    if (pendingIt->second.hasUplinkMCS) {
                        conn.uplinkMCS = pendingIt->second.uplinkMCS;
                    } else {
                        conn.uplinkMCS = 0;
                    }
                    if (pendingIt->second.hasDownlinkMCS) {
                        conn.downlinkMCS = pendingIt->second.downlinkMCS;
                    } else {
                        conn.downlinkMCS = 0;
                    }

                    NS_LOG_DEBUG("[FlowMonitor] Applied pending data for new connection: " << connId);

                    // Remove from pending storage
                    g_pendingConnectionData.erase(pendingIt);
                } else {
                    // No pending data, initialize to 0
                    conn.apViewRSSI = 0.0;
                    conn.apViewSNR = 0.0;
                    conn.staViewRSSI = 0.0;
                    conn.staViewSNR = 0.0;
                    conn.uplinkMCS = 0;
                    conn.downlinkMCS = 0;
                }
            }

            // Now update/overwrite the entry with new FlowMonitor data + preserved/applied RSSI/SNR/MCS
            ap->connectionMetrics[connId] = conn;
            ap->lastUpdate = now;

            NS_LOG_DEBUG("[FlowMonitor] Updated AP connection metrics: " << connId
                         << " | RTT: " << conn.meanRTTLatency << "ms"
                         << " | Uplink: " << conn.uplinkThroughputMbps << " Mbps"
                         << " | Downlink: " << conn.downlinkThroughputMbps << " Mbps"
                         << " | Loss: " << conn.packetLossRate << "%"
                         << " | MACRetry: " << conn.MACRetryRate << "%");
        } else {
            NS_LOG_WARN("[FlowMonitor] AP not found for BSSID: " << flowData.apBssid);
        }
    }


    // Aggregate connection metrics to calculate AP-level throughput and bytes
    // This provides application-layer metrics consistent with per-connection data
    for (auto& apEntry : g_apMetrics) {
        uint32_t apNodeId = apEntry.first;
        ApMetrics& ap = apEntry.second;

        // Update AP position from mobility model
        Ptr<Node> apNode = NodeList::GetNode(apNodeId);
        if (apNode) {
            Ptr<MobilityModel> mobility = apNode->GetObject<MobilityModel>();
            if (mobility) {
                Vector pos = mobility->GetPosition();
                ap.positionX = pos.x;
                ap.positionY = pos.y;
                ap.positionZ = pos.z;
            }
        }

        double totalUplinkThroughput = 0.0;
        double totalDownlinkThroughput = 0.0;

        // Sum all connection metrics for this AP
        for (const auto& connEntry : ap.connectionMetrics) {
            const ConnectionMetrics& conn = connEntry.second;
            totalUplinkThroughput += conn.uplinkThroughputMbps;
            totalDownlinkThroughput += conn.downlinkThroughputMbps;
        }

        // Update AP-level metrics with aggregated values from FlowMonitor (application layer)
        ap.throughputMbps = totalUplinkThroughput + totalDownlinkThroughput;

        // Note: bytesSent and bytesReceived are populated by OnChannelUtilization (MAC layer)
        // They include all WiFi overhead (headers, retransmissions, ACKs) - don't overwrite them here

        NS_LOG_DEBUG("[FlowMonitor] AP " << ap.bssid << " aggregated throughput: "
                     << ap.throughputMbps << " Mbps (uplink: " << totalUplinkThroughput
                     << ", downlink: " << totalDownlinkThroughput << ")");
    }


    // Reschedule this function for next collection
    Simulator::Schedule(Seconds(1.0), &CollectFlowMonitorStats);


    // Immediately trigger SendMetricsToKafka after stats collection
    // This ensures metrics are sent AFTER they're populated, not before
    if (g_kafkaProducer) {
        Simulator::ScheduleNow(&SendMetricsToKafka, g_kafkaProducer);
    }

}

// ============================================================================
// KAFKA METRICS SENDER
// ============================================================================

/**
 * Update all AP and STA metrics in the Kafka producer
 */
void SendMetricsToKafka(Ptr<KafkaProducer> producer)
{
    NS_LOG_FUNCTION(producer);

    if (!producer) {
        NS_LOG_WARN("Kafka producer is null, skipping metrics send");
        return;
    }

    try {
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

            // Update connection metrics
            for (auto& connEntry : ap.connectionMetrics) {
                ConnectionMetrics& conn = connEntry.second;

                // Find STA node by MAC address string comparison
                for (uint32_t i = 0; i < NodeList::GetNNodes(); i++) {
                    Ptr<Node> node = NodeList::GetNode(i);
                    if (node->GetNDevices() == 0) continue;
                    Ptr<NetDevice> dev = node->GetDevice(0);
                    Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(dev);
                    if (wifiDev) {
                        std::ostringstream nodeOss;
                        nodeOss << wifiDev->GetMac()->GetAddress();
                        if (nodeOss.str() == conn.staAddress) {
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
                            break;
                        }
                    }
                }
            }
        }

        // Send all AP metrics
        for (const auto& entry : g_apMetrics) {
            const ApMetrics& metrics = entry.second;
            producer->UpdateApMetrics(metrics.bssid, metrics);
        }

        // Reset metrics after sending (metrics should only accumulate between sends)
        NS_LOG_INFO("[Metrics] Resetting flow-based metrics after Kafka send");

        // Reset FlowMonitor to start fresh accumulation for next measurement period
        // CollectFlowMonitorStats (runs every 1.0s) will repopulate connectionMetrics
        // from newly accumulated FlowMonitor data before the next Kafka send
        if (g_flowMonitor) {
            g_flowMonitor->ResetAllStats();
            NS_LOG_DEBUG("[Metrics] ✓ FlowMonitor stats reset");
        }

        // Clear connection metrics and reset MAC retry counts
        // NOTE: We do NOT reset channelUtilization, bytesSent, bytesReceived, throughputMbps
        // because OnChannelUtilization OVERWRITES these values (doesn't accumulate).
        // They are already per-window measurements from WifiCcaMonitor.
        for (auto& entry : g_apMetrics) {
            ApMetrics& ap = entry.second;

            // Clear connection metrics for next measurement period
            ap.connectionMetrics.clear();

            NS_LOG_DEBUG("[Metrics] ✓ Cleared connection metrics for AP Node " << ap.nodeId
                         << " (BSSID: " << ap.bssid << ")");
        }

        // Reset MAC retry counts for STAs
        g_macRetryCount.clear();

        NS_LOG_INFO("[Metrics] ✓ Flow-based metrics reset (channel util preserved)");

    } catch (const std::exception& e) {
        NS_LOG_ERROR("[Kafka] Exception while updating metrics: " << e.what());
    }
}

// ============================================================================
// KAFKA CONSUMER CALLBACK
// ============================================================================

/**
 * Helper function to apply PHY parameters with deferred channel switching
 * This avoids the assertion failure when switching channels during active transmission
 */
void ApplyPhyParameters(Ptr<LeverConfig> config, ApParameters params, uint32_t nodeId)
{
    NS_LOG_FUNCTION(config << nodeId);

    NS_LOG_UNCOND("\n╔════════════════════════════════════════════════════════════════════════════╗");
    NS_LOG_UNCOND("║ [KAFKA → PHY] Applying Parameters to AP Node " << nodeId);
    NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════╣");
    NS_LOG_UNCOND("║  Simulation Time: " << Simulator::Now().GetSeconds() << "s");
    NS_LOG_UNCOND("║  Tx Power:        " << params.txPowerStartDbm << " dBm");
    NS_LOG_UNCOND("║  CCA Threshold:   " << params.ccaEdThresholdDbm << " dBm");
    NS_LOG_UNCOND("║  RX Sensitivity:  " << params.rxSensitivityDbm << " dBm");
    NS_LOG_UNCOND("║  Target Channel:  " << +params.channelNumber);
    NS_LOG_UNCOND("╚════════════════════════════════════════════════════════════════════════════╝");

    // Apply non-channel parameters immediately (these are safe)
    config->SetTxPower(params.txPowerStartDbm);
    config->SetCcaEdThreshold(params.ccaEdThresholdDbm);
    config->SetRxSensitivity(params.rxSensitivityDbm);

    // Schedule channel switch with a larger delay to avoid PHY state conflicts
    // This allows any ongoing transmissions to complete before switching
    // Increased from 10ms to 100ms to ensure PHY is idle
    NS_LOG_UNCOND("[KAFKA → PHY] Scheduling channel switch to CH" << +params.channelNumber
                  << " for Node " << nodeId << " in 100ms...\n");

    Simulator::Schedule(MilliSeconds(100), [params, nodeId]() {
        NS_LOG_UNCOND("\n[KAFKA → PHY] Triggering channel switch for Node " << nodeId << " → CH" << +params.channelNumber);

        // Use LeverApi::SwitchChannel() instead of LeverConfig::SwitchChannel()
        // This triggers ApWifiMac::SwitchChannel() which propagates to associated STAs
        auto leverApiIter = g_apLeverApis.find(nodeId);
        if (leverApiIter != g_apLeverApis.end() && leverApiIter->second) {
            leverApiIter->second->SwitchChannel(params.channelNumber);

            // UPDATE g_apMetrics to reflect the new channel, band, AND width (same as channel scoring does)
            auto metricsIt = g_apMetrics.find(nodeId);
            if (metricsIt != g_apMetrics.end()) {
                WifiPhyBand newBand = (params.channelNumber >= 36) ? WIFI_PHY_BAND_5GHZ : WIFI_PHY_BAND_2_4GHZ;

                // Get updated channel width from LeverApi
                uint16_t newWidth = leverApiIter->second->GetChannelWidth();

                metricsIt->second.channel = params.channelNumber;
                metricsIt->second.band = newBand;
                metricsIt->second.channelWidth = newWidth;
                NS_LOG_UNCOND("[KAFKA → PHY] ✓ Updated g_apMetrics[" << nodeId
                              << "].channel = " << +params.channelNumber << " @ " << newWidth << "MHz ("
                              << (newBand == WIFI_PHY_BAND_5GHZ ? "5GHz" : "2.4GHz") << ")");
            }
            NS_LOG_UNCOND("[KAFKA → PHY] ✓ Using LeverApi::SwitchChannel (propagates to STAs)");
        } else {
            NS_LOG_ERROR("[KAFKA → PHY] ✗ No LeverApi found for node " << nodeId
                         << " - STAs will NOT follow!");
        }
    });
}

/**
 * Callback when Kafka parameters are received from Bayesian Optimizer
 */
void OnParametersReceived(std::string bssid, ApParameters params)
{
    NS_LOG_UNCOND("\n" << std::string(80, '='));
    NS_LOG_UNCOND("║ [KAFKA CONSUMER] Parameters Received from Bayesian Optimizer");
    NS_LOG_UNCOND(std::string(80, '='));
    NS_LOG_UNCOND("  Target BSSID:      " << bssid);
    NS_LOG_UNCOND("  Simulation Time:   " << Simulator::Now().GetSeconds() << "s");
    NS_LOG_UNCOND(std::string(80, '-'));

    NS_LOG_UNCOND("  Tx Power:          " << params.txPowerStartDbm << " dBm");
    NS_LOG_UNCOND("  CCA-ED Threshold:  " << params.ccaEdThresholdDbm << " dBm");
    NS_LOG_UNCOND("  RX Sensitivity:    " << params.rxSensitivityDbm << " dBm");
    NS_LOG_UNCOND("  Channel:           " << +params.channelNumber);
    NS_LOG_UNCOND("  Bandwidth:         " << params.channelWidthMhz << " MHz");
    NS_LOG_UNCOND("  Band:              " << params.band);
    NS_LOG_UNCOND("  OBSS_PD:           " << params.obssPd << " dBm [⚠ NOT SUPPORTED]");
    NS_LOG_UNCOND(std::string(80, '-'));

    // Find corresponding LeverConfig by BSSID
    Ptr<LeverConfig> config = nullptr;

    if (g_bssidToLeverConfig.find(bssid) != g_bssidToLeverConfig.end()) {
        config = g_bssidToLeverConfig[bssid];
        NS_LOG_UNCOND("  → Matched BSSID: " << bssid);
    } else if (bssid == "default" || bssid == "*" || bssid == "00:00:00:00:00:00") {
        NS_LOG_UNCOND("  → Broadcast mode: Applying to ALL APs");

        for (auto& entry : g_apLeverConfigs) {
            config = entry.second;
            uint32_t nodeId = entry.first;

            std::string apBssid = "unknown";
            ApMetrics* ap = MetricsHelper::FindApByNodeId(g_apMetrics, nodeId);
            if (ap) {
                std::ostringstream oss;
                oss << ap->bssid;
                apBssid = oss.str();
            }

            NS_LOG_UNCOND("  → Applying to AP Node " << nodeId << " (BSSID: " << apBssid << ")");

            // Use helper function with deferred channel switching
            ApplyPhyParameters(config, params, nodeId);

            // Clear connection metrics for this AP (new optimization iteration)
            if (ap) {
                size_t oldCount = ap->connectionMetrics.size();
                ap->connectionMetrics.clear();
                NS_LOG_UNCOND("  → Cleared " << oldCount << " connection metrics for AP Node " << nodeId);
            }

            NS_LOG_UNCOND("  ✓ Parameters scheduled for AP Node " << nodeId << "!");
        }

        NS_LOG_UNCOND("[Metrics] ✓ Connection metrics cleared for all APs (new optimization iteration)");
        NS_LOG_UNCOND(std::string(80, '=') << "\n");
        return;
    } else {
        NS_LOG_UNCOND("  ✗ No matching AP found for BSSID: " << bssid);
        NS_LOG_UNCOND(std::string(80, '=') << "\n");
        return;
    }

    if (!config) {
        NS_LOG_UNCOND("  ✗ No LeverConfig found for BSSID: " << bssid);
        NS_LOG_UNCOND(std::string(80, '=') << "\n");
        return;
    }

    // Use helper function with deferred channel switching
    uint32_t nodeId = 0;
    for (auto& entry : g_apLeverConfigs) {
        if (entry.second == config) {
            nodeId = entry.first;
            break;
        }
    }
    ApplyPhyParameters(config, params, nodeId);

    // Clear connection metrics for this specific AP (new optimization iteration)
    ApMetrics* ap = MetricsHelper::FindApByBssid(g_apMetrics, Mac48Address(bssid.c_str()));
    if (ap) {
        size_t oldCount = ap->connectionMetrics.size();
        ap->connectionMetrics.clear();
        NS_LOG_UNCOND("  → Cleared " << oldCount << " connection metrics for AP " << bssid);
    }

    NS_LOG_UNCOND("  ✓ Parameters scheduled for AP with BSSID " << bssid << "!");
    NS_LOG_UNCOND(std::string(80, '=') << "\n");
}

// ============================================================================
// MAIN SIMULATION
// ============================================================================

int main(int argc, char* argv[])
{
    // ========================================================================
    // COMMAND-LINE PARAMETERS
    // ========================================================================

    std::string configFile = "config-simulation.json";
    double rssiThreshold = -65.0;
    double measurementInterval = 1.0;
    bool verbose = false;
    bool enablePcap = false;

    // Kafka configuration
    std::string kafkaBroker = "localhost:9092";
    std::string simulationId = "sim-001";

    bool enableKafkaProducer = true;
    std::string kafkaUpstreamTopic = "ns3-metrics";
    double kafkaUpstreamInterval = 1.0;  // Send Kafka metrics every 1.0s

    bool enableKafkaConsumer = true;
    std::string kafkaDownstreamTopic = "optimization-commands";
    std::string kafkaConsumerGroupId = "ns3-config-sim-consumer";
    double kafkaConsumerPollInterval = 0.5;

    CommandLine cmd(__FILE__);
    cmd.AddValue("configFile", "Path to JSON configuration file", configFile);
    cmd.AddValue("rssiThreshold", "RSSI threshold for roaming (dBm)", rssiThreshold);
    cmd.AddValue("measurementInterval", "Link measurement interval (seconds)", measurementInterval);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.AddValue("enablePcap", "Enable PCAP tracing", enablePcap);

    cmd.AddValue("kafkaBroker", "Kafka broker address (host:port)", kafkaBroker);
    cmd.AddValue("simulationId", "Simulation ID", simulationId);
    cmd.AddValue("enableKafkaProducer", "Enable Kafka Producer", enableKafkaProducer);
    cmd.AddValue("enableKafkaConsumer", "Enable Kafka Consumer", enableKafkaConsumer);
    cmd.AddValue("kafkaUpstreamTopic", "Kafka topic for sending metrics", kafkaUpstreamTopic);
    cmd.AddValue("kafkaUpstreamInterval", "Kafka metrics send interval (seconds)", kafkaUpstreamInterval);
    cmd.AddValue("kafkaDownstreamTopic", "Kafka topic for receiving commands", kafkaDownstreamTopic);
    cmd.AddValue("kafkaConsumerGroupId", "Kafka consumer group ID", kafkaConsumerGroupId);
    cmd.AddValue("kafkaConsumerPollInterval", "Kafka consumer poll interval (seconds)", kafkaConsumerPollInterval);

    cmd.Parse(argc, argv);

    // Enable logging
    if (verbose) {
        LogComponentEnable("ConfigSimulation", LOG_LEVEL_INFO);
        // Reduce internal module logging to WARNING to eliminate repetitive debug output
        LogComponentEnable("AutoRoamingKvHelper", LOG_LEVEL_WARN);
        LogComponentEnable("LinkMeasurementProtocol", LOG_LEVEL_WARN);
        LogComponentEnable("WaypointMobilityHelper", LOG_LEVEL_INFO);
    }
    // LogComponentEnable("KafkaConsumer", LOG_LEVEL_INFO);
    // LogComponentEnable("KafkaProducer", LOG_LEVEL_INFO);
    // LogComponentEnable("BssTm11vHelper", LOG_LEVEL_INFO);

    // ========================================================================
    // TCP CONFIGURATION - Optimized for Wireless Roaming Robustness
    // ========================================================================

    // ===== INFINITE RETRIES FOR ROAMING SCENARIOS =====
    // Set high retry counts to prevent socket destruction during roaming
    // Default is only 6 retries (~63s), which can cause disconnections
    // 100 retries provides effectively infinite persistence for simulation duration
    Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(100));
    Config::SetDefault("ns3::TcpSocket::ConnCount", UintegerValue(100));

    // ===== RETRANSMISSION TIMEOUT CONFIGURATION =====
    // MinRto: Minimum retransmission timeout (RFC 6298 standard)
    // Note: MaxRto does not exist in ns-3, RTO grows exponentially without upper bound
    // ConnTimeout: Timeout for initial connection establishment
    Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(Seconds(1.0)));
    Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(Seconds(3.0)));

    // ===== ENHANCED BUFFERS FOR HANDOFF RESILIENCE =====
    // Increased from default 128KB to 256KB to absorb temporary disruptions
    // Allows buffering ~177 full-sized packets during AP transitions
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(262144));  // 256 KB
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(262144));  // 256 KB

    // ===== SEGMENT SIZE OPTIMIZATION =====
    // 1448 bytes optimal for 802.11ac: MTU(1500) - IP(20) - TCP(20) - Options(12)
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));

    // ===== INITIAL CONGESTION WINDOW =====
    // RFC 6928 standard initial window for fast connection startup
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));

    // ===== DELAYED ACKNOWLEDGMENT =====
    // Send ACK after receiving 2 packets (standard TCP behavior)
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(2));

    // ===== TCP CONGESTION CONTROL VARIANT =====
    // TcpCubic: Well-tested, validated variant optimized for high-bandwidth networks
    // (Alternative: TcpWestwoodPlus for wireless, but lacks validation per ns-3 docs)
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpCubic::GetTypeId()));

    NS_LOG_INFO("\n╔═══════════════════════════════════════════════════════════╗");
    NS_LOG_INFO("║  Config-Based Simulation - Waypoint Mobility + Roaming   ║");
    NS_LOG_INFO("╚═══════════════════════════════════════════════════════════╝\n");

    // ========================================================================
    // PARSE CONFIG FILE
    // ========================================================================

    NS_LOG_INFO("[Config] Parsing configuration file: " << configFile);
    SimulationConfigData config = SimulationConfigParser::ParseFile(configFile);

    if (rssiThreshold == -70.0)  // Default value, use config if available
    {
        rssiThreshold = config.bssOrchestrationRssiThreshold;
        NS_LOG_INFO("[Config] Using RSSI threshold from config: " << rssiThreshold << " dBm");
    }

    NS_LOG_INFO("[Config] Configuration loaded successfully:");
    NS_LOG_INFO("  Simulation Time: " << config.simulationTime << " s");
    NS_LOG_INFO("  APs: " << config.aps.size());
    NS_LOG_INFO("  STAs: " << config.stas.size());
    NS_LOG_INFO("  Waypoints: " << config.waypoints.size());
    NS_LOG_INFO("  RSSI Threshold: " << rssiThreshold << " dBm");
    NS_LOG_INFO("  Link Measurement Interval: " << measurementInterval << " s\n");

    // Initialize channel scoring helper from config
    g_channelScoringConfig.weightBssid = config.channelScoring.weightBssid;
    g_channelScoringConfig.weightRssi = config.channelScoring.weightRssi;
    g_channelScoringConfig.weightNonWifi = config.channelScoring.weightNonWifi;
    g_channelScoringConfig.weightOverlap = config.channelScoring.weightOverlap;
    g_channelScoringConfig.nonWifiDiscardThreshold = config.channelScoring.nonWifiDiscardThreshold;
    g_channelScoringHelper.SetConfig(g_channelScoringConfig);

    NS_LOG_INFO("[Config] Channel Scoring: enabled=" << config.channelScoring.enabled
                << ", weights=[bssid=" << g_channelScoringConfig.weightBssid
                << ", rssi=" << g_channelScoringConfig.weightRssi
                << ", nonWifi=" << g_channelScoringConfig.weightNonWifi
                << ", overlap=" << g_channelScoringConfig.weightOverlap << "]"
                << ", nonWifiThreshold=" << g_channelScoringConfig.nonWifiDiscardThreshold << "%\n");

    // ========================================================================
    // CREATE NODES
    // ========================================================================

    NodeContainer apNodes;
    NodeContainer staNodes;
    NodeContainer dsNode;

    // Create AP nodes one by one and build sim node ID -> config nodeId mapping
    for (size_t i = 0; i < config.aps.size(); i++) {
        Ptr<Node> apNode = CreateObject<Node>();
        apNodes.Add(apNode);
        uint32_t simNodeId = apNode->GetId();
        uint32_t configNodeId = config.aps[i].nodeId;
        g_apSimNodeIdToConfigNodeId[simNodeId] = configNodeId;
    }

    // Create STA nodes one by one and build sim node ID -> config nodeId mapping
    for (size_t i = 0; i < config.stas.size(); i++) {
        Ptr<Node> staNode = CreateObject<Node>();
        staNodes.Add(staNode);
        uint32_t simNodeId = staNode->GetId();
        uint32_t configNodeId = config.stas[i].nodeId;
        g_simNodeIdToConfigNodeId[simNodeId] = configNodeId;
    }

    dsNode.Create(1);

    NS_LOG_INFO("[Setup] Created " << config.aps.size() << " AP nodes, "
                  << config.stas.size() << " STA nodes, 1 DS node");

    // ========================================================================
    // MOBILITY SETUP
    // ========================================================================

    // APs: ConstantPosition from config
    MobilityHelper mobilityAp;
    mobilityAp.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityAp.Install(apNodes);

    for (size_t i = 0; i < config.aps.size(); i++) {
        apNodes.Get(i)->GetObject<MobilityModel>()->SetPosition(config.aps[i].position);
    }

    NS_LOG_INFO("[Setup] AP positions from config:");
    for (size_t i = 0; i < config.aps.size(); i++) {
        Vector pos = config.aps[i].position;
        NS_LOG_INFO("        AP" << i << ": (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
    }

    // STAs: Waypoint-based mobility from config
    Ptr<WaypointGrid> waypointGrid = CreateObject<WaypointGrid>();
    for (const auto& wp : config.waypoints) {
        waypointGrid->AddWaypoint(wp.id, wp.position);
    }

    WaypointMobilityHelper mobilityHelper;
    mobilityHelper.SetWaypointGrid(waypointGrid);
    mobilityHelper.Install(staNodes, config.stas);

    NS_LOG_INFO("[Setup] STAs configured with waypoint mobility:");
    for (size_t i = 0; i < config.stas.size(); i++) {
        NS_LOG_INFO("        STA" << i << ": starts at waypoint " << config.stas[i].initialWaypointId
                      << ", velocity [" << config.stas[i].transferVelocityMin << ", "
                      << config.stas[i].transferVelocityMax << "] m/s");
    }

    // DS node: Center of AP positions
    Vector dsPosition(0, 0, 0);
    for (const auto& ap : config.aps) {
        dsPosition.x += ap.position.x;
        dsPosition.y += ap.position.y;
        dsPosition.z += ap.position.z;
    }
    dsPosition.x /= config.aps.size();
    dsPosition.y /= config.aps.size();
    dsPosition.z /= config.aps.size();

    MobilityHelper mobilityDs;
    mobilityDs.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityDs.Install(dsNode);
    dsNode.Get(0)->GetObject<MobilityModel>()->SetPosition(dsPosition);

    // ========================================================================
    // YANS WIFI CHANNEL SETUP
    // ========================================================================

    // Create ONE shared channel for all devices (critical for roaming)
    YansWifiChannelHelper channelHelper;
    channelHelper.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                     "Exponent", DoubleValue(3.0),
                                     "ReferenceDistance", DoubleValue(1.0),
                                     "ReferenceLoss", DoubleValue(46.6777));
    channelHelper.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");

    Ptr<YansWifiChannel> sharedChannel = channelHelper.Create();

    NS_LOG_INFO("[Setup] Yans WiFi channel created with log-distance path loss");
    NS_LOG_INFO("[Setup] Using SHARED channel for all devices (essential for roaming)");

    // ========================================================================
    // WIFI SETUP - 5 GHZ BAND
    // ========================================================================

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);  // HE - supports BSS Color for OBSS detection
    wifi.SetRemoteStationManager("ns3::IdealWifiManager");

    YansWifiPhyHelper yansPhy;
    yansPhy.SetChannel(sharedChannel);

    WifiMacHelper wifiMac;
    Ssid ssid = Ssid("ConfigSimulation-Network");

    // Extract AP channels and separate by band
    std::vector<uint8_t> apChannels;
    std::vector<uint8_t> apChannels5GHz;
    std::vector<uint8_t> apChannels2_4GHz;
    NodeContainer apNodes5GHz;
    NodeContainer apNodes2_4GHz;

    for (size_t i = 0; i < config.aps.size(); i++) {
        uint8_t channel = config.aps[i].channel;
        apChannels.push_back(channel);

        // Channel 1-14 = 2.4 GHz, 36+ = 5 GHz
        if (channel >= 36) {
            apChannels5GHz.push_back(channel);
            apNodes5GHz.Add(apNodes.Get(i));
        } else {
            apChannels2_4GHz.push_back(channel);
            apNodes2_4GHz.Add(apNodes.Get(i));
        }
    }

    // Install APs separately by band
    NetDeviceContainer apDevices;

    if (apChannels5GHz.size() > 0) {
        NetDeviceContainer apDevices5GHz = SimulationHelper::InstallApDevices(
            wifi, yansPhy, wifiMac, apNodes5GHz, apChannels5GHz, ssid,
            20.0, 20, WIFI_PHY_BAND_5GHZ);
        apDevices.Add(apDevices5GHz);
        NS_LOG_INFO("[Setup] Installed " << apChannels5GHz.size() << " APs on 5 GHz");
    }

    if (apChannels2_4GHz.size() > 0) {
        NetDeviceContainer apDevices2_4GHz = SimulationHelper::InstallApDevices(
            wifi, yansPhy, wifiMac, apNodes2_4GHz, apChannels2_4GHz, ssid,
            20.0, 20, WIFI_PHY_BAND_2_4GHZ);
        apDevices.Add(apDevices2_4GHz);
        NS_LOG_INFO("[Setup] Installed " << apChannels2_4GHz.size() << " APs on 2.4 GHz");
    }

    NS_LOG_INFO("[Setup] Total APs installed: " << apDevices.GetN());
    for (size_t i = 0; i < config.aps.size(); i++) {
        NS_LOG_INFO("        AP" << i << " (CH" << (int)apChannels[i] << ", "
                    << (apChannels[i] >= 36 ? "5GHz" : "2.4GHz") << ")");
    }

    // Set unique BSS Color for each AP (802.11ax feature for OBSS detection)
    std::cout << "[Setup] Setting BSS Colors for 802.11ax OBSS detection:" << std::endl;
    for (size_t i = 0; i < apDevices.GetN(); i++) {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(apDevices.Get(i));
        if (apDev) {
            Ptr<HeConfiguration> heConfig = apDev->GetHeConfiguration();
            if (heConfig) {
                uint8_t bssColor = static_cast<uint8_t>(i + 1);  // BSS Color 1, 2, 3, 4...
                heConfig->m_bssColor = bssColor;
                std::cout << "  AP" << i << " (Node " << apNodes.Get(i)->GetId()
                          << "): BSS Color = " << (int)bssColor << std::endl;
            }
        }
    }

    // STAs all start on first AP's channel (will discover and roam to other APs)
    std::vector<uint8_t> staChannels(config.stas.size(), apChannels[0]);
    WifiPhyBand staBand = (apChannels[0] >= 36) ? WIFI_PHY_BAND_5GHZ : WIFI_PHY_BAND_2_4GHZ;

    NetDeviceContainer staDevices = SimulationHelper::InstallStaDevices(
        wifi, yansPhy, wifiMac, staNodes, staChannels, ssid,
        20, staBand);

    NS_LOG_INFO("[Setup] STAs installed (all starting on CH" << (int)apChannels[0]
                << ", " << (staBand == WIFI_PHY_BAND_5GHZ ? "5GHz" : "2.4GHz") << ")");

    // ========================================================================
    // DISTRIBUTION SYSTEM (CSMA + BRIDGE)
    // ========================================================================

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("1Gbps"));
    csma.SetChannelAttribute("Delay", TimeValue(MicroSeconds(10)));

    NodeContainer csmaNodes;
    csmaNodes.Add(apNodes);
    csmaNodes.Add(dsNode);

    NetDeviceContainer csmaDevices = csma.Install(csmaNodes);

    NetDeviceContainer apCsmaDevices;
    for (uint32_t i = 0; i < apNodes.GetN(); i++) {
        apCsmaDevices.Add(csmaDevices.Get(i));
    }

    NetDeviceContainer dsDevices;
    dsDevices.Add(csmaDevices.Get(apNodes.GetN()));

    // Bridge WiFi and CSMA on each AP
    BridgeHelper bridge;
    for (uint32_t i = 0; i < apNodes.GetN(); i++) {
        NetDeviceContainer bridgePair;
        bridgePair.Add(apDevices.Get(i));
        bridgePair.Add(apCsmaDevices.Get(i));
        bridge.Install(apNodes.Get(i), bridgePair);
    }

    NS_LOG_INFO("[Setup] CSMA backbone created with bridges on all APs");

    // ========================================================================
    // INTERNET STACK
    // ========================================================================

    InternetStackHelper internet;
    internet.Install(staNodes);
    internet.Install(dsNode);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer staInterfaces = ipv4.Assign(staDevices);
    Ipv4InterfaceContainer dsInterfaces = ipv4.Assign(dsDevices);

    NS_LOG_INFO("[Setup] IP addresses assigned (10.1.1.0/24)");

    // ========================================================================
    // GET MAC ADDRESSES
    // ========================================================================

    std::vector<Ptr<WifiNetDevice>> apNetDevices = SimulationHelper::GetWifiNetDevices(apDevices);
    std::vector<Ptr<WifiNetDevice>> staNetDevices = SimulationHelper::GetWifiNetDevices(staDevices);

    std::vector<Mac48Address> apMacs;
    std::vector<Mac48Address> staMacs;

    for (auto& dev : apNetDevices) {
        apMacs.push_back(dev->GetMac()->GetAddress());
    }
    for (auto& dev : staNetDevices) {
        staMacs.push_back(dev->GetMac()->GetAddress());
    }

    NS_LOG_INFO("\nMAC Addresses:");
    for (size_t i = 0; i < apMacs.size(); i++) {
        NS_LOG_INFO("  AP" << i << ": " << apMacs[i] << " (CH" << (int)apChannels[i] << ")");
    }
    for (size_t i = 0; i < staMacs.size(); i++) {
        NS_LOG_INFO("  STA" << i << ": " << staMacs[i]);
    }

    // ========================================================================
    // VIRTUAL INTERFERERS
    // ========================================================================

    if (config.virtualInterferers.enabled)
    {
        NS_LOG_INFO("\n[VirtualInterferer] Setting up virtual interferers...");

        VirtualInterfererHelper viHelper;

        // Set environment configuration
        VirtualInterfererEnvironmentConfig envConfig;
        envConfig.updateInterval = Seconds(config.virtualInterferers.updateInterval);
        viHelper.SetEnvironmentConfig(envConfig);

        // Create microwave interferers
        for (const auto& mwConfig : config.virtualInterferers.microwaves)
        {
            auto microwave = CreateObject<MicrowaveInterferer>();
            microwave->SetPosition(mwConfig.position);
            microwave->SetTxPowerDbm(mwConfig.txPowerDbm);
            microwave->SetDutyCycle(mwConfig.dutyCycle);

            // Set schedule if configured
            if (mwConfig.schedule.hasSchedule)
            {
                microwave->SetSchedule(Seconds(mwConfig.schedule.onDuration),
                                      Seconds(mwConfig.schedule.offDuration));
                NS_LOG_INFO("  [VirtualInterferer] Microwave schedule: "
                            << mwConfig.schedule.onDuration << "s ON / "
                            << mwConfig.schedule.offDuration << "s OFF");
            }

            if (mwConfig.active)
            {
                microwave->TurnOn();
            }
            else
            {
                microwave->TurnOff();
            }

            microwave->Install();

            if (mwConfig.startTime > 0.0)
            {
                viHelper.ScheduleTurnOn(microwave, Seconds(mwConfig.startTime));
            }

            NS_LOG_INFO("  [VirtualInterferer] Created Microwave: pos=" << mwConfig.position
                        << ", txPower=" << mwConfig.txPowerDbm << " dBm"
                        << ", centerFreq=" << mwConfig.centerFrequencyGHz << " GHz");
        }

        // Create Bluetooth interferers
        for (const auto& btConfig : config.virtualInterferers.bluetooths)
        {
            auto bluetooth = CreateObject<BluetoothInterferer>();
            bluetooth->SetPosition(btConfig.position);
            bluetooth->SetTxPowerDbm(btConfig.txPowerDbm);
            // Note: Hopping now uses std::rand() instead of LFSR seeds

            // Map profile string to enum
            if (btConfig.profile == "HID")
            {
                bluetooth->SetProfile(BluetoothInterferer::HID);
            }
            else if (btConfig.profile == "AUDIO_STREAMING")
            {
                bluetooth->SetProfile(BluetoothInterferer::AUDIO_STREAMING);
            }
            else if (btConfig.profile == "DATA_TRANSFER")
            {
                bluetooth->SetProfile(BluetoothInterferer::DATA_TRANSFER);
            }
            else if (btConfig.profile == "IDLE")
            {
                bluetooth->SetProfile(BluetoothInterferer::IDLE);
            }

            // Set schedule if configured
            if (btConfig.schedule.hasSchedule)
            {
                bluetooth->SetSchedule(Seconds(btConfig.schedule.onDuration),
                                       Seconds(btConfig.schedule.offDuration));
                NS_LOG_INFO("  [VirtualInterferer] Bluetooth schedule: "
                            << btConfig.schedule.onDuration << "s ON / "
                            << btConfig.schedule.offDuration << "s OFF");
            }

            if (btConfig.active)
            {
                bluetooth->TurnOn();
            }
            else
            {
                bluetooth->TurnOff();
            }

            bluetooth->Install();

            if (btConfig.startTime > 0.0)
            {
                viHelper.ScheduleTurnOn(bluetooth, Seconds(btConfig.startTime));
            }

            NS_LOG_INFO("  [VirtualInterferer] Created Bluetooth: pos=" << btConfig.position
                        << ", txPower=" << btConfig.txPowerDbm << " dBm"
                        << ", profile=" << btConfig.profile);
        }

        // Create Cordless phone interferers
        for (const auto& cordlessConfig : config.virtualInterferers.cordless)
        {
            auto cordless = CreateObject<CordlessInterferer>();
            cordless->SetPosition(cordlessConfig.position);
            cordless->SetTxPowerDbm(cordlessConfig.txPowerDbm);
            cordless->SetNumHops(cordlessConfig.numHops);
            cordless->SetHopInterval(cordlessConfig.hopInterval);
            cordless->SetBandwidthMhz(cordlessConfig.bandwidthMhz);
            // Note: Hopping now uses std::rand() instead of LFSR seeds

            // Set schedule if configured
            if (cordlessConfig.schedule.hasSchedule)
            {
                cordless->SetSchedule(Seconds(cordlessConfig.schedule.onDuration),
                                      Seconds(cordlessConfig.schedule.offDuration));
                NS_LOG_INFO("  [VirtualInterferer] Cordless schedule: "
                            << cordlessConfig.schedule.onDuration << "s ON / "
                            << cordlessConfig.schedule.offDuration << "s OFF");
            }

            if (cordlessConfig.active)
            {
                cordless->TurnOn();
            }
            else
            {
                cordless->TurnOff();
            }

            cordless->Install();

            if (cordlessConfig.startTime > 0.0)
            {
                viHelper.ScheduleTurnOn(cordless, Seconds(cordlessConfig.startTime));
            }

            NS_LOG_INFO("  [VirtualInterferer] Created Cordless: pos=" << cordlessConfig.position
                        << ", txPower=" << cordlessConfig.txPowerDbm << " dBm"
                        << ", numHops=" << cordlessConfig.numHops
                        << ", hopInterval=" << cordlessConfig.hopInterval << "s");
        }

        // Create ZigBee interferers
        for (const auto& zbConfig : config.virtualInterferers.zigbees)
        {
            auto zigbee = CreateObject<ZigbeeInterferer>();
            zigbee->SetPosition(zbConfig.position);
            zigbee->SetTxPowerDbm(zbConfig.txPowerDbm);
            zigbee->SetZigbeeChannel(zbConfig.zigbeeChannel);
            zigbee->SetDutyCycle(zbConfig.dutyCycle);

            // Map network type string to enum
            if (zbConfig.networkType == "SENSOR")
            {
                zigbee->SetNetworkType(ZigbeeInterferer::SENSOR);
            }
            else if (zbConfig.networkType == "CONTROL")
            {
                zigbee->SetNetworkType(ZigbeeInterferer::CONTROL);
            }
            else if (zbConfig.networkType == "LIGHTING")
            {
                zigbee->SetNetworkType(ZigbeeInterferer::LIGHTING);
            }

            // Set schedule if configured
            if (zbConfig.schedule.hasSchedule)
            {
                zigbee->SetSchedule(Seconds(zbConfig.schedule.onDuration),
                                    Seconds(zbConfig.schedule.offDuration));
                NS_LOG_INFO("  [VirtualInterferer] ZigBee schedule: "
                            << zbConfig.schedule.onDuration << "s ON / "
                            << zbConfig.schedule.offDuration << "s OFF");
            }

            if (zbConfig.active)
            {
                zigbee->TurnOn();
            }
            else
            {
                zigbee->TurnOff();
            }

            zigbee->Install();

            if (zbConfig.startTime > 0.0)
            {
                viHelper.ScheduleTurnOn(zigbee, Seconds(zbConfig.startTime));
            }

            NS_LOG_INFO("  [VirtualInterferer] Created ZigBee: pos=" << zbConfig.position
                        << ", channel=" << (int)zbConfig.zigbeeChannel
                        << ", networkType=" << zbConfig.networkType);
        }

        // Create Radar interferers
        for (const auto& radarConfig : config.virtualInterferers.radars)
        {
            auto radar = CreateObject<RadarInterferer>();
            radar->SetPosition(radarConfig.position);
            radar->SetTxPowerDbm(radarConfig.txPowerDbm);
            radar->SetDfsChannel(radarConfig.dfsChannel);

            // Map radar type string to enum
            if (radarConfig.radarType == "WEATHER")
            {
                radar->SetRadarType(RadarInterferer::WEATHER);
            }
            else if (radarConfig.radarType == "MILITARY")
            {
                radar->SetRadarType(RadarInterferer::MILITARY);
            }
            else if (radarConfig.radarType == "AVIATION")
            {
                radar->SetRadarType(RadarInterferer::AVIATION);
            }

            // Set schedule if configured
            if (radarConfig.schedule.hasSchedule)
            {
                radar->SetSchedule(Seconds(radarConfig.schedule.onDuration),
                                   Seconds(radarConfig.schedule.offDuration));
                NS_LOG_INFO("  [VirtualInterferer] Radar schedule: "
                            << radarConfig.schedule.onDuration << "s ON / "
                            << radarConfig.schedule.offDuration << "s OFF");
            }

            // Configure channel hopping if specified
            if (!radarConfig.dfsChannels.empty())
            {
                radar->SetDfsChannels(radarConfig.dfsChannels);
                radar->SetHopInterval(Seconds(radarConfig.hopIntervalSec));
                radar->SetRandomHopping(radarConfig.randomHopping);
                std::cout << "[VI Setup] Radar channel hopping: "
                          << radarConfig.dfsChannels.size() << " channels, "
                          << radarConfig.hopIntervalSec << "s interval, "
                          << (radarConfig.randomHopping ? "random" : "sequential") << std::endl;
            }

            // Configure wideband span settings
            radar->SetSpanLength(radarConfig.spanLength);
            radar->SetMaxSpanLength(radarConfig.maxSpanLength);
            radar->SetRandomSpan(radarConfig.randomSpan);

            // Print initial affected channels
            auto initialAffected = radar->GetCurrentlyAffectedChannels();
            std::cout << "[VI Setup] Radar wideband span: ±" << (int)radarConfig.spanLength
                      << " channels (max ±" << (int)radarConfig.maxSpanLength << "), "
                      << (radarConfig.randomSpan ? "random" : "fixed") << " span"
                      << "\n           Initial affected channels: [";
            bool first = true;
            for (uint8_t ch : initialAffected) {
                if (!first) std::cout << ",";
                std::cout << (int)ch;
                first = false;
            }
            std::cout << "]" << std::endl;

            // Store globally for DFS detection to query affected channels
            g_radarInterferer = radar;

            if (radarConfig.active)
            {
                radar->TurnOn();
            }
            else
            {
                radar->TurnOff();
            }

            radar->Install();

            if (radarConfig.startTime > 0.0)
            {
                viHelper.ScheduleTurnOn(radar, Seconds(radarConfig.startTime));
            }

            NS_LOG_INFO("  [VirtualInterferer] Created Radar: pos=" << radarConfig.position
                        << ", txPower=" << radarConfig.txPowerDbm << " dBm"
                        << ", dfsChannel=" << (int)radarConfig.dfsChannel
                        << ", type=" << radarConfig.radarType);
        }

        // Register WiFi devices with virtual interferer environment
        NetDeviceContainer allWifiDevices;
        allWifiDevices.Add(apDevices);
        allWifiDevices.Add(staDevices);
        viHelper.RegisterWifiDevices(allWifiDevices);

        // Start the virtual interferer environment
        auto viEnv = VirtualInterfererEnvironment::Get();
        viEnv->Start();

        NS_LOG_INFO("[VirtualInterferer] ✓ Virtual interferer environment started with "
                    << config.virtualInterferers.microwaves.size() << " microwave, "
                    << config.virtualInterferers.bluetooths.size() << " Bluetooth, "
                    << config.virtualInterferers.cordless.size() << " Cordless, "
                    << config.virtualInterferers.zigbees.size() << " ZigBee, "
                    << config.virtualInterferers.radars.size() << " radar interferers");
    }
    else
    {
        NS_LOG_INFO("\n[VirtualInterferer] Virtual interferers disabled in config");
    }

    // ========================================================================
    // INITIALIZE METRICS STRUCTURES
    // ========================================================================

    NS_LOG_INFO("\n[Metrics] Initializing metric structures...");

    // Initialize AP metrics with proper per-AP band detection
    std::map<uint32_t, ApMetrics> tempApMetrics;
    for (uint32_t i = 0; i < apDevices.GetN(); i++)
    {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(apDevices.Get(i));
        NS_ASSERT_MSG(apDev, "Device is not a WifiNetDevice");
        uint32_t simNodeId = apDev->GetNode()->GetId();

        // Look up config nodeId from mapping
        uint32_t configNodeId = simNodeId;  // Default fallback
        auto nodeIdIt = g_apSimNodeIdToConfigNodeId.find(simNodeId);
        if (nodeIdIt != g_apSimNodeIdToConfigNodeId.end()) {
            configNodeId = nodeIdIt->second;
        }

        // Determine band from channel number
        uint8_t channel = apChannels[i];
        WifiPhyBand band = (channel >= 36) ? WIFI_PHY_BAND_5GHZ : WIFI_PHY_BAND_2_4GHZ;

        // Get channel width from PHY
        Ptr<WifiPhy> phy = apDev->GetPhy();
        uint16_t channelWidth = 20;  // Default to 20 MHz
        if (phy) {
            channelWidth = phy->GetChannelWidth();
        }

        ApMetrics metrics;
        metrics.nodeId = configNodeId;
        metrics.simNodeId = simNodeId;
        metrics.bssid = Mac48Address::ConvertFrom(apDev->GetAddress());
        metrics.channel = channel;
        metrics.band = band;  // Set correct band based on channel
        metrics.channelWidth = channelWidth;  // Set actual channel width
        metrics.associatedClients = 0;
        metrics.channelUtilization = 0.0;
        metrics.throughputMbps = 0.0;
        metrics.bytesSent = 0;
        metrics.bytesReceived = 0;
        metrics.lastUpdate = Seconds(0);

        tempApMetrics[simNodeId] = metrics;

        NS_LOG_INFO("[Metrics] Initialized AP Node (config=" << configNodeId << ", sim=" << simNodeId << "): " << metrics.bssid
                    << " (CH" << (int)channel << " @ " << channelWidth << "MHz, "
                    << (band == WIFI_PHY_BAND_5GHZ ? "5GHz" : "2.4GHz") << ")");
    }
    g_apMetrics = tempApMetrics;

    // ========================================================================
    // CONNECT MAC RETRY TRACES FOR STAs
    // ========================================================================

    NS_LOG_INFO("\n[Traces] Connecting WiFi MAC retry traces for STAs...");
    for (uint32_t i = 0; i < staNetDevices.size(); i++) {
        std::ostringstream path;
        path << "/NodeList/" << staNodes.Get(i)->GetId()
             << "/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/MacTxDataFailed";
        Config::Connect(path.str(), MakeCallback(&OnMacTxDataFailed));
    }
    NS_LOG_INFO("[Traces] ✓ WiFi MAC retry traces connected for " << staNetDevices.size() << " STAs");

    // ========================================================================
    // CONNECT ASSOCIATION TRACES FOR STAs
    // ========================================================================

    NS_LOG_INFO("\n[Traces] Connecting WiFi association traces for STAs...");
    for (uint32_t i = 0; i < staNetDevices.size(); i++) {
        Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(staNetDevices[i]->GetMac());
        if (staMac) {
            std::ostringstream context;
            context << "/NodeList/" << staNodes.Get(i)->GetId()
                    << "/DeviceList/0/$ns3::WifiNetDevice/Mac";

            staMac->TraceConnect("Assoc", context.str(), MakeCallback(&OnStaAssociation));

            NS_LOG_DEBUG("[Traces] Connected association trace for STA Node "
                         << staNodes.Get(i)->GetId());
        }
    }
    NS_LOG_INFO("[Traces] ✓ WiFi association traces connected for " << staNetDevices.size() << " STAs");

    // ========================================================================
    // SETUP ROAMING PROTOCOLS
    // ========================================================================

    NS_LOG_INFO("\n[SimulationHelper] Setting up roaming protocols...");

    std::vector<uint8_t> apOperatingChannels = apChannels;
    std::vector<uint8_t> staOperatingChannels(staChannels);

    // Use scanning channels from config if provided, otherwise default to AP channels
    std::vector<uint8_t> scanningChannels = config.scanningChannels.empty()
                                              ? apChannels
                                              : config.scanningChannels;

    NS_LOG_INFO("[Config] Scanning channels: " << scanningChannels.size() << " channels");
    for (size_t i = 0; i < scanningChannels.size(); i++)
    {
        NS_LOG_INFO("  [" << (i+1) << "] Channel " << (int)scanningChannels[i]);
    }

    // AP DualPhySniffer for Neighbor Protocol
    g_apDualPhySniffer = SimulationHelper::SetupAPDualPhySniffer(
        apDevices,
        apMacs,
        sharedChannel,
        apOperatingChannels,
        scanningChannels,
        MilliSeconds(config.channelHopDurationMs));

    NS_LOG_INFO("[SimulationHelper] ✓ AP DualPhySniffer setup complete");

    // ========================================================================
    // SCANNING RADIO CCA MONITORING (Multi-channel utilization tracking)
    // ========================================================================

    NS_LOG_INFO("\n[Setup] Attaching CCA monitors to AP scanning radios...");
    WifiCcaMonitorHelper scanningCcaHelper;

    // CCA window and update interval synchronized with channel hop duration
    uint32_t hopDuration = config.channelHopDurationMs;
    scanningCcaHelper.SetWindowSize(MilliSeconds(hopDuration));
    scanningCcaHelper.SetUpdateInterval(MilliSeconds(hopDuration));

    NS_LOG_INFO("[CCA-Scanning] Synchronized intervals: hop=" << hopDuration
                << "ms, window=" << hopDuration << "ms, update=" << hopDuration << "ms");

    for (uint32_t i = 0; i < apNodes.GetN(); i++) {
        Ptr<Node> node = apNodes.Get(i);
        uint32_t nodeId = node->GetId();

        // Use DualPhySnifferHelper to get the correct MAC addresses
        Mac48Address operatingMac = g_apDualPhySniffer->GetOperatingMac(nodeId);
        Mac48Address scanningMac = g_apDualPhySniffer->GetScanningMac(nodeId);

        // Find the scanning device by matching its MAC address
        Ptr<WifiNetDevice> scanningDevice = nullptr;
        for (uint32_t d = 0; d < node->GetNDevices(); d++) {
            Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(d));
            if (wifiDev && wifiDev->GetMac()->GetAddress() == scanningMac) {
                scanningDevice = wifiDev;
                break;
            }
        }

        if (scanningDevice) {
            // Store scanning device for later channel queries
            g_apScanningDevices[operatingMac] = scanningDevice;

            // Attach CCA monitor to scanning device
            Ptr<WifiCcaMonitor> scanningMonitor = scanningCcaHelper.Install(scanningDevice);
            g_ccaMonitors.push_back(scanningMonitor);

            scanningMonitor->TraceConnectWithoutContext("ChannelUtilization",
                MakeCallback(&OnScanningChannelUtilization));

            scanningMonitor->Start();

            NS_LOG_INFO("CCA monitor attached to AP" << i << " scanning device (MAC: " << scanningMac << ")");
        } else {
            NS_LOG_WARN("Failed to find scanning device for AP" << i);
        }
    }
    NS_LOG_INFO("[Setup] Scanning CCA monitors attached to " << apNodes.GetN() << " APs");

    // STA DualPhySniffer for Beacon Protocol
    DualPhySnifferHelper* staDualPhySniffer = SimulationHelper::SetupSTADualPhySniffer(
        staDevices,
        staMacs,
        sharedChannel,
        staOperatingChannels,
        scanningChannels,
        MilliSeconds(config.channelHopDurationMs));

    NS_LOG_INFO("[SimulationHelper] ✓ STA DualPhySniffer setup complete");

    // ========================================================================
    // SETUP STA CHANNEL HOPPING (Automatic AP Channel Following)
    // ========================================================================

    NS_LOG_UNCOND("\n[StaChannelHopping] Setting up automatic channel following...");

    StaChannelHoppingHelper staChannelHoppingHelper;
    staChannelHoppingHelper.SetDualPhySniffer(staDualPhySniffer);

    // Scanning delay: Time to wait after disassociation before attempting roaming
    // With 13 channels × configured hop duration (e.g., 0.2s) + buffer ≈ 5s allows full scan
    staChannelHoppingHelper.SetAttribute("ScanningDelay", TimeValue(Seconds(5.0)));

    // Minimum SNR threshold: 0.0 dB = accept any AP (permissive for recovery)
    staChannelHoppingHelper.SetAttribute("MinimumSnr", DoubleValue(0.0));

    // Enable automatic channel following
    staChannelHoppingHelper.SetAttribute("Enabled", BooleanValue(true));

    // Install on all STAs
    std::vector<Ptr<StaChannelHoppingManager>> channelHoppingManagers =
        staChannelHoppingHelper.Install(staDevices);

    NS_LOG_UNCOND("[StaChannelHopping] ✓ Installed on " << channelHoppingManagers.size() << " STAs");
    NS_LOG_UNCOND("[StaChannelHopping]   Scanning Delay: 5.0s");
    NS_LOG_UNCOND("[StaChannelHopping]   Minimum SNR: 0.0 dB");
    NS_LOG_UNCOND("[StaChannelHopping]   Purpose: Failsafe for STAs that lose association");

    // ========================================================================
    // NEIGHBOR PROTOCOL SETUP
    // ========================================================================

    // Neighbor Protocol (dynamic discovery)
    std::vector<ApInfo> neighborTable;  // Empty for dynamic discovery

    std::vector<Ptr<NeighborProtocolHelper>> neighborProtocols =
        SimulationHelper::SetupNeighborProtocol(
            apDevices,
            staDevices,
            apMacs,
            neighborTable,
            g_apDualPhySniffer);

    // Beacon Protocol
    std::vector<Ptr<BeaconProtocolHelper>> beaconProtocols =
        SimulationHelper::SetupBeaconProtocol(
            apDevices,
            staDevices,
            staDualPhySniffer);

    // BSS TM Helper
    std::vector<Ptr<BssTm11vHelper>> bssTmHelpers =
        SimulationHelper::SetupBssTmHelper(apDevices, staDevices);

    // Split protocol vectors
    uint32_t numAps = apDevices.GetN();

    auto [neighborProtocolApList, neighborProtocolStaList] =
        SimulationHelper::SplitProtocolVector(neighborProtocols, numAps);
    auto [beaconProtocolApList, beaconProtocolStaList] =
        SimulationHelper::SplitProtocolVector(beaconProtocols, numAps);
    auto [bssTmHelperApList, bssTmHelperStaList] =
        SimulationHelper::SplitProtocolVector(bssTmHelpers, numAps);

    // Setup roaming helpers for all STAs
    SimulationHelper::AutoRoamingKvHelperContainer roamingContainer =
        SimulationHelper::SetupAutoRoamingKvHelperMulti(
            apDevices,
            staDevices,
            neighborProtocolStaList,
            beaconProtocolStaList,
            beaconProtocolApList,
            bssTmHelperStaList,
            Seconds(measurementInterval),
            rssiThreshold);

    NS_LOG_INFO("[AutoRoamingKvHelper] ✓ All " << roamingContainer.helpers.size()
                  << " STAs configured with roaming orchestration");
    NS_LOG_INFO("[AutoRoamingKvHelper]   - RSSI threshold: " << rssiThreshold << " dBm");
    NS_LOG_INFO("[AutoRoamingKvHelper]   - Measurement interval: " << measurementInterval << " s");
    NS_LOG_INFO("[AutoRoamingKvHelper]   - Chain: Link Meas → Neighbor → Beacon → BSS TM → Roaming");

    // Connect link measurement report traces with STA-specific callbacks
    for (size_t i = 0; i < roamingContainer.staProtocols.size(); i++)
    {
        if (!roamingContainer.staProtocols[i].empty())
        {
            // Get STA MAC address
            Ptr<WifiNetDevice> staDev = DynamicCast<WifiNetDevice>(staDevices.Get(i));
            Mac48Address staMac = Mac48Address::ConvertFrom(staDev->GetAddress());

            // Create callback that captures STA MAC for logging
            roamingContainer.staProtocols[i][0]->TraceConnectWithoutContext(
                "LinkMeasurementReportReceived",
                MakeBoundCallback(&OnLinkMeasurementReport, staMac));
        }
    }

    NS_LOG_INFO("[Traces] ✓ Link measurement report traces connected for all STAs");

    // ========================================================================
    // SETUP AP-INITIATED LINK MEASUREMENTS (for staViewRSSI/SNR)
    // ========================================================================

    NS_LOG_INFO("\n[AP-LinkMeasurement] Setting up AP-initiated link measurements...");

    // Create AP link measurement helper
    ApLinkMeasurementHelper apLinkHelper;
    apLinkHelper.SetMeasurementInterval(Seconds(measurementInterval));  // Match STA interval

    // Connect AP callbacks (APs receive reports FROM STAs)
    // Note: All STA helpers install the same AP protocols, so use the first helper's AP protocol list
    if (!roamingContainer.apProtocols.empty() && !roamingContainer.apProtocols[0].empty())
    {
        for (size_t i = 0; i < apDevices.GetN(); i++)  // Iterate over actual number of APs
        {
            // Get AP MAC address
            Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(apDevices.Get(i));
            Mac48Address apMac = Mac48Address::ConvertFrom(apDev->GetAddress());

            // Connect callback for when AP receives reports FROM STAs
            // Use first helper's AP protocol list (index 0), then get protocol for AP i
            roamingContainer.apProtocols[0][i]->TraceConnectWithoutContext(
                "LinkMeasurementReportReceived",
                MakeBoundCallback(&OnApLinkMeasurementReport, apMac));
        }
    }

    // Register AP devices and protocols (does NOT install - protocols already installed)
    // Use the FIRST helper's AP protocol instances (all helpers have identical AP protocols)
    if (!roamingContainer.apProtocols.empty() && !roamingContainer.apProtocols[0].empty())
    {
        apLinkHelper.RegisterApDevices(apDevices, roamingContainer.apProtocols[0]);
        apLinkHelper.Start();  // Start periodic AP→STA requests

        NS_LOG_INFO("[AP-LinkMeasurement] ✓ AP→STA link measurement helper started");
        NS_LOG_INFO("[AP-LinkMeasurement]   - " << apDevices.GetN() << " APs will send requests to associated STAs");
        NS_LOG_INFO("[AP-LinkMeasurement]   - Interval: " << measurementInterval << " s");
    }
    else
    {
        NS_LOG_WARN("[AP-LinkMeasurement] No AP protocols available for AP link measurement helper");
    }

    // ========================================================================
    // SETUP MCS MONITORING (OPERATING PHY)
    // ========================================================================

    NS_LOG_INFO("[MCS-Monitor] Connecting MonitorSnifferRx to operating PHY for MCS tracking...");

    // Connect to AP operating PHY (not scanning PHY)
    for (uint32_t i = 0; i < apDevices.GetN(); i++)
    {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(apDevices.Get(i));
        uint32_t nodeId = apDev->GetNode()->GetId();
        uint32_t deviceId = apDev->GetIfIndex();

        // Connect to operating PHY's MonitorSnifferRx
        std::stringstream ss;
        ss << "/NodeList/" << nodeId << "/DeviceList/" << deviceId << "/$ns3::WifiNetDevice/Phy/MonitorSnifferRx";
        Config::Connect(ss.str(), MakeCallback(&OnMcsMonitorSnifferRx));

        NS_LOG_INFO("[MCS-Monitor]   - Connected AP Node " << nodeId << " Device " << deviceId);
    }

    // Connect to STA operating PHY (not scanning PHY)
    for (uint32_t i = 0; i < staDevices.GetN(); i++)
    {
        Ptr<WifiNetDevice> staDev = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        uint32_t nodeId = staDev->GetNode()->GetId();
        uint32_t deviceId = staDev->GetIfIndex();

        // Connect to operating PHY's MonitorSnifferRx
        std::stringstream ss;
        ss << "/NodeList/" << nodeId << "/DeviceList/" << deviceId << "/$ns3::WifiNetDevice/Phy/MonitorSnifferRx";
        Config::Connect(ss.str(), MakeCallback(&OnMcsMonitorSnifferRx));

        NS_LOG_INFO("[MCS-Monitor]   - Connected STA Node " << nodeId << " Device " << deviceId);
    }

    NS_LOG_INFO("[MCS-Monitor] ✓ MCS monitoring setup complete");
    NS_LOG_INFO("[MCS-Monitor]   - " << apDevices.GetN() << " APs monitoring uplink MCS");
    NS_LOG_INFO("[MCS-Monitor]   - " << staDevices.GetN() << " STAs monitoring downlink MCS");

    // ========================================================================
    // SETUP MONITORING - CCA
    // ========================================================================

    // WifiCcaMonitor tracks MAC layer metrics (includes all WiFi overhead, retransmissions, ACKs, control frames)
    // Window size must match update interval to avoid gaps/overlaps in measurements
    WifiCcaMonitorHelper ccaHelper;
    ccaHelper.SetWindowSize(MilliSeconds(200));        // Measurement window = 200ms
    ccaHelper.SetUpdateInterval(MilliSeconds(200));    // Update callback every 200ms (matches window)

    std::vector<Ptr<WifiCcaMonitor>> ccaMonitors;

    for (uint32_t i = 0; i < apDevices.GetN(); i++) {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(apDevices.Get(i));
        Ptr<WifiCcaMonitor> monitor = ccaHelper.Install(apDev);
        monitor->TraceConnectWithoutContext("ChannelUtilization",
            MakeCallback(&OnChannelUtilization));
        monitor->Start();
        ccaMonitors.push_back(monitor);
    }

    // ========================================================================
    // SETUP FLOW MONITOR
    // ========================================================================

    FlowMonitorHelper flowMonHelper;
    Ptr<FlowMonitor> flowMonitor = flowMonHelper.InstallAll();
    flowMonitor->Start(Seconds(0.0));

    g_flowMonitor = flowMonitor;
    g_flowClassifier = DynamicCast<Ipv4FlowClassifier>(flowMonHelper.GetClassifier());

    // CollectFlowMonitorStats now calls ProcessBeaconMeasurements internally
    // This ensures scanningChannelData is populated before SendMetricsToKafka runs
    // Execution order at each second:
    //   1. CollectFlowMonitorStats:
    //      a. ProcessBeaconMeasurements → populates scanningChannelData
    //      b. FlowMonitor stats → populates flow metrics
    //   2. SendMetricsToKafka (ScheduleNow) → sends complete data

    Simulator::Schedule(Seconds(1.0), &CollectFlowMonitorStats);

    // ========================================================================
    // LEVER API SETUP FOR APs
    // ========================================================================

    NS_LOG_INFO("\n[LeverAPI] Installing Lever API on APs...");

    for (size_t i = 0; i < config.aps.size(); i++) {
        uint32_t nodeId = apNodes.Get(i)->GetId();
        const ApConfigData& apConfig = config.aps[i];

        Ptr<LeverConfig> leverConfig = CreateObject<LeverConfig>();
        leverConfig->SetTxPower(apConfig.txPower);
        leverConfig->SetCcaEdThreshold(apConfig.ccaThreshold);
        // Use default RX sensitivity (-93 dBm) - config uses obsspdThreshold which is different
        leverConfig->SetRxSensitivity(-93.0);
        leverConfig->SwitchChannel(apConfig.channel);

        g_apLeverConfigs[nodeId] = leverConfig;

        LeverApiHelper leverHelper(leverConfig);
        ApplicationContainer leverApp = leverHelper.Install(apNodes.Get(i));
        leverApp.Start(Seconds(0.0));
        leverApp.Stop(Seconds(config.simulationTime));

        // Store LeverApi instance for later use in channel switching (propagates to STAs)
        Ptr<LeverApi> leverApi = leverApp.Get(0)->GetObject<LeverApi>();
        if (leverApi) {
            g_apLeverApis[nodeId] = leverApi;
            NS_LOG_DEBUG("[LeverAPI] Stored LeverApi instance for Node " << nodeId);
        } else {
            NS_LOG_ERROR("[LeverAPI] Failed to get LeverApi for Node " << nodeId);
        }

        NS_LOG_INFO("[LeverAPI]   AP" << i << " (Node " << nodeId << ", CH" << (int)apConfig.channel
                      << "): TxPower=" << apConfig.txPower << " dBm");
    }

    NS_LOG_INFO("[LeverAPI] ✓ Lever API installed on " << config.aps.size() << " APs");

    // ========================================================================
    // POWER SCORING SETUP - Connect HeSigA callbacks and initialize
    // ========================================================================

    std::cout << "\n========== POWER SCORING SETUP ==========" << std::endl;
    std::cout << "[Power Scoring] Setting up power scoring for " << config.aps.size() << " APs..." << std::endl;

    // Initialize power scoring helper with config
    g_powerScoringHelper.SetConfig(g_powerScoringConfig);

    for (size_t i = 0; i < config.aps.size(); i++) {
        uint32_t nodeId = apNodes.Get(i)->GetId();
        Ptr<Node> apNode = apNodes.Get(i);

        // Get WifiNetDevice
        Ptr<WifiNetDevice> wifiDevice = nullptr;
        for (uint32_t j = 0; j < apNode->GetNDevices(); j++) {
            wifiDevice = DynamicCast<WifiNetDevice>(apNode->GetDevice(j));
            if (wifiDevice) break;
        }

        if (!wifiDevice) {
            std::cout << "[Power Scoring] WARNING: No WifiNetDevice for AP Node " << nodeId << std::endl;
            continue;
        }

        // Get BSS Color from HeConfiguration (only available in 802.11ax/HE)
        uint8_t bssColor = 0;
        bool hasHePhy = false;
        Ptr<HeConfiguration> heConfig = wifiDevice->GetHeConfiguration();
        if (heConfig) {
            bssColor = heConfig->m_bssColor;
            hasHePhy = true;
        }

        // Store BSS Color in g_apMetrics
        auto metricsIt = g_apMetrics.find(nodeId);
        if (metricsIt != g_apMetrics.end()) {
            metricsIt->second.bssColor = bssColor;
            metricsIt->second.txPowerDbm = config.aps[i].txPower;  // Initialize from config
        }

        // Initialize power scoring state for this AP
        g_powerScoringHelper.GetOrCreateApState(nodeId, bssColor);

        // Connect HeSigA callback to AP PHY for RSSI + BSS Color tracking (only for HE/802.11ax)
        Ptr<WifiPhy> phy = wifiDevice->GetPhy();
        if (phy && hasHePhy) {
            auto hePhy = std::dynamic_pointer_cast<HePhy>(phy->GetPhyEntity(WIFI_MOD_CLASS_HE));
            if (hePhy) {
                uint32_t capturedNodeId = nodeId;
                hePhy->SetEndOfHeSigACallback(
                    [capturedNodeId](HeSigAParameters params) {
                        OnApHeSigA(capturedNodeId, params);
                    });
                std::cout << "[Power Scoring]   Connected HeSigA callback (802.11ax)" << std::endl;
            }

            // Connect PhyRxPayloadBegin trace for actual MCS tracking
            uint32_t capturedNodeId = nodeId;
            phy->TraceConnectWithoutContext("PhyRxPayloadBegin",
                MakeBoundCallback(&OnApPhyRxPayloadBegin, capturedNodeId));
            std::cout << "[Power Scoring]   Connected PhyRxPayloadBegin trace for MCS" << std::endl;
        }

        std::string wifiStd = hasHePhy ? "802.11ax (HE)" : "802.11ac (VHT) - No BSS Color";
        std::cout << "[Power Scoring]   AP" << i << " (Node " << nodeId
                  << "): BSS Color=" << (int)bssColor
                  << ", TxPower=" << config.aps[i].txPower << " dBm"
                  << " [" << wifiStd << "]" << std::endl;
    }

    std::cout << "[Power Scoring] Setup complete for " << config.aps.size() << " APs" << std::endl;

    // Schedule first power scoring (start after 5 seconds to let network settle)
    if (g_powerScoringEnabled) {
        Simulator::Schedule(Seconds(5.0), &PerformPowerScoring);
        std::cout << "[Power Scoring] First scoring scheduled at t=5.0s (interval: "
                  << g_powerScoringConfig.updateIntervalSec << "s)" << std::endl;
    }
    std::cout << "==========================================\n" << std::endl;

    // ========================================================================
    // BUILD BSSID → LEVERCONFIG MAPPING
    // ========================================================================

    NS_LOG_INFO("\n[LeverAPI] Building BSSID → LeverConfig mapping...");

    g_bssidToLeverConfig = MetricsHelper::BuildBssidToLeverConfigMap(g_apMetrics, g_apLeverConfigs);

    NS_LOG_INFO("[LeverAPI] ✓ " << g_bssidToLeverConfig.size()
                  << " BSSID→LeverConfig mappings created");

    // ========================================================================
    // KAFKA PRODUCER SETUP
    // ========================================================================

    g_kafkaInterval = kafkaUpstreamInterval;

    NodeContainer kafkaNode;
    KafkaProducerHelper kafkaHelper(kafkaBroker, kafkaUpstreamTopic, simulationId);

    if (enableKafkaProducer) {
        NS_LOG_INFO("\n[Kafka Producer] Setting up Kafka Producer...");
        NS_LOG_INFO("[Kafka Producer]   Broker: " << kafkaBroker);
        NS_LOG_INFO("[Kafka Producer]   Topic: " << kafkaUpstreamTopic);
        NS_LOG_INFO("[Kafka Producer]   Simulation ID: " << simulationId);
        NS_LOG_INFO("[Kafka Producer]   Send Interval: " << (kafkaUpstreamInterval * 1000) << " ms");

        kafkaNode.Create(1);

        kafkaHelper.SetUpdateInterval(Seconds(kafkaUpstreamInterval));
        ApplicationContainer kafkaApp = kafkaHelper.Install(kafkaNode.Get(0));
        kafkaApp.Start(Seconds(0.0));
        kafkaApp.Stop(Seconds(config.simulationTime + 5.0));

        g_kafkaProducer = KafkaProducerHelper::GetKafkaProducer(kafkaNode.Get(0));

        if (g_kafkaProducer) {
            NS_LOG_INFO("[Kafka Producer] ✓ Producer started successfully");
            NS_LOG_INFO("[Kafka Producer] Metrics will be sent after each FlowMonitor collection (every 1.0s)");
        } else {
            NS_LOG_ERROR("[Kafka Producer] ✗ Failed to get producer instance!");
        }
    } else {
        NS_LOG_INFO("\n[Kafka Producer] Disabled");
    }

    // ========================================================================
    // KAFKA CONSUMER SETUP
    // ========================================================================

    if (enableKafkaConsumer) {
        NS_LOG_INFO("\n[Kafka Consumer] Setting up Kafka Consumer...");
        NS_LOG_INFO("[Kafka Consumer]   Broker: " << kafkaBroker);
        NS_LOG_INFO("[Kafka Consumer]   Topic: " << kafkaDownstreamTopic);
        NS_LOG_INFO("[Kafka Consumer]   Group ID: " << kafkaConsumerGroupId);
        NS_LOG_INFO("[Kafka Consumer]   Simulation ID: " << simulationId);
        NS_LOG_INFO("[Kafka Consumer]   Poll Interval: " << (kafkaConsumerPollInterval * 1000) << " ms");

        KafkaConsumerHelper consumerHelper(kafkaBroker, kafkaDownstreamTopic,
                                          kafkaConsumerGroupId, simulationId);
        consumerHelper.SetPollInterval(MilliSeconds(kafkaConsumerPollInterval * 1000));

        ApplicationContainer consumerApp = consumerHelper.Install(apNodes.Get(0));
        consumerApp.Start(Seconds(0.0));
        consumerApp.Stop(Seconds(config.simulationTime));

        Ptr<KafkaConsumer> consumer = consumerHelper.GetKafkaConsumer(apNodes.Get(0));
        if (consumer) {
            consumer->SetParameterCallback(MakeCallback(&OnParametersReceived));
            NS_LOG_INFO("[Kafka Consumer] ✓ Consumer installed");
            NS_LOG_INFO("[Kafka Consumer] ✓ Callback connected: Kafka → LeverApi");
        } else {
            NS_LOG_ERROR("[Kafka Consumer] ✗ Failed to get consumer instance!");
        }
    } else {
        NS_LOG_INFO("\n[Kafka Consumer] Disabled");
    }

    // ========================================================================
    // SETUP TCP TRAFFIC
    // ========================================================================

    NS_LOG_INFO("\n[Traffic] Setting up TCP applications...");

    uint16_t uplinkPort = 50000;
    uint16_t downlinkPort = 60000;
    Ipv4Address dsAddress = dsInterfaces.GetAddress(0);
    g_dsAddress = dsAddress;  // Set global for flow direction detection

    NS_LOG_INFO("[Traffic] DS Address: " << g_dsAddress);

    // TCP Server on DS node (for UPLINK traffic from STAs)
    PacketSinkHelper uplinkSinkHelper("ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), uplinkPort));
    ApplicationContainer uplinkServerApps = uplinkSinkHelper.Install(dsNode.Get(0));
    uplinkServerApps.Start(Seconds(1.0));
    uplinkServerApps.Stop(Seconds(config.simulationTime));

    // TCP Clients on STAs - Mixed Pareto OnOff for realistic bursty traffic patterns
    // 70% Web Browsing, 30% Video Streaming
    uint32_t numWebBrowsing = static_cast<uint32_t>(staNodes.GetN() * 0.7);

    for (uint32_t i = 0; i < staNodes.GetN(); i++) {
        OnOffHelper onoff("ns3::TcpSocketFactory",
                         InetSocketAddress(dsAddress, uplinkPort));

        std::string trafficType;
        if (i < numWebBrowsing) {
            // Web Browsing Pattern: Short bursts with moderate think times
            onoff.SetAttribute("DataRate", DataRateValue(DataRate("40Mbps")));
            onoff.SetAttribute("PacketSize", UintegerValue(1400));
            // Mean ON time: 2 seconds (page download)
            // Scale = Mean * (alpha-1) / alpha = 2.0 * 0.5 / 1.5 = 0.667
            onoff.SetAttribute("OnTime",
                StringValue("ns3::ParetoRandomVariable[Scale=0.667|Shape=1.5]"));
            // Mean OFF time: 1 second (reading/think time) - reduced to ensure continuous metrics
            // Scale = 1.0 * 0.5 / 1.5 = 0.33
            onoff.SetAttribute("OffTime",
                StringValue("ns3::ParetoRandomVariable[Scale=0.33|Shape=1.5]"));
            trafficType = "Web";
        } else {
            // Video Streaming Pattern: Sustained bursts with short gaps
            onoff.SetAttribute("DataRate", DataRateValue(DataRate("50Mbps")));
            onoff.SetAttribute("PacketSize", UintegerValue(1400));
            // Mean ON time: 5 seconds (video chunk)
            // Scale = 5.0 * 0.5 / 1.5 = 1.67
            onoff.SetAttribute("OnTime",
                StringValue("ns3::ParetoRandomVariable[Scale=1.67|Shape=1.5]"));
            // Mean OFF time: 0.5 seconds (buffering gap) - reduced for continuous metrics
            // Scale = 0.5 * 0.5 / 1.5 = 0.167
            onoff.SetAttribute("OffTime",
                StringValue("ns3::ParetoRandomVariable[Scale=0.167|Shape=1.5]"));
            trafficType = "Video";
        }

        ApplicationContainer clientApp = onoff.Install(staNodes.Get(i));
        clientApp.Start(Seconds(2.0 + i * 0.1));
        clientApp.Stop(Seconds(config.simulationTime - 1.0));

        uint32_t staNodeId = staNodes.Get(i)->GetId();

        NS_LOG_INFO("[Traffic] STA Node " << staNodeId << " ("
                      << i << "/" << staNodes.GetN() << "): " << trafficType
                      << " pattern");

        // Connect TCP socket traces with retry mechanism
        // TCP connection may take time, so we retry multiple times
        auto connectSocketTraces = [staNodeId, trafficType](uint32_t attempt) -> void {
            static std::function<void(uint32_t, uint32_t)> tryConnect;
            tryConnect = [staNodeId, trafficType](uint32_t attempt, uint32_t maxAttempts) {
                Ptr<Node> node = NodeList::GetNode(staNodeId);

                bool found = false;
                for (uint32_t appIdx = 0; appIdx < node->GetNApplications(); appIdx++) {
                    Ptr<Application> app = node->GetApplication(appIdx);
                    Ptr<OnOffApplication> onoffApp = DynamicCast<OnOffApplication>(app);

                    if (onoffApp) {
                        Ptr<Socket> socket = onoffApp->GetSocket();
                        if (socket) {
                            Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(socket);

                            if (tcpSocket) {
                                g_socketToNodeId[tcpSocket] = staNodeId;
                                NS_LOG_INFO("[Traces] ✓ Connected TCP traces for STA Node " << staNodeId
                                           << " (" << trafficType << " pattern) on attempt " << attempt);
                                found = true;
                                break;
                            }
                        }
                    }
                }

                if (!found && attempt < maxAttempts) {
                    // Retry after 0.5 seconds
                    Simulator::Schedule(Seconds(0.5), [trafficType, attempt, maxAttempts]() {
                        tryConnect(attempt + 1, maxAttempts);
                    });
                } else if (!found) {
                    NS_LOG_ERROR("[Traces] ✗ Failed to find OnOffApplication socket on STA Node "
                                << staNodeId << " after " << maxAttempts << " attempts");
                }
            };

            tryConnect(attempt, 5);  // Try up to 5 times
        };

        // Start trying to connect traces 0.2s after app starts
        Simulator::Schedule(Seconds(2.2 + i * 0.1), [connectSocketTraces]() {
            connectSocketTraces(1);
        });
    }

    NS_LOG_INFO("[Traffic] ✓ UPLINK Pareto OnOff traffic configured:");
    NS_LOG_INFO("[Traffic]   - " << numWebBrowsing << " STAs with Web Browsing pattern (40Mbps, ON=2s, OFF=1s)");
    NS_LOG_INFO("[Traffic]   - " << (staNodes.GetN() - numWebBrowsing) << " STAs with Video Streaming pattern (50Mbps, ON=5s, OFF=0.5s)");

    // ========================================================================
    // SETUP DOWNLINK TCP TRAFFIC (DS → STAs)
    // ========================================================================

    NS_LOG_INFO("\n[Traffic] Setting up DOWNLINK TCP applications...");

    // PacketSink on each STA to receive downlink traffic
    for (uint32_t i = 0; i < staNodes.GetN(); i++) {
        PacketSinkHelper downlinkSinkHelper("ns3::TcpSocketFactory",
            InetSocketAddress(Ipv4Address::GetAny(), downlinkPort));
        ApplicationContainer staSinkApp = downlinkSinkHelper.Install(staNodes.Get(i));
        staSinkApp.Start(Seconds(1.0));
        staSinkApp.Stop(Seconds(config.simulationTime));
    }

    // OnOff applications on DS sending to each STA
    for (uint32_t i = 0; i < staNodes.GetN(); i++) {
        Ipv4Address staAddress = staInterfaces.GetAddress(i);

        OnOffHelper downlinkOnoff("ns3::TcpSocketFactory",
                                 InetSocketAddress(staAddress, downlinkPort));

        std::string trafficType;
        if (i < numWebBrowsing) {
            // Web Browsing: Downlink typically 2-5x faster than uplink
            downlinkOnoff.SetAttribute("DataRate", DataRateValue(DataRate("80Mbps")));
            downlinkOnoff.SetAttribute("PacketSize", UintegerValue(1400));
            // Same timing as uplink: ON=2s, OFF=1s (reduced for continuous metrics)
            downlinkOnoff.SetAttribute("OnTime",
                StringValue("ns3::ParetoRandomVariable[Scale=0.667|Shape=1.5]"));
            downlinkOnoff.SetAttribute("OffTime",
                StringValue("ns3::ParetoRandomVariable[Scale=0.33|Shape=1.5]"));
            trafficType = "Web";
        } else {
            // Video Streaming: Downlink much higher (HD video download)
            downlinkOnoff.SetAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
            downlinkOnoff.SetAttribute("PacketSize", UintegerValue(1400));
            // Same timing as uplink: ON=5s, OFF=0.5s (reduced for continuous metrics)
            downlinkOnoff.SetAttribute("OnTime",
                StringValue("ns3::ParetoRandomVariable[Scale=1.67|Shape=1.5]"));
            downlinkOnoff.SetAttribute("OffTime",
                StringValue("ns3::ParetoRandomVariable[Scale=0.167|Shape=1.5]"));
            trafficType = "Video";
        }

        ApplicationContainer downlinkApp = downlinkOnoff.Install(dsNode.Get(0));
        // Stagger slightly different from uplink to avoid synchronization
        downlinkApp.Start(Seconds(2.05 + i * 0.1));
        downlinkApp.Stop(Seconds(config.simulationTime - 1.0));

        NS_LOG_INFO("[Traffic] DS → STA " << staAddress << ": " << trafficType
                      << " pattern downlink");
    }

    NS_LOG_INFO("[Traffic] ✓ DOWNLINK Pareto OnOff traffic configured:");
    NS_LOG_INFO("[Traffic]   - " << numWebBrowsing << " STAs with Web Browsing pattern (80Mbps, ON=2s, OFF=1s)");
    NS_LOG_INFO("[Traffic]   - " << (staNodes.GetN() - numWebBrowsing) << " STAs with Video Streaming pattern (100Mbps, ON=5s, OFF=0.5s)");
    NS_LOG_INFO("[Traffic]   - Pareto shape parameter: α=1.5 (standard burstiness)");
    NS_LOG_INFO("[Traffic]   - Total flows: " << (staNodes.GetN() * 2) << " (uplink + downlink)");

    // ========================================================================
    // ENABLE PCAP (OPTIONAL)
    // ========================================================================

    if (enablePcap) {
        yansPhy.EnablePcap("config-simulation-wifi", staDevices);
        csma.EnablePcap("config-simulation-csma", dsDevices, false);
    }

    // ========================================================================
    // START WAYPOINT MOBILITY
    // ========================================================================

    NS_LOG_INFO("[Mobility] Starting waypoint-based mobility for STAs...");
    mobilityHelper.StartMobility();
    NS_LOG_INFO("[Mobility] ✓ Waypoint mobility started");

    // ========================================================================
    // RUN SIMULATION
    // ========================================================================

    NS_LOG_INFO("\n╔═══════════════════════════════════════════════════════════╗");
    NS_LOG_INFO("║  Starting Simulation...                                   ║");
    NS_LOG_INFO("╚═══════════════════════════════════════════════════════════╝\n");

    // Schedule periodic channel scoring (start after 15s to allow scan data collection)
    Simulator::Schedule(Seconds(10), &PerformChannelScoring);
    NS_LOG_INFO("[Channel Scoring] Scheduled to start at t=10s, then every 10s");

    // Schedule periodic virtual interferer detection logging
    if (g_virtualInterfererLoggingEnabled) {
        Simulator::Schedule(Seconds(10), &PerformVirtualInterfererLogging);
        NS_LOG_INFO("[Virtual Interferer] Detection logging scheduled to start at t=10s, then every "
                    << g_virtualInterfererLoggingInterval << "s");
    }

    Simulator::Stop(Seconds(config.simulationTime));
    Simulator::Run();

    // Flush Kafka if enabled
    if (enableKafkaProducer && kafkaNode.GetN() > 0) {
        NS_LOG_INFO("Simulation completed - waiting for Kafka Producer flush...");
        auto producer = kafkaHelper.GetKafkaProducer(kafkaNode.Get(0));
        if (producer) {
            producer->GetKafkaProducer()->flush(std::chrono::milliseconds(10));
        }
    }

    // Cleanup virtual interferer environment before destroying simulator
    if (config.virtualInterferers.enabled)
    {
        NS_LOG_INFO("\n[VirtualInterferer] Cleaning up virtual interferer environment...");
        VirtualInterfererEnvironment::Destroy();
        NS_LOG_INFO("[VirtualInterferer] ✓ Virtual interferer environment cleaned up");
    }

    Simulator::Destroy();

    NS_LOG_INFO("\n╔═══════════════════════════════════════════════════════════╗");
    NS_LOG_INFO("║  Simulation Complete!                                     ║");
    NS_LOG_INFO("╚═══════════════════════════════════════════════════════════╝\n");

    return 0;
}