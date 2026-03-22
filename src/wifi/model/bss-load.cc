/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * BSS Load Information Element (IEEE 802.11e-2005, IEEE 802.11k-2008)
 */

#include "bss-load.h"

namespace ns3
{

BssLoad::BssLoad()
    : m_stationCount(0),
      m_channelUtilization(0),
      m_availableAdmissionCapacity(0)
{
}

WifiInformationElementId
BssLoad::ElementId() const
{
    return IE_BSS_LOAD;  // 11
}

void
BssLoad::SetStationCount(uint16_t count)
{
    m_stationCount = count;
}

void
BssLoad::SetChannelUtilization(uint8_t utilization)
{
    m_channelUtilization = utilization;
}

void
BssLoad::SetAvailableAdmissionCapacity(uint16_t capacity)
{
    m_availableAdmissionCapacity = capacity;
}

uint16_t
BssLoad::GetStationCount() const
{
    return m_stationCount;
}

uint8_t
BssLoad::GetChannelUtilization() const
{
    return m_channelUtilization;
}

uint16_t
BssLoad::GetAvailableAdmissionCapacity() const
{
    return m_availableAdmissionCapacity;
}

void
BssLoad::SetWifiUtilization(uint8_t utilization)
{
    // Pack WiFi utilization into high byte of AvailableAdmissionCapacity
    m_availableAdmissionCapacity = (static_cast<uint16_t>(utilization) << 8) |
                                   (m_availableAdmissionCapacity & 0x00FF);
}

void
BssLoad::SetNonWifiUtilization(uint8_t utilization)
{
    // Pack non-WiFi utilization into low byte of AvailableAdmissionCapacity
    m_availableAdmissionCapacity = (m_availableAdmissionCapacity & 0xFF00) |
                                   static_cast<uint16_t>(utilization);
}

uint8_t
BssLoad::GetWifiUtilization() const
{
    // Extract WiFi utilization from high byte of AvailableAdmissionCapacity
    return static_cast<uint8_t>((m_availableAdmissionCapacity >> 8) & 0xFF);
}

uint8_t
BssLoad::GetNonWifiUtilization() const
{
    // Extract non-WiFi utilization from low byte of AvailableAdmissionCapacity
    return static_cast<uint8_t>(m_availableAdmissionCapacity & 0xFF);
}

uint16_t
BssLoad::GetInformationFieldSize() const
{
    return 5;  // Fixed size: 2 + 1 + 2 bytes
}

void
BssLoad::SerializeInformationField(Buffer::Iterator start) const
{
    start.WriteHtolsbU16(m_stationCount);
    start.WriteU8(m_channelUtilization);
    start.WriteHtolsbU16(m_availableAdmissionCapacity);
}

uint16_t
BssLoad::DeserializeInformationField(Buffer::Iterator start, uint16_t length)
{
    Buffer::Iterator i = start;
    m_stationCount = i.ReadLsbtohU16();
    m_channelUtilization = i.ReadU8();
    m_availableAdmissionCapacity = i.ReadLsbtohU16();
    return i.GetDistanceFrom(start);
}

void
BssLoad::Print(std::ostream& os) const
{
    os << "BssLoad[StationCount=" << m_stationCount
       << ",ChannelUtil=" << static_cast<uint32_t>(m_channelUtilization)
       << "/255"
       << ",AAC=" << m_availableAdmissionCapacity << "]";
}

} // namespace ns3
