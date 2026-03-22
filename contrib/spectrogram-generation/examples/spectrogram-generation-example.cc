/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 * \file base-example.cc
 * \brief Example simulation of ISM band interference with realistic patterns
 *
 * This example creates a 1-second simulation with various ISM band interferers:
 * - WiFi (2.4 GHz channels)
 * - Bluetooth (frequency hopping)
 * - ZigBee (IEEE 802.15.4)
 * - Cordless phones (DECT-like)
 * - Microwave ovens
 *
 * All interferers activate at random times with realistic parameters to simulate
 * real-world spectrum conditions.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/spectrum-analyzer-helper.h"
#include "ns3/spectrogram-generation.h"
#include "ns3/spectrogram-generation-helper.h"
#include <cmath>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("BaseExample");

void AveragePowerSpectralDensityCallback(Ptr<const SpectrumValue> psd)
{
  static double lastPrintTime = 0.0;
  double timestamp = Simulator::Now().GetSeconds();

  if ((timestamp - lastPrintTime) < 0.1)
  {
    return;
  }

  lastPrintTime = timestamp;
 // std::cout<<timestamp<<std::endl;
  
  Values::const_iterator it = psd->ConstValuesBegin();
  Bands::const_iterator bit = psd->ConstBandsBegin();
  
  while (it != psd->ConstValuesEnd())
  {
    std::cout << timestamp << " " 
              << bit->fc << " " 
              << (*it) << std::endl;
    ++it;
    ++bit;
  }
}


