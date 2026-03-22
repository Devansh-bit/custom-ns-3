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

#ifndef SPECTRUM_PIPE_STREAMER_HELPER_H
#define SPECTRUM_PIPE_STREAMER_HELPER_H

#include "ns3/spectrum-pipe-streamer.h"
#include "ns3/spectrum-channel.h"
#include "ns3/node-container.h"
#include "ns3/net-device-container.h"
#include "ns3/spectrum-analyzer-helper.h"
#include <string>
#include <map>

namespace ns3 {

/**
 * \ingroup spectrum-pipe-streamer
 * \brief Helper to install spectrum analyzer pipe streamers on nodes
 *
 * This helper provides a plug-and-play API to install spectrum analyzers
 * with named pipe streaming on any node/AP in a simulation. Each node
 * writes to its own named pipe file. Named pipes provide blocking behavior:
 * when the pipe buffer is full, the simulation will block until the reader
 * consumes data.
 *
 * Example usage:
 * \code
 *   SpectrumPipeStreamerHelper helper;
 *   helper.SetBasePath ("/tmp/ns3-spectrum");
 *   helper.SetFrequencyRange (2.4e9, 100e3, 1000);
 *   helper.SetNoiseFloor (-140.0);
 *   helper.SetStreamInterval (MilliSeconds(100));
 *   helper.InstallOnNodes (apNodes, channel);
 * \endcode
 */
class SpectrumPipeStreamerHelper
{
public:
  /**
   * Constructor
   */
  SpectrumPipeStreamerHelper ();

  /**
   * Destructor
   */
  ~SpectrumPipeStreamerHelper ();

  /**
   * \brief Set the base path for named pipes
   * \param basePath Base directory path (e.g., "/tmp/ns3-spectrum")
   *
   * Each node will create a pipe at basePath/node-<id>.pipe
   */
  void SetBasePath (const std::string& basePath);

  /**
   * \brief Enable shared pipe mode (all nodes write to single pipe)
   * \param pipePath Full path to the shared pipe file (e.g., "/tmp/ns3-spectrum/spectrum.pipe")
   *
   * In shared mode, all spectrum analyzers write to a single pipe.
   * The node_id in each message identifies which node the data is from.
   * This is cleaner than having multiple per-node pipes.
   */
  void EnableSharedPipe(const std::string& pipePath);

  /**
   * \brief Disable shared pipe mode (use per-node pipes)
   */
  void DisableSharedPipe();

  /**
   * \brief Check if shared pipe mode is enabled
   * \return true if shared pipe mode is active
   */
  bool IsSharedPipeEnabled() const;

  /**
   * \brief Get the shared pipe path
   * \return Path to the shared pipe, or empty if not in shared mode
   */
  std::string GetSharedPipePath() const;

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
   * \brief Set the streaming interval
   * \param interval Time interval between consecutive pipe writes
   */
  void SetStreamInterval (Time interval);

  /**
   * \brief Set the spectrum analyzer resolution (sampling interval)
   * \param resolution Time resolution for spectrum analyzer
   */
  void SetAnalyzerResolution (Time resolution);

  /**
   * \brief Install spectrum analyzer streamer on a single node
   * \param node The node/AP to install on
   * \return The installed net device
   */
  Ptr<NetDevice> InstallOnNode (Ptr<Node> node);

  /**
   * \brief Install spectrum analyzer streamers on multiple nodes
   * \param nodes Container of nodes/APs to install on
   * \return Container of installed net devices
   */
  NetDeviceContainer InstallOnNodes (NodeContainer nodes);

  /**
   * \brief Get the streamer for a specific node
   * \param nodeId Node ID
   * \return Pointer to the streamer, or nullptr if not found
   */
  Ptr<SpectrumPipeStreamer> GetStreamer (uint32_t nodeId) const;

  /**
   * \brief Get all installed streamers
   * \return Map of node IDs to streamers
   */
  std::map<uint32_t, Ptr<SpectrumPipeStreamer>> GetAllStreamers () const;

  /**
   * \brief Get the pipe path for a specific node
   * \param nodeId Node ID
   * \return Full path to the pipe, or empty string if not found
   */
  std::string GetPipePath (uint32_t nodeId) const;

private:
  std::string m_basePath;                       ///< Base path for pipe files
  Ptr<SpectrumChannel> m_channel;               ///< Spectrum channel to monitor
  double m_startFreq;                           ///< Starting frequency (Hz)
  double m_freqResolution;                      ///< Frequency resolution (Hz)
  uint32_t m_numFreqBins;                       ///< Number of frequency bins
  double m_noisePowerDbm;                       ///< Noise floor (dBm/Hz)
  Time m_streamInterval;                        ///< Streaming interval
  Time m_analyzerResolution;                    ///< Spectrum analyzer resolution
  std::map<uint32_t, Ptr<SpectrumPipeStreamer>> m_streamers;  ///< Map of streamers per node

  /**
   * \brief Create the spectrum model based on configured parameters
   * \return The spectrum model
   */
  Ptr<SpectrumModel> CreateSpectrumModel () const;

  /**
   * \brief Configure and install spectrum analyzer on a node
   * \param node The node to install on
   * \param streamer The streamer to attach
   * \return The installed net device
   */
  Ptr<NetDevice> InstallAnalyzer (Ptr<Node> node, Ptr<SpectrumPipeStreamer> streamer);

  /**
   * \brief Get MAC address for a node
   * \param node The node
   * \return MAC address string
   */
  std::string GetNodeMacAddress (Ptr<Node> node) const;
};

} // namespace ns3

#endif /* SPECTRUM_PIPE_STREAMER_HELPER_H */
