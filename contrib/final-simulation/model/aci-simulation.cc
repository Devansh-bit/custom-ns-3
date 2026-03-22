/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Adjacent Channel Interference (ACI) Simulation Module
 */

#include "aci-simulation.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("AciSimulation");

// Static member initialization
AciConfig AciSimulation::s_config;
std::map<uint32_t, ApAciInfo> AciSimulation::s_apCache;
std::unordered_map<int64_t, AciGridCell> AciSimulation::s_spatialGrid;
bool AciSimulation::s_cacheDirty = true;

// 5 GHz Channel Lookup Table (from ns-3.46.1 wifi-phy-operating-channel.cc)
// Channel number determines both center frequency AND width
static const std::map<uint8_t, AciChannelInfo> s_5GHzChannels = {
    // 20 MHz channels
    {36, {5180, 20}}, {40, {5200, 20}}, {44, {5220, 20}}, {48, {5240, 20}},
    {52, {5260, 20}}, {56, {5280, 20}}, {60, {5300, 20}}, {64, {5320, 20}},
    {100, {5500, 20}}, {104, {5520, 20}}, {108, {5540, 20}}, {112, {5560, 20}},
    {116, {5580, 20}}, {120, {5600, 20}}, {124, {5620, 20}}, {128, {5640, 20}},
    {132, {5660, 20}}, {136, {5680, 20}}, {140, {5700, 20}}, {144, {5720, 20}},
    {149, {5745, 20}}, {153, {5765, 20}}, {157, {5785, 20}}, {161, {5805, 20}},
    {165, {5825, 20}}, {169, {5845, 20}}, {173, {5865, 20}}, {177, {5885, 20}},
    // 40 MHz channels (center of two bonded 20 MHz)
    {38, {5190, 40}}, {46, {5230, 40}}, {54, {5270, 40}}, {62, {5310, 40}},
    {102, {5510, 40}}, {110, {5550, 40}}, {118, {5590, 40}}, {126, {5630, 40}},
    {134, {5670, 40}}, {142, {5710, 40}}, {151, {5755, 40}}, {159, {5795, 40}},
    // 80 MHz channels (center of four bonded 20 MHz)
    {42, {5210, 80}}, {58, {5290, 80}}, {106, {5530, 80}}, {122, {5610, 80}},
    {138, {5690, 80}}, {155, {5775, 80}},
    // 160 MHz channels (center of eight bonded 20 MHz)
    {50, {5250, 160}}, {114, {5570, 160}}, {163, {5815, 160}},
};

// 2.4 GHz Channel Lookup (20 MHz OFDM)
static const std::map<uint8_t, AciChannelInfo> s_2_4GHzChannels = {
    {1, {2412, 20}}, {2, {2417, 20}}, {3, {2422, 20}}, {4, {2427, 20}},
    {5, {2432, 20}}, {6, {2437, 20}}, {7, {2442, 20}}, {8, {2447, 20}},
    {9, {2452, 20}}, {10, {2457, 20}}, {11, {2462, 20}}, {12, {2467, 20}},
    {13, {2472, 20}},
};

void
AciSimulation::SetConfig(const AciConfig& config)
{
    s_config = config;
    s_cacheDirty = true;
}

const AciConfig&
AciSimulation::GetConfig()
{
    return s_config;
}

bool
AciSimulation::IsEnabled()
{
    return s_config.enabled;
}

AciChannelInfo
AciSimulation::GetChannelInfo(uint8_t channel, WifiPhyBand band)
{
    if (band == WIFI_PHY_BAND_5GHZ || band == WIFI_PHY_BAND_6GHZ)
    {
        auto it = s_5GHzChannels.find(channel);
        if (it != s_5GHzChannels.end())
        {
            return it->second;
        }
        // Fallback: derive from channel number (assume 20 MHz)
        return {static_cast<uint16_t>(5000 + channel * 5), 20};
    }
    // 2.4 GHz
    auto it = s_2_4GHzChannels.find(channel);
    if (it != s_2_4GHzChannels.end())
    {
        return it->second;
    }
    return {static_cast<uint16_t>(2407 + channel * 5), 20};
}

