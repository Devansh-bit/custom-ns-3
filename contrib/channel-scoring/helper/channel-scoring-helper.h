/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Channel Scoring Helper for ACS/DCS (Automatic/Dynamic Channel Selection)
 *
 * This module provides channel scoring functionality based on:
 * - BSSID count (number of neighbor APs)
 * - RSSI (signal strength of neighbors)
 * - Non-WiFi interference (from BSS Load IE or scanning radio CCA)
 * - Channel overlap (spectral overlap with neighbors)
 */

#ifndef CHANNEL_SCORING_HELPER_H
#define CHANNEL_SCORING_HELPER_H

#include "ns3/nstime.h"
#include "ns3/wifi-phy-band.h"
#include "ns3/kafka-producer.h"  // For ChannelScanData, ChannelNeighborInfo

#include <map>
#include <vector>
#include <string>
#include <cstdint>
#include <set>

namespace ns3
{

// ============================================================================
// Valid channel sets by width (5 GHz only)
// 20 MHz: Primary channels
// 40/80 MHz: CENTER channels (ns-3 auto-detects width from center channel)
// ============================================================================

/// Valid 20 MHz primary channels (5 GHz)
const std::set<uint8_t> VALID_20MHZ_CHANNELS = {
    36, 40, 44, 48,           // UNII-1
    52, 56, 60, 64,           // UNII-2A (DFS)
    100, 104, 108, 112,       // UNII-2C
    116, 120, 124, 128,
    132, 136, 140, 144,
    149, 153, 157, 161        // UNII-3
};

/// Valid 40 MHz center channels (5 GHz)
const std::set<uint8_t> VALID_40MHZ_CHANNELS = {
    38, 46,                   // UNII-1 (center of 36+40, 44+48)
    54, 62,                   // UNII-2A
    102, 110, 118, 126,       // UNII-2C
    134, 142,
    151, 159                  // UNII-3
};

/// Valid 80 MHz center channels (5 GHz)
const std::set<uint8_t> VALID_80MHZ_CHANNELS = {
    42,                       // UNII-1 (center of 36-48)
    58,                       // UNII-2A
    106, 122, 138,            // UNII-2C
    155                       // UNII-3
};

/**
 * \brief Per-AP state for RL channel bounds
 */
struct ApChannelState
{
    uint32_t nodeId = 0;
    uint8_t rlChannelNumber = 0;       ///< Channel assigned by RL (already applied)
    uint16_t rlChannelWidthMhz = 80;   ///< Width bounds fast loop scoring
    bool rlBoundsSet = false;          ///< True after first RL command received
};

/**
 * \brief Channel score result for a single channel
 */
struct ChannelScore
{
    uint8_t channel = 0;
    double bssidScore = 0.0;      // Score from BSSID count (0-100)
    double rssiScore = 0.0;       // Score from avg RSSI (0-100)
    double nonWifiScore = 0.0;    // Score from non-WiFi interference (0-100)
    double overlapScore = 0.0;    // Score from channel overlap (0-100)
    double totalScore = 0.0;      // Weighted sum of all scores
    bool discarded = false;       // True if channel exceeds non-WiFi threshold
    uint32_t rank = 0;            // 1 = best channel
};

/**
 * \brief Configuration for channel scoring weights and thresholds
 */
struct ChannelScoringConfig
{
    double weightBssid = 0.5;
    double weightRssi = 0.5;
    double weightNonWifi = 0.5;
    double weightOverlap = 0.5;
    double nonWifiDiscardThreshold = 40.0;  // Discard channels with >40% non-WiFi
};

/**
 * \brief Helper class for channel scoring (ACS/DCS)
 *
 * Takes scan data as input and produces channel scores.
 * Data sources remain external (e.g., in config-simulation.cc).
 */
class ChannelScoringHelper
{
  public:
    /**
     * \brief Constructor with default config
     */
    ChannelScoringHelper();

    /**
     * \brief Constructor with custom config
     * \param config Scoring configuration (weights, thresholds)
     */
    explicit ChannelScoringHelper(const ChannelScoringConfig& config);

    /**
     * \brief Set scoring configuration
     * \param config New configuration
     */
    void SetConfig(const ChannelScoringConfig& config);

    /**
     * \brief Get current configuration
     * \return Current scoring configuration
     */
    ChannelScoringConfig GetConfig() const;

    /**
     * \brief Calculate scores for all channels in scan data
     * \param scanningChannelData Map of channel -> ChannelScanData
     * \param apChannelWidth AP's current channel width in MHz (for overlap calculation)
     * \return Vector of ChannelScore sorted by totalScore (best first)
     */
    std::vector<ChannelScore> CalculateScores(
        const std::map<uint8_t, ChannelScanData>& scanningChannelData,
        uint16_t apChannelWidth = 20) const;

    /**
     * \brief Get the best channel for a specific band
     * \param scanningChannelData Map of channel -> ChannelScanData
     * \param band WiFi band to filter (WIFI_PHY_BAND_2_4GHZ or WIFI_PHY_BAND_5GHZ)
     * \return Best channel number for the band (0 if none found)
     */
    uint8_t GetBestChannelForBand(
        const std::map<uint8_t, ChannelScanData>& scanningChannelData,
        WifiPhyBand band) const;

