/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "radar-interferer.h"

#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/enum.h"

#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RadarInterferer");
NS_OBJECT_ENSURE_REGISTERED(RadarInterferer);

TypeId
RadarInterferer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RadarInterferer")
        .SetParent<VirtualInterferer>()
        .SetGroupName("VirtualInterferer")
        .AddConstructor<RadarInterferer>()
        .AddAttribute("DfsChannel",
                      "DFS channel for radar operation (52-144)",
                      UintegerValue(52),
                      MakeUintegerAccessor(&RadarInterferer::m_dfsChannel),
                      MakeUintegerChecker<uint8_t>(52, 144))
        .AddAttribute("RadarType",
                      "Type of radar (WEATHER, MILITARY, AVIATION)",
                      EnumValue(RadarInterferer::WEATHER),
                      MakeEnumAccessor<RadarType>(&RadarInterferer::m_radarType),
                      MakeEnumChecker(RadarInterferer::WEATHER, "WEATHER",
                                      RadarInterferer::MILITARY, "MILITARY",
                                      RadarInterferer::AVIATION, "AVIATION"));
    return tid;
}

RadarInterferer::RadarInterferer()
    : m_dfsChannel(52),
      m_radarType(WEATHER),
      m_pulseBandwidthMhz(1.0),
      m_pulseWidth(MicroSeconds(1)),
      m_pulseInterval(MilliSeconds(1)),
      m_pulseActive(false),
      m_spanLength(2),              // Default: ±2 channels (5 channels total = 100MHz span)
      m_maxSpanLength(4),           // Max: ±4 channels (9 channels total = 180MHz span)
      m_randomSpan(true),           // Randomize span on each hop
      m_hopInterval(Seconds(10)),   // Default 10s between channel hops
      m_randomHopping(true),        // Random hopping by default
      m_currentChannelIndex(0),
      m_lastHopTime(Seconds(0))
{
    NS_LOG_FUNCTION(this);

    // Radar has very high TX power but very low duty cycle
    SetTxPowerDbm(30.0);  // 1 Watt
    SetDutyCycle(0.001);  // 0.1% duty cycle typical for radar

    // Initialize with all DFS channels by default
    m_dfsChannels = GetAllDfsChannels();

    // Initialize RNG for random hopping
    m_hopRng = CreateObject<UniformRandomVariable>();
    m_hopRng->SetAttribute("Min", DoubleValue(0.0));
}

RadarInterferer::~RadarInterferer()
{
    NS_LOG_FUNCTION(this);
}

void
RadarInterferer::DoInitialize()
{
    NS_LOG_FUNCTION(this);

    // Configure radar parameters based on type
    switch (m_radarType)
    {
    case WEATHER:
        m_pulseBandwidthMhz = 1.0;
        m_pulseWidth = MicroSeconds(1);
        m_pulseInterval = MilliSeconds(1);
        SetTxPowerDbm(33.0);  // ~2W
        break;

    case MILITARY:
        m_pulseBandwidthMhz = 5.0;
        m_pulseWidth = MicroSeconds(0.5);
        m_pulseInterval = MicroSeconds(500);
        SetTxPowerDbm(40.0);  // 10W
        break;

    case AVIATION:
        m_pulseBandwidthMhz = 2.0;
        m_pulseWidth = MicroSeconds(2);
        m_pulseInterval = MilliSeconds(2);
        SetTxPowerDbm(36.0);  // 4W
        break;
    }

    VirtualInterferer::DoInitialize();
}

void
RadarInterferer::DoUpdate(Time now)
{
    NS_LOG_FUNCTION(this << now);

    // Skip all updates if radar is not active (turned off)
    if (!IsActive()) {
        m_pulseActive = false;
        return;
    }

    // Channel hopping - check if it's time to hop
    if (!m_dfsChannels.empty()) {
        DoChannelHop(now);
    }

    // Simple pulse timing - toggle pulse state based on interval
    // In real implementation, would use proper timing
    int64_t nowUs = now.GetMicroSeconds();
    int64_t intervalUs = m_pulseInterval.GetMicroSeconds();
    int64_t pulseWidthUs = m_pulseWidth.GetMicroSeconds();

    int64_t positionInInterval = nowUs % intervalUs;
    m_pulseActive = (positionInInterval < pulseWidthUs);
}

