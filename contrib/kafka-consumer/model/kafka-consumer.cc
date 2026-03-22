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

#include "kafka-consumer.h"

#include "ns3/flow-monitor.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

#include <kafka/Properties.h>
#include <chrono>
#include <iostream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("KafkaConsumer");

NS_OBJECT_ENSURE_REGISTERED(KafkaConsumer);

TypeId
KafkaConsumer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::KafkaConsumer")
                            .SetParent<Application>()
                            .SetGroupName("Applications")
                            .AddConstructor<KafkaConsumer>();
    return tid;
}

KafkaConsumer::KafkaConsumer()
    : m_brokers("localhost:9092"),
      m_topic("optimization-commands"),
      m_groupId("ns3-consumer"),
      m_simulationId("sim-001"),
      m_pollInterval(MilliSeconds(100)),
      m_consumer(nullptr),
      m_flowMonitor(nullptr),
      m_messagesReceived(0),
      m_commandsProcessed(0)
{
    NS_LOG_FUNCTION(this);
}

KafkaConsumer::~KafkaConsumer()
{
    NS_LOG_FUNCTION(this);
}

void
KafkaConsumer::SetBrokers(std::string brokers)
{
    NS_LOG_FUNCTION(this << brokers);
    m_brokers = brokers;
}

void
KafkaConsumer::SetTopic(std::string topic)
{
    NS_LOG_FUNCTION(this << topic);
    m_topic = topic;
}

void
KafkaConsumer::SetGroupId(std::string groupId)
{
    NS_LOG_FUNCTION(this << groupId);
    m_groupId = groupId;
}

void
KafkaConsumer::SetSimulationId(std::string simId)
{
    NS_LOG_FUNCTION(this << simId);
    m_simulationId = simId;
}

void
KafkaConsumer::SetPollInterval(Time interval)
{
    NS_LOG_FUNCTION(this << interval);
    m_pollInterval = interval;
}

void
KafkaConsumer::SetParameterCallback(Callback<void, std::string, ApParameters> callback)
{
    NS_LOG_FUNCTION(this);
    m_paramCallback = callback;
}

void
KafkaConsumer::SetCommandCallback(Callback<void, std::string, std::map<std::string, std::string>> callback)
{
    NS_LOG_FUNCTION(this);
    m_commandCallback = callback;
}

void
KafkaConsumer::SetFlowMonitor(Ptr<FlowMonitor> flowMonitor)
{
    NS_LOG_FUNCTION(this << flowMonitor);
    m_flowMonitor = flowMonitor;
}

kafka::clients::consumer::KafkaConsumer*
KafkaConsumer::GetKafkaConsumer() const
{
    return m_consumer.get();
}

void
KafkaConsumer::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_flowMonitor = nullptr;
    Application::DoDispose();
}

void
KafkaConsumer::StartApplication()
{
    NS_LOG_FUNCTION(this);

    try
    {
        // Create Kafka configuration using modern-cpp-kafka Properties
        kafka::Properties props;
        props.put("bootstrap.servers", m_brokers);
        props.put("group.id", m_groupId + "-" + m_simulationId);  // Unique group per simulation
        props.put("enable.auto.commit", "true");
        props.put("auto.offset.reset", "latest");
        props.put("session.timeout.ms", "10000");      // Reduced from 30s to 10s
        props.put("heartbeat.interval.ms", "3000");    // Faster heartbeat for quicker detection
        props.put("max.poll.interval.ms", "30000");    // Max time between polls

        // Create consumer with RAII
        m_consumer = std::make_unique<kafka::clients::consumer::KafkaConsumer>(props);

        NS_LOG_INFO("Kafka consumer created successfully");
        NS_LOG_INFO("  Brokers: " << m_brokers);
        NS_LOG_INFO("  Topic: " << m_topic);
        NS_LOG_INFO("  Group ID: " << m_groupId);
        NS_LOG_INFO("  Simulation ID: " << m_simulationId);
        NS_LOG_INFO("  Poll Interval: " << m_pollInterval.As(Time::MS) << " ms");

        // Subscribe to BOTH topics:
        // 1. Original topic (downstream) for parameter updates
        // 2. simulator-commands topic for stress test commands (FORCE_DFS, etc.)
        kafka::Topics topics = {m_topic, "simulator-commands"};
        m_consumer->subscribe(topics);

        NS_LOG_INFO("Subscribed to topics: " << m_topic << ", simulator-commands");

        // Schedule first poll
        m_pollEvent = Simulator::Schedule(m_pollInterval, &KafkaConsumer::PollMessages, this);

        NS_LOG_INFO("KafkaConsumer started on node " << GetNode()->GetId());
    }
    catch (const kafka::KafkaException& e)
    {
        NS_LOG_ERROR("Failed to create Kafka consumer: " << e.what());
        m_consumer.reset();
    }
}

