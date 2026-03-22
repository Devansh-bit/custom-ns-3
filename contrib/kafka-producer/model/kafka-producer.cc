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

#include "kafka-producer.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/string.h"

#include <kafka/Properties.h>
#include <kafka/ProducerRecord.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("KafkaProducer");
NS_OBJECT_ENSURE_REGISTERED(KafkaProducer);

TypeId
KafkaProducer::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::KafkaProducer")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<KafkaProducer>()
            .AddAttribute("Brokers",
                          "Kafka broker addresses (comma-separated)",
                          StringValue("localhost:9092"),
                          MakeStringAccessor(&KafkaProducer::m_brokers),
                          MakeStringChecker())
            .AddAttribute("Topic",
                          "Kafka topic name",
                          StringValue("ns3-metrics"),
                          MakeStringAccessor(&KafkaProducer::m_topic),
                          MakeStringChecker())
            .AddAttribute("SimulationId",
                          "Simulation identifier (used as message key)",
                          StringValue("sim-001"),
                          MakeStringAccessor(&KafkaProducer::m_simulationId),
                          MakeStringChecker())
            .AddAttribute("UpdateInterval",
                          "Interval between metric updates",
                          TimeValue(Seconds(1.0)),
                          MakeTimeAccessor(&KafkaProducer::m_updateInterval),
                          MakeTimeChecker());
    return tid;
}

KafkaProducer::KafkaProducer()
    : m_brokers("localhost:9092"),
      m_topic("ns3-metrics"),
      m_simulationId("sim-001"),
      m_updateInterval(Seconds(1.0)),
      m_producer(nullptr)
{
    NS_LOG_FUNCTION(this);
}

KafkaProducer::~KafkaProducer()
{
    NS_LOG_FUNCTION(this);
}

void
KafkaProducer::SetBrokers(const std::string& brokers)
{
    NS_LOG_FUNCTION(this << brokers);
    m_brokers = brokers;
}

void
KafkaProducer::SetTopic(const std::string& topic)
{
    NS_LOG_FUNCTION(this << topic);
    m_topic = topic;
}

void
KafkaProducer::SetSimulationId(const std::string& simId)
{
    NS_LOG_FUNCTION(this << simId);
    m_simulationId = simId;
}

void
KafkaProducer::SetUpdateInterval(Time interval)
{
    NS_LOG_FUNCTION(this << interval);
    m_updateInterval = interval;
}

void
KafkaProducer::SetDisplayTimeScaleFactor(double factor)
{
    NS_LOG_FUNCTION(this << factor);
    m_displayTimeScaleFactor = factor;
}

void
KafkaProducer::SetDisplayTimeOffsetSeconds(double offset)
{
    NS_LOG_FUNCTION(this << offset);
    m_displayTimeOffsetSeconds = offset;
}

void
KafkaProducer::UpdateApMetrics(const Mac48Address& bssid, const ApMetrics& metrics)
{
    NS_LOG_FUNCTION(this << bssid);

    // Serialize immediately and store only the JSON string
    // This avoids deep copy of nested vectors/maps (scanningChannelData)
    std::string serialized = SerializeApMetrics(metrics);
    m_serializedMetrics[bssid] = serialized;
}

void
KafkaProducer::DoDispose()
{
    NS_LOG_FUNCTION(this);
    Application::DoDispose();
}

void
KafkaProducer::StartApplication()
{
    NS_LOG_FUNCTION(this);

    try
    {
        // Create Kafka configuration using modern-cpp-kafka Properties
        kafka::Properties props;
        props.put("bootstrap.servers", m_brokers);
        props.put("socket.timeout.ms", "30000");
        props.put("message.timeout.ms", "30000");
        props.put("request.timeout.ms", "30000");

        // Create producer with RAII
        m_producer = std::make_unique<kafka::clients::producer::KafkaProducer>(props);

        NS_LOG_INFO("KafkaProducer started: brokers=" << m_brokers << ", topic=" << m_topic);
        std::cout << "[KafkaProducer] Started: broker=" << m_brokers << " topic=" << m_topic << std::endl;

        // Schedule first metric send
        m_sendEvent = Simulator::Schedule(Seconds(0.0), &KafkaProducer::SendMetrics, this);
    }
    catch (const kafka::KafkaException& e)
    {
        NS_LOG_ERROR("Failed to create Kafka producer: " << e.what());
        std::cout << "[KafkaProducer] ERROR: Failed to create producer: " << e.what() << std::endl;
        m_producer.reset();
    }
}

