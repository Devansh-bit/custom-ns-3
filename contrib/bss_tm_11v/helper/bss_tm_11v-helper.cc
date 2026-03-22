#include "bss_tm_11v-helper.h"

#include "kv-interface.h"

#include "ns3/ap-wifi-mac.h"
#include "ns3/bss_tm_11v.h"
#include "ns3/log.h"
#include "ns3/mac48-address.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/qos-txop.h"
#include "ns3/qos-utils.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-mpdu.h"
#include "ns3/wifi-net-device.h"
#include "ns3/beacon-neighbor-model.h"
#include "ns3/lever-api-helper.h"
#include "ns3/lever-api.h"
#include "ns3/wifi-phy-band.h"
#include "ns3/unified-phy-sniffer.h"     // Unified sniffer for efficient callback handling
#include "ns3/parsed-frame-context.h"

#include <cstdint>
#include <random>

std::random_device rd;
std::mt19937 generator(rd());

uint8_t getClientCount(){
    std::uniform_int_distribution<int> distribution(0, 255);
    return static_cast<uint8_t>(distribution(generator));
    return 0x00;
}

uint8_t getChannelUtil(){
    std::uniform_int_distribution<int> distribution(0, 255);
    return static_cast<uint8_t>(distribution(generator));
    return 0x00;
}


NS_LOG_COMPONENT_DEFINE("BssTm11vHelper");

namespace ns3
{

    BssTm11vHelper::BssTm11vHelper()
        : m_cooldownDuration(Seconds(30)),  // Default 5 minutes
          m_beaconSniffer(nullptr),
          m_pendingRoamingChannel(0),
          m_waitingForBssTmResponseAck(false)
    {}
    BssTm11vHelper::~BssTm11vHelper(){}
TypeId
BssTm11vHelper::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::BssTm11vHelper")
                            .SetParent<rankListManager>()
                            .SetGroupName("Wifi")
                            .AddConstructor<BssTm11vHelper>();
    return tid;
}

void
BssTm11vHelper::SetBeaconSniffer(DualPhySnifferHelper* sniffer)
{
    m_beaconSniffer = sniffer;
    NS_LOG_INFO("Beacon sniffer connected to BSS TM helper");
}

void
BssTm11vHelper::SetStaMac(Mac48Address staMac)
{
    m_staMac = staMac;
    NS_LOG_INFO("STA MAC set for beacon cache queries: " << staMac);
}

void
BssTm11vHelper::SetBeaconCache(const std::vector<BeaconInfo>& beacons)
{
    m_injectedBeacons = beacons;
    NS_LOG_INFO("Beacon cache injected with " << beacons.size() << " entries (real-time load data)");
}

void
BssTm11vHelper::SetCooldown(Time duration)
{
    m_cooldownDuration = duration;
    NS_LOG_INFO("BSS TM Request cooldown set to " << duration.GetSeconds() << "s");
}

Time
BssTm11vHelper::GetCooldown() const
{
    return m_cooldownDuration;
}

void
BssTm11vHelper::InstallOnAp(Ptr<WifiNetDevice> apDevice)
{
    NS_LOG_FUNCTION(this << apDevice);
    m_apDevice = apDevice;

    uint32_t nodeId = apDevice->GetNode()->GetId();
    (void)nodeId;  // Suppress unused variable warning

    // Find the device index of apDevice in the node's device list
    // NOTE: deviceId calculation kept for potential future use with Config::Connect
    uint32_t deviceId [[maybe_unused]] = 0;
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
    // Config::Connect(ss.str(), MakeCallback(&BssTm11vHelper::ApSniffer, this));

    // NEW: Use UnifiedPhySniffer for efficient callback handling
    m_apSniffer = CreateObject<UnifiedPhySniffer>();
    m_apSniffer->Install(apDevice);

    // Subscribe to Category 0x0A (WNM/802.11v) action frames for BSS TM responses
    m_apSnifferSubscriptionId = m_apSniffer->SubscribeAction(0x0A, [this](ParsedFrameContext* ctx) {
        this->UnifiedApActionCallback(ctx);
    });

    NS_LOG_INFO("✓ Roaming protocols AP sniffer installed on Node " << nodeId
                << " (UnifiedSniffer, subscription=" << m_apSnifferSubscriptionId << ")");
}

