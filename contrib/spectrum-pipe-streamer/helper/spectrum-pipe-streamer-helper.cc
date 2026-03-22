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

#include "spectrum-pipe-streamer-helper.h"
#include "ns3/log.h"
#include "ns3/spectrum-analyzer-helper.h"
#include "ns3/spectrum-analyzer.h"
#include "ns3/spectrum-model.h"
#include "ns3/double.h"
#include "ns3/config.h"
#include "ns3/non-communicating-net-device.h"
#include "ns3/net-device.h"
#include "ns3/mac48-address.h"
#include <cmath>
#include <sstream>
#include <iomanip>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SpectrumPipeStreamerHelper");

SpectrumPipeStreamerHelper::SpectrumPipeStreamerHelper ()
  : m_basePath ("/tmp/ns3-spectrum"),
    m_channel (nullptr),
    m_startFreq (2.4e9),           // Default: 2.4 GHz
    m_freqResolution (100e3),      // Default: 100 kHz
    m_numFreqBins (1000),          // Default: 1000 bins (100 MHz span)
    m_noisePowerDbm (-140.0),      // Default: -140 dBm/Hz
    m_streamInterval (MilliSeconds (100)),  // Default: 100 ms
    m_analyzerResolution (MilliSeconds (1)) // Default: 1 ms
{
  NS_LOG_FUNCTION (this);
}

SpectrumPipeStreamerHelper::~SpectrumPipeStreamerHelper ()
{
  NS_LOG_FUNCTION (this);
}

void
SpectrumPipeStreamerHelper::SetBasePath (const std::string& basePath)
{
  NS_LOG_FUNCTION (this << basePath);
  m_basePath = basePath;
}

void
SpectrumPipeStreamerHelper::EnableSharedPipe(const std::string& pipePath)
{
  NS_LOG_FUNCTION(this << pipePath);
  SpectrumPipeStreamer::EnableSharedPipe(pipePath);
  NS_LOG_INFO("Enabled shared pipe mode: " << pipePath);
}

void
SpectrumPipeStreamerHelper::DisableSharedPipe()
{
  NS_LOG_FUNCTION(this);
  SpectrumPipeStreamer::DisableSharedPipe();
  NS_LOG_INFO("Disabled shared pipe mode");
}

bool
SpectrumPipeStreamerHelper::IsSharedPipeEnabled() const
{
  return SpectrumPipeStreamer::IsSharedPipeEnabled();
}

std::string
SpectrumPipeStreamerHelper::GetSharedPipePath() const
{
  return SpectrumPipeStreamer::GetSharedPipePath();
}

void
SpectrumPipeStreamerHelper::SetChannel (Ptr<SpectrumChannel> channel)
{
  NS_LOG_FUNCTION (this << channel);
  m_channel = channel;
}

void
SpectrumPipeStreamerHelper::SetFrequencyRange (double startFreq, double resolution, uint32_t numBins)
{
  NS_LOG_FUNCTION (this << startFreq << resolution << numBins);
  m_startFreq = startFreq;
  m_freqResolution = resolution;
  m_numFreqBins = numBins;
}

void
SpectrumPipeStreamerHelper::SetNoiseFloor (double noisePowerDbm)
{
  NS_LOG_FUNCTION (this << noisePowerDbm);
  m_noisePowerDbm = noisePowerDbm;
}

void
SpectrumPipeStreamerHelper::SetStreamInterval (Time interval)
{
  NS_LOG_FUNCTION (this << interval);
  m_streamInterval = interval;
}

void
SpectrumPipeStreamerHelper::SetAnalyzerResolution (Time resolution)
{
  NS_LOG_FUNCTION (this << resolution);
  m_analyzerResolution = resolution;
}

Ptr<SpectrumModel>
SpectrumPipeStreamerHelper::CreateSpectrumModel () const
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

std::string
SpectrumPipeStreamerHelper::GetNodeMacAddress (Ptr<Node> node) const
{
  NS_LOG_FUNCTION (this << node);

  // Try to get MAC address from first net device
  if (node->GetNDevices () > 0)
    {
      Ptr<NetDevice> dev = node->GetDevice (0);
      Address addr = dev->GetAddress ();

      if (Mac48Address::IsMatchingType (addr))
        {
          Mac48Address mac = Mac48Address::ConvertFrom (addr);
          std::ostringstream oss;
          oss << mac;
          return oss.str ();
        }
    }

  // Fallback: generate MAC from node ID
  uint32_t nodeId = node->GetId ();
  std::ostringstream oss;
  oss << std::setfill ('0') << std::hex
      << "00:00:00:00:"
      << std::setw (2) << ((nodeId >> 8) & 0xFF) << ":"
      << std::setw (2) << (nodeId & 0xFF);

  return oss.str ();
}

