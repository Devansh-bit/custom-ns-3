/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * ROLLBACK MANAGER - Network State Rollback for Power/Channel Changes
 * ====================================================================
 *
 * PURPOSE: Automatic rollback when network score drops below 10% of previous
 *          after power/channel changes (via RL or RACEBOT).
 *
 * THIS MODULE IS COMPLETELY ISOLATED - NO dependencies on other contrib modules.
 *
 * INTEGRATION STEPS:
 * ------------------
 * 1. SaveState() BEFORE any power/channel change
 * 2. Apply your change (RL, RACEBOT, etc.)
 * 3. Schedule EvaluateAndRollback() after evaluation period
 * 4. If score dropped >90%, rollback callback is triggered
 */

#ifndef ROLLBACK_MANAGER_H
#define ROLLBACK_MANAGER_H

#include "ns3/nstime.h"
#include "ns3/simulator.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ns3
{

// =============================================================================
// CONFIGURATION - Adjust these for your simulation
// =============================================================================
struct RollbackConfig
{
    // Rollback if new_score < (old_score * threshold / 100)
    // Default 10% = rollback if score drops to less than 10% of previous
    double rollbackThresholdPercent = 10.0;

    // Wait time after change before evaluating (let network stabilize)
    double evaluationPeriodSec = 2.0;

    // Minimum time between rollbacks on same AP (prevent oscillation)
    double rollbackCooldownSec = 10.0;

    // How many previous states to keep per AP
    uint32_t maxHistoryDepth = 3;

    // Minimum absolute score (rollback if below this regardless of %)
    double minAbsoluteScore = 0.0;

    // Grace period after simulation start (no rollbacks during startup)
    double startupGracePeriodSec = 5.0;

    // Enable logging
    bool enableLogging = true;
};

// =============================================================================
// NETWORK STATE - What gets saved and restored
// =============================================================================
struct NetworkState
{
    uint32_t nodeId = 0;
    Time timestamp;

    // Power state - restore via phy->SetTxPowerStart/End()
    double txPowerDbm = 21.0;
    double obsspdDbm = -82.0;

    // Channel state - restore via phy->SetOperatingChannel()
    uint8_t channel = 36;
    uint16_t channelWidthMhz = 80;

    // Metrics at snapshot time (for comparison, not restored)
    double networkScore = 0.0;
    double throughputMbps = 0.0;
    uint32_t connectedStations = 0;

    // What triggered this state save
    std::string changeSource = "";  // "RL", "RACEBOT", "DFS"
    std::string changeReason = "";
};

// =============================================================================
// ROLLBACK RESULT - Returned by EvaluateAndRollback()
// =============================================================================
struct RollbackResult
{
    bool rollbackTriggered = false;
    bool rollbackSucceeded = false;
    bool inCooldown = false;
    bool inGracePeriod = false;

    double previousScore = 0.0;
    double currentScore = 0.0;
    double scoreDropPercent = 0.0;

    std::string reason = "";
    NetworkState restoredState;
};

// =============================================================================
// STATISTICS
// =============================================================================
struct RollbackStats
{
    uint32_t totalEvaluations = 0;
    uint32_t rollbacksTriggered = 0;
    uint32_t rollbacksBlocked = 0;
    double maxScoreDropPercent = 0.0;
    Time lastRollbackTime;
    uint32_t lastRollbackNodeId = 0;
};

// =============================================================================
// CALLBACK TYPE - You provide this to actually apply the rollback
// =============================================================================
//
// Example implementation in your simulation:
//
//   bool MyRollbackCallback(uint32_t nodeId, const NetworkState& state) {
//       Ptr<WifiPhy> phy = GetPhyForAp(nodeId);
//       phy->SetTxPowerStart(dBm_u{state.txPowerDbm});
//       phy->SetTxPowerEnd(dBm_u{state.txPowerDbm});
//       return true;
//   }
//
using RollbackCallback = std::function<bool(uint32_t nodeId, const NetworkState& state)>;

// =============================================================================
// ROLLBACK MANAGER CLASS
// =============================================================================
class RollbackManager
{
  public:
    RollbackManager();
    explicit RollbackManager(const RollbackConfig& config);

    // Configuration
    void SetConfig(const RollbackConfig& config);
    RollbackConfig GetConfig() const;

    // Register YOUR callback that applies the rollback
    void SetRollbackCallback(RollbackCallback callback);

    // -----------------------------------------------------------------
    // MAIN API - Call these from your simulation
    // -----------------------------------------------------------------

    /**
     * Save state BEFORE applying a power/channel change.
     *
     * INTEGRATION POINT (in your power-scoring or RL code):
     *
     *   // BEFORE applying change:
     *   NetworkState state;
     *   state.txPowerDbm = currentPower;
     *   state.networkScore = CalculateYourNetworkScore();
     *   state.changeSource = "RL";
     *   rollbackMgr.SaveState(nodeId, state);
     *
     *   // NOW apply the change:
     *   phy->SetTxPowerStart(newPower);
     */
    void SaveState(uint32_t nodeId, const NetworkState& state);

    // Convenience overload
    void SaveState(uint32_t nodeId,
                   double txPowerDbm,
                   uint8_t channel,
                   double networkScore,
                   const std::string& source = "");

    /**
     * Evaluate score and trigger rollback if needed.
     *
     * INTEGRATION POINT (schedule after change):
     *
     *   // After applying change, schedule evaluation:
     *   Simulator::Schedule(Seconds(2.0), [&]() {
     *       double newScore = CalculateYourNetworkScore();
     *       auto result = rollbackMgr.EvaluateAndRollback(nodeId, newScore);
     *       if (result.rollbackTriggered) {
     *           std::cout << "Rollback! " << result.reason << std::endl;
     *       }
     *   });
     */
    RollbackResult EvaluateAndRollback(uint32_t nodeId, double currentScore);

    // Force immediate rollback
    bool ForceRollback(uint32_t nodeId);

    // State queries
    bool HasSavedState(uint32_t nodeId) const;
    NetworkState GetLastSavedState(uint32_t nodeId) const;
    std::vector<NetworkState> GetStateHistory(uint32_t nodeId) const;
    bool IsInCooldown(uint32_t nodeId) const;
    Time GetCooldownRemaining(uint32_t nodeId) const;

    // Statistics
    RollbackStats GetStats() const;
    void ResetStats();

    // Cleanup
    void Reset();
    void ClearStateForAp(uint32_t nodeId);

  private:
    std::map<uint32_t, std::vector<NetworkState>> m_stateHistory;
    std::map<uint32_t, Time> m_lastRollbackTime;
    RollbackConfig m_config;
    RollbackCallback m_rollbackCallback;
    RollbackStats m_stats;
    Time m_simulationStartTime;
    bool m_startTimeRecorded = false;

    void RecordStartTime();
    bool IsInGracePeriod() const;
    void UpdateStats(const RollbackResult& result);
    void Log(const std::string& message) const;
};

}  // namespace ns3

#endif  // ROLLBACK_MANAGER_H
