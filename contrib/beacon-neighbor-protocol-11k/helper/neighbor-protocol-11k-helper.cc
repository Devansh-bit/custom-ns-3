#include "neighbor-protocol-11k-helper.h"

#include "ns3/ap-wifi-mac.h"
#include "ns3/log.h"
#include "ns3/qos-txop.h"
#include "ns3/simulator.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/wifi-net-device.h"
#include "ns3/unified-phy-sniffer.h"     // Unified sniffer for efficient callback handling
#include "ns3/unified-phy-sniffer-helper.h"  // For GetOrInstall singleton pattern
#include "ns3/parsed-frame-context.h"

#include <arpa/inet.h>
#include <iostream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NeighborProtocolHelper");
NS_OBJECT_ENSURE_REGISTERED(NeighborProtocolHelper);

TypeId
NeighborProtocolHelper::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::NeighborProtocolHelper")
            .SetParent<Object>()
            .SetGroupName("Wifi")
            .AddTraceSource(
                "NeighborReportReceived",
                "Fired when STA receives neighbor report",
                MakeTraceSourceAccessor(&NeighborProtocolHelper::m_neighborReportReceivedTrace),
                "ns3::NeighborProtocolHelper::NeighborReportCallback");
    return tid;
}

NeighborProtocolHelper::NeighborProtocolHelper()
    : m_externalDualPhy(false)
{
    NS_LOG_FUNCTION(this);
    m_dualPhySniffer = new DualPhySnifferHelper();
}

NeighborProtocolHelper::~NeighborProtocolHelper()
{
    NS_LOG_FUNCTION(this);
    // Only delete if we created it internally
    if (!m_externalDualPhy)
    {
        delete m_dualPhySniffer;
    }
}

void
NeighborProtocolHelper::SetNeighborTable(std::vector<ApInfo> table)
{
    NS_LOG_FUNCTION(this << table.size());
    m_neighborTable = table;
}

// Configuration methods for dual-PHY sniffer
void
NeighborProtocolHelper::SetChannel(Ptr<YansWifiChannel> channel)
{
    NS_LOG_FUNCTION(this);
    m_dualPhySniffer->SetChannel(channel);
}

void
NeighborProtocolHelper::SetScanningChannels(const std::vector<uint8_t>& channels)
{
    NS_LOG_FUNCTION(this);
    m_dualPhySniffer->SetScanningChannels(channels);
}

void
NeighborProtocolHelper::SetHopInterval(Time interval)
{
    NS_LOG_FUNCTION(this);
    m_dualPhySniffer->SetHopInterval(interval);
}

void
NeighborProtocolHelper::SetDualPhySniffer(DualPhySnifferHelper* sniffer)
{
    NS_LOG_FUNCTION(this);

    // Delete internal dual-PHY if we created one
    if (!m_externalDualPhy && m_dualPhySniffer)
    {
        delete m_dualPhySniffer;
    }

    // Use external dual-PHY
    m_dualPhySniffer = sniffer;
    m_externalDualPhy = true;

    NS_LOG_INFO("NeighborProtocolHelper configured to use external dual-PHY sniffer");
}

