#include "spectrogram-generation.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/tv-spectrum-transmitter-helper.h"
#include "ns3/tv-spectrum-transmitter.h"
#include "ns3/non-communicating-net-device.h"
#include "ns3/isotropic-antenna-model.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SpectrogramGeneration");

// -------------------- Lfsr7 Implementation --------------------

Lfsr7::Lfsr7(uint8_t seed)
  : state(seed & 0x7F)
{
  if (state == 0)
  {
    state = 0x5A;
  }
}

uint8_t
Lfsr7::Next()
{
  uint8_t bit6 = (state >> 6) & 1;
  uint8_t bit3 = (state >> 3) & 1;
  uint8_t feedback = bit6 ^ bit3;
  state = ((state << 1) & 0x7E) | feedback;
  return state;
}

// -------------------- AnnotationManager Implementation --------------------

NS_OBJECT_ENSURE_REGISTERED(AnnotationManager);

TypeId
AnnotationManager::GetTypeId()
{
  static TypeId tid = TypeId("ns3::AnnotationManager")
    .SetParent<Object>()
    .SetGroupName("Spectrum")
    .AddConstructor<AnnotationManager>();
  return tid;
}

AnnotationManager::AnnotationManager()
{
  NS_LOG_FUNCTION(this);
}

AnnotationManager::~AnnotationManager()
{
  NS_LOG_FUNCTION(this);
}

void
AnnotationManager::AddAnnotation(const std::string& type, double start, double duration,
                                  double centerFreq, double bw)
{
  InterferenceAnnotation ann;
  ann.interfererType = type;
  ann.startTime = start;
  ann.endTime = start + duration;
  ann.centralFrequency = centerFreq;
  ann.bandwidth = bw;
  m_annotations.push_back(ann);
}

void
AnnotationManager::AddDutyAnnotation(const std::string& type, double start, double duration, double dutyPeriod, double dutyRatio,
                                  double centerFreq, double bw)
{
    DutyInterferenceAnnotation ann;
    ann.interfererType = type;
    ann.startTime = start;
    ann.endTime = start + duration;
    ann.dutyRatio = dutyRatio;
    ann.dutyPeriod = dutyPeriod;
    ann.centralFrequency = centerFreq;
    ann.bandwidth = bw;
    m_duty_annotations.push_back(ann);
}

void
AnnotationManager::WriteAnnotationsToJson(const std::string& filename)
{
  NS_LOG_FUNCTION(this << filename);

  std::ofstream outFile(filename);
  if (!outFile.is_open())
  {
    NS_LOG_ERROR("Could not open " << filename << " for writing");
    return;
  }

  outFile << "{\n";
  outFile << "  \"annotations\": [\n";

  for (size_t i = 0; i < m_annotations.size(); ++i)
  {
    const auto& ann = m_annotations[i];
    outFile << "    {\n";
    outFile << "      \"interferer_type\": \"" << ann.interfererType << "\",\n";
    outFile << "      \"start_time\": " << ann.startTime << ",\n";
    outFile << "      \"end_time\": " << ann.endTime << ",\n";
    outFile << "      \"central_frequency\": " << ann.centralFrequency << ",\n";
    outFile << "      \"bandwidth\": " << ann.bandwidth << "\n";
    outFile << "    }";

    if (i < m_annotations.size() - 1)
      outFile << ",";
    outFile << "\n";
  }

    for (size_t i = 0; i < m_duty_annotations.size(); ++i)
    {
        const auto& ann = m_duty_annotations[i];
        outFile << "    {\n";
        outFile << "      \"interferer_type\": \"" << ann.interfererType << "\",\n";
        outFile << "      \"start_time\": " << ann.startTime << ",\n";
        outFile << "      \"end_time\": " << ann.endTime << ",\n";
        outFile << "      \"duty_period\": " << ann.dutyPeriod << ",\n";
        outFile << "      \"duty_ratio\": " << ann.dutyRatio << ",\n";
        outFile << "      \"central_frequency\": " << ann.centralFrequency << ",\n";
        outFile << "      \"bandwidth\": " << ann.bandwidth << "\n";
        outFile << "    }";

        if (i < m_duty_annotations.size() - 1)
            outFile << ",";
        outFile << "\n";
    }

  outFile << "  ],\n";
  outFile << "  \"total_count\": " << m_annotations.size() + m_duty_annotations.size() << "\n";
  outFile << "}\n";

  outFile.close();
  NS_LOG_INFO("Annotations written to: " << filename);
  NS_LOG_INFO("Total annotations: " << m_annotations.size() + m_duty_annotations.size());
}

