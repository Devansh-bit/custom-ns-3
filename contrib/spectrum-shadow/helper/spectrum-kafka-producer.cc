/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "spectrum-kafka-producer.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include <kafka/Properties.h>
#include <kafka/ProducerRecord.h>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SpectrumKafkaProducer");
NS_OBJECT_ENSURE_REGISTERED(SpectrumKafkaProducer);

TypeId
SpectrumKafkaProducer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::SpectrumKafkaProducer")
        .SetParent<Object>()
        .SetGroupName("SpectrumShadow")
        .AddConstructor<SpectrumKafkaProducer>();
    return tid;
}

SpectrumKafkaProducer::SpectrumKafkaProducer()
    : m_producer(nullptr),
      m_initialized(false),
      m_windowIndex(0),
      m_windowStartTime(Seconds(0)),
      m_running(false)
{
    NS_LOG_FUNCTION(this);
}

SpectrumKafkaProducer::~SpectrumKafkaProducer()
{
    NS_LOG_FUNCTION(this);
    Stop();

    // modern-cpp-kafka handles flushing via RAII destructor
    if (m_producer)
    {
        m_producer.reset();
    }
}

void
SpectrumKafkaProducer::SetConfig(const SpectrumKafkaConfig& config)
{
    NS_LOG_FUNCTION(this);
    m_config = config;
}

bool
SpectrumKafkaProducer::Initialize()
{
    NS_LOG_FUNCTION(this);

    if (m_initialized)
    {
        return true;
    }

    try
    {
        // Create Kafka configuration using modern-cpp-kafka Properties
        kafka::Properties props;
        props.put("bootstrap.servers", m_config.brokers);
        // Background thread handles delivery callbacks automatically
        props.put("enable.manual.events.poll", "false");
        // Optimized for low latency
        props.put("linger.ms", "5");
        props.put("batch.size", "16384");
        props.put("compression.type", "lz4");

        // Create producer instance with background thread for delivery callbacks
        m_producer = std::make_unique<kafka::clients::producer::KafkaProducer>(props);

        m_initialized = true;
        NS_LOG_INFO("SpectrumKafkaProducer initialized: " << m_config.brokers
                    << " topic=" << m_config.topic);
        std::cout << "[SpectrumKafka] Initialized: " << m_config.brokers
                  << " topic=" << m_config.topic << std::endl;

        return true;
    }
    catch (const kafka::KafkaException& e)
    {
        NS_LOG_ERROR("Failed to create Kafka producer: " << e.what());
        return false;
    }
}

void
SpectrumKafkaProducer::Start()
{
    NS_LOG_FUNCTION(this);
    
    if (!m_initialized)
    {
        NS_LOG_WARN("SpectrumKafkaProducer not initialized, call Initialize() first");
        return;
    }
    
    m_running = true;
    StartWindow();
    
    NS_LOG_INFO("SpectrumKafkaProducer started");
    std::cout << "[SpectrumKafka] Started - publishing every " 
              << m_config.windowDuration.GetMilliSeconds() << "ms" << std::endl;
}

void
SpectrumKafkaProducer::Stop()
{
    NS_LOG_FUNCTION(this);
    m_running = false;
    
    if (m_windowEndEvent.IsPending())
    {
        Simulator::Cancel(m_windowEndEvent);
    }
}

void
SpectrumKafkaProducer::ReceivePsd(uint32_t nodeId, Ptr<const SpectrumValue> psd)
{
    NS_LOG_FUNCTION(this << nodeId);
    
    if (!m_running || !psd)
    {
        return;
    }
    
    double timestamp = Simulator::Now().GetSeconds();
    
    // Lock for thread safety
    std::lock_guard<std::mutex> lock(m_sampleMutex);
    
    // Extract PSD values
    Values::const_iterator it = psd->ConstValuesBegin();
    Bands::const_iterator bit = psd->ConstBandsBegin();
    
    while (it != psd->ConstValuesEnd())
    {
        PsdSample sample;
        sample.nodeId = nodeId;
        sample.timestampSec = timestamp;
        sample.frequencyHz = bit->fc;
        sample.psdWattsPerHz = *it;
        
        m_currentWindowSamples.push_back(sample);
        
        ++it;
        ++bit;
    }
}

