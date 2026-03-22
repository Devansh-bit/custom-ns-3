/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Adjacent Channel Interference (ACI) Simulation Module
 * Simulates interference effects between overlapping WiFi channels
 */

#ifndef ACI_SIMULATION_H
#define ACI_SIMULATION_H

#include "ns3/wifi-phy-band.h"
#include "ns3/vector.h"
#include "ns3/kafka-producer.h"  // For ApMetrics, ConnectionMetrics
#include "ns3/simulation-config-parser.h"  // For ApConfigData
#include "ns3/log.h"

#include <map>
#include <unordered_map>
#include <vector>
#include <cmath>

namespace ns3
{

/**
 * @brief ACI Configuration - controls interference simulation parameters
 */
struct AciConfig
{
    bool enabled = false;
    double pathLossExponent = 3.0;
    double maxInterferenceDistanceM = 50.0;
    double clientWeightFactor = 0.1;  // Weight per associated client

    struct
    {
        double throughput = 0.3;   // At IF=1: 30% throughput reduction
        double packetLoss = 5.0;   // At IF=1: +5% packet loss
        double latency = 0.5;      // At IF=1: 50% latency increase
        double jitter = 0.4;       // At IF=1: 40% jitter increase
        double channelUtil = 0.15; // At IF=1: +15% utilization
    } degradation;
};

/**
 * @brief Per-AP ACI info cache for fast interference calculation
 */
struct ApAciInfo
{
    uint32_t nodeId;
    Vector position;
    double txPowerDbm;
    uint8_t channel;            // Channel number encodes width (36=20MHz, 42=80MHz, etc.)
    WifiPhyBand band;           // 2.4 GHz or 5 GHz
    uint32_t associatedClients;
    int64_t gridCell;           // Spatial index cell
};

/**
 * @brief Spatial grid cell for O(k) neighbor lookup instead of O(N²)
 */
struct AciGridCell
{
    std::vector<uint32_t> apIds;
};

/**
 * @brief Channel info structure for frequency-based overlap calculation
 */
struct AciChannelInfo
{
    uint16_t centerFreqMHz;
    uint16_t widthMHz;
};

/**
 * @ingroup final-simulation
 * @brief Adjacent Channel Interference (ACI) simulation module
 *
 * This class provides functionality to simulate adjacent channel interference
 * effects in WiFi networks. It calculates interference factors based on:
 * - Channel overlap (frequency-based, asymmetric)
 * - Distance between APs (log-distance path loss)
 * - TX power levels
 * - Number of associated clients
 *
 * The interference factor is then applied to degrade metrics:
 * - Throughput (multiplicative reduction)
 * - Packet loss (additive increase)
 * - Latency/Jitter (multiplicative increase)
 * - Channel utilization (additive increase)
 */
class AciSimulation
{
  public:
    /**
     * @brief Set the ACI configuration
     * @param config ACI configuration parameters
     */
    static void SetConfig(const AciConfig& config);

    /**
     * @brief Get the current ACI configuration
     * @return Current ACI configuration
     */
    static const AciConfig& GetConfig();

    /**
     * @brief Initialize ACI simulation from AP configuration
     * @param aps Vector of AP configuration data
     * @param apMetrics Reference to AP metrics map (for initial band/client info)
     */
    static void Initialize(const std::vector<ApConfigData>& aps,
                           const std::map<uint32_t, ApMetrics>& apMetrics);

    /**
     * @brief Apply ACI effects to all AP metrics
     *
     * Calculates interference factor for each AP and applies degradation
     * to both AP metrics and connection metrics.
     *
     * @param apMetrics Reference to AP metrics map to modify
     */
    static void Apply(std::map<uint32_t, ApMetrics>& apMetrics);

    /**
     * @brief Notify ACI of a channel change
     *
     * Call this when an AP changes channel to invalidate the cache.
     *
     * @param nodeId Node ID of the AP that changed channel
     * @param newChannel New channel number
     */
    static void OnChannelChanged(uint32_t nodeId, uint8_t newChannel);

    /**
     * @brief Check if ACI simulation is enabled
     * @return true if enabled, false otherwise
     */
    static bool IsEnabled();

    /**
     * @brief Calculate interference factor for a specific AP
     *
     * Public for debugging/testing purposes.
     *
     * @param targetApId Node ID of the target AP
     * @return Interference factor in range [0.0, 2.0]
     */
    static double CalculateInterferenceFactor(uint32_t targetApId);

  private:
    // Internal helper functions
    static AciChannelInfo GetChannelInfo(uint8_t channel, WifiPhyBand band);
    static double GetChannelOverlap(uint8_t ch1, uint8_t ch2, WifiPhyBand band);
    static int64_t GetGridCellHash(double x, double y);
    static std::vector<int64_t> GetNeighborCells(int64_t centerCell);
    static void UpdateSpatialGrid();
    static double CalculateDistanceFactor(double distance);
    static double CalculateTxPowerFactor(double txPowerDbm);
    static double CalculateClientWeightFactor(uint32_t clientCount);
    static void ApplyToConnectionMetrics(ConnectionMetrics& conn, double IF);
    static void ApplyToApMetrics(ApMetrics& ap, double IF);
    static void UpdateCache(const std::map<uint32_t, ApMetrics>& apMetrics);

    // Static member variables
    static AciConfig s_config;
    static std::map<uint32_t, ApAciInfo> s_apCache;
    static std::unordered_map<int64_t, AciGridCell> s_spatialGrid;
    static bool s_cacheDirty;

    // Grid cell size for spatial indexing (meters)
    static constexpr double GRID_CELL_SIZE = 15.0;
};

} // namespace ns3

#endif // ACI_SIMULATION_H
