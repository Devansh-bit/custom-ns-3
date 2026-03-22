/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Power Scoring Helper Implementation (RACEBOT-based algorithm)
 */

#include "power-scoring-helper.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace ns3
{

// ============================================================================
// ObssTracker Implementation
// ============================================================================

void
ObssTracker::UpdateRssiCount(int rssi, double alpha, bool isObss)
{
    frameCount++;
    if (isObss)
    {
        obssFrameCount++;
    }

    if (rawCounts.find(rssi) == rawCounts.end())
    {
        rawCounts[rssi] = 1;
        rssiCounts[rssi] = 1.0;
    }
    else
    {
        rawCounts[rssi]++;
        rssiCounts[rssi] = alpha * rawCounts[rssi] + (1 - alpha) * rssiCounts[rssi];
    }
}

void
ObssTracker::UpdateBssRssi(double rssi, double alpha)
{
    bssRssiEwma = alpha * rssi + (1 - alpha) * bssRssiEwma;
}

void
ObssTracker::UpdateMcs(double mcs, double alpha)
{
    prevMcsEwma = mcsEwma;
    mcsEwma = alpha * mcs + (1 - alpha) * mcsEwma;
}

void
ObssTracker::Reset()
{
    rawCounts.clear();
    rssiCounts.clear();
    frameCount = 0;
    obssFrameCount = 0;
    bssRssiEwma = -70.0;
    mcsEwma = 7.0;
    prevMcsEwma = 7.0;
}

double
ObssTracker::GetObssRatio() const
{
    return frameCount > 0 ? static_cast<double>(obssFrameCount) / frameCount : 0.0;
}

// ============================================================================
// PowerScoringHelper Implementation
// ============================================================================

PowerScoringHelper::PowerScoringHelper()
    : m_config()
{
}

PowerScoringHelper::PowerScoringHelper(const PowerScoringConfig& config)
    : m_config(config)
{
}

void
PowerScoringHelper::SetConfig(const PowerScoringConfig& config)
{
    m_config = config;
}

PowerScoringConfig
PowerScoringHelper::GetConfig() const
{
    return m_config;
}

ApPowerState&
PowerScoringHelper::GetOrCreateApState(uint32_t nodeId, uint8_t bssColor)
{
    auto it = m_apStates.find(nodeId);
    if (it == m_apStates.end())
    {
        ApPowerState state;
        state.nodeId = nodeId;
        state.myBssColor = bssColor;
        state.currentTxPowerDbm = m_config.txPowerRefDbm;
        state.currentObsspdDbm = m_config.obsspdMinDbm;
        state.goalObsspdDbm = m_config.obsspdMinDbm;
        m_apStates[nodeId] = state;
    }
    else if (it->second.myBssColor != bssColor && bssColor != 0)
    {
        // Update BSS Color if changed
        it->second.myBssColor = bssColor;
    }
    return m_apStates[nodeId];
}

void
PowerScoringHelper::ProcessHeSigA(uint32_t nodeId, double rssiDbm, uint8_t rxBssColor)
{
    auto it = m_apStates.find(nodeId);
    if (it == m_apStates.end())
    {
        // AP not initialized yet, skip
        return;
    }

    ApPowerState& state = it->second;

    // Determine if this is an OBSS frame using BSS Color (proper 802.11ax way)
    bool isObss = false;
    if (rxBssColor != 0 && state.myBssColor != 0)
    {
        isObss = (rxBssColor != state.myBssColor);
    }

    // Update RSSI histogram
    int rssiInt = static_cast<int>(std::round(rssiDbm));
    state.tracker.UpdateRssiCount(rssiInt, m_config.alpha, isObss);

    // Update BSS RSSI EWMA for intra-BSS frames
    if (!isObss)
    {
        state.tracker.UpdateBssRssi(rssiDbm, m_config.alpha);
    }
}

// DEPRECATED: Now using actual MCS from PhyRxPayloadBegin trace instead of simulating from SNR
// void
// PowerScoringHelper::UpdateMcsFromSnr(uint32_t nodeId, double snrDb)
// {
//     auto it = m_apStates.find(nodeId);
//     if (it == m_apStates.end())
//     {
//         return;
//     }
//
//     uint8_t mcs = SimulateMcsFromSnr(snrDb);
//     it->second.tracker.UpdateMcs(static_cast<double>(mcs), m_config.alpha);
// }

void
PowerScoringHelper::UpdateMcs(uint32_t nodeId, uint8_t mcs)
{
    auto it = m_apStates.find(nodeId);
    if (it == m_apStates.end())
    {
        return;
    }

    it->second.tracker.UpdateMcs(static_cast<double>(mcs), m_config.alpha);
}

// DEPRECATED: Now using actual MCS from PhyRxPayloadBegin trace
// uint8_t
// PowerScoringHelper::SimulateMcsFromSnr(double snrDb)
// {
//     // 802.11ax MCS to SNR mapping (approximate)
//     if (snrDb >= 35) return 11;
//     if (snrDb >= 32) return 10;
//     if (snrDb >= 29) return 9;
//     if (snrDb >= 26) return 8;
//     if (snrDb >= 23) return 7;
//     if (snrDb >= 20) return 6;
//     if (snrDb >= 17) return 5;
//     if (snrDb >= 14) return 4;
//     if (snrDb >= 11) return 3;
//     if (snrDb >= 8) return 2;
//     if (snrDb >= 5) return 1;
//     return 0;
// }

double
PowerScoringHelper::GetNonWifiFromScanData(const ChannelScanData& scanData)
{
    // If there are neighbors, use average BSS Load IE non-WiFi
    if (!scanData.neighbors.empty())
    {
        double totalNonWifi = 0.0;
        uint32_t count = 0;
        for (const auto& neighbor : scanData.neighbors)
        {
            if (neighbor.nonWifiUtil > 0.0)
            {
                totalNonWifi += neighbor.nonWifiUtil;
                count++;
            }
        }
        if (count > 0)
        {
            return totalNonWifi / count;
        }
    }

    // Fall back to scanning radio CCA measurement
    return scanData.nonWifiChannelUtilization;
}

PowerResult
PowerScoringHelper::CalculatePower(uint32_t nodeId, double nonWifiPercent)
{
    PowerResult result;
    result.nodeId = nodeId;

    auto it = m_apStates.find(nodeId);
    if (it == m_apStates.end())
    {
        result.txPowerDbm = m_config.txPowerRefDbm;
        result.obsspdLevelDbm = m_config.obsspdMinDbm;
        result.reason = "AP not initialized";
        return result;
    }

    ApPowerState& state = it->second;
    Time now = Simulator::Now();
    
    // Initialize timestamps on first run
    if (!state.initialized)
    {
        state.lastPowerChange = now;
        state.lastGoalRecalculation = now;
        state.initialized = true;
    }
    
    double prevPower = state.currentTxPowerDbm;
    bool wasInNonWifiMode = state.inNonWifiMode;

    // Update non-WiFi measurement
    state.lastNonWifiPercent = nonWifiPercent;

    // SLOW LOOP (t1): Recalculate goal every t1 seconds
    double timeSinceGoalRecalc = (now - state.lastGoalRecalculation).GetSeconds();
    bool goalRecalculated = false;
    if (timeSinceGoalRecalc >= m_config.t1IntervalSec)
    {
        RecalculateGoal(state);
        state.lastGoalRecalculation = now;
        goalRecalculated = true;
        timeSinceGoalRecalc = 0.0;  // Reset for time-to-next calculation
    }
    double timeToNextGoalRecalc = m_config.t1IntervalSec - timeSinceGoalRecalc;
    
    // Check mode transitions with hysteresis
    bool shouldEnterNonWifi = nonWifiPercent > m_config.nonWifiThresholdPercent;
    bool shouldExitNonWifi = nonWifiPercent < (m_config.nonWifiThresholdPercent - m_config.nonWifiHysteresis);

    if (!state.inNonWifiMode && shouldEnterNonWifi)
    {
        state.inNonWifiMode = true;
    }
    else if (state.inNonWifiMode && shouldExitNonWifi)
    {
        state.inNonWifiMode = false;
    }

    // FAST LOOP (t2): Power change cooldown
    double timeSincePowerChange = (now - state.lastPowerChange).GetSeconds();
    bool inCooldown = (timeSincePowerChange < m_config.t2IntervalSec);
    double timeRemainingT2 = inCooldown ? (m_config.t2IntervalSec - timeSincePowerChange) : 0.0;

    result.powerChanged = false;

    if (!inCooldown)
    {
        // Run appropriate algorithm
        if (state.inNonWifiMode)
        {
            RunNonWifiMode(state);
        }
        else
        {
            RunRacebotAlgorithm(state);  // Now only Stage 3
        }

        // Check if power actually changed
        double powerDelta = std::abs(state.currentTxPowerDbm - prevPower);
        if (powerDelta > 0.5)
        {
            state.lastPowerChange = now;
            result.powerChanged = true;
            timeRemainingT2 = m_config.t2IntervalSec;  // Reset t2 cooldown
        }
    }

    // Determine reason for current state
    if (inCooldown)
    {
        result.reason = "In t2 cooldown";
    }
    else if (result.reason.empty())
    {
        result.reason = DeterminePowerChangeReason(state, prevPower, wasInNonWifiMode);
    }

    // Build result with all timing info
    result.txPowerDbm = state.currentTxPowerDbm;
    result.obsspdLevelDbm = state.currentObsspdDbm;
    result.goalObsspdDbm = state.goalObsspdDbm;
    result.inNonWifiMode = state.inNonWifiMode;
    result.goalRecalculated = goalRecalculated;
    result.inT2Cooldown = inCooldown;
    result.timeToNextGoalRecalc = timeToNextGoalRecalc;
    result.timeRemainingT2Cooldown = timeRemainingT2;

    return result;
}

std::vector<PowerResult>
PowerScoringHelper::CalculateAllPowers(const std::map<uint32_t, double>& nonWifiData)
{
    std::vector<PowerResult> results;

    for (auto& [nodeId, state] : m_apStates)
    {
        double nonWifi = 0.0;
        auto it = nonWifiData.find(nodeId);
        if (it != nonWifiData.end())
        {
            nonWifi = it->second;
        }

        results.push_back(CalculatePower(nodeId, nonWifi));
    }

    return results;
}

void
PowerScoringHelper::RecalculateGoal(ApPowerState& state)
{
    // Determine effective OBSS/PD bounds: use RL bounds if set, otherwise use global config
    double effectiveObsspdMin = state.rlBoundsSet ? state.rlObsspdMinDbm : m_config.obsspdMinDbm;
    double effectiveObsspdMax = state.rlBoundsSet ? state.rlObsspdMaxDbm : m_config.obsspdMaxDbm;

    // Stages 1 & 2: Calculate goal OBSS/PD from observed frames (slow loop)
    double newGoal = effectiveObsspdMin;

    for (const auto& [rssi, count] : state.tracker.rssiCounts)
    {
        if (count > m_config.ofcThreshold && rssi > newGoal)
        {
            double candidate1 = rssi + m_config.margin;
            double candidate2 = state.tracker.bssRssiEwma - m_config.margin;
            newGoal = std::min(candidate1, candidate2);
        }
    }

    // Apply effective bounds to goal (RL bounds if set)
    newGoal = std::max(newGoal, effectiveObsspdMin);
    newGoal = std::min(newGoal, effectiveObsspdMax);
    state.goalObsspdDbm = newGoal;
}

void
PowerScoringHelper::RunRacebotAlgorithm(ApPowerState& state)
{
    // Determine effective bounds: use RL bounds if set, otherwise use global config
    double effectiveObsspdMin = state.rlBoundsSet ? state.rlObsspdMinDbm : m_config.obsspdMinDbm;
    double effectiveObsspdMax = state.rlBoundsSet ? state.rlObsspdMaxDbm : m_config.obsspdMaxDbm;
    double effectivePowerMin = state.rlBoundsSet ? state.rlPowerMinDbm : m_config.txPowerMinDbm;
    double effectivePowerMax = state.rlBoundsSet ? state.rlPowerMaxDbm : m_config.txPowerRefDbm;

    // Stage 3: Adjust current OBSS/PD toward goal (fast loop)
    bool mcsDecreased = state.tracker.prevMcsEwma * m_config.gamma > state.tracker.mcsEwma;
    bool mcsIncreased = state.tracker.mcsEwma > state.tracker.prevMcsEwma * 1.1;

    if (mcsDecreased)
    {
        // Link quality degraded - be more conservative
        double newObsspd = (state.currentObsspdDbm + state.tracker.bssRssiEwma - m_config.margin) / 2.0;
        state.currentObsspdDbm = std::max(newObsspd, effectiveObsspdMin);
        state.goalObsspdDbm = (state.currentObsspdDbm + state.goalObsspdDbm) / 2.0;
    }
    else if (mcsIncreased && state.currentObsspdDbm < state.goalObsspdDbm - 1.0)
    {
        // Link quality good - can be more aggressive
        double step = std::min(2.0, (state.goalObsspdDbm - state.currentObsspdDbm) / 2.0);
        state.currentObsspdDbm = std::min(state.currentObsspdDbm + step, effectiveObsspdMax);
    }
    else
    {
        // Gradual movement toward goal using small step (1% toward goal)
        double newObsspd = state.currentObsspdDbm - (0.01 * (state.currentObsspdDbm - state.goalObsspdDbm));
        state.currentObsspdDbm = std::min(newObsspd, effectiveObsspdMax);
    }

    // Clamp OBSS/PD to effective bounds
    state.currentObsspdDbm = std::max(state.currentObsspdDbm, effectiveObsspdMin);
    state.currentObsspdDbm = std::min(state.currentObsspdDbm, effectiveObsspdMax);

    // Step 3: Calculate TARGET TX power from OBSS/PD
    // Use global config for the formula (relationship between OBSS/PD and power)
    double targetTxPower = m_config.obsspdMinDbm + m_config.txPowerRefDbm - state.currentObsspdDbm;

    // Clamp to effective power bounds (RL bounds if set)
    targetTxPower = std::max(targetTxPower, effectivePowerMin);
    targetTxPower = std::min(targetTxPower, effectivePowerMax);

    // Only apply power change if difference is significant (> 0.5 dBm)
    double powerDiff = std::abs(targetTxPower - state.currentTxPowerDbm);
    if (powerDiff <= 0.5)
    {
        // Not significant - ignore this change
        return;
    }

    // Gradual TX power adjustment (max 1dBm per step) - correct RACEBOT behavior
    double powerStep = 1.0;  // 1 dBm max change per update
    if (targetTxPower > state.currentTxPowerDbm)
    {
        // Increase power gradually
        state.currentTxPowerDbm = std::min(state.currentTxPowerDbm + powerStep, targetTxPower);
    }
    else if (targetTxPower < state.currentTxPowerDbm)
    {
        // Decrease power gradually
        state.currentTxPowerDbm = std::max(state.currentTxPowerDbm - powerStep, targetTxPower);
    }

    // Final clamp to ensure we never exceed RL bounds
    state.currentTxPowerDbm = std::max(state.currentTxPowerDbm, effectivePowerMin);
    state.currentTxPowerDbm = std::min(state.currentTxPowerDbm, effectivePowerMax);
}

void
PowerScoringHelper::RunNonWifiMode(ApPowerState& state)
{
    // Determine effective power bounds: use RL bounds if set, otherwise use global config
    double effectivePowerMax = state.rlBoundsSet ? state.rlPowerMaxDbm : m_config.txPowerRefDbm;
    double effectiveObsspdMin = state.rlBoundsSet ? state.rlObsspdMinDbm : m_config.obsspdMinDbm;

    // Non-WiFi interference mode: increase power to overcome interference
    // Simplified: move toward max power (within RL bounds)
    double targetPower = effectivePowerMax;
    double step = 1.0;  // 1 dBm per update

    if (state.currentTxPowerDbm < targetPower)
    {
        state.currentTxPowerDbm = std::min(state.currentTxPowerDbm + step, targetPower);
    }

    // Update OBSS/PD to match (derived from power)
    state.currentObsspdDbm = m_config.obsspdMinDbm + m_config.txPowerRefDbm - state.currentTxPowerDbm;

    // Clamp OBSS/PD to effective bounds
    state.currentObsspdDbm = std::max(state.currentObsspdDbm, effectiveObsspdMin);
}

std::string
PowerScoringHelper::DeterminePowerChangeReason(
    const ApPowerState& state,
    double prevPower,
    bool wasInNonWifiMode)
{
    if (wasInNonWifiMode != state.inNonWifiMode)
    {
        if (state.inNonWifiMode)
        {
            return "Mode: RACEBOT -> NON_WIFI (non-WiFi=" +
                   std::to_string(static_cast<int>(state.lastNonWifiPercent)) + "%)";
        }
        else
        {
            return "Mode: NON_WIFI -> RACEBOT (non-WiFi=" +
                   std::to_string(static_cast<int>(state.lastNonWifiPercent)) + "%)";
        }
    }

    double delta = state.currentTxPowerDbm - prevPower;
    if (std::abs(delta) < 0.5)
    {
        return "No significant change";
    }

    if (state.inNonWifiMode)
    {
        return "NON_WIFI mode adjustment";
    }

    // RACEBOT mode reasons
    bool mcsDecreased = state.tracker.prevMcsEwma * m_config.gamma > state.tracker.mcsEwma;
    bool mcsIncreased = state.tracker.mcsEwma > state.tracker.prevMcsEwma * 1.1;

    if (mcsDecreased)
    {
        return "MCS decreased (" + std::to_string(state.tracker.prevMcsEwma) +
               " -> " + std::to_string(state.tracker.mcsEwma) + ")";
    }
    else if (mcsIncreased)
    {
        return "MCS increased, moving toward goal";
    }
    else
    {
        return "Gradual adjustment toward goal OBSS/PD";
    }
}

void
PowerScoringHelper::UpdateRlBounds(uint32_t nodeId, double powerCenterDbm, uint16_t channelWidthMhz)
{
    auto it = m_apStates.find(nodeId);
    if (it == m_apStates.end())
    {
        // AP not initialized yet - create state first
        ApPowerState state;
        state.nodeId = nodeId;
        state.currentTxPowerDbm = powerCenterDbm;
        m_apStates[nodeId] = state;
        it = m_apStates.find(nodeId);
    }

    ApPowerState& state = it->second;

    // Store RL-assigned values
    state.rlPowerCenterDbm = powerCenterDbm;
    state.rlChannelWidthMhz = channelWidthMhz;

    // Calculate power bounds: [center - margin, center + margin]
    // RL defines the operating point - RACEBOT operates within ±margin around it
    // Use absolute hardware limits (not config defaults) for safety
    double margin = m_config.rlPowerMarginDbm;
    double absolutePowerMin = 0.0;   // Hardware absolute minimum (0 dBm)
    double absolutePowerMax = 33.0;  // Hardware absolute maximum (33 dBm)

    state.rlPowerMinDbm = std::max(powerCenterDbm - margin, absolutePowerMin);
    state.rlPowerMaxDbm = std::min(powerCenterDbm + margin, absolutePowerMax);

    // Derive OBSS/PD bounds from power bounds using RACEBOT formula:
    // power = obsspdMin + txPowerRef - obsspd
    // => obsspd = obsspdMin + txPowerRef - power
    //
    // For power at max (rlPowerMaxDbm):
    //   obsspd_min = obsspdMin + txPowerRef - rlPowerMaxDbm
    // For power at min (rlPowerMinDbm):
    //   obsspd_max = obsspdMin + txPowerRef - rlPowerMinDbm
    //
    // Note: Use global obsspdMin and txPowerRef for the formula relationship,
    // but don't clamp the result - let RL define the full operating range
    state.rlObsspdMinDbm = m_config.obsspdMinDbm + m_config.txPowerRefDbm - state.rlPowerMaxDbm;
    state.rlObsspdMaxDbm = m_config.obsspdMinDbm + m_config.txPowerRefDbm - state.rlPowerMinDbm;

    // Also update current TX power to RL-assigned center
    state.currentTxPowerDbm = powerCenterDbm;

    // Mark that RL bounds are now active
    state.rlBoundsSet = true;

    std::cout << "[RL BOUNDS] AP " << nodeId << ": power=" << powerCenterDbm
              << "dBm, range=[" << state.rlPowerMinDbm << "," << state.rlPowerMaxDbm << "]"
              << ", obsspd=[" << state.rlObsspdMinDbm << "," << state.rlObsspdMaxDbm << "]"
              << ", width=" << channelWidthMhz << "MHz" << std::endl;
}

void
PowerScoringHelper::Reset()
{
    m_apStates.clear();
}

const std::map<uint32_t, ApPowerState>&
PowerScoringHelper::GetAllApStates() const
{
    return m_apStates;
}

}  // namespace ns3
