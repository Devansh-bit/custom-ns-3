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

#include "link-measurement-report.h"
#include "ns3/log.h"
#include "ns3/mac48-address.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("LinkMeasurementReport");

TpcReportElementLegacy::TpcReportElementLegacy(
    uint8_t transmitPower,
    uint8_t linkMargin
    ):
    elementID(35),
    length(2),
    transmitPower(transmitPower),
    linkMargin(linkMargin)
{}


LinkMeasurementReport::LinkMeasurementReport(
    Mac48Address from,
    Mac48Address to,
    uint8_t dialogToken,
    uint8_t transmitPower,
    uint8_t linkMargin,
    uint8_t receiveAntennaId,
    uint8_t transmitAntennaId,
    uint16_t rcpi,
    uint16_t rsni
    )
    :
      m_dialogToken(dialogToken),
      m_tpcReport(TpcReportElementLegacy(transmitPower, linkMargin)),
      m_receiveAntennaId(receiveAntennaId),
      m_transmitAntennaId(transmitAntennaId),
      m_rcpi(rcpi),
      m_rsni(rsni),
      m_sourceMac(from),
      m_destinationMac(to)
{
}

uint8_t
LinkMeasurementReport::GetDialogToken() const
{
    return m_dialogToken;
}

TpcReportElementLegacy
LinkMeasurementReport::GetTpcReport() const
{
    return m_tpcReport;
}

uint8_t
LinkMeasurementReport::GetReceiveAntennaId() const
{
    return m_receiveAntennaId;
}



uint8_t
LinkMeasurementReport::GetTransmitAntennaId() const
{
    return m_transmitAntennaId;
}



uint8_t LinkMeasurementReport::GetRcpi() const
{
    return m_rcpi;
}


uint8_t LinkMeasurementReport::GetRsni() const
{
    return m_rsni;
}

int8_t
LinkMeasurementReport::GetTransmitPowerDbm() const
{
    return ConvertFromTwosComplement(m_tpcReport.transmitPower);
}

uint8_t
LinkMeasurementReport::GetLinkMarginDb() const
{
    return m_tpcReport.linkMargin;
}

double
LinkMeasurementReport::GetRcpiDbm() const
{
    return ConvertRcpiToDbm(m_rcpi);
}

double
LinkMeasurementReport::GetRsniDb() const
{
    return ConvertRsniToDb(m_rsni);
}

int8_t
LinkMeasurementReport::ConvertFromTwosComplement(uint8_t value)
{
    // Convert uint8_t 2's complement representation to int8_t
    // In 2's complement, if the MSB is 1, the number is negative
    if (value & 0x80)
    {
        // Negative number: convert from 2's complement
        return static_cast<int8_t>(value);
    }
    else
    {
        // Positive number
        return static_cast<int8_t>(value);
    }
}

uint8_t
LinkMeasurementReport::ConvertToTwosComplement(int8_t value)
{
    // Convert int8_t to uint8_t 2's complement representation
    // Simply reinterpret the bits
    return static_cast<uint8_t>(value);
}

double
LinkMeasurementReport::ConvertRcpiToDbm(uint8_t rcpi)
{
    // RCPI formula: RCPI = 2 * (RSSI + 110)
    // Inverse: RSSI (dBm) = (RCPI / 2) - 110
    return (static_cast<double>(rcpi) / 2.0) - 110.0;
}

double
LinkMeasurementReport::ConvertRsniToDb(uint8_t rsni)
{
    // RSNI formula: RSNI = 2 * SNR
    // Inverse: SNR (dB) = RSNI / 2
    return static_cast<double>(rsni) / 2.0;
}


} // namespace ns3
