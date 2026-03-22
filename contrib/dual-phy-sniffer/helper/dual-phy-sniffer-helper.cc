/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "dual-phy-sniffer-helper.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-mac.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/adhoc-wifi-mac.h"  // NEW: For listen-only mode
#include "ns3/wifi-mac-header.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/string.h"
#include "ns3/spectrum-channel.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/mgt-headers.h"      // For MgtBeaconHeader
#include "ns3/bss-load.h"          // For BSS Load IE
#include "ns3/ht-capabilities.h"   // For HT Capabilities IE
#include "ns3/vht-capabilities.h"  // For VHT Capabilities IE
#include "ns3/he-capabilities.h"   // For HE Capabilities IE
#include "ns3/unified-phy-sniffer-helper.h"  // Unified sniffer for efficient callback handling
#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DualPhySnifferHelper");

DualPhySnifferHelper::DualPhySnifferHelper()
    : m_hopInterval(Seconds(1)),
      m_ssid(Ssid("RRM-Network")),  // Default shared SSID for seamless roaming
      m_beaconMaxAge(Seconds(0)),   // 0 = no expiration
      m_beaconMaxEntries(0)         // 0 = no limit
{
    // Default scanning channels: 36, 40, 44, 48
    m_scanningChannels = {36, 40, 44, 48};
}

DualPhySnifferHelper::~DualPhySnifferHelper()
{
    // Clean up trace contexts
    for (auto ctx : m_traceContexts)
    {
        delete ctx;
    }
    m_traceContexts.clear();
}

WifiPhyBand
DualPhySnifferHelper::GetBandForChannel(uint8_t channel)
{
    // Determine WiFi band from channel number
    // Based on IEEE 802.11 channel assignments:
    // - 2.4 GHz: Channels 1-14 (2400-2484 MHz)
    // - 5 GHz: Channels 36-177 (5150-5885 MHz)
    // - 6 GHz: Channels 1-233 (odd numbers, 5955-7115 MHz)
    // Note: Check 5 GHz first to avoid overlap with 6 GHz channel numbering

    if (channel >= 1 && channel <= 14)
    {
        return WIFI_PHY_BAND_2_4GHZ;
    }
    else if (channel >= 36 && channel <= 177)
    {
        return WIFI_PHY_BAND_5GHZ;
    }
    else if (channel >= 1 && channel <= 233)
    {
        // 6 GHz uses channels 1, 5, 9, 13, ..., 229, 233 (odd numbers)
        return WIFI_PHY_BAND_6GHZ;
    }
    else
    {
        // Default fallback for unexpected channel numbers
        NS_LOG_WARN("Unknown channel number: " << (int)channel << ", defaulting to 5 GHz");
        return WIFI_PHY_BAND_5GHZ;
    }
}

void
DualPhySnifferHelper::SetChannel(Ptr<YansWifiChannel> channel)
{
    m_yansChannel = channel;
}

void
DualPhySnifferHelper::SetChannel(Ptr<SpectrumChannel> channel)
{
    m_spectrumChannel = channel;
}

void
DualPhySnifferHelper::SetScanningChannels(const std::vector<uint8_t>& channels)
{
    m_scanningChannels = channels;
}

void
DualPhySnifferHelper::SetHopInterval(Time interval)
{
    m_hopInterval = interval;
}

void
DualPhySnifferHelper::SetMeasurementCallback(MeasurementCallback callback)
{
    m_measurementCallback = callback;
}

void
DualPhySnifferHelper::SetSsid(Ssid ssid)
{
    m_ssid = ssid;
}

void
DualPhySnifferHelper::SetValidApBssids(const std::set<Mac48Address>& apBssids)
{
    m_validApBssids = apBssids;
    NS_LOG_INFO("Set " << apBssids.size() << " valid AP BSSIDs for beacon filtering");
}

