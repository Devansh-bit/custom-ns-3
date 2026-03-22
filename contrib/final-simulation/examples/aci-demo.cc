/**
 * ACI (Adjacent Channel Interference) Demonstration
 *
 * This simulation demonstrates the analytical ACI model effects on WiFi metrics.
 * It sets up 4 APs with various channel configurations and shows how interference
 * affects throughput, latency, packet loss, jitter, and channel utilization.
 *
 * Run with: ./ns3 run aci-demo
 */

#include "ns3/core-module.h"
#include "ns3/vector.h"

#include <iostream>
#include <iomanip>
#include <map>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("AciDemo");

// ============================================================================
// ACI CONFIGURATION
// ============================================================================

struct AciConfig {
    bool enabled = true;
    double pathLossExponent = 3.0;
    double maxInterferenceDistanceM = 50.0;
    double clientWeightFactor = 0.1;
    struct {
        double throughput = 0.3;   // At IF=1: 30% throughput reduction
        double packetLoss = 5.0;   // At IF=1: +5% packet loss
        double latency = 0.5;      // At IF=1: 50% latency increase
        double jitter = 0.4;       // At IF=1: 40% jitter increase
        double channelUtil = 0.15; // At IF=1: +15% utilization
    } degradation;
};

// ============================================================================
// AP INFORMATION STRUCTURE
// ============================================================================

struct ApInfo {
    uint32_t nodeId;
    Vector position;
    double txPowerDbm;
    uint8_t channel;
    uint32_t associatedClients;
    std::string name;
};

// ============================================================================
// METRICS STRUCTURE
// ============================================================================

struct ApMetrics {
    double throughputMbps;
    double latencyMs;
    double packetLossPercent;
    double jitterMs;
    double channelUtilPercent;
};

// ============================================================================
// CHANNEL INFORMATION
// ============================================================================

struct ChannelInfo {
    uint16_t centerFreqMHz;
    uint16_t widthMHz;
};

// 5 GHz Channel Lookup Table (includes 20/40/80/160 MHz channels)
const std::map<uint8_t, ChannelInfo> g_5GHzChannels = {
    // UNII-1 (5150-5250 MHz) - 20 MHz channels
    {36, {5180, 20}}, {40, {5200, 20}}, {44, {5220, 20}}, {48, {5240, 20}},
    // UNII-1 - 40 MHz channels
    {38, {5190, 40}}, {46, {5230, 40}},
    // UNII-1 - 80 MHz channel
    {42, {5210, 80}},

    // UNII-2A (5250-5330 MHz) - 20 MHz channels
    {52, {5260, 20}}, {56, {5280, 20}}, {60, {5300, 20}}, {64, {5320, 20}},
    // UNII-2A - 40 MHz channels
    {54, {5270, 40}}, {62, {5310, 40}},
    // UNII-2A - 80 MHz channel
    {58, {5290, 80}},

    // UNII-1 + UNII-2A - 160 MHz channel
    {50, {5250, 160}},
};

// ============================================================================
// CHANNEL OVERLAP CALCULATION
// ============================================================================

ChannelInfo GetChannelInfo(uint8_t channel) {
    auto it = g_5GHzChannels.find(channel);
    if (it != g_5GHzChannels.end()) {
        return it->second;
    }
    // Default: assume 20 MHz channel with estimated frequency
    return {static_cast<uint16_t>(5000 + channel * 5), 20};
}

/**
 * Calculate channel overlap factor (ASYMMETRIC from victim's perspective)
 * Returns: What fraction of channel1's (victim) bandwidth is overlapped by channel2 (interferer)
 * This is NOT symmetric: GetChannelOverlap(36, 42) != GetChannelOverlap(42, 36)
 *   - CH36 (20MHz) vs CH42 (80MHz): 20/20 = 1.0 (CH36 fully covered)
 *   - CH42 (80MHz) vs CH36 (20MHz): 20/80 = 0.25 (only 25% of CH42 is overlapped)
 */
