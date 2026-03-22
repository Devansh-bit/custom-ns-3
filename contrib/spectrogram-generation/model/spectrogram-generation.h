#ifndef SPECTROGRAM_GENERATION_H
#define SPECTROGRAM_GENERATION_H

#include "ns3/object.h"
#include "ns3/spectrum-channel.h"
#include "ns3/spectrum-value.h"
#include "ns3/spectrum-signal-parameters.h"
#include "ns3/antenna-model.h"
#include "ns3/mobility-model.h"
#include "ns3/node.h"
#include "ns3/random-variable-stream.h"
#include <string>
#include <vector>

namespace ns3 {

// Forward declaration
class SpectrumPhy;

/**
 * \brief Structure to hold interference annotation data
 */
struct InterferenceAnnotation
{
  std::string interfererType;  ///< Type: "wifi", "bluetooth", "zigbee", "cordless", "microwave"
  double startTime;            ///< Start time in seconds
  double endTime;              ///< End time in seconds
  double centralFrequency;     ///< Central frequency in Hz
  double bandwidth;            ///< Bandwidth in Hz
};

struct DutyInterferenceAnnotation
{
    std::string interfererType;  ///< Type: "wifi", "bluetooth", "zigbee", "cordless", "microwave"
    double startTime;            ///< Start time in seconds
    double endTime;              ///< End time in seconds
    double dutyPeriod;
    double dutyRatio;
    double centralFrequency;     ///< Central frequency in Hz
    double bandwidth;            ///< Bandwidth in Hz
};

/**
 * \brief 7-bit Linear Feedback Shift Register for BLE frequency hopping
 */
class Lfsr7
{
public:
  /**
   * \brief Constructor
   * \param seed Initial seed value (7-bit)
   */
  explicit Lfsr7(uint8_t seed = 0x5A);

  /**
   * \brief Get next LFSR value
   * \return Next 7-bit value
   */
  uint8_t Next();

private:
  uint8_t state; ///< Current LFSR state
};

/**
 * \brief Annotation manager for storing and exporting interference data
 */
class AnnotationManager : public Object
{
public:
  /**
   * \brief Get the type ID
   * \return the object TypeId
   */
  static TypeId GetTypeId();

  /**
   * \brief Constructor
   */
  AnnotationManager();

  /**
   * \brief Destructor
   */
  virtual ~AnnotationManager();

  /**
   * \brief Add an annotation
   * \param type Interferer type
   * \param start Start time in seconds
   * \param duration Duration in seconds
   * \param centerFreq Central frequency in Hz
   * \param bw Bandwidth in Hz
   */
  void AddAnnotation(const std::string& type, double start, double duration,
                     double centerFreq, double bw);

    void AddDutyAnnotation(const std::string& type, double start, double duration, double dutyPeriod, double dutyRatio, double centralFrequency, double bandwidth);

  /**
   * \brief Write annotations to JSON file
   * \param filename Output filename
   */
  void WriteAnnotationsToJson(const std::string& filename);

  /**
   * \brief Get all annotations
   * \return Vector of all annotations
   */
  const std::vector<InterferenceAnnotation>& GetAnnotations() const;
  const std::vector<DutyInterferenceAnnotation>& GetDutyAnnotations() const;

  /**
   * \brief Clear all annotations
   */
  void Clear();

private:
  std::vector<InterferenceAnnotation> m_annotations; ///< Storage for annotations
    std::vector<DutyInterferenceAnnotation> m_duty_annotations;
};

/**
 * \brief Timing helper functions
 */
class TimingHelper
{
public:
  /**
   * \brief Calculate BLE packet duration at 1 Mbps
   * \param payloadBytes Payload size in bytes
   * \return Duration in seconds
   */
  static double BlePktDurationSeconds_1M(int payloadBytes);

  /**
   * \brief Get ZigBee channel center frequency
   * \param ch Channel number (11-26)
   * \return Center frequency in Hz
   */
  static double ZigbeeChannelCenterHz(int ch);

  /**
   * \brief Calculate ZigBee packet duration at 2.4 GHz
   * \param payloadBytes Payload size in bytes
   * \return Duration in seconds
   */
  static double ZigbeePktDurationSeconds_2450(int payloadBytes);
};

// ============================================================================
// LAZY INTERFERERS - Use lazy scheduling for long simulations
// ============================================================================

/**
 * \brief Base class for lazy-scheduled interferers
 *
 * Uses lazy scheduling to avoid pre-scheduling millions of events.
 * Only the next event is scheduled, then it chains to the next.
 * Uses direct channel transmission to avoid creating new objects per-tx.
 */
class LazyInterferer : public Object
{
public:
  static TypeId GetTypeId();
  LazyInterferer();
  virtual ~LazyInterferer();

