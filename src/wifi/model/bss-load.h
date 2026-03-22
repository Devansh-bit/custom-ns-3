/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * BSS Load Information Element (IEEE 802.11e-2005, IEEE 802.11k-2008)
 * Element ID: 11
 */

#ifndef BSS_LOAD_H
#define BSS_LOAD_H

#include "ns3/wifi-information-element.h"

namespace ns3
{

/**
 * @brief The BSS Load Information Element
 * @ingroup wifi
 *
 * This class knows how to serialize and deserialize
 * the BSS Load Information Element (IEEE 802.11e/k)
 *
 * Element ID: 11
 * Length: 5 bytes
 *
 * Fields:
 * - Station Count (2 bytes): Number of STAs currently associated with the BSS
 * - Channel Utilization (1 byte): Percentage of time AP sensed medium busy (0-255)
 * - Available Admission Capacity (2 bytes): Remaining amount of medium time (optional)
 */
class BssLoad : public WifiInformationElement
{
  public:
    BssLoad();

    // Implementations of pure virtual methods of WifiInformationElement
    WifiInformationElementId ElementId() const override;
    void Print(std::ostream& os) const override;

    /**
     * Set the Station Count field.
     * Indicates the number of STAs currently associated with the BSS.
     *
     * @param count the number of associated stations
     */
    void SetStationCount(uint16_t count);

    /**
     * Set the Channel Utilization field.
     * Indicates the percentage of time the AP sensed the medium busy.
     * Value is scaled: 0 = 0% busy, 255 = 100% busy
     *
     * @param utilization channel utilization (0-255)
     */
    void SetChannelUtilization(uint8_t utilization);

    /**
     * Set the Available Admission Capacity field.
     * Indicates the remaining amount of medium time available via explicit admission control.
     *
     * @param capacity available admission capacity
     */
    void SetAvailableAdmissionCapacity(uint16_t capacity);

    /**
     * Return the Station Count field.
     *
     * @return the number of associated stations
     */
    uint16_t GetStationCount() const;

    /**
     * Return the Channel Utilization field.
     *
     * @return channel utilization (0-255)
     */
    uint8_t GetChannelUtilization() const;

    /**
     * Return the Available Admission Capacity field.
     *
     * @return available admission capacity
     */
    uint16_t GetAvailableAdmissionCapacity() const;

    /**
     * Set the WiFi channel utilization (packed into high byte of AAC field).
     * This represents channel utilization caused by WiFi signals (preamble detected).
     *
     * @param utilization WiFi channel utilization (0-255)
     */
    void SetWifiUtilization(uint8_t utilization);

    /**
     * Set the non-WiFi channel utilization (packed into low byte of AAC field).
     * This represents channel utilization caused by non-WiFi interference.
     *
     * @param utilization non-WiFi channel utilization (0-255)
     */
    void SetNonWifiUtilization(uint8_t utilization);

    /**
     * Return the WiFi channel utilization (from high byte of AAC field).
     *
     * @return WiFi channel utilization (0-255)
     */
    uint8_t GetWifiUtilization() const;

    /**
     * Return the non-WiFi channel utilization (from low byte of AAC field).
     *
     * @return non-WiFi channel utilization (0-255)
     */
    uint8_t GetNonWifiUtilization() const;

  private:
    uint16_t GetInformationFieldSize() const override;
    void SerializeInformationField(Buffer::Iterator start) const override;
    uint16_t DeserializeInformationField(Buffer::Iterator start, uint16_t length) override;

    uint16_t m_stationCount;                ///< Station Count (2 bytes)
    uint8_t m_channelUtilization;           ///< Channel Utilization (1 byte, 0-255)
    uint16_t m_availableAdmissionCapacity;  ///< Available Admission Capacity (2 bytes)
};

} // namespace ns3

#endif /* BSS_LOAD_H */