void
NeighborProtocolHelper::InstallOnAp(Ptr<WifiNetDevice> apDevice)
{
    NS_LOG_FUNCTION(this << apDevice);
    m_apDevice = apDevice;

    uint32_t nodeId = apDevice->GetNode()->GetId();

    // Find the device index of apDevice in the node's device list
    uint32_t deviceId = 0;
    for (uint32_t i = 0; i < apDevice->GetNode()->GetNDevices(); i++)
    {
        if (apDevice->GetNode()->GetDevice(i) == apDevice)
        {
            deviceId = i;
            break;
        }
    }

    // OLD: Config::Connect approach (commented out - replaced with unified sniffer)
    // std::stringstream ss;
    // ss << "/NodeList/" << nodeId << "/DeviceList/" << deviceId << "/$ns3::WifiNetDevice/Phy/MonitorSnifferRx";
    // Config::Connect(ss.str(), MakeCallback(&NeighborProtocolHelper::ApSniffer, this));

    // Use GetOrInstall for singleton pattern - shares sniffer with other modules
    m_apSniffer = UnifiedPhySnifferHelper::GetOrInstall(apDevice);

    // Subscribe to Category 5 (802.11k) action frames for neighbor report requests
    m_apSnifferSubscriptionId = m_apSniffer->SubscribeAction(5, [this](ParsedFrameContext* ctx) {
        this->UnifiedApActionCallback(ctx);
    });

    NS_LOG_DEBUG("[NeighborProtocol::InstallOnAp] Subscribed to sniffer on Node " << nodeId
                 << " Device " << deviceId << " (subscription=" << m_apSnifferSubscriptionId << ")");

    if (m_externalDualPhy)
    {
        // External dual-PHY provided - use existing device's MAC
        m_dualPhyOperatingMac = apDevice->GetMac()->GetAddress();
        NS_LOG_INFO("✓ Neighbor protocol using external dual-PHY sniffer on Node " << nodeId);
        NS_LOG_INFO("  Operating MAC: " << m_dualPhyOperatingMac);
    }
    else
    {
        // Create and install dual-PHY sniffer on the AP node
        Mac48Address desiredBssid = apDevice->GetMac()->GetAddress();
        uint8_t operatingChannel = 36; // Will be set dynamically from device
        m_dualPhySniffer->Install(apDevice->GetNode(), operatingChannel, desiredBssid);

        // Retrieve the actual operating MAC assigned by the dual-PHY sniffer
        m_dualPhyOperatingMac = m_dualPhySniffer->GetOperatingMac(nodeId);

        // Start channel hopping immediately for continuous background scanning
        m_dualPhySniffer->StartChannelHopping();

        NS_LOG_INFO("✓ Neighbor protocol dual-PHY sniffer installed on Node " << nodeId);
        NS_LOG_INFO("  Dual-PHY Operating MAC: " << m_dualPhyOperatingMac);
        NS_LOG_INFO("  Channel hopping started for continuous neighbor monitoring");
    }
}

void
NeighborProtocolHelper::InstallOnSta(Ptr<WifiNetDevice> staDevice)
{
    NS_LOG_FUNCTION(this << staDevice);
    m_staDevice = staDevice;

    uint32_t nodeId = staDevice->GetNode()->GetId();
    uint32_t deviceId = staDevice->GetIfIndex();

    // OLD: Config::Connect approach (commented out - replaced with unified sniffer)
    // std::stringstream ss;
    // ss << "/NodeList/" << nodeId << "/DeviceList/" << deviceId << "/$ns3::WifiNetDevice/Phy/MonitorSnifferRx";
    // Config::Connect(ss.str(), MakeCallback(&NeighborProtocolHelper::StaSniffer, this));

    // Use GetOrInstall for singleton pattern - shares sniffer with other modules
    m_staSniffer = UnifiedPhySnifferHelper::GetOrInstall(staDevice);

    // Subscribe to Category 5 (802.11k) action frames for neighbor report responses
    m_staSnifferSubscriptionId = m_staSniffer->SubscribeAction(5, [this](ParsedFrameContext* ctx) {
        this->UnifiedStaActionCallback(ctx);
    });

    NS_LOG_INFO("✓ Neighbor protocol subscribed to sniffer on Node " << nodeId << " Device " << deviceId
                << " (subscription=" << m_staSnifferSubscriptionId << ")");
}

// OLD: ApSniffer - replaced by UnifiedApActionCallback
// Kept for reference
/*
void
NeighborProtocolHelper::ApSniffer(std::string context,
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

    if (headerSize == 0) return;
    if (!hdr.IsMgt()) return;

    Mac48Address dst = hdr.GetAddr1();
    Mac48Address src = hdr.GetAddr2();

    if (dst != m_apDevice->GetMac()->GetAddress()) return;
    if (copy->GetSize() < 3) return;

    uint8_t frameHeader[3];
    copy->CopyData(frameHeader, 3);

    uint8_t category = frameHeader[0];
    uint8_t actionCode = frameHeader[1];

    if (category != 5) return;

    if (actionCode == 0) {
        Ptr<Packet> reqCopy = packet->Copy();
        WifiMacHeader reqHdr;
        reqCopy->RemoveHeader(reqHdr);
        Simulator::ScheduleNow(&NeighborProtocolHelper::ProcessNeighborReportRequest,
                               this, m_apDevice, reqCopy, src);
    }
}
*/

