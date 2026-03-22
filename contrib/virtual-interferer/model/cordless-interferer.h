/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Cordless Phone Interferer - Simulates DECT/cordless phone interference
 */

#ifndef CORDLESS_INTERFERER_H
#define CORDLESS_INTERFERER_H

#include "virtual-interferer.h"

namespace ns3
{

/**
 * \brief Cordless phone interferer
 *
 * Simulates interference from 2.4 GHz cordless phones.
 * Characteristics:
 * - Frequency hopping: 79 channels in 2.402-2.480 GHz (Bluetooth-like)
 * - Bandwidth: 1 MHz per channel
 * - Hop interval: Configurable (typically 10 ms)
 * - Random hopping pattern using std::rand()
 *
 * Realistic parameters:
 * - TX Power: 4-10 dBm typical
 * - Duty cycle: 40-60% depending on voice activity
 * - Medium utilization (15-30%) due to frequency hopping
 * - Moderate packet loss (5-15%)
 *
 * NOTE: Aligned with SpectrogramGenerationHelper implementation
 */
class CordlessInterferer : public VirtualInterferer
{
public:
    /**
     * \brief Get the type ID
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    CordlessInterferer();
    virtual ~CordlessInterferer();

    // ==================== CORDLESS-SPECIFIC CONFIGURATION ====================

    /**
     * \brief Set the number of frequency hops
     * \param numHops Number of hops in the hopping sequence
     */
    void SetNumHops(uint32_t numHops);

    /**
     * \brief Get the number of frequency hops
     * \return Number of hops
     */
    uint32_t GetNumHops() const;

    /**
     * \brief Set the hop interval (time between hops)
     * \param hopInterval Time between hops in seconds (default 0.01 = 10ms for DECT)
     */
    void SetHopInterval(double hopInterval);

    /**
     * \brief Get the hop interval
     * \return Hop interval in seconds
     */
    double GetHopInterval() const;

    /**
     * \brief Set the channel bandwidth
     * \param bandwidthMhz Bandwidth in MHz (default 1.0 MHz)
     */
    void SetBandwidthMhz(double bandwidthMhz);

    /**
     * \brief Get the channel bandwidth
     * \return Bandwidth in MHz
     */
    double GetBandwidthMhz() const;

    // ==================== OVERRIDES ====================

    std::string GetInterfererType() const override;
    std::set<uint8_t> GetAffectedChannels() const override;
    double GetCenterFrequencyMhz() const override;
    InterferenceEffect CalculateEffect(const Vector& receiverPos,
                                       uint8_t receiverChannel,
                                       double distanceM,
                                       double rxPowerDbm) const override;

protected:
    void DoInitialize() override;
    void DoDispose() override;

private:
    /**
     * \brief Perform a frequency hop
     */
    void PerformHop();

    /**
     * \brief Get next hop channel using std::rand()
     * \return Channel number (0-78)
     */
    uint8_t GetNextHopChannel();

    uint32_t m_numHops;                 //!< Number of frequency hops
    double m_hopInterval;               //!< Time between hops (seconds)
    double m_bandwidthMhz;              //!< Channel bandwidth (MHz)
    uint8_t m_currentHopChannel;        //!< Current hop channel (0-78)
    EventId m_hopEvent;                 //!< Hop event
    Time m_lastHopTime;                 //!< Last hop time
};

} // namespace ns3

#endif // CORDLESS_INTERFERER_H
