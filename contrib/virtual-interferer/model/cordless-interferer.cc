/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cordless-interferer.h"

#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"

#include <cmath>
#include <algorithm>
#include <cstdlib>  // For std::rand()

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CordlessInterferer");
NS_OBJECT_ENSURE_REGISTERED(CordlessInterferer);

TypeId
CordlessInterferer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CordlessInterferer")
        .SetParent<VirtualInterferer>()
        .SetGroupName("VirtualInterferer")
        .AddConstructor<CordlessInterferer>()
        .AddAttribute("NumHops",
                      "Number of frequency hops",
                      UintegerValue(100),
                      MakeUintegerAccessor(&CordlessInterferer::m_numHops),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("HopInterval",
                      "Time between frequency hops (seconds)",
                      DoubleValue(0.01),  // 10 ms
                      MakeDoubleAccessor(&CordlessInterferer::m_hopInterval),
                      MakeDoubleChecker<double>(0.001, 1.0))
        .AddAttribute("BandwidthMhz",
                      "Channel bandwidth in MHz",
                      DoubleValue(1.0),  // 1 MHz (aligned with spectrogram module)
                      MakeDoubleAccessor(&CordlessInterferer::m_bandwidthMhz),
                      MakeDoubleChecker<double>(0.5, 20.0));
    return tid;
}

CordlessInterferer::CordlessInterferer()
    : m_numHops(100),
      m_hopInterval(0.01),      // 10 ms hop interval
      m_bandwidthMhz(1.0),      // 1 MHz bandwidth
      m_currentHopChannel(0),
      m_lastHopTime(Seconds(0))
{
    NS_LOG_FUNCTION(this);

    // Cordless phones typically have moderate power
    SetTxPowerDbm(4.0);  // 4 dBm typical
    SetDutyCycle(0.5);   // 50% duty cycle for voice activity
}

CordlessInterferer::~CordlessInterferer()
{
    NS_LOG_FUNCTION(this);

    if (m_hopEvent.IsPending())
    {
        Simulator::Cancel(m_hopEvent);
    }
}

void
CordlessInterferer::DoInitialize()
{
    NS_LOG_FUNCTION(this);

    // Start frequency hopping if active
    if (IsActive())
    {
        m_hopEvent = Simulator::Schedule(Seconds(m_hopInterval),
                                         &CordlessInterferer::PerformHop,
                                         this);
    }

    VirtualInterferer::DoInitialize();
}

void
CordlessInterferer::DoDispose()
{
    NS_LOG_FUNCTION(this);

    if (m_hopEvent.IsPending())
    {
        Simulator::Cancel(m_hopEvent);
    }

    VirtualInterferer::DoDispose();
}

void
CordlessInterferer::SetNumHops(uint32_t numHops)
{
    m_numHops = numHops;
}

uint32_t
CordlessInterferer::GetNumHops() const
{
    return m_numHops;
}

void
CordlessInterferer::SetHopInterval(double hopInterval)
{
    m_hopInterval = hopInterval;
}

double
CordlessInterferer::GetHopInterval() const
{
    return m_hopInterval;
}

void
CordlessInterferer::SetBandwidthMhz(double bandwidthMhz)
{
    m_bandwidthMhz = bandwidthMhz;
}

double
CordlessInterferer::GetBandwidthMhz() const
{
    return m_bandwidthMhz;
}

std::string
CordlessInterferer::GetInterfererType() const
{
    return "Cordless";
}

std::set<uint8_t>
CordlessInterferer::GetAffectedChannels() const
{
    std::set<uint8_t> channels;

    // Cordless operates in 2.4 GHz ISM band (2.402-2.480 GHz)
    // Affects WiFi channels 1-14
    for (uint8_t ch = 1; ch <= 14; ch++)
    {
        channels.insert(ch);
    }

    return channels;
}

