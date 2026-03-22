#ifndef SIMULATION_HELPER_HELPER_H
#define SIMULATION_HELPER_HELPER_H

#include "ns3/auto-roaming-kv-helper.h"
#include "ns3/beacon-neighbor-model.h"
#include "ns3/link-measurement-protocol.h"

#include <vector>
#include <map>
#include <tuple>

namespace ns3
{

/**
 * \ingroup simulation-helper
 * \brief Helper class to provide modular setup functions for WiFi simulation components
 *
 * This class provides reusable functions to setup common WiFi simulation helpers
 * including dual-PHY sniffers, neighbor/beacon protocols, BSS TM, roaming managers,
 * and auto-roaming components.
 */
class SimulationHelper
{
  public:
    /**
     * \brief Setup DualPhySniffer for multiple APs (RECOMMENDED for Neighbor Protocol)
     * \param apDevices Net device container with AP WiFi devices
     * \param apMacs Vector of AP MAC addresses
     * \param channel Shared Yans WiFi channel used by all devices
     * \param operatingChannels Vector of operating channels for each AP
     * \param scanningChannels Vector of channels to scan (e.g., {36, 40, 44, 48})
     * \param hopInterval Time interval between channel hops
     * \return Pointer to configured DualPhySnifferHelper for APs
     *
     * This creates a DualPhySniffer instance used by APs for Neighbor Protocol (802.11k).
     * APs scan neighboring channels to discover other APs and build neighbor tables.
     */
    static DualPhySnifferHelper* SetupAPDualPhySniffer(
        NetDeviceContainer apDevices,
        std::vector<Mac48Address> apMacs,
        Ptr<YansWifiChannel> channel,
        std::vector<uint8_t> operatingChannels,
        std::vector<uint8_t> scanningChannels,
        Time hopInterval);

    /**
     * \brief Setup DualPhySniffer for multiple STAs (RECOMMENDED for Beacon Protocol)
     * \param staDevices Net device container with STA WiFi devices
     * \param staMacs Vector of STA MAC addresses
     * \param channel Shared Yans WiFi channel used by all devices
     * \param operatingChannels Vector of operating channels for each STA
     * \param scanningChannels Vector of channels to scan (e.g., {36, 40, 44, 48})
     * \param hopInterval Time interval between channel hops
     * \return Pointer to configured DualPhySnifferHelper for STAs
     *
     * This creates a DualPhySniffer instance used by STAs for Beacon Protocol (802.11k).
     * STAs scan neighboring channels to discover APs and generate beacon reports.
     */
    static DualPhySnifferHelper* SetupSTADualPhySniffer(
        NetDeviceContainer staDevices,
        std::vector<Mac48Address> staMacs,
        Ptr<YansWifiChannel> channel,
        std::vector<uint8_t> operatingChannels,
        std::vector<uint8_t> scanningChannels,
        Time hopInterval);

    /**
     * \brief Setup NeighborProtocol for multiple APs and STAs (RECOMMENDED)
     * \param apDevices Net device container with AP WiFi devices
     * \param staDevices Net device container with STA WiFi devices
     * \param apMacs Vector of AP MAC addresses
     * \param neighborTable Vector of ApInfo structs describing all APs in the network
     * \param apDualPhySniffer Pointer to AP DualPhySniffer (used by APs only, can be nullptr)
     * \return Vector of NeighborProtocolHelper pointers (APs followed by STAs)
     *
     * Neighbor Protocol is used by APs to build neighbor tables. APs actively scan using
     * apDualPhySniffer to discover neighboring APs. STAs passively request neighbor reports
     * from their associated AP, so they don't need a sniffer instance.
     *
     * Typical usage: Pass apDualPhySniffer for APs, nullptr for STAs
     */
    static std::vector<Ptr<NeighborProtocolHelper>> SetupNeighborProtocol(
        NetDeviceContainer apDevices,
        NetDeviceContainer staDevices,
        std::vector<Mac48Address> apMacs,
        std::vector<ApInfo> neighborTable,
        DualPhySnifferHelper* apDualPhySniffer);

    /**
     * \brief Setup BeaconProtocol for multiple APs and STAs (RECOMMENDED)
     * \param apDevices Net device container with AP WiFi devices
     * \param staDevices Net device container with STA WiFi devices
     * \return Vector of BeaconProtocolHelper pointers (APs followed by STAs)
     *
     * Beacon Protocol is used by STAs to generate beacon reports (802.11k). STAs actively scan
     * using staDualPhySniffer to discover APs on neighboring channels. APs don't generate beacon
     * reports, so they don't need a sniffer instance.
     *
     * Typical usage: Pass nullptr for APs, staDualPhySniffer for STAs
     */
    static std::vector<Ptr<BeaconProtocolHelper>> SetupBeaconProtocol(
        NetDeviceContainer apDevices,
        NetDeviceContainer staDevices,
        DualPhySnifferHelper* staDualPhySniffer);

    /**
     * \brief Setup BssTm11vHelper for multiple APs and STAs
     * \param staDevices Net device container with STA WiFi devices
     * \return Vector of BssTm11vHelper pointers (APs followed by STAs)
     */
    static std::vector<Ptr<BssTm11vHelper>> SetupBssTmHelper(
        NetDeviceContainer apDevices,
        NetDeviceContainer staDevices);

