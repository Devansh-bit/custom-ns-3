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

#include "link-measurement-protocol.h"

#include "ns3/config.h"
#include <sstream>
#include "ns3/log.h"
#include "ns3/mac48-address.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy.h"
#include "ns3/qos-txop.h"
#include "ns3/unified-phy-sniffer.h"  // Unified sniffer for efficient callback handling
#include "ns3/unified-phy-sniffer-helper.h"  // For GetOrInstall singleton pattern
#include "ns3/parsed-frame-context.h"

#include <algorithm>
#include <cmath>


namespace ns3
{

NS_LOG_COMPONENT_DEFINE("LinkMeasurementProtocol");
NS_OBJECT_ENSURE_REGISTERED(LinkMeasurementProtocol);

TypeId
LinkMeasurementProtocol::GetTypeId()
{
    static TypeId tid = TypeId("ns3::LinkMeasurementProtocol")
                            .SetParent<Object>()
                            .SetGroupName("LinkProtocol11k")
                            .AddConstructor<LinkMeasurementProtocol>()
                            .AddAttribute("IncludeEsseData",
                                        "Whether to include ESS data in requests",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&LinkMeasurementProtocol::m_includeEsseData),
                                        MakeBooleanChecker())
                            .AddAttribute("MeasurementCacheTtl",
                                        "Time-to-live for cached measurements",
                                        TimeValue(MilliSeconds(100)),
                                        MakeTimeAccessor(&LinkMeasurementProtocol::m_measurementCacheTtl),
                                        MakeTimeChecker())
                            .AddTraceSource("LinkMeasurementRequestReceived",
                                "Link Measurement Request received",
                                         MakeTraceSourceAccessor(&LinkMeasurementProtocol::m_requestReceivedTrace),
                                         "ns3::LinkMeasurementProtocol::LinkMeasurementRequestCallback")
                            .AddTraceSource("LinkMeasurementReportReceived",
                                 "Link Measurement Report received",
                                         MakeTraceSourceAccessor(&LinkMeasurementProtocol::m_reportReceivedTrace),
                              "ns3::LinkMeasurementProtocol::LinkMeasurementReportCallback");

    return tid;
}

LinkMeasurementProtocol::LinkMeasurementProtocol()
    : m_device(nullptr),
      m_mac(nullptr),
      m_phy(nullptr),
      m_nextDialogToken(1),
      m_includeEsseData(false),
      m_measurementCacheTtl(MilliSeconds(100))
{
    NS_LOG_FUNCTION(this);
}

LinkMeasurementProtocol::~LinkMeasurementProtocol()
{
    NS_LOG_FUNCTION(this);
    m_device = nullptr;
    m_mac = nullptr;
    m_phy = nullptr;
    m_peerMeasurements.clear();
}

void
LinkMeasurementProtocol::Install(Ptr<WifiNetDevice> device)
{
    NS_LOG_FUNCTION(this << device);

    // Use singleton pattern via UnifiedPhySnifferHelper::GetOrInstall
    Ptr<UnifiedPhySniffer> sniffer = UnifiedPhySnifferHelper::GetOrInstall(device);
    InstallWithSniffer(device, sniffer);
}

void
LinkMeasurementProtocol::InstallWithSniffer(Ptr<WifiNetDevice> device, Ptr<UnifiedPhySniffer> sniffer)
{
    NS_LOG_FUNCTION(this << device << sniffer);
    NS_ASSERT_MSG(device, "Device cannot be null");
    NS_ASSERT_MSG(sniffer, "Sniffer cannot be null");

    m_device = device;
    m_mac = device->GetMac();
    m_phy = device->GetPhy();

    NS_ASSERT_MSG(m_mac, "MAC cannot be null");
    NS_ASSERT_MSG(m_phy, "PHY cannot be null");

    // Use the provided sniffer (singleton pattern)
    m_sniffer = sniffer;

    // Subscribe to Category 5 (802.11k Radio Measurement) action frames only
    m_snifferSubscriptionId = m_sniffer->SubscribeAction(5, [this](ParsedFrameContext* ctx) {
        this->UnifiedActionCallback(ctx);
    });

    NS_LOG_DEBUG("LinkMeasurementProtocol installed on device " << device->GetAddress()
                 << " with UnifiedPhySniffer (subscription ID=" << m_snifferSubscriptionId << ")");
}



