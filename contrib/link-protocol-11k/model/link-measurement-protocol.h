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

#ifndef LINK_MEASUREMENT_PROTOCOL_H
#define LINK_MEASUREMENT_PROTOCOL_H

#include "link-measurement-request.h"
#include "link-measurement-report.h"

#include "ns3/wifi-phy.h"
#include "ns3/nstime.h"
#include "ns3/wifi-net-device.h"

#include <map>

namespace ns3
{

class WifiMac;
class WifiPhy;
class WifiRemoteStationManager;
class UnifiedPhySniffer;
struct ParsedFrameContext;

/**
 * \ingroup wifi
 * \brief Link Measurement Protocol for IEEE 802.11k
 *
 * This class implements the Link Measurement protocol defined in IEEE 802.11k-2016.
 * It allows STAs and APs to measure and report link quality metrics including RSSI,
 * SNR, transmit power, and link margin.
 *
 * The protocol is installed on a WiFi device and hooks into the MAC layer to
 * send/receive Link Measurement Request and Report frames. Measurement reports
 * are fired via trace sources for use in simulations.
 */
class LinkMeasurementProtocol : public Object
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
    LinkMeasurementProtocol();

    /**
     * \brief Destructor
     */
    ~LinkMeasurementProtocol() override;

    /**
     * \brief Install the protocol on a WiFi device
     * \param device the WiFi device to install on
     *
     * This creates a new UnifiedPhySniffer for the device. For shared sniffer
     * usage, prefer InstallWithSniffer() instead.
     */
    void Install(Ptr<WifiNetDevice> device);

    /**
     * \brief Install the protocol on a WiFi device with an external sniffer
     * \param device the WiFi device to install on
     * \param sniffer the UnifiedPhySniffer to use (singleton pattern)
     *
     * Use this when installing via LinkMeasurementProtocolHelper to share
     * the sniffer with other 802.11k modules.
     */
    void InstallWithSniffer(Ptr<WifiNetDevice> device, Ptr<UnifiedPhySniffer> sniffer);

    /**
     * \brief Send a Link Measurement Request to a peer
     * \param to the MAC address of the peer (AP or STA)
     * \param transmitPowerUsed the transmit power used (dBm) for measurements
     * \param maxTransmitPower the maximum transmit power (dBm) capability
     * \param includeEsseData whether to include ESS data (optional)
     *
     * This initiates a Link Measurement exchange. The peer will respond with a
     * Link Measurement Report, which will be available via the LinkMeasurementReportReceived
     * trace source.
     */
    void SendLinkMeasurementRequest(Mac48Address to,
                                    int8_t transmitPowerUsed,
                                    int8_t maxTransmitPower,
                                    bool includeEsseData = false);
    void ReceiveFromPhy(std::string context,
                        Ptr<const Packet> packet,
                        uint16_t channelFreq,
                        WifiTxVector txVector,
                        MpduInfo mpdu,
                        SignalNoiseDbm signalNoise,
                        uint16_t staId);

    /**
     * \brief Trace source type for Link Measurement Request received
     *
     * Fires when a Link Measurement Request is received. This trace allows monitoring requests.
     *
     * Typedef for: TracedCallback<Mac48Address, Ptr<const LinkMeasurementRequest>>
     */
    typedef void (*LinkMeasurementRequestCallback)(Mac48Address from,
                                                    const LinkMeasurementRequest& request);


    /**
     * \brief Trace source type for Link Measurement Report received
     *
     * Fires when a Link Measurement Report is received from a peer.
     * This is the primary trace source used in simulations to collect measurement data.
     *
     * Parameters:
     * - Mac48Address: source MAC address of the peer
     * - Ptr<const LinkMeasurementReport>: the measurement report
     *
     * Typedef for: TracedCallback<Mac48Address, Ptr<const LinkMeasurementReport>>
     */
    typedef void (*LinkMeasurementReportCallback)(Mac48Address from,
                                                   const LinkMeasurementReport& report);

