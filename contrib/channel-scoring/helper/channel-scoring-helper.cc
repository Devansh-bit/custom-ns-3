/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "channel-scoring-helper.h"

#include "ns3/log.h"

#include <algorithm>
#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("ChannelScoringHelper");

ChannelScoringHelper::ChannelScoringHelper()
    : m_config()
{
    NS_LOG_FUNCTION(this);
}

ChannelScoringHelper::ChannelScoringHelper(const ChannelScoringConfig& config)
    : m_config(config)
{
    NS_LOG_FUNCTION(this);
}

void
ChannelScoringHelper::SetConfig(const ChannelScoringConfig& config)
{
    NS_LOG_FUNCTION(this);
    m_config = config;
}

ChannelScoringConfig
ChannelScoringHelper::GetConfig() const
{
    return m_config;
}

bool
ChannelScoringHelper::IsChannelInBand(uint8_t channel, WifiPhyBand band)
{
    if (band == WIFI_PHY_BAND_2_4GHZ)
    {
        return channel <= 14;
    }
    else if (band == WIFI_PHY_BAND_5GHZ)
    {
        return channel > 14 && channel < 177;
    }
    else if (band == WIFI_PHY_BAND_6GHZ)
    {
        return channel >= 1 && channel <= 233;  // 6 GHz uses different numbering
    }
    return false;
}

double
ChannelScoringHelper::CalculateBssidScore(uint32_t bssidCount)
{
    // Score increases with BSSID count, max at 2 BSSIDs
    // 0 BSSIDs = 0, 1 BSSID = 50, 2+ BSSIDs = 100
    return std::min(static_cast<double>(bssidCount) * 50.0, 100.0);
}

double
ChannelScoringHelper::CalculateRssiScore(const std::vector<ChannelNeighborInfo>& neighbors)
{
    if (neighbors.empty())
    {
        return 0.0;  // No neighbors = best score
    }

    double sumRssi = 0.0;
    for (const auto& neighbor : neighbors)
    {
        sumRssi += neighbor.rssi;
    }
    double avgRssi = sumRssi / neighbors.size();

    // Convert RSSI to score: -90 dBm -> 0, -30 dBm -> 100
    // Higher RSSI (closer) = higher score (worse)
    double score = ((avgRssi + 90.0) / 60.0) * 100.0;
    return std::max(0.0, std::min(100.0, score));
}

double
ChannelScoringHelper::CalculateNonWifiScore(
    const std::vector<ChannelNeighborInfo>& neighbors,
    double scanningRadioNonWifiUtil,
    double nonWifiThreshold,
    bool& shouldDiscard)
{
    shouldDiscard = false;
    double nonWifiPercent = 0.0;

    if (!neighbors.empty())
    {
        // Channels WITH neighbors: Use BSS Load IE non-WiFi from neighbors
        uint8_t maxNonWifi = 0;
        for (const auto& neighbor : neighbors)
        {
            if (neighbor.nonWifiUtil > maxNonWifi)
            {
                maxNonWifi = neighbor.nonWifiUtil;
            }
        }
        // Convert 0-255 to percentage
        nonWifiPercent = (static_cast<double>(maxNonWifi) / 255.0) * 100.0;
    }
    else
    {
        // Channels WITHOUT neighbors: Use scanning radio's measurement
        // Input is 0-1 scale, convert to 0-100%
        nonWifiPercent = scanningRadioNonWifiUtil * 100.0;
    }

    // Check threshold
    if (nonWifiPercent > nonWifiThreshold)
    {
        shouldDiscard = true;
    }

    // Scale to 0-100 score (based on threshold range)
    return std::min((nonWifiPercent / nonWifiThreshold) * 100.0, 100.0);
}

double
ChannelScoringHelper::Get24GHzOverlap(uint8_t ch1, uint8_t ch2)
{
    if (ch1 > 14 || ch2 > 14)
    {
        return 0.0;  // Not 2.4 GHz
    }

    int diff = std::abs(static_cast<int>(ch1) - static_cast<int>(ch2));

    // Channels 5+ apart have no overlap (2.4 GHz channels are 22 MHz wide, 5 MHz apart)
    if (diff >= 5) return 0.0;
    if (diff == 4) return 0.2;
    if (diff == 3) return 0.4;
    if (diff == 2) return 0.6;
    if (diff == 1) return 0.8;
    return 1.0;  // Same channel
}

