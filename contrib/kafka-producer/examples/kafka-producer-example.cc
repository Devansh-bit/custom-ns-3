/*
 * Copyright (c) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 */

/**
 * \file
 * \brief Example demonstrating KafkaProducer usage
 *
 * This example creates a simple WiFi network with 2 APs and 3 STAs,
 * and sends metrics to Kafka using the KafkaProducer module.
 */

#include "ns3/core-module.h"
#include "ns3/kafka-producer-helper.h"
#include "ns3/kafka-producer.h"
#include "ns3/network-module.h"

#include <chrono>
#include <thread>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("KafkaProducerExample");

/**
 * \brief Callback to update AP metrics periodically
 * \param producer Pointer to KafkaProducer
 * \param bssid AP's BSSID
 */
void
UpdateApMetrics(Ptr<KafkaProducer> producer, Mac48Address bssid)
{
    NS_LOG_FUNCTION(producer << bssid);

    // Create dummy AP metrics
    ApMetrics metrics;
    metrics.nodeId = 0;
    metrics.bssid = bssid;
    metrics.channel = 36;
    metrics.band = WIFI_PHY_BAND_5GHZ;

    // Channel utilization (dummy values)
    metrics.phyIdleTime = 0.85;
    metrics.phyTxTime = 0.08;
    metrics.phyRxTime = 0.05;
    metrics.phyCcaBusyTime = 0.02;
    metrics.channelUtilization = 0.15;

    // Client management
    metrics.associatedClients = 2;
    metrics.clientList.insert(Mac48Address("aa:bb:cc:dd:ee:01"));
    metrics.clientList.insert(Mac48Address("aa:bb:cc:dd:ee:02"));

    // Throughput
    metrics.bytesSent = 1500000;
    metrics.bytesReceived = 800000;
    metrics.throughputMbps = 54.3;

    // Connection metrics (example)
    ConnectionMetrics conn1;
    conn1.sourceAddress = "00:00:00:00:00:01";
    conn1.destinationAddress = "aa:bb:cc:dd:ee:01";
    conn1.meanLatencyMs = 12.5;
    conn1.jitterMs = 2.3;
    conn1.packetCount = 1250;
    conn1.byteCount = 750000;
    conn1.throughputMbps = 25.5;
    conn1.lastUpdate = Simulator::Now();
    metrics.connectionMetrics["conn-001"] = conn1;

    // Client link quality (example)
    ClientLinkQuality quality1;
    quality1.rssi = -65.0;
    quality1.snr = 35.0;
    quality1.rcpi = 120;
    quality1.rsni = 70;
    quality1.linkMargin = 20;
    quality1.lastUpdate = Simulator::Now();
    metrics.clientLinkQuality[Mac48Address("aa:bb:cc:dd:ee:01")] = quality1;

    metrics.lastUpdate = Simulator::Now();

    // Update metrics in producer
    producer->UpdateApMetrics(bssid, metrics);

    NS_LOG_INFO("Updated AP metrics for " << bssid);
}

/**
 * \brief Callback to update STA metrics periodically
 * \param producer Pointer to KafkaProducer
 * \param macAddress STA's MAC address
 */