void
RadarInterferer::DoChannelHop(Time now)
{
    // First-time initialization: record time, don't hop yet
    // This prevents immediate hop when radar starts (m_lastHopTime was 0)
    if (m_lastHopTime == Seconds(0)) {
        m_lastHopTime = now;
        NS_LOG_DEBUG("[DFS-RADAR] Radar initialized on channel " << (int)m_dfsChannel
                     << " with span ±" << (int)m_spanLength
                     << " at t=" << now.GetSeconds() << "s");
        return;  // Stay on initial channel
    }

    // Check if it's time to hop
    if (now - m_lastHopTime < m_hopInterval) {
        return;  // Not time to hop yet
    }

    m_lastHopTime = now;

    if (m_dfsChannels.empty()) {
        return;
    }

    uint8_t oldChannel = m_dfsChannel;
    uint8_t oldSpan = m_spanLength;

    // Randomize span length if enabled
    if (m_randomSpan && m_maxSpanLength > 0) {
        m_hopRng->SetAttribute("Max", DoubleValue(m_maxSpanLength + 1));
        m_spanLength = static_cast<uint8_t>(m_hopRng->GetValue());
        if (m_spanLength > m_maxSpanLength) {
            m_spanLength = m_maxSpanLength;
        }
    }

    if (m_randomHopping) {
        // Random hop to any channel in the list
        m_hopRng->SetAttribute("Max", DoubleValue(m_dfsChannels.size()));
        size_t randomIndex = static_cast<size_t>(m_hopRng->GetValue());
        if (randomIndex >= m_dfsChannels.size()) {
            randomIndex = m_dfsChannels.size() - 1;
        }
        m_currentChannelIndex = randomIndex;
    } else {
        // Sequential hopping
        m_currentChannelIndex = (m_currentChannelIndex + 1) % m_dfsChannels.size();
    }

    m_dfsChannel = m_dfsChannels[m_currentChannelIndex];

    // Get all affected channels with new position
    auto affected = GetCurrentlyAffectedChannels();

    // Build affected channels string
    std::ostringstream affectedStr;
    affectedStr << "[";
    bool first = true;
    for (uint8_t ch : affected) {
        if (!first) affectedStr << ",";
        affectedStr << (int)ch;
        first = false;
    }
    affectedStr << "]";

    if (m_dfsChannel != oldChannel || m_spanLength != oldSpan) {
        NS_LOG_DEBUG("[DFS-HOP] Radar hopped: center " << (int)oldChannel << "->" << (int)m_dfsChannel
                     << ", span ±" << (int)m_spanLength << " channels"
                     << ", affected=" << affectedStr.str()
                     << " at t=" << now.GetSeconds() << "s");
    }
}

// Channel hopping setters/getters
void
RadarInterferer::SetDfsChannels(const std::vector<uint8_t>& channels)
{
    m_dfsChannels = channels;
    if (!m_dfsChannels.empty()) {
        m_dfsChannel = m_dfsChannels[0];
        m_currentChannelIndex = 0;
    }
}

std::vector<uint8_t>
RadarInterferer::GetDfsChannels() const
{
    return m_dfsChannels;
}

void
RadarInterferer::SetHopInterval(Time interval)
{
    m_hopInterval = interval;
}

Time
RadarInterferer::GetHopInterval() const
{
    return m_hopInterval;
}

void
RadarInterferer::SetRandomHopping(bool random)
{
    m_randomHopping = random;
}

bool
RadarInterferer::GetRandomHopping() const
{
    return m_randomHopping;
}

// Wideband span setters/getters
void
RadarInterferer::SetSpanLength(uint8_t length)
{
    m_spanLength = length;
}

uint8_t
RadarInterferer::GetSpanLength() const
{
    return m_spanLength;
}