void
BssTm11vHelper::InstallOnSta(Ptr<WifiNetDevice> staDevice)
{
    NS_LOG_FUNCTION(this << staDevice);
    m_staDevice = staDevice;

    uint32_t nodeId = staDevice->GetNode()->GetId();

    // OLD: Config::Connect approach with wildcard (commented out - replaced with unified sniffer)
    // Validation inside the callback ensures we only process BSS TM requests from:
    // 1. Operating device (Device 0) only
    // 2. Currently associated AP only
    // std::stringstream ss;
    // ss << "/NodeList/" << nodeId << "/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx";
    // Config::Connect(ss.str(), MakeCallback(&BssTm11vHelper::StaSniffer, this));

    // NEW: Use UnifiedPhySniffer for efficient callback handling
    // Install on Device 0 only (operating device)
    m_staSniffer = CreateObject<UnifiedPhySniffer>();
    m_staSniffer->Install(staDevice);

    // Subscribe to Category 0x0A (WNM/802.11v) action frames for BSS TM requests
    m_staSnifferSubscriptionId = m_staSniffer->SubscribeAction(0x0A, [this](ParsedFrameContext* ctx) {
        this->UnifiedStaActionCallback(ctx);
    });

    // Create and configure LeverConfig for channel switching
    m_staLeverConfig = CreateObject<LeverConfig>();

    // Get current channel settings from STA's WifiPhy
    Ptr<WifiPhy> phy = staDevice->GetPhy();
    uint8_t currentChannel = phy->GetChannelNumber();
    // Set initial channel settings in LeverConfig
    m_staLeverConfig->SwitchChannel(currentChannel);

    // Install LeverApi application on STA node
    LeverApiHelper leverHelper(m_staLeverConfig);
    m_staLeverApp = leverHelper.Install(staDevice->GetNode());
    m_staLeverApp.Start(Seconds(0.0));
    m_staLeverApp.Stop(Seconds(9999.0));  // Large value; will be stopped by Simulator::Stop

    NS_LOG_INFO("✓ Roaming protocols STA sniffer installed on Node " << nodeId
                << " (UnifiedSniffer, subscription=" << m_staSnifferSubscriptionId << ")");
    NS_LOG_INFO("✓ LeverApi installed on STA Node " << nodeId
                << " with initial channel " << (int)currentChannel);
}

WifiPhyBand
BssTm11vHelper::DetermineWifiPhyBand(uint8_t channel) const
{
    // Simple heuristic: channels 1-14 are 2.4GHz, others are 5GHz
    // Channel 1-14: 2.4 GHz
    // Channel 36+: 5 GHz
    // Channel 1-233 (6 GHz): Would need more complex logic, but for now assume 5GHz
    if (channel <= 14) {
        return WIFI_PHY_BAND_2_4GHZ;
    } else {
        return WIFI_PHY_BAND_5GHZ;
    }
}

// OLD: ApSniffer - replaced by UnifiedApActionCallback
// Kept for reference
/*
void
BssTm11vHelper::ApSniffer(std::string context,
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
    if (dst != m_apDevice->GetMac()->GetAddress()) return;

    if (copy->GetSize() < 1) return;
    uint8_t category;
    copy->CopyData(&category, 1);
    copy->RemoveAtStart(1);

    if (category != 0x0A) return;

    uint8_t actionCode;
    copy->CopyData(&actionCode, 1);

    if (actionCode == 8) {
        // Parse BSS TM Response
        // ...
    }
}
*/

// Stub for ApSniffer (no longer used)
void
BssTm11vHelper::ApSniffer(std::string context,
                          Ptr<const Packet> packet,
                          uint16_t channelFreq,
                          WifiTxVector txVector,
                          MpduInfo mpdu,
                          SignalNoiseDbm signalNoise,
                          uint16_t staId)
{
    NS_LOG_WARN("ApSniffer called but deprecated - should use UnifiedApActionCallback");
}

