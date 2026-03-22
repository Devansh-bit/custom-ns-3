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
 * \file extended-combination-example.cc
 * \brief Systematic simulation of all ISM band interference combinations with randomness
 *
 * This 31-second simulation systematically tests all combinations of 5 technologies
 * with realistic random behavior including channel selection and burst patterns:
 * - Seconds 0-4: Each technology alone (5 combinations, 1 second each)
 * - Seconds 5-14: All pairs of technologies (10 combinations, 1 second each)
 * - Seconds 15-24: All triplets of technologies (10 combinations, 1 second each)
 * - Seconds 25-29: All quadruplets of technologies (5 combinations, 1 second each)
 * - Second 30-31: All five technologies together (1 second)
 *
 * Technologies: WiFi, Bluetooth, ZigBee, Cordless Phone, Microwave Oven
 * All signals constrained to 2.400-2.500 GHz range
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/spectrum-analyzer-helper.h"
#include "ns3/spectrogram-generation.h"
#include "ns3/spectrogram-generation-helper.h"
#include <cmath>
#include <vector>
#include <algorithm>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ExtendedCombinationExample");

// Helper function to generate all k-combinations of n elements
void GenerateCombinations(int n, int k, std::vector<std::vector<int>>& results)
{
  std::vector<int> combination;
  std::vector<bool> selector(n, false);
  std::fill(selector.begin(), selector.begin() + k, true);

  do
  {
    combination.clear();
    for (int i = 0; i < n; ++i)
    {
      if (selector[i])
      {
        combination.push_back(i);
      }
    }
    results.push_back(combination);
  } while (std::prev_permutation(selector.begin(), selector.end()));
}

// Function to activate WiFi with random channel and burst pattern
// Constrained to 2.400-2.500 GHz range
void ActivateWifi(double startTime, double duration, Ptr<SpectrumChannel> channel,
                  Ptr<Node> node, SpectrogramGenerationHelper& helper,
                  Ptr<UniformRandomVariable> uniformRv, double wifiPsd)
{
  // WiFi channels constrained to analyzer range (2.400-2.500 GHz)
  // Channel 1: 2.412 GHz (2.402-2.422 GHz with 20 MHz)
  // Channel 6: 2.437 GHz (2.427-2.447 GHz with 20 MHz)
  // Channel 11: 2.462 GHz (2.452-2.472 GHz with 20 MHz)
  std::vector<double> wifiChannels = {2.412e9, 2.417e9, 2.422e9, 2.427e9, 2.432e9, 2.437e9, 2.442e9, 2.447e9, 2.452e9, 2.457e9, 2.462e9};
    double frame_size = 0.1;


  // Random burst pattern: 40% continuous, 60% bursty
    for (int idx=0; idx < duration / frame_size; idx++)
      if (true)
      {
        int channelIdx = uniformRv->GetInteger(0, 10);
        double frequency = wifiChannels[channelIdx];
        // Continuous transmission with small gap at start
        double start = startTime + idx * frame_size;
        double dur = frame_size;
        helper.GenerateWifiInterferer(channel, node, frequency - 10e6,
                                     start, dur, 20e6, wifiPsd);
      }
      // else
      // {
      //   // Bursty transmission: 3-7 bursts
      //   int numBursts = uniformRv->GetInteger(3, 7);
      //   double timePerBurst = duration / numBursts;
      //
      //   for (int i = 0; i < numBursts; i++)
      //   {
      //     int channelIdx = uniformRv->GetInteger(0, 10);
      //     double frequency = wifiChannels[channelIdx];
      //     double burstStart = startTime + i * timePerBurst + uniformRv->GetValue(0.0, 0.05);
      //     double burstDuration = uniformRv->GetValue(0.03, timePerBurst * 0.7);
      //
      //     if (burstStart + burstDuration < startTime + duration)
      //     {
      //       helper.GenerateWifiInterferer(channel, node, frequency - 10e6,
      //                                    burstStart, burstDuration, 20e6, wifiPsd);
      //     }
      //   }
      // }
}

