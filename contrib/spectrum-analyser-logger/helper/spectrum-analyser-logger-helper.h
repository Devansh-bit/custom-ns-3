/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
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

#ifndef SPECTRUM_ANALYSER_LOGGER_HELPER_H
#define SPECTRUM_ANALYSER_LOGGER_HELPER_H

#include "ns3/spectrum-analyser-logger.h"
#include "ns3/spectrum-channel.h"
#include "ns3/node-container.h"
#include "ns3/net-device-container.h"
#include "ns3/spectrum-analyzer-helper.h"
#include <string>
#include <map>

namespace ns3 {

/**
 * \ingroup spectrum-analyser-logger
 * \brief Helper to install spectrum analyzer loggers on nodes
 *
 * This helper provides a plug-and-play API to install spectrum analyzers
 * on any node/AP in a simulation. It handles spectrum analyzer creation,
 * configuration, and automatic PSD data logging.
 *
 * Example usage:
 * \code
 *   SpectrumAnalyserLoggerHelper helper;
 *   helper.SetChannel (channel);
 *   helper.SetFrequencyRange (2.4e9, 100e3, 1000);
 *   helper.SetNoiseFloor (-140.0);
 *   helper.SetLoggingInterval (MilliSeconds(100));
 *   helper.InstallOnNodes (apNodes, "output-prefix");
 * \endcode
 */
class SpectrumAnalyserLoggerHelper
{
public:
  /**
   * Constructor
   */
  SpectrumAnalyserLoggerHelper ();

  /**
   * Destructor
   */
  ~SpectrumAnalyserLoggerHelper ();

  /**
   * \brief Set the spectrum channel to monitor
   * \param channel The spectrum channel
   */
  void SetChannel (Ptr<SpectrumChannel> channel);

  /**
   * \brief Set the frequency range for spectrum analysis
   * \param startFreq Starting frequency (Hz)
   * \param resolution Frequency resolution (Hz)
   * \param numBins Number of frequency bins
   */
  void SetFrequencyRange (double startFreq, double resolution, uint32_t numBins);

  /**
   * \brief Set the noise floor
   * \param noisePowerDbm Noise power in dBm/Hz
   */
  void SetNoiseFloor (double noisePowerDbm);

  /**
   * \brief Set the logging interval
   * \param interval Time interval between consecutive logs
   */
  void SetLoggingInterval (Time interval);

  /**
   * \brief Set the spectrum analyzer resolution (sampling interval)
   * \param resolution Time resolution for spectrum analyzer
   */
  void SetAnalyzerResolution (Time resolution);

  /**
   * \brief Enable console output for all installed loggers
   * \param enable True to enable console output
   */
  void EnableConsoleOutput (bool enable);

  /**
   * \brief Enable shared .tr file mode for a specific band
   * \param bandId Band identifier (e.g., "2.4GHz", "5GHz")
   * \param filePath Path to the shared .tr file
   */
  void EnableSharedFile(const std::string& bandId, const std::string& filePath);

  /**
   * \brief Legacy: Enable shared .tr file mode (uses "default" band)
   * \param filePath Path to the shared .tr file
   */
  void EnableSharedFile(const std::string& filePath);

  /**
   * \brief Set the band ID for this helper (loggers will use this band)
   * \param bandId Band identifier
   */
  void SetBandId(const std::string& bandId);

  /**
   * \brief Disable shared file mode for this helper's band
   */
  void DisableSharedFile();

  /**
   * \brief Check if shared file mode is enabled
   * \return true if shared file mode is enabled
   */
  bool IsSharedFileEnabled() const;

  /**
   * \brief Get the shared file path
   * \return Path to the shared file
   */
  std::string GetSharedFilePath() const;

  /**
   * \brief Close the shared file (call at end of simulation)
   */
  void CloseSharedFile();

  /**
   * \brief Close the shared file with final timestamp marker
   * \param finalTimestamp The exact simulation time when stopping
   *
   * Writes an END marker with the exact stop timestamp before closing.
   * This ensures the .tr file records the precise stop time for synchronization.
   */
  void CloseSharedFileWithTimestamp(double finalTimestamp);

  /**
   * \brief Install spectrum analyzer logger on a single node
   * \param node The node/AP to install on
   * \param outputFilePrefix Prefix for output file (will append node ID)
   * \return The installed net device
   */
  Ptr<NetDevice> InstallOnNode (Ptr<Node> node, const std::string& outputFilePrefix);

  /**
   * \brief Install spectrum analyzer loggers on multiple nodes
   * \param nodes Container of nodes/APs to install on
   * \param outputFilePrefix Prefix for output files (will append node IDs)
   * \return Container of installed net devices
   */
  NetDeviceContainer InstallOnNodes (NodeContainer nodes, const std::string& outputFilePrefix);

  /**
   * \brief Get the logger for a specific node
   * \param nodeId Node ID
   * \return Pointer to the logger, or nullptr if not found
   */
  Ptr<SpectrumAnalyserLogger> GetLogger (uint32_t nodeId) const;

  /**
   * \brief Get all installed loggers
   * \return Map of node IDs to loggers
   */
  std::map<uint32_t, Ptr<SpectrumAnalyserLogger>> GetAllLoggers () const;

private:
  Ptr<SpectrumChannel> m_channel;               ///< Spectrum channel to monitor
  double m_startFreq;                           ///< Starting frequency (Hz)
  double m_freqResolution;                      ///< Frequency resolution (Hz)
  uint32_t m_numFreqBins;                       ///< Number of frequency bins
  double m_noisePowerDbm;                       ///< Noise floor (dBm/Hz)
  Time m_loggingInterval;                       ///< Logging interval
  Time m_analyzerResolution;                    ///< Spectrum analyzer resolution
  bool m_consoleOutput;                         ///< Console output flag
  std::string m_bandId;                         ///< Band identifier for shared file selection
  std::map<uint32_t, Ptr<SpectrumAnalyserLogger>> m_loggers;  ///< Map of loggers per node

  /**
   * \brief Create the spectrum model based on configured parameters
   * \return The spectrum model
   */
  Ptr<SpectrumModel> CreateSpectrumModel () const;

  /**
   * \brief Configure and install spectrum analyzer on a node
   * \param node The node to install on
   * \param logger The logger to attach
   * \param outputFile Output file name
   * \return The installed net device
   */
  Ptr<NetDevice> InstallAnalyzer (Ptr<Node> node,
                                  Ptr<SpectrumAnalyserLogger> logger,
                                  const std::string& outputFile);
};

} // namespace ns3

#endif /* SPECTRUM_ANALYSER_LOGGER_HELPER_H */
