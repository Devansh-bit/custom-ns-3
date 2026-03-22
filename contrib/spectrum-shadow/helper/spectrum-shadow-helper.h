/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Spectrum Shadow Helper - Orchestrates the spectrum shadow simulation setup
 */

#ifndef SPECTRUM_SHADOW_HELPER_H
#define SPECTRUM_SHADOW_HELPER_H

#include "ns3/spectrum-shadow.h"
#include "ns3/spectrum-channel.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/node-container.h"
#include "ns3/net-device-container.h"
#include "ns3/wifi-helper.h"
#include "ns3/spectrum-wifi-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/propagation-delay-model.h"

// Config parser from waypoint-simulation
#include "ns3/simulation-config-parser.h"
#include "ns3/waypoint-mobility-helper.h"
#include "ns3/waypoint-grid.h"

// Spectrum modules
#include "ns3/spectrogram-generation-helper.h"
#include "ns3/spectrum-analyser-logger-helper.h"
#include "ns3/spectrum-pipe-streamer-helper.h"
#include "spectrum-kafka-producer.h"

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace ns3 {

/**
 * \ingroup spectrum-shadow
 * \brief Helper class to set up and run spectrum shadow simulation
 *
 * This helper provides a high-level API to create a spectrum shadow
 * simulation that mirrors the main config-simulation but focuses
 * solely on spectral data collection.
 *
 * Usage:
 * \code
 *   SpectrumShadowHelper helper;
 *   helper.LoadConfig("config.json");
 *   helper.SetupNodes();
 *   helper.SetupSpectrumChannel();
 *   helper.SetupWifiDevices();
 *   helper.SetupMobility();
 *   helper.SetupInterferers();
 *   helper.SetupSpectrumAnalyzers();
 *   helper.Run();
 * \endcode
 */
class SpectrumShadowHelper
{
public:
    /**
     * \brief Constructor
     */
    SpectrumShadowHelper();

    /**
     * \brief Destructor
     */
    ~SpectrumShadowHelper();

    // ==================== CONFIGURATION ====================

    /**
     * \brief Load configuration from JSON file
     * \param configFile Path to JSON configuration file
     * \return true if successful
     *
     * Uses the same SimulationConfigParser as the main simulation
     * to ensure identical configuration.
     */
    bool LoadConfig(const std::string& configFile);

    /**
     * \brief Set spectrum shadow specific configuration
     * \param config Spectrum shadow configuration
     */
    void SetSpectrumConfig(const SpectrumShadowConfig& config);

    /**
     * \brief Get the loaded simulation config
     * \return Simulation configuration data
     */
    SimulationConfigData GetSimulationConfig() const;

    // ==================== SETUP METHODS ====================

    /**
     * \brief Create all nodes (APs and STAs)
     *
     * Creates NodeContainers for APs and STAs based on loaded config.
     * Must be called after LoadConfig().
     */
    void SetupNodes();

    /**
     * \brief Set up the spectrum channel with propagation models
     *
     * Creates MultiModelSpectrumChannel with same propagation
     * parameters as the main simulation (LogDistance + ConstantSpeed).
     */
    void SetupSpectrumChannel();

    /**
     * \brief Install SpectrumWifiPhy on all nodes
     *
     * Installs WiFi devices using SpectrumWifiPhy on both APs and STAs.
     * Configures channels to match the main simulation.
     */
    void SetupWifiDevices();

    /**
     * \brief Set up mobility for all nodes
     *
     * - APs: ConstantPositionMobility at configured positions
     * - STAs: WaypointMobility with same paths as main simulation
     */
    void SetupMobility();

    /**
     * \brief Create spectrum-based interferers
     *
     * Converts VirtualInterferer configurations from the JSON config
     * to spectrum-based interferers using SpectrogramGenerationHelper.
     */
    void SetupInterferers();

    /**
     * \brief Install spectrum analyzers and streamers
     *
     * Installs SpectrumAnalyserLogger and SpectrumPipeStreamer
     * on AP nodes for PSD data collection and streaming.
     */
    void SetupSpectrumAnalyzers();

    // ==================== EXECUTION ====================

    /**
     * \brief Run the simulation
     * \param duration Simulation duration (0 = use config value)
     */
    void Run(Time duration = Seconds(0));

    /**
     * \brief Export all results (annotations, stats)
     */
    void ExportResults();

    // ==================== ACCESSORS ====================

    /**
     * \brief Get AP nodes
     * \return NodeContainer with all AP nodes
     */
    NodeContainer GetApNodes() const;

    /**
     * \brief Get STA nodes
     * \return NodeContainer with all STA nodes
     */
    NodeContainer GetStaNodes() const;

    /**
     * \brief Get interferer nodes
     * \return NodeContainer with all interferer nodes
     */
    NodeContainer GetInterfererNodes() const;

    /**
     * \brief Get the spectrum channel
     * \return Pointer to spectrum channel
     */
    Ptr<MultiModelSpectrumChannel> GetSpectrumChannel() const;

    /**
     * \brief Get AP devices
     * \return NetDeviceContainer with AP WiFi devices
     */
    NetDeviceContainer GetApDevices() const;

    /**
     * \brief Get STA devices
     * \return NetDeviceContainer with STA WiFi devices
     */
    NetDeviceContainer GetStaDevices() const;

    /**
     * \brief Get the SpectrumShadow manager object
     * \return Pointer to SpectrumShadow
     */
    Ptr<SpectrumShadow> GetSpectrumShadow() const;

    /**
     * \brief Get simulation statistics
     * \return Statistics structure
     */
    SpectrumShadowStats GetStats() const;

    /**
     * \brief Check if a setup step has been completed
     * \param step Step name ("nodes", "channel", "wifi", "mobility", "interferers", "analyzers")
     * \return true if step is complete
     */
    bool IsSetupComplete(const std::string& step) const;

private:
    // Configuration
    SimulationConfigData m_simConfig;        ///< Loaded simulation config
    SpectrumShadowConfig m_spectrumConfig;   ///< Spectrum-specific config
    bool m_configLoaded;                      ///< Config loaded flag

    // Core manager
    Ptr<SpectrumShadow> m_spectrumShadow;    ///< Core manager object

    // Nodes
    NodeContainer m_apNodes;                  ///< AP nodes
    NodeContainer m_staNodes;                 ///< STA nodes
    NodeContainer m_interfererNodes;          ///< Interferer nodes

    // Devices
    NetDeviceContainer m_apDevices;           ///< AP WiFi devices
    NetDeviceContainer m_staDevices;          ///< STA WiFi devices

    // Channel
    Ptr<MultiModelSpectrumChannel> m_channel; ///< Spectrum channel

    // Helpers (kept alive for simulation duration)
    std::unique_ptr<SpectrogramGenerationHelper> m_spectroHelper;  ///< Interferer generator
    SpectrumAnalyserLoggerHelper m_loggerHelper;       ///< PSD logger helper (2.4 GHz)
    SpectrumAnalyserLoggerHelper m_loggerHelper5GHz;   ///< PSD logger helper (5 GHz)
    SpectrumPipeStreamerHelper m_streamerHelper;       ///< Pipe streamer helper
    
    // CNN periodic logging (every 100ms window) - FILE-BASED (legacy)
    uint32_t m_cnnWindowIndex;                         ///< Current CNN window index (1, 2, 3...)
    std::vector<SpectrumAnalyserLoggerHelper> m_cnnLoggerHelpers;  ///< CNN loggers for each window
    
    /**
     * \brief Start a new CNN trace window (file-based)
     * Called every 100ms to create a new cnn_N.tr file
     */
    void StartCnnWindow();
    
    /**
     * \brief Close the current CNN trace window (file-based)
     * \param windowIndex The window index to close
     */
    void CloseCnnWindow(uint32_t windowIndex);
    
    // CNN Kafka streaming (new - replaces file-based)
    Ptr<SpectrumKafkaProducer> m_kafkaProducer;        ///< Kafka producer for spectrum data
    
    /**
     * \brief Setup Kafka-based spectrum streaming
     */
    void SetupKafkaStreaming();

    // Lazy interferers (kept alive for simulation duration)
    std::vector<Ptr<LazyInterferer>> m_lazyInterferers;  ///< Lazy-scheduled interferers

    // Waypoint mobility
    Ptr<WaypointGrid> m_waypointGrid;                  ///< Waypoint grid for STA mobility
    WaypointMobilityHelper m_waypointHelper;           ///< Waypoint mobility helper

    // Setup tracking
    std::map<std::string, bool> m_setupComplete;  ///< Track completed setup steps

    // ==================== PRIVATE HELPERS ====================

    /**
     * \brief Validate that prerequisites are met for a setup step
     * \param step Step name
     * \param prerequisites List of prerequisite steps
     */
    void ValidatePrerequisites(const std::string& step,
                               const std::vector<std::string>& prerequisites) const;

    /**
     * \brief Mark a setup step as complete
     * \param step Step name
     */
    void MarkSetupComplete(const std::string& step);

    /**
     * \brief Create microwave interferers from config
     */
    void CreateMicrowaveInterferers();

    /**
     * \brief Create Bluetooth interferers from config
     */
    void CreateBluetoothInterferers();

    /**
     * \brief Create ZigBee interferers from config
     */
    void CreateZigbeeInterferers();

    /**
     * \brief Create cordless phone interferers from config
     */
    void CreateCordlessInterferers();

    /**
     * \brief Create DFS radar interferers from config
     */
    void CreateRadarInterferers();

    /**
     * \brief Get WiFi band for a channel number
     * \param channel WiFi channel number
     * \return WiFi PHY band
     */
    WifiPhyBand GetBandForChannel(uint8_t channel) const;
};

} // namespace ns3

#endif /* SPECTRUM_SHADOW_HELPER_H */
