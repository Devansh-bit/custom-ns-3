#include "spectrogram-generation-helper.h"
#include "ns3/tv-spectrum-transmitter-helper.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/log.h"
#include <cstdlib>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SpectrogramGenerationHelper");

SpectrogramGenerationHelper::SpectrogramGenerationHelper(Ptr<AnnotationManager> annotationMgr)
  : m_annotationMgr(annotationMgr)
{
  NS_LOG_FUNCTION(this);
}

void
SpectrogramGenerationHelper::GenerateBluetoothInterferer(Ptr<SpectrumChannel> channel,
                                                          Ptr<Node> node,
                                                          double startTimeBase,
                                                          double duration,
                                                          double hopInterval,
                                                          double jitterStd,
                                                          double basePsd,
                                                          uint8_t lfsr_seed,
                                                          Ptr<UniformRandomVariable> uni,
                                                          Ptr<NormalRandomVariable> jit)
{

  const double bleStartFreq = 2.402e9;
  const double bleStepHz = 1e6;
  const double bleBwHz = 1e6;

  Lfsr7 lfsr(lfsr_seed);

  for (int i = 0; i < duration/hopInterval; ++i)
  {
    int ch = lfsr.Next() % 79;
    double centerFreq = bleStartFreq + ch * bleStepHz;
    int payloadB = uni->GetInteger(20, 80);
    double onTime = TimingHelper::BlePktDurationSeconds_1M(payloadB);
    double startTime = startTimeBase + i * hopInterval + jit->GetValue();

    // Add annotation
    m_annotationMgr->AddAnnotation("bluetooth", startTime, duration, centerFreq, bleBwHz);

    // Install transmitter
    TvSpectrumTransmitterHelper tx;
    tx.SetChannel(channel);
    tx.SetAttribute("StartFrequency", DoubleValue(centerFreq-bleBwHz/2));
    tx.SetAttribute("ChannelBandwidth", DoubleValue(bleBwHz));
    tx.SetAttribute("StartingTime", TimeValue(Seconds(startTime)));
    tx.SetAttribute("TransmitDuration", TimeValue(Seconds(onTime)));
    tx.SetAttribute("BasePsd", DoubleValue(basePsd));
    tx.SetAttribute("Antenna", StringValue("ns3::IsotropicAntennaModel"));
    tx.Install(node);
  }
}

void
SpectrogramGenerationHelper::GenerateWifiInterferer(Ptr<SpectrumChannel> channel,
                                                     Ptr<Node> node,
                                                     double startFreq,
                                                     double startTime,
                                                     double duration,
                                                     double bandwidth,
                                                     double basePsd)
{
  NS_LOG_FUNCTION(this << node->GetId() << startFreq << startTime << duration);

  // Calculate center frequency (startFreq is typically the lower edge)
  double centerFreq = startFreq + bandwidth / 2.0;

  // Add annotation
  m_annotationMgr->AddAnnotation("wifi", startTime, duration, centerFreq, bandwidth);

  // Install transmitter
  TvSpectrumTransmitterHelper tx;
  tx.SetChannel(channel);
  tx.SetAttribute("StartFrequency", DoubleValue(startFreq));
  tx.SetAttribute("ChannelBandwidth", DoubleValue(bandwidth));
  tx.SetAttribute("StartingTime", TimeValue(Seconds(startTime)));
  tx.SetAttribute("TransmitDuration", TimeValue(Seconds(duration)));
  tx.SetAttribute("BasePsd", DoubleValue(basePsd));
  tx.SetAttribute("Antenna", StringValue("ns3::IsotropicAntennaModel"));
  tx.Install(node);
}

