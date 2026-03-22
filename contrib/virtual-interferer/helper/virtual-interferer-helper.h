/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Virtual Interferer Helper - Simplifies interferer creation and installation
 */

#ifndef VIRTUAL_INTERFERER_HELPER_H
#define VIRTUAL_INTERFERER_HELPER_H

#include "ns3/virtual-interferer.h"
#include "ns3/virtual-interferer-environment.h"
#include "ns3/microwave-interferer.h"
#include "ns3/bluetooth-interferer.h"
#include "ns3/zigbee-interferer.h"
#include "ns3/radar-interferer.h"

#include "ns3/node-container.h"
#include "ns3/net-device-container.h"
#include "ns3/vector.h"
#include "ns3/random-variable-stream.h"

#include <vector>

namespace ns3
{

/**
 * \brief Helper class for creating and installing virtual interferers
 *
 * This helper simplifies the process of creating various types of
 * interferers and installing them in the simulation environment.
 */
class VirtualInterfererHelper
{
public:
    VirtualInterfererHelper();
    ~VirtualInterfererHelper();

    /**
     * \brief Configure the environment before creating interferers
     * \param config The environment configuration
     */
    void SetEnvironmentConfig(const VirtualInterfererEnvironmentConfig& config);

    /**
     * \brief Register WiFi devices to receive interference effects
     * \param devices The WiFi NetDevices to register
     */
    void RegisterWifiDevices(NetDeviceContainer devices);

    /**
     * \brief Auto-register all WiFi devices in the given nodes
     * \param nodes The nodes to scan for WiFi devices
     */
    void AutoRegisterWifiDevices(NodeContainer nodes);

    // ==================== MICROWAVE ====================

    /**
     * \brief Create a microwave interferer at the specified position
     * \param position The position of the microwave oven
     * \param powerLevel The power level (LOW, MEDIUM, HIGH)
     * \return The created microwave interferer
     */
    Ptr<MicrowaveInterferer> CreateMicrowave(
        const Vector& position,
        MicrowaveInterferer::PowerLevel powerLevel = MicrowaveInterferer::MEDIUM);

    /**
     * \brief Create multiple microwave interferers at random positions
     * \param count Number of interferers to create
     * \param minPos Minimum coordinates for random placement
     * \param maxPos Maximum coordinates for random placement
     * \return Vector of created interferers
     */
    std::vector<Ptr<MicrowaveInterferer>> CreateMicrowavesRandom(
        uint32_t count,
        const Vector& minPos,
        const Vector& maxPos);

    // ==================== BLUETOOTH ====================

    /**
     * \brief Create a Bluetooth interferer at the specified position
     * \param position The position of the Bluetooth device
     * \param deviceClass The Bluetooth device class
     * \param profile The Bluetooth profile being used
     * \return The created Bluetooth interferer
     */
    Ptr<BluetoothInterferer> CreateBluetooth(
        const Vector& position,
        BluetoothInterferer::DeviceClass deviceClass = BluetoothInterferer::CLASS_2,
        BluetoothInterferer::Profile profile = BluetoothInterferer::DATA_TRANSFER);

    /**
     * \brief Create multiple Bluetooth interferers at random positions
     * \param count Number of interferers to create
     * \param minPos Minimum coordinates for random placement
     * \param maxPos Maximum coordinates for random placement
     * \return Vector of created interferers
     */
    std::vector<Ptr<BluetoothInterferer>> CreateBluetoothsRandom(
        uint32_t count,
        const Vector& minPos,
        const Vector& maxPos);

    // ==================== ZIGBEE ====================

    /**
     * \brief Create a ZigBee interferer at the specified position
     * \param position The position of the ZigBee device
     * \param channel The ZigBee channel (11-26)
     * \param networkType The type of ZigBee network
     * \return The created ZigBee interferer
     */
    Ptr<ZigbeeInterferer> CreateZigbee(
        const Vector& position,
        uint8_t channel = 11,
        ZigbeeInterferer::NetworkType networkType = ZigbeeInterferer::SENSOR);

