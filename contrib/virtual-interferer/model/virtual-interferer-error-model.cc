/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Virtual Interferer Error Model - Implementation
 */

#include "virtual-interferer-error-model.h"

#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/packet.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("VirtualInterfererErrorModel");
NS_OBJECT_ENSURE_REGISTERED(VirtualInterfererErrorModel);

TypeId
VirtualInterfererErrorModel::GetTypeId()
{
    static TypeId tid = TypeId("ns3::VirtualInterfererErrorModel")
        .SetParent<ErrorModel>()
        .SetGroupName("Network")
        .AddConstructor<VirtualInterfererErrorModel>()
        .AddAttribute("PacketLossRate",
                      "Probability of dropping a packet due to interference",
                      DoubleValue(0.0),
                      MakeDoubleAccessor(&VirtualInterfererErrorModel::m_packetLossRate),
                      MakeDoubleChecker<double>(0.0, 1.0));
    return tid;
}

VirtualInterfererErrorModel::VirtualInterfererErrorModel()
    : m_packetLossRate(0.0),
      m_droppedPackets(0),
      m_totalPackets(0)
{
    NS_LOG_FUNCTION(this);
    m_random = CreateObject<UniformRandomVariable>();
    m_random->SetAttribute("Min", DoubleValue(0.0));
    m_random->SetAttribute("Max", DoubleValue(1.0));
}

VirtualInterfererErrorModel::~VirtualInterfererErrorModel()
{
    NS_LOG_FUNCTION(this);
}

void
VirtualInterfererErrorModel::SetPacketLossRate(double rate)
{
    NS_LOG_FUNCTION(this << rate);
    m_packetLossRate = std::clamp(rate, 0.0, 1.0);
}

double
VirtualInterfererErrorModel::GetPacketLossRate() const
{
    return m_packetLossRate;
}

uint64_t
VirtualInterfererErrorModel::GetDroppedPackets() const
{
    return m_droppedPackets;
}

uint64_t
VirtualInterfererErrorModel::GetTotalPackets() const
{
    return m_totalPackets;
}

void
VirtualInterfererErrorModel::ResetStats()
{
    NS_LOG_FUNCTION(this);
    m_droppedPackets = 0;
    m_totalPackets = 0;
}

bool
VirtualInterfererErrorModel::DoCorrupt(Ptr<Packet> pkt)
{
    NS_LOG_FUNCTION(this << pkt);

    m_totalPackets++;

    // If error model is disabled or loss rate is 0, don't corrupt
    if (!IsEnabled() || m_packetLossRate <= 0.0)
    {
        return false;
    }

    // Generate random value and compare to loss rate
    double randomValue = m_random->GetValue();
    bool shouldDrop = (randomValue < m_packetLossRate);

    if (shouldDrop)
    {
        m_droppedPackets++;
        NS_LOG_DEBUG("Virtual interferer dropping packet (rate=" << m_packetLossRate
                     << ", random=" << randomValue << ", size=" << pkt->GetSize() << ")");
    }

    return shouldDrop;
}

void
VirtualInterfererErrorModel::DoReset()
{
    NS_LOG_FUNCTION(this);
    // Reset statistics when model is reset
    m_droppedPackets = 0;
    m_totalPackets = 0;
}

} // namespace ns3
