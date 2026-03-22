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

#include "kafka-producer-helper.h"

#include "ns3/application-container.h"
#include "ns3/kafka-producer.h"
#include "ns3/log.h"
#include "ns3/names.h"
#include "ns3/node-container.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("KafkaProducerHelper");

KafkaProducerHelper::KafkaProducerHelper()
    : m_brokers("10.145.54.131:9092"),  // Default to user's Kafka broker
      m_topic("simulator-metrics"),
      m_simulationId("sim-001"),
      m_updateInterval(Seconds(1.0))
{
    NS_LOG_FUNCTION(this);
}

KafkaProducerHelper::KafkaProducerHelper(const std::string& brokers,
                                         const std::string& topic,
                                         const std::string& simId)
    : m_brokers(brokers),
      m_topic(topic),
      m_simulationId(simId),
      m_updateInterval(Seconds(1.0))
{
    NS_LOG_FUNCTION(this << brokers << topic << simId);
}

void
KafkaProducerHelper::SetBrokers(const std::string& brokers)
{
    NS_LOG_FUNCTION(this << brokers);
    m_brokers = brokers;
}

void
KafkaProducerHelper::SetTopic(const std::string& topic)
{
    NS_LOG_FUNCTION(this << topic);
    m_topic = topic;
}

void
KafkaProducerHelper::SetSimulationId(const std::string& simId)
{
    NS_LOG_FUNCTION(this << simId);
    m_simulationId = simId;
}

void
KafkaProducerHelper::SetUpdateInterval(Time interval)
{
    NS_LOG_FUNCTION(this << interval);
    m_updateInterval = interval;
}

ApplicationContainer
KafkaProducerHelper::Install(Ptr<Node> node) const
{
    NS_LOG_FUNCTION(this << node);
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
KafkaProducerHelper::Install(NodeContainer nodes) const
{
    NS_LOG_FUNCTION(this);
    ApplicationContainer apps;
    for (auto i = nodes.Begin(); i != nodes.End(); ++i)
    {
        apps.Add(InstallPriv(*i));
    }

    return apps;

}

Ptr<Application>
KafkaProducerHelper::InstallPriv(Ptr<Node> node) const
{
    NS_LOG_FUNCTION(this << node);

    Ptr<KafkaProducer> kafka_producer = CreateObject<KafkaProducer>();
    kafka_producer->SetBrokers(m_brokers);
    kafka_producer->SetTopic(m_topic);
    kafka_producer->SetSimulationId(m_simulationId);
    kafka_producer->SetUpdateInterval(m_updateInterval);

    node->AddApplication(kafka_producer);

    return kafka_producer;
}

Ptr<KafkaProducer>
KafkaProducerHelper::GetKafkaProducer(Ptr<Node> node)
{
    NS_LOG_FUNCTION(node);

    for (uint32_t i = 0; i < node->GetNApplications(); ++i)
    {
        Ptr<Application> app = node->GetApplication(i);
        Ptr<KafkaProducer> kafkaProducer = DynamicCast<KafkaProducer>(app);
        if (kafkaProducer)
        {
            return kafkaProducer;
        }
    }

    NS_LOG_WARN("KafkaProducer not found on node " << node->GetId());
    return nullptr;
}

} // namespace ns3
