/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "virtual-interferer-helper.h"
#include "ns3/virtual-interferer-error-model.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-phy.h"
#include "ns3/node.h"
#include "ns3/mobility-model.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("VirtualInterfererHelper");

VirtualInterfererHelper::VirtualInterfererHelper()
{
    NS_LOG_FUNCTION(this);
    m_positionRng = CreateObject<UniformRandomVariable>();
    m_paramRng = CreateObject<UniformRandomVariable>();
}

VirtualInterfererHelper::~VirtualInterfererHelper()
{
    NS_LOG_FUNCTION(this);
}

void
VirtualInterfererHelper::SetEnvironmentConfig(const VirtualInterfererEnvironmentConfig& config)
{
    NS_LOG_FUNCTION(this);
    auto env = VirtualInterfererEnvironment::Get();
    env->SetConfig(config);
}

void
VirtualInterfererHelper::RegisterWifiDevices(NetDeviceContainer devices)
{
    NS_LOG_FUNCTION(this << devices.GetN());

    auto env = VirtualInterfererEnvironment::Get();

    for (uint32_t i = 0; i < devices.GetN(); ++i)
    {
        auto wifiDev = DynamicCast<WifiNetDevice>(devices.Get(i));
        if (!wifiDev)
        {
            continue;
        }

        Ptr<Node> node = wifiDev->GetNode();
        WifiReceiverInfo info;
        info.nodeId = node ? node->GetId() : 0;
        info.device = wifiDev;

        // Get BSSID from MAC
        Ptr<WifiMac> mac = wifiDev->GetMac();
        if (mac)
        {
            info.bssid = mac->GetBssid(0);  // linkId=0 for primary link
        }

        // Position getter using mobility model
        Ptr<MobilityModel> mobility = node ? node->GetObject<MobilityModel>() : nullptr;
        if (mobility)
        {
            info.getPosition = [mobility]() { return mobility->GetPosition(); };
        }
        else
        {
            info.getPosition = []() { return Vector(0, 0, 0); };
        }

        // Channel getter using PHY and error model installation
        Ptr<WifiPhy> phy = wifiDev->GetPhy();
        if (phy)
        {
            info.getChannel = [phy]() { return phy->GetChannelNumber(); };

            // Install error model for packet loss injection
            auto envConfig = env->GetConfig();
            if (envConfig.enablePacketLoss)
            {
                info.errorModel = CreateObject<VirtualInterfererErrorModel>();
                phy->SetPostReceptionErrorModel(info.errorModel);
                std::cout << "Installed VirtualInterfererErrorModel on node " << info.nodeId << std::endl;
            }
        }
        else
        {
            info.getChannel = []() { return uint8_t(0); };
        }

        env->RegisterWifiReceiver(info);
    }
}

void
VirtualInterfererHelper::AutoRegisterWifiDevices(NodeContainer nodes)
{
    NS_LOG_FUNCTION(this << nodes.GetN());

    auto env = VirtualInterfererEnvironment::Get();
    env->AutoRegisterWifiDevices(nodes);
}

// ==================== MICROWAVE ====================

Ptr<MicrowaveInterferer>
VirtualInterfererHelper::CreateMicrowave(
    const Vector& position,
    MicrowaveInterferer::PowerLevel powerLevel)
{
    NS_LOG_FUNCTION(this << position << powerLevel);

    auto microwave = CreateObject<MicrowaveInterferer>();
    microwave->SetPosition(position);
    microwave->SetPowerLevel(powerLevel);

    // Randomize duty cycle slightly (40-60% for realistic variation)
    double dutyCycle = m_paramRng->GetValue(0.4, 0.6);
    microwave->SetDutyCycle(dutyCycle);

    microwave->Install();
    m_interferers.push_back(microwave);

    std::cout << "Created microwave interferer at " << position
                << " with power level " << powerLevel << std::endl;

    return microwave;
}

std::vector<Ptr<MicrowaveInterferer>>
VirtualInterfererHelper::CreateMicrowavesRandom(
    uint32_t count,
    const Vector& minPos,
    const Vector& maxPos)
{
    NS_LOG_FUNCTION(this << count);

    std::vector<Ptr<MicrowaveInterferer>> created;

    for (uint32_t i = 0; i < count; ++i)
    {
        Vector pos(
            m_positionRng->GetValue(minPos.x, maxPos.x),
            m_positionRng->GetValue(minPos.y, maxPos.y),
            m_positionRng->GetValue(minPos.z, maxPos.z)
        );

        // Random power level
        int level = m_paramRng->GetInteger(0, 2);
        auto powerLevel = static_cast<MicrowaveInterferer::PowerLevel>(level);

        created.push_back(CreateMicrowave(pos, powerLevel));
    }

    return created;
}

// ==================== BLUETOOTH ====================

