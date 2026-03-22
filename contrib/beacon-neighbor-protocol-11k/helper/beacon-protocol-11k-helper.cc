#include "beacon-protocol-11k-helper.h"

#include "ns3/ap-wifi-mac.h"
#include "ns3/log.h"
#include "ns3/qos-txop.h"
#include "ns3/simulator.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/wifi-net-device.h"
#include "ns3/unified-phy-sniffer.h"     // Unified sniffer for efficient callback handling
#include "ns3/unified-phy-sniffer-helper.h"  // For GetOrInstall singleton pattern
#include "ns3/parsed-frame-context.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("BeaconProtocolHelper");
NS_OBJECT_ENSURE_REGISTERED(BeaconProtocolHelper);

TypeId
BeaconProtocolHelper::GetTypeId()
{
    static TypeId tid = TypeId("ns3::BeaconProtocolHelper")
                            .SetParent<Object>()
                            .SetGroupName("Wifi")
                            .AddTraceSource("BeaconReportReceived",
                                            "Fired when AP receives beacon report",
                                            MakeTraceSourceAccessor(
                                                &BeaconProtocolHelper::m_beaconReportReceivedTrace),
                                            "ns3::BeaconProtocolHelper::BeaconReportCallback");
    return tid;
}

BeaconProtocolHelper::BeaconProtocolHelper()
    : m_apDevice(nullptr),
      m_staDevice(nullptr),
      m_isMeasuring(false),
      m_measurementStartTime(Seconds(0)),
      m_measurementDuration(Seconds(0)),
      m_useDualPhySniffer(false),  // NEW
      m_externalDualPhy(false)     // NEW
{
    NS_LOG_FUNCTION(this);
    m_dualPhySniffer = new DualPhySnifferHelper();  // NEW
}

BeaconProtocolHelper::~BeaconProtocolHelper()
{
    NS_LOG_FUNCTION(this);
    // Cancel timer if running
    if (m_measurementTimer.IsPending())
    {
        Simulator::Cancel(m_measurementTimer);
    }
    // Only delete if we own the dual-PHY instance
    if (!m_externalDualPhy && m_dualPhySniffer)
    {
        delete m_dualPhySniffer;
    }
}

void
BeaconProtocolHelper::SetNeighborList(std::set<Mac48Address> neighborList)
{
    NS_LOG_FUNCTION(this << neighborList.size());
    m_neighborList = neighborList;
}

// NEW: Dual-PHY sniffer configuration methods
void
BeaconProtocolHelper::SetChannel(Ptr<YansWifiChannel> channel)
{
    NS_LOG_FUNCTION(this);
    m_dualPhySniffer->SetChannel(channel);
    m_useDualPhySniffer = true;  // Enable dual-PHY mode
}

void
BeaconProtocolHelper::SetScanningChannels(const std::vector<uint8_t>& channels)
{
    NS_LOG_FUNCTION(this);
    m_dualPhySniffer->SetScanningChannels(channels);
}

void
BeaconProtocolHelper::SetHopInterval(Time interval)
{
    NS_LOG_FUNCTION(this);
    m_dualPhySniffer->SetHopInterval(interval);
}

void
BeaconProtocolHelper::SetDualPhySniffer(DualPhySnifferHelper* sniffer)
{
    NS_LOG_FUNCTION(this);
    // Delete our internal instance if we own it
    if (!m_externalDualPhy && m_dualPhySniffer)
    {
        delete m_dualPhySniffer;
    }
    // Use the external instance
    m_dualPhySniffer = sniffer;
    m_externalDualPhy = true;
    m_useDualPhySniffer = true;
    NS_LOG_INFO("✓ Beacon protocol using external dual-PHY sniffer instance");
}

void
BeaconProtocolHelper::InstallOnAp(Ptr<WifiNetDevice> apDevice)
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
    // Config::Connect(ss.str(), MakeCallback(&BeaconProtocolHelper::ApSniffer, this));

    // Use GetOrInstall for singleton pattern - shares sniffer with other modules
    m_apSniffer = UnifiedPhySnifferHelper::GetOrInstall(apDevice);

    // Subscribe to Category 5 (802.11k) action frames for beacon reports
    m_apSnifferSubscriptionId = m_apSniffer->SubscribeAction(5, [this](ParsedFrameContext* ctx) {
        this->UnifiedApActionCallback(ctx);
    });

    NS_LOG_INFO("✓ Beacon protocol subscribed to sniffer on Node " << nodeId << " Device " << deviceId
                << " (subscription=" << m_apSnifferSubscriptionId << ")");
}

