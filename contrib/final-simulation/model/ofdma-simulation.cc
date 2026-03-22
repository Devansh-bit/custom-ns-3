/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * OFDMA Effects Simulation Module
 */

#include "ofdma-simulation.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OfdmaSimulation");

// Static member initialization
OfdmaConfig OfdmaSimulation::s_config;

void
OfdmaSimulation::SetConfig(const OfdmaConfig& config)
{
    s_config = config;

    if (s_config.enabled)
    {
        NS_LOG_INFO("[OFDMA] OFDMA Effects Simulation ENABLED");
        NS_LOG_INFO("[OFDMA] Config: minSTAs=" << +s_config.minStasForBenefit
                    << ", saturationSTAs=" << +s_config.saturationStaCount);
        NS_LOG_INFO("[OFDMA] Improvements at BF=1: throughput=+"
                    << (s_config.improvement.throughput * 100) << "%, latency=-"
                    << (s_config.improvement.latency * 100) << "%, jitter=-"
                    << (s_config.improvement.jitter * 100) << "%");
    }
    else
    {
        NS_LOG_INFO("[OFDMA] OFDMA Effects Simulation DISABLED");
    }
}

const OfdmaConfig&
OfdmaSimulation::GetConfig()
{
    return s_config;
}

bool
OfdmaSimulation::IsEnabled()
{
    return s_config.enabled;
}

double
OfdmaSimulation::CalculateBenefitFactor(uint32_t staCount)
{
    // No benefit with fewer than minStasForBenefit STAs
    if (staCount < s_config.minStasForBenefit)
    {
        return 0.0;
    }

    // Cap at saturation count
    uint32_t effectiveStaCount = std::min(staCount, (uint32_t)s_config.saturationStaCount);

    // Normalize to [0, 1] range
    // At minStasForBenefit: normalized = 1/(sat-1)
    // At saturationStaCount: normalized = 1.0
    double normalized = static_cast<double>(effectiveStaCount - 1) /
                       static_cast<double>(s_config.saturationStaCount - 1);

    // Logarithmic curve for diminishing returns
    // log(1 + x*(e-1)) gives:
    //   x=0: log(1) = 0
    //   x=1: log(e) = 1
    // This provides a nice diminishing returns curve
    double benefitFactor = std::log(1.0 + normalized * (M_E - 1.0));

    return std::min(1.0, benefitFactor);
}

void
OfdmaSimulation::ApplyToConnectionMetrics(ConnectionMetrics& conn, double BF)
{
    if (BF <= 0.0)
    {
        return;
    }

    const auto& i = s_config.improvement;

    // 1. Throughput: multiplicative INCREASE
    double throughputMult = 1.0 + (i.throughput * BF);
    conn.uplinkThroughputMbps *= throughputMult;
    conn.downlinkThroughputMbps *= throughputMult;

    // 2. Latency: multiplicative REDUCTION (minimum 10% of original)
    double latencyMult = std::max(0.1, 1.0 - (i.latency * BF));
    conn.meanRTTLatency *= latencyMult;

    // 3. Jitter: multiplicative REDUCTION (minimum 10% of original)
    double jitterMult = std::max(0.1, 1.0 - (i.jitter * BF));
    conn.jitterMs *= jitterMult;

    // 4. Packet loss: multiplicative REDUCTION (can go to 0)
    double pktLossMult = std::max(0.0, 1.0 - (i.packetLoss * BF));
    conn.packetLossRate *= pktLossMult;

    // 5. MAC retry rate: multiplicative REDUCTION (OFDMA = fewer collisions)
    double retryMult = std::max(0.0, 1.0 - (i.packetLoss * BF));
    conn.MACRetryRate *= retryMult;
}

void
OfdmaSimulation::ApplyToApMetrics(ApMetrics& ap, double BF)
{
    if (BF <= 0.0)
    {
        return;
    }

    // Channel utilization: multiplicative REDUCTION (more efficient spectrum use)
    // OFDMA allows parallel transmission, reducing wasted airtime
    // Apply same multiplier to all 3 values to maintain consistency: total = wifi + non_wifi
    double utilMult = std::max(0.1, 1.0 - (s_config.improvement.channelUtil * BF));
    ap.channelUtilization *= utilMult;
    ap.wifiChannelUtilization *= utilMult;
    ap.nonWifiChannelUtilization *= utilMult;

    // Recalculate aggregate throughput from connections
    double totalThroughput = 0.0;
    for (const auto& [connId, conn] : ap.connectionMetrics)
    {
        totalThroughput += conn.uplinkThroughputMbps + conn.downlinkThroughputMbps;
    }
    ap.throughputMbps = totalThroughput;
}

void
OfdmaSimulation::Apply(std::map<uint32_t, ApMetrics>& apMetrics)
{
    if (!s_config.enabled)
    {
        return;
    }

    NS_LOG_DEBUG("[OFDMA] Applying OFDMA effects to " << apMetrics.size() << " APs");

    // Calculate and apply OFDMA benefit for each AP
    for (auto& [nodeId, ap] : apMetrics)
    {
        uint32_t staCount = ap.associatedClients;
        double BF = CalculateBenefitFactor(staCount);

        if (BF > 0.01)
        {
            NS_LOG_DEBUG("[OFDMA] AP" << nodeId << " (STAs=" << staCount << "): BF=" << BF);

            // Apply to connection metrics
            for (auto& [connId, conn] : ap.connectionMetrics)
            {
                ApplyToConnectionMetrics(conn, BF);
            }

            // Apply to AP metrics
            ApplyToApMetrics(ap, BF);
        }
    }
}

} // namespace ns3
