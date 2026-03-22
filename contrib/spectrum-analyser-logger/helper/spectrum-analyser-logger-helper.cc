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

#include "spectrum-analyser-logger-helper.h"
#include "ns3/log.h"
#include "ns3/spectrum-analyzer-helper.h"
#include "ns3/spectrum-analyzer.h"
#include "ns3/spectrum-model.h"
#include "ns3/double.h"
#include "ns3/config.h"
#include "ns3/names.h"
#include "ns3/non-communicating-net-device.h"
#include <cmath>
#include <sstream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SpectrumAnalyserLoggerHelper");

SpectrumAnalyserLoggerHelper::SpectrumAnalyserLoggerHelper ()
  : m_channel (nullptr),
    m_startFreq (2.4e9),           // Default: 2.4 GHz
    m_freqResolution (100e3),      // Default: 100 kHz
    m_numFreqBins (1000),          // Default: 1000 bins (100 MHz span)
    m_noisePowerDbm (-140.0),      // Default: -140 dBm/Hz
    m_loggingInterval (MilliSeconds (100)),  // Default: 100 ms
    m_analyzerResolution (MilliSeconds (1)), // Default: 1 ms
    m_consoleOutput (false)
{
  NS_LOG_FUNCTION (this);
}

SpectrumAnalyserLoggerHelper::~SpectrumAnalyserLoggerHelper ()
{
  NS_LOG_FUNCTION (this);
}

void
SpectrumAnalyserLoggerHelper::SetChannel (Ptr<SpectrumChannel> channel)
{
  NS_LOG_FUNCTION (this << channel);
  m_channel = channel;
}

void
SpectrumAnalyserLoggerHelper::SetFrequencyRange (double startFreq, double resolution, uint32_t numBins)
{
  NS_LOG_FUNCTION (this << startFreq << resolution << numBins);
  m_startFreq = startFreq;
  m_freqResolution = resolution;
  m_numFreqBins = numBins;
}

void
SpectrumAnalyserLoggerHelper::SetNoiseFloor (double noisePowerDbm)
{
  NS_LOG_FUNCTION (this << noisePowerDbm);
  m_noisePowerDbm = noisePowerDbm;
}

void
SpectrumAnalyserLoggerHelper::SetLoggingInterval (Time interval)
{
  NS_LOG_FUNCTION (this << interval);
  m_loggingInterval = interval;
}

void
SpectrumAnalyserLoggerHelper::SetAnalyzerResolution (Time resolution)
{
  NS_LOG_FUNCTION (this << resolution);
  m_analyzerResolution = resolution;
}

void
SpectrumAnalyserLoggerHelper::EnableConsoleOutput (bool enable)
{
  NS_LOG_FUNCTION (this << enable);
  m_consoleOutput = enable;
}

void
SpectrumAnalyserLoggerHelper::EnableSharedFile(const std::string& bandId, const std::string& filePath)
{
  NS_LOG_FUNCTION(this << bandId << filePath);
  m_bandId = bandId;
  SpectrumAnalyserLogger::EnableSharedFile(bandId, filePath);
  NS_LOG_INFO("Enabled shared .tr file mode for band '" << bandId << "': " << filePath);
}

void
SpectrumAnalyserLoggerHelper::EnableSharedFile(const std::string& filePath)
{
  NS_LOG_FUNCTION(this << filePath);
  EnableSharedFile("default", filePath);
}

void
SpectrumAnalyserLoggerHelper::SetBandId(const std::string& bandId)
{
  NS_LOG_FUNCTION(this << bandId);
  m_bandId = bandId;
}

void
SpectrumAnalyserLoggerHelper::DisableSharedFile()
{
  NS_LOG_FUNCTION(this);
  std::string bandId = m_bandId.empty() ? "default" : m_bandId;
  SpectrumAnalyserLogger::DisableSharedFile(bandId);
  NS_LOG_INFO("Disabled shared .tr file mode for band '" << bandId << "'");
}