double
ChannelScoringHelper::Get5GHzOverlap(uint8_t ch1, uint16_t width1, uint8_t ch2, uint16_t width2)
{
    if (ch1 <= 14 || ch2 <= 14)
    {
        return 0.0;  // Not 5 GHz
    }

    // Get channel center frequencies (5000 + ch * 5 MHz)
    int freq1 = 5000 + ch1 * 5;
    int freq2 = 5000 + ch2 * 5;

    // Calculate frequency ranges
    int low1 = freq1 - width1 / 2;
    int high1 = freq1 + width1 / 2;
    int low2 = freq2 - width2 / 2;
    int high2 = freq2 + width2 / 2;

    // Check overlap
    int overlapLow = std::max(low1, low2);
    int overlapHigh = std::min(high1, high2);

    if (overlapHigh <= overlapLow)
    {
        return 0.0;  // No overlap
    }

    // Calculate overlap ratio
    int overlapWidth = overlapHigh - overlapLow;
    int minWidth = std::min(static_cast<int>(width1), static_cast<int>(width2));
    return static_cast<double>(overlapWidth) / minWidth;
}

double
ChannelScoringHelper::CalculateOverlapScore(
    uint8_t targetChannel,
    uint16_t targetWidth,
    const std::map<uint8_t, ChannelScanData>& allChannelData)
{
    double totalOverlap = 0.0;
    int neighborCount = 0;
    std::set<std::string> seenNeighbors;  // Avoid double-counting

    for (const auto& [scannedChannel, scanData] : allChannelData)
    {
        for (const auto& neighbor : scanData.neighbors)
        {
            // Skip if we've already counted this neighbor
            if (seenNeighbors.count(neighbor.bssid) > 0)
            {
                continue;
            }
            seenNeighbors.insert(neighbor.bssid);

            double overlap = 0.0;

            // Determine band and calculate overlap based on neighbor's OPERATING channel
            if (targetChannel <= 14 && neighbor.channel <= 14)
            {
                // Both 2.4 GHz
                overlap = Get24GHzOverlap(targetChannel, neighbor.channel);
            }
            else if (targetChannel > 14 && neighbor.channel > 14)
            {
                // Both 5 GHz
                overlap = Get5GHzOverlap(targetChannel, targetWidth,
                                         neighbor.channel, neighbor.channelWidth);
            }
            // Cross-band (2.4 vs 5 GHz) = no overlap

            if (overlap > 0.0)
            {
                totalOverlap += overlap;
                neighborCount++;
            }
        }
    }

    if (neighborCount == 0)
    {
        return 0.0;
    }

    // Normalize: max expected 2 neighbors with full overlap = 100
    return std::min((totalOverlap / 2.0) * 100.0, 100.0);
}

std::vector<ChannelScore>
ChannelScoringHelper::CalculateScores(const std::map<uint8_t, ChannelScanData>& scanningChannelData,
                                       uint16_t apChannelWidth) const
{
    std::vector<ChannelScore> scores;

    for (const auto& [channel, scanData] : scanningChannelData)
    {
        ChannelScore score;
        score.channel = channel;

        // Factor 1: BSSID Count
        score.bssidScore = CalculateBssidScore(scanData.bssidCount);

        // Factor 2: Average RSSI
        score.rssiScore = CalculateRssiScore(scanData.neighbors);

        // Factor 3: Non-WiFi Utilization
        score.nonWifiScore = CalculateNonWifiScore(
            scanData.neighbors,
            scanData.nonWifiChannelUtilization,
            m_config.nonWifiDiscardThreshold,
            score.discarded);

        // Factor 4: Channel Overlap (use AP's actual channel width)
        score.overlapScore = CalculateOverlapScore(channel, apChannelWidth, scanningChannelData);

        // Calculate weighted total (lower is better)
        if (!score.discarded)
        {
            score.totalScore = m_config.weightBssid * score.bssidScore +
                               m_config.weightRssi * score.rssiScore +
                               m_config.weightNonWifi * score.nonWifiScore +
                               m_config.weightOverlap * score.overlapScore;
        }
        else
        {
            score.totalScore = 999999.0;  // Effectively infinite for discarded channels
        }

        scores.push_back(score);
    }

    // Sort by total score (ascending = best first)
    std::sort(scores.begin(), scores.end(),
              [](const ChannelScore& a, const ChannelScore& b) {
                  return a.totalScore < b.totalScore;
              });

    // Assign ranks
    for (size_t i = 0; i < scores.size(); ++i)
    {
        scores[i].rank = i + 1;
    }

    return scores;
}

uint8_t
ChannelScoringHelper::GetBestChannel(
    const std::map<uint8_t, ChannelScanData>& scanningChannelData) const
{
    auto scores = CalculateScores(scanningChannelData);

    for (const auto& score : scores)
    {
        if (!score.discarded)
        {
            return score.channel;
        }
    }

    return 0;  // No suitable channel found
}

