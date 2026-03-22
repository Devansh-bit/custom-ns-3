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

#ifndef LINK_MEASUREMENT_REPORT_H
#define LINK_MEASUREMENT_REPORT_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"

namespace ns3
{

/**
 * \ingroup wifi
 * \brief TPC Report Element for Link Measurement (Legacy struct)
 *
 * This struct is kept for backward compatibility.
 * For new code, use the TpcReportElement class from tpc-report-element.h
 */
struct TpcReportElementLegacy
{
    uint8_t elementID;
    uint8_t length;
    uint8_t transmitPower;  ///< Transmit power in dBm (signed 2's complement)
    uint8_t linkMargin;     ///< Link margin in dB (unsigned)

    TpcReportElementLegacy(
        uint8_t transmitPower,
        uint8_t linkMargin
    );
};

/**
 * \ingroup wifi
 * \brief Link Measurement Report frame body
 *
 * This class implements the Link Measurement Report frame format
 * as defined in IEEE 802.11k-2008 Section 7.4.5.4
 *
 * Frame Format:
 * +----------------+----------------+------------------+------------------+----------+----------+
 * | Dialog Token   | TPC Report     | Receive          | Transmit         | Optional | Optional |
 * |   (1 octet)    | Element        | Antenna ID       | Antenna ID       | RCPI     | RSNI     |
 * |                | (4 octets)     | (1 octet)        | (1 octet)        | element  | element  |
 * +----------------+----------------+------------------+------------------+----------+----------+
 */
struct LinkMeasurementReport
{
  public:
    LinkMeasurementReport(
    Mac48Address from,
    Mac48Address to,
    uint8_t dialogToken,
    uint8_t transmitPower,
    uint8_t linkMargin,
    uint8_t receiveAntennaId,
    uint8_t transmitAntennaId,
    uint16_t rcpi,
    uint16_t rsni
    );



    /**
     * \brief Get the dialog token
     * \return The dialog token value
     */
    uint8_t GetDialogToken() const;


    /**
     * \brief Get the TPC Report element
     * \return The TPC Report element
     */
    TpcReportElementLegacy GetTpcReport() const;



    /**
     * \brief Get the receive antenna identifier
     * \return Receive antenna ID
     */
    uint8_t GetReceiveAntennaId() const;



    /**
     * \brief Get the transmit antenna identifier
     * \return Transmit antenna ID
     */
    uint8_t GetTransmitAntennaId() const;



    /**
     * \brief Get the RCPI value
     * \return RCPI value if set, std::nullopt otherwise
     */
    uint8_t GetRcpi() const;




    /**
     * \brief Get the RSNI value
     * \return RSNI value if set, std::nullopt otherwise
     */
    uint8_t GetRsni() const;

    /**
     * \brief Get transmit power in dBm (converted from 2's complement)
     * \return Transmit power in dBm
     */
    int8_t GetTransmitPowerDbm() const;

    /**
     * \brief Get link margin in dB
     * \return Link margin in dB
     */
    uint8_t GetLinkMarginDb() const;

    /**
     * \brief Get RCPI in dBm (converted from 802.11k format)
     * \return RCPI in dBm
     */
    double GetRcpiDbm() const;

    /**
     * \brief Get RSNI in dB (converted from 802.11k format)
     * \return RSNI in dB
     */
    double GetRsniDb() const;

    /**
     * \brief Convert uint8_t 2's complement to int8_t
     * \param value The 2's complement value
     * \return The signed integer value
     */
    static int8_t ConvertFromTwosComplement(uint8_t value);

    /**
     * \brief Convert int8_t to uint8_t 2's complement
     * \param value The signed integer value
     * \return The 2's complement value
     */
    static uint8_t ConvertToTwosComplement(int8_t value);

    /**
     * \brief Convert RCPI to dBm
     * \param rcpi RCPI value (0-220)
     * \return Power in dBm
     */
    static double ConvertRcpiToDbm(uint8_t rcpi);

    /**
     * \brief Convert RSNI to dB
     * \param rsni RSNI value (0-255)
     * \return SNR in dB
     */
    static double ConvertRsniToDb(uint8_t rsni);





  private:
    uint8_t m_dialogToken;               ///< Dialog token for matching request/response
    TpcReportElementLegacy m_tpcReport;        ///< TPC Report element
    uint8_t m_receiveAntennaId;          ///< Receive antenna identifier
    uint8_t m_transmitAntennaId;         ///< Transmit antenna identifier
    uint8_t m_rcpi;       ///< Optional RCPI element
    uint8_t m_rsni;       ///< Optional RSNI element
    Mac48Address m_sourceMac;
    Mac48Address m_destinationMac;
};

} // namespace ns3

#endif /* LINK_MEASUREMENT_REPORT_H */