void
BeaconProtocolHelper::InstallOnSta(Ptr<WifiNetDevice> staDevice)
{
    NS_LOG_FUNCTION(this << staDevice);
    m_staDevice = staDevice;

    uint32_t nodeId = staDevice->GetNode()->GetId();
    uint32_t deviceId = staDevice->GetIfIndex();

    // OLD: Config::Connect approach (commented out - replaced with unified sniffer)
    // std::stringstream ss;
    // ss << "/NodeList/" << nodeId << "/DeviceList/" << deviceId << "/$ns3::WifiNetDevice/Phy/MonitorSnifferRx";
    // NS_LOG_DEBUG("[BeaconProtocol::InstallOnSta] Connecting sniffer for Node " << nodeId << " Device " << deviceId << " (this=" << this << ", path=" << ss.str() << ")");
    // Config::Connect(ss.str(), MakeCallback(&BeaconProtocolHelper::StaSniffer, this));

    // Use GetOrInstall for singleton pattern - shares sniffer with other modules
    m_staSniffer = UnifiedPhySnifferHelper::GetOrInstall(staDevice);

    // Subscribe to beacons (for measurement when not using dual-PHY)
    m_staBeaconSubscriptionId = m_staSniffer->SubscribeBeacons([this](ParsedFrameContext* ctx) {
        this->UnifiedStaBeaconCallback(ctx);
    });

    // Subscribe to Category 5 action frames (beacon requests)
    m_staActionSubscriptionId = m_staSniffer->SubscribeAction(5, [this](ParsedFrameContext* ctx) {
        this->UnifiedStaActionCallback(ctx);
    });

    NS_LOG_DEBUG("[BeaconProtocol::InstallOnSta] Subscribed to sniffer on Node " << nodeId
                 << " Device " << deviceId << " (beacon sub=" << m_staBeaconSubscriptionId
                 << ", action sub=" << m_staActionSubscriptionId << ")");

    // NEW: Install dual-PHY sniffer if enabled
    if (m_useDualPhySniffer)
    {
        // Install dual-PHY sniffer on the STA node (First, Set Dual Phy sniffer should be called)
        Mac48Address desiredBssid = staDevice->GetMac()->GetAddress();
        uint8_t operatingChannel = 36; // Will be set from device's actual channel
        m_dualPhySniffer->Install(staDevice->GetNode(), operatingChannel, desiredBssid);

        // Retrieve the actual operating MAC assigned by the dual-PHY sniffer
        m_dualPhyOperatingMac = m_dualPhySniffer->GetOperatingMac(nodeId);

        // Start channel hopping immediately for continuous background scanning
        m_dualPhySniffer->StartChannelHopping();

        NS_LOG_INFO("✓ Beacon protocol dual-PHY sniffer installed on Node " << nodeId);
        NS_LOG_INFO("  Dual-PHY Operating MAC: " << m_dualPhyOperatingMac);
        NS_LOG_INFO("  Channel hopping started for continuous beacon monitoring");
    }
    else
    {
        NS_LOG_INFO("✓ Beacon protocol STA sniffer installed on Node " << nodeId);
    }
}

// OLD: ApSniffer - replaced by UnifiedApActionCallback
// Kept for reference
/*
void
BeaconProtocolHelper::ApSniffer(std::string context,
                                Ptr<const Packet> packet,
                                uint16_t channelFreq,
                                WifiTxVector txVector,
                                MpduInfo mpdu,
                                SignalNoiseDbm signalNoise,
                                uint16_t staId)
{
    static uint64_t callCount = 0;
    callCount++;

    WifiMacHeader hdr;
    Ptr<Packet> copy = packet->Copy();
    uint32_t headerSize = copy->RemoveHeader(hdr);

    if (headerSize == 0) return;
    if (!hdr.IsMgt()) return;

    Mac48Address dst = hdr.GetAddr1();
    Mac48Address src = hdr.GetAddr2();

    if (dst != m_apDevice->GetMac()->GetAddress()) return;

    if (copy->GetSize() < 1) return;
    uint8_t category;
    copy->CopyData(&category, 1);
    copy->RemoveAtStart(1);

    if (category != 5) return;

    uint8_t actionCode;
    copy->CopyData(&actionCode, 1);

    if (actionCode == 1) {
        Ptr<Packet> reportPacket = packet->Copy();
        WifiMacHeader reportHdr;
        reportPacket->RemoveHeader(reportHdr);
        Simulator::ScheduleNow(&BeaconProtocolHelper::ReceiveBeaconReport, this, reportPacket, src);
    }
}
*/

