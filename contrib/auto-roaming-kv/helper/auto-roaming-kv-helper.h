#ifndef AUTO_ROAMING_KV_HELPER_H
#define AUTO_ROAMING_KV_HELPER_H

#include "ns3/link-measurement-protocol.h"
#include "ns3/link-measurement-report.h"
#include "ns3/neighbor-protocol-11k-helper.h"
#include "ns3/beacon-protocol-11k-helper.h"
#include "ns3/bss_tm_11v-helper.h"
#include "ns3/beacon-neighbor-model.h"
#include "ns3/wifi-net-device.h"
#include "ns3/net-device-container.h"
#include "ns3/nstime.h"

namespace ns3
{

/**
 * \ingroup auto-roaming-kv
 * \brief Helper class to install Link Measurement Protocol on AP and STA devices
 *
 * This helper simplifies the installation of Link Measurement Protocol on WiFi devices.
 * For STAs, it also schedules regular link measurement requests to their associated AP.
 * Additionally, it monitors RSSI and triggers neighbor requests when below threshold.
 */
class AutoRoamingKvHelper
{
  public:
    /**
     * \brief Constructor
     */
    AutoRoamingKvHelper();

    /**
     * \brief Destructor
     */
    ~AutoRoamingKvHelper();

    /**
     * \brief Set the interval between link measurement requests for STAs
     * \param interval the time interval between requests
     */
    void SetMeasurementInterval(Time interval);

    /**
     * \brief Set the RSSI threshold for triggering neighbor requests
     * \param threshold the RSSI threshold in dBm (default: -75 dBm)
     *
     * When RSSI from link measurement drops below this threshold,
     * a neighbor report request will be triggered automatically.
     */
    void SetRssiThreshold(double threshold);

    /**
     * \brief Set the delay between neighbor report and beacon request
     * \param delay the delay in Time (default: 50ms)
     *
     * After receiving a neighbor report, a beacon request will be sent
     * to the STA after this delay.
     */
    void SetBeaconRequestDelay(Time delay);

    /**
     * \brief Set the delay between beacon report and BSS TM request
     * \param delay the delay in Time (default: 50ms)
     *
     * After receiving a beacon report, a BSS TM request will be sent
     * after this delay.
     */
    void SetBssTmRequestDelay(Time delay);

    /**
     * \brief Install Link Measurement Protocol on AP devices
     * \param apDevices container of AP WiFi devices
     * \return container of LinkMeasurementProtocol instances
     *
     * This installs the Link Measurement Protocol on each AP device.
     * APs will respond to link measurement requests from STAs.
     */
    std::vector<Ptr<LinkMeasurementProtocol>> InstallAp(NetDeviceContainer apDevices);

    /**
     * \brief Install Link Measurement Protocol and Neighbor Protocol on STA devices
     * \param staDevices container of STA WiFi devices
     * \param neighborProtocol the neighbor protocol helper instance
     * \param beaconProtocol the beacon protocol helper instance
     * \param bssTmHelper the BSS TM helper instance
     * \return container of LinkMeasurementProtocol instances
     *
     * This installs the Link Measurement Protocol on each STA device and
     * schedules regular link measurement requests to the currently associated AP.
     * The helper automatically detects the current AP association.
     * It also monitors RSSI and triggers neighbor requests when below threshold,
     * followed by beacon requests and BSS TM steering.
     */
    std::vector<Ptr<LinkMeasurementProtocol>> InstallSta(
        NetDeviceContainer staDevices,
        Ptr<NeighborProtocolHelper> neighborProtocol,
        Ptr<BeaconProtocolHelper> beaconProtocol,
        Ptr<BssTm11vHelper> bssTmHelper);

    /**
     * \brief Callback for when a neighbor report is received
     * \param staAddress MAC of STA
     * \param apAddress MAC of AP
     * \param neighbors Vector of neighbor report data
     */
    void OnNeighborReportReceived(Mac48Address staAddress,
                                   Mac48Address apAddress,
                                   std::vector<NeighborReportData> neighbors);

    /**
     * \brief Callback for when a beacon report is received
     * \param apAddress MAC of AP
     * \param staAddress MAC of STA
     * \param reports Vector of beacon report data
     */
    void OnBeaconReportReceived(Mac48Address apAddress,
                                 Mac48Address staAddress,
                                 std::vector<BeaconReportData> reports);

  private:
    /**
     * \brief Get the currently associated AP's BSSID from the STA
     * \return MAC address of currently associated AP, or Mac48Address() if not associated
     */
    Mac48Address GetCurrentApBssid() const;

    /**
     * \brief Send a periodic link measurement request from STA to currently associated AP
     * \param protocol the LinkMeasurementProtocol instance
     */
    void SendPeriodicRequest(Ptr<LinkMeasurementProtocol> protocol);

    /**
     * \brief Callback for when a link measurement report is received
     * \param from the MAC address of the sender
     * \param report the link measurement report
     */
    void OnLinkMeasurementReport(Mac48Address from, LinkMeasurementReport report);

    /**
     * \brief Send beacon request after delay
     */
    void SendBeaconRequest();

    /**
     * \brief Send BSS TM request with ranked candidate list
     */
    void SendBssTmRequest();

    Time m_measurementInterval;           ///< Interval between link measurement requests
    double m_rssiThreshold;                ///< RSSI threshold for triggering neighbor requests (dBm)
    Time m_beaconRequestDelay;            ///< Delay between neighbor report and beacon request
    Time m_bssTmRequestDelay;             ///< Delay between beacon report and BSS TM request
    bool m_neighborRequestTriggered;       ///< Flag to track if neighbor request has been triggered
    Mac48Address m_lastAssociatedAp;       ///< Last associated AP BSSID (for detecting roaming)
    Ptr<WifiNetDevice> m_staDevice;        ///< STA device for requests
    std::map<Mac48Address, Ptr<WifiNetDevice>> m_apDevices; ///< Map of AP MAC to device
    Mac48Address m_staAddress;             ///< STA address for requests
    Ptr<NeighborProtocolHelper> m_neighborProtocol; ///< Neighbor protocol helper
    Ptr<BeaconProtocolHelper> m_beaconProtocol;     ///< Beacon protocol helper
    Ptr<BssTm11vHelper> m_bssTmHelper;              ///< BSS TM helper
    std::vector<NeighborReportData> m_lastNeighborReport; ///< Last received neighbor report
    std::vector<BeaconReportData> m_lastBeaconReport;     ///< Last received beacon report
};

} // namespace ns3

#endif // AUTO_ROAMING_KV_HELPER_H