    /**
     * \brief Get the best channel from scan data (any band)
     * \param scanningChannelData Map of channel -> ChannelScanData
     * \return Best channel number (lowest score, not discarded)
     */
    uint8_t GetBestChannel(
        const std::map<uint8_t, ChannelScanData>& scanningChannelData) const;

    /**
     * \brief Check if a channel belongs to a specific band
     * \param channel Channel number
     * \param band WiFi band
     * \return True if channel belongs to the band
     */
    static bool IsChannelInBand(uint8_t channel, WifiPhyBand band);

    /**
     * \brief Calculate BSSID count score
     * \param bssidCount Number of neighbor APs on channel
     * \return Score 0-100 (higher = worse)
     */
    static double CalculateBssidScore(uint32_t bssidCount);

    /**
     * \brief Calculate average RSSI score
     * \param neighbors List of neighbor APs
     * \return Score 0-100 (higher RSSI = closer neighbors = higher score = worse)
     */
    static double CalculateRssiScore(const std::vector<ChannelNeighborInfo>& neighbors);

    /**
     * \brief Calculate non-WiFi interference score
     * \param neighbors List of neighbor APs (uses BSS Load IE data if present)
     * \param scanningRadioNonWifiUtil Scanning radio's CCA non-WiFi measurement (0-100%)
     * \param nonWifiThreshold Threshold for discarding channel
     * \param shouldDiscard [out] Set to true if channel should be discarded
     * \return Score 0-100 (higher = more interference = worse)
     */
    static double CalculateNonWifiScore(
        const std::vector<ChannelNeighborInfo>& neighbors,
        double scanningRadioNonWifiUtil,
        double nonWifiThreshold,
        bool& shouldDiscard);

    /**
     * \brief Calculate channel overlap score
     * \param targetChannel Channel to evaluate
     * \param targetWidth Channel width in MHz
     * \param allChannelData All scan data (to find neighbors)
     * \return Score 0-100 (higher = more overlap = worse)
     */
    static double CalculateOverlapScore(
        uint8_t targetChannel,
        uint16_t targetWidth,
        const std::map<uint8_t, ChannelScanData>& allChannelData);

    /**
     * \brief Calculate 2.4 GHz channel overlap factor
     * \param ch1 First channel number
     * \param ch2 Second channel number
     * \return Overlap factor 0-1 (0 = no overlap, 1 = full overlap)
     */
    static double Get24GHzOverlap(uint8_t ch1, uint8_t ch2);

    /**
     * \brief Calculate 5 GHz channel overlap factor
     * \param ch1 First channel number
     * \param width1 First channel width in MHz
     * \param ch2 Second channel number
     * \param width2 Second channel width in MHz
     * \return Overlap factor 0-1 (0 = no overlap, 1 = full overlap)
     */
    static double Get5GHzOverlap(uint8_t ch1, uint16_t width1, uint8_t ch2, uint16_t width2);

    // =========================================================================
    // RL Integration Methods
    // =========================================================================

    /**
     * \brief Update RL channel bounds for an AP
     *
     * Called when RL command is received. Stores the channel width constraint
     * that the fast loop channel scoring will use to filter valid channels.
     *
     * \param nodeId AP node ID
     * \param channelNumber Channel assigned by RL (already applied to PHY)
     * \param channelWidthMhz Channel width - fast loop will only suggest valid channels
     */
    void UpdateRlChannelBounds(uint32_t nodeId, uint8_t channelNumber, uint16_t channelWidthMhz);

    /**
     * \brief Get valid channels for a specific width
     * \param widthMhz Channel width (20, 40, or 80 MHz)
     * \return Set of valid channel numbers for that width
     */
    static std::set<uint8_t> GetValidChannelsForWidth(uint16_t widthMhz);

    /**
     * \brief Calculate scores filtered by RL-assigned width
     *
     * Only scores channels that are valid for the AP's RL-assigned width.
     * If RL bounds not set, defaults to all channels.
     *
     * \param nodeId AP node ID
     * \param scanningChannelData Map of channel -> ChannelScanData
     * \return Vector of ChannelScore for valid channels only, sorted by totalScore
     */
    std::vector<ChannelScore> CalculateScoresForRlWidth(
        uint32_t nodeId,
        const std::map<uint8_t, ChannelScanData>& scanningChannelData) const;

    /**
     * \brief Get the best channel respecting RL width bounds
     *
     * Returns the best channel that is valid for the AP's RL-assigned width.
     *
     * \param nodeId AP node ID
     * \param scanningChannelData Scan data
     * \return Best valid channel for RL width (0 if none found)
     */
    uint8_t GetBestChannelForRlWidth(
        uint32_t nodeId,
        const std::map<uint8_t, ChannelScanData>& scanningChannelData) const;

    /**
     * \brief Get AP channel state (for debugging)
     * \param nodeId AP node ID
     * \return Pointer to ApChannelState or nullptr if not found
     */
    const ApChannelState* GetApChannelState(uint32_t nodeId) const;

    /**
     * \brief Get all AP channel states (for logging/debugging)
     */
    const std::map<uint32_t, ApChannelState>& GetAllApChannelStates() const;

  private:
    ChannelScoringConfig m_config;  ///< Scoring configuration
    std::map<uint32_t, ApChannelState> m_apChannelStates;  ///< Per-AP RL channel state
};

}  // namespace ns3

#endif  // CHANNEL_SCORING_HELPER_H