    /**
     * \brief Container for multiple AutoRoamingKvHelper instances
     *
     * This structure holds the result of setting up AutoRoamingKvHelper for multiple STAs.
     * Each STA gets its own helper instance with its own protocol instances.
     */
    struct AutoRoamingKvHelperContainer
    {
        std::vector<AutoRoamingKvHelper*> helpers;                              // One helper per STA
        std::vector<std::vector<Ptr<LinkMeasurementProtocol>>> apProtocols;    // AP protocols per helper
        std::vector<std::vector<Ptr<LinkMeasurementProtocol>>> staProtocols;   // STA protocols per helper
    };

    // /**
    //  * \brief Setup AutoRoamingKvHelper for APs and STAs with full protocol chain (DEPRECATED)
    //  * \param apDevices Net device container with AP WiFi devices
    //  * \param staDevices Net device container with STA WiFi devices
    //  * \param neighborProtocolSta Neighbor protocol helper for STA
    //  * \param beaconProtocolSta Beacon protocol helper for STA
    //  * \param beaconProtocolAps Vector of beacon protocol helpers for APs
    //  * \param bssTmHelperSta BSS TM helper for STA
    //  * \param measurementInterval Interval between link measurements
    //  * \param rssiThreshold RSSI threshold for triggering neighbor requests
    //  * \return Tuple of: <helper, AP protocols, STA protocols>
    //  *
    //  * DEPRECATED: Use SetupAutoRoamingKvHelperMulti for multiple STAs.
    //  * This function only handles a single STA properly.
    //  */
    // static std::tuple<AutoRoamingKvHelper*,
    //                   std::vector<Ptr<LinkMeasurementProtocol>>,
    //                   std::vector<Ptr<LinkMeasurementProtocol>>> SetupAutoRoamingKvHelper(
    //     NetDeviceContainer apDevices,
    //     NetDeviceContainer staDevices,
    //     Ptr<NeighborProtocolHelper> neighborProtocolSta,
    //     Ptr<BeaconProtocolHelper> beaconProtocolSta,
    //     std::vector<Ptr<BeaconProtocolHelper>> beaconProtocolAps,
    //     Ptr<BssTm11vHelper> bssTmHelperSta,
    //     Time measurementInterval,
    //     double rssiThreshold);

    // /**
    //  * \brief Setup AutoRoamingKvHelper for multiple STAs with full protocol chain (RECOMMENDED)
    //  * \param apDevices Net device container with AP WiFi devices
    //  * \param staDevices Net device container with multiple STA WiFi devices
    //  * \param neighborProtocolStas Vector of neighbor protocol helpers (one per STA, with nullptr sniffers)
    //  * \param beaconProtocolStas Vector of beacon protocol helpers (one per STA, with staDualPhySniffer)
    //  * \param beaconProtocolAps Vector of beacon protocol helpers for all APs (with nullptr sniffers)
    //  * \param bssTmHelperStas Vector of BSS TM helpers (one per STA)
    //  * \param measurementInterval Interval between link measurements
    //  * \param rssiThreshold RSSI threshold for triggering neighbor requests
    //  * \return AutoRoamingKvHelperContainer with all helpers and protocols
    //  *
    //  * This function creates one AutoRoamingKvHelper instance per STA, each with its
    //  * own protocol instances. The helpers automatically detect which AP each STA is
    //  * currently associated with and manage the complete roaming chain:
    //  * Link Measurement → Neighbor Request → Beacon Request → BSS TM → Roaming
    //  *
    //  * Works with two-instance DualPhySniffer architecture:
    //  * - STAs' NeighborProtocol: nullptr sniffer (passive - request neighbor reports from AP)
    //  * - STAs' BeaconProtocol: staDualPhySniffer (active - scan neighboring channels)
    //  * - APs' BeaconProtocol: nullptr sniffer (can request beacon reports from STAs)
    //  *
    //  * Example usage:
    //  * \code
    //  * // After setting up protocols with SimulationHelper
    //  * std::vector<Ptr<NeighborProtocolHelper>> neighborProtos = ...;
    //  * std::vector<Ptr<BeaconProtocolHelper>> beaconProtos = ...;
    //  * std::vector<Ptr<BssTm11vHelper>> bssTmHelpers = ...;
    //  *
    //  * // Extract STA instances (assuming 3 APs + 3 STAs)
    //  * std::vector<Ptr<NeighborProtocolHelper>> neighborStas = {
    //  *     neighborProtos[3], neighborProtos[4], neighborProtos[5]
    //  * };
    //  * std::vector<Ptr<BeaconProtocolHelper>> beaconStas = {
    //  *     beaconProtos[3], beaconProtos[4], beaconProtos[5]
    //  * };
    //  * std::vector<Ptr<BeaconProtocolHelper>> beaconAps = {
    //  *     beaconProtos[0], beaconProtos[1], beaconProtos[2]
    //  * };
    //  * std::vector<Ptr<BssTm11vHelper>> bssTmStas = {
    //  *     bssTmHelpers[3], bssTmHelpers[4], bssTmHelpers[5]
    //  * };
    //  *
    //  * // Setup roaming helpers
    //  * auto container = SimulationHelper::SetupAutoRoamingKvHelperMulti(
    //  *     apDevices, staDevices,
    //  *     neighborStas, beaconStas, beaconAps, bssTmStas,
    //  *     Seconds(1.0), -70.0);
    //  *
    //  * // Access individual helpers
    //  * AutoRoamingKvHelper* helper0 = container.helpers[0];  // For STA0
    //  * \endcode
    //  */
    static AutoRoamingKvHelperContainer SetupAutoRoamingKvHelperMulti(
        NetDeviceContainer apDevices,
        NetDeviceContainer staDevices,
        std::vector<Ptr<NeighborProtocolHelper>> neighborProtocolStas,
        std::vector<Ptr<BeaconProtocolHelper>> beaconProtocolStas,
        std::vector<Ptr<BeaconProtocolHelper>> beaconProtocolAps,
        std::vector<Ptr<BssTm11vHelper>> bssTmHelperStas,
        Time measurementInterval,
        double rssiThreshold);