// Stub for ApSniffer (no longer used)
void
BeaconProtocolHelper::ApSniffer(std::string context,
                                Ptr<const Packet> packet,
                                uint16_t channelFreq,
                                WifiTxVector txVector,
                                MpduInfo mpdu,
                                SignalNoiseDbm signalNoise,
                                uint16_t staId)
{
    NS_LOG_WARN("ApSniffer called but deprecated - should use UnifiedApActionCallback");
}

// NEW: Unified callback for AP - receives beacon reports (Category 5, Action 1)
void
BeaconProtocolHelper::UnifiedApActionCallback(ParsedFrameContext* ctx)
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

    // Action code 1 = Measurement Report (Beacon Report)
    if (ctx->actionCode == 1)
    {
        // Get payload copy for report parsing
        Ptr<Packet> reportPacket = ctx->GetPayloadCopy();
        if (reportPacket)
        {
            Simulator::ScheduleNow(&BeaconProtocolHelper::ReceiveBeaconReport, this, reportPacket, src);
        }
    }
}

// OLD: StaSniffer - replaced by UnifiedStaBeaconCallback and UnifiedStaActionCallback
// Kept for reference
/*
void
BeaconProtocolHelper::StaSniffer(std::string context,
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

    Mac48Address src = hdr.GetAddr2();
    Mac48Address dst = hdr.GetAddr1();

    // ... beacon and action frame handling ...
}
*/

// Stub for StaSniffer (no longer used)
void
BeaconProtocolHelper::StaSniffer(std::string context,
                                 Ptr<const Packet> packet,
                                 uint16_t channelFreq,
                                 WifiTxVector txVector,
                                 MpduInfo mpdu,
                                 SignalNoiseDbm signalNoise,
                                 uint16_t staId)
{
    NS_LOG_WARN("StaSniffer called but deprecated - should use UnifiedStaBeaconCallback/UnifiedStaActionCallback");
}

// NEW: Unified callback for STA - receives beacons for measurement
void
BeaconProtocolHelper::UnifiedStaBeaconCallback(ParsedFrameContext* ctx)
{
    ctx->EnsureAddressesParsed();

    Mac48Address bssid = ctx->addr2;  // Source/Transmitter is the AP BSSID

    // Skip if from our own AP (already connected)
    if (m_apDevice && bssid == m_apDevice->GetMac()->GetAddress())
    {
        return;
    }

    if (!m_isMeasuring)
    {
        NS_LOG_DEBUG("[IDLE] Beacon from " << bssid << " (not measuring, ignored)");
        return;
    }

    // Filter by neighbor list
    if (!m_neighborList.empty() && m_neighborList.find(bssid) == m_neighborList.end())
    {
        return;
    }

    // Use pre-computed values from unified sniffer
    BeaconMeasurement m;
    m.bssid = bssid;
    m.rssi = ctx->rssi;
    m.snr = ctx->snr;
    m.channel = ctx->channel;

    m_beaconCache[bssid] = m;
}

// NEW: Unified callback for STA - receives action frames (beacon requests)
void
BeaconProtocolHelper::UnifiedStaActionCallback(ParsedFrameContext* ctx)
{
    ctx->EnsureAddressesParsed();
    ctx->EnsureActionParsed();

    Mac48Address src = ctx->addr2;
    Mac48Address dst = ctx->addr1;

    // Check destination is this STA
    if (m_staDevice == nullptr || dst != m_staDevice->GetMac()->GetAddress())
    {
        return;
    }

    // Handle Beacon Request (action code 0)
    if (ctx->actionCode == 0)
    {
        Ptr<Packet> copy = ctx->GetPayloadCopy();
        if (copy)
        {
            Simulator::ScheduleNow(&BeaconProtocolHelper::ReceiveBeaconRequest,
                                   this,
                                   m_staDevice,
                                   copy,
                                   src);
        }
    }
}

// ADD THIS ENTIRE METHOD ↓
void
BeaconProtocolHelper::StartMeasurement(Time duration)
{
    m_isMeasuring = true;
    m_measurementStartTime = Simulator::Now();
    m_measurementDuration = duration;



    // Schedule stop
    m_measurementTimer =
        Simulator::Schedule(duration, &BeaconProtocolHelper::StopMeasurement, this);
}

// ↑ END NEW METHOD

// ADD THIS ENTIRE METHOD ↓
void
BeaconProtocolHelper::StopMeasurement()
{
    m_isMeasuring = false;

}

// ↑ END NEW METHOD