  private:
    /**
     * \brief Handle received Link Measurement Request
     * \param from the source address
     * \param request the request object
     */
    void HandleLinkMeasurementRequest(Mac48Address from, LinkMeasurementRequest request);
    void HandleLinkMeasurementReport(Mac48Address from, LinkMeasurementReport report);

    /**
     * \brief Send a Link Measurement Report in response to a request
     * \param to the peer's MAC address
     * \param dialogToken the dialog token from the request
     */
    void SendLinkMeasurementReport(Mac48Address to, LinkMeasurementRequest request);

    /**
     * \brief Measure current RCPI (Received Channel Power Indicator)
     * \param from the peer's MAC address
     * \return RCPI value in 802.11k format (0-220, approximately -110 to +0 dBm)
     *
     * RCPI is derived from the most recent received frame's RSSI.
     */
    uint8_t MeasureRcpi(Mac48Address from);

    /**
     * \brief Measure current RSNI (Received Signal to Noise Indicator)
     * \param from the peer's MAC address
     * \return RSNI value in 802.11k format (0-255, approximately 0 to 127.5 dB)
     *
     * RSNI is the ratio of received signal power to noise power on the channel.
     */
    uint8_t MeasureRsni(Mac48Address from);

    /**
     * \brief Calculate link margin
     * \param from the peer's MAC address
     * \return link margin in dB
     *
     * Link margin is the difference between current received power and the
     * receiver's minimum sensitivity threshold.
     */
    uint8_t CalculateLinkMargin(Mac48Address from);

    /**
     * \brief Get current transmit power
     * \return transmit power in dBm
     */
    int8_t GetCurrentTransmitPower();

    /**
     * \brief Convert dBm to RCPI format
     * \param dbm power in dBm
     * \return RCPI value (0-220)
     *
     * RCPI = (power_dbm + 110) * 2, clamped to [0, 220]
     */
    static uint8_t ConvertToRcpi(double dbm);

    /**
     * \brief Convert dBm to RSNI format
     * \param snr SNR in dB
     * \return RSNI value (0-255)
     *
     * RSNI = snr * 2, clamped to [0, 255]
     */
    static uint8_t ConvertToRsni(double snr);

    // Trace sources
    TracedCallback<Mac48Address, LinkMeasurementRequest> m_requestReceivedTrace;
    TracedCallback<Mac48Address, LinkMeasurementReport> m_reportReceivedTrace;


    // State management
    Ptr<WifiNetDevice> m_device;
    Ptr<WifiMac> m_mac;
    Ptr<WifiPhy> m_phy;
    // Ptr<WifiRemoteStationManager> m_stationManager;

    // Dialog token management
    uint8_t m_nextDialogToken;
    // std::map<uint8_t, Mac48Address> m_pendingRequests; ///< Maps dialog token to peer address

    // Cache for recent measurements (from PHY layer)
    struct PeerMeasurement
    {
        double lastRssi;      ///< Last received RSSI in dBm
        double lastSnr;       ///< Last received SNR in dB
        Time lastMeasureTime; ///< Timestamp of last measurement
    };

    std::map<Mac48Address, PeerMeasurement> m_peerMeasurements;

    // Configuration
    bool m_includeEsseData; ///< Whether to include ESS data in requests
    Time m_measurementCacheTtl; ///< Time-to-live for cached measurements

    // Unified sniffer integration
    Ptr<UnifiedPhySniffer> m_sniffer; ///< Unified PHY sniffer for efficient callback handling
    uint32_t m_snifferSubscriptionId; ///< Subscription ID for unsubscribe

    /**
     * \brief Unified sniffer callback for Category 5 (802.11k) action frames
     * \param ctx Pre-parsed frame context with signal measurements
     *
     * This replaces the old ReceiveFromPhy callback with efficient pre-parsed data.
     */
    void UnifiedActionCallback(ParsedFrameContext* ctx);
};

} // namespace ns3

#endif /* LINK_MEASUREMENT_PROTOCOL_H */