// NEW: Unified callback for AP - receives BSS TM responses (Category 0x0A, Action 8)
void
BssTm11vHelper::UnifiedApActionCallback(ParsedFrameContext* ctx)
{
    ctx->EnsureAddressesParsed();
    ctx->EnsureActionParsed();

    // Early filter: Only process BSS TM Response (action code 8)
    // Category 0x0A (WNM) includes many action types - we only care about BSS TM
    if (ctx->actionCode != 8)
    {
        return;
    }

    Mac48Address dst = ctx->addr1;

    // Only process frames destined to this AP
    if (dst != m_apDevice->GetMac()->GetAddress())
    {
        return;
    }

    // BSS TM Response (action code 8)
    {
        // Get payload copy for response parsing
        Ptr<Packet> copy = ctx->GetPayloadCopy();
        if (copy && copy->GetSize() >= 4)
        {
            // Parse BSS TM Response fields
            // Payload: category(1) + action(1) + dialogToken(1) + statusCode(1) + ...
            uint8_t buffer[4];
            copy->CopyData(buffer, 4);
            uint8_t dialogToken = buffer[2];
            uint8_t statusCode = buffer[3];

            NS_LOG_INFO("╔════════════════════════════════════════════════════════════╗");
            NS_LOG_INFO("║  AP: Received BSS TM Response                              ║");
            NS_LOG_INFO("╠════════════════════════════════════════════════════════════╣");
            NS_LOG_INFO("║  Time: " << Simulator::Now().GetSeconds() << "s");
            NS_LOG_INFO("║  Dialog Token: " << (int)dialogToken);
            NS_LOG_INFO("║  Status: " << (statusCode == 0 ? "ACCEPT (0)" : "REJECT (" + std::to_string((int)statusCode) + ")"));
            NS_LOG_INFO("╚════════════════════════════════════════════════════════════╝");
        }
    }
}

// OLD: StaSniffer - replaced by UnifiedStaActionCallback
// Kept for reference
/*
void
BssTm11vHelper::StaSniffer(std::string context,
                           Ptr<const Packet> packet,
                           uint16_t channelFreq,
                           WifiTxVector txVector,
                           MpduInfo mpdu,
                           SignalNoiseDbm signalNoise,
                           uint16_t staId)
{
    // Device ID validation, MAC header parsing, category extraction...
    // ...
    if (actionCode == 7) {
        // BSS TM Request handling
        BssTm11vHelper::HandleRead(m_staDevice, respCopy, src);
    }
}
*/

// Stub for StaSniffer (no longer used)
void
BssTm11vHelper::StaSniffer(std::string context,
                           Ptr<const Packet> packet,
                           uint16_t channelFreq,
                           WifiTxVector txVector,
                           MpduInfo mpdu,
                           SignalNoiseDbm signalNoise,
                           uint16_t staId)
{
    NS_LOG_WARN("StaSniffer called but deprecated - should use UnifiedStaActionCallback");
}

// NEW: Unified callback for STA - receives BSS TM requests (Category 0x0A, Action 7)
void
BssTm11vHelper::UnifiedStaActionCallback(ParsedFrameContext* ctx)
{
    ctx->EnsureAddressesParsed();
    ctx->EnsureActionParsed();

    // Early filter: Only process BSS TM Request (action code 7)
    // Category 0x0A (WNM) includes many action types - we only care about BSS TM
    if (ctx->actionCode != 7)
    {
        return;
    }

    Mac48Address src = ctx->addr2;
    Mac48Address dst = ctx->addr1;

    // Check destination is this STA
    if (dst != m_staDevice->GetMac()->GetAddress())
    {
        return;
    }

    // Validation: Only accept from currently associated AP
    Mac48Address associatedAp = m_staDevice->GetMac()->GetBssid(0);  // linkId 0 for single-link STA
    if (src != associatedAp)
    {
        NS_LOG_INFO("UnifiedStaActionCallback: Rejecting action frame from " << src << " (not associated AP " << associatedAp << ")");
        return;
    }

    NS_LOG_INFO("UnifiedStaActionCallback: Received BSS TM Request from " << src << " to " << dst);

    // Get payload copy for request parsing
    Ptr<Packet> respCopy = ctx->GetPayloadCopy();
    if (respCopy)
    {
        BssTm11vHelper::HandleRead(m_staDevice, respCopy, src);
    }
}