double
AciSimulation::GetChannelOverlap(uint8_t ch1, uint8_t ch2, WifiPhyBand band)
{
    if (ch1 == ch2)
    {
        return 1.0; // Same channel = full overlap
    }

    AciChannelInfo info1 = GetChannelInfo(ch1, band); // Victim
    AciChannelInfo info2 = GetChannelInfo(ch2, band); // Interferer

    // Calculate frequency ranges
    double range1Low = info1.centerFreqMHz - info1.widthMHz / 2.0;
    double range1High = info1.centerFreqMHz + info1.widthMHz / 2.0;
    double range2Low = info2.centerFreqMHz - info2.widthMHz / 2.0;
    double range2High = info2.centerFreqMHz + info2.widthMHz / 2.0;

    // Calculate direct spectral overlap
    double overlapLow = std::max(range1Low, range2Low);
    double overlapHigh = std::min(range1High, range2High);
    double overlapBandwidth = std::max(0.0, overlapHigh - overlapLow);

    // Normalize by victim's bandwidth to calculate interference fraction
    double directOverlap = overlapBandwidth / info1.widthMHz;

    if (directOverlap > 0.01)
    {
        return directOverlap; // Direct spectral overlap
    }

    // No direct overlap - calculate adjacent channel leakage
    double gapMHz =
        std::min(std::abs(range1Low - range2High), std::abs(range2Low - range1High));

    // Spectral mask attenuation model (simplified)
    if (gapMHz < 5)
        return 0.10; // Very close, sidelobe leakage
    if (gapMHz < 10)
        return 0.05; // Adjacent channel leakage
    if (gapMHz < 20)
        return 0.01; // Minor sidelobe
    return 0.0;      // Non-overlapping
}

int64_t
AciSimulation::GetGridCellHash(double x, double y)
{
    int cellX = static_cast<int>(std::floor(x / GRID_CELL_SIZE));
    int cellY = static_cast<int>(std::floor(y / GRID_CELL_SIZE));
    return (static_cast<int64_t>(cellX) << 32) | static_cast<uint32_t>(cellY);
}

std::vector<int64_t>
AciSimulation::GetNeighborCells(int64_t centerCell)
{
    int cellX = static_cast<int>(centerCell >> 32);
    int cellY = static_cast<int>(centerCell & 0xFFFFFFFF);
    std::vector<int64_t> neighbors;
    neighbors.reserve(9);
    for (int dx = -1; dx <= 1; dx++)
    {
        for (int dy = -1; dy <= 1; dy++)
        {
            neighbors.push_back((static_cast<int64_t>(cellX + dx) << 32) |
                                static_cast<uint32_t>(cellY + dy));
        }
    }
    return neighbors;
}

void
AciSimulation::UpdateSpatialGrid()
{
    s_spatialGrid.clear();
    for (auto& [nodeId, info] : s_apCache)
    {
        int64_t cell = GetGridCellHash(info.position.x, info.position.y);
        info.gridCell = cell;
        s_spatialGrid[cell].apIds.push_back(nodeId);
    }
}

double
AciSimulation::CalculateDistanceFactor(double distance)
{
    if (distance <= 1.0)
        return 1.0;
    if (distance >= s_config.maxInterferenceDistanceM)
        return 0.0;

    // Log-distance normalized to [0,1]
    double n = s_config.pathLossExponent;
    double pathLossDb = 10.0 * n * std::log10(distance);
    double maxPathLossDb = 10.0 * n * std::log10(s_config.maxInterferenceDistanceM);
    return std::max(0.0, 1.0 - (pathLossDb / maxPathLossDb));
}

