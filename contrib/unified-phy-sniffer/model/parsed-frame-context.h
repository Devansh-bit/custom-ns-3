/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Unified PHY Sniffer
 *
 * Lazy-parsed frame context for efficient packet processing
 */

#ifndef PARSED_FRAME_CONTEXT_H
#define PARSED_FRAME_CONTEXT_H

#include "ns3/mac48-address.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/wifi-phy-band.h"
#include "ns3/wifi-tx-vector.h"

#include <cstdint>

namespace ns3
{

/**
 * \brief Frame type classification
 */
enum class FrameType : uint8_t
{
    UNKNOWN = 0,
    BEACON,
    PROBE_REQUEST,
    PROBE_RESPONSE,
    ACTION,
    DATA,
    CONTROL,
    MANAGEMENT_OTHER
};

/**
 * \brief Frame interest flags for subscription filtering
 */
enum class FrameInterest : uint8_t
{
    NONE = 0,
    BEACON = 1 << 0,
    ACTION_CAT5 = 1 << 1,  // 802.11k Radio Measurement
    ACTION_CAT10 = 1 << 2, // 802.11v WNM (BSS TM)
    ACTION_OTHER = 1 << 3,
    DATA = 1 << 4,
    ALL = 0xFF
};

// Bitwise operators for FrameInterest
inline FrameInterest
operator|(FrameInterest a, FrameInterest b)
{
    return static_cast<FrameInterest>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline FrameInterest
operator&(FrameInterest a, FrameInterest b)
{
    return static_cast<FrameInterest>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool
HasInterest(FrameInterest flags, FrameInterest interest)
{
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(interest)) != 0;
}

/**
 * \brief Lazy-parsed frame context
 *
 * This structure holds pre-computed measurements and provides lazy
 * parsing of header fields to minimize unnecessary processing.
 * Used with ObjectPool for allocation-free packet handling.
 */
struct ParsedFrameContext
{
    // ===== Zero-copy references =====
    Ptr<const Packet> originalPacket; //!< Original packet (never copied)
    const WifiTxVector* txVector;     //!< TX vector pointer (valid during callback)

    // ===== Frame classification (from early 2-byte filter) =====
    FrameType type;    //!< Classified frame type
    uint8_t subtype;   //!< Frame subtype (4 bits)
    bool toDs;         //!< To DS flag
    bool fromDs;       //!< From DS flag

    // ===== Pre-computed signal measurements (done once) =====
    double rssi;    //!< Signal power (dBm)
    double noise;   //!< Noise floor (dBm)
    double snr;     //!< Signal-to-noise ratio (dB)
    uint8_t rcpi;   //!< RCPI: 2 * (rssi + 110), clamped [0, 220]
    uint8_t rsni;   //!< RSNI: 2 * snr, clamped [0, 255]

    // ===== Channel info =====
    uint16_t channelFreqMhz; //!< Channel frequency (MHz)
    uint8_t channel;         //!< Derived channel number
    WifiPhyBand band;        //!< Frequency band

    // ===== MPDU info =====
    uint16_t staId; //!< STA-ID for MU transmissions

    // ===== Lazy-parsed address fields =====
    bool addressesParsed;     //!< Flag: addresses have been parsed
    Mac48Address addr1;       //!< Destination address
    Mac48Address addr2;       //!< Source/Transmitter address
    Mac48Address addr3;       //!< BSSID or other

    // ===== Lazy-parsed action frame fields =====
    bool actionParsed;        //!< Flag: action fields have been parsed
    uint8_t actionCategory;   //!< Action category code
    uint8_t actionCode;       //!< Action code
    uint8_t dialogToken;      //!< Dialog token

    // ===== Timing =====
    Time timestamp;           //!< Reception time

    // ===== Context info =====
    uint32_t nodeId;          //!< Node ID
    uint32_t deviceId;        //!< Device ID

    /**
     * \brief Reset context for reuse from pool
     */
    void Reset();

    /**
     * \brief Initialize context with basic frame info
     * \param packet The received packet
     * \param channelFreq Channel frequency in MHz
     * \param txVec TX vector
     * \param signalDbm Signal power in dBm
     * \param noiseDbm Noise floor in dBm
     * \param sta STA ID
     */
    void Initialize(Ptr<const Packet> packet,
                    uint16_t channelFreq,
                    const WifiTxVector& txVec,
                    double signalDbm,
                    double noiseDbm,
                    uint16_t sta);

    /**
     * \brief Ensure addresses are parsed (lazy)
     *
     * Parses MAC addresses from packet if not already done.
     * Call this before accessing addr1, addr2, addr3.
     */
    void EnsureAddressesParsed();

    /**
     * \brief Ensure action frame fields are parsed (lazy)
     *
     * Parses action category, code, and dialog token if not already done.
     * Only valid for ACTION frame types.
     */
    void EnsureActionParsed();

    /**
     * \brief Get a copy of the payload (on demand)
     *
     * Creates a packet copy only when called. Strips MAC header.
     * \return Copy of packet payload
     */
    Ptr<Packet> GetPayloadCopy() const;

    /**
     * \brief Classify frame type from frame control bytes
     * \param fc0 First byte of frame control
     * \param fc1 Second byte of frame control
     */
    void ClassifyFrame(uint8_t fc0, uint8_t fc1);

    /**
     * \brief Derive channel number from frequency
     */
    void DeriveChannelFromFreq();

    /**
     * \brief Compute signal metrics (RCPI, RSNI)
     */
    void ComputeMetrics();
};

} // namespace ns3

#endif /* PARSED_FRAME_CONTEXT_H */