void
LinkMeasurementProtocol::SendLinkMeasurementRequest(Mac48Address to,
                                                    int8_t transmitPowerUsed,
                                                    int8_t maxTransmitPower,
                                                    bool includeEsseData)
{
    NS_LOG_FUNCTION(this << to << static_cast<int16_t>(transmitPowerUsed)
                         << static_cast<int16_t>(maxTransmitPower) << includeEsseData);

    if (!m_mac)
    {
        NS_LOG_WARN("Protocol not installed on any device");
        return;
    }

    NS_LOG_DEBUG("Sending Link Measurement Request to " << to << " with dialog token "
                                                        << static_cast<uint16_t>(m_nextDialogToken));


    // Create packet with the request header
    auto packet = Create<Packet>();
    uint8_t frameHeader[5];
    frameHeader[0] = 5;
    frameHeader[1] = 2;
    frameHeader[2] = m_nextDialogToken;
    frameHeader[3] = LinkMeasurementReport::ConvertToTwosComplement(transmitPowerUsed);
    frameHeader[4] = LinkMeasurementReport::ConvertToTwosComplement(maxTransmitPower);
    packet->AddAtEnd(Create<Packet>(frameHeader, 5));
    NS_LOG_DEBUG("Adding Frame Header: Cat=5, Act=2");

    // Create MAC header for management frame
    WifiMacHeader hdr;
    hdr.SetType(WIFI_MAC_MGT_ACTION);
    hdr.SetAddr1(to);
    hdr.SetAddr2(m_mac->GetAddress());
    hdr.SetAddr3(to);
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();


    NS_LOG_DEBUG("Enqueued Link Measurement Request");
    Ptr<QosTxop> qosTxop = m_mac->GetQosTxop(AC_VO);
    qosTxop->Queue(Create<WifiMpdu>(packet, hdr));

    m_nextDialogToken++;
    if (m_nextDialogToken == 0)
    {
        m_nextDialogToken = 1; // Dialog token 0 is reserved
    }
    NS_LOG_DEBUG("Sent Link Measurement Request");
}

// OLD: ReceiveFromPhy - replaced by UnifiedActionCallback
// Kept for reference - this was the original callback approach
/*
void
LinkMeasurementProtocol::ReceiveFromPhy(std::string context,
                                  Ptr<const Packet> packet,
                                  uint16_t channelFreq,
                                  WifiTxVector txVector,
                                  MpduInfo mpdu,
                                  SignalNoiseDbm signalNoise,
                                  uint16_t staId)
{
    WifiMacHeader hdr;
    Ptr<Packet> copy = packet->Copy();
    uint32_t headerSize = copy->RemoveHeader(hdr);

    // Verify header was successfully removed
    if (headerSize == 0)
    {
        return;
    }

    // ===== STEP 1: Filter by frame type using SAFE methods =====
    // IsMgt() only checks m_ctrlType - no GetType() call, no assertion risk
    if (!hdr.IsMgt())
    {
        return;  // Skip all data and control frames
    }

    Mac48Address dst = hdr.GetAddr1();
    Mac48Address src = hdr.GetAddr2();

    if (dst != m_device->GetMac()->GetAddress())
    {
        return;
    }

    // Update peer measurements with actual PHY measurements
    double rssi = signalNoise.signal;
    double noise = signalNoise.noise;
    double snr = rssi - noise;

    auto it = m_peerMeasurements.find(src);
    if (it != m_peerMeasurements.end())
    {
        it->second.lastRssi = rssi;
        it->second.lastSnr = snr;
        it->second.lastMeasureTime = Simulator::Now();
    }
    else
    {
        m_peerMeasurements[src] = {rssi, snr, Simulator::Now()};
    }

    NS_LOG_DEBUG("Updated measurements for " << src << ": RSSI=" << rssi
                 << " dBm, Noise=" << noise << " dBm, SNR=" << snr << " dB");

    // ===== STEP 2: Extract and validate action frame category =====
    // Category field is first byte of action frame payload
    if (copy->GetSize() < 3)
    {
        return;  // Not enough data for category + action code + dialog token
    }

    uint8_t frameHeader[3];
    copy->CopyData(frameHeader, 3);
    uint8_t category = frameHeader[0];
    uint8_t actionCode = frameHeader[1];
    uint8_t dialogToken = frameHeader[2];

    // Only handle Radio Measurement (Category 5 for 802.11k Link Measurement)
    // Category 5 only exists in Action frames, so this implicitly validates IsAction()
    if (category != 5) return;
    NS_LOG_DEBUG("Action from " << src);

    if (actionCode == 2)
    {
        uint8_t requestFrameHeader[5];
        copy->CopyData(requestFrameHeader, 5);

        NS_LOG_DEBUG("Receiving Link Measurement Request from " << src);
        LinkMeasurementRequest request {
            src,
            dst,
            dialogToken,
            requestFrameHeader[3],
            requestFrameHeader[4],
            packet,
        };
        HandleLinkMeasurementRequest(src, request);
    }


    if (actionCode == 3)
    {
        uint8_t responseFrameHeader[11];
        copy->CopyData(responseFrameHeader, 11);
        uint8_t dialogToken = responseFrameHeader[2];
        uint8_t transmitPower = responseFrameHeader[5];
        uint8_t linkMargin = responseFrameHeader[6];
        uint8_t receiveAntennaId = responseFrameHeader[7];
        uint8_t transmitAntennaId = responseFrameHeader[8];
        uint8_t rcpi = responseFrameHeader[9];
        uint8_t rsni = responseFrameHeader[10];

        LinkMeasurementReport report {
            src,
            dst,
            dialogToken,
            transmitPower,
            linkMargin,
            receiveAntennaId,
            transmitAntennaId,
            rcpi,
            rsni
        };
        NS_LOG_DEBUG("Receiving Link Measurement Report from " << src);
        m_reportReceivedTrace(src, report);
    }


}
*/

