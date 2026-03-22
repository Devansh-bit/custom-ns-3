/*
 * wifi-cca-monitor-helper.cc
 */

#include "ns3/wifi-cca-monitor-helper.h"
#include "ns3/log.h"
#include "ns3/node.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("WifiCcaMonitorHelper");

WifiCcaMonitorHelper::WifiCcaMonitorHelper()
    : m_windowSize(MilliSeconds(100)),
      m_updateInterval(MilliSeconds(100))
{
    NS_LOG_FUNCTION(this);
}

void
WifiCcaMonitorHelper::SetWindowSize(Time windowSize)
{
    NS_LOG_FUNCTION(this << windowSize);
    m_windowSize = windowSize;
}

void
WifiCcaMonitorHelper::SetUpdateInterval(Time interval)
{
    NS_LOG_FUNCTION(this << interval);
    m_updateInterval = interval;
}

Ptr<WifiCcaMonitor>
WifiCcaMonitorHelper::Install(Ptr<NetDevice> device)
{
    NS_LOG_FUNCTION(this << device);

    // Create monitor instance
    Ptr<WifiCcaMonitor> monitor = CreateObject<WifiCcaMonitor>();

    // Configure window and interval
    monitor->SetWindowSize(m_windowSize);
    monitor->SetUpdateInterval(m_updateInterval);

    // Get node ID from device
    Ptr<Node> node = device->GetNode();
    uint32_t nodeId = node->GetId();

    // Install on device
    monitor->Install(device, nodeId);

    // Start monitoring
    monitor->Start();

    return monitor;
}

std::vector<Ptr<WifiCcaMonitor>>
WifiCcaMonitorHelper::Install(NetDeviceContainer devices)
{
    NS_LOG_FUNCTION(this);

    std::vector<Ptr<WifiCcaMonitor>> monitors;

    for (uint32_t i = 0; i < devices.GetN(); ++i)
    {
        Ptr<WifiCcaMonitor> monitor = Install(devices.Get(i));
        monitors.push_back(monitor);
    }

    return monitors;
}

} // namespace ns3