// Stub for ApSniffer (no longer used)
void
NeighborProtocolHelper::ApSniffer(std::string context,
                                  Ptr<const Packet> packet,
                                  uint16_t channelFreq,
                                  WifiTxVector txVector,
                                  MpduInfo mpdu,
                                  SignalNoiseDbm signalNoise,
                                  uint16_t staId)
{
    NS_LOG_WARN("ApSniffer called but deprecated - should use UnifiedApActionCallback");
}

// NEW: Unified callback for AP - receives neighbor report requests (Category 5, Action 0)
void
NeighborProtocolHelper::UnifiedApActionCallback(ParsedFrameContext* ctx)
{
    ctx->EnsureAddressesParsed();
    ctx->EnsureActionParsed();

    Mac48Address dst = ctx->addr1;
    Mac48Address src = ctx->addr2;

    // Only process frames destined to this AP
    if (dst != m_apDevice->GetMac()->GetAddress())
    {
        return;
    }

    // Neighbor Report Request (action code 0)
    if (ctx->actionCode == 0)
    {
        // Get payload copy for request parsing
        Ptr<Packet> reqCopy = ctx->GetPayloadCopy();
        if (reqCopy)
        {
            Simulator::ScheduleNow(&NeighborProtocolHelper::ProcessNeighborReportRequest,
                                   this,
                                   m_apDevice,
                                   reqCopy,
                                   src);
        }
    }
}

// OLD: StaSniffer - replaced by UnifiedStaActionCallback
// Kept for reference
/*
void
NeighborProtocolHelper::StaSniffer(std::string context,
                                   Ptr<const Packet> packet,
                                   uint16_t channelFreq,
                                   WifiTxVector txVector,
                                   MpduInfo mpdu,
                                   SignalNoiseDbm signalNoise,
                                   uint16_t staId)
{
    if (packet->GetSize() < 24) return;

    WifiMacHeader hdr;
    Ptr<Packet> copy = packet->Copy();
    uint32_t headerSize = copy->RemoveHeader(hdr);

    if (headerSize == 0) return;
    if (!hdr.IsMgt()) return;

    Mac48Address dst = hdr.GetAddr1();
    Mac48Address src = hdr.GetAddr2();

    if (dst != m_staDevice->GetMac()->GetAddress()) return;
    if (copy->GetSize() < 3) return;

    uint8_t frameHeader[3];
    copy->CopyData(frameHeader, 3);

    uint8_t category = frameHeader[0];
    uint8_t actionCode = frameHeader[1];

    if (category != 5) return;

    if (actionCode == 2) {
        Ptr<Packet> respCopy = packet->Copy();
        WifiMacHeader respHdr;
        respCopy->RemoveHeader(respHdr);
        Simulator::ScheduleNow(&NeighborProtocolHelper::ReceiveNeighborReportResponse,
                               this, respCopy, src);
    }
}
*/

// Stub for StaSniffer (no longer used)
void
NeighborProtocolHelper::StaSniffer(std::string context,
                                   Ptr<const Packet> packet,
                                   uint16_t channelFreq,
                                   WifiTxVector txVector,
                                   MpduInfo mpdu,
                                   SignalNoiseDbm signalNoise,
                                   uint16_t staId)
{
    NS_LOG_WARN("StaSniffer called but deprecated - should use UnifiedStaActionCallback");
}

// NEW: Unified callback for STA - receives neighbor report responses (Category 5, Action 2)
void
NeighborProtocolHelper::UnifiedStaActionCallback(ParsedFrameContext* ctx)
{
    ctx->EnsureAddressesParsed();
    ctx->EnsureActionParsed();

    Mac48Address dst = ctx->addr1;
    Mac48Address src = ctx->addr2;

    // Only process frames destined to this STA
    if (dst != m_staDevice->GetMac()->GetAddress())
    {
        return;
    }

    // Neighbor Report Response (action code 2)
    if (ctx->actionCode == 2)
    {
        // Get payload copy for response parsing
        Ptr<Packet> respCopy = ctx->GetPayloadCopy();
        if (respCopy)
        {
            Simulator::ScheduleNow(&NeighborProtocolHelper::ReceiveNeighborReportResponse,
                                   this,
                                   respCopy,
                                   src);
        }
    }
}