double GetChannelOverlap(uint8_t channel1, uint8_t channel2) {
    if (channel1 == channel2) return 1.0;

    ChannelInfo info1 = GetChannelInfo(channel1);  // Victim
    ChannelInfo info2 = GetChannelInfo(channel2);  // Interferer

    double center1 = info1.centerFreqMHz;
    double center2 = info2.centerFreqMHz;
    double halfWidth1 = info1.widthMHz / 2.0;
    double halfWidth2 = info2.widthMHz / 2.0;

    double lower1 = center1 - halfWidth1;
    double upper1 = center1 + halfWidth1;
    double lower2 = center2 - halfWidth2;
    double upper2 = center2 + halfWidth2;

    // Calculate overlap
    double overlapLower = std::max(lower1, lower2);
    double overlapUpper = std::min(upper1, upper2);
    double overlapWidth = std::max(0.0, overlapUpper - overlapLower);

    if (overlapWidth > 0) {
        // CRITICAL: Normalize by VICTIM's (channel1) bandwidth, NOT the smaller one
        // This represents "what fraction of MY bandwidth is being interfered with"
        return overlapWidth / info1.widthMHz;
    }

    // No direct overlap - calculate adjacent channel interference (ACI)
    // Based on IEEE 802.11 spectral mask: ~-20 dB for 1st adjacent, ~-28 dB for 2nd
    double separation = std::abs(center1 - center2);
    double combinedHalfWidth = halfWidth1 + halfWidth2;
    double gapMHz = separation - combinedHalfWidth;

    if (gapMHz <= 0) {
        // Touching edges - spectral mask leakage (~-20 dB = 0.1 power ratio)
        return 0.10;
    }

    // Exponential decay for channels with gap
    // ~-20 dB per 20 MHz separation = factor of 0.1 per 20 MHz
    double decay = std::exp(-gapMHz / 20.0);
    return decay * 0.10; // Max 0.10 for adjacent (non-overlapping) channels
}

// ============================================================================
// INTERFERENCE CALCULATION
// ============================================================================

// Use ns3::CalculateDistance for Vector distance calculation

double CalculateDistanceFactor(double distanceM, double maxDistanceM, double pathLossExp) {
    if (distanceM >= maxDistanceM) return 0.0;
    if (distanceM < 1.0) distanceM = 1.0; // Minimum 1 meter

    // Inverse power law with reference at 1 meter
    double refDistance = 1.0;
    double factor = std::pow(refDistance / distanceM, pathLossExp);

    // Soft cutoff near max distance
    double cutoffFactor = 1.0 - std::pow(distanceM / maxDistanceM, 2.0);
    return factor * std::max(0.0, cutoffFactor);
}

double CalculateTxPowerFactor(double txPowerDbm) {
    // Normalize around 20 dBm as reference
    double refPowerDbm = 20.0;
    double deltaDb = txPowerDbm - refPowerDbm;
    return std::pow(10.0, deltaDb / 20.0); // Voltage ratio
}

double CalculateClientWeightFactor(uint32_t numClients, double weightFactor) {
    // More clients = more traffic = more interference potential
    return 1.0 + (numClients * weightFactor);
}

/**
 * Calculate total interference factor for an AP from all other APs
 */
double CalculateInterferenceFactor(
    const ApInfo& targetAp,
    const std::vector<ApInfo>& allAps,
    const AciConfig& config)
{
    double totalIF = 0.0;

    for (const auto& otherAp : allAps) {
        if (otherAp.nodeId == targetAp.nodeId) continue;

        // Channel overlap
        double channelOverlap = GetChannelOverlap(targetAp.channel, otherAp.channel);
        if (channelOverlap < 0.001) continue; // Skip negligible overlap

        // Distance factor
        double distance = CalculateDistance(targetAp.position, otherAp.position);
        double distanceFactor = CalculateDistanceFactor(
            distance, config.maxInterferenceDistanceM, config.pathLossExponent);
        if (distanceFactor < 0.001) continue; // Skip too far

        // TX power factor
        double txPowerFactor = CalculateTxPowerFactor(otherAp.txPowerDbm);

        // Client weight factor
        double clientFactor = CalculateClientWeightFactor(
            otherAp.associatedClients, config.clientWeightFactor);

        // Combined interference from this AP
        double interference = channelOverlap * distanceFactor * txPowerFactor * clientFactor;
        totalIF += interference;
    }

    // Clamp to [0, 1]
    return std::min(1.0, totalIF);
}