bool
SpectrumAnalyserLoggerHelper::IsSharedFileEnabled() const
{
  std::string bandId = m_bandId.empty() ? "default" : m_bandId;
  return SpectrumAnalyserLogger::IsSharedFileEnabled(bandId);
}

std::string
SpectrumAnalyserLoggerHelper::GetSharedFilePath() const
{
  std::string bandId = m_bandId.empty() ? "default" : m_bandId;
  return SpectrumAnalyserLogger::GetSharedFilePath(bandId);
}

void
SpectrumAnalyserLoggerHelper::CloseSharedFile()
{
  NS_LOG_FUNCTION(this);
  std::string bandId = m_bandId.empty() ? "default" : m_bandId;
  SpectrumAnalyserLogger::CloseSharedFile(bandId);
}

void
SpectrumAnalyserLoggerHelper::CloseSharedFileWithTimestamp(double finalTimestamp)
{
  NS_LOG_FUNCTION(this << finalTimestamp);
  std::string bandId = m_bandId.empty() ? "default" : m_bandId;
  SpectrumAnalyserLogger::CloseSharedFileWithTimestamp(bandId, finalTimestamp);
}

Ptr<SpectrumModel>
SpectrumAnalyserLoggerHelper::CreateSpectrumModel () const
{
  NS_LOG_FUNCTION (this);

  // Create frequency bands
  std::vector<double> freqs;
  freqs.reserve (m_numFreqBins + 1);
  for (uint32_t i = 0; i <= m_numFreqBins; ++i)
    {
      freqs.push_back (m_startFreq + i * m_freqResolution);
    }

  Ptr<SpectrumModel> spectrumModel = Create<SpectrumModel> (freqs);

  NS_LOG_INFO ("Created spectrum model: "
               << "Start=" << m_startFreq / 1e9 << " GHz, "
               << "Resolution=" << m_freqResolution / 1e3 << " kHz, "
               << "Bins=" << m_numFreqBins);

  return spectrumModel;
}

Ptr<NetDevice>
SpectrumAnalyserLoggerHelper::InstallAnalyzer (Ptr<Node> node,
                                                Ptr<SpectrumAnalyserLogger> logger,
                                                const std::string& outputFile)
{
  NS_LOG_FUNCTION (this << node << logger << outputFile);

  if (!m_channel)
    {
      NS_FATAL_ERROR ("Spectrum channel not set. Call SetChannel() before installing.");
    }

  // Create spectrum model
  Ptr<SpectrumModel> spectrumModel = CreateSpectrumModel ();

  // Configure spectrum analyzer using SpectrumAnalyzerHelper
  SpectrumAnalyzerHelper analyzerHelper;
  analyzerHelper.SetChannel (m_channel);
  analyzerHelper.SetRxSpectrumModel (spectrumModel);
  analyzerHelper.SetPhyAttribute ("Resolution", TimeValue (m_analyzerResolution));

  // Set noise floor
  double noisePowerWatts = std::pow (10.0, (m_noisePowerDbm - 30.0) / 10.0);
  analyzerHelper.SetPhyAttribute ("NoisePowerSpectralDensity", DoubleValue (noisePowerWatts));

  // Install spectrum analyzer on the node
  NetDeviceContainer devices = analyzerHelper.Install (node);
  Ptr<NetDevice> device = devices.Get (0);

  NS_LOG_INFO ("Installed spectrum analyzer on node " << node->GetId ());

  // Configure logger and output file (skip if shared file mode is enabled)
  if (!SpectrumAnalyserLogger::IsSharedFileEnabled ())
    {
      logger->SetOutputFile (outputFile);
    }

  // Connect the trace source to the logger's callback
  uint32_t nodeId = node->GetId ();
  // Find the device ID in the node's device list
  uint32_t devId = 0;
  for (uint32_t i = 0; i < node->GetNDevices(); i++) {
      if (node->GetDevice(i) == device) {
          devId = i;
          break;
      }
  }

  std::ostringstream oss;
  oss << "/NodeList/" << nodeId
      << "/DeviceList/" << devId
      << "/$ns3::NonCommunicatingNetDevice/Phy/AveragePowerSpectralDensityReport";

  Config::ConnectWithoutContext (
    oss.str (),
    MakeCallback (&SpectrumAnalyserLogger::PsdCallback, logger)
  );

  NS_LOG_INFO ("Connected trace callback for node " << nodeId << " at path: " << oss.str ());

  // Start the spectrum analyzer to begin generating reports
  Ptr<NonCommunicatingNetDevice> nonCommDev = DynamicCast<NonCommunicatingNetDevice> (device);
  if (nonCommDev)
    {
      Ptr<SpectrumAnalyzer> phy = DynamicCast<SpectrumAnalyzer> (nonCommDev->GetPhy ());
      if (phy)
        {
          phy->Start ();
          NS_LOG_INFO ("Started spectrum analyzer on node " << nodeId);
        }
      else
        {
          NS_LOG_WARN ("Failed to get SpectrumAnalyzer PHY for node " << nodeId);
        }
    }
  else
    {
      NS_LOG_WARN ("Failed to cast device to NonCommunicatingNetDevice for node " << nodeId);
    }

  return device;
}