const std::vector<InterferenceAnnotation>&
AnnotationManager::GetAnnotations() const
{
  return m_annotations;
}

const std::vector<DutyInterferenceAnnotation>&
AnnotationManager::GetDutyAnnotations() const
{
    return m_duty_annotations;
}

void
AnnotationManager::Clear()
{
  m_annotations.clear();
    m_duty_annotations.clear();
}

// -------------------- TimingHelper Implementation --------------------

double
TimingHelper::BlePktDurationSeconds_1M(int payloadBytes)
{
  const int fixedBits = 8 + 32 + 16 + 24;
  return (fixedBits + 8 * payloadBytes) * 1e-6;
}

double
TimingHelper::ZigbeeChannelCenterHz(int ch)
{
  return (2405.0 + 5.0 * (ch - 11)) * 1e6;
}

double
TimingHelper::ZigbeePktDurationSeconds_2450(int payloadBytes)
{
  return (payloadBytes + 8) * 32e-6;
}

// ============================================================================
// LAZY INTERFERERS IMPLEMENTATION
// ============================================================================

// -------------------- LazyInterferer Base Class --------------------

NS_OBJECT_ENSURE_REGISTERED(LazyInterferer);

TypeId
LazyInterferer::GetTypeId()
{
  static TypeId tid = TypeId("ns3::LazyInterferer")
    .SetParent<Object>()
    .SetGroupName("Spectrum");
  return tid;
}

LazyInterferer::LazyInterferer()
  : m_channel(nullptr),
    m_node(nullptr),
    m_endTime(Seconds(0)),
    m_startTime(Seconds(0)),
    m_basePsd(-30.0),
    m_running(false),
    m_hasSchedule(false),
    m_onDuration(Seconds(0)),
    m_offDuration(Seconds(0)),
    m_inOnPhase(true),
    m_phaseStartTime(Seconds(0)),
    m_txPsd(nullptr),
    m_spectrumModel(nullptr),
    m_antenna(nullptr),
    m_txPhy(nullptr),
    m_currentStartFreq(0),
    m_currentBandwidth(0)
{
  NS_LOG_FUNCTION(this);
}

LazyInterferer::~LazyInterferer()
{
  NS_LOG_FUNCTION(this);
  Stop();
}

void
LazyInterferer::Start()
{
  NS_LOG_FUNCTION(this);
  if (!m_running && m_channel && m_node)
  {
    m_running = true;
    // Initialize phase tracking
    m_inOnPhase = true;
    m_phaseStartTime = m_startTime;

    // Schedule first transmission at start time
    Time delay = m_startTime - Simulator::Now();
    if (delay.IsNegative())
    {
      delay = Seconds(0);
    }
    Simulator::Schedule(delay, &LazyInterferer::ScheduleNext, this);

    // If schedule is configured, schedule first phase transition
    if (m_hasSchedule)
    {
      Simulator::Schedule(delay + m_onDuration, &LazyInterferer::HandlePhaseTransition, this);
    }
  }
}

void
LazyInterferer::Stop()
{
  NS_LOG_FUNCTION(this);
  m_running = false;
}

void
LazyInterferer::SetSchedule(Time onDuration, Time offDuration)
{
  NS_LOG_FUNCTION(this << onDuration << offDuration);
  m_hasSchedule = true;
  m_onDuration = onDuration;
  m_offDuration = offDuration;
  m_inOnPhase = true;  // Start in ON phase
  NS_LOG_INFO("Schedule configured: ON=" << onDuration.GetSeconds()
              << "s, OFF=" << offDuration.GetSeconds() << "s");
}