// ===== SEND NEIGHBOR REPORT REQUEST =====
void
NeighborProtocolHelper::SendNeighborReportRequest(Ptr<WifiNetDevice> staDevice,
                                                  Mac48Address apAddress)
{
    NS_LOG_FUNCTION(this << staDevice << apAddress);



    // Create 3-byte frame header manually
    Ptr<Packet> pkt = Create<Packet>();

    uint8_t frameHeader[3];
    frameHeader[0] = 5; // Category: Radio Measurement
    frameHeader[1] = 0; // Action: Neighbor Report Request
    frameHeader[2] = 1; // Dialog Token

    pkt->AddAtEnd(Create<Packet>(frameHeader, 3));


    WifiMacHeader hdr;
    hdr.SetType(WIFI_MAC_MGT_ACTION);
    hdr.SetAddr1(apAddress);
    hdr.SetAddr2(staDevice->GetMac()->GetAddress());
    hdr.SetAddr3(apAddress);
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();

    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(staDevice->GetMac());
    Ptr<QosTxop> qosTxop = staMac->GetQosTxop(AC_VO);
    qosTxop->Queue(Create<WifiMpdu>(pkt, hdr));

}
} // namespace ns3

// ===== PROCESS NEIGHBOR REPORT REQUEST =====
void
NeighborProtocolHelper::ProcessNeighborReportRequest(Ptr<WifiNetDevice> apDevice,
                                                     Ptr<const Packet> packet,
                                                     Mac48Address staAddress)
{
    NS_LOG_FUNCTION(this << apDevice << staAddress);


    // Parse request frame header (3 bytes only)
    Ptr<Packet> reqCopy = packet->Copy();
    uint8_t reqHeader[3];
    if (reqCopy->GetSize() < 3)
    {
        return;
    }
    reqCopy->CopyData(reqHeader, 3);
    uint8_t dialogToken = reqHeader[2];




    // Clear old discovered neighbors
    m_discoveredNeighbors.clear();

    // Query all beacons received by this AP's dual-PHY sniffer
    Mac48Address receiverMac = m_dualPhyOperatingMac;
    std::vector<BeaconInfo> receivedBeacons = m_dualPhySniffer->GetBeaconsReceivedBy(receiverMac);


    // DEBUG: Show ALL beacons in the cache
    for (size_t i = 0; i < receivedBeacons.size(); i++)
    {
        // Placeholder for debug logging if needed
        (void)receivedBeacons[i];
    }

    // Convert BeaconInfo to ApInfo and store in discovered neighbors
    for (const auto& beacon : receivedBeacons)
    {
        ApInfo neighbor;
        neighbor.bssid = beacon.bssid;
        neighbor.channel = beacon.channel;
        neighbor.regulatoryClass = 115; // Default for 5GHz
        neighbor.phyType = 7;           // 802.11n

        m_discoveredNeighbors[beacon.bssid] = neighbor;


    }

    // Send response immediately (minimal delay for processing)
    Simulator::Schedule(MilliSeconds(5),
                        &NeighborProtocolHelper::SendNeighborReportResponse,
                        this,
                        apDevice,
                        staAddress,
                        dialogToken);
}

