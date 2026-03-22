/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "virtual-interferer.h"
#include "virtual-interferer-environment.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/double.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"

#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("VirtualInterferer");
NS_OBJECT_ENSURE_REGISTERED(VirtualInterferer);

// Static member initialization
uint32_t VirtualInterferer::s_nextId = 1;

// ==================== InterferenceEffect ====================

InterferenceEffect&
InterferenceEffect::operator+=(const InterferenceEffect& other)
{
    // Aggregate non-WiFi CCA (capped at 100%)
    nonWifiCcaPercent = std::min(100.0, nonWifiCcaPercent + other.nonWifiCcaPercent);

    // Aggregate packet loss (probabilistic combination)
    // P(loss) = 1 - (1-p1)(1-p2)
    double keepProb1 = 1.0 - packetLossProbability;
    double keepProb2 = 1.0 - other.packetLossProbability;
    packetLossProbability = 1.0 - (keepProb1 * keepProb2);

    // Use max signal power (closest interferer dominates)
    signalPowerDbm = std::max(signalPowerDbm, other.signalPowerDbm);

    // OR the DFS trigger
    triggersDfs = triggersDfs || other.triggersDfs;

    return *this;
}

// ==================== VirtualInterferer ====================

TypeId
VirtualInterferer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::VirtualInterferer")
        .SetParent<Object>()
        .SetGroupName("VirtualInterferer")
        .AddAttribute("TxPowerDbm",
                      "Transmit power in dBm",
                      DoubleValue(0.0),
                      MakeDoubleAccessor(&VirtualInterferer::m_txPowerDbm),
                      MakeDoubleChecker<double>(-50.0, 50.0))
        .AddAttribute("DutyCycle",
                      "Duty cycle (0-1)",
                      DoubleValue(0.5),
                      MakeDoubleAccessor(&VirtualInterferer::m_dutyCycle),
                      MakeDoubleChecker<double>(0.0, 1.0))
        .AddAttribute("DutyPeriod",
                      "Duty cycle period in seconds (e.g., 0.01667 for 60Hz microwave)",
                      DoubleValue(0.01667),
                      MakeDoubleAccessor(&VirtualInterferer::m_dutyPeriod),
                      MakeDoubleChecker<double>(0.001, 1.0))
        .AddAttribute("DutyRatio",
                      "Duty cycle ratio (0-1, fraction of period that device is ON)",
                      DoubleValue(0.5),
                      MakeDoubleAccessor(&VirtualInterferer::m_dutyRatio),
                      MakeDoubleChecker<double>(0.0, 1.0))
        .AddAttribute("Active",
                      "Whether interferer is currently active",
                      BooleanValue(true),
                      MakeBooleanAccessor(&VirtualInterferer::m_active),
                      MakeBooleanChecker())
        .AddAttribute("PathLossExponent",
                      "Path loss exponent for distance-based attenuation",
                      DoubleValue(3.0),
                      MakeDoubleAccessor(&VirtualInterferer::m_pathLossExponent),
                      MakeDoubleChecker<double>(1.0, 6.0))
        .AddAttribute("ReferenceDistance",
                      "Reference distance for path loss calculation (meters)",
                      DoubleValue(1.0),
                      MakeDoubleAccessor(&VirtualInterferer::m_referenceDistanceM),
                      MakeDoubleChecker<double>(0.1, 100.0))
        .AddAttribute("ReferenceLoss",
                      "Path loss at reference distance (dB)",
                      DoubleValue(40.0),
                      MakeDoubleAccessor(&VirtualInterferer::m_referenceLossDb),
                      MakeDoubleChecker<double>(0.0, 100.0));
    return tid;
}

VirtualInterferer::VirtualInterferer()
    : m_id(s_nextId++),
      m_position(Vector(0, 0, 0)),
      m_mobility(nullptr),
      m_txPowerDbm(0.0),
      m_dutyCycle(0.5),
      m_dutyPeriod(0.01667),  // Default: 60Hz period (16.67ms)
      m_dutyRatio(0.5),       // Default: 50% ON time
      m_active(true),
      m_installed(false),
      m_pathLossExponent(3.0),
      m_referenceDistanceM(1.0),
      m_referenceLossDb(40.0),
      m_hasSchedule(false),
      m_onDuration(Seconds(0)),
      m_offDuration(Seconds(0))
{
    NS_LOG_FUNCTION(this);
    m_random = CreateObject<UniformRandomVariable>();
}

VirtualInterferer::~VirtualInterferer()
{
    NS_LOG_FUNCTION(this);
}

void
VirtualInterferer::DoInitialize()
{
    NS_LOG_FUNCTION(this);
    Object::DoInitialize();
}