// Stub for ReceiveFromPhy to satisfy header declaration (no longer used)
void
LinkMeasurementProtocol::ReceiveFromPhy(std::string context,
                                  Ptr<const Packet> packet,
                                  uint16_t channelFreq,
                                  WifiTxVector txVector,
                                  MpduInfo mpdu,
                                  SignalNoiseDbm signalNoise,
                                  uint16_t staId)
{
    // Deprecated: Use UnifiedActionCallback instead
    NS_LOG_WARN("ReceiveFromPhy called but deprecated - should use UnifiedActionCallback");
}

void
LinkMeasurementProtocol::UnifiedActionCallback(ParsedFrameContext* ctx)
{
    // Use pre-parsed data from unified sniffer
    // - ctx->actionCategory already verified as 5 by subscription filter
    // - ctx->rssi, ctx->snr, ctx->rcpi, ctx->rsni pre-computed
    // - ctx->addr1, ctx->addr2, ctx->addr3 available via EnsureAddressesParsed()

    ctx->EnsureAddressesParsed();
    ctx->EnsureActionParsed();

    Mac48Address dst = ctx->addr1;  // Destination
    Mac48Address src = ctx->addr2;  // Source/Transmitter

    // Filter: only process frames destined to us
    if (dst != m_device->GetMac()->GetAddress())
    {
        return;
    }

    // Update peer measurements with pre-computed signal metrics
    auto it = m_peerMeasurements.find(src);
    if (it != m_peerMeasurements.end())
    {
        it->second.lastRssi = ctx->rssi;
        it->second.lastSnr = ctx->snr;
        it->second.lastMeasureTime = Simulator::Now();
    }
    else
    {
        m_peerMeasurements[src] = {ctx->rssi, ctx->snr, Simulator::Now()};
    }

    NS_LOG_DEBUG("Updated measurements for " << src << ": RSSI=" << ctx->rssi
                 << " dBm, Noise=" << ctx->noise << " dBm, SNR=" << ctx->snr << " dB");

    // Handle based on action code (already parsed in ctx)
    uint8_t actionCode = ctx->actionCode;
    uint8_t dialogToken = ctx->dialogToken;

    NS_LOG_DEBUG("Action from " << src << " (code=" << (int)actionCode << ")");

    if (actionCode == 2)  // Link Measurement Request
    {
        // Need payload for additional fields - get copy only when needed
        Ptr<Packet> payload = ctx->GetPayloadCopy();
        if (payload && payload->GetSize() >= 5)
        {
            uint8_t requestFrameHeader[5];
            payload->CopyData(requestFrameHeader, 5);

            NS_LOG_DEBUG("Receiving Link Measurement Request from " << src);
            LinkMeasurementRequest request {
                src,
                dst,
                dialogToken,
                requestFrameHeader[3],
                requestFrameHeader[4],
                ctx->originalPacket,
            };
            HandleLinkMeasurementRequest(src, request);
        }
    }
    else if (actionCode == 3)  // Link Measurement Report
    {
        // Need payload for report fields
        Ptr<Packet> payload = ctx->GetPayloadCopy();
        if (payload && payload->GetSize() >= 11)
        {
            uint8_t responseFrameHeader[11];
            payload->CopyData(responseFrameHeader, 11);

            uint8_t transmitPower = responseFrameHeader[5];
            uint8_t linkMargin = responseFrameHeader[6];
            uint8_t receiveAntennaId = responseFrameHeader[7];
            uint8_t transmitAntennaId = responseFrameHeader[8];
            uint8_t rcpi = responseFrameHeader[9];
            uint8_t rsni = responseFrameHeader[10];

            LinkMeasurementReport report {
                src,
                dst,
                dialogToken,
                transmitPower,
                linkMargin,
                receiveAntennaId,
                transmitAntennaId,
                rcpi,
                rsni
            };
            NS_LOG_DEBUG("Receiving Link Measurement Report from " << src);
            m_reportReceivedTrace(src, report);
        }
    }
}



