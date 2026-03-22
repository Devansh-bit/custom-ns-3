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

#ifndef SPECTRUM_ANALYSER_LOGGER_H
#define SPECTRUM_ANALYSER_LOGGER_H

#include "ns3/object.h"
#include "ns3/spectrum-value.h"
#include "ns3/nstime.h"
#include <string>
#include <fstream>
#include <map>

namespace ns3 {

/**
 * \ingroup spectrum-analyser-logger
 * \brief Logger for spectrum analyzer PSD data
 *
 * This class handles logging of Power Spectral Density (PSD) data from
 * spectrum analyzers. It logs time-stamped PSD values across frequency bands.
 */
class SpectrumAnalyserLogger : public Object
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * Constructor
   */
  SpectrumAnalyserLogger ();

  /**
   * Destructor
   */
  virtual ~SpectrumAnalyserLogger ();

  /**
   * \brief Set the logging interval
   * \param interval Time interval between consecutive logs
   */
  void SetLoggingInterval (Time interval);

  /**
   * \brief Get the logging interval
   * \return Current logging interval
   */
  Time GetLoggingInterval (void) const;

  /**
   * \brief Set the output file for logging
   * \param filename Output file name
   */
  void SetOutputFile (const std::string& filename);

  /**
   * \brief Set the node/AP identifier
   * \param nodeId Identifier for the node/AP this logger is attached to
   */
  void SetNodeId (uint32_t nodeId);

  /**
   * \brief Enable console output
   * \param enable True to enable console output, false to disable
   */
  void SetConsoleOutput (bool enable);

  /**
   * \brief Callback function for PSD data
   * \param psd Power Spectral Density values
   *
   * This callback is invoked when new PSD data is available from the spectrum analyzer.
   * It logs the data based on the configured logging interval.
   */
  void PsdCallback (Ptr<const SpectrumValue> psd);

  // ========== Shared File Mode (Static, Multi-Band) ==========

  /**
   * \brief Enable shared file mode for a specific band
   * \param bandId Band identifier (e.g., "2.4GHz", "5GHz")
   * \param filePath Path to the shared .tr file
   */
  static void EnableSharedFile(const std::string& bandId, const std::string& filePath);

  /**
   * \brief Legacy: Enable shared file mode (uses default band "default")
   * \param filePath Path to the shared .tr file
   */
  static void EnableSharedFile(const std::string& filePath);

  /**
   * \brief Disable shared file mode for a specific band
   * \param bandId Band identifier
   */
  static void DisableSharedFile(const std::string& bandId);

  /**
   * \brief Disable all shared files
   */
  static void DisableSharedFile();

  /**
   * \brief Check if any shared file mode is enabled
   * \return true if any shared file mode is enabled
   */
  static bool IsSharedFileEnabled();

  /**
   * \brief Check if shared file mode is enabled for a specific band
   * \param bandId Band identifier
   * \return true if shared file mode is enabled for this band
   */
  static bool IsSharedFileEnabled(const std::string& bandId);

  /**
   * \brief Get the shared file path for a specific band
   * \param bandId Band identifier
   * \return Path to the shared file
   */
  static std::string GetSharedFilePath(const std::string& bandId);

  /**
   * \brief Legacy: Get the shared file path for default band
   * \return Path to the shared file
   */
  static std::string GetSharedFilePath();

  /**
   * \brief Write END marker with final timestamp to shared file
   * \param bandId Band identifier
   * \param timestamp Final simulation timestamp (seconds)
   *
   * Writes a special END marker line with the exact stop time for synchronization.
   * Format: "# END timestamp"
   */
  static void WriteEndMarker(const std::string& bandId, double timestamp);

  /**
   * \brief Write END marker with final timestamp to all shared files
   * \param timestamp Final simulation timestamp (seconds)
   */
  static void WriteEndMarker(double timestamp);

  /**
   * \brief Close a specific shared file
   * \param bandId Band identifier
   */
  static void CloseSharedFile(const std::string& bandId);

  /**
   * \brief Close all shared files
   */
  static void CloseSharedFile();

  /**
   * \brief Close shared file with final timestamp marker
   * \param bandId Band identifier
   * \param finalTimestamp Write END marker with this timestamp before closing
   */
  static void CloseSharedFileWithTimestamp(const std::string& bandId, double finalTimestamp);

  /**
   * \brief Close all shared files with final timestamp marker
   * \param finalTimestamp Write END marker with this timestamp before closing
   */
  static void CloseAllSharedFilesWithTimestamp(double finalTimestamp);

  /**
   * \brief Set the band this logger belongs to (for shared file selection)
   * \param bandId Band identifier
   */
  void SetBandId(const std::string& bandId);

private:
  Time m_loggingInterval;           ///< Interval between consecutive logs
  Time m_lastLogTime;               ///< Time of last log entry
  std::string m_outputFilename;     ///< Output file name
  std::ofstream m_outputFile;       ///< Output file stream
  uint32_t m_nodeId;                ///< Node/AP identifier
  bool m_consoleOutput;             ///< Flag to enable console output
  bool m_fileOutputEnabled;         ///< Flag to enable file output
  std::string m_bandId;             ///< Band identifier for shared file selection

  /**
   * \brief Write PSD data to output
   * \param timestamp Current simulation time
   * \param psd Power Spectral Density values
   */
  void WritePsdData (double timestamp, Ptr<const SpectrumValue> psd);

  // Static members for shared file mode (multi-band support)
  struct SharedFileInfo {
    std::string filePath;
    std::ofstream* file;
    bool initialized;
  };
  static std::map<std::string, SharedFileInfo> s_sharedFiles;  ///< Band -> shared file info
};

} // namespace ns3

#endif /* SPECTRUM_ANALYSER_LOGGER_H */