void
VirtualInterferer::DoDispose()
{
    NS_LOG_FUNCTION(this);

    // Just mark as uninstalled without calling back to environment
    // to avoid infinite recursion during cleanup
    m_installed = false;

    if (m_scheduleEvent.IsPending())
    {
        Simulator::Cancel(m_scheduleEvent);
    }

    m_mobility = nullptr;
    m_random = nullptr;

    Object::DoDispose();
}

// ==================== INSTALLATION ====================

void
VirtualInterferer::Install()
{
    NS_LOG_FUNCTION(this);

    if (m_installed)
    {
        NS_LOG_WARN("VirtualInterferer " << m_id << " already installed");
        return;
    }

    auto env = VirtualInterfererEnvironment::Get();
    env->RegisterInterferer(this);
    m_installed = true;

    NS_LOG_INFO("VirtualInterferer " << m_id << " (" << GetInterfererType()
                << ") installed at " << m_position);
}

void
VirtualInterferer::Uninstall()
{
    NS_LOG_FUNCTION(this);

    if (!m_installed)
    {
        return;
    }

    // Don't try to access the environment if it's being destroyed
    if (!VirtualInterfererEnvironment::IsBeingDestroyed())
    {
        auto env = VirtualInterfererEnvironment::Get();
        env->UnregisterInterferer(this);
    }
    m_installed = false;

    NS_LOG_INFO("VirtualInterferer " << m_id << " uninstalled");
}

bool
VirtualInterferer::IsInstalled() const
{
    return m_installed;
}

// ==================== POSITION ====================

void
VirtualInterferer::SetPosition(const Vector& position)
{
    NS_LOG_FUNCTION(this << position);
    m_position = position;
}

Vector
VirtualInterferer::GetPosition() const
{
    if (m_mobility)
    {
        return m_mobility->GetPosition();
    }
    return m_position;
}

void
VirtualInterferer::SetMobilityModel(Ptr<MobilityModel> mobility)
{
    NS_LOG_FUNCTION(this << mobility);
    m_mobility = mobility;
}

Ptr<MobilityModel>
VirtualInterferer::GetMobilityModel() const
{
    return m_mobility;
}

// ==================== STATE CONTROL ====================

void
VirtualInterferer::TurnOn()
{
    NS_LOG_FUNCTION(this);
    m_active = true;
    NS_LOG_DEBUG("VirtualInterferer " << m_id << " turned ON");
}

void
VirtualInterferer::TurnOff()
{
    NS_LOG_FUNCTION(this);
    m_active = false;
    NS_LOG_DEBUG("VirtualInterferer " << m_id << " turned OFF");
}

bool
VirtualInterferer::IsActive() const
{
    return m_active;
}

void
VirtualInterferer::SetSchedule(Time onDuration, Time offDuration)
{
    NS_LOG_FUNCTION(this << onDuration << offDuration);

    m_hasSchedule = true;
    m_onDuration = onDuration;
    m_offDuration = offDuration;

    // Start schedule
    m_active = true;
    m_scheduleEvent = Simulator::Schedule(onDuration, &VirtualInterferer::DoScheduleToggle, this);

    NS_LOG_DEBUG("[VI Schedule] " << GetInterfererType() << " " << m_id
                 << " schedule set: " << onDuration.GetSeconds() << "s ON / "
                 << offDuration.GetSeconds() << "s OFF (starting ON, first toggle at t="
                 << (Simulator::Now() + onDuration).GetSeconds() << "s)");
}

void
VirtualInterferer::ClearSchedule()
{
    NS_LOG_FUNCTION(this);

    m_hasSchedule = false;
    if (m_scheduleEvent.IsPending())
    {
        Simulator::Cancel(m_scheduleEvent);
    }
}

void
VirtualInterferer::Update(Time now)
{
    NS_LOG_FUNCTION(this << now);
    DoUpdate(now);
}

void
VirtualInterferer::DoScheduleToggle()
{
    NS_LOG_FUNCTION(this);

    if (!m_hasSchedule)
    {
        return;
    }

    if (m_active)
    {
        m_active = false;
        m_scheduleEvent = Simulator::Schedule(m_offDuration, &VirtualInterferer::DoScheduleToggle, this);
        NS_LOG_DEBUG("[VI Schedule] " << GetInterfererType() << " " << m_id
                     << " toggled OFF at t=" << std::fixed << std::setprecision(3)
                     << Simulator::Now().GetSeconds()
                     << "s (next ON in " << m_offDuration.GetSeconds() << "s)");
    }
    else
    {
        m_active = true;
        m_scheduleEvent = Simulator::Schedule(m_onDuration, &VirtualInterferer::DoScheduleToggle, this);
        NS_LOG_DEBUG("[VI Schedule] " << GetInterfererType() << " " << m_id
                     << " toggled ON at t=" << std::fixed << std::setprecision(3)
                     << Simulator::Now().GetSeconds()
                     << "s (next OFF in " << m_onDuration.GetSeconds() << "s)");
    }
}

