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

#ifndef SPECTRUM_PIPE_STREAMER_H
#define SPECTRUM_PIPE_STREAMER_H

#include "ns3/object.h"
#include "ns3/spectrum-value.h"
#include "ns3/nstime.h"
#include "ns3/node.h"
#include <string>

namespace ns3 {

/**
 * \ingroup spectrum-pipe-streamer
 * \brief Named pipe (FIFO) streamer for spectrum analyzer PSD data
 *
 * This class handles streaming of Power Spectral Density (PSD) data from
 * spectrum analyzers to external processes via named pipes (FIFOs). Named
 * pipes provide blocking behavior - when the pipe buffer is full, the program
 * will block until the reader consumes data.
 *
 * Two modes are supported:
 * - Per-node pipes: Each node writes to its own named pipe file (node-0.pipe, node-1.pipe, ...)
 * - Shared pipe: All nodes write to a single pipe file (all-nodes.pipe)
 *
 * In both modes, each message includes the node_id so readers can identify the source.
 */
class SpectrumPipeStreamer : public Object
{
public:
  /**
   * \brief Enable shared pipe mode (all nodes write to single pipe)
   * \param pipePath Full path to the shared pipe file
   *
   * Call this before installing any streamers. In shared mode, all nodes
   * write to the same pipe, and the node_id in each message identifies the source.
   */
  static void EnableSharedPipe(const std::string& pipePath);

  /**
   * \brief Disable shared pipe mode (revert to per-node pipes)
   */
  static void DisableSharedPipe();

  /**
   * \brief Check if shared pipe mode is enabled
   * \return true if shared pipe mode is active
   */
  static bool IsSharedPipeEnabled();

  /**
   * \brief Get the shared pipe path
   * \return Path to shared pipe, or empty if not enabled
   */
  static std::string GetSharedPipePath();

  /**
   * \brief Close the shared pipe (call at end of simulation)
   */
  static void CloseSharedPipe();
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * Constructor
   */
  SpectrumPipeStreamer ();

  /**
   * Destructor
   */
  virtual ~SpectrumPipeStreamer ();

  /**
   * \brief Set the base path for named pipes
   * \param basePath Base directory path where pipes will be created (e.g., "/tmp/ns3-spectrum")
   */
  void SetBasePath (const std::string& basePath);

  /**
   * \brief Set the streaming interval
   * \param interval Time interval between consecutive sends
   */
  void SetStreamInterval (Time interval);

  /**
   * \brief Get the streaming interval
   * \return Current streaming interval
   */
  Time GetStreamInterval (void) const;

  /**
   * \brief Set the node/AP identifier
   * \param nodeId Identifier for the node/AP
   */
  void SetNodeId (uint32_t nodeId);

  /**
   * \brief Set the node pointer (required for context)
   * \param node The node this streamer is attached to
   */
  void SetNode (Ptr<Node> node);

  /**
   * \brief Set the MAC address for this node
   * \param mac MAC address string (e.g., "00:00:00:00:00:01")
   */
  void SetMacAddress (const std::string& mac);

  /**
   * \brief Set frequency range metadata
   * \param startFreq Starting frequency (Hz)
   * \param resolution Frequency resolution (Hz)
   * \param numBins Number of frequency bins
   */
  void SetFrequencyMetadata (double startFreq, double resolution, uint32_t numBins);

  /**
   * \brief Get the full path to the named pipe for this node
   * \return Full path to the FIFO file
   */
  std::string GetPipePath (void) const;

  /**
   * \brief Callback function for PSD data
   * \param psd Power Spectral Density values
   *
   * This callback is invoked when new PSD data is available from the spectrum analyzer.
   * It streams the data to the named pipe. If the pipe is full, this will block until
   * space becomes available.
   */
  void PsdCallback (Ptr<const SpectrumValue> psd);

  /**
   * \brief Initialize named pipe (FIFO)
   *
   * Creates the FIFO file and opens it for writing. This will block until
   * a reader opens the pipe for reading.
   */
  void InitializePipe (void);

private:

  /**
   * \brief Serialize PSD data to binary
   * \param timestamp Current simulation time
   * \param psd Power Spectral Density values
   * \return binary string
   */
  std::string SerializeToBinary (double timestamp, Ptr<const SpectrumValue> psd);

  /**
   * \brief Send data via named pipe
   * \param data binary string to send
   * 
   * This will block if the pipe buffer is full until the reader consumes data.
   */
  void SendData (const std::string& data);

  Time m_streamInterval;           ///< Interval between consecutive sends
  Time m_lastSendTime;             ///< Time of last send
  uint32_t m_nodeId;               ///< Node/AP identifier
  Ptr<Node> m_node;                ///< Node pointer (for context)
  std::string m_macAddress;        ///< MAC address

  // Frequency metadata
  double m_startFreq;              ///< Starting frequency (Hz)
  double m_freqResolution;         ///< Frequency resolution (Hz)
  uint32_t m_numFreqBins;          ///< Number of frequency bins
  bool m_metadataSet;              ///< Whether metadata has been set

  // Named pipe
  int m_pipeFd;                    ///< Named pipe file descriptor
  std::string m_basePath;          ///< Base path for pipe files
  std::string m_pipePath;          ///< Full path to this node's pipe
  bool m_pipeInitialized;          ///< Whether pipe has been initialized

  // Statistics
  uint64_t m_packetsSent;          ///< Number of packets sent
  uint64_t m_bytesSent;            ///< Total bytes sent

  // Static members for shared pipe mode
  static bool s_sharedPipeEnabled;       ///< Whether shared pipe mode is enabled
  static std::string s_sharedPipePath;   ///< Path to shared pipe
  static int s_sharedPipeFd;             ///< Shared pipe file descriptor
  static bool s_sharedPipeInitialized;   ///< Whether shared pipe has been opened
};

} // namespace ns3

#endif /* SPECTRUM_PIPE_STREAMER_H */
