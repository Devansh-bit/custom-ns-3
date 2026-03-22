/*
 * wifi-cca-monitor.h
 * 
 * Monitor for WiFi PHY state and channel utilization
 */

#ifndef WIFI_CCA_MONITOR_H
#define WIFI_CCA_MONITOR_H

#include "ns3/object.h"
#include "ns3/wifi-phy-state.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/traced-callback.h"
#include "ns3/event-id.h"
#include "ns3/net-device.h"
#include "ns3/ptr.h"

namespace ns3 {

/**
 * \brief WiFi CCA and channel utilization monitor
 *
 * This class monitors WiFi PHY states (IDLE, TX, RX, CCA_BUSY) and
 * calculates channel utilization for a specific WiFi device using a
 * sliding time window. Each instance monitors one device.
 */
class WifiCcaMonitor : public Object
{
public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * \brief Constructor
     */
    WifiCcaMonitor();

    /**
     * \brief Destructor
     */
    virtual ~WifiCcaMonitor();

    /**
     * \brief Install monitoring on a specific device
     * \param device The WiFi device to monitor
     * \param nodeId The node ID (for trace output)
     */
    void Install(Ptr<NetDevice> device, uint32_t nodeId);
    
    /**
     * \brief Set the sliding window size for channel utilization calculation
     * \param windowSize Duration of the sliding window
     */
    void SetWindowSize(Time windowSize);

    /**
     * \brief Set the update interval for channel utilization trace
     * \param interval Time interval between trace updates
     */
    void SetUpdateInterval(Time interval);

    /**
     * \brief Start monitoring and periodic updates
     */
    void Start();

    /**
     * \brief Stop monitoring and periodic updates
     */
    void Stop();

    /**
     * \brief Get current channel utilization percentage
     * \return Channel utilization (0-100%)
     */
    double GetChannelUtilization() const;

    /**
     * \brief Get PHY idle time in current window
     * \return Idle time in seconds
     */
    double GetIdleTime() const;

    /**
     * \brief Get PHY TX time in current window
     * \return TX time in seconds
     */
    double GetTxTime() const;

    /**
     * \brief Get PHY RX time in current window
     * \return RX time in seconds
     */
    double GetRxTime() const;

    /**
     * \brief Get PHY CCA busy time in current window
     * \return CCA busy time in seconds
     */
    double GetCcaBusyTime() const;

    /**
     * \brief Get total bytes sent in current window
     * \return Total bytes sent
     */
    uint64_t GetBytesSent() const;

    /**
     * \brief Get total bytes received in current window
     * \return Total bytes received
     */
    uint64_t GetBytesReceived() const;

private:
    /**
     * \brief PHY state trace callback
     * \param start Start time of the state
     * \param duration Duration of the state
     * \param state The PHY state
     */
    void PhyStateTrace(Time start, Time duration, WifiPhyState state);

    /**
     * \brief MAC TX trace callback
     * \param packet The transmitted packet
     */
    void MacTxTrace(Ptr<const Packet> packet);

    /**
     * \brief MAC RX trace callback
     * \param packet The received packet
     */
    void MacRxTrace(Ptr<const Packet> packet);

    /**
     * \brief PHY TX begin trace callback (tracks ALL transmitted frames including bridged)
     * \param packet The transmitted packet
     * \param txPowerW TX power in watts
     */
    void PhyTxBeginTrace(Ptr<const Packet> packet, double txPowerW);

    /**
     * \brief PHY RX end trace callback (tracks ALL received frames including bridged)
     * \param packet The received packet
     */
    void PhyRxEndTrace(Ptr<const Packet> packet);

    /**
     * \brief CCA busy type trace callback (WiFi vs non-WiFi classification)
     * \param start Start time of the CCA busy state
     * \param duration Duration of the CCA busy state
     * \param wifiCaused true if caused by WiFi signal, false if non-WiFi interference
     */
    void CcaBusyTypeTrace(Time start, Time duration, bool wifiCaused);

    /**
     * \brief Calculate and fire channel utilization trace
     */
    void CalculateAndTraceUtilization();

    /**
     * \brief Reset sliding window counters
     */
    void ResetWindow();

    /**
     * \brief Schedule next channel utilization update
     */
    void ScheduleNextUpdate();

    // Device information
    uint32_t m_nodeId;                //!< Node ID being monitored
    Ptr<NetDevice> m_device;          //!< Device being monitored

    // Sliding window configuration
    Time m_windowSize;                //!< Sliding window duration (default: 100ms)
    Time m_updateInterval;            //!< Update interval for trace (default: 100ms)
    Time m_windowStartTime;           //!< Start time of current window

    // Current window statistics
    double m_windowIdleTime;          //!< PHY idle time in current window
    double m_windowTxTime;            //!< PHY TX time in current window
    double m_windowRxTime;            //!< PHY RX time in current window
    double m_windowCcaBusyTime;       //!< PHY CCA busy time in current window (total)
    double m_windowWifiCcaBusyTime;   //!< PHY CCA busy time caused by WiFi signals (preamble detected)
    double m_windowNonWifiCcaBusyTime; //!< PHY CCA busy time caused by non-WiFi interference
    uint64_t m_windowBytesSent;       //!< Bytes sent in current window
    uint64_t m_windowBytesReceived;   //!< Bytes received in current window

    // Trace sources
    /**
     * \brief Channel utilization trace
     * Signature: (nodeId, timestamp, totalUtil%, wifiUtil%, nonWifiUtil%, txUtil%, rxUtil%,
     *             wifiCcaUtil%, nonWifiCcaUtil%, txTime, rxTime, wifiCcaTime, nonWifiCcaTime,
     *             idleTime, bytesSent, bytesReceived, throughputMbps)
     *
     * wifiUtil = txUtil + rxUtil + wifiCcaUtil (all WiFi-related activity)
     * nonWifiUtil = nonWifiCcaUtil (non-WiFi interference only)
     * totalUtil = wifiUtil + nonWifiUtil
     */
    TracedCallback<uint32_t, double, double, double, double, double, double, double, double,
                   double, double, double, double, double, uint64_t, uint64_t, double> m_channelUtilizationTrace;

    // Control
    bool m_started;                   //!< Flag indicating if monitoring is active
    EventId m_updateEvent;            //!< Event ID for periodic updates
};

} // namespace ns3

#endif /* WIFI_CCA_MONITOR_H */