NetDeviceContainer
DualPhySnifferHelper::Install(Ptr<Node> node, uint8_t operatingChannel, Mac48Address desiredBssid)
{
    NS_LOG_FUNCTION(this << node->GetId() << (int)operatingChannel << desiredBssid);

    // Check which channel type is set
    bool useYans = (m_yansChannel != nullptr);
    bool useSpectrum = (m_spectrumChannel != nullptr);

    if (!useYans && !useSpectrum)
    {
        NS_FATAL_ERROR("No channel set. Call SetChannel() with either YansWifiChannel or SpectrumChannel first.");
    }

    if (useYans && useSpectrum)
    {
        NS_FATAL_ERROR("Both Yans and Spectrum channels set. Use only one channel type.");
    }

    // ===== Scanning PHY FIRST (so operating gets desired BSSID) =====
    // Create PHY helper based on channel type
    WifiPhyHelper* scanPhyHelperPtr = nullptr;
    YansWifiPhyHelper yansPhyHelper;
    SpectrumWifiPhyHelper spectrumPhyHelper;

    if (useYans)
    {
        yansPhyHelper.SetChannel(m_yansChannel);
        scanPhyHelperPtr = &yansPhyHelper;
    }
    else // useSpectrum
    {
        spectrumPhyHelper.SetChannel(m_spectrumChannel);
        scanPhyHelperPtr = &spectrumPhyHelper;
    }

    // Set initial scanning channel
    // Determine band from channel number (2.4 GHz for ch 1-14, 5 GHz otherwise)
    // For 2.4 GHz: must specify width=20 explicitly to avoid ambiguity (DSSS 22MHz, OFDM 20MHz, OFDM 40MHz)
    // For 5 GHz: can use width=0 for auto-width detection
    uint8_t firstChannel = m_scanningChannels[0];
    std::string bandStr;
    uint16_t width;
    if (firstChannel >= 1 && firstChannel <= 14)
    {
        bandStr = "BAND_2_4GHZ";
        width = 20; // Must be explicit for 2.4 GHz
    }
    else
    {
        bandStr = "BAND_5GHZ";
        width = 0; // Auto-width for 5 GHz
    }

    std::ostringstream scanChStr;
    scanChStr << "{" << (int)firstChannel << ", " << width << ", " << bandStr << ", 0}";
    scanPhyHelperPtr->Set("ChannelSettings", StringValue(scanChStr.str()));

    // CHANGE: Disable TX power to make scanning radio listen-only
    scanPhyHelperPtr->Set("TxPowerStart", DoubleValue(-100.0));
    scanPhyHelperPtr->Set("TxPowerEnd", DoubleValue(-100.0));

    // Zero channel switch delay for passive scanning radio
    scanPhyHelperPtr->Set("ChannelSwitchDelay", TimeValue(MicroSeconds(0)));

    WifiHelper wifi;
    // Use 802.11ax for multi-band scanning (2.4 GHz + 5 GHz + 6 GHz) and wider channel support (20/40/80/160 MHz)
    // AdhocWifiMac is used with TX disabled (-100dBm) to avoid ADDBA/Block Ack issues
    // Safe because: scanning radio is TX-disabled, only receives beacons, IE parsing uses raw bytes
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                  "DataMode", StringValue("OfdmRate6Mbps"),
                                  "ControlMode", StringValue("OfdmRate6Mbps"));

    WifiMacHelper scanMac;
    // CHANGE: Use AdhocWifiMac instead of StaWifiMac for true listen-only mode
    // AdhocWifiMac doesn't send beacons, probe requests, or association frames
    scanMac.SetType("ns3::AdhocWifiMac");

    NetDeviceContainer scanDevice = wifi.Install(*scanPhyHelperPtr, scanMac, node);
    Ptr<WifiNetDevice> scanNetDev = DynamicCast<WifiNetDevice>(scanDevice.Get(0));
    Ptr<WifiPhy> scanPhyPtr = scanNetDev->GetPhy();
    Ptr<WifiMac> scanMacPtr = scanNetDev->GetMac();

    // CHANGE: Further disable TX at MAC layer
    scanMacPtr->SetAttribute("BE_MaxAmpduSize", UintegerValue(0));
    scanMacPtr->SetAttribute("BK_MaxAmpduSize", UintegerValue(0));
    scanMacPtr->SetAttribute("VI_MaxAmpduSize", UintegerValue(0));
    scanMacPtr->SetAttribute("VO_MaxAmpduSize", UintegerValue(0));

    // Use the DESIRED BSSID (existing device's BSSID) instead of creating a new operating device
    Mac48Address operatingMac = desiredBssid;
    Mac48Address scanningMac = scanNetDev->GetMac()->GetAddress();

    // Track MAC addresses by node ID for easy retrieval
    uint32_t nodeId = node->GetId();
    m_nodeToOperatingMac[nodeId] = operatingMac;
    m_nodeToScanningMac[nodeId] = scanningMac;

    NS_LOG_INFO("╔═══════════════════════════════════════════════════════════════");
    NS_LOG_INFO("║ DUAL-PHY SNIFFER INSTALLATION ║");
    NS_LOG_INFO("╠═══════════════════════════════════════════════════════════════");
    NS_LOG_INFO("  Node ID: " << nodeId);
    NS_LOG_INFO("  Operating BSSID (rxBssid): " << operatingMac);
    NS_LOG_INFO("    - This is the DESIRED BSSID (existing AP/STA)");
    NS_LOG_INFO("    - Operating Channel: " << (int)operatingChannel);
    NS_LOG_INFO("    - Role: Identifies WHO is listening");
    NS_LOG_INFO("  Scanning MAC: " << scanningMac);
    NS_LOG_INFO("    - Mode: LISTEN-ONLY (no TX)");
    NS_LOG_INFO("    - Channels: Hops through " << m_scanningChannels.size() << " channels");
    NS_LOG_INFO("    - Role: The actual radio doing the scanning");
    NS_LOG_INFO("╚═══════════════════════════════════════════════════════════════");

    // Use UnifiedPhySniffer for efficient beacon processing
    // This eliminates redundant packet copies and calculations across modules
    Ptr<UnifiedPhySniffer> sniffer = CreateObject<UnifiedPhySniffer>();
    sniffer->InstallOnPhy(scanPhyPtr, operatingMac);

    // Subscribe to beacons only - the unified sniffer handles early filtering
    // and pre-computes RSSI/SNR/RCPI/channel
    Mac48Address rxBssid = operatingMac;
    sniffer->SubscribeBeacons([this, rxBssid](ParsedFrameContext* ctx) {
        this->UnifiedBeaconCallback(rxBssid, ctx);
    });

    // Store sniffer to keep it alive (prevent premature destruction)
    m_unifiedSniffers.push_back(sniffer);

    NS_LOG_DEBUG("  UnifiedPhySniffer installed on scanning PHY");
    NS_LOG_DEBUG("    rxBssid = " << rxBssid << " (for beacon filtering)");

    // Track this scanning MAC and PHY for later channel hopping
    m_scanningDevices.push_back(std::make_tuple(scanMacPtr, scanPhyPtr, operatingMac));
    m_hopIndex[scanMacPtr] = 0;

    return scanDevice; // Return scanning device
}