bool
LazyInterferer::IsInOnPhase() const
{
  // If no schedule configured, always in ON phase
  if (!m_hasSchedule)
  {
    return true;
  }
  return m_inOnPhase;
}

void
LazyInterferer::HandlePhaseTransition()
{
  NS_LOG_FUNCTION(this);
  if (!m_running || Simulator::Now() >= m_endTime)
  {
    return;
  }

  // Toggle phase
  m_inOnPhase = !m_inOnPhase;
  m_phaseStartTime = Simulator::Now();

  Time nextPhaseDuration = m_inOnPhase ? m_onDuration : m_offDuration;

  NS_LOG_DEBUG("Phase transition at t=" << Simulator::Now().GetSeconds()
               << "s -> " << (m_inOnPhase ? "ON" : "OFF")
               << " for " << nextPhaseDuration.GetSeconds() << "s");

  // Schedule next phase transition
  if (Simulator::Now() + nextPhaseDuration < m_endTime)
  {
    Simulator::Schedule(nextPhaseDuration, &LazyInterferer::HandlePhaseTransition, this);
  }
}

void
LazyInterferer::InitTransmitter(double startFreq, double bandwidth)
{
  NS_LOG_FUNCTION(this << startFreq << bandwidth);

  m_currentStartFreq = startFreq;
  m_currentBandwidth = bandwidth;

  // Create spectrum model with 100 sub-bands (same as TvSpectrumTransmitter)
  Bands bands;
  double halfSubBand = 0.5 * (bandwidth / 100);
  for (double fl = startFreq - halfSubBand;
       fl <= (startFreq - halfSubBand) + bandwidth;
       fl += bandwidth / 100)
  {
    BandInfo bi;
    bi.fl = fl;
    bi.fc = fl + halfSubBand;
    bi.fh = fl + (2 * halfSubBand);
    bands.push_back(bi);
  }
  m_spectrumModel = Create<SpectrumModel>(bands);

  // Create reusable PSD
  m_txPsd = Create<SpectrumValue>(m_spectrumModel);

  // Create reusable antenna
  m_antenna = CreateObject<IsotropicAntennaModel>();

  // Create a lightweight TvSpectrumTransmitter as our txPhy
  // This is needed because StartTx requires a valid txPhy pointer
  Ptr<TvSpectrumTransmitter> tvTx = CreateObject<TvSpectrumTransmitter>();
  tvTx->SetChannel(m_channel);
  tvTx->SetMobility(m_node->GetObject<MobilityModel>());

  // Create a NonCommunicatingNetDevice and set it on the txPhy
  // This is required for receivers to check the source device
  Ptr<NonCommunicatingNetDevice> dev = CreateObject<NonCommunicatingNetDevice>();
  dev->SetNode(m_node);
  dev->SetPhy(tvTx);
  m_node->AddDevice(dev);
  tvTx->SetDevice(dev);

  m_txPhy = tvTx;
}

void
LazyInterferer::UpdateFrequency(double startFreq, double bandwidth)
{
  NS_LOG_FUNCTION(this << startFreq << bandwidth);

  // Only recreate spectrum model if frequency/bandwidth changed significantly
  if (m_spectrumModel == nullptr ||
      std::abs(startFreq - m_currentStartFreq) > 1.0 ||
      std::abs(bandwidth - m_currentBandwidth) > 1.0)
  {
    m_currentStartFreq = startFreq;
    m_currentBandwidth = bandwidth;

    // Recreate spectrum model and PSD only (not the device/phy)
    Bands bands;
    double halfSubBand = 0.5 * (bandwidth / 100);
    for (double fl = startFreq - halfSubBand;
         fl <= (startFreq - halfSubBand) + bandwidth;
         fl += bandwidth / 100)
    {
      BandInfo bi;
      bi.fl = fl;
      bi.fc = fl + halfSubBand;
      bi.fh = fl + (2 * halfSubBand);
      bands.push_back(bi);
    }
    m_spectrumModel = Create<SpectrumModel>(bands);
    m_txPsd = Create<SpectrumValue>(m_spectrumModel);

    // If txPhy not yet created, do full init
    if (m_txPhy == nullptr)
    {
      InitTransmitter(startFreq, bandwidth);
    }
  }
}