  /**
   * \brief Start the interferer
   */
  virtual void Start();

  /**
   * \brief Stop the interferer
   */
  virtual void Stop();

  /**
   * \brief Configure ON/OFF schedule cycling to match VirtualInterferer behavior
   * \param onDuration Duration of ON phase (transmitting)
   * \param offDuration Duration of OFF phase (silent)
   *
   * When schedule is set, the interferer only transmits during ON phases,
   * synchronized with VirtualInterferers in config-simulation.
   */
  void SetSchedule(Time onDuration, Time offDuration);

  /**
   * \brief Check if currently in ON phase (allowed to transmit)
   * \return true if in ON phase or no schedule configured
   */
  bool IsInOnPhase() const;

protected:
  /**
   * \brief Schedule the next transmission event
   */
  virtual void ScheduleNext() = 0;

  /**
   * \brief Perform a single transmission
   */
  virtual void DoTransmit() = 0;

  /**
   * \brief Initialize the reusable transmitter components
   * \param startFreq Start frequency in Hz
   * \param bandwidth Bandwidth in Hz
   *
   * Creates reusable SpectrumModel, SpectrumValue, and antenna
   * to avoid creating new objects on every transmission.
   */
  void InitTransmitter(double startFreq, double bandwidth);

  /**
   * \brief Transmit directly on channel without creating new objects
   * \param duration Transmission duration
   * \param psdWattsHz PSD in W/Hz (linear, not dBm)
   *
   * Updates the reusable PSD and calls channel->StartTx directly.
   */
  void TransmitDirect(Time duration, double psdWattsHz);

  /**
   * \brief Update the frequency band and reinitialize spectrum model if needed
   * \param startFreq New start frequency in Hz
   * \param bandwidth New bandwidth in Hz
   */
  void UpdateFrequency(double startFreq, double bandwidth);

  /**
   * \brief Handle phase transition (ON->OFF or OFF->ON)
   */
  void HandlePhaseTransition();

  Ptr<SpectrumChannel> m_channel;
  Ptr<Node> m_node;
  Time m_endTime;
  Time m_startTime;
  double m_basePsd;
  bool m_running;

  // Schedule cycling support (synchronized with VirtualInterferers)
  bool m_hasSchedule;         ///< Whether schedule cycling is enabled
  Time m_onDuration;          ///< Duration of ON phase
  Time m_offDuration;         ///< Duration of OFF phase
  bool m_inOnPhase;           ///< Currently in ON phase (transmitting allowed)
  Time m_phaseStartTime;      ///< When current phase started

  // Reusable transmitter components (created once, reused per-tx)
  Ptr<SpectrumValue> m_txPsd;           ///< Reusable PSD
  Ptr<SpectrumModel> m_spectrumModel;   ///< Reusable spectrum model
  Ptr<AntennaModel> m_antenna;          ///< Reusable antenna
  Ptr<SpectrumPhy> m_txPhy;             ///< Reusable PHY for tx
  double m_currentStartFreq;            ///< Current start frequency
  double m_currentBandwidth;            ///< Current bandwidth
};

/**
 * \brief Lazy Bluetooth interferer with frequency hopping
 */
class LazyBluetoothInterferer : public LazyInterferer
{
public:
  static TypeId GetTypeId();
  LazyBluetoothInterferer();
  virtual ~LazyBluetoothInterferer();

  void Configure(Ptr<SpectrumChannel> channel,
                 Ptr<Node> node,
                 Time startTime,
                 Time duration,
                 Time hopInterval,
                 double basePsd,
                 uint8_t lfsrSeed);

protected:
  virtual void ScheduleNext() override;
  virtual void DoTransmit() override;

private:
  Time m_hopInterval;
  Lfsr7 m_lfsr;
  Ptr<UniformRandomVariable> m_uni;
};

/**
 * \brief Lazy cordless phone interferer with frequency hopping
 */
class LazyCordlessInterferer : public LazyInterferer
{
public:
  static TypeId GetTypeId();
  LazyCordlessInterferer();
  virtual ~LazyCordlessInterferer();

