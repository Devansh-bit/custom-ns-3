/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef DUAL_PHY_SNIFFER_HELPER_H
#define DUAL_PHY_SNIFFER_HELPER_H

#include "ns3/yans-wifi-helper.h"
#include "ns3/spectrum-wifi-helper.h"
#include "ns3/wifi-helper.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy-band.h"
#include "ns3/ssid.h"
#include "ns3/node.h"
#include "ns3/callback.h"
#include "ns3/mac48-address.h"
#include "ns3/parsed-frame-context.h"  // For unified sniffer callback
#include <vector>
#include <map>

namespace ns3
{

class UnifiedPhySniffer;  // Forward declaration

/**
 * \brief Measurement data captured by the scanning radio
 *
 * This structure contains all information about a detected beacon.
 * Can be used for various applications: RRM matrices, neighbor discovery,
 * beacon reports, channel quality assessment, etc.
 */
struct DualPhyMeasurement
{
    Mac48Address receiverBssid;    ///< Operating BSSID of the device (AP or STA) that captured the beacon
    Mac48Address transmitterBssid; ///< BSSID of the AP that transmitted the beacon
    uint8_t channel;               ///< Channel number where beacon was detected (scanning radio's channel)
    double rcpi;                   ///< RCPI value (0-220), calculated from RSSI
    double rssi;                   ///< Raw RSSI in dBm
    double timestamp;              ///< Simulation time when measurement was taken
};

/**
 * \brief Stored beacon information in the sniffer cache
 *
 * This structure stores comprehensive beacon data for later retrieval.
 * Used internally by the helper to maintain beacon cache.
 * Applications query this cached data instead of processing real-time callbacks.
 */
struct BeaconInfo
{
    Mac48Address bssid;              ///< BSSID of the transmitting AP
    Mac48Address receivedBy;         ///< BSSID of the device (AP or STA) that received this beacon
    double rssi;                     ///< RSSI in dBm (latest measurement)
    double rcpi;                     ///< RCPI value (0-220)
    double snr;                      ///< Signal-to-Noise Ratio in dB
    uint8_t channel;                 ///< Channel number where beacon was detected
    WifiPhyBand band;                ///< Frequency band (2.4/5/6 GHz)
    Time timestamp;                  ///< Time when beacon was last received
    uint32_t beaconCount;            ///< Number of beacons received from this BSSID

    // Information Elements data (parsed from beacon frame)
    uint16_t channelWidth;           ///< Channel width in MHz (from HT/VHT/HE Capability IE), default 20
    uint16_t staCount;               ///< Number of associated STAs (from BSS Load IE), default 0
    uint8_t channelUtilization;      ///< Channel utilization 0-255 (from BSS Load IE), default 0
    uint8_t wifiUtilization;         ///< WiFi channel utilization 0-255 (from BSS Load IE AAC high byte), default 0
    uint8_t nonWifiUtilization;      ///< Non-WiFi channel utilization 0-255 (from BSS Load IE AAC low byte), default 0
};

/**
 * \ingroup wifi
 * \brief Helper class to create and manage dual-PHY WiFi sniffers
 *
 * This helper creates a dual-radio setup on each node:
 * - Operating PHY: Fixed channel, broadcasts beacons, serves clients (even MAC addresses)
 * - Scanning PHY: Dynamically hops channels, monitors beacons passively (odd MAC addresses)
 *
 * Use cases:
 * - Building RRM interference matrices (RCPI measurements between devices)
 * - Beacon report generation (802.11k)
 * - Neighbor discovery and channel quality assessment
 * - Passive WiFi scanning and monitoring
 *
 * Key features:
 * - Automatically filters out beacons from scanning radios (only reports operating radio beacons)
 * - Provides both operating and scanning MAC addresses for tracking
 * - Configurable channel hopping schedule and interval
 * - Callback-based architecture for flexible data processing
 */
class DualPhySnifferHelper
{
  public:
    /**
     * \brief Callback type for beacon measurements
     *
     * Called whenever the scanning radio detects a beacon from an operating radio.
     * Use this callback to process beacon data for your specific application.
     *
     * Parameters: DualPhyMeasurement struct with RCPI, RSSI, TX MAC, RX MAC, channel, timestamp
     */
    typedef Callback<void, const DualPhyMeasurement&> MeasurementCallback;