void
BssTm11vHelper::sendRankedCandidates(Ptr<WifiNetDevice> apDevice,
                                     Mac48Address apAddress,
                                     Mac48Address staAddress,
                                     std::vector<BeaconReportData> reports)
{
    rankedAPs finalRanks;

    // Use injected beacons with real-time load data (preferred)
    // This prevents thundering herd by using current AP STA counts
    std::vector<BeaconInfo> beacons;
    if (!m_injectedBeacons.empty())
    {
        beacons = m_injectedBeacons;
        m_injectedBeacons.clear();  // Use once
        NS_LOG_INFO("Using injected beacon cache with real-time load (" << beacons.size() << " neighbors)");
    }
    else if (m_beaconSniffer && m_staMac != Mac48Address())
    {
        // Fallback to STA's beacon cache (may have stale load data)
        beacons = m_beaconSniffer->GetBeaconsReceivedBy(m_staMac);
        NS_LOG_INFO("Using STA beacon cache for ranking (" << beacons.size() << " neighbors)");
    }

    if (!beacons.empty())
    {
        // Exclude current AP from candidates
        finalRanks = rankCandidatesFromBeaconCache(beacons, apAddress);
    }

    // Fallback to BeaconReportData if no beacon cache
    if (finalRanks.candidates.empty() && !reports.empty())
    {
        NS_LOG_INFO("Falling back to BeaconReportData (no BSS Load available)");
        candidateAPs cands;

        for (const auto& report : reports)
        {
            unrankedCandidates candidate;
            candidate.operatingClass = report.regulatoryClass;
            candidate.channel = report.channel;
            candidate.RSSI = report.rcpi;
            candidate.SNR = report.rsni;
            report.bssid.CopyTo(candidate.BSSID.data());
            candidate.clientCount = 0;   // Unknown
            candidate.channelUtil = 0;   // Unknown

            cands.candidates.push_back(candidate);
        }

        finalRanks = rankCandidates(cands);
    }

    if (!finalRanks.candidates.empty())
    {
        BssTmParameters params = convertToBssTmParameters(finalRanks);
        Simulator::ScheduleNow(&BssTm11vHelper::SendDynamicBssTmRequest,
                               this, apDevice, params, staAddress);
    }
    else
    {
        NS_LOG_WARN("No candidate APs available for BSS TM request");
    }
}

