/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: [Your Name]
 */

#include "beacon-report-element.h"

#include "ns3/buffer.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("BeaconReportElement");

BeaconReportElement::BeaconReportElement()
    : m_measurementToken(0),
      m_measurementReportMode(0),
      m_operatingClass(0),
      m_channelNumber(0),
      m_actualMeasurementStartTime(0),
      m_measurementDuration(0),
      m_reportedFrameInfo(0),
      m_rcpi(255),
      m_rsni(255),
      m_bssid(),
      m_antennaId(0),
      m_parentTsf(0)
{
    NS_LOG_FUNCTION(this);
}

WifiInformationElementId
BeaconReportElement::ElementId() const
{
    return IE_MEASUREMENT_REPORT;
}

void
BeaconReportElement::Print(std::ostream& os) const
{
    os << "BeaconReport("
       << "Token=" << +m_measurementToken
       << ", BSSID=" << m_bssid
       << ", Channel=" << +m_channelNumber
       << ", RCPI=" << +m_rcpi
       << ", RSNI=" << +m_rsni
       << ")";
}

void
BeaconReportElement::SetMeasurementToken(uint8_t token)
{
    m_measurementToken = token;
}

uint8_t
BeaconReportElement::GetMeasurementToken() const
{
    return m_measurementToken;
}

void
BeaconReportElement::SetMeasurementReportMode(uint8_t mode)
{
    m_measurementReportMode = mode;
}

uint8_t
BeaconReportElement::GetMeasurementReportMode() const
{
    return m_measurementReportMode;
}

void
BeaconReportElement::SetOperatingClass(uint8_t operatingClass)
{
    m_operatingClass = operatingClass;
}

uint8_t
BeaconReportElement::GetOperatingClass() const
{
    return m_operatingClass;
}

void
BeaconReportElement::SetChannelNumber(uint8_t channel)
{
    m_channelNumber = channel;
}

uint8_t
BeaconReportElement::GetChannelNumber() const
{
    return m_channelNumber;
}

void
BeaconReportElement::SetActualMeasurementStartTime(uint64_t tsf)
{
    m_actualMeasurementStartTime = tsf;
}

uint64_t
BeaconReportElement::GetActualMeasurementStartTime() const
{
    return m_actualMeasurementStartTime;
}

void
BeaconReportElement::SetMeasurementDuration(uint16_t duration)
{
    m_measurementDuration = duration;
}

uint16_t
BeaconReportElement::GetMeasurementDuration() const
{
    return m_measurementDuration;
}

void
BeaconReportElement::SetReportedFrameInfo(uint8_t info)
{
    m_reportedFrameInfo = info;
}

uint8_t
BeaconReportElement::GetReportedFrameInfo() const
{
    return m_reportedFrameInfo;
}

void
BeaconReportElement::SetRcpi(uint8_t rcpi)
{
    m_rcpi = rcpi;
}

uint8_t
BeaconReportElement::GetRcpi() const
{
    return m_rcpi;
}

void
BeaconReportElement::SetRsni(uint8_t rsni)
{
    m_rsni = rsni;
}

uint8_t
BeaconReportElement::GetRsni() const
{
    return m_rsni;
}

void
BeaconReportElement::SetBssid(Mac48Address bssid)
{
    m_bssid = bssid;
}

Mac48Address
BeaconReportElement::GetBssid() const
{
    return m_bssid;
}

void
BeaconReportElement::SetAntennaId(uint8_t antennaId)
{
    m_antennaId = antennaId;
}

uint8_t
BeaconReportElement::GetAntennaId() const
{
    return m_antennaId;
}

void
BeaconReportElement::SetParentTsf(uint32_t parentTsf)
{
    m_parentTsf = parentTsf;
}

uint32_t
BeaconReportElement::GetParentTsf() const
{
    return m_parentTsf;
}

uint16_t
BeaconReportElement::GetInformationFieldSize() const
{
    // Token(1) + Mode(1) + Type(1) + OpClass(1) + Channel(1) + StartTime(8)
    // + Duration(2) + FrameInfo(1) + RCPI(1) + RSNI(1) + BSSID(6) + AntennaID(1) + ParentTSF(4)
    // = 3 + 23 = 26 octets
    return 26;
}

void
BeaconReportElement::SerializeInformationField(Buffer::Iterator start) const
{
    NS_LOG_FUNCTION(this);

    // Measurement header (3 octets)
    start.WriteU8(m_measurementToken);
    start.WriteU8(m_measurementReportMode);
    start.WriteU8(MEASUREMENT_TYPE_BEACON);

    // Beacon report fields
    start.WriteU8(m_operatingClass);
    start.WriteU8(m_channelNumber);
    start.WriteHtolsbU64(m_actualMeasurementStartTime);
    start.WriteHtolsbU16(m_measurementDuration);
    start.WriteU8(m_reportedFrameInfo);
    start.WriteU8(m_rcpi);
    start.WriteU8(m_rsni);

    // BSSID (6 octets)
    uint8_t bssidBytes[6];
    m_bssid.CopyTo(bssidBytes);
    start.Write(bssidBytes, 6);

    start.WriteU8(m_antennaId);
    start.WriteHtolsbU32(m_parentTsf);
}

uint16_t
BeaconReportElement::DeserializeInformationField(Buffer::Iterator start, uint16_t length)
{
    NS_LOG_FUNCTION(this << length);

    if (length < 26)
    {
        NS_LOG_WARN("BeaconReportElement: insufficient length " << length << " (expected >= 26)");
        return length;
    }

    // Measurement header
    m_measurementToken = start.ReadU8();
    m_measurementReportMode = start.ReadU8();
    uint8_t measurementType = start.ReadU8();

    if (measurementType != MEASUREMENT_TYPE_BEACON)
    {
        NS_LOG_WARN("BeaconReportElement: unexpected measurement type " << +measurementType);
    }

    // Beacon report fields
    m_operatingClass = start.ReadU8();
    m_channelNumber = start.ReadU8();
    m_actualMeasurementStartTime = start.ReadLsbtohU64();
    m_measurementDuration = start.ReadLsbtohU16();
    m_reportedFrameInfo = start.ReadU8();
    m_rcpi = start.ReadU8();
    m_rsni = start.ReadU8();

    // BSSID
    uint8_t bssidBytes[6];
    start.Read(bssidBytes, 6);
    m_bssid.CopyFrom(bssidBytes);

    m_antennaId = start.ReadU8();
    m_parentTsf = start.ReadLsbtohU32();

    return length;
}

} // namespace ns3