void
SpectrogramGenerationHelper::GenerateDutyWifiInterferer(Ptr<SpectrumChannel> channel,
                                                     Ptr<Node> node,
                                                     double startFreq,
                                                     double startTime,
                                                     double duration,
                                                     double dutyPeriod,
                                                     double dutyRatio,
                                                     double bandwidth,
                                                     double basePsd)
{

    // Calculate center frequency (startFreq is typically the lower edge)
    double centerFreq = startFreq + bandwidth / 2.0;

    // Add annotation
    m_annotationMgr->AddDutyAnnotation("wifi", startTime, duration, dutyPeriod, dutyRatio, centerFreq, bandwidth);

    // Install transmitter
    for (int i=0; i<duration/dutyPeriod; i++)
    {
        TvSpectrumTransmitterHelper tx;
        tx.SetChannel(channel);
        tx.SetAttribute("StartFrequency", DoubleValue(startFreq));
        tx.SetAttribute("ChannelBandwidth", DoubleValue(bandwidth));
        tx.SetAttribute("StartingTime", TimeValue(Seconds(startTime+i*dutyPeriod)));
        tx.SetAttribute("TransmitDuration", TimeValue(Seconds(dutyPeriod*dutyRatio)));
        tx.SetAttribute("BasePsd", DoubleValue(basePsd));
        tx.SetAttribute("Antenna", StringValue("ns3::IsotropicAntennaModel"));
        tx.Install(node);
    }
}

void
SpectrogramGenerationHelper::GenerateZigbeeInterferer(Ptr<SpectrumChannel> channel,
                                                       Ptr<Node> node,
                                                       int zigbeeChannel,
                                                       double startTimeBase,
                                                       double interval,
                                                       double basePsd,
                                                       Ptr<UniformRandomVariable> uni)
{

  const double centerFreq = TimingHelper::ZigbeeChannelCenterHz(zigbeeChannel);
  const double bandwidth = 2e6;
    double duty_period = uni->GetValue(0.01, 0.03);
    double duty_ratio = uni->GetValue(0.01, 0.05);

  for (int k = 0; k < interval/duty_period; ++k)
  {
      double startTime = startTimeBase + k*duty_period;
    // Add annotation
    m_annotationMgr->AddDutyAnnotation("zigbee", startTime, interval, duty_period, duty_ratio, centerFreq, bandwidth);

    // Install transmitter
    TvSpectrumTransmitterHelper tx;
    tx.SetChannel(channel);
    tx.SetAttribute("StartFrequency", DoubleValue(centerFreq - bandwidth/2));
    tx.SetAttribute("ChannelBandwidth", DoubleValue(bandwidth));
    tx.SetAttribute("StartingTime", TimeValue(Seconds(startTime)));
    tx.SetAttribute("TransmitDuration", TimeValue(Seconds(duty_period*duty_ratio)));
    tx.SetAttribute("BasePsd", DoubleValue(basePsd));
    tx.SetAttribute("Antenna", StringValue("ns3::IsotropicAntennaModel"));
    tx.Install(node);
  }
}

void
SpectrogramGenerationHelper::GenerateCordlessInterferer(Ptr<SpectrumChannel> channel,
                                                         Ptr<Node> node,
                                                         double startTimeBase,
                                                         int numHops,
                                                         double hopInterval,
                                                         double duration,
                                                         double bandwidth,
                                                         double basePsd)
{
  NS_LOG_FUNCTION(this << node->GetId() << startTimeBase << numHops);

  const double startFreq = 2.402e9;
  const double stepHz = 1e6;

  for (int i = 0; i < numHops; ++i)
  {
    int ch = std::rand() % 79;
    double centerFreq = startFreq + ch * stepHz;
    double startTime = startTimeBase + i * hopInterval;

    // Add annotation
    m_annotationMgr->AddAnnotation("cordless", startTime, duration, centerFreq, bandwidth);

    // Install transmitter
    TvSpectrumTransmitterHelper tx;
    tx.SetChannel(channel);
    tx.SetAttribute("StartFrequency", DoubleValue(centerFreq - bandwidth/2));
    tx.SetAttribute("ChannelBandwidth", DoubleValue(bandwidth));
    tx.SetAttribute("StartingTime", TimeValue(Seconds(startTime)));
    tx.SetAttribute("TransmitDuration", TimeValue(Seconds(duration)));
    tx.SetAttribute("BasePsd", DoubleValue(basePsd));
    tx.SetAttribute("Antenna", StringValue("ns3::IsotropicAntennaModel"));
    tx.Install(node);
  }
}