void
LazyInterferer::TransmitDirect(Time duration, double psdWattsHz)
{
  NS_LOG_FUNCTION(this << duration << psdWattsHz);

  if (!m_txPsd || !m_channel)
  {
    NS_LOG_WARN("TransmitDirect called before InitTransmitter");
    return;
  }

  // Fill PSD with flat power (COFDM-like, simpler than TV shapes)
  for (auto it = m_txPsd->ValuesBegin(); it != m_txPsd->ValuesEnd(); ++it)
  {
    *it = psdWattsHz;
  }

  // Create signal parameters (lightweight, short-lived)
  Ptr<SpectrumSignalParameters> signal = Create<SpectrumSignalParameters>();
  signal->duration = duration;
  signal->psd = m_txPsd;
  signal->txPhy = m_txPhy;
  signal->txAntenna = m_antenna;
  signal->txMobility = m_node->GetObject<MobilityModel>();

  // Transmit directly on channel
  m_channel->StartTx(signal);
}

// -------------------- LazyBluetoothInterferer --------------------

NS_OBJECT_ENSURE_REGISTERED(LazyBluetoothInterferer);

TypeId
LazyBluetoothInterferer::GetTypeId()
{
  static TypeId tid = TypeId("ns3::LazyBluetoothInterferer")
    .SetParent<LazyInterferer>()
    .SetGroupName("Spectrum")
    .AddConstructor<LazyBluetoothInterferer>();
  return tid;
}

LazyBluetoothInterferer::LazyBluetoothInterferer()
  : m_hopInterval(MicroSeconds(625)),
    m_lfsr(0x5A)
{
  NS_LOG_FUNCTION(this);
  m_uni = CreateObject<UniformRandomVariable>();
}

LazyBluetoothInterferer::~LazyBluetoothInterferer()
{
  NS_LOG_FUNCTION(this);
}

void
LazyBluetoothInterferer::Configure(Ptr<SpectrumChannel> channel,
                                    Ptr<Node> node,
                                    Time startTime,
                                    Time duration,
                                    Time hopInterval,
                                    double basePsd,
                                    uint8_t lfsrSeed)
{
  NS_LOG_FUNCTION(this << channel << node << startTime << duration);
  m_channel = channel;
  m_node = node;
  m_startTime = startTime;
  m_endTime = startTime + duration;
  m_hopInterval = hopInterval;
  m_basePsd = basePsd;
  m_lfsr = Lfsr7(lfsrSeed);

  // Initialize transmitter with BLE bandwidth (1 MHz)
  // We'll update frequency for each hop
  const double bleBwHz = 1e6;
  InitTransmitter(2.402e9, bleBwHz);
}

void
LazyBluetoothInterferer::ScheduleNext()
{
  NS_LOG_FUNCTION(this);
  if (!m_running || Simulator::Now() >= m_endTime)
  {
    m_running = false;
    return;
  }
  // Only transmit during ON phase (schedule cycling)
  if (IsInOnPhase())
  {
    DoTransmit();
  }
  // Schedule next hop (continue scheduling even during OFF phase)
  Simulator::Schedule(m_hopInterval, &LazyBluetoothInterferer::ScheduleNext, this);
}

void
LazyBluetoothInterferer::DoTransmit()
{
  NS_LOG_FUNCTION(this);
  const double bleStartFreq = 2.402e9;
  const double bleStepHz = 1e6;
  const double bleBwHz = 1e6;

  int ch = m_lfsr.Next() % 79;
  double centerFreq = bleStartFreq + ch * bleStepHz;
  int payloadB = m_uni->GetInteger(20, 80);
  double onTime = TimingHelper::BlePktDurationSeconds_1M(payloadB);

  // Update frequency for this hop (lightweight if BW same)
  UpdateFrequency(centerFreq - bleBwHz/2, bleBwHz);

  // Convert dBm/Hz to W/Hz and transmit directly
  double psdWattsHz = std::pow(10.0, (m_basePsd - 30) / 10.0);
  TransmitDirect(Seconds(onTime), psdWattsHz);
}

