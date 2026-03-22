/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "microwave-interferer.h"

#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/double.h"
#include "ns3/simulator.h"

#include <cmath>
#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MicrowaveInterferer");
NS_OBJECT_ENSURE_REGISTERED(MicrowaveInterferer);

TypeId
MicrowaveInterferer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::MicrowaveInterferer")
        .SetParent<VirtualInterferer>()
        .SetGroupName("VirtualInterferer")
        .AddConstructor<MicrowaveInterferer>()
        .AddAttribute("PowerLevel",
                      "Microwave power level preset",
                      EnumValue(MEDIUM),
                      MakeEnumAccessor<PowerLevel>(&MicrowaveInterferer::m_powerLevel),
                      MakeEnumChecker(LOW, "Low",
                                      MEDIUM, "Medium",
                                      HIGH, "High"))
        .AddAttribute("AcFrequency",
                      "AC power frequency in Hz (50 or 60)",
                      DoubleValue(60.0),
                      MakeDoubleAccessor(&MicrowaveInterferer::m_acFrequencyHz),
                      MakeDoubleChecker<double>(50.0, 60.0))
        .AddAttribute("DoorLeakage",
                      "Door leakage factor (0-1, higher = more leakage)",
                      DoubleValue(0.5),
                      MakeDoubleAccessor(&MicrowaveInterferer::m_doorLeakage),
                      MakeDoubleChecker<double>(0.0, 1.0))
        .AddAttribute("Bandwidth",
                      "Signal bandwidth in MHz",
                      DoubleValue(DEFAULT_BANDWIDTH_MHZ),
                      MakeDoubleAccessor(&MicrowaveInterferer::m_bandwidthMhz),
                      MakeDoubleChecker<double>(20.0, 150.0));
    return tid;
}

MicrowaveInterferer::MicrowaveInterferer()
    : m_powerLevel(MEDIUM),
      m_acFrequencyHz(60.0),
      m_doorLeakage(0.5),
      m_bandwidthMhz(DEFAULT_BANDWIDTH_MHZ)
{
    NS_LOG_FUNCTION(this);

    // Set default duty cycle based on AC cycle (~50%)
    m_dutyCycle = 0.5;
}

MicrowaveInterferer::~MicrowaveInterferer()
{
    NS_LOG_FUNCTION(this);
}

void
MicrowaveInterferer::DoInitialize()
{
    NS_LOG_FUNCTION(this);

    // Update TX power based on power level and leakage
    UpdateTxPower();

    VirtualInterferer::DoInitialize();
}

void
MicrowaveInterferer::DoUpdate(Time now)
{
    NS_LOG_FUNCTION(this << now);

    // Microwave has periodic on/off based on AC cycle
    // The magnetron only operates during positive half of AC cycle
    // Add some randomness to simulate real-world variation
    double variation = m_random->GetValue(-0.05, 0.05);
    m_dutyCycle = std::clamp(0.5 + variation, 0.4, 0.6);

    VirtualInterferer::DoUpdate(now);
}

// ==================== MICROWAVE-SPECIFIC CONFIGURATION ====================

void
MicrowaveInterferer::SetPowerLevel(PowerLevel level)
{
    NS_LOG_FUNCTION(this << level);
    m_powerLevel = level;
    UpdateTxPower();
}

MicrowaveInterferer::PowerLevel
MicrowaveInterferer::GetPowerLevel() const
{
    return m_powerLevel;
}

void
MicrowaveInterferer::SetAcFrequency(double freqHz)
{
    NS_LOG_FUNCTION(this << freqHz);
    m_acFrequencyHz = std::clamp(freqHz, 50.0, 60.0);
}

double
MicrowaveInterferer::GetAcFrequency() const
{
    return m_acFrequencyHz;
}

void
MicrowaveInterferer::SetDoorLeakage(double factor)
{
    NS_LOG_FUNCTION(this << factor);
    m_doorLeakage = std::clamp(factor, 0.0, 1.0);
    UpdateTxPower();
}

double
MicrowaveInterferer::GetDoorLeakage() const
{
    return m_doorLeakage;
}

void
MicrowaveInterferer::UpdateTxPower()
{
    // Base power depends on power level
    double basePower;
    switch (m_powerLevel)
    {
        case LOW:
            basePower = TX_POWER_LOW_DBM;
            break;
        case HIGH:
            basePower = TX_POWER_HIGH_DBM;
            break;
        case MEDIUM:
        default:
            basePower = TX_POWER_MEDIUM_DBM;
            break;
    }

    // Door leakage increases radiated power
    // Leakage factor of 1.0 adds up to 10 dB
    double leakageBoost = m_doorLeakage * 10.0;

    m_txPowerDbm = basePower + leakageBoost;

    NS_LOG_DEBUG("MicrowaveInterferer TX power: " << m_txPowerDbm << " dBm "
                 << "(level=" << m_powerLevel << ", leakage=" << m_doorLeakage << ")");
}