// ============================================================================
// METRICS DEGRADATION
// ============================================================================

ApMetrics ApplyAciDegradation(const ApMetrics& original, double interferenceF, const AciConfig& config) {
    ApMetrics degraded = original;

    // Throughput: reduce by degradation factor
    degraded.throughputMbps = original.throughputMbps * (1.0 - interferenceF * config.degradation.throughput);

    // Latency: increase by degradation factor
    degraded.latencyMs = original.latencyMs * (1.0 + interferenceF * config.degradation.latency);

    // Packet loss: add percentage points
    degraded.packetLossPercent = original.packetLossPercent +
        (interferenceF * config.degradation.packetLoss);

    // Jitter: increase by degradation factor
    degraded.jitterMs = original.jitterMs * (1.0 + interferenceF * config.degradation.jitter);

    // Channel utilization: add percentage points (capped at 100%)
    degraded.channelUtilPercent = std::min(100.0,
        original.channelUtilPercent + (interferenceF * config.degradation.channelUtil * 100.0));

    return degraded;
}

// ============================================================================
// DISPLAY FUNCTIONS
// ============================================================================

void PrintSeparator(char c = '=', int width = 80) {
    std::cout << std::string(width, c) << std::endl;
}

void PrintHeader(const std::string& title) {
    PrintSeparator();
    std::cout << " " << title << std::endl;
    PrintSeparator();
}

void PrintApSetup(const std::vector<ApInfo>& aps) {
    PrintHeader("AP CONFIGURATION");
    std::cout << std::left
              << std::setw(10) << "Name"
              << std::setw(8) << "NodeID"
              << std::setw(10) << "Channel"
              << std::setw(10) << "Width"
              << std::setw(12) << "TxPower"
              << std::setw(10) << "Clients"
              << "Position" << std::endl;
    PrintSeparator('-');

    for (const auto& ap : aps) {
        ChannelInfo chInfo = GetChannelInfo(ap.channel);
        std::cout << std::left
                  << std::setw(10) << ap.name
                  << std::setw(8) << ap.nodeId
                  << std::setw(10) << (int)ap.channel
                  << std::setw(10) << (std::to_string(chInfo.widthMHz) + " MHz")
                  << std::setw(12) << (std::to_string((int)ap.txPowerDbm) + " dBm")
                  << std::setw(10) << ap.associatedClients
                  << "(" << ap.position.x << ", " << ap.position.y << ", " << ap.position.z << ")"
                  << std::endl;
    }
    std::cout << std::endl;
}

