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

#ifndef KAFKA_PRODUCER_H
#define KAFKA_PRODUCER_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/mac48-address.h"
#include "ns3/nstime.h"
#include "ns3/wifi-phy-band.h"

#include <kafka/KafkaProducer.h>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ns3
{

/**
 * \brief Connection metrics for latency and jitter tracking
 */
struct ConnectionMetrics
{
    std::string staAddress;
    std::string APAddress;
    uint32_t nodeId = 0;          // STA's config node ID
    uint32_t simNodeId = 0;       // STA's simulation node ID (ns-3 internal)
    double positionX = 0.0;   // STA position X coordinate
    double positionY = 0.0;   // STA position Y coordinate
    double positionZ = 0.0;   // STA position Z coordinate
    double meanRTTLatency = 0.0; // From FlowMonitor
    double jitterMs = 0.0; // From FlowMonitor
    uint64_t packetCount = 0;
    double uplinkThroughputMbps = 0.0; // From FlowMonitor
    double downlinkThroughputMbps = 0.0; // From FlowMonitor

    // Packet Loss
    double packetLossRate = 0.0; // Using FlowMonitor
    double MACRetryRate = 0.0; // This needs to be fixed in config-simulation.cc

    double apViewRSSI = 0.0;
    double apViewSNR = 0.0;

    double staViewRSSI = 0.0;
    double staViewSNR = 0.0;

    uint8_t uplinkMCS = 0;   // MCS for STA→AP transmissions
    uint8_t downlinkMCS = 0; // MCS for AP→STA transmissions

    Time lastUpdate;
};

/**
 * \brief Neighbor AP information detected on a specific scanning channel
 *
 * Contains information about a neighbor AP whose beacon was heard
 * by this AP's scanning radio on a particular channel.
 */
struct ChannelNeighborInfo
{
    std::string bssid;        ///< Neighbor AP BSSID - use std::string like connectionMetrics does
    double rssi = 0.0;        ///< RSSI of neighbor's beacon as heard by scanning AP (dBm)
    uint8_t channel = 0;      ///< Operating channel of this neighbor AP (from their beacon)
    uint16_t channelWidth = 20;  ///< Channel width of neighbor AP in MHz (from HT/VHT/HE IE)
    uint16_t staCount = 0;    ///< Number of STAs associated with neighbor AP (from BSS Load IE)
    uint8_t channelUtil = 0;  ///< Channel utilization of neighbor AP (from BSS Load IE, 0-255)
    uint8_t wifiUtil = 0;     ///< WiFi channel utilization (from BSS Load IE AAC high byte, 0-255)
    uint8_t nonWifiUtil = 0;  ///< Non-WiFi channel utilization (from BSS Load IE AAC low byte, 0-255)
};

/**
 * \brief Per-channel scan data from scanning radio
 *
 * Contains all information gathered while scanning a specific channel,
 * including channel utilization and list of detected neighbor APs.
 */
struct ChannelScanData
{
    double channelUtilization = 0.0;      ///< Total CCA utilization on this scanning channel (0-100%)
    double wifiChannelUtilization = 0.0;  ///< WiFi CCA utilization (TX + RX + WiFi CCA busy) (0-100%)
    double nonWifiChannelUtilization = 0.0; ///< Non-WiFi CCA utilization (interference) (0-100%)
    uint32_t bssidCount = 0;              ///< Number of unique neighbor APs detected on this channel
    std::vector<ChannelNeighborInfo> neighbors;  ///< List of all neighbor APs detected
};

/**
 * \brief AP (Access Point) metrics
 */
struct ApMetrics
{
    uint32_t nodeId = 0;          // Config node ID (from config-simulation.json)
    uint32_t simNodeId = 0;       // Simulation node ID (ns-3 internal)
    Mac48Address bssid;
    double positionX = 0.0;   // AP position X coordinate
    double positionY = 0.0;   // AP position Y coordinate
    double positionZ = 0.0;   // AP position Z coordinate
    uint8_t channel = 0;
    WifiPhyBand band = WIFI_PHY_BAND_UNSPECIFIED;
    uint16_t channelWidth = 20;  // Channel bandwidth in MHz (20/40/80/160)

    double channelUtilization = 0.0;
    double wifiChannelUtilization = 0.0;     // WiFi-only utilization (%)
    double nonWifiChannelUtilization = 0.0;  // Non-WiFi utilization (%)

    // TX Power Control (from power-scoring module)
    double txPowerDbm = 21.0;         // Current TX power (dBm)
    double obsspdLevelDbm = -82.0;    // Current OBSS/PD level (dBm)
    uint8_t bssColor = 0;             // BSS Color (802.11ax)
    double mcsEwma = 0.0;             // MCS EWMA from power scoring (0-11 for HE)

    // Client Management (flat)
    uint32_t associatedClients;
    std::set<Mac48Address> clientList;

    // Throughput (flat)
    uint64_t bytesSent;
    uint64_t bytesReceived;
    double throughputMbps;

    // MAC Address of the associated STA mapped to Connection Metrics of that STA.
    std::map<std::string, ConnectionMetrics> connectionMetrics;

    // Per-channel scan data from scanning radio
    // Maps: Channel number -> Scan data (utilization + list of neighbors)
    // For each channel this AP scans, stores:
    //   - Channel utilization (measured by this AP's scanning radio)
    //   - Count of neighbor BSSIDs detected
    //   - Details of each neighbor AP whose beacon was heard on that channel
    std::map<uint8_t, ChannelScanData> scanningChannelData;

    Time lastUpdate;
};

/**
 * \ingroup applications
 * \brief Kafka Producer application for streaming simulator metrics
 *
 * This application collects AP and STA metrics from the simulator
 * and publishes them to a Kafka topic at regular intervals.
 */
class KafkaProducer : public Application
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    KafkaProducer();
    ~KafkaProducer() override;

    /**
     * \brief Set the Kafka broker address
     * \param brokers Comma-separated list of brokers (e.g., "localhost:9092")
     */
    void SetBrokers(const std::string& brokers);

    /**
     * \brief Set the Kafka topic name
     * \param topic Topic name (e.g., "simulator-metrics")
     */
    void SetTopic(const std::string& topic);

    /**
     * \brief Set the simulation ID used as Kafka message key
     * \param simId Simulation identifier (e.g., "sim-001")
     */
    void SetSimulationId(const std::string& simId);

    /**
     * \brief Set the update interval for sending metrics
     * \param interval Time between metric updates
     */
    void SetUpdateInterval(Time interval);

    /**
     * \brief Set the display time scale factor
     * \param factor Scale factor (e.g., 10.0 means 1 sim second = 10 display seconds)
     */
    void SetDisplayTimeScaleFactor(double factor);
    void SetDisplayTimeOffsetSeconds(double offset);

    /**
     * \brief Register AP metrics for collection
     * \param bssid AP's BSSID
     * \param metrics AP metrics structure
     */
    void UpdateApMetrics(const Mac48Address& bssid, const ApMetrics& metrics);


    kafka::clients::producer::KafkaProducer* GetKafkaProducer();

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    /**
     * \brief Periodically collect and send metrics to Kafka
     */
    void SendMetrics();

    /**
     * \brief Serialize all metrics to JSON
     * \return JSON string containing all AP and STA metrics
     */
    std::string SerializeAllMetrics();

    /**
     * \brief Serialize AP metrics to JSON
     * \param metrics AP metrics structure
     * \return JSON string
     */
    std::string SerializeApMetrics(const ApMetrics& metrics);

    /**
     * \brief Helper: Convert WifiPhyBand to string
     */
    std::string BandToString(WifiPhyBand band);

    /**
     * \brief Helper: Convert Time to seconds (double)
     */
    double TimeToSeconds(Time t);

    std::string m_brokers;                           //!< Kafka broker addresses
    std::string m_topic;                             //!< Kafka topic name
    std::string m_simulationId;                      //!< Simulation ID (message key)
    Time m_updateInterval;                           //!< Update interval for metrics
    double m_displayTimeScaleFactor = 1.0;           //!< Display time scale factor (1 sim sec = N display sec)
    double m_displayTimeOffsetSeconds = 0.0;         //!< Display time offset in seconds
    EventId m_sendEvent;                             //!< Event for periodic sending
    std::unique_ptr<kafka::clients::producer::KafkaProducer> m_producer;  //!< Kafka producer (RAII)
    // Store pre-serialized JSON instead of ApMetrics to avoid deep copy issues
    std::map<Mac48Address, std::string> m_serializedMetrics; //!< Pre-serialized JSON
};

} // namespace ns3

#endif /* KAFKA_PRODUCER_H */
