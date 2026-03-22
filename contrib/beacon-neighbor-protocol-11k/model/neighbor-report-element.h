/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: [Your Name]
 */

#ifndef NEIGHBOR_REPORT_ELEMENT_H
#define NEIGHBOR_REPORT_ELEMENT_H

#include "ns3/mac48-address.h"
#include "ns3/wifi-information-element.h"

namespace ns3
{

/**
 * \ingroup wifi
 * \brief IEEE 802.11k Neighbor Report Element
 *
 * This class implements the Neighbor Report element as defined in
 * IEEE 802.11-2016 Section 9.4.2.37 (Neighbor Report element).
 *
 * The Neighbor Report element contains information about a neighboring BSS,
 * including its BSSID, channel, regulatory class, PHY type, and capabilities.
 *
 * Element format (Figure 9-195):
 * | Element ID | Length | BSSID | BSSID Info | Operating Class | Channel | PHY Type |
 * |     1      |   1    |   6   |     4      |        1        |    1    |    1     |
 *
 * Element ID = 52 (Neighbor Report)
 * Minimum length = 13 octets (without optional subelements)
 */
class NeighborReportElement : public WifiInformationElement
{
  public:
    NeighborReportElement();

    // WifiInformationElement interface
    WifiInformationElementId ElementId() const override;
    void Print(std::ostream& os) const override;

    /**
     * \brief Set the BSSID of the neighbor AP
     * \param bssid the MAC address of the neighbor AP
     */
    void SetBssid(Mac48Address bssid);

    /**
     * \brief Get the BSSID of the neighbor AP
     * \return the MAC address of the neighbor AP
     */
    Mac48Address GetBssid() const;

    /**
     * \brief Set the BSSID Information field
     *
     * The BSSID Information field is a 32-bit field containing capability
     * and reachability information about the neighbor AP. See IEEE 802.11-2016
     * Figure 9-196 for bit definitions.
     *
     * \param info the BSSID Information value
     */
    void SetBssidInfo(uint32_t info);

    /**
     * \brief Get the BSSID Information field
     * \return the BSSID Information value
     */
    uint32_t GetBssidInfo() const;

    /**
     * \brief Set the Operating Class (regulatory class)
     *
     * The Operating Class field indicates the operating class of the neighbor AP
     * as defined in Annex E of IEEE 802.11-2016.
     *
     * \param operatingClass the operating class value
     */
    void SetOperatingClass(uint8_t operatingClass);

    /**
     * \brief Get the Operating Class
     * \return the operating class value
     */
    uint8_t GetOperatingClass() const;

    /**
     * \brief Set the Channel Number
     * \param channel the channel number of the neighbor AP
     */
    void SetChannelNumber(uint8_t channel);

    /**
     * \brief Get the Channel Number
     * \return the channel number of the neighbor AP
     */
    uint8_t GetChannelNumber() const;

    /**
     * \brief Set the PHY Type
     *
     * The PHY Type field indicates the PHY type of the neighbor AP
     * as defined in IEEE 802.11-2016 Table 9-176.
     *
     * Common values:
     * - 4: DSSS
     * - 5: OFDM
     * - 7: HT
     * - 8: VHT
     * - 9: HE
     *
     * \param phyType the PHY type value
     */
    void SetPhyType(uint8_t phyType);

    /**
     * \brief Get the PHY Type
     * \return the PHY type value
     */
    uint8_t GetPhyType() const;

  private:
    uint16_t GetInformationFieldSize() const override;
    void SerializeInformationField(Buffer::Iterator start) const override;
    uint16_t DeserializeInformationField(Buffer::Iterator start, uint16_t length) override;

    Mac48Address m_bssid;          ///< BSSID of the neighbor AP (6 octets)
    uint32_t m_bssidInfo{0};       ///< BSSID Information field (4 octets)
    uint8_t m_operatingClass{0};   ///< Operating Class (1 octet)
    uint8_t m_channelNumber{0};    ///< Channel Number (1 octet)
    uint8_t m_phyType{0};          ///< PHY Type (1 octet)
};

} // namespace ns3

#endif /* NEIGHBOR_REPORT_ELEMENT_H */