// -------------------- LazyCordlessInterferer --------------------

NS_OBJECT_ENSURE_REGISTERED(LazyCordlessInterferer);

TypeId
LazyCordlessInterferer::GetTypeId()
{
  static TypeId tid = TypeId("ns3::LazyCordlessInterferer")
    .SetParent<LazyInterferer>()
    .SetGroupName("Spectrum")
    .AddConstructor<LazyCordlessInterferer>();
  return tid;
}

LazyCordlessInterferer::LazyCordlessInterferer()
  : m_hopInterval(MilliSeconds(10)),
    m_txDuration(MilliSeconds(8)),
    m_bandwidth(1.728e6)
{
  NS_LOG_FUNCTION(this);
}

LazyCordlessInterferer::~LazyCordlessInterferer()
{
  NS_LOG_FUNCTION(this);
}

void
LazyCordlessInterferer::Configure(Ptr<SpectrumChannel> channel,
                                   Ptr<Node> node,
                                   Time startTime,
                                   Time duration,
                                   Time hopInterval,
                                   Time txDuration,
                                   double bandwidth,
                                   double basePsd)
{
  NS_LOG_FUNCTION(this << channel << node << startTime << duration);
  m_channel = channel;
  m_node = node;
  m_startTime = startTime;
  m_endTime = startTime + duration;
  m_hopInterval = hopInterval;
  m_txDuration = txDuration;
  m_bandwidth = bandwidth;
  m_basePsd = basePsd;

  // Initialize transmitter with cordless bandwidth
  InitTransmitter(2.402e9, bandwidth);
}

void
LazyCordlessInterferer::ScheduleNext()
{
  NS_LOG_FUNCTION(this);
  if (!m_running || Simulator::Now() >= m_endTime)
  {
    m_running = false;
    return;
  }
  // Only transmit during ON phase (schedule cycling)
  if (IsInOnPhase())
  {
    DoTransmit();
  }
  Simulator::Schedule(m_hopInterval, &LazyCordlessInterferer::ScheduleNext, this);
}

void
LazyCordlessInterferer::DoTransmit()
{
  NS_LOG_FUNCTION(this);
  const double startFreq = 2.402e9;
  const double stepHz = 1e6;

  int ch = std::rand() % 79;
  double centerFreq = startFreq + ch * stepHz;

  // Update frequency for this hop
  UpdateFrequency(centerFreq - m_bandwidth/2, m_bandwidth);

  // Convert dBm/Hz to W/Hz and transmit directly
  double psdWattsHz = std::pow(10.0, (m_basePsd - 30) / 10.0);
  TransmitDirect(m_txDuration, psdWattsHz);
}

// -------------------- LazyMicrowaveInterferer --------------------

NS_OBJECT_ENSURE_REGISTERED(LazyMicrowaveInterferer);

TypeId
LazyMicrowaveInterferer::GetTypeId()
{
  static TypeId tid = TypeId("ns3::LazyMicrowaveInterferer")
    .SetParent<LazyInterferer>()
    .SetGroupName("Spectrum")
    .AddConstructor<LazyMicrowaveInterferer>();
  return tid;
}

LazyMicrowaveInterferer::LazyMicrowaveInterferer()
  : m_centerFreq(2.45e9),
    m_bandwidth(80e6),
    m_dutyPeriod(MilliSeconds(16.67)),  // 60 Hz
    m_dutyRatio(0.5)
{
  NS_LOG_FUNCTION(this);
}

LazyMicrowaveInterferer::~LazyMicrowaveInterferer()
{
  NS_LOG_FUNCTION(this);
}

void
LazyMicrowaveInterferer::Configure(Ptr<SpectrumChannel> channel,
                                    Ptr<Node> node,
                                    Time startTime,
                                    Time duration,
                                    double centerFreq,
                                    double bandwidth,
                                    Time dutyPeriod,
                                    double dutyRatio,
                                    double basePsd)
{
  NS_LOG_FUNCTION(this << channel << node << startTime << duration);
  m_channel = channel;
  m_node = node;
  m_startTime = startTime;
  m_endTime = startTime + duration;
  m_centerFreq = centerFreq;
  m_bandwidth = bandwidth;
  m_dutyPeriod = dutyPeriod;
  m_dutyRatio = dutyRatio;
  m_basePsd = basePsd;

  // Initialize transmitter once (fixed frequency)
  InitTransmitter(centerFreq - bandwidth/2, bandwidth);
}