double
AciSimulation::CalculateTxPowerFactor(double txPowerDbm)
{
    return 1.0 + (txPowerDbm - 20.0) / 20.0; // Range [0.5, 1.5]
}

double
AciSimulation::CalculateClientWeightFactor(uint32_t clientCount)
{
    return 1.0 + (clientCount * s_config.clientWeightFactor);
}

double
AciSimulation::CalculateInterferenceFactor(uint32_t targetApId)
{
    auto targetIt = s_apCache.find(targetApId);
    if (targetIt == s_apCache.end())
    {
        return 0.0;
    }

    const ApAciInfo& target = targetIt->second;
    double totalInterference = 0.0;

    // Only check neighboring grid cells (O(k) instead of O(N))
    auto neighborCells = GetNeighborCells(target.gridCell);

    for (int64_t cell : neighborCells)
    {
        auto cellIt = s_spatialGrid.find(cell);
        if (cellIt == s_spatialGrid.end())
        {
            continue;
        }

        for (uint32_t otherId : cellIt->second.apIds)
        {
            if (otherId == targetApId)
            {
                continue;
            }

            const ApAciInfo& other = s_apCache[otherId];

            // Skip different bands (no cross-band interference)
            if (target.band != other.band)
            {
                continue;
            }

            // Channel overlap (channel number encodes width)
            double channelOverlap = GetChannelOverlap(target.channel, other.channel, target.band);
            if (channelOverlap < 0.01)
            {
                continue;
            }

            // Distance factor
            double dx = target.position.x - other.position.x;
            double dy = target.position.y - other.position.y;
            double dz = target.position.z - other.position.z;
            double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            double distFactor = CalculateDistanceFactor(distance);
            if (distFactor < 0.01)
            {
                continue;
            }

            // TX power and client weight
            double txFactor = CalculateTxPowerFactor(other.txPowerDbm);
            double clientFactor = CalculateClientWeightFactor(other.associatedClients);

            // Combine factors
            totalInterference += channelOverlap * distFactor * txFactor * clientFactor;

            NS_LOG_DEBUG("[ACI] AP" << targetApId << " <- AP" << otherId
                                    << ": ch_overlap=" << channelOverlap << ", dist=" << distance
                                    << "m"
                                    << ", dist_factor=" << distFactor
                                    << ", contrib="
                                    << (channelOverlap * distFactor * txFactor * clientFactor));
        }
    }

    // Cap at 2.0 (severe interference)
    return std::min(totalInterference, 2.0);
}

void
AciSimulation::ApplyToConnectionMetrics(ConnectionMetrics& conn, double IF)
{
    if (IF <= 0.0)
    {
        return;
    }

    const auto& d = s_config.degradation;

    // 1. Throughput: multiplicative reduction
    double throughputMult = std::max(0.0, 1.0 - (d.throughput * IF));
    conn.uplinkThroughputMbps *= throughputMult;
    conn.downlinkThroughputMbps *= throughputMult;

    // 2. Packet loss: additive increase
    conn.packetLossRate = std::min(100.0, conn.packetLossRate + d.packetLoss * IF);

    // 3. Latency: multiplicative increase
    conn.meanRTTLatency *= (1.0 + d.latency * IF);

    // 4. Jitter: multiplicative increase
    conn.jitterMs *= (1.0 + d.jitter * IF);

    // 5. MAC retry rate: additive increase
    conn.MACRetryRate = std::min(100.0, conn.MACRetryRate + 10.0 * IF);
}

void
AciSimulation::ApplyToApMetrics(ApMetrics& ap, double IF)
{
    if (IF <= 0.0)
    {
        return;
    }

    // Channel utilization: additive increase (interference = busy medium)
    // ACI is WiFi-to-WiFi interference, so add to wifiChannelUtilization
    // Then recalculate total to maintain consistency: total = wifi + non_wifi
    double aciIncrease = s_config.degradation.channelUtil * IF;
    ap.wifiChannelUtilization = std::min(1.0, ap.wifiChannelUtilization + aciIncrease);
    ap.channelUtilization = std::min(1.0, ap.wifiChannelUtilization + ap.nonWifiChannelUtilization);

    // Recalculate aggregate throughput from connections
    double totalThroughput = 0.0;
    for (const auto& [connId, conn] : ap.connectionMetrics)
    {
        totalThroughput += conn.uplinkThroughputMbps + conn.downlinkThroughputMbps;
    }
    ap.throughputMbps = totalThroughput;
}

