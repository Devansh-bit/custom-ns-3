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
 * \file spectrum-pipe-streamer-example.cc
 * \brief Complete end-to-end example: RRM simulation with named pipe streaming
 *
 * This example demonstrates the FULL pipeline:
 * 1. Setup WiFi APs with RRM
 * 2. Generate interferers (WiFi, Bluetooth, Microwave, Zigbee, Cordless Phone)
 * 3. Install spectrum analyzers with BOTH:
 *    - File logging (for offline analysis)
 *    - Named pipe streaming (for real-time processing)
 * 4. Python reader receives data via named pipes for real-time processing
 * 5. Results can be compared: offline (files) vs online (pipe)
 *
 * Named pipes provide blocking behavior: simulation synchronizes with reader.
 *
 * Usage:
 *   Terminal 1: python3 spectrum_pipe_reader.py --base-path /tmp/ns3-spectrum --num-nodes 3
 *   Terminal 2: ./ns3 run spectrum-pipe-streamer-example
 */

/**
 * run normally
 * to check spectrogram do the following - python contrib/lever-api/examples/plot-multi-ap-spectrum.py rrm-spectrum-pipe-node0.tr
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/spectrum-pipe-streamer-helper.h"
#include "ns3/spectrum-analyser-logger-helper.h"
#include "ns3/spectrogram-generation-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("CompleteRrmPipeExample");

int
main (int argc, char *argv[])
{
  // ========== Simulation Parameters ==========
  double simTime = 10.0;           // 5 second simulation
  uint32_t numAPs = 1;            // 1 AP with RRM
  std::string basePath = "/tmp/ns3-spectrum";

  // Spectrum analyzer parameters
  double startFreq = 2.400e9;      // 2.4 GHz ISM band
  double freqResolution = 100e3;   // 100 kHz resolution
  uint32_t numFreqBins = 1001;     // 100.1 MHz span (2.4-2.5001 GHz)
  double noisePowerDbm = -140.0;   // -140 dBm/Hz noise floor
  // double streamInterval = 1.0;   // 100 ms streaming interval
  // double logInterval = 1.0;      // 100 ms logging interval
  uint32_t streamInterval = 10;   // 100 ms streaming interval
  uint32_t logInterval = 10;      // 100 ms logging interval


  bool enableFileLogging = false;  // Save to files for offline analysis (disabled for dual-band)
  bool enablePipeStreaming = true; // Stream to Python via named pipes

  bool stream2_4Ghz = true;   // Stream 2.4 GHz band
  bool stream5Ghz = true;     // Stream 5 GHz band

  // Command line arguments
  CommandLine cmd (__FILE__);
  cmd.AddValue ("simTime", "Simulation time in seconds", simTime);
  cmd.AddValue ("numAPs", "Number of APs", numAPs);
  cmd.AddValue ("basePath", "Base path for named pipes", basePath);
  cmd.AddValue ("streamInterval", "Streaming interval (ms)", streamInterval);
  cmd.AddValue ("logInterval", "Logging interval (ms)", logInterval);
  cmd.AddValue ("enableLogging", "Enable file logging", enableFileLogging);
  cmd.AddValue ("enableStreaming", "Enable pipe streaming", enablePipeStreaming);
  cmd.AddValue ("stream2_4Ghz", "Stream 2.4 GHz band", stream2_4Ghz);
  cmd.AddValue ("stream5Ghz", "Stream 5 GHz band", stream5Ghz);
  cmd.Parse (argc, argv);

  // Enable logging
  LogComponentEnable ("CompleteRrmPipeExample", LOG_LEVEL_INFO);

  NS_LOG_INFO ("╔═══════════════════════════════════════════════════════════╗");
  NS_LOG_INFO ("║  Complete RRM Simulation with Named Pipe Streaming       ║");
  NS_LOG_INFO ("╚═══════════════════════════════════════════════════════════╝");
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Simulation Parameters:");
  NS_LOG_INFO ("  Duration: " << simTime << " seconds");
  NS_LOG_INFO ("  Number of APs: " << numAPs);
  NS_LOG_INFO ("  Frequency range: " << startFreq / 1e9 << " - "
               << (startFreq + numFreqBins * freqResolution) / 1e9 << " GHz");
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Output Configuration:");
  NS_LOG_INFO ("  File logging: " << (enableFileLogging ? "ENABLED" : "DISABLED"));
  NS_LOG_INFO ("  Pipe streaming: " << (enablePipeStreaming ? "ENABLED" : "DISABLED"));
  if (enablePipeStreaming)
    {
      NS_LOG_INFO ("  Pipe base path: " << basePath);
    }
  NS_LOG_INFO ("");

  // ========== Create Spectrum Channel ==========
  NS_LOG_INFO ("Setting up spectrum channel...");
  SpectrumChannelHelper channelHelper = SpectrumChannelHelper::Default ();
  channelHelper.SetChannel ("ns3::MultiModelSpectrumChannel");
  channelHelper.AddSpectrumPropagationLoss ("ns3::FriisSpectrumPropagationLossModel");
  Ptr<SpectrumChannel> channel = channelHelper.Create ();

  // ========== Create Nodes ==========
  NS_LOG_INFO ("Creating nodes...");

  // RRM AP nodes
  NodeContainer apNodes;
  apNodes.Create (numAPs);

  // Interferer nodes
  NodeContainer interfererNodes;
  interfererNodes.Create (7);  // WiFi, Bluetooth, 3x microwave bursts, Zigbee, Cordless phone

  NodeContainer allNodes;
  allNodes.Add (apNodes);
  allNodes.Add (interfererNodes);

  // ========== Setup Mobility ==========
  NS_LOG_INFO ("Configuring mobility...");
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();

  // Position APs in a line
  for (uint32_t i = 0; i < numAPs; ++i)
    {
      double x = i * 50.0;  // 50m spacing
      positionAlloc->Add (Vector (x, 0.0, 0.0));
      NS_LOG_INFO ("  AP " << i << " at position (" << x << ", 0, 0)");
    }

  // Position interferers randomly
  Ptr<UniformRandomVariable> uniformRv = CreateObject<UniformRandomVariable> ();
  uniformRv->SetStream (0);
  for (uint32_t i = 0; i < interfererNodes.GetN (); ++i)
    {
      double distance = uniformRv->GetValue (10.0, 40.0);
      double angle = uniformRv->GetValue (0.0, 2 * M_PI);
      double x = distance * std::cos (angle);
      double y = distance * std::sin (angle);
      positionAlloc->Add (Vector (x, y, 0.0));
    }

  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (allNodes);

  // ========== Generate Interferers ==========
  NS_LOG_INFO ("");
  NS_LOG_INFO ("Generating interferers...");

  Ptr<AnnotationManager> annotationMgr = CreateObject<AnnotationManager> ();
  SpectrogramGenerationHelper interfererHelper (annotationMgr);

  // Power levels
  const double wifiPsd = 1e-3;      // ~-30 dBm/Hz (WiFi AP)
  const double btPsd = 1e-5;        // ~-50 dBm/Hz (Bluetooth)
  const double microwavePsd = 5e-4; // ~-33 dBm/Hz (Microwave oven)

  Ptr<NormalRandomVariable> jitterRv = CreateObject<NormalRandomVariable> ();
  jitterRv->SetAttribute ("Mean", DoubleValue (0.0));
  jitterRv->SetAttribute ("Variance", DoubleValue (1e-12));

  // 1. WiFi interferer on channel 6 (2.437 GHz)
  double wifiStart = 0.1;
  double wifiDuration = simTime - 0.2;
  interfererHelper.GenerateWifiInterferer (channel, interfererNodes.Get (0),
                                           2.437e9 - 10e6, wifiStart, wifiDuration,
                                           20e6, wifiPsd);
  NS_LOG_INFO ("  WiFi interferer: Channel 6 (2.437 GHz), 20 MHz bandwidth");
  NS_LOG_INFO ("WIFI debug");

  // 2. Bluetooth interferer (frequency hopping)
  double btStart = 0.25;
  double btDuration = simTime - btStart;  // Duration, not number of hops
  interfererHelper.GenerateBluetoothInterferer (channel, interfererNodes.Get (1),
                                                btStart, btDuration, 625e-6, 1e-6,
                                                btPsd, 0x5A, uniformRv, jitterRv);
  NS_LOG_INFO ("  Bluetooth interferer: Duration " << btDuration << "s, hop interval 625us");
  NS_LOG_INFO ("BLE debug");

  // 3. Microwave oven (pulsed interference at 60 Hz)
  double microwaveStart = 0.5;
  double dist_between_lines = 0.25;
  double duration = 0.008;
  int numPulses = static_cast<int> ((simTime - microwaveStart) / dist_between_lines);
  for (int i = 0; i < numPulses; i++)
    {
      double start_time = microwaveStart + i * dist_between_lines;
      if (start_time + duration < simTime)
        {
          interfererHelper.GenerateMicrowaveInterferer (channel, interfererNodes.Get (2 + (i % 3)),
                                                        2.45e9, start_time, duration, 100e6,
                                                        microwavePsd);
        }
    }
  NS_LOG_INFO ("  Microwave oven: " << numPulses << " pulses at 2.45 GHz, 100 MHz wide");

  // 4. Zigbee interferer on channel 15 (2.425 GHz)
  double zigbeeStart = 0.75;
  double zigbee_hop_interval = 0.0016;
  const double zigbeePsd = 1e-6;  // ~-60 dBm/Hz (Zigbee is low power)
  interfererHelper.GenerateZigbeeInterferer (channel, interfererNodes.Get (5),15, zigbeeStart,
                                             zigbee_hop_interval, zigbeePsd, uniformRv);
  NS_LOG_INFO ("  Zigbee interferer: Channel 15 (2.425 GHz)");

  // 5. Cordless phone (DECT-like frequency hopping)
  double cordlessStart = 1;
  double cordless_hop_interval = 0.01;
  double cordless_duty_time = 0.005;
  int cordlessHops = static_cast<int> ((simTime - cordlessStart) / cordless_hop_interval);  // 10ms hop interval
  const double cordlessPsd = 5e-5;  // ~-43 dBm/Hz (Cordless phone)
  interfererHelper.GenerateCordlessInterferer (channel, interfererNodes.Get (6),
                                               cordlessStart, cordlessHops,
                                               cordless_hop_interval, cordless_duty_time, 1e6, cordlessPsd);
  NS_LOG_INFO ("  Cordless phone: " << cordlessHops << " hops, 1 MHz bandwidth");

  NS_LOG_INFO ("");
  // 5GHz signal generation
    const double wifiPsd_5 = 3.16e-9;
  const double radarPsd = 3.16e-3;

  std::vector<double> wifi5ghzchannels = {
      5.180e9, 5.190e9, 5.210e9, 5.260e9,
      5.270e9, 5.290e9, 5.745e9, 5.785e9
  };

  std::vector<double> bandwidths = {
      20e6, 40e6, 80e6, 20e6,
      40e6, 80e6, 20e6, 80e6
  };

  int idx_wifi = uniformRv->GetInteger(0, wifi5ghzchannels.size() - 1);
  double wifi_start_5 = 0;
  double wifi_duration_5 = simTime - wifi_start_5 - 0.5;
  interfererHelper.GenerateWifiInterferer (channel, interfererNodes.Get (0),
                                           wifi5ghzchannels[idx_wifi] - (bandwidths[idx_wifi]/2), wifi_start_5, wifi_duration_5,bandwidths[idx_wifi], wifiPsd_5);

  NS_LOG_INFO ("  5 GHz WiFi interferer: Center " << wifi5ghzchannels[idx_wifi]/1e9 << " GHz, " << (bandwidths[idx_wifi]/1e6) << " MHz bandwidth");
  NS_LOG_INFO ("");

  int idx_radar = uniformRv->GetInteger(0, 3);
  std::vector<double> radar_frequencies = {5.250e9, 5.350e9, 5.470e9, 5.725e9};
  const double radarBandwidth = 100e6;  // 100 MHz typical radar bandwidth
  
  // Make radar highly detectable for CNN with significantly increased duty cycle and duration:
  // Strategy: Add multiple long continuous radar segments (2-5 seconds each) throughout simulation
  // These long segments ensure radar is present when CNN collects data
  int num_radar_segments = 8;  // 8 radar segments throughout simulation for better coverage
  for (int seg = 0; seg < num_radar_segments; seg++)
    {
      double seg_start = 1.0 + seg * (simTime - 3.0) / num_radar_segments;  // Start after 1s, spread evenly
      double seg_duration = uniformRv->GetValue(2.0, 5.0);  // 2-5 second continuous radar (significantly increased)
      int seg_freq_idx = uniformRv->GetInteger(0, radar_frequencies.size() - 1);
      double seg_freq = radar_frequencies[seg_freq_idx];
      
      if (seg_start + seg_duration < simTime)
        {
          uint32_t seg_node = 2 + (seg % 3);
          if (seg_node < interfererNodes.GetN())
            {
              interfererHelper.GenerateMicrowaveInterferer (channel, interfererNodes.Get (seg_node),
                                                            seg_freq, seg_start, seg_duration, radarBandwidth,
                                                            radarPsd);
              NS_LOG_INFO ("  Radar segment " << seg << ": " << seg_duration << " s continuous at " 
                           << seg_freq/1e9 << " GHz, start=" << seg_start << " s");
            }
        }
    }
  
  // Also add periodic longer bursts (200-500ms) every 1.5 seconds for pulse-like radar with high duty cycle
  double burst_interval = 1.5;  // Every 1.5 seconds (more frequent)
  int num_bursts = static_cast<int>((simTime - 0.5) / burst_interval);
  for (int burst_idx = 0; burst_idx < num_bursts; burst_idx++)
    {
      double burst_start = 0.5 + burst_idx * burst_interval;
      double burst_duration = uniformRv->GetValue(0.200, 0.500);  // 200-500 ms bursts (significantly increased)
      int burst_freq_idx = uniformRv->GetInteger(0, radar_frequencies.size() - 1);
      double burst_freq = radar_frequencies[burst_freq_idx];
      
      if (burst_start + burst_duration < simTime)
        {
          uint32_t burst_node = 2 + (burst_idx % 3);
          if (burst_node < interfererNodes.GetN())
            {
              interfererHelper.GenerateMicrowaveInterferer (channel, interfererNodes.Get (burst_node),
                                                            burst_freq, burst_start, burst_duration, radarBandwidth,
                                                            radarPsd);
            }
        }
    }

  // ========== OPTION 1: File Logging (Offline Analysis) ==========
  // Disabled for dual-band pipe streaming
  // if (enableFileLogging)
  //   {
  //     NS_LOG_INFO ("Installing spectrum analyzer with FILE LOGGING...");
  //     SpectrumAnalyserLoggerHelper loggerHelper;
  //     loggerHelper.SetChannel (channel);
  //     loggerHelper.SetFrequencyRange (startFreq, freqResolution, numFreqBins);
  //     loggerHelper.SetNoiseFloor (noisePowerDbm);
  //     loggerHelper.SetLoggingInterval (MilliSeconds (logInterval));
  //     loggerHelper.SetAnalyzerResolution (MilliSeconds (1));
  //     loggerHelper.InstallOnNodes (apNodes, "rrm-spectrum-pipe");
  //     NS_LOG_INFO ("  Output files: rrm-spectrum-pipe-node-{0.." << (numAPs-1) << "}.tr");
  //     NS_LOG_INFO ("  Logging interval: " << logInterval << " ms");
  //   }

  // ========== OPTION 2: Named Pipe Streaming (Real-time Processing) ==========
  if (enablePipeStreaming)
    {
      NS_LOG_INFO ("Installing spectrum analyzer with NAMED PIPE STREAMING...");

      // Stream 2.4 GHz band
      if (stream2_4Ghz)
        {
          NS_LOG_INFO ("  Setting up 2.4 GHz band streaming...");
          SpectrumPipeStreamerHelper pipeStreamer2_4;
          pipeStreamer2_4.SetBasePath (basePath + "-2.4ghz");
          pipeStreamer2_4.SetChannel (channel);
          pipeStreamer2_4.SetFrequencyRange (2.400e9, freqResolution, numFreqBins);  // 2.4 GHz monitoring
          pipeStreamer2_4.SetNoiseFloor (noisePowerDbm);
          pipeStreamer2_4.SetStreamInterval (MilliSeconds (streamInterval));
          pipeStreamer2_4.SetAnalyzerResolution (MilliSeconds (1));
          pipeStreamer2_4.InstallOnNodes (apNodes);

          NS_LOG_INFO ("    2.4 GHz Pipe base path: " << basePath + "-2.4ghz");
          NS_LOG_INFO ("    Streaming interval: " << streamInterval << " ms");
          NS_LOG_INFO ("    Expected packets per AP: ~" << (int)(simTime * 1000 / streamInterval));
          NS_LOG_INFO ("    Pipe paths:");
          for (uint32_t i = 0; i < numAPs; ++i)
            {
              std::string pipePath = pipeStreamer2_4.GetPipePath (apNodes.Get (i)->GetId ());
              NS_LOG_INFO ("      Node " << apNodes.Get (i)->GetId () << ": " << pipePath);
            }
        }

      // Stream 5 GHz band
      if (stream5Ghz)
        {
          NS_LOG_INFO ("  Setting up 5 GHz band streaming...");
          SpectrumPipeStreamerHelper pipeStreamer5;
          pipeStreamer5.SetBasePath (basePath + "-5ghz");
          pipeStreamer5.SetChannel (channel);
          pipeStreamer5.SetFrequencyRange (5150e6, 1000e3, 1001);  // 5 GHz monitoring (5.15-6.15 GHz, 1 MHz res, 1001 bins)
          pipeStreamer5.SetNoiseFloor (noisePowerDbm);
          pipeStreamer5.SetStreamInterval (MilliSeconds (streamInterval));
          pipeStreamer5.SetAnalyzerResolution (MilliSeconds (1));
          pipeStreamer5.InstallOnNodes (apNodes);

          NS_LOG_INFO ("    5 GHz Pipe base path: " << basePath + "-5ghz");
          NS_LOG_INFO ("    Streaming interval: " << streamInterval << " ms");
          NS_LOG_INFO ("    Expected packets per AP: ~" << (int)(simTime * 1000 / streamInterval));
          NS_LOG_INFO ("    Pipe paths:");
          for (uint32_t i = 0; i < numAPs; ++i)
            {
              std::string pipePath = pipeStreamer5.GetPipePath (apNodes.Get (i)->GetId ());
              NS_LOG_INFO ("      Node " << apNodes.Get (i)->GetId () << ": " << pipePath);
            }
        }
    }

  NS_LOG_INFO ("");

  // ========== Information Messages ==========
  if (enablePipeStreaming)
    {
      NS_LOG_INFO ("╔═══════════════════════════════════════════════════════════╗");
      NS_LOG_INFO ("║  IMPORTANT: Start Python reader before running!          ║");
      NS_LOG_INFO ("║                                                           ║");
      NS_LOG_INFO ("║  Terminal 1:                                              ║");
      NS_LOG_INFO ("║    cd contrib/spectrum-pipe-streamer/examples             ║");
      NS_LOG_INFO ("║    python3 simple_pipe_test.py                            ║");
      NS_LOG_INFO ("║                                                           ║");
      NS_LOG_INFO ("║  Dual-band streaming configuration:                      ║");
      if (stream2_4Ghz)
        {
          NS_LOG_INFO ("║    2.4 GHz: " << basePath << "-2.4ghz");
        }
      if (stream5Ghz)
        {
          NS_LOG_INFO ("║    5 GHz:   " << basePath << "-5ghz");
        }
      NS_LOG_INFO ("║    Num nodes: " << numAPs << "                                       ║");
      NS_LOG_INFO ("║                                                           ║");
      NS_LOG_INFO ("║  Then run this simulation in Terminal 2                  ║");
      NS_LOG_INFO ("║  Note: Simulation will BLOCK until reader connects!      ║");
      NS_LOG_INFO ("╚═══════════════════════════════════════════════════════════╝");
      NS_LOG_INFO ("");
    }

  NS_LOG_INFO ("Starting simulation for " << simTime << " seconds...");
  NS_LOG_INFO ("");

  // ========== Run Simulation ==========
  Simulator::Stop (Seconds (simTime));
  Simulator::Run ();

  NS_LOG_INFO ("");
  NS_LOG_INFO ("╔═══════════════════════════════════════════════════════════╗");
  NS_LOG_INFO ("║  Simulation Completed Successfully                        ║");
  NS_LOG_INFO ("╚═══════════════════════════════════════════════════════════╝");
  NS_LOG_INFO ("");

  if (enableFileLogging)
    {
      NS_LOG_INFO ("Offline Analysis:");
      NS_LOG_INFO ("  Files saved in current directory:");
      for (uint32_t i = 0; i < numAPs; ++i)
        {
          NS_LOG_INFO ("    - rrm-spectrum-pipe-node-" << i << ".tr");
        }
      NS_LOG_INFO ("");
    }

  if (enablePipeStreaming)
    {
      NS_LOG_INFO ("Real-time Processing:");
      NS_LOG_INFO ("  Check Python reader output for processing results");
      NS_LOG_INFO ("  Named pipes closed (readers should see EOF)");
      NS_LOG_INFO ("");
    }

  NS_LOG_INFO ("Data Analysis:");
  NS_LOG_INFO ("  Each data record contains 1001 PSD values");
  NS_LOG_INFO ("  Frequency bins: 2.400 GHz to 2.5001 GHz");
  NS_LOG_INFO ("  Expected signals:");
  NS_LOG_INFO ("    - WiFi peak around bin 370 (2.437 GHz)");
  NS_LOG_INFO ("    - Bluetooth peaks scattered (frequency hopping)");
  NS_LOG_INFO ("    - Microwave peaks around bin 500 (2.45 GHz)");
  NS_LOG_INFO ("    - Zigbee peaks around bin 250 (2.425 GHz)");
  NS_LOG_INFO ("    - Cordless phone peaks scattered (frequency hopping)");
  NS_LOG_INFO ("");

  // Cleanup
  Simulator::Destroy ();

  return 0;
}