void
BssTm11vHelper::SendDynamicBssTmRequest(Ptr<WifiNetDevice> apDevice,
                                        const BssTmParameters& params,
                                        Mac48Address staAddress)
{
    Mac48Address apBssid = apDevice->GetMac()->GetAddress();
    auto key = std::make_pair(apBssid, staAddress);

    // Check rate limit
    auto it = m_lastRequestTime.find(key);
    if (it != m_lastRequestTime.end())
    {
        Time elapsed = Simulator::Now() - it->second;
        if (elapsed < m_cooldownDuration)
        {
            NS_LOG_INFO("╔════════════════════════════════════════════════════════════╗");
            NS_LOG_INFO("║  Rate Limit: BSS TM Request SKIPPED                        ║");
            NS_LOG_INFO("╠════════════════════════════════════════════════════════════╣");
            NS_LOG_INFO("║  To: " << staAddress);
            NS_LOG_INFO("║  Last request: " << elapsed.GetSeconds() << "s ago");
            NS_LOG_INFO("║  Cooldown: " << m_cooldownDuration.GetSeconds() << "s");
            NS_LOG_INFO("╚════════════════════════════════════════════════════════════╝");
            return;
        }
    }

    // Update last request time
    m_lastRequestTime[key] = Simulator::Now();

    NS_LOG_INFO("╔════════════════════════════════════════════════════════════╗");
    NS_LOG_INFO("║  AP: Sending BSS TM Request                                ║");
    NS_LOG_INFO("╠════════════════════════════════════════════════════════════╣");
    NS_LOG_INFO("║  Time: " << Simulator::Now().GetSeconds() << "s");
    NS_LOG_INFO("║  To: " << staAddress);
    NS_LOG_INFO("║  Dialog Token: " << (int)params.dialogToken);
    NS_LOG_INFO("║  Candidates: " << params.candidates.size());
    for (size_t i = 0; i < params.candidates.size(); i++) {
        const auto& c = params.candidates[i];
        NS_LOG_INFO("║    [" << i << "] BSSID: " << std::hex << std::setfill('0')
                    << std::setw(2) << (int)c.BSSID[0] << ":"
                    << std::setw(2) << (int)c.BSSID[1] << ":"
                    << std::setw(2) << (int)c.BSSID[2] << ":"
                    << std::setw(2) << (int)c.BSSID[3] << ":"
                    << std::setw(2) << (int)c.BSSID[4] << ":"
                    << std::setw(2) << (int)c.BSSID[5] << std::dec
                    << ", Ch=" << (int)c.channel
                    << ", Pref=" << (int)c.preference);
    }
    NS_LOG_INFO("╚════════════════════════════════════════════════════════════╝");

    NS_LOG_FUNCTION(params.dialogToken);

    std::vector<uint8_t> frame;

    // Category
    frame.push_back(0x0A);
    // Action
    frame.push_back(0x07);

    // Dialog Token
    frame.push_back(params.dialogToken);

    // Request Mode flags
    std::vector<int> requestMode = {1, 1, 0, 0, 0, 0, 0, 0};

    if (params.reasonCode == BssTmParameters::ReasonCode::ESS_DISASSOCIATION)
    {
        requestMode[4] = 1;
    }
    if (params.reasonCode == BssTmParameters::ReasonCode::AP_UNAVAILABLE ||
        params.reasonCode == BssTmParameters::ReasonCode::HIGH_LOAD)
    {
        requestMode[2] = 1;
        requestMode[3] = 1;
    }

    uint8_t byte = 0;
    for (int i = 0; i < 8; ++i)
    {
        byte |= (requestMode[i] << i);
    }

    frame.push_back(byte);

    // Disassociation Timer
    frame.push_back(params.disassociationTimer & 0xFF);
    frame.push_back((params.disassociationTimer >> 8) & 0xFF);

    // Validity Interval
    frame.push_back(params.validityInterval);

    // BSS Termination Duration
    if (requestMode[3] == 1)
    {
        frame.push_back(0x04); // subelement ID
        frame.push_back(0x0A); // length

        for (int i = 0; i < 8; i++)
        {
            frame.push_back(0x00); // TSF placeholder
        }

        frame.push_back(params.terminationDuration & 0xFF);
        frame.push_back((params.terminationDuration >> 8) & 0xFF);
    }

    // Candidate List
    if (!params.candidates.empty())
    {
        for (const auto& candidate : params.candidates)
        {
            frame.push_back(0x34); // Neighbor Report
            frame.push_back(0x10); // length

            for (int i = 0; i < 6; i++)
            {
                frame.push_back(candidate.BSSID[i]);
            }

            uint32_t bssidInfo = 0x00000003;
            frame.push_back(bssidInfo & 0xFF);
            frame.push_back((bssidInfo >> 8) & 0xFF);
            frame.push_back((bssidInfo >> 16) & 0xFF);
            frame.push_back((bssidInfo >> 24) & 0xFF);

            frame.push_back(candidate.operatingClass);
            frame.push_back(candidate.channel);
            frame.push_back(candidate.phyType);

            // Preference
            frame.push_back(0x03);
            frame.push_back(0x01);
            frame.push_back(candidate.preference);
        }
    }

    std::ostringstream oss;
    for (size_t i = 0; i < frame.size(); i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)frame[i] << " ";
    }
    NS_LOG_INFO("AP: Frame bytes: " << oss.str());

    Ptr<Packet> packet = Create<Packet>(frame.data(), frame.size());

    WifiMacHeader hdr;
    hdr.SetType(WIFI_MAC_MGT_ACTION);
    hdr.SetAddr1(staAddress);
    hdr.SetAddr2(apDevice->GetMac()->GetAddress());
    //hdr.SetAddr3(apDevice->GetMac()->GetAddress());
    hdr.SetAddr3(staAddress);
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();

    //apDevice->Send(packet, staAddress, 0);
    Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDevice->GetMac());
    Ptr<QosTxop> qosTxop = apMac->GetQosTxop(AC_VO);
    qosTxop->Queue(Create<WifiMpdu>(packet, hdr));

    NS_LOG_INFO("╔════════════════════════════════════╗");
    NS_LOG_INFO("║  TRACE: BSS TM Request Queued      ║");
    NS_LOG_INFO("╚════════════════════════════════════╝");

    // AP-side cleanup: Schedule removal of STA from staList after sending BSS TM Request
    // This ensures the old AP properly removes the STA even if the STA moves out of range
    // before the deassociation frame can be received (common in cross-channel roaming)
    // Delay of 500ms allows BSS TM exchange to complete before cleanup
    Simulator::Schedule(MilliSeconds(500), [apMac, staAddress]() {
        if (apMac)
        {
            NS_LOG_UNCOND("[BSS-TM-CLEANUP] " << Simulator::Now().GetSeconds() << "s: "
                          << "AP " << apMac->GetAddress() << " removing STA " << staAddress
                          << " after BSS TM Request");
            apMac->DeassociateSta(staAddress, 0);  // link 0 for single-link
        }
    });

}