// ==================== PARAMETER ACCESS ====================

double
VirtualInterferer::GetTxPowerDbm() const
{
    return m_txPowerDbm;
}

void
VirtualInterferer::SetTxPowerDbm(double txPowerDbm)
{
    m_txPowerDbm = txPowerDbm;
}

double
VirtualInterferer::GetDutyCycle() const
{
    return m_dutyCycle;
}

void
VirtualInterferer::SetDutyCycle(double dutyCycle)
{
    m_dutyCycle = std::clamp(dutyCycle, 0.0, 1.0);
}

double
VirtualInterferer::GetDutyPeriod() const
{
    return m_dutyPeriod;
}

void
VirtualInterferer::SetDutyPeriod(double dutyPeriod)
{
    NS_LOG_FUNCTION(this << dutyPeriod);
    m_dutyPeriod = std::max(0.001, dutyPeriod);  // Minimum 1ms period
}

double
VirtualInterferer::GetDutyRatio() const
{
    return m_dutyRatio;
}

void
VirtualInterferer::SetDutyRatio(double dutyRatio)
{
    NS_LOG_FUNCTION(this << dutyRatio);
    m_dutyRatio = std::clamp(dutyRatio, 0.0, 1.0);
}

uint32_t
VirtualInterferer::GetId() const
{
    return m_id;
}

// ==================== HELPER METHODS ====================

void
VirtualInterferer::DoUpdate(Time now)
{
    NS_LOG_FUNCTION(this << now);
    // Default implementation does nothing
    // Subclasses can override for time-varying behavior
}

double
VirtualInterferer::CalculatePathLoss(double distanceM) const
{
    if (distanceM <= 0)
    {
        distanceM = 0.01; // Minimum 1cm
    }

    if (distanceM <= m_referenceDistanceM)
    {
        return m_referenceLossDb;
    }

    // Log-distance path loss model
    // PL(d) = PL(d0) + 10 * n * log10(d/d0)
    double pathLoss = m_referenceLossDb +
                      10.0 * m_pathLossExponent * std::log10(distanceM / m_referenceDistanceM);

    return pathLoss;
}

double
VirtualInterferer::CalculateRxPower(double distanceM) const
{
    double pathLoss = CalculatePathLoss(distanceM);
    return m_txPowerDbm - pathLoss;
}

bool
VirtualInterferer::ChannelOverlaps(uint8_t wifiChannel) const
{
    return GetChannelOverlapFactor(wifiChannel) > 0.0;
}

double
VirtualInterferer::GetChannelOverlapFactor(uint8_t wifiChannel) const
{
    double wifiCenterMhz = GetWifiChannelCenterMhz(wifiChannel);
    double wifiBwMhz = GetWifiChannelBandwidthMhz(wifiChannel);

    double intfCenterMhz = GetCenterFrequencyMhz();
    double intfBwMhz = GetBandwidthMhz();

    // Calculate frequency ranges
    double wifiLow = wifiCenterMhz - wifiBwMhz / 2.0;
    double wifiHigh = wifiCenterMhz + wifiBwMhz / 2.0;
    double intfLow = intfCenterMhz - intfBwMhz / 2.0;
    double intfHigh = intfCenterMhz + intfBwMhz / 2.0;

    // Calculate overlap
    double overlapLow = std::max(wifiLow, intfLow);
    double overlapHigh = std::min(wifiHigh, intfHigh);

    if (overlapHigh <= overlapLow)
    {
        return 0.0; // No overlap
    }

    double overlapBw = overlapHigh - overlapLow;
    double overlapFactor = overlapBw / wifiBwMhz;

    return std::clamp(overlapFactor, 0.0, 1.0);
}

double
VirtualInterferer::GetWifiChannelCenterMhz(uint8_t channel)
{
    // 2.4 GHz band (channels 1-14)
    if (channel >= 1 && channel <= 14)
    {
        if (channel == 14)
        {
            return 2484.0;
        }
        return 2407.0 + channel * 5.0;
    }

    // 5 GHz band (channels 36-177)
    if (channel >= 36 && channel <= 177)
    {
        return 5000.0 + channel * 5.0;
    }

    // Unknown channel
    NS_LOG_WARN("Unknown WiFi channel: " << (int)channel);
    return 0.0;
}

double
VirtualInterferer::GetWifiChannelBandwidthMhz(uint8_t channel)
{
    // Default to 20 MHz
    // Could be extended to support 40/80/160 MHz
    (void)channel;
    return 20.0;
}

} // namespace ns3