void
KafkaConsumer::StopApplication()
{
    NS_LOG_FUNCTION(this);

    // Cancel scheduled polling
    if (m_pollEvent.IsPending())
    {
        Simulator::Cancel(m_pollEvent);
    }

    // Close consumer (RAII handles cleanup via unique_ptr)
    if (m_consumer)
    {
        m_consumer->close();
        m_consumer.reset();
    }

    NS_LOG_INFO("KafkaConsumer stopped");
    NS_LOG_INFO("  Total messages received: " << m_messagesReceived);
    NS_LOG_INFO("  Total commands processed: " << m_commandsProcessed);
}

void
KafkaConsumer::PollMessages()
{
    NS_LOG_FUNCTION(this);

    if (!m_consumer)
    {
        NS_LOG_WARN("Consumer not initialized, skipping poll");
        return;
    }

    // Non-blocking poll (0ms timeout) - better for simulation thread
    auto records = m_consumer->poll(std::chrono::milliseconds(0));

    for (const auto& record : records)
    {
        if (!record.error())
        {
            // Message received successfully
            m_messagesReceived++;

            // Extract payload
            std::string payload(static_cast<const char*>(record.value().data()), record.value().size());

            // Extract key (simulation ID)
            std::string key;
            if (record.key().size() > 0)
            {
                key = std::string(static_cast<const char*>(record.key().data()), record.key().size());
            }

            NS_LOG_INFO("Received message #" << m_messagesReceived
                                             << " from topic " << record.topic()
                                             << " partition " << record.partition()
                                             << " offset " << record.offset()
                                             << " key=" << key
                                             << " payload_size=" << record.value().size());

            // Filter by simulation ID if key matches
            if (key.empty() || key == m_simulationId)
            {
                // Process the message
                try
                {
                    ProcessMessage(payload);
                }
                catch (const std::exception& e)
                {
                    NS_LOG_ERROR("Error processing message: " << e.what());
                }
            }
            else
            {
                NS_LOG_INFO("Skipping message with mismatched simulation ID: " << key
                            << " (expected: " << m_simulationId << ")");
            }
        }
        else if (record.error().value() == RD_KAFKA_RESP_ERR__PARTITION_EOF)
        {
            // End of partition, not an error
            NS_LOG_DEBUG("Reached end of partition");
        }
        else
        {
            // Actual error
            NS_LOG_WARN("Kafka consumer error: " << record.error().message());
        }
    }

    // Schedule next poll
    m_pollEvent = Simulator::Schedule(m_pollInterval, &KafkaConsumer::PollMessages, this);
}

void
KafkaConsumer::ProcessMessage(const std::string& json)
{
    NS_LOG_FUNCTION(this << json.substr(0, 100));

    try
    {
        // Parse JSON into OptimizationCommand
        OptimizationCommand command = ParseJsonCommand(json);

        NS_LOG_INFO("Parsed command: type=" << command.commandType
                                            << " sim_id=" << command.simulationId
                                            << " timestamp=" << command.timestampUnix
                                            << " ap_count=" << command.apParameters.size());

        // Verify simulation ID matches
        if (!command.simulationId.empty() && command.simulationId != m_simulationId)
        {
            NS_LOG_WARN("Command simulation ID mismatch: " << command.simulationId
                        << " (expected: " << m_simulationId << ")");
            return;
        }

        // Route based on command type:
        // - Stress test commands (FORCE_DFS, HIGH_INTERFERENCE, etc.) → command callback
        // - Parameter updates (UPDATE_AP_PARAMETERS, etc.) → parameter callback
        if (command.commandType == "FORCE_DFS" ||
            command.commandType == "HIGH_INTERFERENCE" ||
            command.commandType == "HIGH_THROUGHPUT")
        {
            // It's a stress test command - invoke command callback
            if (!m_commandCallback.IsNull())
            {
                // Extract parameters from the command (if any)
                std::map<std::string, std::string> params;
                // For now, commands don't have additional parameters
                // Future: parse from command.apParameters or add new field
                
                NS_LOG_INFO("Invoking command callback for: " << command.commandType);
                m_commandCallback(command.commandType, params);
            }
            else
            {
                NS_LOG_WARN("No command callback registered for: " << command.commandType);
            }
        }
        else
        {
            // It's a parameter update command - dispatch to parameter callback
            DispatchParameters(command);
        }

        m_commandsProcessed++;

    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Failed to parse or process message: " << e.what());
    }
}

void
KafkaConsumer::DispatchParameters(const OptimizationCommand& command)
{
    NS_LOG_FUNCTION(this);

    if (!m_paramCallback.IsNull())
    {
        // Invoke callback for each AP's parameters
        for (const auto& [bssid, params] : command.apParameters)
        {
            NS_LOG_INFO("Dispatching parameters for AP " << bssid
                        << ": power=" << params.txPowerStartDbm
                        << " channel=" << +params.channelNumber
                        << " width=" << params.channelWidthMhz);

            m_paramCallback(bssid, params);
        }
    }
    else
    {
        NS_LOG_WARN("No parameter callback registered, parameters will not be applied");
    }
}

} // namespace ns3