void
SpectrogramGenerationHelper::GenerateMicrowaveInterferer(Ptr<SpectrumChannel> channel,
                                                          Ptr<Node> node,
                                                          double centerFreq,
                                                          double startTime,
                                                          double duration,
                                                          double bandwidth,
                                                          double basePsd)
{
  NS_LOG_FUNCTION(this << node->GetId() << centerFreq << startTime << duration);

  // Add annotation
  m_annotationMgr->AddAnnotation("microwave", startTime, duration, centerFreq, bandwidth);

  // Install transmitter
  TvSpectrumTransmitterHelper tx;
  tx.SetChannel(channel);
  tx.SetAttribute("StartFrequency", DoubleValue(centerFreq-bandwidth/2));
  tx.SetAttribute("ChannelBandwidth", DoubleValue(bandwidth));
  tx.SetAttribute("StartingTime", TimeValue(Seconds(startTime)));
  tx.SetAttribute("TransmitDuration", TimeValue(Seconds(duration)));
  tx.SetAttribute("BasePsd", DoubleValue(basePsd));
  tx.SetAttribute("Antenna", StringValue("ns3::IsotropicAntennaModel"));
  tx.Install(node);
}

void
SpectrogramGenerationHelper::GenerateDutyMicrowaveInterferer(Ptr<SpectrumChannel> channel,
                                                          Ptr<Node> node,
                                                          double centerFreq,
                                                          double startTime,
                                                          double duration,
                                                          double dutyPeriod,
                                                          double dutyRatio,
                                                          double bandwidth,
                                                          double basePsd)
{
    NS_LOG_FUNCTION(this << node->GetId() << centerFreq << startTime << duration);

    // Add annotation
    m_annotationMgr->AddDutyAnnotation("microwave", startTime, duration, dutyPeriod, dutyRatio, centerFreq, bandwidth);

    // Install transmitter
    for (int i=0; i<duration/dutyPeriod; i++)
    {
        TvSpectrumTransmitterHelper tx;
        tx.SetChannel(channel);
        tx.SetAttribute("StartFrequency", DoubleValue(centerFreq-bandwidth/2));
        tx.SetAttribute("ChannelBandwidth", DoubleValue(bandwidth));
        tx.SetAttribute("StartingTime", TimeValue(Seconds(startTime+i*dutyPeriod)));
        tx.SetAttribute("TransmitDuration", TimeValue(Seconds(dutyPeriod*dutyRatio)));
        tx.SetAttribute("BasePsd", DoubleValue(basePsd));
        tx.SetAttribute("Antenna", StringValue("ns3::IsotropicAntennaModel"));
        tx.Install(node);
    }
}

// ============== DFS Radar Interferer Implementation ==============

double
SpectrogramGenerationHelper::ChannelToFrequency(uint16_t wifiChannel)
{
  // 5 GHz channels: center frequency = 5000 + (channel * 5) MHz
  return (5000.0 + wifiChannel * 5.0) * 1e6;  // Return in Hz
}

bool
SpectrogramGenerationHelper::IsDfsChannel(uint16_t wifiChannel)
{
  // UNII-2A: 52-64
  if (wifiChannel >= 52 && wifiChannel <= 64)
    return true;
  // UNII-2C/Extended: 100-144
  if (wifiChannel >= 100 && wifiChannel <= 144)
    return true;
  return false;
}

uint32_t
SpectrogramGenerationHelper::GetCacTime(uint16_t wifiChannel)
{
  // Weather radar band (5600-5650 MHz): channels 120, 124, 128 require 10 min CAC
  if (wifiChannel >= 120 && wifiChannel <= 128)
    return 600;  // 10 minutes in seconds
  // All other DFS channels: 60 second CAC
  return 60;
}