Ptr<BluetoothInterferer>
VirtualInterfererHelper::CreateBluetooth(
    const Vector& position,
    BluetoothInterferer::DeviceClass deviceClass,
    BluetoothInterferer::Profile profile)
{
    NS_LOG_FUNCTION(this << position << deviceClass << profile);

    auto bt = CreateObject<BluetoothInterferer>();
    bt->SetPosition(position);
    bt->SetDeviceClass(deviceClass);
    bt->SetProfile(profile);

    // Note: Hopping uses std::rand() - aligned with SpectrogramGenerationHelper

    bt->Install();
    m_interferers.push_back(bt);

    std::cout << "Created Bluetooth interferer at " << position
                << " class " << deviceClass << " profile " << profile << std::endl;

    return bt;
}

std::vector<Ptr<BluetoothInterferer>>
VirtualInterfererHelper::CreateBluetoothsRandom(
    uint32_t count,
    const Vector& minPos,
    const Vector& maxPos)
{
    NS_LOG_FUNCTION(this << count);

    std::vector<Ptr<BluetoothInterferer>> created;

    for (uint32_t i = 0; i < count; ++i)
    {
        Vector pos(
            m_positionRng->GetValue(minPos.x, maxPos.x),
            m_positionRng->GetValue(minPos.y, maxPos.y),
            m_positionRng->GetValue(minPos.z, maxPos.z)
        );

        // Random device class (mostly Class 2)
        BluetoothInterferer::DeviceClass deviceClass;
        double classRoll = m_paramRng->GetValue();
        if (classRoll < 0.1)
        {
            deviceClass = BluetoothInterferer::CLASS_1;
        }
        else if (classRoll < 0.3)
        {
            deviceClass = BluetoothInterferer::CLASS_3;
        }
        else
        {
            deviceClass = BluetoothInterferer::CLASS_2;
        }

        // Random profile
        int profileInt = m_paramRng->GetInteger(0, 3);
        auto profile = static_cast<BluetoothInterferer::Profile>(profileInt);

        created.push_back(CreateBluetooth(pos, deviceClass, profile));
    }

    return created;
}

// ==================== ZIGBEE ====================

Ptr<ZigbeeInterferer>
VirtualInterfererHelper::CreateZigbee(
    const Vector& position,
    uint8_t channel,
    ZigbeeInterferer::NetworkType networkType)
{
    NS_LOG_FUNCTION(this << position << (int)channel << networkType);

    auto zb = CreateObject<ZigbeeInterferer>();
    zb->SetPosition(position);
    zb->SetZigbeeChannel(channel);
    zb->SetNetworkType(networkType);

    zb->Install();
    m_interferers.push_back(zb);

    std::cout << "Created ZigBee interferer at " << position
                << " channel " << (int)channel << " type " << networkType << std::endl;

    return zb;
}

std::vector<Ptr<ZigbeeInterferer>>
VirtualInterfererHelper::CreateZigbeesRandom(
    uint32_t count,
    const Vector& minPos,
    const Vector& maxPos)
{
    NS_LOG_FUNCTION(this << count);

    std::vector<Ptr<ZigbeeInterferer>> created;

    for (uint32_t i = 0; i < count; ++i)
    {
        Vector pos(
            m_positionRng->GetValue(minPos.x, maxPos.x),
            m_positionRng->GetValue(minPos.y, maxPos.y),
            m_positionRng->GetValue(minPos.z, maxPos.z)
        );

        // Random channel (11-26)
        uint8_t channel = m_paramRng->GetInteger(11, 26);

        // Random network type
        int typeInt = m_paramRng->GetInteger(0, 2);
        auto networkType = static_cast<ZigbeeInterferer::NetworkType>(typeInt);

        created.push_back(CreateZigbee(pos, channel, networkType));
    }

    return created;
}

// ==================== RADAR ====================

Ptr<RadarInterferer>
VirtualInterfererHelper::CreateRadar(
    const Vector& position,
    uint8_t dfsChannel,
    RadarInterferer::RadarType radarType)
{
    NS_LOG_FUNCTION(this << position << (int)dfsChannel << radarType);

    auto radar = CreateObject<RadarInterferer>();
    radar->SetPosition(position);
    radar->SetDfsChannel(dfsChannel);
    radar->SetRadarType(radarType);

    radar->Install();
    m_interferers.push_back(radar);

    std::cout << "Created Radar interferer at " << position
                << " DFS channel " << (int)dfsChannel << " type " << radarType << std::endl;

    return radar;
}

// ==================== SCHEDULING ====================

void
VirtualInterfererHelper::SetSchedule(Ptr<VirtualInterferer> interferer, Time onDuration, Time offDuration)
{
    NS_LOG_FUNCTION(this << interferer << onDuration << offDuration);
    interferer->SetSchedule(onDuration, offDuration);
}

void
VirtualInterfererHelper::ScheduleTurnOn(Ptr<VirtualInterferer> interferer, Time startTime)
{
    NS_LOG_FUNCTION(this << interferer << startTime);
    Simulator::Schedule(startTime, &VirtualInterferer::TurnOn, interferer);
}

void
VirtualInterfererHelper::ScheduleTurnOff(Ptr<VirtualInterferer> interferer, Time stopTime)
{
    NS_LOG_FUNCTION(this << interferer << stopTime);
    Simulator::Schedule(stopTime, &VirtualInterferer::TurnOff, interferer);
}

// ==================== SCENARIOS ====================