Mac48Address
DualPhySnifferHelper::GetOperatingMac(uint32_t nodeId) const
{
    auto it = m_nodeToOperatingMac.find(nodeId);
    if (it != m_nodeToOperatingMac.end())
    {
        return it->second;
    }
    return Mac48Address("00:00:00:00:00:00"); // Return null MAC if not found
}

Mac48Address
DualPhySnifferHelper::GetScanningMac(uint32_t nodeId) const
{
    auto it = m_nodeToScanningMac.find(nodeId);
    if (it != m_nodeToScanningMac.end())
    {
        return it->second;
    }
    return Mac48Address("00:00:00:00:00:00"); // Return null MAC if not found
}

void
DualPhySnifferHelper::StartChannelHopping()
{
    NS_LOG_FUNCTION(this);

    for (const auto& entry : m_scanningDevices)
    {
        Ptr<WifiMac> scanMac = std::get<0>(entry);
        Ptr<WifiPhy> scanPhy = std::get<1>(entry);
        Mac48Address bssid = std::get<2>(entry);

        // Schedule first hop after initial beacon capture period
        Simulator::Schedule(m_hopInterval,
                           &DualPhySnifferHelper::ScheduleNextHop,
                           this, scanMac, scanPhy, bssid);

        NS_LOG_INFO("Scheduled channel hopping for device " << bssid);
    }
}

void
DualPhySnifferHelper::MonitorSnifferRxStatic(TraceContext* ctx,
                                                 Ptr<const Packet> packet,
                                                 uint16_t channelFreq,
                                                 WifiTxVector txVector,
                                                 MpduInfo mpdu,
                                                 SignalNoiseDbm signalNoise,
                                                 uint16_t staId)
{
    ctx->helper->ScanningRadioCallback(ctx->rxBssid, packet, channelFreq,
                                      txVector, mpdu, signalNoise, staId);
}