    /**
     * Constructor
     */
    DualPhySnifferHelper();

    /**
     * Destructor
     */
    ~DualPhySnifferHelper();

    /**
     * \brief Set the shared Yans WiFi channel for all radios
     * \param channel The shared YansWifiChannel used by all devices
     */
    void SetChannel(Ptr<YansWifiChannel> channel);

    /**
     * \brief Set the shared Spectrum channel for all radios
     * \param channel The shared SpectrumChannel used by all devices
     */
    void SetChannel(Ptr<SpectrumChannel> channel);

    /**
     * \brief Set channels for scanning radio to hop through
     * \param channels Vector of channel numbers (e.g., {36, 40, 44, 48})
     */
    void SetScanningChannels(const std::vector<uint8_t>& channels);

    /**
     * \brief Set channel hop interval
     * \param interval Time between channel hops (default: 0.3 seconds)
     */
    void SetHopInterval(Time interval);

    /**
     * \brief Set measurement callback to receive RCPI/MAC data
     * \param callback Function to call when beacon is detected
     */
    void SetMeasurementCallback(MeasurementCallback callback);

    /**
     * \brief Set the shared SSID for all operating radios
     * \param ssid The SSID to use for the RRM network
     *
     * All APs will broadcast this shared SSID, allowing STAs to roam
     * seamlessly between APs. If not set, defaults to "RRM-Network".
     * The scanning radio operates independently and does not use this SSID.
     */
    void SetSsid(Ssid ssid);

    /**
     * \brief Set the list of valid AP BSSIDs for beacon filtering
     * \param apBssids Set of MAC addresses that are valid beacon sources (APs only)
     *
     * Only beacons from these addresses will be stored in the cache.
     * This prevents storing beacons from invalid sources like STAs or null addresses.
     */
    void SetValidApBssids(const std::set<Mac48Address>& apBssids);

    /**
     * \brief Install dual-PHY sniffer on a node
     * \param node The node to install the sniffer on (typically an AP or STA)
     * \param operatingChannel Channel for the operating radio (36, 40, 44, etc.)
     * \param desiredBssid Desired BSSID (will be auto-assigned by ns-3, use GetOperatingMac to retrieve actual)
     * \return NetDeviceContainer with the operating radio's device
     *
     * This method creates two WiFi devices on the node:
     * 1. Scanning PHY (created first) - Gets odd MAC address (01, 03, 05, ...)
     *    - Starts on first scanning channel
     *    - Will hop through all scanning channels when StartChannelHopping() is called
     *    - Passive monitoring only (StaWifiMac, no association)
     *
     * 2. Operating PHY (created second) - Gets even MAC address (02, 04, 06, ...)
     *    - Fixed on operatingChannel
     *    - ApWifiMac with beacon generation enabled
     *    - Can serve clients (though not configured in this example)
     *
     * After installation, use GetOperatingMac() and GetScanningMac() to retrieve actual MAC addresses.
     */
    NetDeviceContainer Install(Ptr<Node> node, uint8_t operatingChannel, Mac48Address desiredBssid);

    /**
     * \brief Get the operating radio's MAC address for a node
     * \param nodeId The node ID
     * \return Operating radio's MAC address (even number), or Mac48Address("00:00:00:00:00:00") if not found
     */
    Mac48Address GetOperatingMac(uint32_t nodeId) const;

    /**
     * \brief Get the scanning radio's MAC address for a node
     * \param nodeId The node ID
     * \return Scanning radio's MAC address (odd number), or Mac48Address("00:00:00:00:00:00") if not found
     */
    Mac48Address GetScanningMac(uint32_t nodeId) const;

    /**
     * \brief Start channel hopping for all scanning radios
     *
     * Call this after all nodes have been installed to begin beacon monitoring.
     * Each scanning radio will hop through the configured channels at the specified interval.
     */
    void StartChannelHopping();

