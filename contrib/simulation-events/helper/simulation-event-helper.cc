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

#include "simulation-event-helper.h"

#include "ns3/log.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/wifi-net-device.h"

#include <regex>
#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("SimulationEventHelper");

SimulationEventHelper::SimulationEventHelper()
    : m_brokers("localhost:9092"),
      m_topic("simulator-events"),
      m_simulationId("sim-001"),
      m_flushInterval(MilliSeconds(100)),
      m_producer(nullptr)
{
    NS_LOG_FUNCTION(this);
}

SimulationEventHelper::SimulationEventHelper(const std::string& brokers,
                                             const std::string& topic,
                                             const std::string& simulationId)
    : m_brokers(brokers),
      m_topic(topic),
      m_simulationId(simulationId),
      m_flushInterval(MilliSeconds(100)),
      m_producer(nullptr)
{
    NS_LOG_FUNCTION(this << brokers << topic << simulationId);
}

SimulationEventHelper::~SimulationEventHelper()
{
    NS_LOG_FUNCTION(this);
}

void
SimulationEventHelper::SetBrokers(const std::string& brokers)
{
    NS_LOG_FUNCTION(this << brokers);
    m_brokers = brokers;
}

void
SimulationEventHelper::SetTopic(const std::string& topic)
{
    NS_LOG_FUNCTION(this << topic);
    m_topic = topic;
}

void
SimulationEventHelper::SetSimulationId(const std::string& simId)
{
    NS_LOG_FUNCTION(this << simId);
    m_simulationId = simId;
}

void
SimulationEventHelper::SetFlushInterval(Time interval)
{
    NS_LOG_FUNCTION(this << interval);
    m_flushInterval = interval;
}

void
SimulationEventHelper::SetNodeIdMappings(const std::map<uint32_t, uint32_t>& staMapping,
                                         const std::map<uint32_t, uint32_t>& apMapping)
{
    NS_LOG_FUNCTION(this);
    m_staNodeIdMapping = staMapping;
    m_apNodeIdMapping = apMapping;
}

Ptr<SimulationEventProducer>
SimulationEventHelper::Install(Ptr<Node> node)
{
    NS_LOG_FUNCTION(this << node);

    m_producer = CreateObject<SimulationEventProducer>();
    m_producer->SetBrokers(m_brokers);
    m_producer->SetTopic(m_topic);
    m_producer->SetSimulationId(m_simulationId);
    m_producer->SetFlushInterval(m_flushInterval);
    m_producer->SetNodeIdMappings(m_staNodeIdMapping, m_apNodeIdMapping);

    node->AddApplication(m_producer);

    // Start immediately (time 0)
    m_producer->SetStartTime(Seconds(0.0));

    NS_LOG_INFO("Installed SimulationEventProducer on node " << node->GetId());

    return m_producer;
}

Ptr<SimulationEventProducer>
SimulationEventHelper::GetProducer() const
{
    return m_producer;
}

uint32_t
SimulationEventHelper::GetStaConfigNodeId(uint32_t simNodeId) const
{
    auto it = m_staNodeIdMapping.find(simNodeId);
    if (it != m_staNodeIdMapping.end())
    {
        return it->second;
    }
    return simNodeId;
}

uint32_t
SimulationEventHelper::GetApConfigNodeId(uint32_t simNodeId) const
{
    auto it = m_apNodeIdMapping.find(simNodeId);
    if (it != m_apNodeIdMapping.end())
    {
        return it->second;
    }
    return simNodeId;
}

uint32_t
SimulationEventHelper::GetApConfigNodeIdByBssid(const Mac48Address& bssid) const
{
    auto it = m_bssidToConfigNodeId.find(bssid);
    if (it != m_bssidToConfigNodeId.end())
    {
        return it->second;
    }
    return 0;
}

void
SimulationEventHelper::RegisterBssidMapping(const Mac48Address& bssid, uint32_t configNodeId)
{
    NS_LOG_FUNCTION(this << bssid << configNodeId);
    m_bssidToConfigNodeId[bssid] = configNodeId;
}

void
SimulationEventHelper::ConnectStaTraces(NetDeviceContainer staDevices)
{
    NS_LOG_FUNCTION(this);

    if (!m_producer)
    {
        NS_LOG_WARN("Producer not installed, cannot connect traces");
        return;
    }

    // Note: Trace connection with callbacks that need access to helper state
    // should be done in config-simulation.cc where the callbacks can be
    // defined as free functions with access to global state.
    // This method is a placeholder that logs the intent.

    NS_LOG_INFO("STA trace connections should be done in config-simulation.cc "
                << "where callbacks can access global node ID mappings. "
                << "Use the producer's RecordAssociation/RecordDeassociation methods.");
}

void
SimulationEventHelper::ConnectRoamingTraces(NetDeviceContainer staDevices)
{
    NS_LOG_FUNCTION(this);

    if (!m_producer)
    {
        NS_LOG_WARN("Producer not installed, cannot connect traces");
        return;
    }

    // Note: Trace connection with callbacks that need access to helper state
    // should be done in config-simulation.cc where the callbacks can be
    // defined as free functions with access to global state.

    NS_LOG_INFO("Roaming trace connections should be done in config-simulation.cc "
                << "where callbacks can access global node ID mappings. "
                << "Use the producer's RecordClientRoamed method.");
}

} // namespace ns3