    /**
     * \brief Convert NetDeviceContainer to vector of WifiNetDevice pointers
     * \param devices Net device container with WiFi devices
     * \return Vector of WifiNetDevice pointers
     */
    static std::vector<Ptr<WifiNetDevice>> GetWifiNetDevices(NetDeviceContainer devices);

    /**
     * \brief Split combined AP+STA protocol vector into separate vectors
     *
     * Many setup functions return a combined vector of protocols with APs first, then STAs.
     * This template function splits such vectors at the specified index.
     *
     * \tparam T Protocol type (e.g., NeighborProtocolHelper, BeaconProtocolHelper)
     * \param combined Combined vector with AP protocols followed by STA protocols
     * \param numAps Number of APs (split point)
     * \return Tuple of <AP protocols, STA protocols>
     *
     * Example:
     * \code
     * auto [apProtos, staProtos] = SimulationHelper::SplitProtocolVector(allProtos, 3);
     * \endcode
     */
    template <typename T>
    static std::tuple<std::vector<Ptr<T>>, std::vector<Ptr<T>>>
    SplitProtocolVector(const std::vector<Ptr<T>>& combined, uint32_t numAps)
    {
        NS_ASSERT_MSG(combined.size() >= numAps, "Combined vector too small");

        std::vector<Ptr<T>> apProtocols(combined.begin(), combined.begin() + numAps);
        std::vector<Ptr<T>> staProtocols(combined.begin() + numAps, combined.end());

        return {apProtocols, staProtocols};
    }

    /**
     * \brief Install WiFi devices on multiple APs with per-AP channel configuration
     *
     * Simplifies the repetitive pattern of:
     * - Setting channel for each AP
     * - Installing WiFi MAC/PHY
     * - Adding to device container
     *
     * \param wifi WifiHelper instance (pre-configured with standard and rate manager)
     * \param phy YansWifiPhyHelper instance (pre-configured with channel)
     * \param mac WifiMacHelper instance (will be configured as ApWifiMac)
     * \param nodes NodeContainer with AP nodes
     * \param channels Vector of channel numbers (one per AP)
     * \param ssid Network SSID
     * \param txPower Transmit power in dBm (default: 20.0)
     * \param channelWidth Channel width in MHz (default: 20)
     * \param band WiFi band (default: 5 GHz)
     * \return NetDeviceContainer with all installed AP devices
     */
    static NetDeviceContainer InstallApDevices(
        WifiHelper& wifi,
        YansWifiPhyHelper& phy,
        WifiMacHelper& mac,
        NodeContainer nodes,
        const std::vector<uint8_t>& channels,
        Ssid ssid,
        double txPower = 20.0,
        uint16_t channelWidth = 20,
        WifiPhyBand band = WIFI_PHY_BAND_5GHZ);

    /**
     * \brief Install WiFi devices on multiple STAs with per-STA channel configuration
     *
     * Simplifies the repetitive pattern of installing STAs on different initial channels.
     *
     * \param wifi WifiHelper instance (pre-configured with standard and rate manager)
     * \param phy YansWifiPhyHelper instance (pre-configured with channel)
     * \param mac WifiMacHelper instance (will be configured as StaWifiMac)
     * \param nodes NodeContainer with STA nodes
     * \param channels Vector of channel numbers (one per STA)
     * \param ssid Network SSID
     * \param channelWidth Channel width in MHz (default: 20)
     * \param band WiFi band (default: 5 GHz)
     * \return NetDeviceContainer with all installed STA devices
     */
    static NetDeviceContainer InstallStaDevices(
        WifiHelper& wifi,
        YansWifiPhyHelper& phy,
        WifiMacHelper& mac,
        NodeContainer nodes,
        const std::vector<uint8_t>& channels,
        Ssid ssid,
        uint16_t channelWidth = 20,
        WifiPhyBand band = WIFI_PHY_BAND_5GHZ);
};

} // namespace ns3

#endif // SIMULATION_HELPER_HELPER_H