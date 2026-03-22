/*
 * Copyright (c) 2024
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

#include "kafka-consumer-helper.h"

#include "ns3/log.h"
#include "ns3/names.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("KafkaConsumerHelper");

KafkaConsumerHelper::KafkaConsumerHelper()
    : m_brokers("localhost:9092"),
      m_topic("optimization-commands"),
      m_groupId("ns3-consumer"),
      m_simulationId("sim-001"),
      m_pollInterval(MilliSeconds(100))
{
    NS_LOG_FUNCTION(this);
}

KafkaConsumerHelper::KafkaConsumerHelper(std::string brokers,
                                       std::string topic,
                                       std::string groupId,
                                       std::string simulationId)
    : m_brokers(brokers),
      m_topic(topic),
      m_groupId(groupId),
      m_simulationId(simulationId),
      m_pollInterval(MilliSeconds(100))
{
    NS_LOG_FUNCTION(this << brokers << topic << groupId << simulationId);
}

void
KafkaConsumerHelper::SetPollInterval(Time interval)
{
    NS_LOG_FUNCTION(this << interval);
    m_pollInterval = interval;
}

ApplicationContainer
KafkaConsumerHelper::Install(Ptr<Node> node) const
{
    NS_LOG_FUNCTION(this << node);

    ApplicationContainer apps;

    // Create KafkaConsumer application
    Ptr<KafkaConsumer> consumer = CreateObject<KafkaConsumer>();

    // Configure the consumer
    consumer->SetBrokers(m_brokers);
    consumer->SetTopic(m_topic);
    consumer->SetGroupId(m_groupId);
    consumer->SetSimulationId(m_simulationId);
    consumer->SetPollInterval(m_pollInterval);

    // Add to node
    node->AddApplication(consumer);
    apps.Add(consumer);

    NS_LOG_INFO("Installed KafkaConsumer on node " << node->GetId()
                << " (brokers=" << m_brokers
                << " topic=" << m_topic
                << " groupId=" << m_groupId
                << " simId=" << m_simulationId << ")");

    return apps;
}

ApplicationContainer
KafkaConsumerHelper::Install(NodeContainer nodes) const
{
    NS_LOG_FUNCTION(this);

    ApplicationContainer apps;

    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        apps.Add(Install(nodes.Get(i)));
    }

    return apps;
}

Ptr<KafkaConsumer>
KafkaConsumerHelper::GetKafkaConsumer(Ptr<Node> node)
{
    NS_LOG_FUNCTION(node);

    if (!node)
    {
        NS_LOG_ERROR("Node is null");
        return nullptr;
    }

    // Search for KafkaConsumer application on the node
    for (uint32_t i = 0; i < node->GetNApplications(); ++i)
    {
        Ptr<Application> app = node->GetApplication(i);
        Ptr<KafkaConsumer> consumer = DynamicCast<KafkaConsumer>(app);

        if (consumer)
        {
            NS_LOG_INFO("Found KafkaConsumer at application index " << i
                        << " on node " << node->GetId());
            return consumer;
        }
    }

    NS_LOG_WARN("No KafkaConsumer found on node " << node->GetId());
    return nullptr;
}

} // namespace ns3