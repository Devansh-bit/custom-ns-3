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

#include "spectrum-pipe-streamer.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SpectrumPipeStreamer");

NS_OBJECT_ENSURE_REGISTERED (SpectrumPipeStreamer);

// Initialize static members
bool SpectrumPipeStreamer::s_sharedPipeEnabled = false;
std::string SpectrumPipeStreamer::s_sharedPipePath = "";
int SpectrumPipeStreamer::s_sharedPipeFd = -1;
bool SpectrumPipeStreamer::s_sharedPipeInitialized = false;

void
SpectrumPipeStreamer::EnableSharedPipe(const std::string& pipePath)
{
  NS_LOG_FUNCTION(pipePath);
  s_sharedPipeEnabled = true;
  s_sharedPipePath = pipePath;
  s_sharedPipeFd = -1;
  s_sharedPipeInitialized = false;
  NS_LOG_INFO("Shared pipe mode enabled: " << pipePath);
}

void
SpectrumPipeStreamer::DisableSharedPipe()
{
  NS_LOG_FUNCTION_NOARGS();
  CloseSharedPipe();
  s_sharedPipeEnabled = false;
  s_sharedPipePath = "";
  NS_LOG_INFO("Shared pipe mode disabled");
}

bool
SpectrumPipeStreamer::IsSharedPipeEnabled()
{
  return s_sharedPipeEnabled;
}

std::string
SpectrumPipeStreamer::GetSharedPipePath()
{
  return s_sharedPipePath;
}

void
SpectrumPipeStreamer::CloseSharedPipe()
{
  NS_LOG_FUNCTION_NOARGS();
  if (s_sharedPipeFd >= 0)
    {
      close(s_sharedPipeFd);
      s_sharedPipeFd = -1;
      s_sharedPipeInitialized = false;
      NS_LOG_INFO("Shared pipe closed");
    }
}

TypeId
SpectrumPipeStreamer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::SpectrumPipeStreamer")
    .SetParent<Object> ()
    .SetGroupName ("SpectrumPipeStreamer")
    .AddConstructor<SpectrumPipeStreamer> ()
  ;
  return tid;
}

SpectrumPipeStreamer::SpectrumPipeStreamer ()
  : m_streamInterval (MilliSeconds (100)),  // Default: 100ms
    m_lastSendTime (Seconds (0.0)),
    m_nodeId (0),
    m_node (nullptr),
    m_macAddress ("00:00:00:00:00:00"),
    m_startFreq (0.0),
    m_freqResolution (0.0),
    m_numFreqBins (0),
    m_metadataSet (false),
    m_pipeFd (-1),
    m_basePath ("/tmp/ns3-spectrum"),
    m_pipePath (""),
    m_pipeInitialized (false),
    m_packetsSent (0),
    m_bytesSent (0)
{
  NS_LOG_FUNCTION (this);
}

SpectrumPipeStreamer::~SpectrumPipeStreamer ()
{
  NS_LOG_FUNCTION (this);
  if (m_pipeFd >= 0)
    {
      close (m_pipeFd);
      m_pipeFd = -1;
    }

  NS_LOG_INFO ("Node " << m_nodeId << " statistics: "
               << m_packetsSent << " packets, "
               << m_bytesSent << " bytes sent");
}

void
SpectrumPipeStreamer::SetBasePath (const std::string& basePath)
{
  NS_LOG_FUNCTION (this << basePath);
  m_basePath = basePath;

  std::ostringstream oss;
  oss << m_basePath << "/node-" << m_nodeId << ".pipe";
  m_pipePath = oss.str ();
}

void
SpectrumPipeStreamer::SetStreamInterval (Time interval)
{
  NS_LOG_FUNCTION (this << interval);
  m_streamInterval = interval;
}

Time
SpectrumPipeStreamer::GetStreamInterval (void) const
{
  return m_streamInterval;
}

void
SpectrumPipeStreamer::SetNodeId (uint32_t nodeId)
{
  NS_LOG_FUNCTION (this << nodeId);
  m_nodeId = nodeId;
  
  // Update pipe path when node ID changes
  std::ostringstream oss;
  oss << m_basePath << "/node-" << nodeId << ".pipe";
  m_pipePath = oss.str ();
}

void
SpectrumPipeStreamer::SetNode (Ptr<Node> node)
{
  NS_LOG_FUNCTION (this << node);
  m_node = node;
}

void
SpectrumPipeStreamer::SetMacAddress (const std::string& mac)
{
  NS_LOG_FUNCTION (this << mac);
  m_macAddress = mac;
}

void
SpectrumPipeStreamer::SetFrequencyMetadata (double startFreq, double resolution, uint32_t numBins)
{
  NS_LOG_FUNCTION (this << startFreq << resolution << numBins);
  m_startFreq = startFreq;
  m_freqResolution = resolution;
  m_numFreqBins = numBins;
  m_metadataSet = true;

  NS_LOG_INFO ("Frequency metadata set for node " << m_nodeId << ": "
               << "start=" << startFreq / 1e9 << " GHz, "
               << "resolution=" << resolution / 1e3 << " kHz, "
               << "bins=" << numBins);
}