  void Configure(Ptr<SpectrumChannel> channel,
                 Ptr<Node> node,
                 Time startTime,
                 Time duration,
                 Time hopInterval,
                 Time txDuration,
                 double bandwidth,
                 double basePsd);

protected:
  virtual void ScheduleNext() override;
  virtual void DoTransmit() override;

private:
  Time m_hopInterval;
  Time m_txDuration;
  double m_bandwidth;
};

/**
 * \brief Lazy microwave interferer with duty cycling
 */
class LazyMicrowaveInterferer : public LazyInterferer
{
public:
  static TypeId GetTypeId();
  LazyMicrowaveInterferer();
  virtual ~LazyMicrowaveInterferer();

  void Configure(Ptr<SpectrumChannel> channel,
                 Ptr<Node> node,
                 Time startTime,
                 Time duration,
                 double centerFreq,
                 double bandwidth,
                 Time dutyPeriod,
                 double dutyRatio,
                 double basePsd);

protected:
  virtual void ScheduleNext() override;
  virtual void DoTransmit() override;

private:
  double m_centerFreq;
  double m_bandwidth;
  Time m_dutyPeriod;
  double m_dutyRatio;
};

/**
 * \brief Lazy ZigBee interferer
 */
class LazyZigbeeInterferer : public LazyInterferer
{
public:
  static TypeId GetTypeId();
  LazyZigbeeInterferer();
  virtual ~LazyZigbeeInterferer();

  void Configure(Ptr<SpectrumChannel> channel,
                 Ptr<Node> node,
                 Time startTime,
                 Time duration,
                 int zigbeeChannel,
                 Time txInterval,
                 double basePsd);

protected:
  virtual void ScheduleNext() override;
  virtual void DoTransmit() override;

private:
  double m_centerFreq;
  double m_bandwidth;
  Time m_txInterval;
  Ptr<UniformRandomVariable> m_uni;
};

/**
 * \brief Lazy radar interferer with pulse bursts and channel hopping
 */
class LazyRadarInterferer : public LazyInterferer
{
public:
  static TypeId GetTypeId();
  LazyRadarInterferer();
  virtual ~LazyRadarInterferer();

  void Configure(Ptr<SpectrumChannel> channel,
                 Ptr<Node> node,
                 Time startTime,
                 Time duration,
                 double centerFreq,
                 double pulseWidth,
                 double prf,
                 int pulsesPerBurst,
                 Time burstInterval,
                 double basePsd);

  /**
   * \brief Configure channel hopping (synchronized with VirtualRadarInterferer)
   * \param channels List of DFS channels to hop across
   * \param hopInterval Time between channel hops
   * \param randomHopping Whether to hop randomly or sequentially
   * \param seed RNG seed for synchronized random hopping (0 for default)
   */
  void SetChannelHopping(const std::vector<uint8_t>& channels,
                         Time hopInterval,
                         bool randomHopping,
                         uint32_t seed = 0);

  /**
   * \brief Configure wideband span
   * \param spanLength Number of 20MHz bins to extend in each direction
   * \param maxSpanLength Maximum span for random selection
   * \param randomSpan Whether to randomize span on each hop
   */
  void SetWidebandSpan(uint8_t spanLength, uint8_t maxSpanLength, bool randomSpan);

protected:
  virtual void ScheduleNext() override;
  virtual void DoTransmit() override;

private:
  double m_centerFreq;
  double m_pulseWidth;
  double m_pri;           // Pulse Repetition Interval
  int m_pulsesPerBurst;
  Time m_burstInterval;
  int m_currentPulse;     // Current pulse within burst
  Time m_nextBurstTime;   // Time for next burst

  // Channel hopping (synchronized with VirtualRadarInterferer)
  std::vector<uint8_t> m_dfsChannels;   ///< List of center channels to hop across
  Time m_hopInterval;                    ///< Time between channel hops
  bool m_randomHopping;                  ///< Whether to hop randomly or sequentially
  size_t m_currentChannelIndex;          ///< Current index in channel list
  Time m_lastHopTime;                    ///< When we last hopped channels
  Ptr<UniformRandomVariable> m_hopRng;   ///< RNG for random hopping

  // Wideband span configuration
  uint8_t m_spanLength;                  ///< Current span: ±length channels affected
  uint8_t m_maxSpanLength;               ///< Maximum span length for random selection
  bool m_randomSpan;                     ///< Whether to randomize span on each hop

  void DoChannelHop();
  static double ChannelToFrequency(uint8_t channel);
};

} // namespace ns3

#endif /* SPECTROGRAM_GENERATION_H */