// ===== SEND BEACON REQUEST =====
void
BeaconProtocolHelper::SendBeaconRequest(Ptr<WifiNetDevice> apDevice, Mac48Address staAddress)
{
    static uint64_t requestCount = 0;
    requestCount++;



    // Use the struct from model.h
    BeaconRequestFrame frame;
    frame.dialogToken = 1;
    // repetitions and category/action already set to defaults

    Ptr<Packet> packet = Create<Packet>((uint8_t*)&frame, sizeof(frame));

    // Also use the BeaconRequestElement struct
    BeaconRequestElement element;
    element.measurementToken = 1;
    element.regulatoryClass = 115;
    element.channelNumber = 0;
    element.randomizationInterval = 0;
    element.measurementDuration = 100;
    element.measurementMode = 0;

    for (int i = 0; i < 6; i++)
    {
        element.bssid[i] = 0xFF;
    }

    Ptr<Packet> elemPacket = Create<Packet>((uint8_t*)&element, sizeof(element));
    packet->AddAtEnd(elemPacket);

    WifiMacHeader hdr;
    hdr.SetType(WIFI_MAC_MGT_ACTION);
    hdr.SetAddr1(staAddress);
    hdr.SetAddr2(apDevice->GetMac()->GetAddress());
    hdr.SetAddr3(apDevice->GetMac()->GetAddress());
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();

    Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDevice->GetMac());
    Ptr<QosTxop> qosTxop = apMac->GetQosTxop(AC_VO);
    qosTxop->Queue(Create<WifiMpdu>(packet, hdr));

}

// ===== RECEIVE BEACON REQUEST =====
void
BeaconProtocolHelper::ReceiveBeaconRequest(Ptr<WifiNetDevice> staDevice,
                                           Ptr<const Packet> packet,
                                           Mac48Address apAddress)
{
    static uint64_t receiveCount = 0;
    receiveCount++;



    // Extract ONLY the frame header first
    Ptr<Packet> copy = packet->Copy();

    BeaconRequestFrame frame;
    copy->CopyData((uint8_t*)&frame, sizeof(frame));

    BeaconRequestElement element;
    copy->RemoveAtStart(sizeof(frame)); // Skip frame header
    copy->CopyData((uint8_t*)&element, sizeof(element));


    if (m_useDualPhySniffer)
    {
        // DUAL-PHY MODE: Query already-collected beacon data immediately
        // (Sniffer has been continuously scanning in background since STA installation)


        // Clear old beacon cache
        m_beaconCache.clear();

        // Query all beacons received by this STA's dual-PHY sniffer
        Mac48Address receiverMac = m_dualPhyOperatingMac;
        std::vector<BeaconInfo> receivedBeacons = m_dualPhySniffer->GetBeaconsReceivedBy(receiverMac);


        // DEBUG: Show ALL beacons in the cache (before filtering)
        for (size_t i = 0; i < receivedBeacons.size(); i++)
        {
            // Placeholder for debug logging if needed
            (void)receivedBeacons[i];
        }

        // DEBUG: Show neighbor list contents (placeholder for future use)


        // Filter beacons: only keep beacons from APs in the neighbor list
        for (const auto& beacon : receivedBeacons)
        {
            // Check if this beacon is from a neighbor AP
            if (m_neighborList.find(beacon.bssid) != m_neighborList.end())
            {
                BeaconMeasurement measurement;
                measurement.bssid = beacon.bssid;
                measurement.rssi = beacon.rssi;
                measurement.snr = beacon.snr;
                measurement.channel = beacon.channel;

                m_beaconCache[beacon.bssid] = measurement;


            }
            else
            {

            }
        }


        // Send report immediately (minimal delay for processing)
        Simulator::Schedule(MilliSeconds(5),
                            &BeaconProtocolHelper::SendBeaconReport,
                            this,
                            staDevice,
                            apAddress,
                            frame.dialogToken);
    }
    else
    {
        NS_LOG_DEBUG("\n  Opening measurement window (legacy single-channel mode)\n");
        Time measurementDuration = MilliSeconds(element.measurementDuration * 1.024);
        StartMeasurement(measurementDuration);

        // Schedule beacon report after measurement duration
        Simulator::Schedule(MilliSeconds(element.measurementDuration * 1.024),
                            &BeaconProtocolHelper::SendBeaconReport,
                            this,
                            staDevice,
                            apAddress,
                            frame.dialogToken);
    }

}