void
LazyMicrowaveInterferer::ScheduleNext()
{
  NS_LOG_FUNCTION(this);
  if (!m_running || Simulator::Now() >= m_endTime)
  {
    m_running = false;
    return;
  }
  // Only transmit during ON phase (schedule cycling)
  if (IsInOnPhase())
  {
    DoTransmit();
  }
  Simulator::Schedule(m_dutyPeriod, &LazyMicrowaveInterferer::ScheduleNext, this);
}

void
LazyMicrowaveInterferer::DoTransmit()
{
  NS_LOG_FUNCTION(this);
  Time txDuration = Seconds(m_dutyPeriod.GetSeconds() * m_dutyRatio);

  // Convert dBm/Hz to W/Hz and transmit directly
  double psdWattsHz = std::pow(10.0, (m_basePsd - 30) / 10.0);
  TransmitDirect(txDuration, psdWattsHz);
}

// -------------------- LazyZigbeeInterferer --------------------

NS_OBJECT_ENSURE_REGISTERED(LazyZigbeeInterferer);

TypeId
LazyZigbeeInterferer::GetTypeId()
{
  static TypeId tid = TypeId("ns3::LazyZigbeeInterferer")
    .SetParent<LazyInterferer>()
    .SetGroupName("Spectrum")
    .AddConstructor<LazyZigbeeInterferer>();
  return tid;
}

LazyZigbeeInterferer::LazyZigbeeInterferer()
  : m_centerFreq(2.405e9),
    m_bandwidth(2e6),
    m_txInterval(MilliSeconds(100))
{
  NS_LOG_FUNCTION(this);
  m_uni = CreateObject<UniformRandomVariable>();
}

LazyZigbeeInterferer::~LazyZigbeeInterferer()
{
  NS_LOG_FUNCTION(this);
}

void
LazyZigbeeInterferer::Configure(Ptr<SpectrumChannel> channel,
                                 Ptr<Node> node,
                                 Time startTime,
                                 Time duration,
                                 int zigbeeChannel,
                                 Time txInterval,
                                 double basePsd)
{
  NS_LOG_FUNCTION(this << channel << node << startTime << duration);
  m_channel = channel;
  m_node = node;
  m_startTime = startTime;
  m_endTime = startTime + duration;
  m_centerFreq = TimingHelper::ZigbeeChannelCenterHz(zigbeeChannel);
  m_txInterval = txInterval;
  m_basePsd = basePsd;

  // Initialize transmitter once (fixed frequency)
  InitTransmitter(m_centerFreq - m_bandwidth/2, m_bandwidth);
}

void
LazyZigbeeInterferer::ScheduleNext()
{
  NS_LOG_FUNCTION(this);
  if (!m_running || Simulator::Now() >= m_endTime)
  {
    m_running = false;
    return;
  }
  // Only transmit during ON phase (schedule cycling)
  if (IsInOnPhase())
  {
    DoTransmit();
  }
  // Randomize next interval slightly
  double nextInterval = m_txInterval.GetSeconds() * m_uni->GetValue(0.8, 1.2);
  Simulator::Schedule(Seconds(nextInterval), &LazyZigbeeInterferer::ScheduleNext, this);
}

void
LazyZigbeeInterferer::DoTransmit()
{
  NS_LOG_FUNCTION(this);
  int payloadBytes = m_uni->GetInteger(10, 100);
  double txDuration = TimingHelper::ZigbeePktDurationSeconds_2450(payloadBytes);

  // Convert dBm/Hz to W/Hz and transmit directly
  double psdWattsHz = std::pow(10.0, (m_basePsd - 30) / 10.0);
  TransmitDirect(Seconds(txDuration), psdWattsHz);
}

