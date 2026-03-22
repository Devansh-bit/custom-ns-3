/*
* Copyright (c) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "link-measurement-request.h"
#include "link-measurement-report.h"



namespace ns3
{
NS_LOG_COMPONENT_DEFINE("LinkMeasurementRequest");

LinkMeasurementRequest::LinkMeasurementRequest(Mac48Address from,
                                               Mac48Address to,
                                               uint8_t dialogToken,
                                               uint8_t transmitPowerUsed,
                                               uint8_t maxTransmitPower,
                                               Ptr<const Packet> packet)
    : m_from(from),
      m_to(to),
      m_dialogToken(dialogToken),
      m_transmitPowerUsed(transmitPowerUsed),
      m_maxTransmitPower(maxTransmitPower),
      m_packet(packet)
{
}

Mac48Address
LinkMeasurementRequest::GetFrom() const
{
    return m_from;
}

Mac48Address
LinkMeasurementRequest::GetTo() const
{
    return m_to;
}

uint8_t
LinkMeasurementRequest::GetDialogToken() const
{
    return m_dialogToken;
}

uint8_t
LinkMeasurementRequest::GetTransmitPowerUsed() const
{
    return m_transmitPowerUsed;
}

uint8_t
LinkMeasurementRequest::GetMaxTransmitPower() const
{
    return m_maxTransmitPower;
}

Ptr<const Packet>
LinkMeasurementRequest::GetPacket() const
{
    return m_packet;
}

int8_t
LinkMeasurementRequest::GetTransmitPowerUsedDbm() const
{
    return LinkMeasurementReport::ConvertFromTwosComplement(m_transmitPowerUsed);
}

int8_t
LinkMeasurementRequest::GetMaxTransmitPowerDbm() const
{
    return LinkMeasurementReport::ConvertFromTwosComplement(m_maxTransmitPower);
}

} // namespace ns3