kafka::clients::producer::KafkaProducer* KafkaProducer::GetKafkaProducer()
{
    return m_producer.get();
}

void
KafkaProducer::StopApplication()
{
    NS_LOG_FUNCTION(this);

    Simulator::Cancel(m_sendEvent);

    if (m_producer)
    {
        // Flush any pending messages - wait up to 30 seconds
        NS_LOG_INFO("Flushing pending messages...");
        auto error = m_producer->flush(std::chrono::seconds(30));
        if (error)
        {
            NS_LOG_WARN("Some messages could not be delivered: " << error.message());
        }
        else
        {
            NS_LOG_INFO("All messages flushed successfully");
        }
        m_producer.reset();
    }

    NS_LOG_INFO("KafkaProducer stopped");
}

void
KafkaProducer::SendMetrics()
{
    NS_LOG_FUNCTION(this);

    if (!m_producer)
    {
        NS_LOG_WARN("Producer not initialized, skipping send");
        return;
    }

    // Serialize all metrics to JSON (using pre-serialized individual metrics)
    std::string jsonPayload = SerializeAllMetrics();

    try
    {
        // Create producer record with topic, key, and value
        kafka::clients::producer::ProducerRecord record(
            m_topic,
            kafka::Key(m_simulationId.c_str(), m_simulationId.size()),
            kafka::Value(jsonPayload.c_str(), jsonPayload.size())
        );

        // Send asynchronously with no callback (fire and forget)
        m_producer->send(record,
                         [](const kafka::clients::producer::RecordMetadata& /*metadata*/,
                            const kafka::Error& error) {
                             if (error) {
                                 std::cerr << "[KafkaProducer] Delivery failed: " << error.message() << std::endl;
                             }
                         },
                         kafka::clients::producer::KafkaProducer::SendOption::ToCopyRecordValue);

        NS_LOG_INFO("Sent metrics to Kafka: " << jsonPayload.size() << " bytes");
        std::cout << "[KafkaProducer] Sent " << jsonPayload.size() << " bytes at t="
                  << Simulator::Now().GetSeconds() << "s" << std::endl;
    }
    catch (const kafka::KafkaException& e)
    {
        NS_LOG_ERROR("Failed to produce message: " << e.what());
        std::cout << "[KafkaProducer] ERROR: Failed to produce: " << e.what() << std::endl;
    }

    // Schedule next send
    m_sendEvent = Simulator::Schedule(m_updateInterval, &KafkaProducer::SendMetrics, this);
}

std::string
KafkaProducer::SerializeAllMetrics()
{
    NS_LOG_FUNCTION(this);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);

    // Get current time (apply display time scale factor and offset)
    std::time_t unixTime = std::time(nullptr);
    double simTimeSeconds = Simulator::Now().GetSeconds() * m_displayTimeScaleFactor + m_displayTimeOffsetSeconds;

    oss << "{";
    oss << "\"timestamp_unix\":" << unixTime << ",";
    oss << "\"sim_time_seconds\":" << simTimeSeconds << ",";

    // Use pre-serialized AP metrics (no deep copy or re-serialization needed)
    oss << "\"ap_metrics\":{";
    bool firstAp = true;
    for (const auto& entry : m_serializedMetrics)
    {
        if (!firstAp)
            oss << ",";
        firstAp = false;

        std::ostringstream macOss;
        macOss << entry.first;
        oss << "\"" << macOss.str() << "\":" << entry.second;  // Use pre-serialized JSON
    }
    oss << "}";

    oss << "}";

    return oss.str();
}

