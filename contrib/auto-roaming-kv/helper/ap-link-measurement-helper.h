#ifndef AP_LINK_MEASUREMENT_HELPER_H
#define AP_LINK_MEASUREMENT_HELPER_H

#include "ns3/link-measurement-protocol.h"
#include "ns3/wifi-net-device.h"
#include "ns3/nstime.h"
#include "ns3/mac48-address.h"
#include <map>
#include <vector>

namespace ns3
{

/**
 * \ingroup auto-roaming-kv
 * \brief Helper class to manage AP-initiated link measurement requests
 *
 * This helper manages periodic link measurement requests FROM APs TO associated STAs.
 * Unlike AutoRoamingKvHelper (which handles STA→AP requests), this helper enables
 * APs to measure how well STAs can hear them (staViewRSSI/SNR).
 *
 * Note: This helper does NOT install LinkMeasurementProtocol on APs.
 * The protocol must already be installed (e.g., via AutoRoamingKvHelper::InstallAp).
 */
class ApLinkMeasurementHelper
{
  public:
    /**
     * \brief Constructor
     */
    ApLinkMeasurementHelper();

    /**
     * \brief Destructor
     */
    ~ApLinkMeasurementHelper();

    /**
     * \brief Set the interval between link measurement requests from APs
     * \param interval the time interval between requests
     */
    void SetMeasurementInterval(Time interval);

    /**
     * \brief Register AP devices and their LinkMeasurementProtocol instances
     * \param apDevices container of AP WiFi devices
     * \param apProtocols corresponding LinkMeasurementProtocol instances
     *
     * This method stores the AP device and protocol references.
     * It does NOT install new protocols - the protocols must already be installed.
     */
    void RegisterApDevices(NetDeviceContainer apDevices,
                          std::vector<Ptr<LinkMeasurementProtocol>> apProtocols);

    /**
     * \brief Start periodic link measurement requests from all registered APs
     *
     * This method schedules the first round of AP→STA link measurement requests.
     * Each AP will send requests to all its currently associated STAs.
     * The requests will continue automatically at the configured interval.
     */
    void Start();

    /**
     * \brief Stop periodic link measurement requests
     */
    void Stop();

  private:
    /**
     * \brief Send periodic link measurement requests from all APs to their associated STAs
     */
    void SendPeriodicRequests();

    Time m_measurementInterval;  ///< Interval between AP→STA requests
    bool m_running;              ///< Whether periodic requests are active

    /// Map: AP Node ID → AP WifiNetDevice
    std::map<uint32_t, Ptr<WifiNetDevice>> m_apDevices;

    /// Map: AP Node ID → LinkMeasurementProtocol instance
    std::map<uint32_t, Ptr<LinkMeasurementProtocol>> m_apProtocols;
};

} // namespace ns3

#endif /* AP_LINK_MEASUREMENT_HELPER_H */
