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

#ifndef KAFKA_CONSUMER_HELPER_H
#define KAFKA_CONSUMER_HELPER_H

#include "../model/kafka-consumer.h"

#include "ns3/application-container.h"
#include "ns3/node-container.h"
#include "ns3/nstime.h"

#include <string>

namespace ns3
{

/**
 * \ingroup kafka-consumer
 * \brief Helper to simplify KafkaConsumer installation
 *
 * This helper creates and configures KafkaConsumer applications,
 * making it easy to add Kafka message consumption to any simulation.
 *
 * Example usage:
 * \code
 *   KafkaConsumerHelper helper("localhost:9092", "optimization-commands", "ns3-consumer", "sim-001");
 *   helper.SetPollInterval(MilliSeconds(100));
 *   ApplicationContainer apps = helper.Install(nodes);
 *   apps.Start(Seconds(0.0));
 *   apps.Stop(Seconds(100.0));
 * \endcode
 */
class KafkaConsumerHelper
{
  public:
    /**
     * \brief Create a KafkaConsumerHelper with default configuration
     *
     * Default values:
     * - brokers: "localhost:9092"
     * - topic: "optimization-commands"
     * - groupId: "ns3-consumer"
     * - simulationId: "sim-001"
     */
    KafkaConsumerHelper();

    /**
     * \brief Create a KafkaConsumerHelper with specified configuration
     *
     * \param brokers Kafka broker addresses (e.g., "localhost:9092")
     * \param topic Kafka topic to consume from
     * \param groupId Consumer group ID
     * \param simulationId Simulation identifier for message filtering
     */
    KafkaConsumerHelper(std::string brokers,
                       std::string topic,
                       std::string groupId,
                       std::string simulationId);

    /**
     * \brief Set the polling interval
     * \param interval How often to poll Kafka for new messages
     */
    void SetPollInterval(Time interval);

    /**
     * \brief Install a KafkaConsumer on a single node
     * \param node The node to install the application on
     * \return ApplicationContainer holding the installed application
     */
    ApplicationContainer Install(Ptr<Node> node) const;

    /**
     * \brief Install KafkaConsumer on multiple nodes
     * \param nodes The nodes to install the application on
     * \return ApplicationContainer holding all installed applications
     */
    ApplicationContainer Install(NodeContainer nodes) const;

    /**
     * \brief Get the KafkaConsumer application from a node
     *
     * This static helper function searches for the first KafkaConsumer
     * application on a node and returns it.
     *
     * \param node The node to search
     * \return Pointer to KafkaConsumer application, or nullptr if not found
     */
    static Ptr<KafkaConsumer> GetKafkaConsumer(Ptr<Node> node);

  private:
    std::string m_brokers;       ///< Kafka broker addresses
    std::string m_topic;         ///< Kafka topic
    std::string m_groupId;       ///< Consumer group ID
    std::string m_simulationId;  ///< Simulation ID
    Time m_pollInterval;         ///< Polling interval
};

} // namespace ns3

#endif /* KAFKA_CONSUMER_HELPER_H */