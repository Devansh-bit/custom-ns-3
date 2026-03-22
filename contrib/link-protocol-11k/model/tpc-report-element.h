/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef TPC_REPORT_ELEMENT_H
#define TPC_REPORT_ELEMENT_H

#include "ns3/wifi-information-element.h"

namespace ns3
{

/**
 * \ingroup wifi
 * \brief IEEE 802.11 TPC Report Element
 *
 * This class implements the TPC Report element as defined in
 * IEEE 802.11-2016. The TPC Report element contains transmit power
 * control information used in Link Measurement exchanges.
 *
 * Element format:
 * | Element ID | Length | Transmit Power | Link Margin |
 * |     1      |   1    |       1        |      1      |
 *
 * Element ID = 35 (TPC Report)
 * Length = 2 octets
 */
class TpcReportElement : public WifiInformationElement
{
  public:
    TpcReportElement();

    /**
     * \brief Construct with values
     * \param transmitPower Transmit power in dBm (signed, 2's complement)
     * \param linkMargin Link margin in dB (unsigned)
     */
    TpcReportElement(int8_t transmitPower, uint8_t linkMargin);

    // WifiInformationElement interface
    WifiInformationElementId ElementId() const override;
    void Print(std::ostream& os) const override;

    /**
     * \brief Set the transmit power
     * \param power Transmit power in dBm
     */
    void SetTransmitPower(int8_t power);

    /**
     * \brief Get the transmit power
     * \return Transmit power in dBm
     */
    int8_t GetTransmitPower() const;

    /**
     * \brief Set the link margin
     * \param margin Link margin in dB
     */
    void SetLinkMargin(uint8_t margin);

    /**
     * \brief Get the link margin
     * \return Link margin in dB
     */
    uint8_t GetLinkMargin() const;

  private:
    uint16_t GetInformationFieldSize() const override;
    void SerializeInformationField(Buffer::Iterator start) const override;
    uint16_t DeserializeInformationField(Buffer::Iterator start, uint16_t length) override;

    int8_t m_transmitPower{0};  ///< Transmit power in dBm
    uint8_t m_linkMargin{0};     ///< Link margin in dB
};

} // namespace ns3

#endif /* TPC_REPORT_ELEMENT_H */