void
LinkMeasurementProtocol::HandleLinkMeasurementRequest(Mac48Address from, LinkMeasurementRequest request)
{
    NS_LOG_FUNCTION(this << from);

    // Fire trace source for monitoring
    m_requestReceivedTrace(from, request);

    NS_LOG_DEBUG("Handling Link Measurement Request from " << from);

    // Automatically send a response
    SendLinkMeasurementReport(from, request);
}

void
LinkMeasurementProtocol::HandleLinkMeasurementReport(Mac48Address from,
                                                     const LinkMeasurementReport report)
{
    NS_LOG_FUNCTION(this << from);

    // Fire trace source - this is the primary callback for simulation data collection
    m_reportReceivedTrace(from, report);
}

void
LinkMeasurementProtocol::SendLinkMeasurementReport(Mac48Address to, LinkMeasurementRequest request)
{
    NS_LOG_FUNCTION(this << to);

    if (!m_mac)
    {
        NS_LOG_WARN("Protocol not installed on any device");
        return;
    }

    // Create the report on the stack
    uint8_t dialogToken = request.GetDialogToken();
    uint8_t rcpi = MeasureRcpi(to);
    uint8_t rsni = MeasureRsni(to);
    uint8_t linkMargin = CalculateLinkMargin(to);
    int8_t transmitPowerSigned = GetCurrentTransmitPower();
    uint8_t transmitPower = LinkMeasurementReport::ConvertToTwosComplement(transmitPowerSigned);

    Mac48Address from = m_device->GetMac()->GetAddress();

    LinkMeasurementReport report {
        from,
        to,
        dialogToken,
        transmitPower,
        linkMargin,
        255,
        255,
        rcpi,
        rsni,
    };

    auto packet = Create<Packet>();
    uint8_t frameHeader[11];
    frameHeader[0] = 5;
    frameHeader[1] = 3;
    frameHeader[2] = report.GetDialogToken();
    frameHeader[3] = report.GetTpcReport().elementID;
    frameHeader[4] = report.GetTpcReport().length;
    frameHeader[5] = report.GetTpcReport().transmitPower;
    frameHeader[6] = report.GetTpcReport().linkMargin;
    frameHeader[7] = report.GetReceiveAntennaId();
    frameHeader[8] = report.GetTransmitAntennaId();
    frameHeader[9] = report.GetRcpi();
    frameHeader[10] = report.GetRsni();
    packet->AddAtEnd(Create<Packet>(frameHeader, 11));
    NS_LOG_DEBUG("Adding Frame Header: Cat=5, Act=3");

    // Create MAC header for management frame
    WifiMacHeader hdr;
    hdr.SetType(WIFI_MAC_MGT_ACTION);
    hdr.SetAddr1(to);
    hdr.SetAddr2(from);
    hdr.SetAddr3(to);
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();


    NS_LOG_DEBUG("Enqueued Link Measurement Report");
    Ptr<QosTxop> qosTxop = m_mac->GetQosTxop(AC_VO);
    qosTxop->Queue(Create<WifiMpdu>(packet, hdr));

    NS_LOG_DEBUG("Sent Link Measurement Report");
}