std::string
SpectrumPipeStreamer::GetPipePath (void) const
{
  return m_pipePath;
}

void
SpectrumPipeStreamer::InitializePipe (void)
{
  NS_LOG_FUNCTION (this);

  // Handle shared pipe mode
  if (s_sharedPipeEnabled)
    {
      if (s_sharedPipeInitialized)
        {
          m_pipeInitialized = true;
          return;
        }

      // Initialize shared pipe (only done once by first streamer)
      std::string pipePath = s_sharedPipePath;

      // Extract base directory from pipe path
      size_t lastSlash = pipePath.rfind('/');
      if (lastSlash != std::string::npos)
        {
          std::string baseDir = pipePath.substr(0, lastSlash);
          struct stat st;
          if (stat(baseDir.c_str(), &st) != 0)
            {
              if (mkdir(baseDir.c_str(), 0755) != 0)
                {
                  NS_LOG_ERROR("Failed to create directory " << baseDir << ": " << std::strerror(errno));
                  return;
                }
              NS_LOG_INFO("Created directory: " << baseDir);
            }
        }

      // Check if pipe exists
      struct stat st;
      if (stat(pipePath.c_str(), &st) != 0)
        {
          if (mkfifo(pipePath.c_str(), 0666) != 0)
            {
              NS_LOG_ERROR("Failed to create shared pipe " << pipePath << ": " << std::strerror(errno));
              return;
            }
          NS_LOG_INFO("Created shared pipe: " << pipePath);
        }
      else if (!S_ISFIFO(st.st_mode))
        {
          NS_LOG_ERROR("File exists but is not a FIFO: " << pipePath);
          return;
        }

      NS_LOG_INFO("Opening shared pipe for writing: " << pipePath);
      s_sharedPipeFd = open(pipePath.c_str(), O_WRONLY | O_NONBLOCK);

      if (s_sharedPipeFd < 0)
        {
          if (errno == ENXIO)
            {
              NS_LOG_WARN("No reader connected to shared pipe - data will be discarded");
              return;
            }
          NS_LOG_ERROR("Failed to open shared pipe: " << std::strerror(errno));
          return;
        }

      s_sharedPipeInitialized = true;
      m_pipeInitialized = true;
      NS_LOG_INFO("Shared pipe opened: " << pipePath);
      return;
    }

  // Per-node pipe mode (original behavior)
  if (m_pipeInitialized)
    {
      return;
    }

  if (m_pipePath.empty ())
    {
      NS_LOG_ERROR ("Pipe path not set for node " << m_nodeId);
      return;
    }

  // Create base directory if it doesn't exist
  struct stat st;
  if (stat (m_basePath.c_str (), &st) != 0)
    {
      if (mkdir (m_basePath.c_str (), 0755) != 0)
        {
          NS_LOG_ERROR ("Failed to create directory " << m_basePath << ": " << std::strerror (errno));
          return;
        }
      NS_LOG_INFO ("Created directory: " << m_basePath);
    }

  // Check if pipe exists, create only if it doesn't
  if (stat (m_pipePath.c_str (), &st) != 0)
    {
      // Pipe doesn't exist, create it
      if (mkfifo (m_pipePath.c_str (), 0666) != 0)
        {
          NS_LOG_ERROR ("Failed to create named pipe " << m_pipePath << ": " << std::strerror (errno));
          return;
        }
      NS_LOG_INFO ("Created named pipe: " << m_pipePath);
    }
  else
    {
      // Pipe already exists
      if (!S_ISFIFO (st.st_mode))
        {
          NS_LOG_ERROR ("File exists but is not a FIFO: " << m_pipePath);
          return;
        }
      NS_LOG_INFO ("Using existing named pipe: " << m_pipePath);
    }

  // Open pipe for writing with O_NONBLOCK to prevent simulation from hanging
  // Use O_RDWR to avoid blocking - this opens the pipe immediately
  NS_LOG_INFO ("Opening pipe for writing (non-blocking): " << m_pipePath);
  m_pipeFd = open (m_pipePath.c_str (), O_WRONLY | O_NONBLOCK);

  if (m_pipeFd < 0)
    {
      if (errno == ENXIO)
        {
          // No reader connected yet - this is okay, we'll try again later
          NS_LOG_WARN ("No reader connected to pipe " << m_pipePath << " - data will be discarded until reader connects");
          m_pipeInitialized = false;
          return;
        }
      NS_LOG_ERROR ("Failed to open named pipe " << m_pipePath << ": " << std::strerror (errno));
      return;
    }

  m_pipeInitialized = true;
  NS_LOG_INFO ("Named pipe opened for node " << m_nodeId << ": " << m_pipePath);
}

