/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "zigbee-interferer.h"

#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("ZigbeeInterferer");
NS_OBJECT_ENSURE_REGISTERED(ZigbeeInterferer);

TypeId
ZigbeeInterferer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ZigbeeInterferer")
        .SetParent<VirtualInterferer>()
        .SetGroupName("VirtualInterferer")
        .AddConstructor<ZigbeeInterferer>()
        .AddAttribute("ZigbeeChannel",
                      "ZigBee channel (11-26)",
                      UintegerValue(15),
                      MakeUintegerAccessor(&ZigbeeInterferer::m_zigbeeChannel),
                      MakeUintegerChecker<uint8_t>(11, 26))
        .AddAttribute("NetworkType",
                      "ZigBee network type",
                      EnumValue(SENSOR),
                      MakeEnumAccessor<NetworkType>(&ZigbeeInterferer::m_networkType),
                      MakeEnumChecker(SENSOR, "Sensor",
                                      CONTROL, "Control",
                                      LIGHTING, "Lighting"));
    return tid;
}

ZigbeeInterferer::ZigbeeInterferer()
    : m_zigbeeChannel(15),
      m_networkType(SENSOR)
{
    NS_LOG_FUNCTION(this);
    m_txPowerDbm = TX_POWER_DBM;
    m_dutyCycle = 0.02; // 2% default for sensor
}

ZigbeeInterferer::~ZigbeeInterferer()
{
    NS_LOG_FUNCTION(this);
}

void
ZigbeeInterferer::DoInitialize()
{
    NS_LOG_FUNCTION(this);

    // Set duty cycle based on network type
    switch (m_networkType)
    {
        case SENSOR:
            m_dutyCycle = 0.02;
            break;
        case CONTROL:
            m_dutyCycle = 0.03;
            break;
        case LIGHTING:
            m_dutyCycle = 0.05;
            break;
    }

    VirtualInterferer::DoInitialize();
}

void
ZigbeeInterferer::SetZigbeeChannel(uint8_t channel)
{
    m_zigbeeChannel = std::clamp(channel, uint8_t(11), uint8_t(26));
}

uint8_t
ZigbeeInterferer::GetZigbeeChannel() const
{
    return m_zigbeeChannel;
}

void
ZigbeeInterferer::SetNetworkType(NetworkType type)
{
    m_networkType = type;
}

ZigbeeInterferer::NetworkType
ZigbeeInterferer::GetNetworkType() const
{
    return m_networkType;
}

double
ZigbeeInterferer::GetZigbeeChannelCenterMhz(uint8_t channel)
{
    // ZigBee 2.4 GHz channels: 2405 + 5*(channel-11) MHz
    return 2405.0 + 5.0 * (channel - 11);
}

std::string
ZigbeeInterferer::GetInterfererType() const
{
    return "ZigBee";
}

std::set<uint8_t>
ZigbeeInterferer::GetAffectedChannels() const
{
    std::set<uint8_t> affected;

    double zbCenter = GetZigbeeChannelCenterMhz(m_zigbeeChannel);
    double zbLow = zbCenter - ZB_CHANNEL_BW_MHZ / 2.0;
    double zbHigh = zbCenter + ZB_CHANNEL_BW_MHZ / 2.0;

    for (uint8_t wifiCh = 1; wifiCh <= 14; ++wifiCh)
    {
        double wifiCenter = GetWifiChannelCenterMhz(wifiCh);
        double wifiBw = GetWifiChannelBandwidthMhz(wifiCh);
        double wifiLow = wifiCenter - wifiBw / 2.0;
        double wifiHigh = wifiCenter + wifiBw / 2.0;

        if (zbLow < wifiHigh && zbHigh > wifiLow)
        {
            affected.insert(wifiCh);
        }
    }

    return affected;
}

double
ZigbeeInterferer::GetBandwidthMhz() const
{
    return ZB_CHANNEL_BW_MHZ;
}

double
ZigbeeInterferer::GetCenterFrequencyMhz() const
{
    return GetZigbeeChannelCenterMhz(m_zigbeeChannel);
}

InterferenceEffect
ZigbeeInterferer::CalculateEffect(
    const Vector& receiverPos,
    uint8_t receiverChannel,
    double distanceM,
    double rxPowerDbm) const
{
    InterferenceEffect effect;

    if (rxPowerDbm < CCA_THRESHOLD_DBM)
    {
        return effect;
    }

    double overlapFactor = GetChannelOverlapFactor(receiverChannel);
    if (overlapFactor <= 0)
    {
        return effect;
    }

    effect.signalPowerDbm = rxPowerDbm;

    // ZigBee has very low impact due to low duty cycle
    double powerFactor = std::min(1.0, (rxPowerDbm - CCA_THRESHOLD_DBM) / 20.0);

    // Very low utilization (1-5%)
    effect.nonWifiCcaPercent = (1.0 + 4.0 * powerFactor) * m_dutyCycle * overlapFactor;
    effect.nonWifiCcaPercent *= m_random->GetValue(0.8, 1.2);
    effect.nonWifiCcaPercent = std::clamp(effect.nonWifiCcaPercent, 0.0, 10.0);

    // Very low packet loss
    effect.packetLossProbability = (0.005 + 0.015 * powerFactor) * m_dutyCycle * overlapFactor;
    effect.packetLossProbability = std::clamp(effect.packetLossProbability, 0.0, 0.05);

    effect.triggersDfs = false;

    return effect;
}

} // namespace ns3
