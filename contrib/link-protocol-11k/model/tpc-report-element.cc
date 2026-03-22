/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "tpc-report-element.h"

namespace ns3
{

TpcReportElement::TpcReportElement()
    : m_transmitPower(0),
      m_linkMargin(0)
{
}

TpcReportElement::TpcReportElement(int8_t transmitPower, uint8_t linkMargin)
    : m_transmitPower(transmitPower),
      m_linkMargin(linkMargin)
{
}

WifiInformationElementId
TpcReportElement::ElementId() const
{
    return IE_TPC_REPORT;  // ID 35
}

void
TpcReportElement::Print(std::ostream& os) const
{
    os << "TPC Report Element ["
       << "Transmit Power=" << static_cast<int>(m_transmitPower) << " dBm, "
       << "Link Margin=" << static_cast<int>(m_linkMargin) << " dB]";
}

void
TpcReportElement::SetTransmitPower(int8_t power)
{
    m_transmitPower = power;
}

int8_t
TpcReportElement::GetTransmitPower() const
{
    return m_transmitPower;
}

void
TpcReportElement::SetLinkMargin(uint8_t margin)
{
    m_linkMargin = margin;
}

uint8_t
TpcReportElement::GetLinkMargin() const
{
    return m_linkMargin;
}

uint16_t
TpcReportElement::GetInformationFieldSize() const
{
    return 2;  // 1 byte transmit power + 1 byte link margin
}

void
TpcReportElement::SerializeInformationField(Buffer::Iterator start) const
{
    start.WriteU8(static_cast<uint8_t>(m_transmitPower));  // 2's complement
    start.WriteU8(m_linkMargin);
}

uint16_t
TpcReportElement::DeserializeInformationField(Buffer::Iterator start, uint16_t length)
{
    m_transmitPower = static_cast<int8_t>(start.ReadU8());
    m_linkMargin = start.ReadU8();
    return 2;
}

} // namespace ns3
