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

#ifndef LINK_MEASUREMENT_PROTOCOL_HELPER_H
#define LINK_MEASUREMENT_PROTOCOL_HELPER_H

#include "../model/link-measurement-protocol.h"
#include "../model/link-measurement-request.h"
#include "../model/link-measurement-report.h"

#include "ns3/object.h"
#include "ns3/traced-callback.h"
#include "ns3/wifi-net-device.h"
#include "ns3/mac48-address.h"

#include <map>

namespace ns3
{

/**
 * \brief Helper class for 802.11k Link Measurement Protocol
 *
 * This helper manages LinkMeasurementProtocol instances across multiple devices
 * using the unified sniffer architecture (singleton pattern via UnifiedPhySnifferHelper).
 *
 * Usage:
 *   Ptr<LinkMeasurementProtocolHelper> helper = CreateObject<LinkMeasurementProtocolHelper>();
 *   helper->InstallOnAp(apDevice);
 *   helper->InstallOnSta(staDevice);
 *   helper->SendLinkMeasurementRequest(staDevice, apAddress);
 */
class LinkMeasurementProtocolHelper : public Object
{
  public:
    /**
     * \brief Get the type ID
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * \brief Default constructor
     */
    LinkMeasurementProtocolHelper();

    /**
     * \brief Destructor
     */
    virtual ~LinkMeasurementProtocolHelper();

    // ===== Installation =====

    /**
     * \brief Install the Link Measurement Protocol on an AP device
     * \param apDevice the AP WiFi device
     */
    void InstallOnAp(Ptr<WifiNetDevice> apDevice);

    /**
     * \brief Install the Link Measurement Protocol on a STA device
     * \param staDevice the STA WiFi device
     */
    void InstallOnSta(Ptr<WifiNetDevice> staDevice);

    // ===== Protocol Methods =====

    /**
     * \brief Send a Link Measurement Request from a device to a peer
     * \param device the WiFi device sending the request
     * \param to the MAC address of the peer
     * \param transmitPowerUsed the transmit power used (dBm)
     * \param maxTransmitPower the maximum transmit power (dBm)
     */
    void SendLinkMeasurementRequest(Ptr<WifiNetDevice> device,
                                    Mac48Address to,
                                    int8_t transmitPowerUsed = 20,
                                    int8_t maxTransmitPower = 23);

    // ===== Trace Sources =====

    /**
     * \brief Trace source for Link Measurement Request received
     */
    TracedCallback<Mac48Address, LinkMeasurementRequest> m_requestReceivedTrace;

    /**
     * \brief Trace source for Link Measurement Report received
     */
    TracedCallback<Mac48Address, LinkMeasurementReport> m_reportReceivedTrace;

    // ===== Accessors =====

    /**
     * \brief Get the protocol instance for a device
     * \param device the WiFi device
     * \return the protocol instance, or nullptr if not installed
     */
    Ptr<LinkMeasurementProtocol> GetProtocol(Ptr<WifiNetDevice> device) const;

  private:
    /**
     * \brief Internal trace callback for request received
     * \param from source MAC address
     * \param request the request object
     */
    void OnRequestReceived(Mac48Address from, LinkMeasurementRequest request);

    /**
     * \brief Internal trace callback for report received
     * \param from source MAC address
     * \param report the report object
     */
    void OnReportReceived(Mac48Address from, LinkMeasurementReport report);

    // Protocol instances keyed by device MAC address
    std::map<Mac48Address, Ptr<LinkMeasurementProtocol>> m_protocols;
};

} // namespace ns3

#endif // LINK_MEASUREMENT_PROTOCOL_HELPER_H
