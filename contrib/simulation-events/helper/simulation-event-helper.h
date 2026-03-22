/*
 * Copyright (c) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef SIMULATION_EVENT_HELPER_H
#define SIMULATION_EVENT_HELPER_H

#include "ns3/mac48-address.h"
#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/nstime.h"
#include "ns3/simulation-event-producer.h"

#include <map>
#include <string>

namespace ns3
{

/**
 * \brief Helper class for installing and configuring SimulationEventProducer
 *
 * This helper simplifies the process of setting up the SimulationEventProducer
 * and connecting it to relevant trace sources for capturing simulation events.
 *
 * Usage:
 * 1. Create helper and configure Kafka settings
 * 2. Set node ID mappings
 * 3. Install on a node
 * 4. Connect trace sources for each event type
 */
class SimulationEventHelper
{
  public:
    /**
     * \brief Constructor with default settings
     */
    SimulationEventHelper();

    /**
     * \brief Constructor with Kafka configuration
     * \param brokers Kafka broker addresses
     * \param topic Kafka topic name
     * \param simulationId Simulation identifier
     */
    SimulationEventHelper(const std::string& brokers,
                          const std::string& topic,
                          const std::string& simulationId);

    ~SimulationEventHelper();

    /**
     * \brief Set Kafka broker addresses
     * \param brokers Comma-separated list of brokers
     */
    void SetBrokers(const std::string& brokers);

    /**
     * \brief Set Kafka topic name
     * \param topic Topic name for events
     */
    void SetTopic(const std::string& topic);

    /**
     * \brief Set simulation identifier
     * \param simId Simulation ID for batch metadata
     */
    void SetSimulationId(const std::string& simId);

    /**
     * \brief Set flush interval for batched publishing
     * \param interval Time between batch flushes
     */
    void SetFlushInterval(Time interval);

    /**
     * \brief Set node ID mappings (simulation node ID -> config node ID)
     * \param staMapping STA node ID mapping
     * \param apMapping AP node ID mapping
     */
    void SetNodeIdMappings(const std::map<uint32_t, uint32_t>& staMapping,
                           const std::map<uint32_t, uint32_t>& apMapping);

    /**
     * \brief Install SimulationEventProducer on a node
     * \param node Node to install on
     * \return Pointer to installed producer
     */
    Ptr<SimulationEventProducer> Install(Ptr<Node> node);

    /**
     * \brief Get the installed producer
     * \return Pointer to producer (nullptr if not installed)
     */
    Ptr<SimulationEventProducer> GetProducer() const;

    /**
     * \brief Connect association/deassociation traces for STA devices
     *
     * Connects to StaWifiMac::Assoc and StaWifiMac::DeAssoc trace sources.
     *
     * \param staDevices Container of STA WiFi net devices
     */
    void ConnectStaTraces(NetDeviceContainer staDevices);

    /**
     * \brief Connect roaming completed trace for STA devices
     *
     * Connects to StaWifiMac::RoamingCompleted trace source.
     *
     * \param staDevices Container of STA WiFi net devices
     */
    void ConnectRoamingTraces(NetDeviceContainer staDevices);

    /**
     * \brief Get STA config node ID from simulation node ID
     * \param simNodeId Simulation node ID
     * \return Config node ID
     */
    uint32_t GetStaConfigNodeId(uint32_t simNodeId) const;

    /**
     * \brief Get AP config node ID from simulation node ID
     * \param simNodeId Simulation node ID
     * \return Config node ID
     */
    uint32_t GetApConfigNodeId(uint32_t simNodeId) const;

    /**
     * \brief Get AP config node ID from BSSID
     * \param bssid AP's BSSID
     * \return Config node ID (0 if not found)
     */
    uint32_t GetApConfigNodeIdByBssid(const Mac48Address& bssid) const;

    /**
     * \brief Register BSSID to config node ID mapping
     * \param bssid AP's BSSID
     * \param configNodeId Config node ID of the AP
     */
    void RegisterBssidMapping(const Mac48Address& bssid, uint32_t configNodeId);

  private:
    std::string m_brokers;
    std::string m_topic;
    std::string m_simulationId;
    Time m_flushInterval;

    std::map<uint32_t, uint32_t> m_staNodeIdMapping;
    std::map<uint32_t, uint32_t> m_apNodeIdMapping;
    std::map<Mac48Address, uint32_t> m_bssidToConfigNodeId;

    Ptr<SimulationEventProducer> m_producer;
};

} // namespace ns3

#endif /* SIMULATION_EVENT_HELPER_H */