void
SpectrumKafkaProducer::StartWindow()
{
    NS_LOG_FUNCTION(this);
    
    m_windowIndex++;
    m_windowStartTime = Simulator::Now();
    
    // Clear samples from previous window
    {
        std::lock_guard<std::mutex> lock(m_sampleMutex);
        m_currentWindowSamples.clear();
    }
    
    NS_LOG_INFO("[SpectrumKafka] Window " << m_windowIndex << " started at " 
                << m_windowStartTime.GetSeconds() << "s");
    
    // Schedule window end
    m_windowEndEvent = Simulator::Schedule(
        m_config.windowDuration,
        &SpectrumKafkaProducer::EndWindow,
        this
    );
}

void
SpectrumKafkaProducer::EndWindow()
{
    NS_LOG_FUNCTION(this);
    
    // Serialize and publish
    std::string json = SerializeWindowToJson();
    PublishToKafka(json);
    
    NS_LOG_INFO("[SpectrumKafka] Window " << m_windowIndex << " published ("
                << m_currentWindowSamples.size() << " samples)");
    std::cout << "[SpectrumKafka] Window " << m_windowIndex << " published ("
              << m_currentWindowSamples.size() << " samples)" << std::endl;
    
    // Start next window if still running
    if (m_running)
    {
        StartWindow();
    }
}

std::string
SpectrumKafkaProducer::SerializeWindowToJson()
{
    std::lock_guard<std::mutex> lock(m_sampleMutex);
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(9);
    
    oss << "{";
    oss << "\"window_index\":" << m_windowIndex << ",";
    oss << "\"simulation_id\":\"" << m_config.simulationId << "\",";
    oss << "\"band\":\"" << m_config.bandId << "\",";
    oss << "\"window_start_sec\":" << m_windowStartTime.GetSeconds() << ",";
    oss << "\"window_duration_ms\":" << m_config.windowDuration.GetMilliSeconds() << ",";
    oss << "\"num_samples\":" << m_currentWindowSamples.size() << ",";
    
    // Aggregate PSD data by node and frequency
    // Format: { nodeId: { freqBin: [psd_values] } }
    std::map<uint32_t, std::map<double, std::vector<double>>> aggregatedPsd;
    
    for (const auto& sample : m_currentWindowSamples)
    {
        aggregatedPsd[sample.nodeId][sample.frequencyHz].push_back(sample.psdWattsPerHz);
    }
    
    oss << "\"psd_data\":{";
    bool firstNode = true;
    for (const auto& [nodeId, freqMap] : aggregatedPsd)
    {
        if (!firstNode) oss << ",";
        firstNode = false;
        
        oss << "\"node_" << nodeId << "\":{";
        oss << "\"frequencies\":[";
        bool firstFreq = true;
        for (const auto& [freq, _] : freqMap)
        {
            if (!firstFreq) oss << ",";
            firstFreq = false;
            oss << freq;
        }
        oss << "],";
        
        // Average PSD for each frequency bin
        oss << "\"psd_avg_dbm\":[";
        firstFreq = true;
        for (const auto& [freq, psdValues] : freqMap)
        {
            if (!firstFreq) oss << ",";
            firstFreq = false;
            
            // Calculate average and convert to dBm/Hz
            double sum = 0;
            for (double v : psdValues) sum += v;
            double avgWatts = sum / psdValues.size();
            double dbm = (avgWatts > 1e-20) ? 10.0 * std::log10(avgWatts * 1000.0) : -200.0;
            oss << std::setprecision(2) << dbm;
        }
        oss << "]}";
    }
    oss << "}";
    
    oss << "}";
    
    return oss.str();
}

void
SpectrumKafkaProducer::PublishToKafka(const std::string& json)
{
    NS_LOG_FUNCTION(this);

    if (!m_producer)
    {
        NS_LOG_WARN("Kafka producer not initialized");
        return;
    }

    // Message key: simulation_id + window_index
    std::string key = m_config.simulationId + "_" + std::to_string(m_windowIndex);

    try
    {
        // Create producer record with topic, key, and value
        kafka::clients::producer::ProducerRecord record(
            m_config.topic,
            kafka::Key(key.c_str(), key.size()),
            kafka::Value(json.c_str(), json.size())
        );

        // Async send with delivery callback - non-blocking
        m_producer->send(record,
            [](const kafka::clients::producer::RecordMetadata& metadata,
               const kafka::Error& error) {
                if (error)
                {
                    NS_LOG_ERROR("Failed to deliver spectrum message: " << error.message());
                }
            },
            kafka::clients::producer::KafkaProducer::SendOption::ToCopyRecordValue
        );
    }
    catch (const kafka::KafkaException& e)
    {
        NS_LOG_ERROR("Failed to produce message: " << e.what());
    }
}

} // namespace ns3