void
DualPhySnifferHelper::ScanningRadioCallback(Mac48Address rxBssid,
                                               Ptr<const Packet> packet,
                                               uint16_t channelFreq,
                                               WifiTxVector txVector,
                                               MpduInfo mpdu,
                                               SignalNoiseDbm signalNoise,
                                               uint16_t staId)
{
    // Early check: packet must be large enough for WiFi MAC header
    // Management frames like beacons are at least 24 bytes
    if (packet->GetSize() < 24)
    {
        return;  // Silently reject - too small
    }

    // Check frame control field (first 2 bytes) directly to filter beacons before deserializing header
    // This avoids potential NS_ASSERT crashes from corrupted headers
    uint8_t frameControl[2];
    packet->CopyData(frameControl, 2);

    // Frame Control format (little-endian):
    // Byte 0: |Protocol(2)|Type(2)|Subtype(4)|
    // Byte 1: |ToDS(1)|FromDS(1)|...|
    // Type=00 (Management), Subtype=1000 (Beacon) = 0x80
    // So for beacons: byte0 & 0xFC should be 0x80
    uint8_t type = (frameControl[0] >> 2) & 0x03;    // Bits 2-3
    uint8_t subtype = (frameControl[0] >> 4) & 0x0F; // Bits 4-7

    // Check if it's a management frame (type=0) and beacon (subtype=8)
    if (type != 0 || subtype != 8)
    {
        return;  // Silently reject non-beacons
    }

    // Deserialize header - safe now since we verified it's a beacon
    WifiMacHeader hdr;
    Ptr<Packet> copy = packet->Copy();
    copy->RemoveHeader(hdr);

    // Get transmitter BSSID (Addr2 for beacons)
    Mac48Address txBssid = hdr.GetAddr2();

    // Reject self-beacons (same device receiving its own beacon)
    if (txBssid == rxBssid)
    {
        return;  // Silent rejection
    }

    // Filter: only accept beacons from valid APs (if filter is configured)
    if (!m_validApBssids.empty() && m_validApBssids.find(txBssid) == m_validApBssids.end())
    {
        NS_LOG_DEBUG("ScanningRadioCallback: Rejecting beacon from non-AP source: " << txBssid);
        return;  // Not from a valid AP
    }

    // Convert frequency to channel number
    // IEEE 802.11 channel assignments:
    // - 2.4 GHz: 2407 MHz + (5 MHz × channel) for channels 1-13, channel 14 at 2484 MHz
    // - 5 GHz: 5000 MHz + (5 MHz × channel) for channels 36-177
    // - 6 GHz: 5950 MHz + (5 MHz × channel) for channels 1-233
    uint8_t channel;
    std::string bandType;
    if (channelFreq >= 5950)
    {
        // 6 GHz band (5955-7115 MHz)
        channel = (channelFreq - 5950) / 5;
        bandType = "6 GHz";
    }
    else if (channelFreq >= 5000)
    {
        // 5 GHz band (5150-5885 MHz)
        channel = (channelFreq - 5000) / 5;
        bandType = "5 GHz";
    }
    else
    {
        // 2.4 GHz band (2412-2484 MHz)
        channel = (channelFreq - 2407) / 5;
        bandType = "2.4 GHz";
    }

    NS_LOG_DEBUG("Channel: " << (int)channel << " (" << bandType << "), Freq: " << channelFreq << " MHz");

    // Convert RSSI (dBm) to RCPI (0-220)
    double rssi = signalNoise.signal;
    double rcpi = (rssi + 110.0) * 2.0;
    rcpi = std::max(0.0, std::min(220.0, rcpi)); // Clamp to valid range
    double snr = signalNoise.signal - signalNoise.noise;

    NS_LOG_DEBUG("Signal: RSSI=" << rssi << " dBm, RCPI=" << rcpi << ", Noise=" << signalNoise.noise << " dBm, SNR=" << snr << " dB");

    // Create measurement structure
    DualPhyMeasurement measurement;
    measurement.receiverBssid = rxBssid;
    measurement.transmitterBssid = txBssid;
    measurement.channel = channel;
    measurement.rcpi = rcpi;
    measurement.rssi = rssi;  // Include raw RSSI for flexibility
    measurement.timestamp = Simulator::Now().GetSeconds();

    // Parse BSS Load IE from beacon body using raw bytes (safe method - no RemoveHeader on body)
    // After MAC header removal, 'copy' contains: Timestamp(8) + BeaconInterval(2) + Capability(2) + IEs
    uint16_t parsedStaCount = 0;
    uint8_t parsedChannelUtil = 0;
    uint8_t parsedWifiUtil = 0;       // WiFi utilization (from AAC high byte)
    uint8_t parsedNonWifiUtil = 0;    // Non-WiFi utilization (from AAC low byte)
    uint16_t parsedChannelWidth = 20;  // Default 20 MHz

    uint32_t packetSize = copy->GetSize();
    if (packetSize > 12)  // Need at least fixed fields + some IEs
    {
        // Copy raw beacon body bytes
        uint8_t* beaconBody = new uint8_t[packetSize];
        copy->CopyData(beaconBody, packetSize);

        // Skip fixed fields: Timestamp(8) + BeaconInterval(2) + Capability(2) = 12 bytes
        uint32_t offset = 12;

        // Parse Information Elements
        while (offset + 2 <= packetSize)  // Need at least Element ID + Length
        {
            uint8_t elementId = beaconBody[offset];
            uint8_t elementLen = beaconBody[offset + 1];

            // Bounds check: ensure we don't read past end of packet
            if (offset + 2 + elementLen > packetSize)
            {
                break;  // Malformed IE or end of data
            }

            // BSS Load IE (Element ID 11, Length 5)
            if (elementId == 11 && elementLen == 5)
            {
                // Station Count: 2 bytes little-endian at offset+2
                parsedStaCount = beaconBody[offset + 2] | (beaconBody[offset + 3] << 8);
                // Channel Utilization: 1 byte at offset+4
                parsedChannelUtil = beaconBody[offset + 4];
                // Available Admission Capacity: 2 bytes little-endian at offset+5
                // We repurpose AAC: high byte = WiFi util, low byte = non-WiFi util
                uint16_t aac = beaconBody[offset + 5] | (beaconBody[offset + 6] << 8);
                parsedWifiUtil = static_cast<uint8_t>((aac >> 8) & 0xFF);      // High byte
                parsedNonWifiUtil = static_cast<uint8_t>(aac & 0xFF);          // Low byte
                NS_LOG_DEBUG("Parsed BSS Load IE: StaCount=" << parsedStaCount
                             << ", ChannelUtil=" << (int)parsedChannelUtil
                             << ", WifiUtil=" << (int)parsedWifiUtil
                             << ", NonWifiUtil=" << (int)parsedNonWifiUtil);
            }

            // HT Operation IE (Element ID 61) - tells us ACTUAL 20/40 MHz operation
            // This is more accurate than HT Capabilities which only shows what's supported
            if (elementId == 61 && elementLen >= 2)
            {
                // Byte 1 (offset+2) contains STA Channel Width field
                uint8_t htOpInfo = beaconBody[offset + 3];  // HT Operation Info byte 1
                // Bit 1-0: STA Channel Width (0 = 20 MHz only, any other = 20/40 MHz)
                // But for actual operation, check Secondary Channel Offset (bits 0-1):
                //   0 = no secondary channel (20 MHz)
                //   1 = secondary above
                //   3 = secondary below
                uint8_t secChannelOffset = htOpInfo & 0x03;
                if (secChannelOffset == 1 || secChannelOffset == 3)
                {
                    parsedChannelWidth = 40;  // Actually operating at 40 MHz
                }
                // else remains 20 MHz
            }

            // VHT Operation IE (Element ID 192) - tells us ACTUAL channel width
            // This is more accurate than VHT Capabilities which only shows what's supported
            if (elementId == 192 && elementLen >= 1)
            {
                // Byte 0 (offset+2): Channel Width field
                //   0 = 20 or 40 MHz (use HT Operation to distinguish)
                //   1 = 80 MHz
                //   2 = 160 MHz
                //   3 = 80+80 MHz
                uint8_t vhtChannelWidth = beaconBody[offset + 2];
                if (vhtChannelWidth == 1)
                {
                    parsedChannelWidth = 80;
                }
                else if (vhtChannelWidth == 2 || vhtChannelWidth == 3)
                {
                    parsedChannelWidth = 160;
                }
                // else vhtChannelWidth == 0: use HT Operation (20 or 40 MHz)
            }

            // Move to next IE
            offset += 2 + elementLen;
        }

        delete[] beaconBody;
    }

    // Store beacon in internal cache with parsed IE data
    StoreBeacon(measurement, snr, parsedStaCount, parsedChannelUtil, parsedChannelWidth,
                parsedWifiUtil, parsedNonWifiUtil);

    // Log beacon cache size after storing
    NS_LOG_DEBUG("Beacon stored. Total beacons in cache: " << m_beaconCache.size()
                 << " (RX: " << rxBssid << " <- TX: " << txBssid
                 << ", StaCount=" << parsedStaCount << ", ChUtil=" << (int)parsedChannelUtil << ")");

    // Invoke user callback with measurement data (optional, for backward compatibility)
    if (!m_measurementCallback.IsNull())
    {
        m_measurementCallback(measurement);
    }
}