Ptr<NetDevice>
SpectrumPipeStreamerHelper::InstallAnalyzer (Ptr<Node> node, Ptr<SpectrumPipeStreamer> streamer)
{
  NS_LOG_FUNCTION (this << node << streamer);

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

  // Configure streamer
  uint32_t nodeId = node->GetId ();
  streamer->SetNodeId (nodeId);
  streamer->SetNode (node);
  streamer->SetMacAddress (GetNodeMacAddress (node));
  streamer->SetFrequencyMetadata (m_startFreq, m_freqResolution, m_numFreqBins);
  streamer->SetBasePath (m_basePath);
  streamer->SetStreamInterval (m_streamInterval);

  // Connect the trace source to the streamer's callback
  uint32_t devId = device->GetIfIndex ();  // Get actual device index

  std::ostringstream oss;
  oss << "/NodeList/" << nodeId
      << "/DeviceList/" << devId
      << "/$ns3::NonCommunicatingNetDevice/Phy/AveragePowerSpectralDensityReport";

  Config::ConnectWithoutContext (
    oss.str (),
    MakeCallback (&SpectrumPipeStreamer::PsdCallback, streamer)
  );

  NS_LOG_INFO ("Connected trace callback for node " << nodeId << " at path: " << oss.str ());

  // Start the spectrum analyzer
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

  // Initialize pipe immediately (don't wait for first PSD callback)
  streamer->InitializePipe ();
  NS_LOG_INFO ("Named pipe initialized: " << streamer->GetPipePath ());

  return device;
}

Ptr<NetDevice>
SpectrumPipeStreamerHelper::InstallOnNode (Ptr<Node> node)
{
  NS_LOG_FUNCTION (this << node);

  uint32_t nodeId = node->GetId ();

  // Check if already installed on this node
  if (m_streamers.find (nodeId) != m_streamers.end ())
    {
      NS_LOG_WARN ("Spectrum analyzer streamer already installed on node " << nodeId);
      return nullptr;
    }

  // Create streamer for this node
  Ptr<SpectrumPipeStreamer> streamer = CreateObject<SpectrumPipeStreamer> ();

  // Install analyzer
  Ptr<NetDevice> device = InstallAnalyzer (node, streamer);

  // Store streamer
  m_streamers[nodeId] = streamer;

  NS_LOG_INFO ("Installed spectrum analyzer streamer on node " << nodeId
               << " -> pipe " << streamer->GetPipePath ());

  return device;
}

NetDeviceContainer
SpectrumPipeStreamerHelper::InstallOnNodes (NodeContainer nodes)
{
  NS_LOG_FUNCTION (this << &nodes);

  NetDeviceContainer devices;

  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<Node> node = nodes.Get (i);
      Ptr<NetDevice> device = InstallOnNode (node);
      if (device)
        {
          devices.Add (device);
        }
    }

  NS_LOG_INFO ("Installed spectrum analyzer streamers on " << devices.GetN () << " nodes");

  return devices;
}

Ptr<SpectrumPipeStreamer>
SpectrumPipeStreamerHelper::GetStreamer (uint32_t nodeId) const
{
  NS_LOG_FUNCTION (this << nodeId);

  auto it = m_streamers.find (nodeId);
  if (it != m_streamers.end ())
    {
      return it->second;
    }
  return nullptr;
}

std::map<uint32_t, Ptr<SpectrumPipeStreamer>>
SpectrumPipeStreamerHelper::GetAllStreamers () const
{
  return m_streamers;
}

std::string
SpectrumPipeStreamerHelper::GetPipePath (uint32_t nodeId) const
{
  NS_LOG_FUNCTION (this << nodeId);

  auto it = m_streamers.find (nodeId);
  if (it != m_streamers.end ())
    {
      return it->second->GetPipePath ();
    }
  return "";
}

} // namespace ns3