// ===== SEND BEACON REPORT =====
void
BeaconProtocolHelper::SendBeaconReport(Ptr<WifiNetDevice> staDevice,
                                       Mac48Address apAddress,
                                       uint8_t dialogToken)
{
    static uint64_t sendCount = 0;
    sendCount++;



    // CORRECT: Create empty packet FIRST
    Ptr<Packet> packet = Create<Packet>();

    // Build the 3-byte frame header manually (NOT using struct!)
    uint8_t frameHeader[3];
    frameHeader[0] = 5; // Category: Radio Measurement
    frameHeader[1] = 1; // Action: Measurement Report
    frameHeader[2] = dialogToken;

    packet->AddAtEnd(Create<Packet>(frameHeader, 3));

    // Add ALL beacon measurements as elements
    int reportCount = 0;
    for (auto& entry : m_beaconCache)
    {
        BeaconMeasurement m = entry.second;

        BeaconReportElementLegacy element;
        element.elementID = 39;
        element.length = 14;
        element.measurementToken = 1;
        element.measurementReportMode = 0;
        element.measurementType = 5;
        element.regulatoryClass = 115;
        element.channelNumber = m.channel;
        element.actualMeasurementStartTime = 0;
        element.measurementDuration = 100;
        element.reportedFrameInfo = 0x07;
        element.rcpi = RssiToRcpi(m.rssi);
        element.rsni = SnrToRsni(m.snr);
        m.bssid.CopyTo(element.bssid);
        element.antennaID = 0;
        element.parentTSF = 0;



        Ptr<Packet> elemPacket = Create<Packet>((uint8_t*)&element, sizeof(element));
        packet->AddAtEnd(elemPacket);


        reportCount++;
    }

    // Send as action frame
    WifiMacHeader hdr;
    hdr.SetType(WIFI_MAC_MGT_ACTION);
    hdr.SetAddr1(apAddress);
    hdr.SetAddr2(staDevice->GetMac()->GetAddress());
    hdr.SetAddr3(staDevice->GetMac()->GetAddress());
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();

    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(staDevice->GetMac());
    Ptr<QosTxop> qosTxop = staMac->GetQosTxop(AC_VO);

    qosTxop->Queue(Create<WifiMpdu>(packet, hdr));

}

// ===== RECEIVE BEACON REPORT =====
void
BeaconProtocolHelper::ReceiveBeaconReport(Ptr<const Packet> packet, Mac48Address staAddress)
{


    Ptr<Packet> copy = packet->Copy();

    // Parse 3-byte frame header manually
    uint8_t frameHeader[3];
    if (copy->GetSize() < 3)
    {
        return;
    }

    copy->CopyData(frameHeader, 3);
    copy->RemoveAtStart(3);

    uint8_t category = frameHeader[0];
    uint8_t action = frameHeader[1];
    // dialogToken could be used for tracking requests/responses if needed
    // uint8_t dialogToken = frameHeader[2];



    if (category != 5 || action != 1)
    {
        return;
    }

    // Collect all raw beacon report data
    std::vector<BeaconReportData> reports;
    int reportCount = 0;

    while (copy->GetSize() >= sizeof(BeaconReportElementLegacy))
    {
        BeaconReportElementLegacy element;
        copy->CopyData((uint8_t*)&element, sizeof(element));
        copy->RemoveAtStart(sizeof(element));


        if (element.elementID != 39)
        {
            NS_LOG_ERROR("Not a beacon report!");
            break; // Not a beacon report element
        }

        // Extract BSSID
        Mac48Address bssid;
        bssid.CopyFrom(element.bssid);

        // Create raw beacon report data (NO conversion to RSSI/SNR)
        BeaconReportData reportData;
        reportData.bssid = bssid;
        reportData.channel = element.channelNumber;
        reportData.regulatoryClass = element.regulatoryClass;
        reportData.rcpi = element.rcpi; // Raw RCPI value
        reportData.rsni = element.rsni; // Raw RSNI value
        reportData.reportedFrameInfo = element.reportedFrameInfo;
        reportData.measurementDuration = element.measurementDuration;
        reportData.actualMeasurementStartTime = element.actualMeasurementStartTime;
        reportData.antennaID = element.antennaID;
        reportData.parentTSF = element.parentTSF;

        reports.push_back(reportData);

        // For logging only, calculate RSSI/SNR (currently unused, available for debug)
        // double rssi = RcpiToRssi(element.rcpi);
        // double snr = RsniToSnr(element.rsni);

        reportCount++;
    }


    // Fire trace with raw beacon report data
    if (m_apDevice)
    {
        m_beaconReportReceivedTrace(m_apDevice->GetMac()->GetAddress(), staAddress, reports);
    }

}

} // namespace ns3
