/*
 * wifi-cca-monitor-helper.h
 */

#ifndef WIFI_CCA_MONITOR_HELPER_H
#define WIFI_CCA_MONITOR_HELPER_H

#include "ns3/wifi-cca-monitor.h"
#include "ns3/ptr.h"
#include "ns3/net-device-container.h"
#include "ns3/nstime.h"
#include <vector>

namespace ns3 {

/**
 * \brief Helper class for WifiCcaMonitor
 *
 * This helper simplifies the creation and installation of WifiCcaMonitor objects
 * on specific WiFi devices.
 */
class WifiCcaMonitorHelper
{
public:
    /**
     * \brief Constructor
     */
    WifiCcaMonitorHelper();

    /**
     * \brief Set the sliding window size for all monitors created by this helper
     * \param windowSize Duration of the sliding window
     */
    void SetWindowSize(Time windowSize);

    /**
     * \brief Set the update interval for all monitors created by this helper
     * \param interval Time interval between channel utilization updates
     */
    void SetUpdateInterval(Time interval);

    /**
     * \brief Install a monitor on a specific device
     * \param device The WiFi device to monitor
     * \return Pointer to the created monitor
     */
    Ptr<WifiCcaMonitor> Install(Ptr<NetDevice> device);

    /**
     * \brief Install monitors on multiple devices
     * \param devices Container of devices to monitor
     * \return Vector of pointers to the created monitors
     */
    std::vector<Ptr<WifiCcaMonitor>> Install(NetDeviceContainer devices);

private:
    Time m_windowSize;      //!< Sliding window size
    Time m_updateInterval;  //!< Update interval for trace
};

} // namespace ns3

#endif /* WIFI_CCA_MONITOR_HELPER_H */