void
SpectrumPipeStreamer::PsdCallback (Ptr<const SpectrumValue> psd)
{
  NS_LOG_FUNCTION (this << psd);

  double timestamp = Simulator::Now ().GetMilliSeconds ();

  // Check if enough time has passed since last send
  if ((timestamp - m_lastSendTime.GetMilliSeconds ()) < m_streamInterval.GetMilliSeconds ())
    {
      return;
    }

  // Try to initialize pipe if not ready (retry periodically)
  bool needsInit = false;
  if (s_sharedPipeEnabled)
    {
      needsInit = !s_sharedPipeInitialized || s_sharedPipeFd < 0;
    }
  else
    {
      needsInit = !m_pipeInitialized || m_pipeFd < 0;
    }

  if (needsInit)
    {
      InitializePipe ();
      // Check again after init attempt
      if (s_sharedPipeEnabled)
        {
          if (!s_sharedPipeInitialized || s_sharedPipeFd < 0)
            {
              return;
            }
        }
      else if (!m_pipeInitialized)
        {
          return;
        }
    }

  m_lastSendTime = MilliSeconds (timestamp);

  // Serialize and send
  std::string binary = SerializeToBinary (timestamp, psd);
  if (!binary.empty())
    {
      SendData (binary);
    }
}

std::string
SpectrumPipeStreamer::SerializeToBinary (double timestamp, Ptr<const SpectrumValue> psd)
{
  NS_LOG_FUNCTION (this << timestamp << psd);

  if (!psd)
    {
      NS_LOG_WARN ("Received null PSD pointer for node " << m_nodeId);
      return "";
    }

  // Binary format:
  // [node_id: uint32_t][timestamp: double][num_values: uint32_t][psd_values: double array]
  
  uint32_t numValues = psd->GetSpectrumModel ()->GetNumBands ();
  
  // Calculate total size
  size_t totalSize = sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t) + (numValues * sizeof(double));
  
  std::string binaryData;
  binaryData.reserve (totalSize);
  
  // Append node_id
  uint32_t nodeId = m_nodeId;
  binaryData.append (reinterpret_cast<const char*>(&nodeId), sizeof(uint32_t));
  
  // Append timestamp
  binaryData.append (reinterpret_cast<const char*>(&timestamp), sizeof(double));
  
  // Append number of values
  binaryData.append (reinterpret_cast<const char*>(&numValues), sizeof(uint32_t));
  
  // Append PSD values
  Values::const_iterator it = psd->ConstValuesBegin ();
  while (it != psd->ConstValuesEnd ())
    {
      double value = *it;
      binaryData.append (reinterpret_cast<const char*>(&value), sizeof(double));
      ++it;
    }

  NS_LOG_DEBUG ("Serialized binary data: node=" << nodeId 
                << " timestamp=" << timestamp 
                << " values=" << numValues 
                << " total_bytes=" << binaryData.size ());

  return binaryData;
}

void
SpectrumPipeStreamer::SendData (const std::string& data)
{
  NS_LOG_FUNCTION (this << data.length ());

  int fd;
  if (s_sharedPipeEnabled)
    {
      fd = s_sharedPipeFd;
      if (fd < 0)
        {
          NS_LOG_DEBUG ("Shared pipe not open - skipping");
          return;
        }
    }
  else
    {
      fd = m_pipeFd;
      if (fd < 0)
        {
          NS_LOG_DEBUG ("Pipe not open for node " << m_nodeId << " - skipping");
          return;
        }
    }

  // Write to named pipe
  ssize_t written = write (fd, data.c_str (), data.length ());

  if (written < 0)
    {
      if (errno == EPIPE)
        {
          // Reader disconnected - close pipe and try to reconnect later
          NS_LOG_WARN ("Reader disconnected from pipe for node " << m_nodeId);
          if (s_sharedPipeEnabled)
            {
              close (s_sharedPipeFd);
              s_sharedPipeFd = -1;
              s_sharedPipeInitialized = false;
            }
          else
            {
              close (m_pipeFd);
              m_pipeFd = -1;
            }
          m_pipeInitialized = false;
        }
      else if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
          // Pipe buffer full - discard this sample
          NS_LOG_DEBUG ("Pipe buffer full for node " << m_nodeId << " - discarding sample");
        }
      else
        {
          NS_LOG_ERROR ("Failed to write to pipe for node " << m_nodeId << ": " << std::strerror (errno));
        }
    }
  else if (static_cast<size_t>(written) < data.length ())
    {
      NS_LOG_WARN ("Partial write to pipe for node " << m_nodeId << ": "
                   << written << " of " << data.length () << " bytes");
      m_packetsSent++;
      m_bytesSent += written;
    }
  else
    {
      m_packetsSent++;
      m_bytesSent += data.length ();
      NS_LOG_DEBUG ("Node " << m_nodeId << " wrote " << written << " bytes to pipe");
    }
}

} // namespace ns3
