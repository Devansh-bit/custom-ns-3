//
// Copyright (c) 2024
//
// SPDX-License-Identifier: GPL-2.0-only
//

#ifndef ADAPTIVE_UDP_HELPER_H
#define ADAPTIVE_UDP_HELPER_H

#include "ns3/application-helper.h"
#include "ns3/data-rate.h"

#include <stdint.h>

namespace ns3
{

/**
 * @brief A helper to make it easier to instantiate AdaptiveUdpApplication on nodes.
 *
 * This helper creates UDP applications with TCP-like AIMD congestion control.
 */
class AdaptiveUdpHelper : public ApplicationHelper
{
  public:
    /**
     * Create an AdaptiveUdpHelper.
     *
     * @param protocol the name of the protocol to use (e.g., "ns3::UdpSocketFactory")
     * @param address the address of the remote node to send traffic to
     */
    AdaptiveUdpHelper(const std::string& protocol, const Address& address);

    /**
     * Set the initial data rate and packet size.
     *
     * @param dataRate initial sending rate
     * @param packetSize size of packets in bytes
     */
    void SetInitialRate(DataRate dataRate, uint32_t packetSize = 1400);

    /**
     * Set the rate limits for AIMD.
     *
     * @param minRate minimum rate floor
     * @param maxRate maximum rate ceiling
     */
    void SetRateLimits(DataRate minRate, DataRate maxRate);

    /**
     * Set the AIMD parameters.
     *
     * @param backoffMultiplier rate multiplier on failure (e.g., 0.5)
     * @param additiveIncrease rate increase per success window (bps)
     * @param successThreshold successful sends needed before rate increase
     */
    void SetAimdParameters(double backoffMultiplier,
                           uint64_t additiveIncrease,
                           uint32_t successThreshold);
};

} // namespace ns3

#endif /* ADAPTIVE_UDP_HELPER_H */
