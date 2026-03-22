/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Radar Interferer - Simulates radar for DFS testing
 */

#ifndef RADAR_INTERFERER_H
#define RADAR_INTERFERER_H

#include "virtual-interferer.h"
#include "ns3/random-variable-stream.h"
#include <vector>

namespace ns3
{

/**
 * \brief Radar interferer for DFS testing
 *
 * Simulates radar signals that trigger DFS channel switching.
 * Characteristics:
 * - 5 GHz DFS bands only
 * - Very low duty cycle (<1%)
 * - Pulse-based transmission
 * - Triggers DFS channel switch
 */
class RadarInterferer : public VirtualInterferer
{
public:
    enum RadarType
    {
        WEATHER = 0,    ///< Weather radar
        MILITARY = 1,   ///< Military radar
        AVIATION = 2    ///< Aviation radar
    };

    static TypeId GetTypeId();

    RadarInterferer();
    virtual ~RadarInterferer();

    void SetDfsChannel(uint8_t channel);
    uint8_t GetDfsChannel() const;

    void SetRadarType(RadarType type);
    RadarType GetRadarType() const;

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

    // Override to use span-based affected channels instead of narrow pulse bandwidth
    bool ChannelOverlaps(uint8_t wifiChannel) const override;

    // Wideband radar configuration
    void SetSpanLength(uint8_t length);      // Number of 20MHz bins to extend in each direction
    uint8_t GetSpanLength() const;
    void SetMaxSpanLength(uint8_t maxLength); // Max random span length
    uint8_t GetMaxSpanLength() const;
    void SetRandomSpan(bool random);          // Randomize span length on each hop
    bool GetRandomSpan() const;

    // Get all channels currently affected by radar (center ± length)
    std::set<uint8_t> GetCurrentlyAffectedChannels() const;

    // Channel hopping configuration
    void SetDfsChannels(const std::vector<uint8_t>& channels);
    std::vector<uint8_t> GetDfsChannels() const;
    void SetHopInterval(Time interval);
    Time GetHopInterval() const;
    void SetRandomHopping(bool random);
    bool GetRandomHopping() const;

protected:
    void DoInitialize() override;
    void DoUpdate(Time now) override;

private:
    uint8_t m_dfsChannel;                    // Current center channel
    RadarType m_radarType;
    double m_pulseBandwidthMhz;
    Time m_pulseWidth;
    Time m_pulseInterval;
    bool m_pulseActive;

    // Wideband span configuration
    uint8_t m_spanLength;                    // Current span: ±length channels affected
    uint8_t m_maxSpanLength;                 // Maximum span length for random selection
    bool m_randomSpan;                       // Whether to randomize span on each hop

    // Channel hopping state
    std::vector<uint8_t> m_dfsChannels;     // List of center channels to hop across
    Time m_hopInterval;                      // Time between channel hops
    bool m_randomHopping;                    // Whether to hop randomly or sequentially
    size_t m_currentChannelIndex;            // Current index in channel list
    Time m_lastHopTime;                      // When we last hopped channels
    Ptr<UniformRandomVariable> m_hopRng;     // RNG for random hopping

    void DoChannelHop(Time now);
    static bool IsDfsChannel(uint8_t channel);
    static std::vector<uint8_t> GetAllDfsChannels();
};

} // namespace ns3

#endif /* RADAR_INTERFERER_H */
