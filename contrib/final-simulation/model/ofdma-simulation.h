/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * OFDMA Effects Simulation Module
 * Simulates the QoE benefits of 802.11ax OFDMA based on network conditions
 */

#ifndef OFDMA_SIMULATION_H
#define OFDMA_SIMULATION_H

#include "ns3/kafka-producer.h"  // For ApMetrics, ConnectionMetrics
#include "ns3/log.h"

#include <map>
#include <cmath>

namespace ns3
{

/**
 * @brief OFDMA Configuration - controls benefit simulation parameters
 */
struct OfdmaConfig
{
    bool enabled = false;
    uint8_t minStasForBenefit = 2;     // Min STAs per AP to enable OFDMA benefit
    uint8_t saturationStaCount = 9;    // STAs at which benefit saturates (max RU split)

    struct
    {
        double throughput = 0.35;      // At BF=1: 35% throughput increase
        double latency = 0.45;         // At BF=1: 45% latency reduction
        double jitter = 0.50;          // At BF=1: 50% jitter reduction
        double packetLoss = 0.20;      // At BF=1: 20% packet loss reduction
        double channelUtil = 0.25;     // At BF=1: 25% channel util improvement
    } improvement;
};

/**
 * @ingroup final-simulation
 * @brief OFDMA Effects Simulation module
 *
 * This class provides functionality to simulate the QoE benefits of 802.11ax
 * OFDMA without implementing full PHY/MAC OFDMA. It calculates a benefit factor
 * based on:
 * - Number of associated STAs per AP (more STAs = more OFDMA benefit)
 *
 * The benefit factor is then applied to IMPROVE metrics:
 * - Throughput (multiplicative increase)
 * - Latency/Jitter (multiplicative reduction)
 * - Packet loss (multiplicative reduction)
 * - Channel utilization (multiplicative reduction - more efficient use)
 *
 * This follows the inverse pattern of the ACI simulation - applying improvements
 * instead of degradations to model the aggregate effects of OFDMA scheduling.
 *
 * Based on published research:
 * - Magrin et al. 2023 IEEE TWC: OFDMA throughput/latency validation
 * - Typical gains: throughput +20-50%, latency -30-60%
 */
class OfdmaSimulation
{
  public:
    /**
     * @brief Set the OFDMA configuration
     * @param config OFDMA configuration parameters
     */
    static void SetConfig(const OfdmaConfig& config);

    /**
     * @brief Get the current OFDMA configuration
     * @return Current OFDMA configuration
     */
    static const OfdmaConfig& GetConfig();

    /**
     * @brief Check if OFDMA simulation is enabled
     * @return true if enabled, false otherwise
     */
    static bool IsEnabled();

    /**
     * @brief Calculate OFDMA Benefit Factor based on STA count
     *
     * Uses logarithmic scaling with diminishing returns:
     * - BF = 0 at 1 STA (no OFDMA possible)
     * - BF ~0.35 at 2 STAs (minimal benefit)
     * - BF ~0.65 at 4 STAs (moderate benefit)
     * - BF ~0.85 at 6 STAs (strong benefit)
     * - BF = 1.0 at saturationStaCount+ STAs (maximum benefit)
     *
     * @param staCount Number of associated STAs
     * @return Benefit factor in range [0.0, 1.0]
     */
    static double CalculateBenefitFactor(uint32_t staCount);

    /**
     * @brief Apply OFDMA effects to all AP metrics
     *
     * Calculates benefit factor for each AP based on STA count and applies
     * improvements to both AP metrics and connection metrics.
     *
     * @param apMetrics Reference to AP metrics map to modify
     */
    static void Apply(std::map<uint32_t, ApMetrics>& apMetrics);

  private:
    /**
     * @brief Apply OFDMA improvements to connection metrics
     * @param conn Connection metrics to modify
     * @param BF Benefit factor [0.0, 1.0]
     */
    static void ApplyToConnectionMetrics(ConnectionMetrics& conn, double BF);

    /**
     * @brief Apply OFDMA improvements to AP metrics
     * @param ap AP metrics to modify
     * @param BF Benefit factor [0.0, 1.0]
     */
    static void ApplyToApMetrics(ApMetrics& ap, double BF);

    // Static member variables
    static OfdmaConfig s_config;
};

} // namespace ns3

#endif // OFDMA_SIMULATION_H
