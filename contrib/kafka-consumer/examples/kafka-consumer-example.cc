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

/**
 * \file kafka-consumer-example.cc
 * \brief Example demonstrating KafkaConsumer module usage
 *
 * This example shows how to:
 * 1. Install a KafkaConsumer on a node
 * 2. Register a callback to receive optimization parameters
 * 3. Process parameters when they arrive from Kafka
 *
 * Prerequisites:
 * - Kafka broker running on localhost:9092
 * - Topic "optimization-commands" created (or auto-created)
 * - Python producer sending messages (run test_producer.py)
 *
 * Usage:
 *   ./ns3 run kafka-consumer-example
 *
 * While running, use the Python producer to send parameters:
 *   python3 test_producer.py --scenario single-ap
 */

#include "ns3/core-module.h"
#include "ns3/kafka-consumer-helper.h"
#include "ns3/kafka-consumer.h"
#include "ns3/optimization-command.h"

#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("KafkaConsumerExample");

/**
 * \brief Callback function invoked when parameters are received
 *
 * This function is called by the KafkaConsumer for each AP in the
 * received optimization command.
 *
 * \param bssid AP BSSID (MAC address as string)
 * \param params AP parameters from the optimization command
 */
void
OnParametersReceived(std::string bssid, ApParameters params)
{
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "PARAMETERS RECEIVED FOR AP: " << bssid << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << "Power Configuration:" << std::endl;
    std::cout << "  TxPowerStart:     " << params.txPowerStartDbm << " dBm" << std::endl;
    std::cout << "  TxPowerEnd:       " << params.txPowerEndDbm << " dBm" << std::endl;
    std::cout << "  CCA ED Threshold: " << params.ccaEdThresholdDbm << " dBm" << std::endl;
    std::cout << "  OBSS PD:          " << params.obssPd << " dBm" << std::endl;
    std::cout << "  RX Sensitivity:   " << params.rxSensitivityDbm << " dBm" << std::endl;

    std::cout << "\nChannel Configuration:" << std::endl;
    std::cout << "  Channel Number:   " << +params.channelNumber << std::endl;
    std::cout << "  Channel Width:    " << params.channelWidthMhz << " MHz" << std::endl;
    std::cout << "  Band:             " << params.band << std::endl;
    std::cout << "  Primary 20 Index: " << +params.primary20Index << std::endl;

    std::cout << std::string(80, '=') << std::endl;
    std::cout << "✓ Parameters would be applied to AP " << bssid << " here" << std::endl;
    std::cout << std::string(80, '=') << "\n" << std::endl;
}

int
main(int argc, char* argv[])
{
    // Simulation parameters
    std::string brokers = "localhost:9092";
    std::string topic = "optimization-commands";
    std::string groupId = "ns3-consumer-example";
    std::string simulationId = "sim-001";
    double simTime = 60.0;  // Run for 60 seconds
    Time pollInterval = MilliSeconds(100);

    // Command line arguments
    CommandLine cmd(__FILE__);
    cmd.AddValue("brokers", "Kafka broker addresses", brokers);
    cmd.AddValue("topic", "Kafka topic to consume from", topic);
    cmd.AddValue("groupId", "Consumer group ID", groupId);
    cmd.AddValue("simulationId", "Simulation identifier", simulationId);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("pollInterval", "Kafka poll interval in milliseconds", pollInterval);
    cmd.Parse(argc, argv);

    // Enable logging
    LogComponentEnable("KafkaConsumer", LOG_LEVEL_INFO);
    LogComponentEnable("KafkaConsumerExample", LOG_LEVEL_INFO);

    NS_LOG_INFO("=============================================================================");
    NS_LOG_INFO("Kafka Consumer Example");
    NS_LOG_INFO("=============================================================================");
    NS_LOG_INFO("Configuration:");
    NS_LOG_INFO("  Brokers:         " << brokers);
    NS_LOG_INFO("  Topic:           " << topic);
    NS_LOG_INFO("  Group ID:        " << groupId);
    NS_LOG_INFO("  Simulation ID:   " << simulationId);
    NS_LOG_INFO("  Simulation Time: " << simTime << " seconds");
    NS_LOG_INFO("  Poll Interval:   " << pollInterval.As(Time::MS) << " ms");
    NS_LOG_INFO("=============================================================================");

    // Create a single node for the Kafka consumer
    NodeContainer nodes;
    nodes.Create(1);

    // Install Kafka consumer on the node
    KafkaConsumerHelper consumerHelper(brokers, topic, groupId, simulationId);
    consumerHelper.SetPollInterval(pollInterval);
    ApplicationContainer apps = consumerHelper.Install(nodes.Get(0));

    // Set start and stop time
    apps.Start(Seconds(0.0));
    apps.Stop(Seconds(simTime + 1.0));

    // Get the consumer and register the callback
    Ptr<KafkaConsumer> consumer = consumerHelper.GetKafkaConsumer(nodes.Get(0));
    if (consumer)
    {
        consumer->SetParameterCallback(MakeCallback(&OnParametersReceived));
        NS_LOG_INFO("✓ Callback registered successfully");
    }
    else
    {
        NS_LOG_ERROR("✗ Failed to get KafkaConsumer");
        return 1;
    }

    NS_LOG_INFO("=============================================================================");
    NS_LOG_INFO("Starting simulation...");
    NS_LOG_INFO("Waiting for optimization commands from Kafka...");
    NS_LOG_INFO("(Run 'python3 test_producer.py' to send parameters)");
    NS_LOG_INFO("=============================================================================\n");

    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    NS_LOG_INFO("\n=============================================================================");
    NS_LOG_INFO("Simulation finished");
    NS_LOG_INFO("=============================================================================");

    Simulator::Destroy();

    return 0;
}