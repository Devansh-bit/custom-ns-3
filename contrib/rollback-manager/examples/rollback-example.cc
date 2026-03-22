/*
 * ROLLBACK MANAGER - INTEGRATION EXAMPLE
 *
 * Shows how to integrate with basic-simulation.cc
 *
 * INTEGRATION STEPS FOR YOUR SIMULATION:
 * 1. #include "ns3/rollback-manager.h"
 * 2. Add global: RollbackManager g_rollbackManager;
 * 3. Configure in main(): g_rollbackManager.SetConfig(config);
 * 4. Register callback: g_rollbackManager.SetRollbackCallback(MyCallback);
 * 5. Before power change: g_rollbackManager.SaveState(nodeId, state);
 * 6. After change: Schedule EvaluateAndRollback()
 */

#include "ns3/rollback-manager.h"
#include "ns3/core-module.h"
#include "ns3/simulator.h"
#include <iostream>
#include <map>

using namespace ns3;

// Global rollback manager
RollbackManager g_rollbackManager;

// Simulated AP metrics
struct ApMetrics { double txPowerDbm = 21.0; double throughputMbps = 100.0; uint8_t channel = 36; };
std::map<uint32_t, ApMetrics> g_metrics;

// Calculate network score (YOUR implementation)
double CalculateNetworkScore(uint32_t nodeId) {
    return g_metrics[nodeId].throughputMbps;
}

// Rollback callback - restore power (YOUR implementation)
bool RollbackCallback(uint32_t nodeId, const NetworkState& state) {
    std::cout << "[CALLBACK] AP " << nodeId << " restored to " << state.txPowerDbm << " dBm\n";
    g_metrics[nodeId].txPowerDbm = state.txPowerDbm;
    return true;
}

// Evaluate after delay
void RunEvaluation(uint32_t nodeId) {
    double score = CalculateNetworkScore(nodeId);
    auto result = g_rollbackManager.EvaluateAndRollback(nodeId, score);
    if (result.rollbackTriggered) {
        std::cout << "[ROLLBACK] Triggered! Score dropped " << result.scoreDropPercent << "%\n";
    }
}

// Simulate power change with rollback integration
void ChangePower(uint32_t nodeId, double newPower, bool causeCatastrophe) {
    // SAVE STATE BEFORE CHANGE
    NetworkState state;
    state.txPowerDbm = g_metrics[nodeId].txPowerDbm;
    state.networkScore = CalculateNetworkScore(nodeId);
    state.changeSource = "TEST";
    g_rollbackManager.SaveState(nodeId, state);

    // Apply change
    g_metrics[nodeId].txPowerDbm = newPower;
    if (causeCatastrophe) g_metrics[nodeId].throughputMbps *= 0.05; // Drop to 5%

    // Schedule evaluation
    Simulator::Schedule(Seconds(2.0), &RunEvaluation, nodeId);
}

int main(int argc, char* argv[]) {
    // Configure
    RollbackConfig config;
    config.rollbackThresholdPercent = 10.0; // Rollback if <10% of previous
    config.evaluationPeriodSec = 2.0;
    config.startupGracePeriodSec = 1.0;
    g_rollbackManager.SetConfig(config);
    g_rollbackManager.SetRollbackCallback(RollbackCallback);

    // Init APs
    g_metrics[1] = {21.0, 100.0, 36};
    g_metrics[2] = {21.0, 100.0, 36};

    // Test: Normal change (no rollback)
    Simulator::Schedule(Seconds(2.0), []() {
        std::cout << "\n=== TEST 1: Normal change ===\n";
        ChangePower(1, 18.0, false);
    });

    // Test: Catastrophic change (triggers rollback)
    Simulator::Schedule(Seconds(6.0), []() {
        std::cout << "\n=== TEST 2: Catastrophic change ===\n";
        ChangePower(2, 5.0, true);
    });

    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "\nFinal: AP1=" << g_metrics[1].txPowerDbm << "dBm, AP2=" << g_metrics[2].txPowerDbm << "dBm\n";
    return 0;
}
