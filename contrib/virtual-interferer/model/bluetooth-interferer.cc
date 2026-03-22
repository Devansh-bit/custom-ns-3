/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "bluetooth-interferer.h"

#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/boolean.h"
#include "ns3/simulator.h"

#include <cmath>
#include <algorithm>
#include <cstdlib>  // For std::rand()

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("BluetoothInterferer");
NS_OBJECT_ENSURE_REGISTERED(BluetoothInterferer);

TypeId
BluetoothInterferer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::BluetoothInterferer")
        .SetParent<VirtualInterferer>()
        .SetGroupName("VirtualInterferer")
        .AddConstructor<BluetoothInterferer>()
        .AddAttribute("DeviceClass",
                      "Bluetooth device class (determines TX power)",
                      EnumValue(CLASS_2),
                      MakeEnumAccessor<DeviceClass>(&BluetoothInterferer::m_deviceClass),
                      MakeEnumChecker(CLASS_1, "Class1",
                                      CLASS_2, "Class2",
                                      CLASS_3, "Class3"))
        .AddAttribute("Profile",
                      "Usage profile (determines duty cycle)",
                      EnumValue(AUDIO_STREAMING),
                      MakeEnumAccessor<Profile>(&BluetoothInterferer::m_profile),
                      MakeEnumChecker(AUDIO_STREAMING, "AudioStreaming",
                                      DATA_TRANSFER, "DataTransfer",
                                      HID, "HID",
                                      IDLE, "Idle"))
        .AddAttribute("HoppingEnabled",
                      "Enable frequency hopping simulation",
                      BooleanValue(true),
                      MakeBooleanAccessor(&BluetoothInterferer::m_hoppingEnabled),
                      MakeBooleanChecker());
    return tid;
}

BluetoothInterferer::BluetoothInterferer()
    : m_deviceClass(CLASS_2),
      m_profile(AUDIO_STREAMING),
      m_hoppingEnabled(true),
      m_currentHopChannel(0),
      m_lastHopTime(Seconds(0))
{
    NS_LOG_FUNCTION(this);
}

BluetoothInterferer::~BluetoothInterferer()
{
    NS_LOG_FUNCTION(this);

    if (m_hopEvent.IsPending())
    {
        Simulator::Cancel(m_hopEvent);
    }
}

void
BluetoothInterferer::DoInitialize()
{
    NS_LOG_FUNCTION(this);

    UpdateTxPower();
    UpdateDutyCycle();

    // Initialize to random hop channel using std::rand() (aligned with SpectrogramGenerationHelper)
    m_currentHopChannel = std::rand() % BT_NUM_CHANNELS;

    VirtualInterferer::DoInitialize();

    // Start hopping if enabled
    if (m_hoppingEnabled && m_installed)
    {
        m_hopEvent = Simulator::Schedule(MicroSeconds(HOP_INTERVAL_US),
                                          &BluetoothInterferer::DoHop, this);
    }
}

void
BluetoothInterferer::DoUpdate(Time now)
{
    NS_LOG_FUNCTION(this << now);

    // Add some variation to duty cycle based on profile
    double baseVariation = m_random->GetValue(-0.05, 0.05);
    double baseDuty;

    switch (m_profile)
    {
        case AUDIO_STREAMING:
            baseDuty = DUTY_AUDIO + baseVariation;
            break;
        case DATA_TRANSFER:
            baseDuty = DUTY_DATA + baseVariation;
            break;
        case HID:
            baseDuty = DUTY_HID + baseVariation * 0.5;
            break;
        case IDLE:
        default:
            baseDuty = DUTY_IDLE + baseVariation * 0.2;
            break;
    }

    m_dutyCycle = std::clamp(baseDuty, 0.01, 0.6);

    VirtualInterferer::DoUpdate(now);
}

// ==================== BLUETOOTH-SPECIFIC CONFIGURATION ====================

void
BluetoothInterferer::SetDeviceClass(DeviceClass deviceClass)
{
    NS_LOG_FUNCTION(this << deviceClass);
    m_deviceClass = deviceClass;
    UpdateTxPower();
}

BluetoothInterferer::DeviceClass
BluetoothInterferer::GetDeviceClass() const
{
    return m_deviceClass;
}

void
BluetoothInterferer::SetProfile(Profile profile)
{
    NS_LOG_FUNCTION(this << profile);
    m_profile = profile;
    UpdateDutyCycle();
}

BluetoothInterferer::Profile
BluetoothInterferer::GetProfile() const
{
    return m_profile;
}

