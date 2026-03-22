//
// Copyright (c) 2024
//
// SPDX-License-Identifier: GPL-2.0-only
//

#include "adaptive-udp-helper.h"

#include "ns3/adaptive-udp-application.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

namespace ns3
{

AdaptiveUdpHelper::AdaptiveUdpHelper(const std::string& protocol, const Address& address)
    : ApplicationHelper("ns3::AdaptiveUdpApplication")
{
    m_factory.Set("Protocol", StringValue(protocol));
    m_factory.Set("Remote", AddressValue(address));
}

void
AdaptiveUdpHelper::SetInitialRate(DataRate dataRate, uint32_t packetSize)
{
    m_factory.Set("InitialDataRate", DataRateValue(dataRate));
    m_factory.Set("PacketSize", UintegerValue(packetSize));
}

void
AdaptiveUdpHelper::SetRateLimits(DataRate minRate, DataRate maxRate)
{
    m_factory.Set("MinDataRate", DataRateValue(minRate));
    m_factory.Set("MaxDataRate", DataRateValue(maxRate));
}

void
AdaptiveUdpHelper::SetAimdParameters(double backoffMultiplier,
                                     uint64_t additiveIncrease,
                                     uint32_t successThreshold)
{
    m_factory.Set("BackoffMultiplier", DoubleValue(backoffMultiplier));
    m_factory.Set("AdditiveIncrease", UintegerValue(additiveIncrease));
    m_factory.Set("SuccessThreshold", UintegerValue(successThreshold));
}

} // namespace ns3