// Function to activate Bluetooth with random hopping pattern
// Bluetooth naturally operates in 2.402-2.480 GHz (79 channels @ 1 MHz)
void ActivateBluetooth(double startTime, double duration, Ptr<SpectrumChannel> channel,
                       Ptr<Node> node, SpectrogramGenerationHelper& helper,
                       Ptr<UniformRandomVariable> uniformRv, Ptr<NormalRandomVariable> jitterRv,
                       double btPsd)
{
  // Random hopping seed for variety
  uint8_t hoppingSeed = uniformRv->GetInteger(0, 255);

  // Bluetooth can be continuous or have idle periods
  if (true)
  {
    // Continuous hopping
    int btHops = static_cast<int>(duration / 625e-6);
    helper.GenerateBluetoothInterferer(channel,
                                       node,
                                       startTime,
                                       btHops,
                                       625e-6,
                                       1e-6,
                                       btPsd,
                                       hoppingSeed,
                                       uniformRv,
                                       jitterRv);
  }
  else
  {
    // Intermittent hopping: 2-4 active periods
    int numPeriods = uniformRv->GetInteger(2, 4);
    double timePerPeriod = duration / numPeriods;

    for (int i = 0; i < numPeriods; i++)
    {
      double periodStart = startTime + i * timePerPeriod + uniformRv->GetValue(0.0, 0.05);
      double periodDuration = uniformRv->GetValue(timePerPeriod * 0.4, timePerPeriod * 0.7);

      if (periodStart + periodDuration < startTime + duration)
      {
        int hops = static_cast<int>(periodDuration / 625e-6);
        helper.GenerateBluetoothInterferer(channel,
                                           node,
                                           periodStart,
                                           hops,
                                           625e-6,
                                           1e-6,
                                           btPsd,
                                           hoppingSeed,
                                           uniformRv,
                                           jitterRv);
      }
    }
  }
}

// Function to activate ZigBee with random channel
// ZigBee channels 11-26 correspond to 2.405-2.480 GHz (all within range)
void ActivateZigbee(double startTime, double duration, Ptr<SpectrumChannel> channel,
                    Ptr<Node> node, SpectrogramGenerationHelper& helper,
                    Ptr<UniformRandomVariable> uniformRv, double zigbeePsd)
{
  // Divide duration into 5 sub-periods for channel hopping
  int numChannelHops = 5;
  double hopDuration = duration / numChannelHops;

  // Track used channels to ensure variety
  std::vector<int> usedChannels;

  for (int hop = 0; hop < numChannelHops; hop++)
  {
    // Select a random ZigBee channel (11-26), preferring unused channels
    int zigbeeChannel;
    int attempts = 0;
    do {
      zigbeeChannel = uniformRv->GetInteger(11, 26);
      attempts++;
      // After 10 attempts, accept any channel (allows repeats if needed)
    } while (attempts < 10 &&
             std::find(usedChannels.begin(), usedChannels.end(), zigbeeChannel) != usedChannels.end());

    usedChannels.push_back(zigbeeChannel);

    // Calculate time window for this hop
    double hopStart = startTime + hop * hopDuration;

    // ZigBee has variable transmission patterns for each hop
    double txPattern = uniformRv->GetValue();

    if (txPattern < 0.3)
    {
      // Low duty cycle: periodic sensor readings (4-8 frames per hop)
      int numFrames = uniformRv->GetInteger(4, 8);
      double interval = uniformRv->GetValue(0.025, 0.040);
      helper.GenerateZigbeeInterferer(channel,
                                      node,
                                      zigbeeChannel,
                                      hopStart,
                                      numFrames,
                                      zigbeePsd,
                                      uniformRv);
    }
    else if (txPattern < 0.7)
    {
      // Medium duty cycle: moderate traffic (8-14 frames per hop)
      int numFrames = uniformRv->GetInteger(8, 14);
      double interval = uniformRv->GetValue(0.015, 0.025);
      helper.GenerateZigbeeInterferer(channel,
                                      node,
                                      zigbeeChannel,
                                      hopStart,
                                      numFrames,
                                      zigbeePsd,
                                      uniformRv);
    }
    else
    {
      // High duty cycle: intensive communication (14-20 frames per hop)
      int numFrames = uniformRv->GetInteger(14, 20);
      double interval = uniformRv->GetValue(0.008, 0.015);
      helper.GenerateZigbeeInterferer(channel,
                                      node,
                                      zigbeeChannel,
                                      hopStart,
                                      numFrames,
                                      zigbeePsd,
                                      uniformRv);
    }
  }
}

