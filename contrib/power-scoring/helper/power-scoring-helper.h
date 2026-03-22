/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Power Scoring Helper for Dynamic TX Power Control (RACEBOT-based)
 *
 * This module provides TX power control functionality based on:
 * - RSSI (from AP's perspective via HeSigA callback)
 * - BSS Color (for OBSS detection - 802.11ax)
 * - MCS (simulated from SNR)
 * - Non-WiFi interference (from BSS Load IE or scanning radio CCA)
 */

#ifndef POWER_SCORING_HELPER_H
#define POWER_SCORING_HELPER_H

#include "ns3/nstime.h"
#include "ns3/wifi-phy-band.h"
#include "ns3/kafka-producer.h"  // For ChannelScanData, ChannelNeighborInfo

#include <map>
#include <vector>
#include <string>
#include <cstdint>

namespace ns3
{

/**
 * \brief OBSS Tracker for per-AP RSSI and MCS tracking
 */
struct ObssTracker
{
    std::map<int, double> rssiCounts;     // RSSI histogram (EWMA counts)
    std::map<int, int> rawCounts;         // Raw RSSI counts
    double bssRssiEwma = -70.0;           // EWMA of intra-BSS RSSI
    double mcsEwma = 7.0;                 // EWMA of MCS
    double prevMcsEwma = 7.0;             // Previous MCS EWMA (for delta detection)
    uint32_t frameCount = 0;              // Total frames received
    uint32_t obssFrameCount = 0;          // OBSS frames received

    void UpdateRssiCount(int rssi, double alpha, bool isObss);
    void UpdateBssRssi(double rssi, double alpha);
    void UpdateMcs(double mcs, double alpha);
    void Reset();
    double GetObssRatio() const;
};

/**
 * \brief Configuration for power scoring algorithm
 */
struct PowerScoringConfig
{
    // RACEBOT algorithm parameters
    double margin = 3.0;                    // M parameter (dBm)
    double gamma = 0.7;                     // MCS change threshold
    double alpha = 0.3;                     // EWMA smoothing factor
    double ofcThreshold = 500.0;              // OFC threshold (frame count)

    // OBSS/PD bounds
    double obsspdMinDbm = -82.0;            // Minimum OBSS/PD (dBm)
    double obsspdMaxDbm = -62.0;            // Maximum OBSS/PD (dBm)

    // TX Power bounds
    double txPowerRefDbm = 33.0;            // Reference/Maximum TX Power (dBm)
    double txPowerMinDbm = 10.0;             // Minimum TX Power (dBm)

    // Non-WiFi mode parameters
    double nonWifiThresholdPercent = 50.0;  // Non-WiFi threshold for mode switch (%)
    double nonWifiHysteresis = 10.0;         // Hysteresis for mode transitions (%)

    // RACEBOT two-loop timing architecture
    double t1IntervalSec = 10.0;            // Slow loop: goal recalculation interval (Stages 1&2)
    double t2IntervalSec = 2.0;             // Fast loop: power change cooldown (Stage 3)

    // Update interval
    double updateIntervalSec = 1.0;         // How often to run algorithm (seconds)

    // RL integration: margin for power bounds around RL-assigned center
    double rlPowerMarginDbm = 2.0;          // ±margin around RL power center (e.g., ±2 dBm)
};

/**
 * \brief Result of power calculation for one AP
 */
struct PowerResult
{
    uint32_t nodeId = 0;
    double txPowerDbm = 21.0;               // Calculated TX power
    double obsspdLevelDbm = -82.0;          // Calculated OBSS/PD level
    double goalObsspdDbm = -82.0;           // Target OBSS/PD level
    bool inNonWifiMode = false;             // True if in non-WiFi interference mode
    bool powerChanged = false;              // True if power changed significantly
    bool goalRecalculated = false;          // True if goal was recalculated this cycle (t1 triggered)
    bool inT2Cooldown = false;              // True if in t2 power change cooldown
    double timeToNextGoalRecalc = 0.0;      // Seconds until next t1 goal recalculation
    double timeRemainingT2Cooldown = 0.0;   // Seconds remaining in t2 cooldown (0 if not in cooldown)
    std::string reason;                     // Reason for power change
};

/**
 * \brief Per-AP state for power scoring
 */
struct ApPowerState
{
    uint32_t nodeId = 0;
    uint8_t myBssColor = 0;                 // This AP's BSS Color
    ObssTracker tracker;                    // RSSI/MCS tracker
    double currentTxPowerDbm = 21.0;        // Current TX power
    double currentObsspdDbm = -82.0;        // Current OBSS/PD level
    double goalObsspdDbm = -82.0;           // Goal OBSS/PD level
    bool inNonWifiMode = false;             // Non-WiFi mode flag
    double lastNonWifiPercent = 0.0;        // Last non-WiFi measurement

    // Timer tracking for two-loop architecture
    Time lastPowerChange;                   // Last time TX power actually changed
    Time lastGoalRecalculation;             // Last time goal OBSS/PD was recalculated
    bool initialized = false;               // Flag for first-run initialization

    // RL integration: per-AP bounds (set by slow loop RL, used by fast loop RACEBOT)
    double rlPowerCenterDbm = 20.0;         // Power center assigned by RL
    double rlPowerMinDbm = 10.0;            // Effective min power (center - margin)
    double rlPowerMaxDbm = 20.0;            // Effective max power (center + margin)
    double rlObsspdMinDbm = -82.0;          // Effective min OBSS/PD (derived from power)
    double rlObsspdMaxDbm = -62.0;          // Effective max OBSS/PD (derived from power)
    uint16_t rlChannelWidthMhz = 80;        // Channel width assigned by RL
    bool rlBoundsSet = false;               // True after first RL command received
};

/**
 * \brief Helper class for power scoring (RACEBOT-based algorithm)
 *
 * Takes RSSI, BSS Color, MCS, and Non-WiFi data as input.
 * Produces TX power recommendations for each AP.
 */
class PowerScoringHelper
{
  public:
    /**
     * \brief Constructor with default config
     */
    PowerScoringHelper();

    /**
     * \brief Constructor with custom config
     * \param config Power scoring configuration
     */
    explicit PowerScoringHelper(const PowerScoringConfig& config);

    /**
     * \brief Set configuration
     * \param config New configuration
     */
    void SetConfig(const PowerScoringConfig& config);

    /**
     * \brief Get current configuration
     * \return Current configuration
     */
    PowerScoringConfig GetConfig() const;

    /**
     * \brief Initialize or get AP power state
     * \param nodeId AP node ID
     * \param bssColor AP's BSS Color (from HeConfiguration)
     * \return Reference to AP's power state
     */
    ApPowerState& GetOrCreateApState(uint32_t nodeId, uint8_t bssColor);

    /**
     * \brief Process received HE-SIG-A data (RSSI + BSS Color)
     * \param nodeId AP node ID that received the frame
     * \param rssiDbm RSSI of received frame
     * \param rxBssColor BSS Color from received frame
     *
     * Call this from HeSigA callback for each received HE frame.
     */
    void ProcessHeSigA(uint32_t nodeId, double rssiDbm, uint8_t rxBssColor);

    // DEPRECATED: Now using actual MCS from PhyRxPayloadBegin trace
    // /**
    //  * \brief Update MCS for an AP (simulated from SNR)
    //  * \param nodeId AP node ID
    //  * \param snrDb SNR in dB
    //  */
    // void UpdateMcsFromSnr(uint32_t nodeId, double snrDb);

    /**
     * \brief Update MCS for an AP with actual MCS value
     * \param nodeId AP node ID
     * \param mcs MCS index (0-11 for 802.11ax)
     */
    void UpdateMcs(uint32_t nodeId, uint8_t mcs);

    /**
     * \brief Calculate power for a single AP
     * \param nodeId AP node ID
     * \param nonWifiPercent Non-WiFi interference percentage (0-100)
     * \return Power calculation result
     */
    PowerResult CalculatePower(uint32_t nodeId, double nonWifiPercent);

    /**
     * \brief Calculate power for all tracked APs
     * \param nonWifiData Map of nodeId -> non-WiFi percentage
     * \return Vector of power results
     */
    std::vector<PowerResult> CalculateAllPowers(
        const std::map<uint32_t, double>& nonWifiData);

    // DEPRECATED: Now using actual MCS from PhyRxPayloadBegin trace
    // /**
    //  * \brief Simulate MCS from SNR (802.11ax mapping)
    //  * \param snrDb SNR in dB
    //  * \return Simulated MCS index (0-11)
    //  */
    // static uint8_t SimulateMcsFromSnr(double snrDb);

    /**
     * \brief Get non-WiFi percentage from scan data
     * \param scanData Channel scan data
     * \return Non-WiFi percentage (0-100)
     *
     * Uses BSS Load IE if neighbors exist, otherwise scanning radio CCA.
     */
    static double GetNonWifiFromScanData(const ChannelScanData& scanData);

    /**
     * \brief Update RL bounds for an AP (called when RL command is received)
     *
     * This sets the power and OBSS/PD bounds that RACEBOT will operate within.
     * Power bounds: [powerCenterDbm - margin, powerCenterDbm + margin]
     * OBSS/PD bounds: derived from power bounds using the relationship:
     *   obsspd = obsspdMin + txPowerRef - power
     *
     * \param nodeId AP node ID
     * \param powerCenterDbm Power center assigned by RL
     * \param channelWidthMhz Channel width assigned by RL (for future channel scoring)
     */
    void UpdateRlBounds(uint32_t nodeId, double powerCenterDbm, uint16_t channelWidthMhz = 80);

    /**
     * \brief Reset all AP states
     */
    void Reset();

    /**
     * \brief Get all AP states (for logging/debugging)
     */
    const std::map<uint32_t, ApPowerState>& GetAllApStates() const;

  private:
    /**
     * \brief Run RACEBOT algorithm for normal mode (Stage 3 only)
     * \param state AP power state (modified in place)
     */
    void RunRacebotAlgorithm(ApPowerState& state);

    /**
     * \brief Recalculate goal OBSS/PD threshold (Stages 1 & 2 - slow loop)
     * \param state AP power state (modified in place)
     */
    void RecalculateGoal(ApPowerState& state);

    /**
     * \brief Run non-WiFi interference mode algorithm
     * \param state AP power state (modified in place)
     */
    void RunNonWifiMode(ApPowerState& state);

    /**
     * \brief Determine reason for power change
     */
    std::string DeterminePowerChangeReason(
        const ApPowerState& state,
        double prevPower,
        bool wasInNonWifiMode);

    PowerScoringConfig m_config;                    // Algorithm configuration
    std::map<uint32_t, ApPowerState> m_apStates;    // Per-AP state
};

}  // namespace ns3

#endif  // POWER_SCORING_HELPER_H