    /**
     * \brief Get all stored beacon information
     * \return Map of (receiverBssid, transmitterBssid) -> BeaconInfo
     *
     * Returns the complete beacon cache. Applications can iterate through
     * this map to build interference matrices, neighbor lists, etc.
     */
    const std::map<std::pair<Mac48Address, Mac48Address>, BeaconInfo>& GetAllBeacons() const;

    /**
     * \brief Get beacons received by a specific device
     * \param receiverBssid The BSSID of the receiving device (AP or STA)
     * \return Vector of BeaconInfo for all beacons received by this device
     */
    std::vector<BeaconInfo> GetBeaconsReceivedBy(Mac48Address receiverBssid) const;

    /**
     * \brief Get beacons transmitted by a specific AP
     * \param transmitterBssid The BSSID of the transmitting AP
     * \return Vector of BeaconInfo for all devices (APs or STAs) that heard this transmitter
     */
    std::vector<BeaconInfo> GetBeaconsFrom(Mac48Address transmitterBssid) const;

    /**
     * \brief Clear all stored beacon data
     */
    void ClearBeaconCache();

    /**
     * \brief Clear beacon data for a specific receiver
     * \param receiverBssid The BSSID of the receiver whose beacon cache should be cleared
     *
     * This removes all beacon entries received by the specified BSSID,
     * useful for forcing a fresh scan when roaming.
     */
    void ClearBeaconsReceivedBy(Mac48Address receiverBssid);

    /**
     * \brief Set maximum age for beacon entries
     * \param maxAge Maximum time before beacon entries are purged
     *
     * Old entries are removed when new beacons arrive.
     * Default: No expiration (beacons stored forever)
     */
    void SetBeaconMaxAge(Time maxAge);

    /**
     * \brief Set maximum number of beacon entries
     * \param maxEntries Maximum number of beacon cache entries
     *
     * When exceeded, oldest entries are removed.
     * Default: No limit
     */
    void SetBeaconMaxEntries(uint32_t maxEntries);

  private:
    /**
     * \brief Determine WiFi band from channel number
     * \param channel Channel number
     * \return WifiPhyBand (2.4/5/6 GHz)
     *
     * Uses IEEE 802.11 channel assignments:
     * - 2.4 GHz: Channels 1-14
     * - 5 GHz: Channels 36-177
     * - 6 GHz: Channels 1-233 (not overlapping with 2.4 GHz after checking 5 GHz)
     */
    static WifiPhyBand GetBandForChannel(uint8_t channel);

    /**
     * \brief Structure to hold context for trace callback
     */
    struct TraceContext
    {
        DualPhySnifferHelper* helper;
        Mac48Address rxBssid;
    };

    /// Store trace contexts to keep them alive
    std::vector<TraceContext*> m_traceContexts;

    /**
     * \brief Static wrapper for MonitorSnifferRx trace
     */
    static void MonitorSnifferRxStatic(TraceContext* ctx,
                                       Ptr<const Packet> packet,
                                       uint16_t channelFreq,
                                       WifiTxVector txVector,
                                       MpduInfo mpdu,
                                       SignalNoiseDbm signalNoise,
                                       uint16_t staId);

    /**
     * \brief Internal callback for MonitorSnifferRx trace (legacy)
     *
     * Called when scanning radio receives any packet. Filters for beacons
     * and converts to RrmMeasurement before invoking user callback.
     */
    void ScanningRadioCallback(Mac48Address rxBssid,
                               Ptr<const Packet> packet,
                               uint16_t channelFreq,
                               WifiTxVector txVector,
                               MpduInfo mpdu,
                               SignalNoiseDbm signalNoise,
                               uint16_t staId);

    /**
     * \brief Unified sniffer callback for beacon processing
     *
     * Called by UnifiedPhySniffer when a beacon is received.
     * Uses pre-computed RSSI/SNR/RCPI/channel from the context,
     * eliminating redundant calculations.
     *
     * \param rxBssid The receiver's BSSID (for self-beacon filtering)
     * \param ctx Pre-parsed frame context with signal measurements
     */
    void UnifiedBeaconCallback(Mac48Address rxBssid, ParsedFrameContext* ctx);