    /**
     * \brief Create multiple ZigBee interferers at random positions
     * \param count Number of interferers to create
     * \param minPos Minimum coordinates for random placement
     * \param maxPos Maximum coordinates for random placement
     * \return Vector of created interferers
     */
    std::vector<Ptr<ZigbeeInterferer>> CreateZigbeesRandom(
        uint32_t count,
        const Vector& minPos,
        const Vector& maxPos);

    // ==================== RADAR ====================

    /**
     * \brief Create a radar interferer at the specified position
     * \param position The position of the radar
     * \param dfsChannel The DFS channel (52-144)
     * \param radarType The type of radar
     * \return The created radar interferer
     */
    Ptr<RadarInterferer> CreateRadar(
        const Vector& position,
        uint8_t dfsChannel = 52,
        RadarInterferer::RadarType radarType = RadarInterferer::WEATHER);

    // ==================== SCHEDULING ====================

    /**
     * \brief Set a schedule for an interferer
     * \param interferer The interferer to schedule
     * \param onDuration How long the interferer is active
     * \param offDuration How long the interferer is inactive
     */
    void SetSchedule(Ptr<VirtualInterferer> interferer, Time onDuration, Time offDuration);

    /**
     * \brief Schedule an interferer to turn on at a specific time
     * \param interferer The interferer
     * \param startTime When to turn on
     */
    void ScheduleTurnOn(Ptr<VirtualInterferer> interferer, Time startTime);

    /**
     * \brief Schedule an interferer to turn off at a specific time
     * \param interferer The interferer
     * \param stopTime When to turn off
     */
    void ScheduleTurnOff(Ptr<VirtualInterferer> interferer, Time stopTime);

    // ==================== SCENARIOS ====================

    /**
     * \brief Create a typical home interference scenario
     *
     * Creates a microwave, some Bluetooth devices, and optionally ZigBee
     * \param centerPos Center position of the home
     * \param roomSize Size of the room (diameter)
     * \param numBluetooth Number of Bluetooth devices
     * \param includeZigbee Whether to include ZigBee network
     */
    void CreateHomeScenario(
        const Vector& centerPos,
        double roomSize,
        uint32_t numBluetooth = 3,
        bool includeZigbee = true);

    /**
     * \brief Create an office interference scenario
     *
     * Creates multiple Bluetooth devices and WiFi-like interferers
     * \param centerPos Center position of the office
     * \param officeSize Size of the office area
     * \param numBluetooth Number of Bluetooth devices
     */
    void CreateOfficeScenario(
        const Vector& centerPos,
        double officeSize,
        uint32_t numBluetooth = 10);

    /**
     * \brief Create a DFS test scenario with radar
     *
     * Creates radar interferers for DFS channel switch testing
     * \param position Position of the radar
     * \param dfsChannel The DFS channel to test
     * \param radarType Type of radar to simulate
     */
    void CreateDfsTestScenario(
        const Vector& position,
        uint8_t dfsChannel = 52,
        RadarInterferer::RadarType radarType = RadarInterferer::WEATHER);

    // ==================== UTILITIES ====================

    /**
     * \brief Get all created interferers
     * \return Vector of all interferers created by this helper
     */
    std::vector<Ptr<VirtualInterferer>> GetAllInterferers() const;

    /**
     * \brief Turn on all interferers
     */
    void TurnOnAll();

    /**
     * \brief Turn off all interferers
     */
    void TurnOffAll();

    /**
     * \brief Install all created interferers (register with environment)
     */
    void InstallAll();

    /**
     * \brief Uninstall all interferers
     */
    void UninstallAll();

    /**
     * \brief Clear all interferer references (call before Simulator::Destroy)
     */
    void Clear();

    /**
     * \brief Set the random variable stream for random placement
     * \param stream The random variable stream index
     * \return The number of streams used
     */
    int64_t AssignStreams(int64_t stream);

private:
    std::vector<Ptr<VirtualInterferer>> m_interferers;
    Ptr<UniformRandomVariable> m_positionRng;
    Ptr<UniformRandomVariable> m_paramRng;
};

} // namespace ns3

#endif /* VIRTUAL_INTERFERER_HELPER_H */