// ==================== VIRTUALS FROM BASE CLASS ====================

std::string
MicrowaveInterferer::GetInterfererType() const
{
    return "Microwave";
}

std::set<uint8_t>
MicrowaveInterferer::GetAffectedChannels() const
{
    // Microwave affects ALL 2.4 GHz channels due to broadband noise
    return {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
}

double
MicrowaveInterferer::GetBandwidthMhz() const
{
    return m_bandwidthMhz;
}

double
MicrowaveInterferer::GetCenterFrequencyMhz() const
{
    return CENTER_FREQ_MHZ;
}

InterferenceEffect
MicrowaveInterferer::CalculateEffect(
    const Vector& receiverPos,
    uint8_t receiverChannel,
    double distanceM,
    double rxPowerDbm) const
{
    NS_LOG_FUNCTION(this << receiverChannel << distanceM << rxPowerDbm);

    InterferenceEffect effect;

    if (!IsActive())
    {
        NS_LOG_DEBUG("MicrowaveInterferer NOT ACTIVE - returning zero effect");
        return effect;
    }

    // Check if signal is above CCA threshold
    if (rxPowerDbm < CCA_THRESHOLD_DBM)
    {
        NS_LOG_DEBUG("Microwave RX power " << rxPowerDbm << " dBm below CCA threshold "
                     << CCA_THRESHOLD_DBM << " dBm, no effect");
        return effect;
    }

    // Calculate overlap factor (microwave has full overlap with 2.4 GHz)
    double overlapFactor = GetChannelOverlapFactor(receiverChannel);
    if (overlapFactor <= 0)
    {
        return effect;
    }

    effect.signalPowerDbm = rxPowerDbm;

    // Calculate non-WiFi CCA utilization
    // Higher RX power = more time above CCA threshold = higher utilization
    // Scale based on how far above CCA threshold
    double powerAboveThreshold = rxPowerDbm - CCA_THRESHOLD_DBM;
    double powerFactor = std::min(1.0, powerAboveThreshold / 30.0); // Normalize to 30 dB range

    // Base utilization scaled by duty cycle, power factor, and overlap
    double baseUtil = BASE_UTIL_MIN_PERCENT +
                      (BASE_UTIL_MAX_PERCENT - BASE_UTIL_MIN_PERCENT) * powerFactor;
    effect.nonWifiCcaPercent = baseUtil * m_dutyCycle * overlapFactor;

    NS_LOG_DEBUG("Microwave CalculateEffect DEBUG:"
                 << " IsActive=" << IsActive()
                 << " rxPower=" << rxPowerDbm << "dBm"
                 << " powerAboveThresh=" << powerAboveThreshold << "dB"
                 << " powerFactor=" << powerFactor
                 << " baseUtil=" << baseUtil << "%"
                 << " dutyCycle=" << m_dutyCycle
                 << " overlapFactor=" << overlapFactor
                 << " PRE-RANDOM CCA=" << effect.nonWifiCcaPercent << "%");

    // Add some randomness (real microwave interference is bursty)
    double variation = m_random->GetValue(0.8, 1.2);
    effect.nonWifiCcaPercent *= variation;
    effect.nonWifiCcaPercent = std::clamp(effect.nonWifiCcaPercent, 0.0, 100.0);

    // Calculate packet loss probability
    // Higher power = higher loss
    double baseLoss = PACKET_LOSS_MIN +
                      (PACKET_LOSS_MAX - PACKET_LOSS_MIN) * powerFactor;
    effect.packetLossProbability = baseLoss * m_dutyCycle * overlapFactor;

    // Add randomness
    variation = m_random->GetValue(0.8, 1.2);
    effect.packetLossProbability *= variation;
    effect.packetLossProbability = std::clamp(effect.packetLossProbability, 0.0, 1.0);

    // Microwave does not trigger DFS (it's in 2.4 GHz, not 5 GHz DFS bands)
    effect.triggersDfs = false;

    NS_LOG_DEBUG("MicrowaveInterferer effect on ch " << (int)receiverChannel
                 << ": nonWifiCca=" << effect.nonWifiCcaPercent
                 << "%, packetLoss=" << effect.packetLossProbability
                 << " (distance=" << distanceM << "m, rxPower=" << rxPowerDbm << "dBm)");

    return effect;
}

} // namespace ns3
