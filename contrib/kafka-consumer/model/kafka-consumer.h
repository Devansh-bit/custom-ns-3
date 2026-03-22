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

#ifndef KAFKA_CONSUMER_H
#define KAFKA_CONSUMER_H

#include "optimization-command.h"

#include "ns3/application.h"
#include "ns3/callback.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"

#include <kafka/KafkaConsumer.h>
#include <memory>
#include <string>

namespace ns3
{

class FlowMonitor;

/**
 * \ingroup applications
 * \defgroup kafka-consumer KafkaConsumer
 *
 * This application consumes optimization commands from Apache Kafka
 * and triggers callbacks to apply parameters to the simulation.
 */

/**
 * \ingroup kafka-consumer
 * \brief NS-3 Application that consumes optimization commands from Kafka
 *
 * KafkaConsumer is an NS-3 Application that connects to an Apache Kafka broker,
 * subscribes to a topic containing optimization commands (typically from a
 * Bayesian Optimizer), and polls for messages periodically.
 *
 * When a message arrives, it parses the JSON payload into an OptimizationCommand
 * structure and invokes user-registered callbacks for each AP's parameters.
 *
 * Usage:
 * \code
 *   Ptr<KafkaConsumer> consumer = CreateObject<KafkaConsumer>();
 *   consumer->SetBrokers("localhost:9092");
 *   consumer->SetTopic("optimization-commands");
 *   consumer->SetSimulationId("sim-001");
 *   consumer->SetParameterCallback(MakeCallback(&MyCallbackFunction));
 *   node->AddApplication(consumer);
 *   consumer->SetStartTime(Seconds(0.0));
 *   consumer->SetStopTime(Seconds(100.0));
 * \endcode
 */
class KafkaConsumer : public Application
{
  public:
    /**
     * \brief Get the type ID
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * \brief Constructor
     */
    KafkaConsumer();

    /**
     * \brief Destructor
     */
    ~KafkaConsumer() override;

    /**
     * \brief Set Kafka broker addresses
     * \param brokers Comma-separated list of broker addresses (e.g., "localhost:9092")
     */
    void SetBrokers(std::string brokers);

    /**
     * \brief Set Kafka topic to consume from
     * \param topic Topic name (e.g., "optimization-commands")
     */
    void SetTopic(std::string topic);

    /**
     * \brief Set consumer group ID
     * \param groupId Consumer group identifier (e.g., "ns3-consumer")
     */
    void SetGroupId(std::string groupId);

    /**
     * \brief Set simulation ID for message filtering
     * \param simId Simulation identifier (e.g., "sim-001")
     */
    void SetSimulationId(std::string simId);

    /**
     * \brief Set polling interval
     * \param interval How often to poll Kafka for new messages
     */
    void SetPollInterval(Time interval);

    /**
     * \brief Register callback for AP parameter updates
     *
     * The callback will be invoked for each AP in the received command.
     *
     * \param callback Callback function: void callback(std::string bssid, ApParameters params)
     */
    void SetParameterCallback(Callback<void, std::string, ApParameters> callback);

    /**
     * \brief Register callback for stress test commands (FORCE_DFS, HIGH_INTERFERENCE, etc.)
     *
     * The callback will be invoked when a command message is received.
     * Commands are distinguished from parameter updates by having a "command" field
     * instead of "bssid" field in the JSON payload.
     *
     * \param callback Callback function: void callback(std::string commandType, std::map<std::string, std::string> params)
     */
    void SetCommandCallback(Callback<void, std::string, std::map<std::string, std::string>> callback);

    /**
     * \brief Set the FlowMonitor to reset when Kafka messages are consumed
     * \param flowMonitor Pointer to the FlowMonitor object
     */
    void SetFlowMonitor(Ptr<FlowMonitor> flowMonitor);

    /**
     * \brief Get the underlying modern-cpp-kafka consumer
     * \return Pointer to kafka::clients::consumer::KafkaConsumer
     */
    kafka::clients::consumer::KafkaConsumer* GetKafkaConsumer() const;

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    /**
     * \brief Poll Kafka for new messages
     *
     * This method is scheduled periodically according to m_pollInterval.
     * It calls the Kafka consumer's consume() method and processes any
     * messages that arrive.
     */
    void PollMessages();

    /**
     * \brief Process a received Kafka message
     * \param json JSON string from the message payload
     */
    void ProcessMessage(const std::string& json);

    /**
     * \brief Dispatch parameters from a command to callbacks
     * \param command Parsed optimization command
     */
    void DispatchParameters(const OptimizationCommand& command);

    // Kafka configuration
    std::string m_brokers;                       ///< Kafka broker addresses
    std::string m_topic;                         ///< Kafka topic to consume from
    std::string m_groupId;                       ///< Consumer group ID
    std::string m_simulationId;                  ///< Simulation ID for filtering
    Time m_pollInterval;                         ///< Polling interval

    // Kafka objects (modern-cpp-kafka)
    std::unique_ptr<kafka::clients::consumer::KafkaConsumer> m_consumer;  ///< Kafka consumer instance (RAII)

    // Callbacks
    Callback<void, std::string, ApParameters> m_paramCallback;  ///< User callback for AP parameters
    Callback<void, std::string, std::map<std::string, std::string>> m_commandCallback;  ///< User callback for commands

    // FlowMonitor
    Ptr<FlowMonitor> m_flowMonitor;              ///< FlowMonitor to reset when messages are consumed

    // Polling event
    EventId m_pollEvent;                         ///< Scheduled polling event

    // Statistics
    uint64_t m_messagesReceived;                 ///< Total messages received
    uint64_t m_commandsProcessed;                ///< Total commands processed
};

} // namespace ns3

#endif /* KAFKA_CONSUMER_H */