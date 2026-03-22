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

/**
 * \file spectrum-analyser-logger-example.cc
 * \brief Example demonstrating the plug-and-play spectrum analyzer logger
 *
 * This example shows how to use SpectrumAnalyserLoggerHelper to easily
 * add spectrum analysis capabilities to any simulation with just a few lines.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/spectrum-analyser-logger-helper.h"
#include "ns3/spectrum-analyzer-helper.h"
#include "ns3/spectrogram-generation-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("SpectrumAnalyserLoggerExample");

int
main (int argc, char *argv[])
{
  // Simulation parameters
  double simTime = 2.0;  // 2 second simulation
  std::string outputPrefix = "spectrum-log";
  uint32_t numAPs = 3;

  // Spectrum analyzer parameters
  double startFreq = 2.400e9;      // 2.4 GHz
  double freqResolution = 100e3;   // 100 kHz resolution
  uint32_t numFreqBins = 1000;     // 100 MHz span
  double noisePowerDbm = -140.0;   // -140 dBm/Hz noise floor
  double loggingInterval = 1.0;  // 100 ms logging interval

  // Command line arguments
  CommandLine cmd (__FILE__);
  cmd.AddValue ("simTime", "Simulation time in seconds", simTime);
  cmd.AddValue ("outputPrefix", "Output file prefix", outputPrefix);
  cmd.AddValue ("numAPs", "Number of APs", numAPs);
  cmd.AddValue ("startFreq", "Start frequency (Hz)", startFreq);
  cmd.AddValue ("freqRes", "Frequency resolution (Hz)", freqResolution);
  cmd.AddValue ("numBins", "Number of frequency bins", numFreqBins);
  cmd.AddValue ("noisePower", "Noise floor (dBm/Hz)", noisePowerDbm);
  cmd.AddValue ("loggingInterval", "Logging interval (ms)", loggingInterval);
  cmd.Parse (argc, argv);

  // Enable logging
  LogComponentEnable ("SpectrumAnalyserLoggerExample", LOG_LEVEL_INFO);

  NS_LOG_INFO ("=== Spectrum Analyser Logger Example ===");
  NS_LOG_INFO ("Simulation time: " << simTime << " seconds");
  NS_LOG_INFO ("Number of APs: " << numAPs);
  NS_LOG_INFO ("Frequency range: " << startFreq / 1e9 << " - "
               << (startFreq + numFreqBins * freqResolution) / 1e9 << " GHz");

  // Create spectrum channel
  SpectrumChannelHelper channelHelper = SpectrumChannelHelper::Default ();
  channelHelper.SetChannel ("ns3::MultiModelSpectrumChannel");
  channelHelper.AddSpectrumPropagationLoss ("ns3::FriisSpectrumPropagationLossModel");
  Ptr<SpectrumChannel> channel = channelHelper.Create ();

  // Create AP nodes
  NodeContainer apNodes;
  apNodes.Create (numAPs);

  // Create interferer nodes
  NodeContainer interfererNodes;
  interfererNodes.Create (3);  // 3 interferer nodes

  NodeContainer allNodes;
  allNodes.Add (apNodes);
  allNodes.Add (interfererNodes);

  // Setup mobility - place APs at different locations
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();

  // Position APs
  for (uint32_t i = 0; i < numAPs; ++i)
    {
      double x = i * 50.0;  // Space APs 50m apart
      positionAlloc->Add (Vector (x, 0.0, 0.0));
    }

  // Position interferers at random locations (10-30m from origin)
  Ptr<UniformRandomVariable> uniformRv = CreateObject<UniformRandomVariable> ();
  for (uint32_t i = 0; i < interfererNodes.GetN (); ++i)
    {
      double distance = uniformRv->GetValue (10.0, 30.0);
      double angle = uniformRv->GetValue (0.0, 2 * M_PI);
      double x = distance * std::cos (angle);
      double y = distance * std::sin (angle);
      positionAlloc->Add (Vector (x, y, 0.0));
    }

  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (allNodes);

  NS_LOG_INFO ("Created " << numAPs << " AP nodes and " << interfererNodes.GetN () << " interferer nodes");

  // ========== THIS IS THE PLUG-AND-PLAY API! ==========
  // Just a few lines to add spectrum analysis to any simulation

  SpectrumAnalyserLoggerHelper spectrumLogger;
  spectrumLogger.SetChannel (channel);
  spectrumLogger.SetFrequencyRange (startFreq, freqResolution, numFreqBins);
  spectrumLogger.SetNoiseFloor (noisePowerDbm);
  spectrumLogger.SetLoggingInterval (MilliSeconds (loggingInterval));
  spectrumLogger.EnableConsoleOutput (true);  // Set to true for console output

  // Install on all AP nodes - one line!
  spectrumLogger.InstallOnNodes (apNodes, outputPrefix);

  NS_LOG_INFO ("Installed spectrum analyzers on all APs");

  // =====================================================

  // ========== GENERATE INTERFERERS FOR DEMONSTRATION ==========
  NS_LOG_INFO ("Generating interferers...");

  // Create helper for interferer generation
  Ptr<AnnotationManager> annotationMgr = CreateObject<AnnotationManager> ();
  SpectrogramGenerationHelper interfererHelper (annotationMgr);

  // Power levels
  const double wifiPsd = 1e-3;      // ~-30 dBm/Hz
  const double btPsd = 1e-5;        // ~-50 dBm/Hz
  const double microwavePsd = 1e-2; // ~-20 dBm/Hz

  Ptr<NormalRandomVariable> jitterRv = CreateObject<NormalRandomVariable> ();
  jitterRv->SetAttribute ("Mean", DoubleValue (0.0));
  jitterRv->SetAttribute ("Variance", DoubleValue (1e-12));

  // WiFi interferer on channel 6 (2.437 GHz) - continuous
  double wifiStart = 0.05;
  double wifiDuration = simTime - 0.1;
  interfererHelper.GenerateWifiInterferer (channel, interfererNodes.Get (0), 2.437e9 - 10e6,
                                           wifiStart, wifiDuration, 20e6, wifiPsd);
  NS_LOG_INFO ("  WiFi @ 2.437 GHz");

  // Bluetooth interferer - frequency hopping
  double btStart = 0.1;
  interfererHelper.GenerateBluetoothInterferer (channel,
                                               interfererNodes.Get(1),
                                               btStart,
                                               0.1,
                                               625e-6,
                                               1e-6,
                                               btPsd,
                                               0x5A,
                                               uniformRv,
                                               jitterRv);
  NS_LOG_INFO ("  Bluetooth (frequency hopping)");

  // Microwave oven - periodic pulses at 60 Hz
  double microwaveStart = 0.2;
  int numPulses = static_cast<int> ((simTime - microwaveStart) / 0.01667);
  for (int i = 0; i < numPulses; i++)
    {
      double start = microwaveStart + i * 0.01667;
      if (start + 0.008 < simTime)
        {
          interfererHelper.GenerateMicrowaveInterferer (channel, interfererNodes.Get (2), 2.45e9,
                                                        start, 0.008, 100e6, microwavePsd);
        }
    }
  NS_LOG_INFO ("  Microwave @ 2.45 GHz (periodic pulses)");

  // =============================================================

  NS_LOG_INFO ("Starting simulation...");

  // Run simulation
  Simulator::Stop (Seconds (simTime));
  Simulator::Run ();

  NS_LOG_INFO ("Simulation completed");

  // Get all loggers and print statistics
  auto loggers = spectrumLogger.GetAllLoggers ();
  NS_LOG_INFO ("===== Output Files =====");
  for (const auto& pair : loggers)
    {
      NS_LOG_INFO ("  Node " << pair.first << ": " << outputPrefix << "-node" << pair.first << ".tr");
    }

  // Cleanup
  Simulator::Destroy ();

  return 0;
}
