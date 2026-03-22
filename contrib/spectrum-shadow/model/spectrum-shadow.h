/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Spectrum Shadow Simulation Module
 *
 * This module provides a lightweight spectrum-based simulation that mirrors
 * the main config-simulation but focuses solely on PSD (Power Spectral Density)
 * data collection. It runs as a separate process alongside the main simulation.
 *
 * Key features:
 * - Uses SpectrumWifiPhy instead of YansWifiPhy
 * - Replicates exact STA movements via WaypointMobility
 * - Generates spectral data via SpectrumAnalyserLogger
 * - Streams PSD to dashboard via SpectrumPipeStreamer
 * - Converts VirtualInterferers to spectrum-based interferers
 */

#ifndef SPECTRUM_SHADOW_H
#define SPECTRUM_SHADOW_H

#include "ns3/object.h"
#include "ns3/spectrum-channel.h"
#include "ns3/node-container.h"
#include "ns3/net-device-container.h"
#include "ns3/nstime.h"
#include "ns3/spectrogram-generation.h"  // For AnnotationManager

#include <string>
#include <vector>

namespace ns3 {

/**
 * \ingroup spectrum-shadow
 * \brief Configuration for spectrum shadow simulation
 *
 * Holds all configuration parameters needed to set up the spectrum
 * shadow simulation, including frequency ranges, streaming intervals,
 * and output paths.
 */
struct SpectrumShadowConfig
{
    // 2.4 GHz band configuration
    double startFrequency = 2.4e9;      ///< Start frequency (Hz) - default 2.4 GHz
    double frequencyResolution = 100e3;  ///< Frequency resolution (Hz) - default 100 kHz
    uint32_t numFrequencyBins = 1000;    ///< Number of frequency bins (100 MHz span)

    // 5 GHz band configuration (for DFS radar monitoring)
    bool enable5GHz = true;              ///< Enable 5 GHz band monitoring
    double startFrequency5GHz = 5.15e9;  ///< 5 GHz start frequency (Hz)
    double frequencyResolution5GHz = 100e3;  ///< 5 GHz resolution (Hz)
    uint32_t numFrequencyBins5GHz = 7500;   ///< 5 GHz bins (750 MHz span: 5.15-5.9 GHz)

    // Noise configuration
    double noiseFloorDbm = -174.0;       ///< Noise floor (dBm/Hz)

    // Timing configuration
    Time analyzerInterval = MilliSeconds(10);  ///< Spectrum analyzer sampling interval
    Time streamInterval = MilliSeconds(10);    ///< Pipe streaming interval
    Time logInterval = MilliSeconds(100);      ///< File logging interval

    // Output configuration
    std::string pipePath = "/tmp/ns3-spectrum";  ///< Base path for named pipes
    std::string logPath = "./spectrum-logs";      ///< Path for log files
    std::string annotationFile = "spectrum-annotations.json";  ///< Annotation output file

    // Feature flags
    bool enablePipeStreaming = true;    ///< Enable named pipe streaming
    bool enableFileLogging = true;      ///< Enable file-based PSD logging
    bool enableConsoleOutput = false;   ///< Enable console PSD output (debug)
    
    // Kafka streaming (replaces file-based CNN windows)
    bool enableKafkaStreaming = false;  ///< Enable Kafka spectrum streaming
    std::string kafkaBrokers = "localhost:9092";  ///< Kafka broker addresses
    std::string kafkaTopic = "spectrum-data";     ///< Kafka topic for PSD data
    std::string simulationId = "sim-001";         ///< Simulation ID for Kafka messages
};

/**
 * \ingroup spectrum-shadow
 * \brief Statistics collected during spectrum shadow simulation
 */
struct SpectrumShadowStats
{
    uint64_t totalPsdSamples = 0;        ///< Total PSD samples collected
    uint64_t psdPacketsSent = 0;         ///< PSD packets sent via pipes
    uint64_t psdBytesWritten = 0;        ///< Bytes written to log files
    uint32_t interferersActive = 0;      ///< Number of active interferers
    Time simulationDuration;             ///< Total simulation duration
};

/**
 * \ingroup spectrum-shadow
 * \brief Core class for spectrum shadow simulation management
 *
 * This class manages the spectrum shadow simulation infrastructure,
 * including spectrum channel setup, analyzer installation, and
 * data streaming configuration.
 */
class SpectrumShadow : public Object
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
    SpectrumShadow();

    /**
     * \brief Destructor
     */
    virtual ~SpectrumShadow();

    /**
     * \brief Set configuration
     * \param config Configuration parameters
     */
    void SetConfig(const SpectrumShadowConfig& config);

    /**
     * \brief Get current configuration
     * \return Configuration parameters
     */
    SpectrumShadowConfig GetConfig() const;

    /**
     * \brief Set the spectrum channel
     * \param channel The spectrum channel to use
     */
    void SetChannel(Ptr<SpectrumChannel> channel);

    /**
     * \brief Get the spectrum channel
     * \return The spectrum channel
     */
    Ptr<SpectrumChannel> GetChannel() const;

    /**
     * \brief Initialize the spectrum shadow infrastructure
     *
     * This must be called after setting the channel and configuration,
     * but before installing analyzers on nodes.
     */
    void Initialize();

    /**
     * \brief Check if initialized
     * \return true if Initialize() has been called
     */
    bool IsInitialized() const;

    /**
     * \brief Get simulation statistics
     * \return Current statistics
     */
    SpectrumShadowStats GetStats() const;

    /**
     * \brief Get the annotation manager
     * \return Pointer to annotation manager
     */
    Ptr<AnnotationManager> GetAnnotationManager() const;

    /**
     * \brief Export annotations to file
     * \param filename Output filename (empty = use config default)
     */
    void ExportAnnotations(const std::string& filename = "");

protected:
    virtual void DoDispose() override;
    virtual void DoInitialize() override;

private:
    SpectrumShadowConfig m_config;           ///< Configuration
    Ptr<SpectrumChannel> m_channel;          ///< Spectrum channel
    Ptr<AnnotationManager> m_annotationMgr;  ///< Annotation manager
    SpectrumShadowStats m_stats;             ///< Statistics
    bool m_initialized;                       ///< Initialization flag
};

} // namespace ns3

#endif /* SPECTRUM_SHADOW_H */