void
UpdateStaMetrics(Ptr<KafkaProducer> producer, Mac48Address macAddress)
{
    NS_LOG_FUNCTION(producer << macAddress);

    // Create dummy STA metrics
    StaMetrics metrics;
    metrics.nodeId = 10;
    metrics.macAddress = macAddress;

    // Current association
    metrics.currentBssid = Mac48Address("00:00:00:00:00:01");
    metrics.currentChannel = 36;
    metrics.currentBand = WIFI_PHY_BAND_5GHZ;
    metrics.isAssociated = true;

    // Link quality
    metrics.currentRssi = -65.0;
    metrics.currentSnr = 35.0;
    metrics.retries = 5;
    metrics.lastLinkQualityUpdate = Simulator::Now();

    // Latency/Jitter
    metrics.meanLatency = 12.5;
    metrics.jitter = 2.3;
    metrics.packetCount = 1250;
    metrics.throughputMbps = 45.2;
    metrics.lastLatencyUpdate = Simulator::Now();

    // Discovered neighbors (example)
    NeighborInfo neighbor;
    neighbor.bssid = Mac48Address("00:00:00:00:00:02");
    neighbor.channel = 40;
    neighbor.regulatoryClass = 115;
    neighbor.phyType = 9;
    neighbor.rssi = -72.0;
    neighbor.discovered = Seconds(35.0);
    metrics.discoveredNeighbors.push_back(neighbor);
    metrics.lastNeighborUpdate = Simulator::Now();

    // Scan results
    metrics.scanResults[Mac48Address("00:00:00:00:00:01")] = -65.0;
    metrics.scanResults[Mac48Address("00:00:00:00:00:02")] = -72.0;
    metrics.lastScanTime = Seconds(40.0);

    // Roaming history (empty for now)
    metrics.lastUpdate = Simulator::Now();

    // Update metrics in producer
    producer->UpdateStaMetrics(macAddress, metrics);

    NS_LOG_INFO("Updated STA metrics for " << macAddress);
}

int
main(int argc, char* argv[])
{
    std::string brokers = "localhost:9092";
    std::string topic = "simulator-metrics";
    std::string simId = "sim-001";
    double simTime = 10.0; // seconds

    CommandLine cmd(__FILE__);
    cmd.AddValue("brokers", "Kafka broker addresses", brokers);
    cmd.AddValue("topic", "Kafka topic name", topic);
    cmd.AddValue("simId", "Simulation ID", simId);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.Parse(argc, argv);

    LogComponentEnable("KafkaProducerExample", LOG_LEVEL_INFO);
    LogComponentEnable("KafkaProducer", LOG_LEVEL_INFO);

    NS_LOG_INFO("Starting KafkaProducer example");
    NS_LOG_INFO("Brokers: " << brokers);
    NS_LOG_INFO("Topic: " << topic);
    NS_LOG_INFO("Simulation ID: " << simId);

    // Create a single node for the KafkaProducer
    NodeContainer nodes;
    nodes.Create(1);

    // Install KafkaProducer application
    KafkaProducerHelper kafkaHelper(brokers, topic, simId);
    kafkaHelper.SetUpdateInterval(Seconds(1.0));
    ApplicationContainer apps = kafkaHelper.Install(nodes.Get(0));
    apps.Start(Seconds(0.0));  // Start app immediately to begin Kafka connection
    apps.Stop(Seconds(simTime + 3.0));  // Give 3 extra seconds to flush messages

    // Get the KafkaProducer instance
    Ptr<KafkaProducer> producer = kafkaHelper.GetKafkaProducer(nodes.Get(0));

    // MAC addresses for AP and STA
    Mac48Address ap1Bssid("00:00:00:00:00:01");
    Mac48Address sta1Mac("aa:bb:cc:dd:ee:01");

    // Initial update at t=0.0 so first message has data
    Simulator::Schedule(Seconds(0.0),
                        &UpdateApMetrics,
                        producer,
                        ap1Bssid);
    Simulator::Schedule(Seconds(0.0),
                        &UpdateStaMetrics,
                        producer,
                        sta1Mac);

    // Schedule periodic metric updates (simulating data collection)
    for (double t = 1.0; t < simTime; t += 1.0)
    {
        Simulator::Schedule(Seconds(t),
                            &UpdateApMetrics,
                            producer,
                            ap1Bssid);
        Simulator::Schedule(Seconds(t),
                            &UpdateStaMetrics,
                            producer,
                            sta1Mac);
    }

    NS_LOG_INFO("Running simulation for " << simTime << " seconds");
    Simulator::Stop(Seconds(simTime + 3.0));  // Extra time for Kafka flush
    Simulator::Run();

    // Important: Give Kafka time to flush messages before destroying
    // The flush happens in StopApplication() which needs time to complete
    NS_LOG_INFO("Simulation completed - waiting for Kafka flush...");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    Simulator::Destroy();

    NS_LOG_INFO("All done!");
    return 0;
}