void
BssTm11vHelper::HandleRead(Ptr<WifiNetDevice> staDevice, Ptr<const Packet> packet, Mac48Address apAddress)
{
    NS_LOG_INFO("STA: HandleRead called at " << Simulator::Now().GetSeconds() << "s");

    // Extract and validate frame
    uint32_t packetSize = packet->GetSize();
    if (packetSize > 512)
    {
        NS_LOG_WARN("Packet too large (" << packetSize << " bytes), truncating to 512");
        packetSize = 512;
    }
    uint8_t buffer[512];
    packet->CopyData(buffer, packetSize);

    if (packetSize < 3)
    {
        NS_LOG_WARN("Packet too small to be valid 802.11v frame");
        return;
    }

    uint8_t category = buffer[0];
    uint8_t action = buffer[1];

    if (category == 0x0A)
    {
        // --- BSS Transition Management ---
        if (action == 0x07)
        {
            ParsedParameters parsedParams = ParseRequestParameters(buffer, packetSize);
            NS_LOG_INFO("STA: Received BSS TM Request - Reason: "
                        << ReasonCodeToString(parsedParams.reasonCode));

            BssTmResponseParameters responseParams = GenerateResponseParameters(parsedParams);

            NS_LOG_INFO("Request Parameters Received");
            // Schedule response (10 ms later)

            //BssTm11vHelper::SendDynamicBssTmResponse(staDevice, responseParams,apAddress);

            Simulator::Schedule(MilliSeconds(10),
                                &BssTm11vHelper::SendDynamicBssTmResponse,
                                this,
                                staDevice,
                                responseParams,
                                apAddress);
        }
    }
    else
    {
        NS_LOG_WARN((m_isAp ? "AP" : "STA") << ": Received non-802.11v frame (Category: "
                                            << (int)category << ", Action: " << (int)action << ")");
    }
}