void
DualPhySnifferHelper::UnifiedBeaconCallback(Mac48Address rxBssid, ParsedFrameContext* ctx)
{
    // Get transmitter BSSID from pre-parsed addresses
    ctx->EnsureAddressesParsed();
    Mac48Address txBssid = ctx->addr2;

    // Reject self-beacons (same device receiving its own beacon)
    if (txBssid == rxBssid)
    {
        return;  // Silent rejection
    }

    // Filter: only accept beacons from valid APs (if filter is configured)
    if (!m_validApBssids.empty() && m_validApBssids.find(txBssid) == m_validApBssids.end())
    {
        NS_LOG_DEBUG("Rejecting beacon from non-AP source: " << txBssid);
        return;  // Not from a valid AP
    }

    NS_LOG_DEBUG("UnifiedBeaconCallback: Channel=" << (int)ctx->channel
                 << ", Freq=" << ctx->channelFreqMhz << " MHz");

    // Use pre-computed signal metrics from unified sniffer
    NS_LOG_DEBUG("Signal: RSSI=" << ctx->rssi << " dBm, RCPI=" << (int)ctx->rcpi
                 << ", Noise=" << ctx->noise << " dBm, SNR=" << ctx->snr << " dB");

    // Create measurement structure with pre-computed values
    DualPhyMeasurement measurement;
    measurement.receiverBssid = rxBssid;
    measurement.transmitterBssid = txBssid;
    measurement.channel = ctx->channel;
    measurement.rcpi = ctx->rcpi;
    measurement.rssi = ctx->rssi;
    measurement.timestamp = Simulator::Now().GetSeconds();

    // Parse BSS Load IE from beacon body - still need packet copy for IE parsing
    uint16_t parsedStaCount = 0;
    uint8_t parsedChannelUtil = 0;
    uint8_t parsedWifiUtil = 0;
    uint8_t parsedNonWifiUtil = 0;
    uint16_t parsedChannelWidth = 20;

    // Get payload copy for IE parsing (only copy made in this callback)
    Ptr<Packet> payload = ctx->GetPayloadCopy();
    if (payload && payload->GetSize() > 12)
    {
        uint32_t packetSize = payload->GetSize();
        uint8_t* beaconBody = new uint8_t[packetSize];
        payload->CopyData(beaconBody, packetSize);

        // Skip fixed fields: Timestamp(8) + BeaconInterval(2) + Capability(2) = 12 bytes
        uint32_t offset = 12;

        // Parse Information Elements
        while (offset + 2 <= packetSize)
        {
            uint8_t elementId = beaconBody[offset];
            uint8_t elementLen = beaconBody[offset + 1];

            if (offset + 2 + elementLen > packetSize)
            {
                break;
            }

            // BSS Load IE (Element ID 11, Length 5)
            if (elementId == 11 && elementLen == 5)
            {
                parsedStaCount = beaconBody[offset + 2] | (beaconBody[offset + 3] << 8);
                parsedChannelUtil = beaconBody[offset + 4];
                uint16_t aac = beaconBody[offset + 5] | (beaconBody[offset + 6] << 8);
                parsedWifiUtil = static_cast<uint8_t>((aac >> 8) & 0xFF);
                parsedNonWifiUtil = static_cast<uint8_t>(aac & 0xFF);
            }

            // HT Capabilities IE (Element ID 45)
            if (elementId == 45 && elementLen >= 2)
            {
                uint16_t htCapInfo = beaconBody[offset + 2] | (beaconBody[offset + 3] << 8);
                if (htCapInfo & 0x0002)
                {
                    parsedChannelWidth = 40;
                }
            }

            // VHT Capabilities IE (Element ID 191)
            if (elementId == 191 && elementLen >= 4)
            {
                uint32_t vhtCapInfo = beaconBody[offset + 2] |
                                      (beaconBody[offset + 3] << 8) |
                                      (beaconBody[offset + 4] << 16) |
                                      (beaconBody[offset + 5] << 24);
                uint8_t widthSet = (vhtCapInfo >> 2) & 0x03;
                if (widthSet >= 1)
                {
                    parsedChannelWidth = 160;
                }
                else
                {
                    parsedChannelWidth = 80;
                }
            }

            offset += 2 + elementLen;
        }

        delete[] beaconBody;
    }

    // Store beacon with parsed IE data
    StoreBeacon(measurement, ctx->snr, parsedStaCount, parsedChannelUtil, parsedChannelWidth,
                parsedWifiUtil, parsedNonWifiUtil);

    NS_LOG_DEBUG("Beacon stored via UnifiedSniffer. Cache size: " << m_beaconCache.size()
                 << " (RX: " << rxBssid << " <- TX: " << txBssid << ")");

    // Invoke user callback
    if (!m_measurementCallback.IsNull())
    {
        m_measurementCallback(measurement);
    }
}