int
main(int argc, char *argv[])
{
  // Simulation parameters
  double simTime = 1.0;  // 1 second simulation
  std::string traceFile = "base-example";
  std::string annotationFile = "base-example-annotations.json";
  uint32_t rngSeed = 42;

  // Spectrum analyzer parameters
  double noisePowerDbm = -140.0;  // Lower noise floor to see signals more clearly
  double startFreq = 2.400e9;
  double freqResolution = 100e3;  // 100 kHz resolution
  int numFreqBins = 1000;  // 100 MHz span

  // Command line arguments
  CommandLine cmd(__FILE__);
  cmd.AddValue("simTime", "Simulation time in seconds", simTime);
  cmd.AddValue("traceFile", "Output trace file prefix", traceFile);
  cmd.AddValue("annotationFile", "Output annotation file name", annotationFile);
  cmd.AddValue("seed", "RNG seed", rngSeed);
  cmd.AddValue("noisePower", "Thermal noise power (dBm/Hz)", noisePowerDbm);
  cmd.AddValue("startFreq", "Start frequency (Hz)", startFreq);
  cmd.AddValue("freqRes", "Frequency resolution (Hz)", freqResolution);
  cmd.AddValue("numBins", "Number of frequency bins", numFreqBins);
  cmd.Parse(argc, argv);

  // Set RNG seed for reproducibility
  RngSeedManager::SetSeed(rngSeed);
  RngSeedManager::SetRun(1);

  // Enable logging
  LogComponentEnable("BaseExample", LOG_LEVEL_INFO);

  NS_LOG_INFO("Creating ISM Band Interference Simulation");
  NS_LOG_INFO("Simulation time: " << simTime << " seconds");
  NS_LOG_INFO("Trace file: " << traceFile << "-*.tr");
  NS_LOG_INFO("Annotation file: " << annotationFile);

  // Create spectrum channel
  SpectrumChannelHelper channelHelper = SpectrumChannelHelper::Default();
  channelHelper.SetChannel("ns3::MultiModelSpectrumChannel");
  channelHelper.AddSpectrumPropagationLoss("ns3::FriisSpectrumPropagationLossModel");
  Ptr<SpectrumChannel> channel = channelHelper.Create();

  // Create annotation manager
  Ptr<AnnotationManager> annotationMgr = CreateObject<AnnotationManager>();

  // Create helper
  SpectrogramGenerationHelper helper(annotationMgr);

  // Create nodes
  NodeContainer interfererNodes;
  NodeContainer spectrumAnalyzerNodes;
  interfererNodes.Create(10);  // Nodes for different interferers
  spectrumAnalyzerNodes.Create(1);  // One spectrum analyzer

  NodeContainer allNodes;
  allNodes.Add(interfererNodes);
  allNodes.Add(spectrumAnalyzerNodes);

  // Setup mobility - place nodes at random positions
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

  Ptr<UniformRandomVariable> uniformRv = CreateObject<UniformRandomVariable>();

  // Interferer nodes at random positions (5-20m from origin)
  for (uint32_t i = 0; i < interfererNodes.GetN(); ++i)
  {
    double distance = uniformRv->GetValue(5.0, 20.0);
    double angle = uniformRv->GetValue(0.0, 2 * M_PI);
    double x = distance * std::cos(angle);
    double y = distance * std::sin(angle);
    positionAlloc->Add(Vector(x, y, 0.0));
  }

  // Spectrum analyzer at origin
  positionAlloc->Add(Vector(0.0, 0.0, 0.0));

  mobility.SetPositionAllocator(positionAlloc);
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(allNodes);

  // Random variable generators
  Ptr<NormalRandomVariable> jitterRv = CreateObject<NormalRandomVariable>();
  jitterRv->SetAttribute("Mean", DoubleValue(0.0));
  jitterRv->SetAttribute("Variance", DoubleValue(1e-12));  // 1 µs^2 variance

  // Realistic power levels (W/Hz) - increased for better visibility
  const double wifiPsd = 1e-3;      // ~-30 dBm/Hz (10x increase)
  const double btPsd = 1e-5;        // ~-50 dBm/Hz (10x increase)
  const double zigbeePsd = 5e-6;    // ~-53 dBm/Hz (10x increase)
  const double cordlessPsd = 5e-4;  // ~-33 dBm/Hz (10x increase)
  const double microwavePsd = 1e-2; // ~-20 dBm/Hz (10x increase)

  // ========== WiFi Interferers ==========
  NS_LOG_INFO("Generating WiFi interferers...");

  // WiFi on channel 1 (2.412 GHz) - continuous data transfer
  double wifiCh1Start = uniformRv->GetValue(0.0, 0.1);
  double wifiCh1Duration = uniformRv->GetValue(0.3, 0.5);
  helper.GenerateWifiInterferer(channel, interfererNodes.Get(0), 2.412e9 - 10e6,
                                wifiCh1Start, wifiCh1Duration, 20e6, wifiPsd);

  // WiFi on channel 6 (2.437 GHz) - bursty traffic
  for (int i = 0; i < 5; i++)
  {
    double start = uniformRv->GetValue(0.1 + i * 0.15, 0.2 + i * 0.15);
    double duration = uniformRv->GetValue(0.03, 0.08);
    if (start + duration < simTime)
    {
      helper.GenerateWifiInterferer(channel, interfererNodes.Get(1), 2.437e9 - 10e6,
                                    start, duration, 20e6, wifiPsd);
    }
  }

  // WiFi on channel 11 (2.462 GHz) - intermittent
  double wifiCh11Start = uniformRv->GetValue(0.5, 0.7);
  double wifiCh11Duration = uniformRv->GetValue(0.2, 0.3);
  if (wifiCh11Start + wifiCh11Duration < simTime)
  {
    helper.GenerateWifiInterferer(channel, interfererNodes.Get(2), 2.462e9 - 10e6,
                                  wifiCh11Start, wifiCh11Duration, 20e6, wifiPsd);
  }

  // ========== Bluetooth Interferers ==========
  NS_LOG_INFO("Generating Bluetooth interferers...");

  // Bluetooth device 1 - active throughout simulation
  double bt1Start = uniformRv->GetValue(0.0, 0.05);
  int bt1Hops = static_cast<int>((simTime - bt1Start) / 625e-6);
  helper.GenerateBluetoothInterferer(channel,
                                     interfererNodes.Get(3),
                                     bt1Start,
                                     TODO,
                                     625e-6,
                                     1e-6,
                                     btPsd,
                                     0x5A,
                                     uniformRv,
                                     jitterRv);

  // Bluetooth device 2 - intermittent activity
  double bt2Start = uniformRv->GetValue(0.3, 0.4);
  int bt2Hops = static_cast<int>(0.3 / 625e-6);
  if (bt2Start < simTime)
  {
    helper.GenerateBluetoothInterferer(channel,
                                         interfererNodes.Get(4),
                                         bt2Start,
                                         TODO,
                                         625e-6,
                                         1e-6,
                                         btPsd,
                                         0x3C,
                                         uniformRv,
                                         jitterRv);
  }

  // Bluetooth device 3 - late activation
  double bt3Start = uniformRv->GetValue(0.6, 0.7);
  int bt3Hops = static_cast<int>((simTime - bt3Start) / 625e-6);
  if (bt3Start < simTime)
  {
    helper.GenerateBluetoothInterferer(channel,
                                         interfererNodes.Get(5),
                                         bt3Start,
                                         TODO,
                                         625e-6,
                                         1e-6,
                                         btPsd,
                                         0x6F,
                                         uniformRv,
                                         jitterRv);
  }

  // ========== ZigBee Interferers ==========
  NS_LOG_INFO("Generating ZigBee interferers...");

  // ZigBee on channel 15 (2.425 GHz) - periodic sensor data
  double zigbee1Start = uniformRv->GetValue(0.05, 0.1);
  int zigbee1Frames = 30;
  helper.GenerateZigbeeInterferer(channel,
                                  interfererNodes.Get(6),
                                  15,
                                  zigbee1Start,
                                  TODO,
                                  zigbeePsd,
                                  uniformRv);

  // ZigBee on channel 20 (2.450 GHz) - bursty communication
  double zigbee2Start = uniformRv->GetValue(0.2, 0.3);
  int zigbee2Frames = 50;
  if (zigbee2Start < simTime)
  {
    helper.GenerateZigbeeInterferer(channel,
                                      interfererNodes.Get(7),
                                      20,
                                      zigbee2Start,
                                      TODO,
                                      zigbeePsd,
                                      uniformRv);
  }

  // ========== Cordless Phone Interferers ==========
  NS_LOG_INFO("Generating cordless phone interferers...");

  double cordlessStart = uniformRv->GetValue(0.15, 0.25);
  int cordlessHops = 100;  // DECT hops every 10 ms
  helper.GenerateCordlessInterferer(channel, interfererNodes.Get(8), cordlessStart,
                                    cordlessHops, 0.01, 0.008, 1.728e6, cordlessPsd);

  // ========== Microwave Oven Interferers ==========
  NS_LOG_INFO("Generating microwave oven interferers...");

  // Microwave with 60 Hz AC modulation pattern (16.67 ms period)
  double microwaveStart = uniformRv->GetValue(0.3, 0.4);

  // Generate pulses: ~8 ms on, 8 ms off
  int numPulses = static_cast<int>((simTime - microwaveStart) / 0.01667);
  for (int i = 0; i < numPulses; i++)
  {
    double start = microwaveStart + i * 0.01667;
    if (start + 0.008 < simTime)
    {
      helper.GenerateMicrowaveInterferer(channel, interfererNodes.Get(9), 2.45e9,
                                        start, 0.008, 100e6, microwavePsd);
    }
  }

  // ========== Setup Spectrum Analyzer ==========
  NS_LOG_INFO("Setting up spectrum analyzer...");

  // Create frequency model for spectrum analyzer
  std::vector<double> freqs;
  freqs.reserve(numFreqBins + 1);
  for (int i = 0; i <= numFreqBins; ++i)
  {
    freqs.push_back(startFreq + i * freqResolution);
  }
  Ptr<SpectrumModel> spectrumModel = Create<SpectrumModel>(freqs);

  // Configure spectrum analyzer
  SpectrumAnalyzerHelper spectrumAnalyzerHelper;
  spectrumAnalyzerHelper.SetChannel(channel);
  spectrumAnalyzerHelper.SetRxSpectrumModel(spectrumModel);
  spectrumAnalyzerHelper.SetPhyAttribute("Resolution", TimeValue(MilliSeconds(1)));

  // Set noise floor
  double noisePowerWatts = std::pow(10.0, (noisePowerDbm - 30.0) / 10.0);
  spectrumAnalyzerHelper.SetPhyAttribute("NoisePowerSpectralDensity",
                                         DoubleValue(noisePowerWatts));

  // Enable ASCII trace output
  spectrumAnalyzerHelper.EnableAsciiAll(traceFile);

  // Install spectrum analyzer
  NetDeviceContainer spectrumAnalyzerDevices =
    spectrumAnalyzerHelper.Install(spectrumAnalyzerNodes);
  
   // ========== Connect Trace Source to Callback ==========
   NS_LOG_INFO("Connecting trace callback for power density logging...");
  
   // Get the node and device IDs
   uint32_t nodeId = spectrumAnalyzerNodes.Get(0)->GetId();
   uint32_t devId = 0; // First device on the node
 
   // Build the Config path
   std::ostringstream oss;
   oss << "/NodeList/" << nodeId 
       << "/DeviceList/" << devId 
       << "/$ns3::NonCommunicatingNetDevice/Phy/AveragePowerSpectralDensityReport";
 
   NS_LOG_INFO("Connecting to path: " << oss.str());
 
   // Connect using Config::ConnectWithoutContext
   Config::ConnectWithoutContext(oss.str(), MakeCallback(&AveragePowerSpectralDensityCallback));
 
   NS_LOG_INFO("Successfully connected trace callback");

  NS_LOG_INFO("Starting simulation...");

  // Run simulation
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  NS_LOG_INFO("Simulation completed");
  NS_LOG_INFO("Writing annotations to " << annotationFile);

  // Write annotations to JSON
  annotationMgr->WriteAnnotationsToJson(annotationFile);

  // Print statistics
  const auto& annotations = annotationMgr->GetAnnotations();
  std::map<std::string, int> counts;
  for (const auto& ann : annotations)
  {
    counts[ann.interfererType]++;
  }

  NS_LOG_INFO("===== Interference Statistics =====");
  for (const auto& pair : counts)
  {
    NS_LOG_INFO("  " << pair.first << ": " << pair.second << " events");
  }
  NS_LOG_INFO("  Total: " << annotations.size() << " events");

  // Cleanup
  Simulator::Destroy();

  NS_LOG_INFO("Trace file written to: " << traceFile << "-0-0.tr");
  NS_LOG_INFO("Annotation file written to: " << annotationFile);

  return 0;
}