void
BluetoothInterferer::SetHoppingEnabled(bool enable)
{
    NS_LOG_FUNCTION(this << enable);
    m_hoppingEnabled = enable;

    if (enable && m_installed && !m_hopEvent.IsPending())
    {
        m_hopEvent = Simulator::Schedule(MicroSeconds(HOP_INTERVAL_US),
                                          &BluetoothInterferer::DoHop, this);
    }
    else if (!enable && m_hopEvent.IsPending())
    {
        Simulator::Cancel(m_hopEvent);
    }
}

bool
BluetoothInterferer::IsHoppingEnabled() const
{
    return m_hoppingEnabled;
}

uint8_t
BluetoothInterferer::GetCurrentHopChannel() const
{
    return m_currentHopChannel;
}

void
BluetoothInterferer::UpdateTxPower()
{
    switch (m_deviceClass)
    {
        case CLASS_1:
            m_txPowerDbm = TX_POWER_CLASS1_DBM;
            break;
        case CLASS_3:
            m_txPowerDbm = TX_POWER_CLASS3_DBM;
            break;
        case CLASS_2:
        default:
            m_txPowerDbm = TX_POWER_CLASS2_DBM;
            break;
    }

    NS_LOG_DEBUG("BluetoothInterferer TX power: " << m_txPowerDbm << " dBm "
                 << "(class=" << m_deviceClass << ")");
}

void
BluetoothInterferer::UpdateDutyCycle()
{
    switch (m_profile)
    {
        case AUDIO_STREAMING:
            m_dutyCycle = DUTY_AUDIO;
            break;
        case DATA_TRANSFER:
            m_dutyCycle = DUTY_DATA;
            break;
        case HID:
            m_dutyCycle = DUTY_HID;
            break;
        case IDLE:
        default:
            m_dutyCycle = DUTY_IDLE;
            break;
    }

    NS_LOG_DEBUG("BluetoothInterferer duty cycle: " << m_dutyCycle
                 << " (profile=" << m_profile << ")");
}

void
BluetoothInterferer::DoHop()
{
    NS_LOG_FUNCTION(this);

    if (!m_hoppingEnabled || !m_active)
    {
        return;
    }

    // Hop to next channel using std::rand() (aligned with SpectrogramGenerationHelper)
    m_currentHopChannel = std::rand() % BT_NUM_CHANNELS;
    m_lastHopTime = Simulator::Now();

    NS_LOG_DEBUG("BluetoothInterferer hopped to channel " << (int)m_currentHopChannel
                 << " (freq=" << (BT_START_FREQ_MHZ + m_currentHopChannel * BT_CHANNEL_STEP_MHZ)
                 << " MHz)");

    // Schedule next hop
    m_hopEvent = Simulator::Schedule(MicroSeconds(HOP_INTERVAL_US),
                                      &BluetoothInterferer::DoHop, this);
}

std::set<uint8_t>
BluetoothInterferer::GetOverlappingWifiChannels() const
{
    std::set<uint8_t> overlapping;

    // Current BT frequency
    double btFreqMhz = BT_START_FREQ_MHZ + m_currentHopChannel * BT_CHANNEL_STEP_MHZ;
    double btLow = btFreqMhz - BT_CHANNEL_BW_MHZ / 2.0;
    double btHigh = btFreqMhz + BT_CHANNEL_BW_MHZ / 2.0;

    // Check overlap with 2.4 GHz WiFi channels
    for (uint8_t wifiCh = 1; wifiCh <= 14; ++wifiCh)
    {
        double wifiCenter = GetWifiChannelCenterMhz(wifiCh);
        double wifiBw = GetWifiChannelBandwidthMhz(wifiCh);
        double wifiLow = wifiCenter - wifiBw / 2.0;
        double wifiHigh = wifiCenter + wifiBw / 2.0;

        // Check overlap
        if (btLow < wifiHigh && btHigh > wifiLow)
        {
            overlapping.insert(wifiCh);
        }
    }

    return overlapping;
}

// ==================== VIRTUALS FROM BASE CLASS ====================

std::string
BluetoothInterferer::GetInterfererType() const
{
    return "Bluetooth";
}

