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

void ActivateMicrowave(double startTime, double duration, Ptr<SpectrumChannel> channel, double startFreq, double bandwidth,
                       Ptr<Node> node, SpectrogramGenerationHelper& helper,
                       Ptr<UniformRandomVariable> uniformRv, double microwavePsd)
{

    double centerFreq = startFreq + bandwidth/2;


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

// Function to activate WiFi with random channel and burst pattern
// Constrained to 2.400-2.500 GHz range
void Activate5GWifi(double startTime, double duration, Ptr<SpectrumChannel> channel, double frequencyStart, double bandwidth,
                  Ptr<Node> node, SpectrogramGenerationHelper& helper,
                  Ptr<UniformRandomVariable> uniformRv, double wifiPsd)
{
  // Random burst pattern: 40% continuous, 60% bursty
  if (uniformRv->GetValue() < 0.4)
  {
    // Continuous transmission with small gap at start
    double start = startTime + uniformRv->GetValue(0.0, 0.05);
    double dur = duration - (start - startTime) - uniformRv->GetValue(0.0, 0.05);
    helper.GenerateWifiInterferer(channel, node, frequencyStart,
                                 start, dur, bandwidth, wifiPsd);
  }
  else
  {
    // Bursty transmission: 3-7 bursts
    int numBursts = uniformRv->GetInteger(3, 7);
    double timePerBurst = duration / numBursts;

    for (int i = 0; i < numBursts; i++)
    {
      double burstStart = startTime + i * timePerBurst + uniformRv->GetValue(0.0, 0.05);
      double burstDuration = uniformRv->GetValue(0.03, timePerBurst * 0.7);

      if (burstStart + burstDuration < startTime + duration)
      {
        helper.GenerateWifiInterferer(channel, node, frequencyStart,
                                     burstStart, burstDuration, bandwidth, wifiPsd);
      }
    }
  }
}

int
main(int argc, char *argv[])
{
    // Simulation parameters
    double simTime = 3.0;  // 31 second simulation
    std::string traceFile = "extended-combination-example";
    std::string annotationFile = "extended-combination-annotations.json";
    uint32_t rngSeed = 42;

    // Spectrum analyzer parameters
    double noisePowerDbm = -140.0;
    double startFreq = 5.000e9;
    double freqResolution = 100e3;  // 100 kHz resolution
    int numFreqBins = 10000;  // 100 MHz span (2.400-2.500 GHz)

    Ptr<UniformRandomVariable> uniformRv = CreateObject<UniformRandomVariable>();
    Ptr<NormalRandomVariable> jitterRv = CreateObject<NormalRandomVariable>();

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
    LogComponentEnable("ExtendedCombinationExample", LOG_LEVEL_ALL);

    NS_LOG_INFO("Simulation time: " << simTime << " seconds");

    // Create spectrum channel
    SpectrumChannelHelper channelHelper = SpectrumChannelHelper::Default();
    channelHelper.SetChannel("ns3::MultiModelSpectrumChannel");
    channelHelper.AddSpectrumPropagationLoss("ns3::FriisSpectrumPropagationLossModel");
    Ptr<SpectrumChannel> channel = channelHelper.Create();

    // Create annotation manager
    Ptr<AnnotationManager> annotationMgr = CreateObject<AnnotationManager>();

    // Create helper
    SpectrogramGenerationHelper helper(annotationMgr);

    // Create nodes (2 interferers + 1 spectrum analyzer)
    NodeContainer interfererNodes;
    NodeContainer spectrumAnalyzerNodes;
    interfererNodes.Create(2);
    spectrumAnalyzerNodes.Create(1);

    NodeContainer allNodes;
    allNodes.Add(interfererNodes);
    allNodes.Add(spectrumAnalyzerNodes);

    // Setup mobility - random positions for each combination
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(10, 10, 0.0));
    positionAlloc->Add(Vector(-10, -10, 0.0));
    positionAlloc->Add(Vector(0, 0, 0.0));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allNodes);

    const double wifiPsd = 1e-7;      // ~-30 dBm/Hz
    const double microwavePsd = 1e-2; // ~-20 dBm/Hz
    std::vector<double> wifi5ghzchannels = {5.150e9, 5.250e9, 5.350e9, 5.470e9, 5.725e9};
    std::vector<double> bandwidths = {100e6, 100e6, 120e6, 5.725e9-5.470e9, 5.850e9-5.725e9};

    for (int i=0; i<10; i++)
    {
        int idx = uniformRv->GetInteger(0, 4);
        Activate5GWifi(i*0.3, (i+1)*0.3, channel, wifi5ghzchannels[idx], bandwidths[idx], interfererNodes.Get(0), helper, uniformRv, wifiPsd);
    }
    ActivateMicrowave(1.5, 1.5, channel, 5.000e9, 6.000e9, interfererNodes.Get(1), helper, uniformRv, microwavePsd);

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
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    annotationMgr->WriteAnnotationsToJson(annotationFile);


}