#ifndef METRICS_HELPER_H
#define METRICS_HELPER_H

#include "ns3/net-device-container.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy.h"
#include "ns3/mac48-address.h"
#include "ns3/lever-api.h"
#include "ns3/kafka-producer.h"
#include "ns3/beacon-neighbor-model.h"
#include "ns3/nstime.h"
#include "ns3/log.h"

#include <map>
#include <functional>
#include <string>
#include <sstream>

namespace ns3
{

/**
 * @ingroup simulation-helper
 * @brief Helper class for managing AP and STA metrics initialization and updates
 *
 * This class provides utility functions to:
 * - Initialize metrics structures from device containers
 * - Update metrics using predicate-based search
 * - Build mapping structures for Kafka/LeverAPI integration
 */
class MetricsHelper
{
  public:
    /**
     * @brief Initialize AP metrics from device containers
     *
     * Creates an ApMetrics structure for each AP device with default values.
     * All metric fields are initialized to zero/empty except node ID, BSSID, and channel info.
     *
     * @param apDevices Container of AP WiFi devices
     * @param channels Vector of channel numbers (must match device count)
     * @param band WiFi PHY band (default: 5 GHz)
     * @return Map from node ID to initialized ApMetrics
     */
    static std::map<uint32_t, ApMetrics> InitializeApMetrics(
        const NetDeviceContainer& apDevices,
        const std::vector<uint8_t>& channels,
        WifiPhyBand band = WIFI_PHY_BAND_5GHZ);


    /**
     * @brief Generic metrics finder and updater with predicate
     *
     * Searches through a metrics map using a predicate, and if found, applies an update function.
     * This eliminates the repetitive pattern of:
     *   - Loop through metrics
     *   - Find matching entry
     *   - Update fields
     *   - Set lastUpdate timestamp
     *   - Log warning if not found
     *
     * @tparam MetricsType The type of metrics (ApMetrics or StaMetrics)
     * @tparam PredicateFunc Function type for the search predicate
     * @tparam UpdateFunc Function type for the update operation
     *
     * @param metricsMap Reference to the metrics map to search/update
     * @param predicate Function that returns true for the metrics entry to update
     * @param updateFunc Function that performs the update on the found metrics
     * @param logContext String for logging context (e.g., "OnStaReportReceived")
     * @return true if metrics were found and updated, false otherwise
     *
     * Example usage:
     * @code
     * MetricsHelper::UpdateMetricsIf<StaMetrics>(
     *     g_staMetrics,
     *     [&](const StaMetrics& m) { return m.isAssociated && m.currentBssid == from; },
     *     [&](StaMetrics& m) {
     *         m.currentRssi = rcpiDbm;
     *         m.currentSnr = rsniDb;
     *         m.lastLinkQualityUpdate = Simulator::Now();
     *     },
     *     "OnStaReportReceived"
     * );
     * @endcode
     */
    template <typename MetricsType, typename PredicateFunc, typename UpdateFunc>
    static bool UpdateMetricsIf(std::map<uint32_t, MetricsType>& metricsMap,
                                PredicateFunc predicate,
                                UpdateFunc updateFunc,
                                const std::string& logContext = "")
    {
        for (auto& [nodeId, metrics] : metricsMap)
        {
            if (predicate(metrics))
            {
                updateFunc(metrics);
                metrics.lastUpdate = Simulator::Now();
                return true;
            }
        }

        // Metrics entry not found - caller should handle logging if needed
        return false;
    }

    /**
     * @brief Build BSSID to LeverConfig mapping for Kafka Consumer
     *
     * Creates a string-based BSSID to LeverConfig map by combining AP metrics
     * (which contain BSSIDs) with the LeverConfig map (indexed by node ID).
     *
     * This mapping allows Kafka Consumer to apply received AP parameters to the
     * correct LeverConfig based on BSSID strings in the Kafka message.
     *
     * @param apMetrics Map of AP metrics (contains BSSID information)
     * @param leverConfigs Map of LeverConfig objects indexed by node ID
     * @return Map from BSSID string to LeverConfig pointer
     */
    static std::map<std::string, Ptr<LeverConfig>> BuildBssidToLeverConfigMap(
        const std::map<uint32_t, ApMetrics>& apMetrics,
        const std::map<uint32_t, Ptr<LeverConfig>>& leverConfigs);

    /**
     * @brief Extract MAC addresses from WiFi device container
     *
     * Convenience function to get all MAC addresses from a device container.
     *
     * @param devices Container of WiFi devices
     * @return Vector of MAC addresses
     */
    static std::vector<Mac48Address> ExtractMacAddresses(const NetDeviceContainer& devices);


    /**
     * @brief Find AP metrics by BSSID
     *
     * @param metricsMap Reference to AP metrics map
     * @param bssid BSSID to search for
     * @return Pointer to ApMetrics if found, nullptr otherwise
     */
    static ApMetrics* FindApByBssid(std::map<uint32_t, ApMetrics>& metricsMap,
                                    Mac48Address bssid);

    /**
     * @brief Find AP metrics by node ID
     *
     * @param metricsMap Reference to AP metrics map
     * @param nodeId Node ID to search for
     * @return Pointer to ApMetrics if found, nullptr otherwise
     */
    static ApMetrics* FindApByNodeId(std::map<uint32_t, ApMetrics>& metricsMap,
                                     uint32_t nodeId);

    // ========================================================================
    // SPECIALIZED UPDATE FUNCTIONS
    // ========================================================================

    /**
     * @brief Update AP client list (add or remove client)
     *
     * Finds AP by BSSID and updates its client list and associated client count.
     *
     * @param apMetrics Reference to AP metrics map
     * @param bssid BSSID of the AP
     * @param clientMac MAC address of the client
     * @param isAdding true to add client, false to remove
     * @return true if AP was found and updated, false otherwise
     */
    static bool UpdateApClientList(std::map<uint32_t, ApMetrics>& apMetrics,
                                   Mac48Address bssid,
                                   Mac48Address clientMac,
                                   bool isAdding);


    /**
     * @brief Build connection metrics structure
     *
     * Helper to construct a ConnectionMetrics object with all fields populated.
     *
     * @param srcMac Source MAC address
     * @param dstMac Destination MAC address
     * @param meanLatency Mean latency in milliseconds
     * @param jitter Jitter in milliseconds
     * @param packetCount Number of packets
     * @param throughputMbps Throughput in Mbps
     * @return Populated ConnectionMetrics structure
     */
    static ConnectionMetrics BuildConnectionMetrics(const std::string& srcMac,
                                                    const std::string& dstMac,
                                                    double meanLatency,
                                                    double jitter,
                                                    uint32_t packetCount,
                                                    double throughputMbps);

    /**
     * @brief Extract node ID from context string
     *
     * Parses context strings like "/NodeList/3/$ns3::Node/DeviceList/0/..."
     * to extract the node ID.
     *
     * @param context Context string from trace callback
     * @return Node ID, or 0 if parsing fails
     */
    static uint32_t ExtractNodeIdFromContext(const std::string& context);
};

} // namespace ns3

#endif // METRICS_HELPER_H