// -------------------- LazyRadarInterferer --------------------

NS_OBJECT_ENSURE_REGISTERED(LazyRadarInterferer);

TypeId
LazyRadarInterferer::GetTypeId()
{
  static TypeId tid = TypeId("ns3::LazyRadarInterferer")
    .SetParent<LazyInterferer>()
    .SetGroupName("Spectrum")
    .AddConstructor<LazyRadarInterferer>();
  return tid;
}

LazyRadarInterferer::LazyRadarInterferer()
  : m_centerFreq(5.26e9),
    m_pulseWidth(1.5e-6),
    m_pri(1e-3),
    m_pulsesPerBurst(15),
    m_burstInterval(Seconds(1)),
    m_currentPulse(0),
    m_nextBurstTime(Seconds(0)),
    m_hopInterval(Seconds(0)),
    m_randomHopping(false),
    m_currentChannelIndex(0),
    m_lastHopTime(Seconds(0)),
    m_spanLength(0),
    m_maxSpanLength(0),
    m_randomSpan(false)
{
  NS_LOG_FUNCTION(this);
  m_hopRng = CreateObject<UniformRandomVariable>();
}

LazyRadarInterferer::~LazyRadarInterferer()
{
  NS_LOG_FUNCTION(this);
}

void
LazyRadarInterferer::Configure(Ptr<SpectrumChannel> channel,
                                Ptr<Node> node,
                                Time startTime,
                                Time duration,
                                double centerFreq,
                                double pulseWidth,
                                double prf,
                                int pulsesPerBurst,
                                Time burstInterval,
                                double basePsd)
{
  NS_LOG_FUNCTION(this << channel << node << startTime << duration);
  m_channel = channel;
  m_node = node;
  m_startTime = startTime;
  m_endTime = startTime + duration;
  m_centerFreq = centerFreq;
  m_pulseWidth = pulseWidth;
  m_pri = 1.0 / prf;
  m_pulsesPerBurst = pulsesPerBurst;
  m_burstInterval = burstInterval;
  m_basePsd = basePsd;
  m_currentPulse = 0;
  m_nextBurstTime = startTime;

  // Initialize transmitter once (fixed frequency, 20 MHz radar bandwidth)
  const double bandwidth = 20e6;
  InitTransmitter(centerFreq - bandwidth/2, bandwidth);
}

void
LazyRadarInterferer::ScheduleNext()
{
  NS_LOG_FUNCTION(this);
  if (!m_running || Simulator::Now() >= m_endTime)
  {
    m_running = false;
    return;
  }

  // Only transmit during ON phase (schedule cycling)
  if (IsInOnPhase())
  {
    DoTransmit();
  }
  m_currentPulse++;

  // Check if burst is complete
  if (m_currentPulse >= m_pulsesPerBurst)
  {
    // Start new burst
    m_currentPulse = 0;
    m_nextBurstTime = m_nextBurstTime + m_burstInterval;
    Time delay = m_nextBurstTime - Simulator::Now();
    if (delay.IsNegative())
    {
      delay = Seconds(0);
    }
    Simulator::Schedule(delay, &LazyRadarInterferer::ScheduleNext, this);
  }
  else
  {
    // Next pulse in this burst
    Simulator::Schedule(Seconds(m_pri), &LazyRadarInterferer::ScheduleNext, this);
  }
}

void
LazyRadarInterferer::DoTransmit()
{
  NS_LOG_FUNCTION(this);

  // Check for channel hopping before transmitting
  if (!m_dfsChannels.empty() && m_hopInterval.IsStrictlyPositive())
  {
    DoChannelHop();
  }

  // Convert dBm/Hz to W/Hz and transmit directly
  double psdWattsHz = std::pow(10.0, (m_basePsd - 30) / 10.0);
  TransmitDirect(Seconds(m_pulseWidth), psdWattsHz);
}