Ptr<NetDevice>
SpectrumAnalyserLoggerHelper::InstallOnNode (Ptr<Node> node, const std::string& outputFilePrefix)
{
  NS_LOG_FUNCTION (this << node << outputFilePrefix);

  uint32_t nodeId = node->GetId ();

  // Check if already installed on this node
  if (m_loggers.find (nodeId) != m_loggers.end ())
    {
      NS_LOG_WARN ("Spectrum analyzer logger already installed on node " << nodeId);
      return nullptr;
    }

  // Create logger for this node
  Ptr<SpectrumAnalyserLogger> logger = CreateObject<SpectrumAnalyserLogger> ();
  logger->SetNodeId (nodeId);
  logger->SetLoggingInterval (m_loggingInterval);
  logger->SetConsoleOutput (m_consoleOutput);

  // Set band ID for shared file selection
  if (!m_bandId.empty())
    {
      logger->SetBandId(m_bandId);
    }

  // Generate output filename
  std::ostringstream oss;
  oss << outputFilePrefix << "-node" << nodeId << ".tr";
  std::string outputFile = oss.str ();

  // Install analyzer
  Ptr<NetDevice> device = InstallAnalyzer (node, logger, outputFile);

  // Store logger
  m_loggers[nodeId] = logger;

  NS_LOG_INFO ("Installed spectrum analyzer logger on node " << nodeId
               << ", output file: " << outputFile);

  return device;
}

NetDeviceContainer
SpectrumAnalyserLoggerHelper::InstallOnNodes (NodeContainer nodes, const std::string& outputFilePrefix)
{
  NS_LOG_FUNCTION (this << &nodes << outputFilePrefix);

  NetDeviceContainer devices;

  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<Node> node = nodes.Get (i);
      Ptr<NetDevice> device = InstallOnNode (node, outputFilePrefix);
      if (device)
        {
          devices.Add (device);
        }
    }

  NS_LOG_INFO ("Installed spectrum analyzer loggers on " << devices.GetN () << " nodes");

  return devices;
}

Ptr<SpectrumAnalyserLogger>
SpectrumAnalyserLoggerHelper::GetLogger (uint32_t nodeId) const
{
  NS_LOG_FUNCTION (this << nodeId);

  auto it = m_loggers.find (nodeId);
  if (it != m_loggers.end ())
    {
      return it->second;
    }
  return nullptr;
}

std::map<uint32_t, Ptr<SpectrumAnalyserLogger>>
SpectrumAnalyserLoggerHelper::GetAllLoggers () const
{
  return m_loggers;
}

} // namespace ns3
