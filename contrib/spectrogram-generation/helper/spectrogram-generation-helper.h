#ifndef SPECTROGRAM_GENERATION_HELPER_H
#define SPECTROGRAM_GENERATION_HELPER_H

#include "ns3/spectrum-channel.h"
#include "ns3/node.h"
#include "ns3/random-variable-stream.h"
#include "ns3/spectrogram-generation.h"

namespace ns3 {

/**
 * \brief Radar types for DFS testing
 */
enum class RadarType
{
  WEATHER_RADAR,    ///< Weather radar (1.5µs pulse, 1000 Hz PRF, 15 pulses)
  MILITARY_RADAR,   ///< Military radar (0.5µs pulse, 3000 Hz PRF, 10 pulses)
  AIRPORT_RADAR     ///< Airport surveillance radar (5µs pulse, 500 Hz PRF, 18 pulses)
};

/**
 * \brief DFS channel bands
 */
enum class DfsBand
{
  UNII_2A,    ///< Channels 52-64 (5250-5350 MHz) - 60s CAC
  UNII_2C,    ///< Channels 100-116, 132-144 (5470-5725 MHz) - 60s CAC
  WEATHER     ///< Channels 120-128 (5600-5650 MHz) - 10 min CAC
};

/**
 * \brief Helper class for generating various ISM band interferers
 */
class SpectrogramGenerationHelper
{
public:
  /**
   * \brief Constructor
   * \param annotationMgr Pointer to annotation manager
   */
  explicit SpectrogramGenerationHelper(Ptr<AnnotationManager> annotationMgr);

  /**
   * \brief Generate Bluetooth interferer with frequency hopping
   * \param channel Spectrum channel
   * \param node Node to install transmitter on
   * \param startTimeBase Base start time
   * \param duration
   * \param hopInterval Time between hops (typically 625 µs)
   * \param jitterStd Standard deviation of timing jitter
   * \param basePsd Base power spectral density
   * \param lfsr_seed Seed for LFSR hopping sequence
   * \param uni Uniform random variable for packet sizes
   * \param jit Normal random variable for timing jitter
   */
  void GenerateBluetoothInterferer(Ptr<SpectrumChannel> channel,
                                   Ptr<Node> node,
                                   double startTimeBase,
                                   double duration,
                                   double hopInterval,
                                   double jitterStd,
                                   double basePsd,
                                   uint8_t lfsr_seed,
                                   Ptr<UniformRandomVariable> uni,
                                   Ptr<NormalRandomVariable> jit);

  /**
   * \brief Generate WiFi interferer
   * \param channel Spectrum channel
   * \param node Node to install transmitter on
   * \param startFreq Start frequency (lower edge)
   * \param startTime Start time
   * \param duration Transmission duration
   * \param bandwidth Channel bandwidth
   * \param basePsd Base power spectral density
   */
  void GenerateWifiInterferer(Ptr<SpectrumChannel> channel, Ptr<Node> node,
                              double startFreq, double startTime,
                              double duration, double bandwidth,
                              double basePsd);
  void GenerateDutyWifiInterferer(Ptr<SpectrumChannel> channel,
                                  Ptr<Node> node,
                                  double startFreq,
                                  double startTime,
                                  double duration,
                                  double dutyPeriod,
                                  double dutyRatio,
                                  double bandwidth,
                                  double basePsd);

  /**
   * \brief Generate ZigBee interferer
   * \param channel Spectrum channel
   * \param node Node to install transmitter on
   * \param zigbeeChannel ZigBee channel (11-26)
   * \param startTimeBase Base start time
   * \param interval
   * \param basePsd Base power spectral density
   * \param uni Uniform random variable for packet sizes
   */
  void GenerateZigbeeInterferer(Ptr<SpectrumChannel> channel,
                                Ptr<Node> node,
                                int zigbeeChannel,
                                double startTimeBase,
                                double interval,
                                double basePsd,
                                Ptr<UniformRandomVariable> uni);

  /**
   * \brief Generate cordless phone interferer
   * \param channel Spectrum channel
   * \param node Node to install transmitter on
   * \param startTimeBase Base start time
   * \param numHops Number of frequency hops
   * \param hopInterval Time between hops
   * \param duration Transmission duration per hop
   * \param bandwidth Channel bandwidth
   * \param basePsd Base power spectral density
   */
  void GenerateCordlessInterferer(Ptr<SpectrumChannel> channel, Ptr<Node> node,
                                   double startTimeBase, int numHops,
                                   double hopInterval, double duration,
                                   double bandwidth, double basePsd);

  /**
   * \brief Generate microwave oven interferer
   * \param channel Spectrum channel
   * \param node Node to install transmitter on
   * \param centerFreq Center frequency (typically 2.45 GHz)
   * \param startTime Start time
   * \param duration Transmission duration
   * \param bandwidth Channel bandwidth
   * \param basePsd Base power spectral density
   */
  void GenerateMicrowaveInterferer(Ptr<SpectrumChannel> channel, Ptr<Node> node,
                                    double centerFreq, double startTime,
                                    double duration, double bandwidth,
                                    double basePsd);

    void GenerateDutyMicrowaveInterferer(Ptr<SpectrumChannel> channel, Ptr<Node> node,
                                    double centerFreq, double startTime,
                                    double duration, double dutyPeriod,
                                    double dutyRatio, double bandwidth,
                                    double basePsd);

  /**
   * \brief Generate DFS radar interferer for testing radar detection
   * \param channel Spectrum channel
   * \param node Node to install transmitter on
   * \param centerFreq Center frequency in Hz (e.g., 5.26e9 for channel 52)
   * \param startTime Start time in seconds
   * \param duration Total duration of radar activity
   * \param radarType Type of radar pattern to generate
   * \param basePsd Base power spectral density in W/Hz
   */
  void GenerateRadarInterferer(Ptr<SpectrumChannel> channel,
                               Ptr<Node> node,
                               double centerFreq,
                               double startTime,
                               double duration,
                               RadarType radarType,
                               double basePsd);

  /**
   * \brief Generate DFS radar interferer using WiFi channel number
   * \param channel Spectrum channel
   * \param node Node to install transmitter on
   * \param wifiChannel WiFi channel number (52-144 for DFS)
   * \param startTime Start time in seconds
   * \param duration Total duration of radar activity
   * \param radarType Type of radar pattern to generate
   * \param basePsd Base power spectral density in W/Hz
   */
  void GenerateRadarInterfererOnChannel(Ptr<SpectrumChannel> channel,
                                        Ptr<Node> node,
                                        uint16_t wifiChannel,
                                        double startTime,
                                        double duration,
                                        RadarType radarType,
                                        double basePsd);

  /**
   * \brief Convert WiFi 5GHz channel number to center frequency
   * \param wifiChannel WiFi channel number
   * \return Center frequency in Hz
   */
  static double ChannelToFrequency(uint16_t wifiChannel);

  /**
   * \brief Check if a channel is a DFS channel
   * \param wifiChannel WiFi channel number
   * \return true if channel requires DFS
   */
  static bool IsDfsChannel(uint16_t wifiChannel);

  /**
   * \brief Get CAC time for a DFS channel
   * \param wifiChannel WiFi channel number
   * \return CAC time in seconds (60 or 600)
   */
  static uint32_t GetCacTime(uint16_t wifiChannel);

private:
  Ptr<AnnotationManager> m_annotationMgr; ///< Annotation manager for recording events
};

} // namespace ns3

#endif /* SPECTROGRAM_GENERATION_HELPER_H */