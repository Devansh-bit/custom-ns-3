/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Spectrum Kafka Producer - Publishes PSD data to Kafka every 100ms window
 */

#ifndef SPECTRUM_KAFKA_PRODUCER_H
#define SPECTRUM_KAFKA_PRODUCER_H

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include "ns3/spectrum-value.h"
#include "ns3/node-container.h"
#include "ns3/multi-model-spectrum-channel.h"

#include <kafka/KafkaProducer.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <mutex>

namespace ns3 {

/**
 * \brief PSD sample for a single frequency bin at a moment in time
 */
struct PsdSample
{
    uint32_t nodeId;
    double timestampSec;
    double frequencyHz;
    double psdWattsPerHz;
};

/**
 * \brief Configuration for spectrum Kafka producer
 */
struct SpectrumKafkaConfig
{
    std::string brokers = "localhost:9092";
    std::string topic = "spectrum-data";
    std::string simulationId = "sim-001";
    Time windowDuration = MilliSeconds(100);      ///< 100ms windows
    Time sampleInterval = MilliSeconds(10);       ///< Sample every 10ms within window
    double startFrequency = 2.4e9;                ///< 2.4 GHz
    double frequencyResolution = 100e3;           ///< 100 kHz bins
    uint32_t numFrequencyBins = 1000;
    std::string bandId = "2.4GHz";
};

/**
 * \ingroup spectrum-shadow
 * \brief Kafka producer that publishes PSD spectrum data every 100ms window
 *
 * This replaces the file-based cnn_X.tr approach with real-time Kafka streaming.
 * Every 100ms, it collects PSD samples and publishes them as a JSON message.
 */
class SpectrumKafkaProducer : public Object
{
public:
    static TypeId GetTypeId();
    
    SpectrumKafkaProducer();
    ~SpectrumKafkaProducer() override;
    
    /**
     * \brief Set the configuration
     * \param config Spectrum Kafka configuration
     */
    void SetConfig(const SpectrumKafkaConfig& config);
    
    /**
     * \brief Initialize Kafka producer
     * \return true if successful
     */
    bool Initialize();
    
    /**
     * \brief Start producing (schedules first window)
     */
    void Start();
    
    /**
     * \brief Stop producing
     */
    void Stop();
    
    /**
     * \brief Receive a PSD sample from spectrum analyzer
     * \param nodeId Source node ID
     * \param psd PSD spectrum value
     */
    void ReceivePsd(uint32_t nodeId, Ptr<const SpectrumValue> psd);
    
    /**
     * \brief Get window index
     * \return Current window index
     */
    uint32_t GetWindowIndex() const { return m_windowIndex; }

private:
    /**
     * \brief Start a new 100ms window
     */
    void StartWindow();
    
    /**
     * \brief End current window and publish to Kafka
     */
    void EndWindow();
    
    /**
     * \brief Serialize current window samples to JSON
     * \return JSON string
     */
    std::string SerializeWindowToJson();
    
    /**
     * \brief Publish JSON message to Kafka
     * \param json JSON string to publish
     */
    void PublishToKafka(const std::string& json);
    
    SpectrumKafkaConfig m_config;

    // Kafka (modern-cpp-kafka)
    std::unique_ptr<kafka::clients::producer::KafkaProducer> m_producer;
    bool m_initialized;
    
    // Window management
    uint32_t m_windowIndex;
    Time m_windowStartTime;
    std::vector<PsdSample> m_currentWindowSamples;
    std::mutex m_sampleMutex;
    
    // Events
    EventId m_windowEndEvent;
    bool m_running;
};

} // namespace ns3

#endif /* SPECTRUM_KAFKA_PRODUCER_H */