double
CordlessInterferer::GetCenterFrequencyMhz() const
{
    // Current hop channel: 2.402 + (m_currentHopChannel * 0.001) GHz
    return (2402.0 + m_currentHopChannel);  // Return in MHz
}

void
CordlessInterferer::PerformHop()
{
    if (!IsActive())
    {
        return;
    }

    m_currentHopChannel = GetNextHopChannel();
    m_lastHopTime = Simulator::Now();

    // Schedule next hop
    m_hopEvent = Simulator::Schedule(Seconds(m_hopInterval),
                                     &CordlessInterferer::PerformHop,
                                     this);
}

uint8_t
CordlessInterferer::GetNextHopChannel()
{
    // Use std::rand() for random hopping (aligned with SpectrogramGenerationHelper)
    return std::rand() % 79;
}

InterferenceEffect
CordlessInterferer::CalculateEffect(const Vector& receiverPos,
                                   uint8_t receiverChannel,
                                   double distanceM,
                                   double rxPowerDbm) const
{
    NS_LOG_FUNCTION(this << receiverChannel << distanceM << rxPowerDbm);

    InterferenceEffect effect;
    effect.nonWifiCcaPercent = 0.0;
    effect.packetLossProbability = 0.0;
    effect.signalPowerDbm = rxPowerDbm;
    effect.triggersDfs = false;

    if (!IsActive())
    {
        NS_LOG_DEBUG("CordlessInterferer NOT ACTIVE - returning zero effect");
        return effect;
    }

    // CCA threshold for WiFi 2.4 GHz
    const double CCA_THRESHOLD_DBM = -82.0;

    // Check if signal is above CCA threshold
    if (rxPowerDbm < CCA_THRESHOLD_DBM)
    {
        NS_LOG_DEBUG("Cordless RX power " << rxPowerDbm << " dBm below CCA threshold, no effect");
        return effect;
    }

    // Calculate channel overlap factor
    double overlapFactor = GetChannelOverlapFactor(receiverChannel);
    NS_LOG_DEBUG("Cordless overlapFactor = " << overlapFactor << " for channel " << (int)receiverChannel);
    if (overlapFactor <= 0)
    {
        return effect;
    }

    // Calculate non-WiFi CCA utilization
    // Cordless hops frequently (10ms hop interval with 8ms transmission)
    double dutyCycle = GetDutyCycle();
    double effectiveDutyCycle = dutyCycle * (0.008 / m_hopInterval);  // 8ms tx per 10ms hop
    double powerAboveThreshold = rxPowerDbm - CCA_THRESHOLD_DBM;
    double powerFactor = std::min(1.0, powerAboveThreshold / 30.0);

    effect.nonWifiCcaPercent = overlapFactor * effectiveDutyCycle * powerFactor * 100.0;

    NS_LOG_DEBUG("Cordless CalculateEffect DEBUG:"
                 << " IsActive=" << IsActive()
                 << " rxPower=" << rxPowerDbm << "dBm"
                 << " powerAboveThresh=" << powerAboveThreshold << "dB"
                 << " dutyCycle=" << dutyCycle
                 << " effectiveDutyCycle=" << effectiveDutyCycle
                 << " powerFactor=" << powerFactor
                 << " overlapFactor=" << overlapFactor
                 << " FINAL CCA=" << effect.nonWifiCcaPercent << "%");

    // Packet loss calculation
    // Cordless has moderate impact due to frequency hopping
    double snrDb = rxPowerDbm - CCA_THRESHOLD_DBM;
    if (snrDb > 0.0)
    {
        // Scale by overlap factor and hopping pattern
        effect.packetLossProbability = std::min(1.0, (snrDb / 30.0) * overlapFactor * 0.15);
    }

    NS_LOG_DEBUG("Cordless effect: CCA=" << effect.nonWifiCcaPercent
                 << "%, packetLoss=" << (effect.packetLossProbability * 100.0) << "%");

    return effect;
}

} // namespace ns3
