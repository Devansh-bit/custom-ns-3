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

#ifndef LINK_MEASUREMENT_REQUEST_H
#define LINK_MEASUREMENT_REQUEST_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/packet.h"
#include "ns3/mac48-address.h"

namespace ns3
{

// Forward declaration to avoid circular dependency
struct LinkMeasurementReport;

/**
 * \ingroup wifi
 * \brief A simple structure to hold link measurement request details.
 *
 * This struct is used to pass around the parameters of a link
 * measurement request, including the source, destination, power levels,
 * and the request packet itself.
 */
struct LinkMeasurementRequest
{
  public:
    /**
     * \brief Create a new LinkMeasurementRequest.
     *
     * This is the "method to create it" as requested, implemented as a
     * constructor.
     *
     * \param from The source MAC address.
     * \param to The destination MAC address.
     * \param transmitPowerUsed The power used to send the request packet (in dBm).
     * \param maxTransmitPower The maximum transmit power of the sender (in dBm).
     * \param packet The original request packet.
     */
    LinkMeasurementRequest(Mac48Address from,
                           Mac48Address to,
                           uint8_t dialogToken,
                           uint8_t transmitPowerUsed,
                           uint8_t maxTransmitPower,
                           Ptr<const Packet> packet);

    /**
     * \brief Get the source MAC address.
     * \return The source MAC address.
     */
    Mac48Address GetFrom() const;

    /**
     * \brief Get the destination MAC address.
     * \return The destination MAC address.
     */
    Mac48Address GetTo() const;
    uint8_t GetDialogToken() const;

    /**
     * \brief Get the transmit power used.
     * \return The transmit power in dBm.
     */
    uint8_t GetTransmitPowerUsed() const;

    /**
     * \brief Get the maximum transmit power.
     * \return The maximum transmit power in dBm.
     */
    uint8_t GetMaxTransmitPower() const;

    /**
     * \brief Get the associated packet.
     * \return A const pointer to the packet.
     */
    Ptr<const Packet> GetPacket() const;

    /**
     * \brief Get the transmit power used in dBm (converted from 2's complement)
     * \return The transmit power in dBm
     */
    int8_t GetTransmitPowerUsedDbm() const;

    /**
     * \brief Get the maximum transmit power in dBm (converted from 2's complement)
     * \return The maximum transmit power in dBm
     */
    int8_t GetMaxTransmitPowerDbm() const;

  private:
    Mac48Address m_from;              ///< The source of the request.
    Mac48Address m_to;                ///< The destination of the request.
    uint8_t m_dialogToken;
    uint8_t m_transmitPowerUsed;      ///< Power used to transmit the request.
    uint8_t m_maxTransmitPower;       ///< Max power capability of the requester.
    Ptr<const Packet> m_packet;       ///< The request packet.
};

} // namespace ns3

#endif /* LINK_MEASUREMENT_REQUEST_H */
