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

#ifndef KAFKA_PRODUCER_HELPER_H
#define KAFKA_PRODUCER_HELPER_H

#include "ns3/application-helper.h"
#include "ns3/kafka-producer.h"
#include "ns3/nstime.h"

#include <string>

namespace ns3
{

/**
 * \ingroup applications
 * \brief Helper to install KafkaProducer applications
 *
 * This helper simplifies the creation and installation of KafkaProducer
 * applications on nodes in the simulator.
 */
class KafkaProducerHelper
{
  public:
    /**
     * \brief Create a KafkaProducerHelper with default configuration
     *
     * Default values:
     * - Brokers: "localhost:9092"
     * - Topic: "simulator-metrics"
     * - SimulationId: "sim-001"
     * - UpdateInterval: 1 second
     */
    KafkaProducerHelper();

    /**
     * \brief Create a KafkaProducerHelper with custom configuration
     * \param brokers Kafka broker addresses (comma-separated)
     * \param topic Kafka topic name
     * \param simId Simulation identifier
     */
    KafkaProducerHelper(const std::string& brokers,
                        const std::string& topic,
                        const std::string& simId);

    /**
     * \brief Set the Kafka broker addresses
     * \param brokers Comma-separated list of brokers (e.g., "localhost:9092")
     */
    void SetBrokers(const std::string& brokers);

    /**
     * \brief Set the Kafka topic name
     * \param topic Topic name (e.g., "simulator-metrics")
     */
    void SetTopic(const std::string& topic);

    /**
     * \brief Set the simulation ID
     * \param simId Simulation identifier (e.g., "sim-001")
     */
    void SetSimulationId(const std::string& simId);

    /**
     * \brief Set the update interval
     * \param interval Time between metric updates
     */
    void SetUpdateInterval(Time interval);

    /**
     * \brief Install the KafkaProducer application on a node
     * \param node Node to install the application on
     * \return Application container with the installed application
     */
    ApplicationContainer Install(Ptr<Node> node) const;

    /**
     * \brief Install the KafkaProducer application on multiple nodes
     * \param nodes Container of nodes to install on
     * \return Application container with all installed applications
     */
    ApplicationContainer Install(NodeContainer nodes) const;

    /**
     * \brief Get a pointer to the KafkaProducer application
     * \param node Node that has the KafkaProducer installed
     * \return Pointer to KafkaProducer, or nullptr if not found
     */
    static Ptr<KafkaProducer> GetKafkaProducer(Ptr<Node> node);


  private:
    /**
     * \brief Internal method to create and configure KafkaProducer
     * \return Configured KafkaProducer application
     */
    Ptr<Application> InstallPriv(Ptr<Node> node) const;

    std::string m_brokers;      //!< Kafka broker addresses
    std::string m_topic;        //!< Kafka topic name
    std::string m_simulationId; //!< Simulation ID
    Time m_updateInterval;      //!< Update interval
};

} // namespace ns3

#endif /* KAFKA_PRODUCER_HELPER_H */