void
SpectrogramGenerationHelper::GenerateRadarInterferer(Ptr<SpectrumChannel> channel,
                                                     Ptr<Node> node,
                                                     double centerFreq,
                                                     double startTime,
                                                     double duration,
                                                     RadarType radarType,
                                                     double basePsd)
{
  NS_LOG_FUNCTION(this << node->GetId() << centerFreq << startTime << duration);

  // Radar pulse characteristics based on type
  double pulseWidth;   // Pulse width in seconds
  double prf;          // Pulse Repetition Frequency in Hz
  int numPulses;       // Number of pulses per burst
  double bandwidth = 20e6;  // 20 MHz detection bandwidth (standard)

  switch (radarType)
  {
    case RadarType::WEATHER_RADAR:
      pulseWidth = 1.5e-6;   // 1.5 microseconds
      prf = 1000;            // 1000 Hz (1ms interval)
      numPulses = 15;
      break;
    case RadarType::MILITARY_RADAR:
      pulseWidth = 0.5e-6;   // 0.5 microseconds
      prf = 3000;            // 3000 Hz
      numPulses = 10;
      break;
    case RadarType::AIRPORT_RADAR:
      pulseWidth = 5e-6;     // 5 microseconds
      prf = 500;             // 500 Hz
      numPulses = 18;
      break;
    default:
      pulseWidth = 1.5e-6;   // Default to weather radar
      prf = 1000;
      numPulses = 15;
      break;
  }

  double pri = 1.0 / prf;  // Pulse Repetition Interval

  // Add annotation for the entire radar event
  m_annotationMgr->AddAnnotation("radar", startTime, duration, centerFreq, bandwidth);

  NS_LOG_INFO("Generating radar interferer: type=" << static_cast<int>(radarType)
              << ", freq=" << centerFreq/1e9 << " GHz"
              << ", pulseWidth=" << pulseWidth*1e6 << " µs"
              << ", PRF=" << prf << " Hz"
              << ", pulses/burst=" << numPulses);

  // Generate radar pulse bursts throughout the duration
  // Radar typically has bursts separated by antenna rotation time
  double burstInterval = 1.0;  // 1 second between bursts (typical rotation)

  for (double burstStart = startTime; burstStart < startTime + duration; burstStart += burstInterval)
  {
    // Generate pulses within each burst
    for (int i = 0; i < numPulses; ++i)
    {
      double pulseStartTime = burstStart + i * pri;

      // Don't schedule past the end of duration
      if (pulseStartTime >= startTime + duration)
        break;

      TvSpectrumTransmitterHelper tx;
      tx.SetChannel(channel);
      tx.SetAttribute("StartFrequency", DoubleValue(centerFreq - bandwidth/2));
      tx.SetAttribute("ChannelBandwidth", DoubleValue(bandwidth));
      tx.SetAttribute("StartingTime", TimeValue(Seconds(pulseStartTime)));
      tx.SetAttribute("TransmitDuration", TimeValue(Seconds(pulseWidth)));
      tx.SetAttribute("BasePsd", DoubleValue(basePsd));
      tx.SetAttribute("Antenna", StringValue("ns3::IsotropicAntennaModel"));
      tx.Install(node);
    }
  }
}

void
SpectrogramGenerationHelper::GenerateRadarInterfererOnChannel(Ptr<SpectrumChannel> channel,
                                                              Ptr<Node> node,
                                                              uint16_t wifiChannel,
                                                              double startTime,
                                                              double duration,
                                                              RadarType radarType,
                                                              double basePsd)
{
  NS_LOG_FUNCTION(this << node->GetId() << wifiChannel << startTime << duration);

  if (!IsDfsChannel(wifiChannel))
  {
    NS_LOG_WARN("Channel " << wifiChannel << " is not a DFS channel");
  }

  double centerFreq = ChannelToFrequency(wifiChannel);

  NS_LOG_INFO("Generating radar on WiFi channel " << wifiChannel
              << " (center freq: " << centerFreq/1e9 << " GHz)"
              << ", CAC time: " << GetCacTime(wifiChannel) << "s");

  GenerateRadarInterferer(channel, node, centerFreq, startTime, duration, radarType, basePsd);
}

} // namespace ns3