std::string
KafkaProducer::SerializeApMetrics(const ApMetrics& metrics)
{
    NS_LOG_FUNCTION(this);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);

    std::ostringstream bssidOss;
    bssidOss << metrics.bssid;

    oss << "{";
    oss << "\"node_id\":" << metrics.nodeId << ",";
    oss << "\"sim_node_id\":" << metrics.simNodeId << ",";
    oss << "\"bssid\":\"" << bssidOss.str() << "\",";
    oss << "\"position_x\":" << metrics.positionX << ",";
    oss << "\"position_y\":" << metrics.positionY << ",";
    oss << "\"position_z\":" << metrics.positionZ << ",";
    oss << "\"channel\":" << static_cast<int>(metrics.channel) << ",";
    oss << "\"channel_width\":" << metrics.channelWidth << ",";
    oss << "\"band\":\"" << BandToString(metrics.band) << "\",";

    // Convert 0-1 scale to percentage (0-100)
    oss << "\"channel_utilization\":" << (metrics.channelUtilization * 100.0) << ",";
    oss << "\"wifi_channel_utilization\":" << (metrics.wifiChannelUtilization * 100.0) << ",";
    oss << "\"non_wifi_channel_utilization\":" << (metrics.nonWifiChannelUtilization * 100.0) << ",";

    // TX Power Control fields
    oss << "\"tx_power_dbm\":" << metrics.txPowerDbm << ",";
    oss << "\"obsspd_level_dbm\":" << metrics.obsspdLevelDbm << ",";
    oss << "\"bss_color\":" << static_cast<int>(metrics.bssColor) << ",";
    oss << "\"mcs_ewma\":" << std::fixed << std::setprecision(2) << metrics.mcsEwma << ",";

    // Flat client management fields
    oss << "\"associated_clients\":" << metrics.associatedClients << ",";
    oss << "\"client_list\":[";
    bool firstClient = true;
    for (const auto& client : metrics.clientList)
    {
        if (!firstClient)
            oss << ",";
        firstClient = false;
        std::ostringstream clientOss;
        clientOss << client;
        oss << "\"" << clientOss.str() << "\"";
    }
    oss << "],";

    // Flat throughput fields
    oss << "\"bytes_sent\":" << metrics.bytesSent << ",";
    oss << "\"bytes_received\":" << metrics.bytesReceived << ",";
    oss << "\"throughput_mbps\":" << metrics.throughputMbps << ",";

    // Nested: connection_metrics
    oss << "\"connection_metrics\":{";
    bool firstConn = true;
    for (const auto& conn : metrics.connectionMetrics)
    {
        if (!firstConn)
            oss << ",";
        firstConn = false;
        oss << "\"" << conn.first << "\":{";
        oss << "\"sta_address\":\"" << conn.second.staAddress << "\",";
        oss << "\"ap_address\":\"" << conn.second.APAddress << "\",";
        oss << "\"node_id\":" << conn.second.nodeId << ",";
        oss << "\"sim_node_id\":" << conn.second.simNodeId << ",";
        oss << "\"position_x\":" << conn.second.positionX << ",";
        oss << "\"position_y\":" << conn.second.positionY << ",";
        oss << "\"position_z\":" << conn.second.positionZ << ",";
        oss << "\"mean_rtt_latency\":" << conn.second.meanRTTLatency << ",";
        oss << "\"jitter_ms\":" << conn.second.jitterMs << ",";
        oss << "\"packet_count\":" << conn.second.packetCount << ",";
        oss << "\"uplink_throughput_mbps\":" << conn.second.uplinkThroughputMbps << ",";
        oss << "\"downlink_throughput_mbps\":" << conn.second.downlinkThroughputMbps << ",";
        // Keep rates as fractions (0-1 scale)
        oss << "\"packet_loss_rate\":" << conn.second.packetLossRate << ",";
        oss << "\"mac_retry_rate\":" << conn.second.MACRetryRate << ",";
        oss << "\"ap_view_rssi\":" << conn.second.apViewRSSI << ",";
        oss << "\"ap_view_snr\":" << conn.second.apViewSNR << ",";
        oss << "\"sta_view_rssi\":" << conn.second.staViewRSSI << ",";
        oss << "\"sta_view_snr\":" << conn.second.staViewSNR << ",";
        // Temporarily disabled MCS logging - will enable later
        // oss << "\"uplink_mcs\":" << (int)conn.second.uplinkMCS << ",";
        // oss << "\"downlink_mcs\":" << (int)conn.second.downlinkMCS << ",";
        oss << "\"last_update_seconds\":" << TimeToSeconds(conn.second.lastUpdate);
        oss << "}";
    }
    oss << "},";

    // Nested: scanning_channel_data (Part B - Multi-channel scanning)
    oss << "\"scanning_channel_data\":{";
    bool firstChannel = true;
    for (const auto& channelEntry : metrics.scanningChannelData)
    {
        if (!firstChannel)
            oss << ",";
        firstChannel = false;

        uint8_t channel = channelEntry.first;
        const ChannelScanData& scanData = channelEntry.second;

        oss << "\"" << static_cast<int>(channel) << "\":{";
        // ChannelScanData stores 0-1 scale, convert to percentage for output
        oss << "\"channel_utilization\":" << (scanData.channelUtilization * 100.0) << ",";
        oss << "\"wifi_channel_utilization\":" << (scanData.wifiChannelUtilization * 100.0) << ",";
        oss << "\"non_wifi_channel_utilization\":" << (scanData.nonWifiChannelUtilization * 100.0) << ",";
        oss << "\"bssid_count\":" << scanData.bssidCount << ",";
        oss << "\"neighbors\":[";

        bool firstNeighbor = true;
        for (const auto& neighbor : scanData.neighbors)
        {
            if (!firstNeighbor)
                oss << ",";
            firstNeighbor = false;

            oss << "{";
            oss << "\"bssid\":\"" << neighbor.bssid << "\"";
            oss << ",\"rssi\":" << neighbor.rssi;
            oss << ",\"channel\":" << static_cast<int>(neighbor.channel);
            oss << ",\"channel_width\":" << neighbor.channelWidth;
            oss << ",\"sta_count\":" << neighbor.staCount;
            // Convert 0-255 scale to percentage
            double utilPercent = (neighbor.channelUtil / 255.0) * 100.0;
            oss << ",\"channel_utilization\":" << utilPercent;
            // WiFi and non-WiFi utilization (0-255 scale to percentage)
            double wifiUtilPercent = (neighbor.wifiUtil / 255.0) * 100.0;
            double nonWifiUtilPercent = (neighbor.nonWifiUtil / 255.0) * 100.0;
            oss << ",\"wifi_utilization\":" << wifiUtilPercent;
            oss << ",\"non_wifi_utilization\":" << nonWifiUtilPercent;
            oss << "}";
        }

        oss << "]";
        oss << "}";
    }
    oss << "},";

    oss << "\"last_update_seconds\":" << TimeToSeconds(metrics.lastUpdate);
    oss << "}";

    return oss.str();
}

std::string
KafkaProducer::BandToString(WifiPhyBand band)
{
    switch (band)
    {
    case WIFI_PHY_BAND_2_4GHZ:
        return "BAND_2_4GHZ";
    case WIFI_PHY_BAND_5GHZ:
        return "BAND_5GHZ";
    case WIFI_PHY_BAND_6GHZ:
        return "BAND_6GHZ";
    default:
        return "BAND_UNSPECIFIED";
    }
}

double
KafkaProducer::TimeToSeconds(Time t)
{
    return t.GetSeconds() * m_displayTimeScaleFactor + m_displayTimeOffsetSeconds;
}

} // namespace ns3