ParsedParameters
BssTm11vHelper::ParseRequestParameters(const uint8_t* buffer, uint32_t size)
{
    ParsedParameters params;

    if (size < 7)
    {
        NS_LOG_WARN("BSS TM Request frame too short");
        return params;
    }

    params.dialogToken = buffer[2];

    uint8_t requestMode = buffer[3];
    params.disassociationTimer = buffer[4] | (buffer[5] << 8);

    bool disassocImminent = (requestMode & 0x04) != 0; // Bit 2
    bool essDisassoc = (requestMode & 0x10) != 0;      // Bit 4

    // Determine reason based on request mode and timer
    if (essDisassoc)
    {
        params.reasonCode = BssTmParameters::ReasonCode::ESS_DISASSOCIATION;
        NS_LOG_INFO("STA: Parsed reason - ESS_DISASSOCIATION");
    }
    else if (disassocImminent && params.disassociationTimer == 0)
    {
        params.reasonCode = BssTmParameters::ReasonCode::LOW_RSSI;
        NS_LOG_INFO("STA: Parsed reason - LOW_RSSI (immediate disassoc)");
    }
    else if (disassocImminent && params.disassociationTimer > 0)
    {
        params.reasonCode = BssTmParameters::ReasonCode::HIGH_LOAD;
        NS_LOG_INFO("STA: Parsed reason - HIGH_LOAD (timed disassoc)");
    }
    else
    {
        params.reasonCode = BssTmParameters::ReasonCode::LOW_RSSI;
        NS_LOG_INFO("STA: Parsed reason - LOW_RSSI (default)");
    }

    // Parse candidate APs if Preferred Candidate List is included
    bool hasCandidates = (requestMode & 0x01) != 0; // Bit 0

    if (hasCandidates)
    {
        uint32_t pos = 7; // Start after validity interval

        // Skip BSS Termination Duration if included
        bool hasTermination = (requestMode & 0x08) != 0; // Bit 3
        if (hasTermination && pos + 12 <= size)
        {
            pos += 12; // Skip subelement ID, length, TSF (8 bytes), duration (2 bytes)
        }

        // Parse Neighbor Report elements (0x34)
        while (pos + 2 <= size)
        {
            uint8_t elementId = buffer[pos];
            uint8_t length = buffer[pos + 1];

            if (elementId == 0x34 && pos + 2 + length <= size)
            {
                // Parse BSSID from neighbor report
                if (length >= 13)
                {
                    CandidateInfo candidate;

                    for (int i = 0; i < 6; i++)
                    {
                        candidate.BSSID[i] = buffer[pos + 2 + i];
                    }

                    // Parse Operating Class (1 byte at offset 12)
                    candidate.operatingClass = buffer[pos + 12];

                    // Parse Channel Number (1 byte at offset 13)
                    candidate.channel = buffer[pos + 13];

                    params.candidates.push_back(candidate);

                    NS_LOG_INFO("STA: Found candidate AP - BSSID: "
                                << std::hex << (int)candidate.BSSID[0] << ":"
                                << (int)candidate.BSSID[1] << ":" << (int)candidate.BSSID[2] << ":"
                                << (int)candidate.BSSID[3] << ":" << (int)candidate.BSSID[4] << ":"
                                << (int)candidate.BSSID[5] << std::dec);
                }
                pos += 2 + length;
            }
            else
            {
                break; // Unknown element or malformed
            }
        }
    }

    return params;
}

BssTmResponseParameters
BssTm11vHelper::GenerateResponseParameters(const ParsedParameters& requestParams)
{
    BssTmResponseParameters response;

    response.dialogToken = requestParams.dialogToken;

    // Initialize default values
    response.statusCode = 0x01; // Default: reject
    response.terminationDelay = 0x00;

    // Decision logic based on STA conditions
    bool shouldAccept = false;

    // Check if STA has poor RSSI - more likely to accept
    if (m_lastRssi < -75.0)
    {
        shouldAccept = true;
        NS_LOG_INFO("STA: Accepting transition due to poor RSSI (" << m_lastRssi << " dBm)");
    }
    // Check if request is due to high load - accept for network optimization
    else if (requestParams.reasonCode == BssTmParameters::ReasonCode::LOW_RSSI) {
        shouldAccept = true;
        NS_LOG_INFO("STA: Accepting transition due to past Low RSSI (Change Client Logic Here)");
    }
    else if (requestParams.reasonCode == BssTmParameters::ReasonCode::HIGH_LOAD)
    {
        shouldAccept = true;
        NS_LOG_INFO("STA: Accepting transition for load balancing");
    }
    // Check if it's ESS disassociation - should accept
    else if (requestParams.reasonCode == BssTmParameters::ReasonCode::ESS_DISASSOCIATION)
    {
        shouldAccept = true;
        NS_LOG_INFO("STA: Accepting ESS disassociation");
    }
    // Check if immediate disassociation timer (urgent)
    else if (requestParams.disassociationTimer < 50)
    {
        shouldAccept = true;
        NS_LOG_INFO("STA: Accepting due to urgent disassociation timer");
    }
    else
    {
        NS_LOG_INFO("STA: Rejecting transition - good conditions (RSSI: " << m_lastRssi << " dBm)");
    }

    if (shouldAccept)
    {
        response.statusCode = 0x00; // Accept

        // Set target BSSID - use first candidate if available
        if (!requestParams.candidates.empty())
        {
            for (int i = 0; i < 6; i++)
            {
                response.targetBSSID[i] =
                    requestParams.candidates[0].BSSID[i]; // ✅ First candidate
            }

            response.channel = requestParams.candidates[0].channel;

            NS_LOG_INFO(
                "STA: Target BSSID set from candidate list, Channel: " << (int)response.channel);
        }
        else
        {
            // Use default BSSID if no candidates provided
            NS_LOG_INFO("STA: Using default target BSSID (no candidates in request)");
        }

        //BssTm11vHelper::SendDynamicBssTmResponse();
    }
    else
    {
        // Rejection status codes:
        // 0x01 = Unspecified rejection
        // 0x05 = Reject but with BSS Termination Delay
        response.statusCode = 0x01;
    }

    return response;
}

