/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * ZigBee Interferer - Simulates IEEE 802.15.4 interference
 */

#ifndef ZIGBEE_INTERFERER_H
#define ZIGBEE_INTERFERER_H

#include "virtual-interferer.h"

namespace ns3
{

/**
 * \brief ZigBee (IEEE 802.15.4) interferer
 *
 * Simulates interference from ZigBee/802.15.4 devices.
 * Characteristics:
 * - Channels 11-26 in 2.4 GHz band
 * - Bandwidth: 2 MHz
 * - Very low duty cycle (1-5%)
 * - Bursty traffic pattern
 */
class ZigbeeInterferer : public VirtualInterferer
{
public:
    enum NetworkType
    {
        SENSOR = 0,     ///< Sensor network: very low duty
        CONTROL = 1,    ///< Control network: low duty
        LIGHTING = 2    ///< Smart lighting: medium duty
    };

    static TypeId GetTypeId();

    ZigbeeInterferer();
    virtual ~ZigbeeInterferer();

    void SetZigbeeChannel(uint8_t channel);
    uint8_t GetZigbeeChannel() const;

    void SetNetworkType(NetworkType type);
    NetworkType GetNetworkType() const;

    // Virtuals from base class
    std::string GetInterfererType() const override;
    std::set<uint8_t> GetAffectedChannels() const override;
    double GetBandwidthMhz() const override;
    double GetCenterFrequencyMhz() const override;

    InterferenceEffect CalculateEffect(
        const Vector& receiverPos,
        uint8_t receiverChannel,
        double distanceM,
        double rxPowerDbm) const override;

protected:
    void DoInitialize() override;

private:
    uint8_t m_zigbeeChannel;  // 11-26
    NetworkType m_networkType;

    static constexpr double ZB_CHANNEL_BW_MHZ = 2.0;
    static constexpr double TX_POWER_DBM = 3.0;
    static constexpr double CCA_THRESHOLD_DBM = -62.0;

    static double GetZigbeeChannelCenterMhz(uint8_t channel);
};

} // namespace ns3

#endif /* ZIGBEE_INTERFERER_H */