void
DualPhySnifferHelper::ScheduleNextHop(Ptr<WifiMac> scanMac, Ptr<WifiPhy> scanPhy, Mac48Address deviceBssid)
{
    HopChannel(scanMac, scanPhy, deviceBssid);

    // Schedule next hop
    Simulator::Schedule(m_hopInterval,
                       &DualPhySnifferHelper::ScheduleNextHop,
                       this, scanMac, scanPhy, deviceBssid);
}

void
DualPhySnifferHelper::HopChannel(Ptr<WifiMac> scanMac, Ptr<WifiPhy> scanPhy, Mac48Address deviceBssid)
{
    // Get next channel in rotation
    uint32_t& idx = m_hopIndex[scanMac];
    idx = (idx + 1) % m_scanningChannels.size();
    uint16_t nextChannel = m_scanningChannels[idx];  // Use uint16_t for SwitchChannel

    // MAC-level coordination: check if operating PHY on same node is busy
    Ptr<NetDevice> scanNetDevice = scanPhy->GetDevice();
    if (scanNetDevice)
    {
        Ptr<Node> node = scanNetDevice->GetNode();
        uint32_t scanDeviceIdx = scanNetDevice->GetIfIndex();

        // Find the operating PHY (Device 0 is typically the operating PHY)
        // Scanning PHY is Device 1 or higher
        for (uint32_t i = 0; i < node->GetNDevices(); i++)
        {
            if (i == scanDeviceIdx)
                continue;  // Skip self

            Ptr<WifiNetDevice> otherDevice = DynamicCast<WifiNetDevice>(node->GetDevice(i));
            if (otherDevice)
            {
                Ptr<WifiPhy> operatingPhy = otherDevice->GetPhy();
                if (operatingPhy && (operatingPhy->IsStateTx() || operatingPhy->IsStateSwitching()))
                {
                    // Operating PHY is busy - defer this channel hop
                    NS_LOG_DEBUG("Scanning PHY on Node " << node->GetId()
                                 << " deferring channel hop - operating PHY busy (TX="
                                 << operatingPhy->IsStateTx() << ", Switching="
                                 << operatingPhy->IsStateSwitching() << ")");

                    // Reschedule this hop for 10ms later
                    Simulator::Schedule(MilliSeconds(10),
                                       &DualPhySnifferHelper::HopChannel,
                                       this, scanMac, scanPhy, deviceBssid);
                    return;  // Don't switch now
                }
            }
        }
    }

    // Use MAC-level SwitchChannel which automatically determines channel width
    // Channel numbers encode both frequency and width:
    // - 20 MHz: 36, 40, 44, 48, 52, 56, 60, 64, etc.
    // - 40 MHz: 38, 46, 54, 62, 102, 110, 118, 126, etc.
    // - 80 MHz: 42, 58, 106, 122, 138, 155
    // - 160 MHz: 50, 114
    scanMac->SwitchChannel(nextChannel, 0);  // linkId = 0 for single link

    // Determine band for logging
    WifiPhyBand hopBand = GetBandForChannel(static_cast<uint8_t>(nextChannel));
    std::string hopBandStr;
    if (hopBand == WIFI_PHY_BAND_2_4GHZ)
        hopBandStr = "2.4GHz";
    else if (hopBand == WIFI_PHY_BAND_5GHZ)
        hopBandStr = "5GHz";
    else
        hopBandStr = "6GHz";

    NS_LOG_DEBUG("Device " << deviceBssid << " scanning radio hopped to channel "
                 << nextChannel << " (" << hopBandStr << ") using MAC SwitchChannel at "
                 << Simulator::Now().GetSeconds() << "s");
}