void
BssTm11vHelper::SendDynamicBssTmResponse(Ptr<WifiNetDevice> staDevice,
                                         const BssTmResponseParameters& params,
                                         Mac48Address apAddress)
{
    NS_LOG_INFO("BSS TM Response Started");

    if (params.statusCode)
    {
        NS_LOG_INFO("STA: Sending BSS TM Response for Transition Rejection (Status: "
                    << (int)params.statusCode << ")");
    }
    else
    {
        NS_LOG_INFO("STA: Sending BSS TM Response, BSS Transition Accepted");

        // ==== ROAMING INTEGRATION ====
        // Store roaming parameters - will initiate roaming AFTER response is ACKed
        m_pendingRoamingChannel = params.channel;
        m_pendingRoamingBand = DetermineWifiPhyBand(params.channel);
        m_pendingRoamingBssid.CopyFrom(params.targetBSSID);
        m_waitingForBssTmResponseAck = true;

        NS_LOG_INFO("STA: Will initiate roaming to BSSID " << m_pendingRoamingBssid
                    << " on channel " << (int)m_pendingRoamingChannel
                    << " (Band: " << (m_pendingRoamingBand == WIFI_PHY_BAND_2_4GHZ ? "2.4GHz" : "5GHz")
                    << ") after BSS TM Response is acknowledged");
    }

    std::vector<uint8_t> responseFrame;

    // Category Header
    responseFrame.push_back(0x0A);

    // Action Header
    responseFrame.push_back(0x08);

    // Dialog Token
    responseFrame.push_back(params.dialogToken);

    // Status Code
    responseFrame.push_back(params.statusCode);

    // BSS Termination Delay (If Status Code is 5, else reserved)
    if (params.statusCode == 5)
    {
        responseFrame.push_back(params.terminationDelay);
    }
    else
    {
        responseFrame.push_back(0x00);
    }

    // Target BSSID (Only if Status Code is 0)
    if (!params.statusCode)
    {
        for (int i = 0; i < 6; i++)
        {
            responseFrame.push_back(params.targetBSSID[i]);
        }
    }

    Ptr<Packet> packet = Create<Packet>(responseFrame.data(), responseFrame.size());
    // Fixed string concatenation
    std::stringstream ss;
    ss << "BSS_TM_RESPONSE[Status:" << (int)params.statusCode << "]";

    WifiMacHeader hdr;
    hdr.SetType(WIFI_MAC_MGT_ACTION);
    hdr.SetAddr1(apAddress);
    hdr.SetAddr2(staDevice->GetMac()->GetAddress());
    hdr.SetAddr3(apAddress);
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();

    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(staDevice->GetMac());
    Ptr<QosTxop> qosTxop = staMac->GetQosTxop(AC_VO);
    qosTxop->Queue(Create<WifiMpdu>(packet, hdr));

    NS_LOG_INFO("╔════════════════════════════════════╗");
    NS_LOG_INFO("║  TRACE: BSS TM Response Queued     ║");
    NS_LOG_INFO("╚════════════════════════════════════╝");

    // Schedule roaming with a delay to allow BSS TM Response transmission to complete
    // The 100ms delay ensures the response frame and its ACK are exchanged, and
    // any ongoing transmissions complete before channel switching begins.
    // The PHY state check in InitiateRoaming() provides additional safety.
    if (!params.statusCode)
    {
        NS_LOG_INFO("STA: Scheduling roaming in 100ms to allow ongoing transmissions to complete");
        Simulator::Schedule(MilliSeconds(100),
                          &StaWifiMac::InitiateRoaming,
                          staMac,
                          m_pendingRoamingBssid,
                          m_pendingRoamingChannel,
                          m_pendingRoamingBand);
    }
}

} // namespace ns3

