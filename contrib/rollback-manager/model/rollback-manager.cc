/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * ROLLBACK MANAGER IMPLEMENTATION
 * ===============================
 *
 * This file implements the rollback functionality. It is COMPLETELY ISOLATED
 * and has NO dependencies on power-scoring, channel-scoring, or any other
 * contrib modules.
 *
 * HOW TO INTEGRATE WITH YOUR SIMULATION:
 * --------------------------------------
 * See the example file (examples/rollback-example.cc) for complete integration.
 *
 * The key integration points are marked with "INTEGRATION POINT" comments below.
 */

#include "rollback-manager.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace ns3
{

// =============================================================================
// CONSTRUCTORS
// =============================================================================

RollbackManager::RollbackManager()
    : m_config()
{
}

RollbackManager::RollbackManager(const RollbackConfig& config)
    : m_config(config)
{
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void
RollbackManager::SetConfig(const RollbackConfig& config)
{
    m_config = config;
}

RollbackConfig
RollbackManager::GetConfig() const
{
    return m_config;
}

// =============================================================================
// CALLBACK REGISTRATION
// =============================================================================
//
// INTEGRATION POINT:
// ------------------
// You MUST register a callback that actually applies the rollback.
// This is YOUR code that modifies the PHY/MAC state.
//
// Example in your simulation:
//
//   g_rollbackManager.SetRollbackCallback(
//       [](uint32_t nodeId, const NetworkState& state) -> bool {
//           // Find the AP
//           auto apIt = g_apNetDevices.find(nodeId);
//           if (apIt == g_apNetDevices.end()) return false;
//
//           Ptr<WifiNetDevice> device = apIt->second;
//           Ptr<WifiPhy> phy = device->GetPhy();
//
//           // Restore TX power
//           phy->SetTxPowerStart(dBm_u{state.txPowerDbm});
//           phy->SetTxPowerEnd(dBm_u{state.txPowerDbm});
//
//           // Update your metrics tracking
//           g_apMetrics[nodeId].txPowerDbm = state.txPowerDbm;
//
//           // Optionally update power-scoring state
//           // g_powerScoringHelper.UpdateRlBounds(nodeId, state.txPowerDbm, ...);
//
//           return true;
//       }
//   );
//
void
RollbackManager::SetRollbackCallback(RollbackCallback callback)
{
    m_rollbackCallback = callback;
}

// =============================================================================
// STATE SAVING
// =============================================================================
//
// INTEGRATION POINT:
// ------------------
// Call SaveState() BEFORE applying any power or channel change.
//
// In your power-scoring code (e.g., when RACEBOT calculates new power):
//
//   PowerResult result = g_powerScoringHelper.CalculatePower(nodeId, nonWifi);
//   if (result.powerChanged) {
//       // SAVE STATE BEFORE APPLYING
//       NetworkState state;
//       state.nodeId = nodeId;
//       state.txPowerDbm = prevPower;  // Current power BEFORE change
//       state.networkScore = CalculateNetworkScore(nodeId);
//       state.changeSource = "RACEBOT";
//       state.changeReason = result.reason;
//       g_rollbackManager.SaveState(nodeId, state);
//
//       // NOW apply the change
//       phy->SetTxPowerStart(dBm_u{result.txPowerDbm});
//   }
//
// In your RL code (when Kafka message arrives with new power):
//
//   // SAVE STATE BEFORE APPLYING RL CHANGE
//   NetworkState state;
//   state.txPowerDbm = currentPower;
//   state.networkScore = CalculateNetworkScore(nodeId);
//   state.changeSource = "RL";
//   g_rollbackManager.SaveState(nodeId, state);
//
//   // NOW apply RL change
//   g_powerScoringHelper.UpdateRlBounds(nodeId, newPower, width);
//
void
RollbackManager::SaveState(uint32_t nodeId, const NetworkState& state)
{
    RecordStartTime();

    // Get or create history for this AP
    auto& history = m_stateHistory[nodeId];

    // Create copy with timestamp
    NetworkState stateWithTime = state;
    stateWithTime.nodeId = nodeId;
    stateWithTime.timestamp = Simulator::Now();

    // Insert at front (most recent first)
    history.insert(history.begin(), stateWithTime);

    // Trim to max history depth
    if (history.size() > m_config.maxHistoryDepth)
    {
        history.resize(m_config.maxHistoryDepth);
    }

    if (m_config.enableLogging)
    {
        std::ostringstream ss;
        ss << "[ROLLBACK] SaveState: AP " << nodeId
           << " power=" << state.txPowerDbm << "dBm"
           << " score=" << state.networkScore
           << " source=" << state.changeSource;
        Log(ss.str());
    }
}

void
RollbackManager::SaveState(uint32_t nodeId,
                           double txPowerDbm,
                           uint8_t channel,
                           double networkScore,
                           const std::string& source)
{
    NetworkState state;
    state.nodeId = nodeId;
    state.txPowerDbm = txPowerDbm;
    state.channel = channel;
    state.networkScore = networkScore;
    state.changeSource = source;
    SaveState(nodeId, state);
}

// =============================================================================
// EVALUATION AND ROLLBACK
// =============================================================================
//
// INTEGRATION POINT:
// ------------------
// Schedule this call AFTER applying a power/channel change.
// Wait for the evaluation period to let the network stabilize.
//
// Example integration:
//
//   // After applying power change in your simulation:
//   void OnPowerChanged(uint32_t nodeId, double oldPower, double newPower) {
//       // Schedule evaluation after the configured period
//       Time evalDelay = Seconds(g_rollbackConfig.evaluationPeriodSec);
//
//       Simulator::Schedule(evalDelay, [nodeId]() {
//           // Calculate YOUR network score metric
//           // This could be throughput, latency, or a composite
//           double currentScore = CalculateNetworkScoreForAp(nodeId);
//
//           // Evaluate and potentially rollback
//           RollbackResult result = g_rollbackManager.EvaluateAndRollback(
//               nodeId, currentScore);
//
//           if (result.rollbackTriggered) {
//               std::cout << "[ROLLBACK] Triggered for AP " << nodeId
//                         << ": score dropped " << result.scoreDropPercent << "%"
//                         << " (" << result.previousScore << " -> "
//                         << result.currentScore << ")" << std::endl;
//           }
//       });
//   }
//
// NETWORK SCORE CALCULATION:
// --------------------------
// You need to provide the network score. Here's an example:
//
//   double CalculateNetworkScoreForAp(uint32_t nodeId) {
//       auto& metrics = g_apMetrics[nodeId];
//
//       // Option 1: Pure throughput
//       return metrics.throughputMbps;
//
//       // Option 2: Composite score (throughput + channel utilization penalty)
//       double throughputScore = metrics.throughputMbps;
//       double utilizationPenalty = metrics.channelUtilization * 0.5;
//       return throughputScore - utilizationPenalty;
//
//       // Option 3: Per-STA average throughput
//       if (metrics.connections.empty()) return 0.0;
//       double totalThroughput = 0.0;
//       for (const auto& conn : metrics.connections) {
//           totalThroughput += conn.uplinkThroughputMbps + conn.downlinkThroughputMbps;
//       }
//       return totalThroughput / metrics.connections.size();
//   }
//
RollbackResult
RollbackManager::EvaluateAndRollback(uint32_t nodeId, double currentScore)
{
    RecordStartTime();
    RollbackResult result;
    result.currentScore = currentScore;

    m_stats.totalEvaluations++;

    // Check grace period
    if (IsInGracePeriod())
    {
        result.inGracePeriod = true;
        result.reason = "In startup grace period";
        if (m_config.enableLogging)
        {
            Log("[ROLLBACK] Skipping evaluation - in grace period");
        }
        return result;
    }

    // Check if we have saved state
    if (!HasSavedState(nodeId))
    {
        result.reason = "No saved state for this AP";
        return result;
    }

    // Check cooldown
    if (IsInCooldown(nodeId))
    {
        result.inCooldown = true;
        result.reason = "In rollback cooldown";
        m_stats.rollbacksBlocked++;
        if (m_config.enableLogging)
        {
            Time remaining = GetCooldownRemaining(nodeId);
            std::ostringstream ss;
            ss << "[ROLLBACK] AP " << nodeId << " in cooldown ("
               << remaining.GetSeconds() << "s remaining)";
            Log(ss.str());
        }
        return result;
    }

    // Get the saved state (before the change)
    NetworkState savedState = GetLastSavedState(nodeId);
    result.previousScore = savedState.networkScore;

    // Calculate score drop percentage
    // Formula: ((old - new) / old) * 100
    // If old score is 0, use absolute comparison
    if (savedState.networkScore > 0.0)
    {
        double scoreDrop = savedState.networkScore - currentScore;
        result.scoreDropPercent = (scoreDrop / savedState.networkScore) * 100.0;
    }
    else
    {
        result.scoreDropPercent = (currentScore < m_config.minAbsoluteScore) ? 100.0 : 0.0;
    }

    // Update max drop stat
    if (result.scoreDropPercent > m_stats.maxScoreDropPercent)
    {
        m_stats.maxScoreDropPercent = result.scoreDropPercent;
    }

    // Determine if rollback is needed
    // Rollback if:
    //   1. Score dropped more than (100 - threshold)% OR
    //   2. Current score is below minimum absolute threshold
    //
    // With threshold=10%, rollback if score is now less than 10% of original
    // i.e., dropped more than 90%
    double dropThreshold = 100.0 - m_config.rollbackThresholdPercent;
    bool scoreDropTrigger = result.scoreDropPercent > dropThreshold;
    bool absoluteTrigger = (m_config.minAbsoluteScore > 0.0) &&
                           (currentScore < m_config.minAbsoluteScore);

    if (m_config.enableLogging)
    {
        std::ostringstream ss;
        ss << "[ROLLBACK] Evaluate AP " << nodeId
           << ": score " << savedState.networkScore << " -> " << currentScore
           << " (drop=" << result.scoreDropPercent << "%, threshold=" << dropThreshold << "%)";
        Log(ss.str());
    }

    if (scoreDropTrigger || absoluteTrigger)
    {
        // ROLLBACK NEEDED!
        result.rollbackTriggered = true;
        result.restoredState = savedState;

        std::ostringstream reasonSs;
        if (scoreDropTrigger)
        {
            reasonSs << "Score dropped " << result.scoreDropPercent << "% (threshold: "
                     << dropThreshold << "%)";
        }
        else
        {
            reasonSs << "Score below absolute minimum (" << currentScore
                     << " < " << m_config.minAbsoluteScore << ")";
        }
        result.reason = reasonSs.str();

        // Execute rollback callback if registered
        if (m_rollbackCallback)
        {
            if (m_config.enableLogging)
            {
                std::ostringstream ss;
                ss << "[ROLLBACK] TRIGGERING rollback for AP " << nodeId
                   << ": restoring power=" << savedState.txPowerDbm << "dBm"
                   << ", reason: " << result.reason;
                Log(ss.str());
            }

            result.rollbackSucceeded = m_rollbackCallback(nodeId, savedState);

            if (result.rollbackSucceeded)
            {
                // Update cooldown
                m_lastRollbackTime[nodeId] = Simulator::Now();
                m_stats.rollbacksTriggered++;
                m_stats.lastRollbackTime = Simulator::Now();
                m_stats.lastRollbackNodeId = nodeId;

                // Remove the saved state since we've rolled back
                // (Keep history for potential multi-level rollback)
                if (!m_stateHistory[nodeId].empty())
                {
                    m_stateHistory[nodeId].erase(m_stateHistory[nodeId].begin());
                }
            }
            else
            {
                if (m_config.enableLogging)
                {
                    Log("[ROLLBACK] Callback returned false - rollback may have failed");
                }
            }
        }
        else
        {
            result.reason += " (no callback registered!)";
            if (m_config.enableLogging)
            {
                Log("[ROLLBACK] WARNING: Rollback needed but no callback registered!");
            }
        }
    }
    else
    {
        result.reason = "Score within acceptable range";
    }

    UpdateStats(result);
    return result;
}

// =============================================================================
// FORCE ROLLBACK
// =============================================================================
//
// Use this for manual rollback when you detect problems outside the normal
// evaluation flow. For example, if you detect a catastrophic failure.
//
bool
RollbackManager::ForceRollback(uint32_t nodeId)
{
    if (!HasSavedState(nodeId))
    {
        if (m_config.enableLogging)
        {
            std::ostringstream ss;
            ss << "[ROLLBACK] ForceRollback: no saved state for AP " << nodeId;
            Log(ss.str());
        }
        return false;
    }

    NetworkState savedState = GetLastSavedState(nodeId);

    if (m_rollbackCallback)
    {
        if (m_config.enableLogging)
        {
            std::ostringstream ss;
            ss << "[ROLLBACK] ForceRollback: AP " << nodeId
               << " restoring power=" << savedState.txPowerDbm << "dBm";
            Log(ss.str());
        }

        bool success = m_rollbackCallback(nodeId, savedState);

        if (success)
        {
            m_lastRollbackTime[nodeId] = Simulator::Now();
            m_stats.rollbacksTriggered++;
            m_stats.lastRollbackTime = Simulator::Now();
            m_stats.lastRollbackNodeId = nodeId;

            if (!m_stateHistory[nodeId].empty())
            {
                m_stateHistory[nodeId].erase(m_stateHistory[nodeId].begin());
            }
        }

        return success;
    }

    return false;
}

// =============================================================================
// STATE QUERIES
// =============================================================================

bool
RollbackManager::HasSavedState(uint32_t nodeId) const
{
    auto it = m_stateHistory.find(nodeId);
    return (it != m_stateHistory.end()) && (!it->second.empty());
}

NetworkState
RollbackManager::GetLastSavedState(uint32_t nodeId) const
{
    auto it = m_stateHistory.find(nodeId);
    if (it != m_stateHistory.end() && !it->second.empty())
    {
        return it->second.front();
    }
    return NetworkState{};
}

std::vector<NetworkState>
RollbackManager::GetStateHistory(uint32_t nodeId) const
{
    auto it = m_stateHistory.find(nodeId);
    if (it != m_stateHistory.end())
    {
        return it->second;
    }
    return {};
}

bool
RollbackManager::IsInCooldown(uint32_t nodeId) const
{
    auto it = m_lastRollbackTime.find(nodeId);
    if (it == m_lastRollbackTime.end())
    {
        return false;
    }

    Time elapsed = Simulator::Now() - it->second;
    return elapsed.GetSeconds() < m_config.rollbackCooldownSec;
}

Time
RollbackManager::GetCooldownRemaining(uint32_t nodeId) const
{
    auto it = m_lastRollbackTime.find(nodeId);
    if (it == m_lastRollbackTime.end())
    {
        return Seconds(0);
    }

    Time elapsed = Simulator::Now() - it->second;
    double remaining = m_config.rollbackCooldownSec - elapsed.GetSeconds();
    return Seconds(std::max(0.0, remaining));
}

// =============================================================================
// STATISTICS
// =============================================================================

RollbackStats
RollbackManager::GetStats() const
{
    return m_stats;
}

void
RollbackManager::ResetStats()
{
    m_stats = RollbackStats{};
}

// =============================================================================
// CLEANUP
// =============================================================================

void
RollbackManager::Reset()
{
    m_stateHistory.clear();
    m_lastRollbackTime.clear();
    m_stats = RollbackStats{};
    m_startTimeRecorded = false;
}

void
RollbackManager::ClearStateForAp(uint32_t nodeId)
{
    m_stateHistory.erase(nodeId);
    m_lastRollbackTime.erase(nodeId);
}

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

void
RollbackManager::RecordStartTime()
{
    if (!m_startTimeRecorded)
    {
        m_simulationStartTime = Simulator::Now();
        m_startTimeRecorded = true;
    }
}

bool
RollbackManager::IsInGracePeriod() const
{
    if (!m_startTimeRecorded)
    {
        return true;
    }

    Time elapsed = Simulator::Now() - m_simulationStartTime;
    return elapsed.GetSeconds() < m_config.startupGracePeriodSec;
}

void
RollbackManager::UpdateStats(const RollbackResult& result)
{
    // Stats already updated in EvaluateAndRollback
}

void
RollbackManager::Log(const std::string& message) const
{
    std::cout << "[" << Simulator::Now().GetSeconds() << "s] " << message << std::endl;
}

}  // namespace ns3