void
RadarInterferer::SetMaxSpanLength(uint8_t maxLength)
{
    m_maxSpanLength = maxLength;
}

uint8_t
RadarInterferer::GetMaxSpanLength() const
{
    return m_maxSpanLength;
}

void
RadarInterferer::SetRandomSpan(bool random)
{
    m_randomSpan = random;
}

bool
RadarInterferer::GetRandomSpan() const
{
    return m_randomSpan;
}

std::set<uint8_t>
RadarInterferer::GetCurrentlyAffectedChannels() const
{
    std::set<uint8_t> affected;

    // Calculate channel range: center ± (span * 4) since 5GHz channels are 4 apart
    int lowerBound = static_cast<int>(m_dfsChannel) - static_cast<int>(m_spanLength) * 4;
    int upperBound = static_cast<int>(m_dfsChannel) + static_cast<int>(m_spanLength) * 4;

    // DFS 20MHz channels
    // U-NII-2A: 52, 56, 60, 64
    // U-NII-2C: 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144
    static const std::vector<uint8_t> dfs20MhzChannels = {
        52, 56, 60, 64,
        100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144
    };

    // DFS 40MHz channels (center channel, covers 2x20MHz)
    // Channel 54 covers 52+56, Channel 62 covers 60+64, etc.
    static const std::vector<std::pair<uint8_t, std::pair<int,int>>> dfs40MhzChannels = {
        {54, {52, 56}},   // U-NII-2A
        {62, {60, 64}},   // U-NII-2A
        {102, {100, 104}}, // U-NII-2C
        {110, {108, 112}},
        {118, {116, 120}},
        {126, {124, 128}},
        {134, {132, 136}},
        {142, {140, 144}}
    };

    // DFS 80MHz channels (center channel, covers 4x20MHz)
    static const std::vector<std::pair<uint8_t, std::pair<int,int>>> dfs80MhzChannels = {
        {58, {52, 64}},    // U-NII-2A (covers 52,56,60,64)
        {106, {100, 112}}, // U-NII-2C
        {122, {116, 128}},
        {138, {132, 144}}
    };

    // DFS 160MHz channels
    static const std::vector<std::pair<uint8_t, std::pair<int,int>>> dfs160MhzChannels = {
        {114, {100, 128}}  // U-NII-2C only (50 includes non-DFS 36-48)
    };

    // Add 20MHz channels in range
    for (uint8_t ch : dfs20MhzChannels) {
        if (static_cast<int>(ch) >= lowerBound && static_cast<int>(ch) <= upperBound) {
            affected.insert(ch);
        }
    }

    // Add 40MHz channels if ANY part overlaps with radar span
    for (const auto& ch40 : dfs40MhzChannels) {
        int chLow = ch40.second.first;
        int chHigh = ch40.second.second;
        // Check if ranges overlap: NOT (chHigh < lowerBound OR chLow > upperBound)
        if (!(chHigh < lowerBound || chLow > upperBound)) {
            affected.insert(ch40.first);
        }
    }

    // Add 80MHz channels if ANY part overlaps
    for (const auto& ch80 : dfs80MhzChannels) {
        int chLow = ch80.second.first;
        int chHigh = ch80.second.second;
        if (!(chHigh < lowerBound || chLow > upperBound)) {
            affected.insert(ch80.first);
        }
    }

    // Add 160MHz channels if ANY part overlaps
    for (const auto& ch160 : dfs160MhzChannels) {
        int chLow = ch160.second.first;
        int chHigh = ch160.second.second;
        if (!(chHigh < lowerBound || chLow > upperBound)) {
            affected.insert(ch160.first);
        }
    }

    // Always include at least the center channel if it's a DFS channel
    if (affected.empty() && IsDfsChannel(m_dfsChannel)) {
        affected.insert(m_dfsChannel);
    }

    return affected;
}

std::vector<uint8_t>
RadarInterferer::GetAllDfsChannels()
{
    return {52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144};
}