// Function to activate Cordless Phone with random frequency
// DECT operates in 2.402-2.480 GHz (79 channels @ 1 MHz, same as Bluetooth)
void ActivateCordless(double startTime, double duration, Ptr<SpectrumChannel> channel,
                      Ptr<Node> node, SpectrogramGenerationHelper& helper,
                      Ptr<UniformRandomVariable> uniformRv, double cordlessPsd)
{
  // DECT operates across 2.4 GHz band (2.402-2.480 GHz naturally fits)

  // Variable hop duration (8-12 ms typical)
  double hopDuration = uniformRv->GetValue(0.008, 0.012);

  // Variable active time within hop (60-90%)
  double activeFraction = uniformRv->GetValue(0.6, 0.9);
  double activeDuration = hopDuration * activeFraction;

  int numHops = static_cast<int>(duration / hopDuration);

  helper.GenerateCordlessInterferer(channel, node, startTime, numHops,
                                   hopDuration, activeDuration, 1.728e6, cordlessPsd);
}

// Function to activate Microwave Oven with random duty cycle
// Constrained to stay within 2.400-2.500 GHz range (ISM band)
void ActivateMicrowave(double startTime, double duration, Ptr<SpectrumChannel> channel,
                       Ptr<Node> node, SpectrogramGenerationHelper& helper,
                       Ptr<UniformRandomVariable> uniformRv, double microwavePsd)
{

  double bandwidth = 100e6;


  double centerFreq = 2.450e9;

  // AC cycle period (16.67 ms for 60 Hz, 20 ms for 50 Hz)
  double acPeriod = (uniformRv->GetValue() < 0.5) ? 0.01667 : 0.020;

  // Duty cycle variation: 40-60% on-time
  double dutyCycle = uniformRv->GetValue(0.4, 0.6);
  double pulseDuration = acPeriod * dutyCycle;

  // Generate pulses synchronized with AC
  int numPulses = static_cast<int>(duration / acPeriod);

  for (int i = 0; i < numPulses; i++)
  {
    double pulseStart = startTime + i * acPeriod;
    if (pulseStart + pulseDuration < startTime + duration)
    {
      helper.GenerateMicrowaveInterferer(channel, node, centerFreq, pulseStart,
                                        pulseDuration, bandwidth, microwavePsd);
    }
  }
}

// Function to activate interferers for a specific time period with randomness
void ActivateInterferers(const std::vector<int>& activeTechs,
                         double startTime,
                         double duration,
                         Ptr<SpectrumChannel> channel,
                         NodeContainer& interfererNodes,
                         SpectrogramGenerationHelper& helper,
                         Ptr<UniformRandomVariable> uniformRv,
                         Ptr<NormalRandomVariable> jitterRv,
                         const double wifiPsd,
                         const double btPsd,
                         const double zigbeePsd,
                         const double cordlessPsd,
                         const double microwavePsd)
{
  for (int tech : activeTechs)
  {
    switch (tech)
    {
      case 0: // WiFi
        ActivateWifi(startTime, duration, channel, interfererNodes.Get(tech),
                    helper, uniformRv, wifiPsd);
        break;

      case 1: // Bluetooth
        ActivateBluetooth(startTime, duration, channel, interfererNodes.Get(tech),
                         helper, uniformRv, jitterRv, btPsd);
        break;

      case 2: // ZigBee
        ActivateZigbee(startTime, duration, channel, interfererNodes.Get(tech),
                      helper, uniformRv, zigbeePsd);
        break;

      case 3: // Cordless Phone
        ActivateCordless(startTime, duration, channel, interfererNodes.Get(tech),
                        helper, uniformRv, cordlessPsd);
        break;

      case 4: // Microwave Oven
        ActivateMicrowave(startTime, duration, channel, interfererNodes.Get(tech),
                         helper, uniformRv, microwavePsd);
        break;
    }
  }
}

