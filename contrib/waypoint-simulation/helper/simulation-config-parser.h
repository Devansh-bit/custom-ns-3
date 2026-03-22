#ifndef SIMULATION_CONFIG_PARSER_H
#define SIMULATION_CONFIG_PARSER_H

#include "ns3/vector.h"
#include "waypoint-mobility-helper.h"

#include <string>
#include <vector>

namespace ns3
{

/**
 * @ingroup waypoint-simulation
 * @brief Configuration for an Access Point
 */
struct ApConfigData
{
    uint32_t nodeId;              //!< Node ID
    Vector position;              //!< Position (x, y, z)
    double txPower;               //!< TX power (dBm)
    uint8_t channel;              //!< WiFi channel number
    double ccaThreshold;          //!< CCA-ED threshold (dBm)
    double rxSensitivity;         //!< RX sensitivity (dBm)
};

/**
 * @ingroup waypoint-simulation
 * @brief Waypoint position data
 */
struct WaypointData
{
    uint32_t id;                  //!< Waypoint ID
    Vector position;              //!< Position (x, y, z)
};

/**
 * @ingroup waypoint-simulation
 * @brief Interference configuration data
 */
struct InterferenceConfigData
{
    bool enabled;                 //!< Whether interference is enabled
    Vector position;              //!< Position of interference source (x, y, z)
    uint32_t numSources;          //!< Number of interference sources
    double startTime;             //!< Start time for interference (seconds)
    double centerFrequencyGHz;    //!< Center frequency in GHz
    double bandwidthMHz;          //!< Bandwidth in MHz
    double powerPsdDbmHz;         //!< Power spectral density in dBm/Hz
};

/**
 * @ingroup waypoint-simulation
 * @brief Adjacent Channel Interference (ACI) degradation factors
 */
struct AciDegradationData
{
    double throughputFactor = 0.3;   //!< At IF=1: 30% throughput reduction
    double packetLossFactor = 5.0;   //!< At IF=1: +5% packet loss
    double latencyFactor = 0.5;      //!< At IF=1: 50% latency increase
    double jitterFactor = 0.4;       //!< At IF=1: 40% jitter increase
    double channelUtilFactor = 0.15; //!< At IF=1: +15% utilization
};

/**
 * @ingroup waypoint-simulation
 * @brief Adjacent Channel Interference (ACI) simulation configuration
 */
struct AciConfigData
{
    bool enabled = false;                   //!< Whether ACI simulation is enabled
    double pathLossExponent = 3.0;          //!< Path loss exponent for distance calculation
    double maxInterferenceDistanceM = 50.0; //!< Max distance for interference (meters)
    double clientWeightFactor = 0.1;        //!< Weight per associated client
    AciDegradationData degradation;         //!< Metric degradation factors
};

/**
 * @ingroup waypoint-simulation
 * @brief OFDMA benefit improvement factors
 */
struct OfdmaImprovementData
{
    double throughputFactor = 0.35;   //!< At BF=1: 35% throughput increase
    double latencyFactor = 0.45;      //!< At BF=1: 45% latency reduction
    double jitterFactor = 0.50;       //!< At BF=1: 50% jitter reduction
    double packetLossFactor = 0.20;   //!< At BF=1: 20% packet loss reduction
    double channelUtilFactor = 0.25;  //!< At BF=1: 25% channel util improvement
};

/**
 * @ingroup waypoint-simulation
 * @brief OFDMA effects simulation configuration
 */
struct OfdmaConfigData
{
    bool enabled = false;              //!< Whether OFDMA simulation is enabled
    uint8_t minStasForBenefit = 2;     //!< Min STAs per AP to enable OFDMA benefit
    uint8_t saturationStaCount = 9;    //!< STAs at which benefit saturates (max RU split)
    OfdmaImprovementData improvement;  //!< Metric improvement factors
};

/**
 * @ingroup waypoint-simulation
 * @brief Channel scoring configuration data for ACS/DCS
 */
struct ChannelScoringConfigData
{
    bool enabled = true;           //!< Whether channel scoring is enabled
    double weightBssid = 0.5;      //!< Weight for BSSID count score
    double weightRssi = 0.5;       //!< Weight for RSSI score
    double weightNonWifi = 0.5;    //!< Weight for non-WiFi interference score
    double weightOverlap = 0.5;    //!< Weight for channel overlap score
    double nonWifiDiscardThreshold = 40.0;  //!< Discard channels with >X% non-WiFi
};

/**
 * @ingroup waypoint-simulation
 * @brief Schedule configuration for interferers (on/off cycling)
 */
struct ScheduleConfig
{
    double onDuration = 0.0;       //!< Duration interferer is ON (seconds)
    double offDuration = 0.0;      //!< Duration interferer is OFF (seconds)
    bool hasSchedule = false;      //!< Whether this interferer has scheduling enabled
};

/**
 * @ingroup waypoint-simulation
 * @brief Microwave interferer configuration data
 */
struct MicrowaveInterfererConfigData
{
    Vector position;               //!< Position (x, y, z)
    double txPowerDbm;             //!< Transmit power in dBm (-35 to -15 typical)
    double centerFrequencyGHz;     //!< Center frequency in GHz (2.45 typical)
    double bandwidthMHz;           //!< Bandwidth in MHz (50-100 typical)
    double dutyCycle;              //!< Duty cycle 0.0-1.0 (0.4-0.6 typical)
    double startTime;              //!< Start time (seconds)
    bool active;                   //!< Whether interferer is active
    ScheduleConfig schedule;       //!< On/off schedule configuration
};

/**
 * @ingroup waypoint-simulation
 * @brief Bluetooth interferer configuration data
 */
struct BluetoothInterfererConfigData
{
    Vector position;               //!< Position (x, y, z)
    double txPowerDbm;             //!< Transmit power in dBm (0/4/20 for Class 3/2/1)
    uint8_t hoppingSeed;           //!< LFSR seed for frequency hopping (1-127)
    double hopInterval;            //!< Hop interval in seconds (0.000625 typical)
    std::string profile;           //!< Profile: "HID", "AUDIO_STREAMING", "DATA_TRANSFER", "BLE"
    double startTime;              //!< Start time (seconds)
    bool active;                   //!< Whether interferer is active
    ScheduleConfig schedule;       //!< On/off schedule configuration
};

/**
 * @ingroup waypoint-simulation
 * @brief Cordless phone interferer configuration data
 */
struct CordlessInterfererConfigData
{
    Vector position;               //!< Position (x, y, z)
    double txPowerDbm;             //!< Transmit power in dBm (4-10 typical for DECT)
    uint32_t numHops;              //!< Number of frequency hops (100 typical)
    double hopInterval;            //!< Hop interval in seconds (0.01 = 10ms for DECT)
    double bandwidthMhz;           //!< Bandwidth in MHz (1.728 for DECT)
    uint8_t hoppingSeed;           //!< LFSR seed for frequency hopping (1-127)
    double startTime;              //!< Start time (seconds)
    bool active;                   //!< Whether interferer is active
    ScheduleConfig schedule;       //!< On/off schedule configuration
};

/**
 * @ingroup waypoint-simulation
 * @brief ZigBee interferer configuration data
 */
struct ZigbeeInterfererConfigData
{
    Vector position;               //!< Position (x, y, z)
    double txPowerDbm;             //!< Transmit power in dBm (0 typical)
    uint8_t zigbeeChannel;         //!< ZigBee channel 11-26
    double bandwidthMHz;           //!< Bandwidth in MHz (2 for ZigBee)
    std::string networkType;       //!< Network type: "SENSOR", "CONTROL", "LIGHTING"
    double dutyCycle;              //!< Duty cycle 0.0-1.0 (0.01-0.05 typical)
    double startTime;              //!< Start time (seconds)
    bool active;                   //!< Whether interferer is active
    ScheduleConfig schedule;       //!< On/off schedule configuration
};

/**
 * @ingroup waypoint-simulation
 * @brief Radar interferer configuration data
 */
struct RadarInterfererConfigData
{
    Vector position;               //!< Position (x, y, z)
    double txPowerDbm;             //!< Transmit power in dBm (30-40 typical)
    double centerFrequencyGHz;     //!< Center frequency in GHz (5.18-5.825 for DFS)
    uint8_t dfsChannel;            //!< DFS channel (52,56,60,64,100-144)
    double pulseDuration;          //!< Pulse duration in seconds (0.0005-0.005)
    double pulseInterval;          //!< Pulse interval in seconds (0.0001-0.001)
    std::string radarType;         //!< Radar type: "WEATHER", "MILITARY", "AVIATION"
    double startTime;              //!< Start time (seconds)
    bool active;                   //!< Whether interferer is active
    ScheduleConfig schedule;       //!< On/off schedule configuration
    // Channel hopping configuration
    std::vector<uint8_t> dfsChannels;  //!< List of DFS channels to hop across
    double hopIntervalSec;             //!< Interval between channel hops (seconds)
    bool randomHopping;                //!< Whether to hop randomly or sequentially
    // Wideband span configuration
    uint8_t spanLength;                //!< Number of 20MHz bins to extend in each direction (±span)
    uint8_t maxSpanLength;             //!< Maximum span length for random selection
    bool randomSpan;                   //!< Whether to randomize span on each hop
};

/**
 * @ingroup waypoint-simulation
 * @brief Virtual interferers environment configuration
 */
struct VirtualInterfererEnvConfigData
{
    bool enabled;                                                 //!< Whether virtual interferers are enabled
    double updateInterval;                                        //!< Environment update interval (seconds)
    std::vector<MicrowaveInterfererConfigData> microwaves;        //!< Microwave interferer configs
    std::vector<BluetoothInterfererConfigData> bluetooths;        //!< Bluetooth interferer configs
    std::vector<CordlessInterfererConfigData> cordless;           //!< Cordless phone interferer configs
    std::vector<ZigbeeInterfererConfigData> zigbees;              //!< ZigBee interferer configs
    std::vector<RadarInterfererConfigData> radars;                //!< Radar interferer configs
};

/**
 * @ingroup waypoint-simulation
 * @brief Complete simulation configuration
 */
struct SimulationConfigData
{
    double simulationTime;                        //!< Simulation duration (seconds)
    std::vector<ApConfigData> aps;                //!< AP configurations
    std::vector<WaypointData> waypoints;          //!< Waypoint positions
    std::vector<StaMobilityConfig> stas;          //!< STA mobility configurations
    std::vector<uint8_t> scanningChannels;        //!< Scanning channel list for DualPhySniffer
    double channelHopDurationMs;                  //!< Channel hop duration in milliseconds (default 300)
    double bssOrchestrationRssiThreshold;         //!< RSSI threshold for BSS orchestration (dBm), default -70
    InterferenceConfigData interference;          //!< Interference configuration (legacy/SpectrumWifiPhy)
    VirtualInterfererEnvConfigData virtualInterferers;  //!< Virtual interferer configuration
    ChannelScoringConfigData channelScoring;      //!< Channel scoring configuration for ACS/DCS
    AciConfigData aci;                            //!< Adjacent Channel Interference simulation config
    OfdmaConfigData ofdma;                        //!< OFDMA effects simulation config
};

/**
 * @ingroup waypoint-simulation
 * @brief Parser for JSON-based simulation configuration files
 *
 * This class uses RapidJSON to parse configuration files containing:
 * - Access Point configurations (position, LeverAPI settings)
 * - Waypoint grid layout
 * - STA mobility parameters
 */
class SimulationConfigParser
{
  public:
    /**
     * @brief Parse a JSON configuration file
     * @param filename Path to JSON configuration file
     * @return Parsed simulation configuration
     */
    static SimulationConfigData ParseFile(const std::string& filename);

  private:
    /**
     * @brief Parse a position object from JSON
     * @param posObj JSON value containing x, y, z fields
     * @return Position vector
     */
    static Vector ParsePosition(const void* posObj);
};

} // namespace ns3

#endif // SIMULATION_CONFIG_PARSER_H