void
VirtualInterfererHelper::CreateHomeScenario(
    const Vector& centerPos,
    double roomSize,
    uint32_t numBluetooth,
    bool includeZigbee)
{
    NS_LOG_FUNCTION(this << centerPos << roomSize << numBluetooth << includeZigbee);

    double halfSize = roomSize / 2.0;

    // Microwave in kitchen area (offset from center)
    Vector microwavePos(
        centerPos.x + halfSize * 0.6,
        centerPos.y + halfSize * 0.3,
        centerPos.z + 1.0  // On counter
    );
    CreateMicrowave(microwavePos, MicrowaveInterferer::MEDIUM);

    // Bluetooth devices spread around
    Vector minPos(
        centerPos.x - halfSize,
        centerPos.y - halfSize,
        centerPos.z
    );
    Vector maxPos(
        centerPos.x + halfSize,
        centerPos.y + halfSize,
        centerPos.z + 2.0
    );
    CreateBluetoothsRandom(numBluetooth, minPos, maxPos);

    // ZigBee smart home network
    if (includeZigbee)
    {
        // ZigBee coordinator near center
        CreateZigbee(centerPos, 15, ZigbeeInterferer::CONTROL);

        // A few sensor nodes
        for (uint32_t i = 0; i < 3; ++i)
        {
            Vector sensorPos(
                centerPos.x + m_positionRng->GetValue(-halfSize, halfSize),
                centerPos.y + m_positionRng->GetValue(-halfSize, halfSize),
                centerPos.z + m_positionRng->GetValue(0, 2.5)
            );
            CreateZigbee(sensorPos, 15, ZigbeeInterferer::SENSOR);
        }
    }

    std::cout << "Created home scenario with " << m_interferers.size() << " interferers" << std::endl;
}

void
VirtualInterfererHelper::CreateOfficeScenario(
    const Vector& centerPos,
    double officeSize,
    uint32_t numBluetooth)
{
    NS_LOG_FUNCTION(this << centerPos << officeSize << numBluetooth);

    double halfSize = officeSize / 2.0;

    Vector minPos(
        centerPos.x - halfSize,
        centerPos.y - halfSize,
        centerPos.z
    );
    Vector maxPos(
        centerPos.x + halfSize,
        centerPos.y + halfSize,
        centerPos.z + 3.0
    );

    // Many Bluetooth devices (keyboards, mice, headsets)
    for (uint32_t i = 0; i < numBluetooth; ++i)
    {
        Vector pos(
            m_positionRng->GetValue(minPos.x, maxPos.x),
            m_positionRng->GetValue(minPos.y, maxPos.y),
            m_positionRng->GetValue(minPos.z + 0.7, minPos.z + 1.5)  // Desk height
        );

        // Mostly HID devices in office
        BluetoothInterferer::Profile profile;
        double roll = m_paramRng->GetValue();
        if (roll < 0.5)
        {
            profile = BluetoothInterferer::HID;
        }
        else if (roll < 0.8)
        {
            profile = BluetoothInterferer::AUDIO_STREAMING;
        }
        else
        {
            profile = BluetoothInterferer::DATA_TRANSFER;
        }

        CreateBluetooth(pos, BluetoothInterferer::CLASS_2, profile);
    }

    std::cout << "Created office scenario with " << m_interferers.size() << " interferers" << std::endl;
}

void
VirtualInterfererHelper::CreateDfsTestScenario(
    const Vector& position,
    uint8_t dfsChannel,
    RadarInterferer::RadarType radarType)
{
    NS_LOG_FUNCTION(this << position << (int)dfsChannel << radarType);

    CreateRadar(position, dfsChannel, radarType);

    std::cout << "Created DFS test scenario with radar on channel " << (int)dfsChannel << std::endl;
}

// ==================== UTILITIES ====================

std::vector<Ptr<VirtualInterferer>>
VirtualInterfererHelper::GetAllInterferers() const
{
    return m_interferers;
}

void
VirtualInterfererHelper::TurnOnAll()
{
    NS_LOG_FUNCTION(this);
    for (auto& intf : m_interferers)
    {
        intf->TurnOn();
    }
}

void
VirtualInterfererHelper::TurnOffAll()
{
    NS_LOG_FUNCTION(this);
    for (auto& intf : m_interferers)
    {
        intf->TurnOff();
    }
}

void
VirtualInterfererHelper::InstallAll()
{
    NS_LOG_FUNCTION(this);
    for (auto& intf : m_interferers)
    {
        if (!intf->IsInstalled())
        {
            intf->Install();
        }
    }
}

void
VirtualInterfererHelper::UninstallAll()
{
    NS_LOG_FUNCTION(this);
    for (auto& intf : m_interferers)
    {
        if (intf->IsInstalled())
        {
            intf->Uninstall();
        }
    }
}

void
VirtualInterfererHelper::Clear()
{
    NS_LOG_FUNCTION(this);
    m_interferers.clear();
    m_positionRng = nullptr;
    m_paramRng = nullptr;
}

int64_t
VirtualInterfererHelper::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);
    m_positionRng->SetStream(stream);
    m_paramRng->SetStream(stream + 1);
    return 2;
}

} // namespace ns3