void
NeighborProtocolHelper::SendNeighborReportResponse(Ptr<WifiNetDevice> apDevice,
                                                   Mac48Address staAddress,
                                                   uint8_t dialogToken)
{
    // Use discovered neighbors, fallback to hardcoded table
    std::vector<ApInfo> neighborsToReport;

    if (!m_discoveredNeighbors.empty())
    {
        // Use dynamically discovered neighbors
        for (auto& [bssid, info] : m_discoveredNeighbors)
        {
            neighborsToReport.push_back(info);
        }
    }
    // } else {
    //     // Fallback to hardcoded table
    //     neighborsToReport = m_neighborTable;
    // }

    // Create response packet with 3-byte frame header
    Ptr<Packet> replyPacket = Create<Packet>();

    uint8_t frameHeader[3];
    frameHeader[0] = 5;           // Category: Radio Measurement
    frameHeader[1] = 2;           // Action: Neighbor Report Response
    frameHeader[2] = dialogToken; // Echo dialog token

    replyPacket->AddAtEnd(Create<Packet>(frameHeader, 3));


    // Add neighbor elements
    int elemCount = 0;
    for (auto& ap : neighborsToReport)
    {
        NeighborReportElementLegacy element;
        element.elementId = 52;
        element.length = 13;
        ap.bssid.CopyTo(element.bssid);

        uint32_t bssidInfo = 0x00000003;
        element.bssidInfo = htonl(bssidInfo);
        element.regulatoryClass = ap.regulatoryClass;
        element.channelNumber = ap.channel;
        element.phyType = ap.phyType;

        Ptr<Packet> elemPacket = Create<Packet>((uint8_t*)&element, sizeof(element));
        replyPacket->AddAtEnd(elemPacket);

        elemCount++;
    }

    // Send response
    WifiMacHeader hdr;
    hdr.SetType(WIFI_MAC_MGT_ACTION);
    hdr.SetAddr1(staAddress);
    hdr.SetAddr2(apDevice->GetMac()->GetAddress());
    hdr.SetAddr3(apDevice->GetMac()->GetAddress());
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();

    Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDevice->GetMac());
    Ptr<QosTxop> qosTxop = apMac->GetQosTxop(AC_VO);
    qosTxop->Queue(Create<WifiMpdu>(replyPacket, hdr));

}

// ===== RECEIVE NEIGHBOR REPORT RESPONSE =====
void
NeighborProtocolHelper::ReceiveNeighborReportResponse(Ptr<const Packet> packet,
                                                      Mac48Address apAddress)
{
    NS_LOG_FUNCTION(this << apAddress);

    static uint64_t receiveCount = 0;
    receiveCount++;



    Ptr<Packet> copy = packet->Copy();

    // CORRECT: Parse only 3-byte frame header
    uint8_t frameHeader[3];
    if (copy->GetSize() < 3)
    {
        return;
    }
    copy->CopyData(frameHeader, 3);
    copy->RemoveAtStart(3); // Remove ONLY 3 bytes!

    uint8_t category = frameHeader[0];
    uint8_t action = frameHeader[1];
    // dialogToken could be used for tracking requests/responses if needed
    // uint8_t dialogToken = frameHeader[2];



    if (category != 5 || action != 2)
    {
        return;
    }

    std::vector<Mac48Address> neighborList;
    std::vector<NeighborReportData> neighborReports;

    while (copy->GetSize() >= sizeof(NeighborReportElementLegacy))
    {
        NeighborReportElementLegacy element;
        copy->CopyData((uint8_t*)&element, sizeof(element));
        copy->RemoveAtStart(sizeof(element));

        // Verify element ID
        if (element.elementId != 52)
        {
            break;
        }

        Mac48Address bssid;
        bssid.CopyFrom(element.bssid);
        neighborList.push_back(bssid);

        // Create NeighborReportData
        NeighborReportData reportData;
        reportData.bssid = bssid;
        reportData.bssidInfo = ntohl(element.bssidInfo);  // Convert from network byte order
        reportData.channel = element.channelNumber;
        reportData.regulatoryClass = element.regulatoryClass;
        reportData.phyType = element.phyType;
        neighborReports.push_back(reportData);


    }


    // Store neighbor list
    StoreNeighborList(neighborList);

    // FIRE TRACE with full neighbor report data
    m_neighborReportReceivedTrace(m_staDevice->GetMac()->GetAddress(),
                                  apAddress,
                                  neighborReports);

}

// ===== STORE NEIGHBOR LIST =====
void
NeighborProtocolHelper::StoreNeighborList(std::vector<Mac48Address> neighbors)
{
    NS_LOG_FUNCTION(this << neighbors.size());

    m_neighborList.clear();
    for (auto& bssid : neighbors)
    {
        m_neighborList.insert(bssid);
    }


} // namespace ns3