uint8_t
ChannelScoringHelper::GetBestChannelForBand(
    const std::map<uint8_t, ChannelScanData>& scanningChannelData,
    WifiPhyBand band) const
{
    auto scores = CalculateScores(scanningChannelData);

    for (const auto& score : scores)
    {
        if (score.discarded)
        {
            continue;
        }

        // Check if channel belongs to requested band
        if (IsChannelInBand(score.channel, band))
        {
            return score.channel;
        }
    }

    return 0;  // No suitable channel found for this band
}

// ============================================================================
// RL Integration Methods
// ============================================================================

void
ChannelScoringHelper::UpdateRlChannelBounds(uint32_t nodeId, uint8_t channelNumber, uint16_t channelWidthMhz)
{
    NS_LOG_FUNCTION(this << nodeId << (uint32_t)channelNumber << channelWidthMhz);

    ApChannelState& state = m_apChannelStates[nodeId];
    state.nodeId = nodeId;
    state.rlChannelNumber = channelNumber;
    state.rlChannelWidthMhz = channelWidthMhz;
    state.rlBoundsSet = true;

    // Get valid channels for logging
    auto validChannels = GetValidChannelsForWidth(channelWidthMhz);

    std::cout << "[RL CHANNEL BOUNDS] AP " << nodeId
              << ": channel=" << (uint32_t)channelNumber
              << ", width=" << channelWidthMhz << "MHz"
              << ", valid={";

    bool first = true;
    for (auto ch : validChannels)
    {
        if (!first) std::cout << ",";
        std::cout << (uint32_t)ch;
        first = false;
    }
    std::cout << "}" << std::endl;
}

std::set<uint8_t>
ChannelScoringHelper::GetValidChannelsForWidth(uint16_t widthMhz)
{
    switch (widthMhz)
    {
        case 20:
            return VALID_20MHZ_CHANNELS;
        case 40:
            return VALID_40MHZ_CHANNELS;
        case 80:
            return VALID_80MHZ_CHANNELS;
        default:
            // Default to 80 MHz if unknown width
            NS_LOG_WARN("Unknown channel width " << widthMhz << " MHz, defaulting to 80 MHz channels");
            return VALID_80MHZ_CHANNELS;
    }
}

std::vector<ChannelScore>
ChannelScoringHelper::CalculateScoresForRlWidth(
    uint32_t nodeId,
    const std::map<uint8_t, ChannelScanData>& scanningChannelData) const
{
    NS_LOG_FUNCTION(this << nodeId);

    // Get RL-assigned width for this AP
    uint16_t width = 80;  // Default
    auto it = m_apChannelStates.find(nodeId);
    if (it != m_apChannelStates.end() && it->second.rlBoundsSet)
    {
        width = it->second.rlChannelWidthMhz;
    }
    else
    {
        NS_LOG_DEBUG("AP " << nodeId << " has no RL bounds set, using default 80 MHz");
    }

    // Get valid channels for this width
    auto validChannels = GetValidChannelsForWidth(width);

    // Filter scan data to only include valid channels
    std::map<uint8_t, ChannelScanData> filteredData;
    for (const auto& [channel, data] : scanningChannelData)
    {
        if (validChannels.count(channel) > 0)
        {
            filteredData[channel] = data;
        }
    }

    NS_LOG_DEBUG("AP " << nodeId << ": width=" << width << "MHz, "
                 << filteredData.size() << "/" << scanningChannelData.size()
                 << " channels valid for RL width");

    // Calculate scores for filtered channels only
    return CalculateScores(filteredData, width);
}

uint8_t
ChannelScoringHelper::GetBestChannelForRlWidth(
    uint32_t nodeId,
    const std::map<uint8_t, ChannelScanData>& scanningChannelData) const
{
    NS_LOG_FUNCTION(this << nodeId);

    auto scores = CalculateScoresForRlWidth(nodeId, scanningChannelData);

    for (const auto& score : scores)
    {
        if (!score.discarded)
        {
            NS_LOG_DEBUG("AP " << nodeId << ": best channel for RL width = " << (uint32_t)score.channel);
            return score.channel;
        }
    }

    NS_LOG_WARN("AP " << nodeId << ": no suitable channel found for RL width");
    return 0;  // No suitable channel found
}

const ApChannelState*
ChannelScoringHelper::GetApChannelState(uint32_t nodeId) const
{
    auto it = m_apChannelStates.find(nodeId);
    if (it != m_apChannelStates.end())
    {
        return &(it->second);
    }
    return nullptr;
}

const std::map<uint32_t, ApChannelState>&
ChannelScoringHelper::GetAllApChannelStates() const
{
    return m_apChannelStates;
}

}  // namespace ns3