void PrintChannelOverlapMatrix(const std::vector<ApInfo>& aps) {
    PrintHeader("CHANNEL OVERLAP MATRIX");

    // Header row
    std::cout << std::setw(12) << "";
    for (const auto& ap : aps) {
        std::cout << std::setw(10) << ap.name;
    }
    std::cout << std::endl;
    PrintSeparator('-');

    // Data rows
    for (const auto& ap1 : aps) {
        std::cout << std::setw(12) << ap1.name;
        for (const auto& ap2 : aps) {
            double overlap = GetChannelOverlap(ap1.channel, ap2.channel);
            std::cout << std::setw(10) << std::fixed << std::setprecision(3) << overlap;
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

void PrintDistanceMatrix(const std::vector<ApInfo>& aps) {
    PrintHeader("DISTANCE MATRIX (meters)");

    // Header row
    std::cout << std::setw(12) << "";
    for (const auto& ap : aps) {
        std::cout << std::setw(10) << ap.name;
    }
    std::cout << std::endl;
    PrintSeparator('-');

    // Data rows
    for (const auto& ap1 : aps) {
        std::cout << std::setw(12) << ap1.name;
        for (const auto& ap2 : aps) {
            double dist = CalculateDistance(ap1.position, ap2.position);
            std::cout << std::setw(10) << std::fixed << std::setprecision(1) << dist;
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

void PrintInterferenceFactors(const std::vector<ApInfo>& aps, const AciConfig& config) {
    PrintHeader("INTERFERENCE FACTORS (per AP)");

    std::cout << std::left
              << std::setw(12) << "AP"
              << std::setw(15) << "IF Total"
              << "Contributing APs" << std::endl;
    PrintSeparator('-');

    for (const auto& targetAp : aps) {
        double totalIF = CalculateInterferenceFactor(targetAp, aps, config);

        std::cout << std::left << std::setw(12) << targetAp.name
                  << std::setw(15) << std::fixed << std::setprecision(4) << totalIF;

        // Show breakdown
        std::vector<std::pair<std::string, double>> contributors;
        for (const auto& otherAp : aps) {
            if (otherAp.nodeId == targetAp.nodeId) continue;

            double channelOverlap = GetChannelOverlap(targetAp.channel, otherAp.channel);
            double distance = CalculateDistance(targetAp.position, otherAp.position);
            double distanceFactor = CalculateDistanceFactor(
                distance, config.maxInterferenceDistanceM, config.pathLossExponent);
            double txPowerFactor = CalculateTxPowerFactor(otherAp.txPowerDbm);
            double clientFactor = CalculateClientWeightFactor(
                otherAp.associatedClients, config.clientWeightFactor);

            double contribution = channelOverlap * distanceFactor * txPowerFactor * clientFactor;
            if (contribution > 0.001) {
                contributors.push_back({otherAp.name, contribution});
            }
        }

        if (!contributors.empty()) {
            bool first = true;
            for (const auto& [name, contrib] : contributors) {
                if (!first) std::cout << ", ";
                std::cout << name << ":" << std::fixed << std::setprecision(3) << contrib;
                first = false;
            }
        } else {
            std::cout << "(none)";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

void PrintMetricsComparison(
    const std::string& apName,
    const ApMetrics& original,
    const ApMetrics& degraded,
    double interferenceF)
{
    std::cout << std::left << std::setw(12) << apName;
    std::cout << std::fixed << std::setprecision(4) << std::setw(10) << interferenceF;

    // Throughput
    std::cout << std::setprecision(1) << std::setw(10) << original.throughputMbps
              << std::setw(10) << degraded.throughputMbps;

    // Latency
    std::cout << std::setprecision(2) << std::setw(10) << original.latencyMs
              << std::setw(10) << degraded.latencyMs;

    // Packet Loss
    std::cout << std::setprecision(2) << std::setw(10) << original.packetLossPercent
              << std::setw(10) << degraded.packetLossPercent;

    std::cout << std::endl;
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[])
{
    // Parse command line
    CommandLine cmd;
    cmd.Parse(argc, argv);

    std::cout << "\n";
    PrintSeparator('*');
    std::cout << " ACI (Adjacent Channel Interference) DEMONSTRATION\n";
    std::cout << " Analytical model for fast WiFi simulation\n";
    PrintSeparator('*');
    std::cout << "\n";

    // ========================================================================
    // SETUP: Create AP configurations
    // ========================================================================

    // Scenario: Dense AP deployment showing various interference cases
    // Demonstrates: same channel, adjacent channel, overlapping wide channels, non-overlapping
    std::vector<ApInfo> aps = {
        // 20 MHz channels in UNII-1 band
        {0, Vector(0, 0, 3), 20.0, 36, 3, "AP0-CH36"},      // 5170-5190 MHz
        {1, Vector(5, 0, 3), 20.0, 40, 5, "AP1-CH40"},      // 5190-5210 MHz (adjacent to 36)
        {2, Vector(10, 0, 3), 20.0, 44, 2, "AP2-CH44"},     // 5210-5230 MHz (adjacent to 40)
        {3, Vector(15, 0, 3), 20.0, 48, 4, "AP3-CH48"},     // 5230-5250 MHz (adjacent to 44)

        // 40 MHz channels
        {4, Vector(0, 8, 3), 21.0, 38, 6, "AP4-CH38"},      // 5170-5210 MHz (overlaps 36+40)
        {5, Vector(8, 8, 3), 21.0, 46, 3, "AP5-CH46"},      // 5210-5250 MHz (overlaps 44+48)

        // 80 MHz channel - overlaps entire UNII-1 band
        {6, Vector(4, 4, 3), 23.0, 42, 8, "AP6-CH42"},      // 5170-5250 MHz (overlaps 36/40/44/48)

        // UNII-2A band - non-overlapping with UNII-1
        {7, Vector(20, 0, 3), 20.0, 52, 4, "AP7-CH52"},     // 5250-5270 MHz (gap from UNII-1)
        {8, Vector(20, 8, 3), 22.0, 58, 5, "AP8-CH58"},     // 5250-5330 MHz (80 MHz in UNII-2A)
    };

    // ACI Configuration
    AciConfig config;
    config.enabled = true;
    config.pathLossExponent = 3.0;
    config.maxInterferenceDistanceM = 50.0;
    config.clientWeightFactor = 0.1;

    // ========================================================================
    // DISPLAY: Show AP setup and relationships
    // ========================================================================

    PrintApSetup(aps);
    PrintChannelOverlapMatrix(aps);
    PrintDistanceMatrix(aps);
    PrintInterferenceFactors(aps, config);

    // ========================================================================
    // SIMULATE: Create baseline metrics and apply ACI degradation
    // ========================================================================

    PrintHeader("METRICS COMPARISON (Original vs ACI-Degraded)");

    // Create baseline metrics for each AP (simulated "ideal" conditions)
    // Format: {throughputMbps, latencyMs, packetLossPercent, jitterMs, channelUtilPercent}
    std::map<uint32_t, ApMetrics> baselineMetrics = {
        // 20 MHz channels (max ~150 Mbps theoretical for 802.11ax)
        {0, {150.0, 5.0, 0.5, 1.0, 25.0}},  // AP0-CH36: Moderate load
        {1, {140.0, 6.0, 0.8, 1.2, 30.0}},  // AP1-CH40: Higher load
        {2, {120.0, 4.5, 0.3, 0.8, 20.0}},  // AP2-CH44: Light load
        {3, {145.0, 5.5, 0.6, 1.1, 28.0}},  // AP3-CH48: Moderate load

        // 40 MHz channels (max ~300 Mbps theoretical)
        {4, {280.0, 4.0, 0.4, 0.9, 35.0}},  // AP4-CH38: High throughput
        {5, {250.0, 4.2, 0.5, 1.0, 30.0}},  // AP5-CH46: Moderate throughput

        // 80 MHz channel (max ~600 Mbps theoretical)
        {6, {500.0, 3.5, 0.3, 0.7, 40.0}},  // AP6-CH42: Very high throughput

        // UNII-2A band (isolated from UNII-1)
        {7, {155.0, 4.8, 0.4, 0.9, 22.0}},  // AP7-CH52: Light load, isolated
        {8, {480.0, 3.8, 0.4, 0.8, 38.0}},  // AP8-CH58: High throughput 80MHz
    };

    std::cout << std::left
              << std::setw(12) << "AP"
              << std::setw(10) << "IF"
              << std::setw(10) << "Tput(O)"
              << std::setw(10) << "Tput(D)"
              << std::setw(10) << "Lat(O)"
              << std::setw(10) << "Lat(D)"
              << std::setw(10) << "Loss(O)"
              << std::setw(10) << "Loss(D)"
              << std::endl;
    std::cout << std::left
              << std::setw(12) << ""
              << std::setw(10) << ""
              << std::setw(10) << "Mbps"
              << std::setw(10) << "Mbps"
              << std::setw(10) << "ms"
              << std::setw(10) << "ms"
              << std::setw(10) << "%"
              << std::setw(10) << "%"
              << std::endl;
    PrintSeparator('-');

    for (const auto& ap : aps) {
        double interferenceF = CalculateInterferenceFactor(ap, aps, config);
        ApMetrics original = baselineMetrics[ap.nodeId];
        ApMetrics degraded = ApplyAciDegradation(original, interferenceF, config);

        PrintMetricsComparison(ap.name, original, degraded, interferenceF);
    }

    std::cout << "\n";
    PrintSeparator('-');
    std::cout << "Legend: O = Original, D = Degraded (after ACI)\n";
    std::cout << "        IF = Interference Factor [0-1]\n";
    std::cout << "        Tput = Throughput, Lat = Latency, Loss = Packet Loss\n";
    PrintSeparator('-');

    // ========================================================================
    // DETAILED BREAKDOWN for most affected AP
    // ========================================================================

    std::cout << "\n";
    PrintHeader("DETAILED BREAKDOWN: AP6-CH42 (80 MHz overlapping channel)");

    ApInfo& ap6 = aps[6];
    double if6 = CalculateInterferenceFactor(ap6, aps, config);

    std::cout << "AP6 operates on channel 42 (80 MHz, 5170-5250 MHz)\n";
    std::cout << "This wide channel overlaps with channels 36, 40, 44, 48\n\n";

    std::cout << "Interference breakdown:\n";
    for (const auto& otherAp : aps) {
        if (otherAp.nodeId == ap6.nodeId) continue;

        double channelOverlap = GetChannelOverlap(ap6.channel, otherAp.channel);
        double distance = CalculateDistance(ap6.position, otherAp.position);
        double distanceFactor = CalculateDistanceFactor(
            distance, config.maxInterferenceDistanceM, config.pathLossExponent);
        double txPowerFactor = CalculateTxPowerFactor(otherAp.txPowerDbm);
        double clientFactor = CalculateClientWeightFactor(
            otherAp.associatedClients, config.clientWeightFactor);
        double contribution = channelOverlap * distanceFactor * txPowerFactor * clientFactor;

        ChannelInfo otherInfo = GetChannelInfo(otherAp.channel);

        std::cout << "  " << otherAp.name << " (CH" << (int)otherAp.channel
                  << ", " << otherInfo.widthMHz << " MHz):\n";
        std::cout << "    Channel overlap:  " << std::fixed << std::setprecision(3) << channelOverlap << "\n";
        std::cout << "    Distance:         " << std::setprecision(1) << distance << " m\n";
        std::cout << "    Distance factor:  " << std::setprecision(3) << distanceFactor << "\n";
        std::cout << "    TX power factor:  " << std::setprecision(3) << txPowerFactor << "\n";
        std::cout << "    Client factor:    " << std::setprecision(3) << clientFactor
                  << " (" << otherAp.associatedClients << " clients)\n";
        std::cout << "    -> Contribution:  " << std::setprecision(4) << contribution << "\n\n";
    }

    std::cout << "Total Interference Factor: " << std::fixed << std::setprecision(4) << if6 << "\n\n";

    ApMetrics orig6 = baselineMetrics[ap6.nodeId];
    ApMetrics deg6 = ApplyAciDegradation(orig6, if6, config);

    std::cout << "Metrics impact:\n";
    std::cout << "  Throughput:    " << orig6.throughputMbps << " -> " << std::setprecision(1) << deg6.throughputMbps
              << " Mbps (" << std::setprecision(1) << ((orig6.throughputMbps - deg6.throughputMbps) / orig6.throughputMbps * 100) << "% reduction)\n";
    std::cout << "  Latency:       " << orig6.latencyMs << " -> " << std::setprecision(2) << deg6.latencyMs
              << " ms (" << std::setprecision(1) << ((deg6.latencyMs - orig6.latencyMs) / orig6.latencyMs * 100) << "% increase)\n";
    std::cout << "  Packet Loss:   " << orig6.packetLossPercent << " -> " << std::setprecision(2) << deg6.packetLossPercent
              << "% (+" << std::setprecision(2) << (deg6.packetLossPercent - orig6.packetLossPercent) << " pp)\n";
    std::cout << "  Jitter:        " << orig6.jitterMs << " -> " << std::setprecision(2) << deg6.jitterMs
              << " ms (" << std::setprecision(1) << ((deg6.jitterMs - orig6.jitterMs) / orig6.jitterMs * 100) << "% increase)\n";
    std::cout << "  Channel Util:  " << orig6.channelUtilPercent << " -> " << std::setprecision(1) << deg6.channelUtilPercent
              << "% (+" << std::setprecision(1) << (deg6.channelUtilPercent - orig6.channelUtilPercent) << " pp)\n";

    std::cout << "\n";
    PrintSeparator('*');
    std::cout << " ACI Demonstration Complete\n";
    PrintSeparator('*');
    std::cout << "\n";

    return 0;
}