void
AciSimulation::UpdateCache(const std::map<uint32_t, ApMetrics>& apMetrics)
{
    for (auto& [nodeId, info] : s_apCache)
    {
        auto apIt = apMetrics.find(nodeId);
        if (apIt != apMetrics.end())
        {
            info.channel = apIt->second.channel;
            info.band = apIt->second.band;
            info.associatedClients = apIt->second.associatedClients;
        }
    }
    UpdateSpatialGrid();
    s_cacheDirty = false;
}

void
AciSimulation::Initialize(const std::vector<ApConfigData>& aps,
                          const std::map<uint32_t, ApMetrics>& apMetrics)
{
    if (!s_config.enabled)
    {
        NS_LOG_INFO("[ACI] Adjacent Channel Interference simulation DISABLED");
        return;
    }

    s_apCache.clear();

    // Populate cache from AP configs and metrics
    for (size_t i = 0; i < aps.size(); i++)
    {
        uint32_t nodeId = aps[i].nodeId;

        ApAciInfo info;
        info.nodeId = nodeId;
        info.position = aps[i].position;
        info.txPowerDbm = aps[i].txPower;
        info.channel = aps[i].channel;

        // Get band and client count from metrics if available
        auto apIt = apMetrics.find(nodeId);
        if (apIt != apMetrics.end())
        {
            info.band = apIt->second.band;
            info.associatedClients = apIt->second.associatedClients;
        }
        else
        {
            info.band = WIFI_PHY_BAND_5GHZ; // Default
            info.associatedClients = 0;
        }

        s_apCache[nodeId] = info;
    }

    UpdateSpatialGrid();
    s_cacheDirty = false;

    NS_LOG_INFO("[ACI] Initialized for " << s_apCache.size() << " APs");
    NS_LOG_INFO("[ACI] Config: pathLossExp=" << s_config.pathLossExponent
                                             << ", maxDist=" << s_config.maxInterferenceDistanceM
                                             << "m"
                                             << ", clientWeight=" << s_config.clientWeightFactor);
}

void
AciSimulation::Apply(std::map<uint32_t, ApMetrics>& apMetrics)
{
    if (!s_config.enabled)
    {
        return;
    }

    if (s_cacheDirty)
    {
        UpdateCache(apMetrics);
    }

    NS_LOG_DEBUG("[ACI] Applying ACI simulation to " << apMetrics.size() << " APs");

    // Calculate and apply interference for each AP
    for (auto& [nodeId, ap] : apMetrics)
    {
        double IF = CalculateInterferenceFactor(nodeId);

        if (IF > 0.01)
        {
            NS_LOG_DEBUG("[ACI] AP" << nodeId << " (CH" << +ap.channel << "): IF=" << IF);

            // Apply to connection metrics
            for (auto& [connId, conn] : ap.connectionMetrics)
            {
                ApplyToConnectionMetrics(conn, IF);
            }

            // Apply to AP metrics
            ApplyToApMetrics(ap, IF);
        }
    }
}

void
AciSimulation::OnChannelChanged(uint32_t nodeId, uint8_t newChannel)
{
    auto it = s_apCache.find(nodeId);
    if (it != s_apCache.end())
    {
        it->second.channel = newChannel;
        s_cacheDirty = true;
        NS_LOG_DEBUG("[ACI] Cache invalidated: AP" << nodeId << " -> CH" << +newChannel);
    }
}

} // namespace ns3