// ========== BEACON STORAGE METHODS ==========

void
DualPhySnifferHelper::StoreBeacon(const DualPhyMeasurement& measurement, double snr,
                                   uint16_t staCount, uint8_t channelUtil, uint16_t channelWidth,
                                   uint8_t wifiUtil, uint8_t nonWifiUtil)
{
    // Determine frequency band from channel using helper method
    WifiPhyBand band = GetBandForChannel(measurement.channel);

    // Create or update beacon entry
    auto key = std::make_pair(measurement.receiverBssid, measurement.transmitterBssid);
    auto it = m_beaconCache.find(key);

    if (it != m_beaconCache.end())
    {
        // Update existing entry
        it->second.rssi = measurement.rssi;
        it->second.rcpi = measurement.rcpi;
        it->second.snr = snr;
        it->second.channel = measurement.channel;
        it->second.band = band;
        it->second.timestamp = Simulator::Now();
        it->second.beaconCount++;
        // Update IE fields with parsed values (use latest)
        it->second.staCount = staCount;
        it->second.channelUtilization = channelUtil;
        it->second.channelWidth = channelWidth;
        it->second.wifiUtilization = wifiUtil;
        it->second.nonWifiUtilization = nonWifiUtil;
    }
    else
    {
        // Create new entry
        BeaconInfo info;
        info.bssid = measurement.transmitterBssid;
        info.receivedBy = measurement.receiverBssid;
        info.rssi = measurement.rssi;
        info.rcpi = measurement.rcpi;
        info.snr = snr;
        info.channel = measurement.channel;
        info.band = band;
        info.timestamp = Simulator::Now();
        info.beaconCount = 1;

        // Use parsed IE fields from beacon
        info.channelWidth = channelWidth;
        info.staCount = staCount;
        info.channelUtilization = channelUtil;
        info.wifiUtilization = wifiUtil;
        info.nonWifiUtilization = nonWifiUtil;

        m_beaconCache[key] = info;
    }

    // Purge old entries if needed
    PurgeOldBeacons();
}

