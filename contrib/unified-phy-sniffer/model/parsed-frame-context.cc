/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Unified PHY Sniffer
 */

#include "parsed-frame-context.h"

#include "ns3/simulator.h"

#include <algorithm>
#include <cstring>

namespace ns3
{

void
ParsedFrameContext::Reset()
{
    originalPacket = nullptr;
    txVector = nullptr;
    type = FrameType::UNKNOWN;
    subtype = 0;
    toDs = false;
    fromDs = false;
    rssi = 0.0;
    noise = 0.0;
    snr = 0.0;
    rcpi = 0;
    rsni = 0;
    channelFreqMhz = 0;
    channel = 0;
    band = WifiPhyBand::WIFI_PHY_BAND_UNSPECIFIED;
    staId = 0;
    addressesParsed = false;
    addr1 = Mac48Address();
    addr2 = Mac48Address();
    addr3 = Mac48Address();
    actionParsed = false;
    actionCategory = 0;
    actionCode = 0;
    dialogToken = 0;
    nodeId = 0;
    deviceId = 0;
}

void
ParsedFrameContext::Initialize(Ptr<const Packet> packet,
                                uint16_t channelFreq,
                                const WifiTxVector& txVec,
                                double signalDbm,
                                double noiseDbm,
                                uint16_t sta)
{
    originalPacket = packet;
    txVector = &txVec;
    channelFreqMhz = channelFreq;
    staId = sta;
    timestamp = Simulator::Now();

    // Store signal info
    rssi = signalDbm;
    noise = noiseDbm;
    snr = rssi - noise;

    // Compute derived metrics
    ComputeMetrics();
    DeriveChannelFromFreq();

    // Early frame classification (read only 2 bytes)
    if (packet->GetSize() >= 2)
    {
        uint8_t fc[2];
        packet->CopyData(fc, 2);
        ClassifyFrame(fc[0], fc[1]);
    }
    else
    {
        type = FrameType::UNKNOWN;
    }
}

void
ParsedFrameContext::ClassifyFrame(uint8_t fc0, uint8_t fc1)
{
    // Frame Control field layout:
    // Byte 0: Protocol(2) | Type(2) | Subtype(4)
    // Byte 1: ToDS(1) | FromDS(1) | MoreFrag(1) | Retry(1) | PwrMgt(1) | MoreData(1) | WEP(1) |
    // Order(1)

    uint8_t frameType = (fc0 >> 2) & 0x03;
    subtype = (fc0 >> 4) & 0x0F;
    toDs = (fc1 & 0x01) != 0;
    fromDs = (fc1 & 0x02) != 0;

    switch (frameType)
    {
    case 0: // Management
        switch (subtype)
        {
        case 8: // Beacon
            type = FrameType::BEACON;
            break;
        case 4: // Probe Request
            type = FrameType::PROBE_REQUEST;
            break;
        case 5: // Probe Response
            type = FrameType::PROBE_RESPONSE;
            break;
        case 13: // Action
            type = FrameType::ACTION;
            break;
        default:
            type = FrameType::MANAGEMENT_OTHER;
            break;
        }
        break;
    case 1: // Control
        type = FrameType::CONTROL;
        break;
    case 2: // Data
        type = FrameType::DATA;
        break;
    default:
        type = FrameType::UNKNOWN;
        break;
    }
}

void
ParsedFrameContext::DeriveChannelFromFreq()
{
    if (channelFreqMhz >= 2400 && channelFreqMhz <= 2484)
    {
        // 2.4 GHz band
        band = WifiPhyBand::WIFI_PHY_BAND_2_4GHZ;
        if (channelFreqMhz == 2484)
        {
            channel = 14;
        }
        else
        {
            channel = static_cast<uint8_t>((channelFreqMhz - 2407) / 5);
        }
    }
    else if (channelFreqMhz >= 5150 && channelFreqMhz <= 5895)
    {
        // 5 GHz band
        band = WifiPhyBand::WIFI_PHY_BAND_5GHZ;
        channel = static_cast<uint8_t>((channelFreqMhz - 5000) / 5);
    }
    else if (channelFreqMhz >= 5925 && channelFreqMhz <= 7125)
    {
        // 6 GHz band
        band = WifiPhyBand::WIFI_PHY_BAND_6GHZ;
        channel = static_cast<uint8_t>((channelFreqMhz - 5950) / 5);
    }
    else
    {
        band = WifiPhyBand::WIFI_PHY_BAND_UNSPECIFIED;
        channel = 0;
    }
}

void
ParsedFrameContext::ComputeMetrics()
{
    // RCPI = 2 * (RSSI + 110), clamped to [0, 220]
    // Per IEEE 802.11k-2008
    double rcpiValue = (rssi + 110.0) * 2.0;
    rcpi = static_cast<uint8_t>(std::max(0.0, std::min(220.0, rcpiValue)));

    // RSNI = 2 * SNR, clamped to [0, 255]
    double rsniValue = snr * 2.0;
    rsni = static_cast<uint8_t>(std::max(0.0, std::min(255.0, rsniValue)));
}

void
ParsedFrameContext::EnsureAddressesParsed()
{
    if (addressesParsed)
    {
        return;
    }

    // MAC header structure (minimum 24 bytes):
    // Frame Control (2) + Duration (2) + Addr1 (6) + Addr2 (6) + Addr3 (6) + Seq (2)
    // Addresses start at offset 4

    if (!originalPacket || originalPacket->GetSize() < 22)
    {
        addressesParsed = true;
        return;
    }

    // Read entire header (first 22 bytes: FC(2) + Dur(2) + Addr1(6) + Addr2(6) + Addr3(6))
    uint8_t headerBytes[22];
    originalPacket->CopyData(headerBytes, 22);

    // Extract addresses from header (starting at offset 4)
    addr1.CopyFrom(headerBytes + 4);
    addr2.CopyFrom(headerBytes + 10);
    addr3.CopyFrom(headerBytes + 16);

    addressesParsed = true;
}

void
ParsedFrameContext::EnsureActionParsed()
{
    if (actionParsed)
    {
        return;
    }

    if (type != FrameType::ACTION)
    {
        actionParsed = true;
        return;
    }

    // Action frame body starts after MAC header (24 bytes for non-QoS)
    // Category (1) + Action Code (1) + Dialog Token (1) + ...
    const uint32_t macHeaderSize = 24;
    const uint32_t actionFieldsSize = 3;
    const uint32_t totalSize = macHeaderSize + actionFieldsSize;

    if (!originalPacket || originalPacket->GetSize() < totalSize)
    {
        actionParsed = true;
        return;
    }

    // Read MAC header + action fields in one call
    uint8_t buffer[27]; // 24 + 3
    originalPacket->CopyData(buffer, totalSize);

    // Action fields start at offset 24 (after MAC header)
    actionCategory = buffer[24];
    actionCode = buffer[25];
    dialogToken = buffer[26];

    actionParsed = true;
}

Ptr<Packet>
ParsedFrameContext::GetPayloadCopy() const
{
    if (!originalPacket)
    {
        return nullptr;
    }

    // Create a copy and remove MAC header
    Ptr<Packet> copy = originalPacket->Copy();

    // MAC header is typically 24 bytes (non-QoS) or 26 bytes (QoS)
    // For simplicity, remove 24 bytes
    const uint32_t macHeaderSize = 24;

    if (copy->GetSize() > macHeaderSize)
    {
        copy->RemoveAtStart(macHeaderSize);
    }

    return copy;
}

} // namespace ns3