void
RadarInterferer::SetDfsChannel(uint8_t channel)
{
    NS_LOG_FUNCTION(this << (int)channel);

    if (!IsDfsChannel(channel))
    {
        NS_LOG_WARN("Channel " << (int)channel << " is not a DFS channel, using 52");
        m_dfsChannel = 52;
    }
    else
    {
        m_dfsChannel = channel;
    }
}

uint8_t
RadarInterferer::GetDfsChannel() const
{
    return m_dfsChannel;
}

void
RadarInterferer::SetRadarType(RadarType type)
{
    NS_LOG_FUNCTION(this << type);
    m_radarType = type;
}

RadarInterferer::RadarType
RadarInterferer::GetRadarType() const
{
    return m_radarType;
}

std::string
RadarInterferer::GetInterfererType() const
{
    return "Radar";
}

std::set<uint8_t>
RadarInterferer::GetAffectedChannels() const
{
    // Use wideband calculation: center ± span channels
    return GetCurrentlyAffectedChannels();
}

double
RadarInterferer::GetBandwidthMhz() const
{
    return m_pulseBandwidthMhz;
}

double
RadarInterferer::GetCenterFrequencyMhz() const
{
    // 5 GHz DFS channels
    return 5000.0 + m_dfsChannel * 5.0;
}

InterferenceEffect
RadarInterferer::CalculateEffect(
    const Vector& receiverPos,
    uint8_t receiverChannel,
    double distanceM,
    double rxPowerDbm) const
{
    InterferenceEffect effect;

    // Only affects DFS channels
    if (!IsDfsChannel(receiverChannel))
    {
        return effect;  // No effect on non-DFS channels
    }

    // Check if receiver is on an affected channel
    auto affected = GetAffectedChannels();
    if (affected.find(receiverChannel) == affected.end())
    {
        // Check for partial overlap
        double overlapFactor = GetChannelOverlapFactor(receiverChannel);
        if (overlapFactor < 0.1)
        {
            return effect;  // No significant overlap
        }
    }

    // Calculate received power
    double pathLoss = CalculatePathLoss(distanceM);
    double radarRxPower = GetTxPowerDbm() - pathLoss;

    effect.signalPowerDbm = radarRxPower;

    // Radar has very low duty cycle, so CCA impact is minimal
    // But any detection should trigger DFS
    effect.nonWifiCcaPercent = GetDutyCycle() * 100.0;

    // Packet loss during pulses
    if (radarRxPower > -70.0)  // Strong enough to cause issues
    {
        // Loss probability is duty cycle * signal strength factor
        double strengthFactor = std::min(1.0, (radarRxPower + 70.0) / 30.0);
        effect.packetLossProbability = GetDutyCycle() * strengthFactor;
    }

    // THE KEY FEATURE: Trigger DFS if radar is strong enough
    // DFS detection threshold is typically -62 to -64 dBm
    const double DFS_DETECTION_THRESHOLD_DBM = -62.0;
    if (radarRxPower > DFS_DETECTION_THRESHOLD_DBM)
    {
        effect.triggersDfs = true;
        NS_LOG_DEBUG("Radar detected! RxPower=" << radarRxPower
                     << " dBm > threshold " << DFS_DETECTION_THRESHOLD_DBM << " dBm");
    }

    return effect;
}

bool
RadarInterferer::IsDfsChannel(uint8_t channel)
{
    // U-NII-2A: 52, 56, 60, 64
    // U-NII-2C: 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144
    // (Some regions also include U-NII-2B and others)

    // U-NII-2A
    if (channel >= 52 && channel <= 64)
    {
        return true;
    }

    // U-NII-2C (extended)
    if (channel >= 100 && channel <= 144)
    {
        return true;
    }

    return false;
}

bool
RadarInterferer::ChannelOverlaps(uint8_t wifiChannel) const
{
    // Use span-based affected channels instead of narrow pulse bandwidth
    // This ensures all channels within the radar's wideband span are detected
    auto affected = GetCurrentlyAffectedChannels();
    return affected.count(wifiChannel) > 0;
}

} // namespace ns3