std::set<uint8_t>
BluetoothInterferer::GetAffectedChannels() const
{
    if (m_hoppingEnabled)
    {
        // When hopping, we affect channels probabilistically
        // Return all 2.4 GHz channels that could potentially be affected
        return {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    }
    else
    {
        // When not hopping, only current channel's overlap
        return GetOverlappingWifiChannels();
    }
}

double
BluetoothInterferer::GetBandwidthMhz() const
{
    return BT_CHANNEL_BW_MHZ;
}

double
BluetoothInterferer::GetCenterFrequencyMhz() const
{
    return BT_START_FREQ_MHZ + m_currentHopChannel * BT_CHANNEL_STEP_MHZ;
}

InterferenceEffect
BluetoothInterferer::CalculateEffect(
    const Vector& receiverPos,
    uint8_t receiverChannel,
    double distanceM,
    double rxPowerDbm) const
{
    NS_LOG_FUNCTION(this << receiverChannel << distanceM << rxPowerDbm);

    InterferenceEffect effect;

    if (!IsActive())
    {
        NS_LOG_DEBUG("BluetoothInterferer NOT ACTIVE - returning zero effect");
        return effect;
    }

    // Check if signal is above CCA threshold
    if (rxPowerDbm < CCA_THRESHOLD_DBM)
    {
        NS_LOG_DEBUG("Bluetooth RX power " << rxPowerDbm << " dBm below CCA threshold, no effect");
        return effect;
    }

    // For hopping, calculate probability of being on an overlapping channel
    double overlapProbability = 1.0;

    if (m_hoppingEnabled)
    {
        // Calculate how many BT channels overlap with this WiFi channel
        double wifiCenter = GetWifiChannelCenterMhz(receiverChannel);
        double wifiBw = GetWifiChannelBandwidthMhz(receiverChannel);
        double wifiLow = wifiCenter - wifiBw / 2.0;
        double wifiHigh = wifiCenter + wifiBw / 2.0;

        int overlappingBtChannels = 0;
        for (int btCh = 0; btCh < BT_NUM_CHANNELS; ++btCh)
        {
            double btFreq = BT_START_FREQ_MHZ + btCh * BT_CHANNEL_STEP_MHZ;
            double btLow = btFreq - BT_CHANNEL_BW_MHZ / 2.0;
            double btHigh = btFreq + BT_CHANNEL_BW_MHZ / 2.0;

            if (btLow < wifiHigh && btHigh > wifiLow)
            {
                overlappingBtChannels++;
            }
        }

        // Probability of being on an overlapping channel
        overlapProbability = static_cast<double>(overlappingBtChannels) / BT_NUM_CHANNELS;
    }
    else
    {
        // Not hopping - check direct overlap
        overlapProbability = GetChannelOverlapFactor(receiverChannel);
    }

    if (overlapProbability <= 0)
    {
        return effect;
    }

    effect.signalPowerDbm = rxPowerDbm;

    // Calculate non-WiFi CCA utilization
    double powerAboveThreshold = rxPowerDbm - CCA_THRESHOLD_DBM;
    double powerFactor = std::min(1.0, powerAboveThreshold / 20.0); // Normalize to 20 dB range

    // Base utilization scaled by duty cycle, power, and overlap probability
    double baseUtil = BASE_UTIL_MIN_PERCENT +
                      (BASE_UTIL_MAX_PERCENT - BASE_UTIL_MIN_PERCENT) * powerFactor;
    effect.nonWifiCcaPercent = baseUtil * m_dutyCycle * overlapProbability;

    NS_LOG_DEBUG("Bluetooth CalculateEffect DEBUG:"
                 << " IsActive=" << IsActive()
                 << " rxPower=" << rxPowerDbm << "dBm"
                 << " powerAboveThresh=" << powerAboveThreshold << "dB"
                 << " powerFactor=" << powerFactor
                 << " baseUtil=" << baseUtil << "%"
                 << " dutyCycle=" << m_dutyCycle
                 << " overlapProb=" << overlapProbability
                 << " PRE-RANDOM CCA=" << effect.nonWifiCcaPercent << "%");

    // Add randomness
    double variation = m_random->GetValue(0.7, 1.3);
    effect.nonWifiCcaPercent *= variation;
    effect.nonWifiCcaPercent = std::clamp(effect.nonWifiCcaPercent, 0.0, 100.0);

    // Calculate packet loss probability
    double baseLoss = PACKET_LOSS_MIN +
                      (PACKET_LOSS_MAX - PACKET_LOSS_MIN) * powerFactor;
    effect.packetLossProbability = baseLoss * m_dutyCycle * overlapProbability;

    variation = m_random->GetValue(0.7, 1.3);
    effect.packetLossProbability *= variation;
    effect.packetLossProbability = std::clamp(effect.packetLossProbability, 0.0, 1.0);

    // Bluetooth does not trigger DFS
    effect.triggersDfs = false;

    NS_LOG_DEBUG("BluetoothInterferer effect on ch " << (int)receiverChannel
                 << ": nonWifiCca=" << effect.nonWifiCcaPercent
                 << "%, packetLoss=" << effect.packetLossProbability
                 << " (overlapProb=" << overlapProbability
                 << ", distance=" << distanceM << "m)");

    return effect;
}

} // namespace ns3
