/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: [Your Name]
 */

#ifndef BEACON_REPORT_ELEMENT_H
#define BEACON_REPORT_ELEMENT_H

#include "ns3/mac48-address.h"
#include "ns3/wifi-information-element.h"

namespace ns3
{

/**
 * \ingroup wifi
 * \brief IEEE 802.11k Beacon Report Element
 *
 * This class implements the Beacon Report element as defined in
 * IEEE 802.11-2016 Section 9.4.2.22.6 (Beacon Report).
 *
 * The Beacon Report element is sent by a STA in response to a Beacon
 * Measurement Request, containing information about observed beacons.
 *
 * Element format:
 * | Element ID | Length | Token | Report Mode | Type | Regulatory Class | Channel |
 * |     1      |   1    |   1   |      1      |   1  |        1         |    1    |
 *
 * | Start Time | Duration | Frame Info | RCPI | RSNI | BSSID | Antenna ID | Parent TSF |
 * |     8      |    2     |      1     |   1  |   1  |   6   |      1     |      4     |
 *
 * Element ID = 39 (Measurement Report)
 * Measurement Type = 5 (Beacon)
 * Base length = 26 octets (without optional subelements)
 */
class BeaconReportElement : public WifiInformationElement
{
  public:
    BeaconReportElement();

    // WifiInformationElement interface
    WifiInformationElementId ElementId() const override;
    void Print(std::ostream& os) const override;

    // Measurement header fields
    void SetMeasurementToken(uint8_t token);
    uint8_t GetMeasurementToken() const;

    void SetMeasurementReportMode(uint8_t mode);
    uint8_t GetMeasurementReportMode() const;

    // Beacon Report specific fields
    void SetOperatingClass(uint8_t operatingClass);
    uint8_t GetOperatingClass() const;

    void SetChannelNumber(uint8_t channel);
    uint8_t GetChannelNumber() const;

    void SetActualMeasurementStartTime(uint64_t tsf);
    uint64_t GetActualMeasurementStartTime() const;

    void SetMeasurementDuration(uint16_t duration);
    uint16_t GetMeasurementDuration() const;

    /**
     * \brief Set the Reported Frame Information field
     *
     * Bits 0-6: PHY Type
     * Bit 7: Frame Type (0=Beacon/Probe Response, 1=Measurement Pilot)
     *
     * \param info the reported frame information
     */
    void SetReportedFrameInfo(uint8_t info);
    uint8_t GetReportedFrameInfo() const;

    /**
     * \brief Set RCPI (Received Channel Power Indicator)
     *
     * RCPI = 2 * (RSSI + 110), where RSSI is in dBm
     * Value 0 means RSSI < -110 dBm
     * Value 220 means RSSI >= 0 dBm
     * Values 221-254 are reserved
     * Value 255 means measurement not available
     *
     * \param rcpi the RCPI value (0-255)
     */
    void SetRcpi(uint8_t rcpi);
    uint8_t GetRcpi() const;

    /**
     * \brief Set RSNI (Received Signal to Noise Indicator)
     *
     * RSNI = 2 * SNR, where SNR is in dB
     * Value 0 means SNR < 0 dB
     * Value 254 means SNR >= 127 dB
     * Value 255 means measurement not available
     *
     * \param rsni the RSNI value (0-255)
     */
    void SetRsni(uint8_t rsni);
    uint8_t GetRsni() const;

    void SetBssid(Mac48Address bssid);
    Mac48Address GetBssid() const;

    void SetAntennaId(uint8_t antennaId);
    uint8_t GetAntennaId() const;

    void SetParentTsf(uint32_t parentTsf);
    uint32_t GetParentTsf() const;

  private:
    uint16_t GetInformationFieldSize() const override;
    void SerializeInformationField(Buffer::Iterator start) const override;
    uint16_t DeserializeInformationField(Buffer::Iterator start, uint16_t length) override;

    // Measurement header
    uint8_t m_measurementToken{0};
    uint8_t m_measurementReportMode{0};
    static constexpr uint8_t MEASUREMENT_TYPE_BEACON = 5;

    // Beacon report fields
    uint8_t m_operatingClass{0};
    uint8_t m_channelNumber{0};
    uint64_t m_actualMeasurementStartTime{0};
    uint16_t m_measurementDuration{0};
    uint8_t m_reportedFrameInfo{0};
    uint8_t m_rcpi{255};  // 255 = not available
    uint8_t m_rsni{255};  // 255 = not available
    Mac48Address m_bssid;
    uint8_t m_antennaId{0};
    uint32_t m_parentTsf{0};
};

} // namespace ns3

#endif /* BEACON_REPORT_ELEMENT_H */