    /**
     * \brief Schedule next channel hop for a scanning radio
     * \param scanMac The scanning MAC to use for channel switching
     * \param scanPhy The scanning PHY (kept for compatibility)
     * \param deviceBssid BSSID of the device (AP or STA) using this sniffer (for logging)
     */
    void ScheduleNextHop(Ptr<WifiMac> scanMac, Ptr<WifiPhy> scanPhy, Mac48Address deviceBssid);

    /**
     * \brief Perform channel hop on scanning radio using MAC-level SwitchChannel
     * \param scanMac The scanning MAC to use for channel switching
     * \param scanPhy The scanning PHY (kept for compatibility)
     * \param deviceBssid BSSID of the device (AP or STA) using this sniffer (for logging)
     */
    void HopChannel(Ptr<WifiMac> scanMac, Ptr<WifiPhy> scanPhy, Mac48Address deviceBssid);

    Ptr<YansWifiChannel> m_yansChannel;          ///< Shared Yans WiFi channel for all radios
    Ptr<SpectrumChannel> m_spectrumChannel;      ///< Shared Spectrum channel for all radios
    std::vector<uint8_t> m_scanningChannels;     ///< Channels to hop through
    Time m_hopInterval;                          ///< Time between hops
    MeasurementCallback m_measurementCallback;   ///< User callback for measurements
    Ssid m_ssid;                                 ///< Shared SSID for all operating radios

    /// Track scanning MACs and PHYs for each device (MAC, PHY, BSSID)
    std::vector<std::tuple<Ptr<WifiMac>, Ptr<WifiPhy>, Mac48Address>> m_scanningDevices;

    /// UnifiedPhySniffer instances (must persist for callbacks to work)
    std::vector<Ptr<UnifiedPhySniffer>> m_unifiedSniffers;

    /// Current hop index for each scanning device (keyed by MAC pointer)
    std::map<Ptr<WifiMac>, uint32_t> m_hopIndex;

    /// Map node ID to operating MAC address (even MACs: 02, 04, 06, ...)
    std::map<uint32_t, Mac48Address> m_nodeToOperatingMac;

    /// Map node ID to scanning MAC address (odd MACs: 01, 03, 05, ...)
    std::map<uint32_t, Mac48Address> m_nodeToScanningMac;

    /// Beacon cache: maps (receiver BSSID, transmitter BSSID) -> BeaconInfo
    std::map<std::pair<Mac48Address, Mac48Address>, BeaconInfo> m_beaconCache;

    /// Maximum age for beacon entries (0 = no expiration)
    Time m_beaconMaxAge;

    /// Maximum number of beacon cache entries (0 = no limit)
    uint32_t m_beaconMaxEntries;

    /// Valid AP BSSIDs for beacon filtering (empty = accept all)
    std::set<Mac48Address> m_validApBssids;

    /**
     * \brief Store or update beacon in cache
     * \param measurement The measurement data from scanning radio
     * \param snr Signal-to-noise ratio
     * \param staCount Station count from BSS Load IE (default 0)
     * \param channelUtil Channel utilization from BSS Load IE (default 0)
     * \param channelWidth Channel width from HT/VHT/HE capabilities (default 20 MHz)
     * \param wifiUtil WiFi channel utilization from AAC high byte (default 0)
     * \param nonWifiUtil Non-WiFi channel utilization from AAC low byte (default 0)
     */
    void StoreBeacon(const DualPhyMeasurement& measurement, double snr,
                     uint16_t staCount = 0, uint8_t channelUtil = 0, uint16_t channelWidth = 20,
                     uint8_t wifiUtil = 0, uint8_t nonWifiUtil = 0);

    /**
     * \brief Purge old beacon entries based on age and max entries
     */
    void PurgeOldBeacons();
};

} // namespace ns3

#endif /* DUAL_PHY_SNIFFER_HELPER_H */
