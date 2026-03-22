#ifndef BEACON_NEIGHBOR_MODEL_H
#define BEACON_NEIGHBOR_MODEL_H

// Add a doxygen group for this module.
// If you have more than one file, this should be in only one of them.
/**
 * @defgroup beacon-neighbor-protocol-11k Description of the beacon-neighbor-protocol-11k
 */

#include "ns3/core-module.h"
#include "ns3/wifi-module.h"

#include <arpa/inet.h> // For htonl

namespace ns3
{

// ===== RSSI/SNR Converters =====
inline uint8_t
RssiToRcpi(double rssi_dbm)
{
    int rcpi = static_cast<int>((rssi_dbm + 110) * 2);
    return std::max(0, std::min(255, rcpi));
}

inline double
RcpiToRssi(uint8_t rcpi)
{
    return (rcpi / 2.0) - 110.0;
}

inline uint8_t
SnrToRsni(double snr_db)
{
    int rsni = static_cast<int>(snr_db * 2);
    return std::max(0, std::min(255, rsni));
}

inline double
RsniToSnr(uint8_t rsni)
{
    return rsni / 2.0;
}

// ===== Data Structures =====
struct ApInfo
{
    Mac48Address bssid;
    std::string ssid;
    uint8_t channel;
    uint8_t regulatoryClass;
    uint8_t phyType;
    Vector position;
    uint8_t load;        // ADD THIS - AP client count
    Time lastDiscovered; // ADD THIS - when discovered
};

struct BeaconMeasurement
{
    Mac48Address bssid;
    double rssi;
    double snr;
    uint8_t channel;
};

// Raw beacon report data (for trace callbacks)
struct BeaconReportData
{
    Mac48Address bssid;
    uint8_t channel;
    uint8_t regulatoryClass;
    uint8_t rcpi;                   // Raw RCPI value (0-255)
    uint8_t rsni;                   // Raw RSNI value (0-255)
    uint8_t reportedFrameInfo;      // PHY type and frame type
    uint16_t measurementDuration;   // In TUs
    uint64_t actualMeasurementStartTime; // TSF timestamp
    uint8_t antennaID;
    uint32_t parentTSF;
};

// Neighbor report data (for trace callbacks)
struct NeighborReportData
{
    Mac48Address bssid;
    uint32_t bssidInfo;           // BSSID Information field (4 octets)
    uint8_t channel;
    uint8_t regulatoryClass;
    uint8_t phyType;
};

// ===== IEEE 802.11k Frame Structures (Exact Match to Spec) =====

// Neighbor Report Request (Figure 7-101e)
struct NeighborReportRequestFrame
{
    uint8_t category = 5; // Radio Measurement
    uint8_t action = 4;   // Neighbor Report Request
    uint8_t dialogToken;
    // Optional Subelements follow (not implemented for now)
} __attribute__((packed));

// Neighbor Report Response (Figure 7-101f)
struct NeighborReportResponseFrame
{
    uint8_t category = 5; // Radio Measurement
    uint8_t action = 5;   // Neighbor Report Response
    uint8_t dialogToken;
    // Neighbor Report Elements follow
} __attribute__((packed));

// Neighbor Report Element (Figure 7-95b, Section 7.3.2.37)
// LEGACY: Use NeighborReportElement class from neighbor-report-element.h instead
struct NeighborReportElementLegacy
{
    uint8_t elementId = 52;  // Neighbor Report
    uint8_t length = 13;     // Minimum: 13 octets (no optional subelements)
    uint8_t bssid[6];        // 6 octets
    uint32_t bssidInfo;      // 4 octets (Figure 7-95c)
    uint8_t regulatoryClass; // 1 octet
    uint8_t channelNumber;   // 1 octet
    uint8_t phyType;         // 1 octet
    // Optional Subelements follow (variable length)
} __attribute__((packed));

// Beacon Request (Figure 7-62e, Section 7.3.2.21.6)
// NOTE: This is the MEASUREMENT REQUEST ELEMENT, not the full action frame
struct BeaconRequestElement
{
    uint8_t elementID = 38; // Measurement Request
    uint8_t length = 13;    // Base length (can be longer with subelements)
    uint8_t measurementToken;
    uint8_t measurementRequestMode = 0;
    uint8_t measurementType = 5; // Beacon (5)

    // Beacon Request specific fields
    uint8_t regulatoryClass;        // 1 octet
    uint8_t channelNumber;          // 1 octet (0=current, 255=all)
    uint16_t randomizationInterval; // 2 octets (TUs)
    uint16_t measurementDuration;   // 2 octets (TUs)
    uint8_t measurementMode;        // 1 octet (0=Passive, 1=Active, 2=Table)
    uint8_t bssid[6];               // 6 octets (FF:FF:FF:FF:FF:FF = all)
    // Optional Subelements follow (variable)
} __attribute__((packed));

// Action frame wrapper for Beacon Request
struct BeaconRequestFrame
{
    uint8_t category = 5; // Radio Measurement
    uint8_t action = 0;   // Measurement Request
    uint8_t dialogToken;
    uint16_t repetitions = 0;
    // BeaconRequestElement follows
} __attribute__((packed));

// Beacon Report (Figure 7-68c, Section 7.3.2.22.6)
// LEGACY: Use BeaconReportElement class from beacon-report-element.h instead
struct BeaconReportElementLegacy
{
    uint8_t elementID = 39; // Measurement Report
    uint8_t length = 26;    // Base: 26 octets (excludes optional subelements)
    uint8_t measurementToken;
    uint8_t measurementReportMode;
    uint8_t measurementType = 5; // Beacon (5)

    // Beacon Report specific fields
    uint8_t regulatoryClass;             // 1 octet
    uint8_t channelNumber;               // 1 octet
    uint64_t actualMeasurementStartTime; // 8 octets (TSF)
    uint16_t measurementDuration;        // 2 octets (TUs)
    uint8_t reportedFrameInfo;           // 1 octet (Figure 7-68d)
                                         // Bits 0-6: PHY Type
                                         // Bit 7: Frame Type (0=Beacon/Probe, 1=Pilot)
    uint8_t rcpi;                        // 1 octet (RSSI in 0.5 dBm units)
    uint8_t rsni;                        // 1 octet (SNR in 0.5 dB units)
    uint8_t bssid[6];                    // 6 octets
    uint8_t antennaID = 0;               // 1 octet
    uint32_t parentTSF;                  // 4 octets
    // Optional Subelements follow (variable)
} __attribute__((packed));

// Action frame wrapper for Beacon Report
struct BeaconReportFrame
{
    uint8_t category = 5; // Radio Measurement
    uint8_t action = 1;   // Measurement Report
    uint8_t dialogToken;
    // BeaconReportElement follows
} __attribute__((packed));

} // namespace ns3
#endif
