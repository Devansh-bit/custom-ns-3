/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: [Your Name]
 */

#include "neighbor-report-element.h"

#include "ns3/buffer.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NeighborReportElement");

NeighborReportElement::NeighborReportElement()
    : m_bssid(),
      m_bssidInfo(0),
      m_operatingClass(0),
      m_channelNumber(0),
      m_phyType(0)
{
    NS_LOG_FUNCTION(this);
}

WifiInformationElementId
NeighborReportElement::ElementId() const
{
    return IE_NEIGHBOR_REPORT;
}

void
NeighborReportElement::Print(std::ostream& os) const
{
    os << "NeighborReport("
       << "BSSID=" << m_bssid
       << ", BSSIDInfo=0x" << std::hex << m_bssidInfo << std::dec
       << ", OpClass=" << +m_operatingClass
       << ", Channel=" << +m_channelNumber
       << ", PHY=" << +m_phyType
       << ")";
}

void
NeighborReportElement::SetBssid(Mac48Address bssid)
{
    NS_LOG_FUNCTION(this << bssid);
    m_bssid = bssid;
}

Mac48Address
NeighborReportElement::GetBssid() const
{
    return m_bssid;
}

void
NeighborReportElement::SetBssidInfo(uint32_t info)
{
    NS_LOG_FUNCTION(this << info);
    m_bssidInfo = info;
}

uint32_t
NeighborReportElement::GetBssidInfo() const
{
    return m_bssidInfo;
}

void
NeighborReportElement::SetOperatingClass(uint8_t operatingClass)
{
    NS_LOG_FUNCTION(this << +operatingClass);
    m_operatingClass = operatingClass;
}

uint8_t
NeighborReportElement::GetOperatingClass() const
{
    return m_operatingClass;
}

void
NeighborReportElement::SetChannelNumber(uint8_t channel)
{
    NS_LOG_FUNCTION(this << +channel);
    m_channelNumber = channel;
}

uint8_t
NeighborReportElement::GetChannelNumber() const
{
    return m_channelNumber;
}

void
NeighborReportElement::SetPhyType(uint8_t phyType)
{
    NS_LOG_FUNCTION(this << +phyType);
    m_phyType = phyType;
}

uint8_t
NeighborReportElement::GetPhyType() const
{
    return m_phyType;
}

uint16_t
NeighborReportElement::GetInformationFieldSize() const
{
    // BSSID (6) + BSSIDInfo (4) + OpClass (1) + Channel (1) + PHY (1) = 13 octets
    return 13;
}

void
NeighborReportElement::SerializeInformationField(Buffer::Iterator start) const
{
    NS_LOG_FUNCTION(this);

    // Serialize BSSID (6 octets)
    uint8_t bssidBytes[6];
    m_bssid.CopyTo(bssidBytes);
    start.Write(bssidBytes, 6);

    // Serialize BSSID Information (4 octets, little-endian as per 802.11)
    start.WriteHtolsbU32(m_bssidInfo);

    // Serialize Operating Class (1 octet)
    start.WriteU8(m_operatingClass);

    // Serialize Channel Number (1 octet)
    start.WriteU8(m_channelNumber);

    // Serialize PHY Type (1 octet)
    start.WriteU8(m_phyType);
}

uint16_t
NeighborReportElement::DeserializeInformationField(Buffer::Iterator start, uint16_t length)
{
    NS_LOG_FUNCTION(this << length);

    // Minimum length check
    if (length < 13)
    {
        NS_LOG_WARN("NeighborReportElement: insufficient length " << length << " (expected >= 13)");
        return length;
    }

    // Deserialize BSSID (6 octets)
    uint8_t bssidBytes[6];
    start.Read(bssidBytes, 6);
    m_bssid.CopyFrom(bssidBytes);

    // Deserialize BSSID Information (4 octets, little-endian)
    m_bssidInfo = start.ReadLsbtohU32();

    // Deserialize Operating Class (1 octet)
    m_operatingClass = start.ReadU8();

    // Deserialize Channel Number (1 octet)
    m_channelNumber = start.ReadU8();

    // Deserialize PHY Type (1 octet)
    m_phyType = start.ReadU8();

    // Skip any optional subelements (length - 13 bytes)
    // These are not parsed in this implementation
    uint16_t remainingBytes = length - 13;
    if (remainingBytes > 0)
    {
        NS_LOG_DEBUG("Skipping " << remainingBytes << " bytes of optional subelements");
    }

    return length;
}

} // namespace ns3