void
LazyRadarInterferer::SetChannelHopping(const std::vector<uint8_t>& channels,
                                        Time hopInterval,
                                        bool randomHopping,
                                        uint32_t seed)
{
  NS_LOG_FUNCTION(this << hopInterval << randomHopping << seed);
  m_dfsChannels = channels;
  m_hopInterval = hopInterval;
  m_randomHopping = randomHopping;
  m_currentChannelIndex = 0;
  m_lastHopTime = Seconds(0);

  // Set RNG stream for synchronized hopping with VirtualRadarInterferer
  if (seed > 0)
  {
    m_hopRng->SetStream(static_cast<int64_t>(seed));
  }

  // Set initial center frequency from first channel
  if (!channels.empty())
  {
    m_centerFreq = ChannelToFrequency(channels[0]);
  }

  NS_LOG_INFO("Radar channel hopping configured: "
              << channels.size() << " channels, "
              << hopInterval.GetSeconds() << "s interval, "
              << (randomHopping ? "random" : "sequential")
              << ", seed=" << seed);
}

void
LazyRadarInterferer::SetWidebandSpan(uint8_t spanLength, uint8_t maxSpanLength, bool randomSpan)
{
  NS_LOG_FUNCTION(this << (int)spanLength << (int)maxSpanLength << randomSpan);
  m_spanLength = spanLength;
  m_maxSpanLength = maxSpanLength;
  m_randomSpan = randomSpan;
}

void
LazyRadarInterferer::DoChannelHop()
{
  NS_LOG_FUNCTION(this);

  Time now = Simulator::Now();

  // Check if it's time to hop
  if (now - m_lastHopTime < m_hopInterval)
  {
    return;  // Not time to hop yet
  }

  m_lastHopTime = now;

  if (m_dfsChannels.empty())
  {
    return;
  }

  uint8_t oldChannel = m_dfsChannels[m_currentChannelIndex];

  // Randomize span length if enabled
  if (m_randomSpan && m_maxSpanLength > 0)
  {
    m_hopRng->SetAttribute("Max", DoubleValue(m_maxSpanLength + 1));
    m_spanLength = static_cast<uint8_t>(m_hopRng->GetValue());
    if (m_spanLength > m_maxSpanLength)
    {
      m_spanLength = m_maxSpanLength;
    }
  }

  if (m_randomHopping)
  {
    // Random hop to any channel in the list
    m_hopRng->SetAttribute("Max", DoubleValue(m_dfsChannels.size()));
    size_t randomIndex = static_cast<size_t>(m_hopRng->GetValue());
    if (randomIndex >= m_dfsChannels.size())
    {
      randomIndex = m_dfsChannels.size() - 1;
    }
    m_currentChannelIndex = randomIndex;
  }
  else
  {
    // Sequential hopping
    m_currentChannelIndex = (m_currentChannelIndex + 1) % m_dfsChannels.size();
  }

  uint8_t newChannel = m_dfsChannels[m_currentChannelIndex];
  m_centerFreq = ChannelToFrequency(newChannel);

  // Update transmitter frequency for new channel
  const double bandwidth = 20e6 * (1 + 2 * m_spanLength);  // Wideband span
  UpdateFrequency(m_centerFreq - bandwidth/2, bandwidth);

  NS_LOG_INFO("Radar hopped: center " << (int)oldChannel << "->" << (int)newChannel
              << ", span ±" << (int)m_spanLength
              << ", freq=" << m_centerFreq/1e9 << " GHz"
              << " at t=" << now.GetSeconds() << "s");
}

double
LazyRadarInterferer::ChannelToFrequency(uint8_t channel)
{
  // 5 GHz band channel to frequency mapping
  // UNII-1: 36, 40, 44, 48 (5.18-5.24 GHz)
  // UNII-2: 52, 56, 60, 64 (5.26-5.32 GHz) - DFS
  // UNII-2e: 100-144 (5.50-5.72 GHz) - DFS
  // UNII-3: 149, 153, 157, 161, 165 (5.745-5.825 GHz)

  // Base frequency for channel 36 is 5180 MHz
  // Each channel is 5 MHz apart (for 20 MHz channels, use 36, 40, 44, etc.)
  double freqMhz = 5000.0 + 5.0 * channel;
  return freqMhz * 1e6;  // Return Hz
}

} // namespace ns3