void
DualPhySnifferHelper::PurgeOldBeacons()
{
    Time now = Simulator::Now();

    // Remove entries older than max age
    if (m_beaconMaxAge > Seconds(0))
    {
        auto it = m_beaconCache.begin();
        while (it != m_beaconCache.end())
        {
            if (now - it->second.timestamp > m_beaconMaxAge)
            {
                it = m_beaconCache.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Remove oldest entries if over max limit
    if (m_beaconMaxEntries > 0 && m_beaconCache.size() > m_beaconMaxEntries)
    {
        // Find oldest entry
        while (m_beaconCache.size() > m_beaconMaxEntries)
        {
            auto oldest = m_beaconCache.begin();
            for (auto it = m_beaconCache.begin(); it != m_beaconCache.end(); ++it)
            {
                if (it->second.timestamp < oldest->second.timestamp)
                {
                    oldest = it;
                }
            }
            m_beaconCache.erase(oldest);
        }
    }
}

const std::map<std::pair<Mac48Address, Mac48Address>, BeaconInfo>&
DualPhySnifferHelper::GetAllBeacons() const
{
    return m_beaconCache;
}

std::vector<BeaconInfo>
DualPhySnifferHelper::GetBeaconsReceivedBy(Mac48Address receiverBssid) const
{
    std::vector<BeaconInfo> result;
    for (const auto& entry : m_beaconCache)
    {
        if (entry.first.first == receiverBssid)
        {
            result.push_back(entry.second);
        }
    }
    return result;
}

std::vector<BeaconInfo>
DualPhySnifferHelper::GetBeaconsFrom(Mac48Address transmitterBssid) const
{
    std::vector<BeaconInfo> result;
    for (const auto& entry : m_beaconCache)
    {
        if (entry.first.second == transmitterBssid)
        {
            result.push_back(entry.second);
        }
    }
    return result;
}

void
DualPhySnifferHelper::ClearBeaconCache()
{
    m_beaconCache.clear();
}

void
DualPhySnifferHelper::ClearBeaconsReceivedBy(Mac48Address receiverBssid)
{
    // Iterate through the beacon cache and remove all entries where the receiver matches
    auto it = m_beaconCache.begin();
    while (it != m_beaconCache.end())
    {
        if (it->first.first == receiverBssid)  // First element of pair is receiverBssid
        {
            it = m_beaconCache.erase(it);
        }
        else
        {
            ++it;
        }
    }
    NS_LOG_DEBUG("Cleared beacon cache for receiver " << receiverBssid);
}

void
DualPhySnifferHelper::SetBeaconMaxAge(Time maxAge)
{
    m_beaconMaxAge = maxAge;
}

void
DualPhySnifferHelper::SetBeaconMaxEntries(uint32_t maxEntries)
{
    m_beaconMaxEntries = maxEntries;
}

} // namespace ns3