int
main(int argc, char *argv[])
{
  // Simulation parameters
  double simTime = 31.0;  // 31 second simulation
  std::string traceFile = "extended-combination-example";
  std::string annotationFile = "extended-combination-annotations.json";
  uint32_t rngSeed = 42;

  // Spectrum analyzer parameters
  double noisePowerDbm = -140.0;
  double startFreq = 2.400e9;
  double freqResolution = 100e3;  // 100 kHz resolution
  int numFreqBins = 1000;  // 100 MHz span (2.400-2.500 GHz)

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
  LogComponentEnable("ExtendedCombinationExample", LOG_LEVEL_INFO);

  NS_LOG_INFO("Creating Extended ISM Band Interference Combination Simulation");
  NS_LOG_INFO("Simulation time: " << simTime << " seconds");
  NS_LOG_INFO("Testing all combinations with realistic random behavior");
  NS_LOG_INFO("Frequency range: 2.400-2.500 GHz (all signals constrained)");

  // Create spectrum channel
  SpectrumChannelHelper channelHelper = SpectrumChannelHelper::Default();
  channelHelper.SetChannel("ns3::MultiModelSpectrumChannel");
  channelHelper.AddSpectrumPropagationLoss("ns3::FriisSpectrumPropagationLossModel");
  Ptr<SpectrumChannel> channel = channelHelper.Create();

  // Create annotation manager
  Ptr<AnnotationManager> annotationMgr = CreateObject<AnnotationManager>();

  // Create helper
  SpectrogramGenerationHelper helper(annotationMgr);

  // Create nodes (5 interferers + 1 spectrum analyzer)
  NodeContainer interfererNodes;
  NodeContainer spectrumAnalyzerNodes;
  interfererNodes.Create(5);  // One node per technology
  spectrumAnalyzerNodes.Create(1);

  NodeContainer allNodes;
  allNodes.Add(interfererNodes);
  allNodes.Add(spectrumAnalyzerNodes);

  // Setup mobility - random positions for each combination
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

  Ptr<UniformRandomVariable> positionRv = CreateObject<UniformRandomVariable>();

  // Place interferer nodes at random positions (8-15m from origin)
  for (uint32_t i = 0; i < interfererNodes.GetN(); ++i)
  {
    double distance = positionRv->GetValue(8.0, 15.0);
    double angle = positionRv->GetValue(0.0, 2 * M_PI);
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
  Ptr<UniformRandomVariable> uniformRv = CreateObject<UniformRandomVariable>();
  Ptr<NormalRandomVariable> jitterRv = CreateObject<NormalRandomVariable>();
  jitterRv->SetAttribute("Mean", DoubleValue(0.0));
  jitterRv->SetAttribute("Variance", DoubleValue(1e-12));

  // Realistic power levels (W/Hz) with some variation
  const double wifiPsd = 1e-3;      // ~-30 dBm/Hz
  const double btPsd = 1e-5;        // ~-50 dBm/Hz
  const double zigbeePsd = 5e-6;    // ~-53 dBm/Hz
  const double cordlessPsd = 5e-4;  // ~-33 dBm/Hz
  const double microwavePsd = 1e-2; // ~-20 dBm/Hz

  // Technology names for logging
  std::vector<std::string> techNames = {"WiFi", "Bluetooth", "ZigBee",
                                        "Cordless", "Microwave"};

  // Generate all combinations
  double currentTime = 0.0;
  double slotDuration = 1.0;  // 1 second per combination

  // ========== Individual Technologies (k=1, 5 combinations) ==========
  NS_LOG_INFO("===== Phase 1: Individual Technologies (0-5s) =====");
  for (int tech = 0; tech < 5; tech++)
  {
    std::vector<int> activeTechs = {tech};
    NS_LOG_INFO("Time " << currentTime << "s: Activating " << techNames[tech]);

    ActivateInterferers(activeTechs, currentTime, slotDuration, channel,
                       interfererNodes, helper, uniformRv, jitterRv,
                       wifiPsd, btPsd, zigbeePsd, cordlessPsd, microwavePsd);

    currentTime += slotDuration;
  }

  // ========== Pairs of Technologies (k=2, 10 combinations) ==========
  NS_LOG_INFO("===== Phase 2: Technology Pairs (5-15s) =====");
  std::vector<std::vector<int>> pairs;
  GenerateCombinations(5, 2, pairs);

  for (const auto& pair : pairs)
  {
    std::string techList = techNames[pair[0]] + " + " + techNames[pair[1]];
    NS_LOG_INFO("Time " << currentTime << "s: Activating " << techList);

    ActivateInterferers(pair, currentTime, slotDuration, channel,
                       interfererNodes, helper, uniformRv, jitterRv,
                       wifiPsd, btPsd, zigbeePsd, cordlessPsd, microwavePsd);

    currentTime += slotDuration;
  }

  // ========== Triplets of Technologies (k=3, 10 combinations) ==========
  NS_LOG_INFO("===== Phase 3: Technology Triplets (15-25s) =====");
  std::vector<std::vector<int>> triplets;
  GenerateCombinations(5, 3, triplets);

  for (const auto& triplet : triplets)
  {
    std::string techList = techNames[triplet[0]] + " + " +
                          techNames[triplet[1]] + " + " +
                          techNames[triplet[2]];
    NS_LOG_INFO("Time " << currentTime << "s: Activating " << techList);

    ActivateInterferers(triplet, currentTime, slotDuration, channel,
                       interfererNodes, helper, uniformRv, jitterRv,
                       wifiPsd, btPsd, zigbeePsd, cordlessPsd, microwavePsd);

    currentTime += slotDuration;
  }

  // ========== Quadruplets of Technologies (k=4, 5 combinations) ==========
  NS_LOG_INFO("===== Phase 4: Technology Quadruplets (25-30s) =====");
  std::vector<std::vector<int>> quadruplets;
  GenerateCombinations(5, 4, quadruplets);

  for (const auto& quad : quadruplets)
  {
    std::string techList = techNames[quad[0]] + " + " +
                          techNames[quad[1]] + " + " +
                          techNames[quad[2]] + " + " +
                          techNames[quad[3]];
    NS_LOG_INFO("Time " << currentTime << "s: Activating " << techList);

    ActivateInterferers(quad, currentTime, slotDuration, channel,
                       interfererNodes, helper, uniformRv, jitterRv,
                       wifiPsd, btPsd, zigbeePsd, cordlessPsd, microwavePsd);

    currentTime += slotDuration;
  }

  // ========== All Technologies (k=5, 1 combination) ==========
  NS_LOG_INFO("===== Phase 5: All Technologies Together (30-31s) =====");
  std::vector<int> allTechs = {0, 1, 2, 3, 4};
  NS_LOG_INFO("Time " << currentTime << "s: Activating ALL technologies");

  ActivateInterferers(allTechs, currentTime, slotDuration, channel,
                     interfererNodes, helper, uniformRv, jitterRv,
                     wifiPsd, btPsd, zigbeePsd, cordlessPsd, microwavePsd);

  // ========== Setup Spectrum Analyzer ==========
  NS_LOG_INFO("Setting up spectrum analyzer...");

  // Create frequency model
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

  NS_LOG_INFO("Starting simulation...");

  // Run simulation
  Simulator::Stop(Seconds(simTime+5));
  Simulator::Run();

  NS_LOG_INFO("Simulation completed");
  NS_LOG_INFO("Writing annotations to " << annotationFile);

  // Write annotations to JSON
  annotationMgr->WriteAnnotationsToJson(annotationFile);

  // Print statistics



  NS_LOG_INFO("===== Combination Summary =====");
  NS_LOG_INFO("  Phase 1 (Singles):     5 combinations x 1s = 5s");
  NS_LOG_INFO("  Phase 2 (Pairs):      10 combinations x 1s = 10s");
  NS_LOG_INFO("  Phase 3 (Triplets):   10 combinations x 1s = 10s");
  NS_LOG_INFO("  Phase 4 (Quadruplets): 5 combinations x 1s = 5s");
  NS_LOG_INFO("  Phase 5 (All Five):    1 combination  x 1s = 1s");
  NS_LOG_INFO("  Total: 31 combinations in 31 seconds");
  NS_LOG_INFO("");
  NS_LOG_INFO("NOTE: Each combination uses randomized channels and burst patterns");
  NS_LOG_INFO("      All signals constrained to 2.400-2.500 GHz spectrum analyzer range");

  // Cleanup
  Simulator::Destroy();

  NS_LOG_INFO("Trace file written to: " << traceFile << "-0-0.tr");
  NS_LOG_INFO("Annotation file written to: " << annotationFile);

  return 0;
}