uint8_t
LinkMeasurementProtocol::MeasureRcpi(Mac48Address from)
{
    NS_LOG_FUNCTION(this << from);

    double rssi = -110.0; // Default minimum value if no measurement available

    // Check if measurement is cached
    auto it = m_peerMeasurements.find(from);
    if (it != m_peerMeasurements.end())
    {
        rssi = it->second.lastRssi;
        NS_LOG_DEBUG("Using measured RSSI for " << from << ": " << rssi << " dBm");
    }
    else
    {
        NS_LOG_WARN("No measurement available for " << from << ", using default RSSI");
    }

    uint8_t rcpi = ConvertToRcpi(rssi);
    NS_LOG_DEBUG("Measured RCPI for " << from << ": " << static_cast<uint16_t>(rcpi)
                                     << " (from " << rssi << " dBm)");

    return rcpi;
}

uint8_t
LinkMeasurementProtocol::MeasureRsni(Mac48Address from)
{
    NS_LOG_FUNCTION(this << from);

    double snr = 0.0; // Default minimum value if no measurement available

    // Check if measurement is cached
    auto it = m_peerMeasurements.find(from);
    if (it != m_peerMeasurements.end())
    {
        snr = it->second.lastSnr;
        NS_LOG_DEBUG("Using measured SNR for " << from << ": " << snr << " dB");
    }
    else
    {
        NS_LOG_WARN("No measurement available for " << from << ", using default SNR");
    }

    uint8_t rsni = ConvertToRsni(snr);
    NS_LOG_DEBUG("Measured RSNI for " << from << ": " << static_cast<uint16_t>(rsni)
                                     << " (from SNR=" << snr << " dB)");

    return rsni;
}

uint8_t
LinkMeasurementProtocol::CalculateLinkMargin(Mac48Address from)
{
    NS_LOG_FUNCTION(this << from);

    // Link margin = RX power - RX sensitivity
    // RX sensitivity is typically the CCA-ED threshold
    double rxSensitivity = m_phy->GetCcaEdThreshold(); // Typically -62 dBm

    // Get current RSSI for this peer
    auto it = m_peerMeasurements.find(from);
    double rssi = -110.0; // Default if not cached
    if (it != m_peerMeasurements.end())
    {
        rssi = it->second.lastRssi;
    }

    double linkMargin = rssi - rxSensitivity;
    linkMargin = std::max(0.0, std::min(linkMargin, 255.0)); // Clamp to valid range

    NS_LOG_DEBUG("Link margin to " << from << ": " << linkMargin << " dB"
                                  << " (RSSI=" << rssi << " dBm, RxSensitivity="
                                  << rxSensitivity << " dBm)");

    return static_cast<uint8_t>(linkMargin);
}

int8_t
LinkMeasurementProtocol::GetCurrentTransmitPower()
{
    NS_LOG_FUNCTION(this);

    if (!m_phy)
    {
        NS_LOG_WARN("PHY not available, using default 20 dBm");
        return 20; // Default 20 dBm
    }

    // Get configured TX power
    double txPower = m_phy->GetTxPowerStart();
    txPower = std::max(-128.0, std::min(txPower, 127.0)); // Clamp to int8_t range

    NS_LOG_DEBUG("Current TX power: " << txPower << " dBm");

    return static_cast<int8_t>(txPower);
}

uint8_t
LinkMeasurementProtocol::ConvertToRcpi(double dbm)
{
    NS_LOG_FUNCTION(dbm);

    // RCPI formula: RCPI = 2 * (RSSI + 110)
    // Where RSSI is in dBm and result is in the range [0, 220]
    // -110 dBm -> RCPI = 0
    // 0 dBm -> RCPI = 220
    double rcpi = 2.0 * (dbm + 110.0);
    rcpi = std::max(0.0, std::min(rcpi, 220.0)); // Clamp to valid range

    return static_cast<uint8_t>(rcpi);
}

uint8_t
LinkMeasurementProtocol::ConvertToRsni(double snr)
{
    NS_LOG_FUNCTION(snr);

    // RSNI formula: RSNI = 2 * SNR
    // Where SNR is in dB and result is in the range [0, 255]
    // 0 dB SNR -> RSNI = 0
    // 127.5 dB SNR -> RSNI = 255
    double rsni = 2.0 * snr;
    rsni = std::max(0.0, std::min(rsni, 255.0)); // Clamp to valid range

    return static_cast<uint8_t>(rsni);
}

} // namespace ns3
