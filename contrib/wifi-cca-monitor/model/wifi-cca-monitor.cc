/*
 * wifi-cca-monitor.cc
 */

#include "ns3/wifi-cca-monitor.h"
#include "ns3/log.h"
#include "ns3/config.h"
#include "ns3/simulator.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-phy-state-helper.h"
#include "ns3/wifi-mac.h"
#include "ns3/virtual-interferer-environment.h"  // For virtual interferer effects
#include "ns3/mobility-model.h"                   // For getting device position
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("WifiCcaMonitor");
NS_OBJECT_ENSURE_REGISTERED(WifiCcaMonitor);

TypeId
WifiCcaMonitor::GetTypeId()
{
    static TypeId tid = TypeId("ns3::WifiCcaMonitor")
        .SetParent<Object>()
        .SetGroupName("WifiCcaMonitor")
        .AddConstructor<WifiCcaMonitor>()
        .AddTraceSource("ChannelUtilization",
                       "Trace fired periodically with channel utilization metrics",
                       MakeTraceSourceAccessor(&WifiCcaMonitor::m_channelUtilizationTrace),
                       "ns3::WifiCcaMonitor::ChannelUtilizationTracedCallback");
    return tid;
}

WifiCcaMonitor::WifiCcaMonitor()
    : m_nodeId(0),
      m_device(nullptr),
      m_windowSize(MilliSeconds(100)),
      m_updateInterval(MilliSeconds(100)),
      m_windowStartTime(Seconds(0)),
      m_windowIdleTime(0.0),
      m_windowTxTime(0.0),
      m_windowRxTime(0.0),
      m_windowCcaBusyTime(0.0),
      m_windowWifiCcaBusyTime(0.0),
      m_windowNonWifiCcaBusyTime(0.0),
      m_windowBytesSent(0),
      m_windowBytesReceived(0),
      m_started(false)
{
    NS_LOG_FUNCTION(this);
}

WifiCcaMonitor::~WifiCcaMonitor()
{
    NS_LOG_FUNCTION(this);
    if (m_updateEvent.IsPending())
    {
        Simulator::Cancel(m_updateEvent);
    }
}

void
WifiCcaMonitor::Install(Ptr<NetDevice> device, uint32_t nodeId)
{
    NS_LOG_FUNCTION(this << device << nodeId);

    m_device = device;
    m_nodeId = nodeId;

    // Connect to device-specific trace sources
    Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(device);
    NS_ASSERT_MSG(wifiDevice != nullptr, "Device must be a WifiNetDevice");

    // Get PHY and MAC
    Ptr<WifiPhy> phy = wifiDevice->GetPhy();
    Ptr<WifiMac> mac = wifiDevice->GetMac();

    NS_ASSERT_MSG(phy != nullptr, "WifiNetDevice must have a PHY");
    NS_ASSERT_MSG(mac != nullptr, "WifiNetDevice must have a MAC");

    // Connect PHY state trace
    // Note: The "State" trace source is on WifiPhyStateHelper, not WifiPhy directly
    Ptr<WifiPhyStateHelper> state = phy->GetState();
    NS_ASSERT_MSG(state != nullptr, "WifiPhy must have a WifiPhyStateHelper");
    state->TraceConnectWithoutContext("State",
        MakeCallback(&WifiCcaMonitor::PhyStateTrace, this));

    // Connect CCA busy type trace for WiFi vs non-WiFi classification
    state->TraceConnectWithoutContext("CcaBusyType",
        MakeCallback(&WifiCcaMonitor::CcaBusyTypeTrace, this));

    // Connect PHY traces (fires for ALL frames including bridged traffic)
    // MacTx/MacRx don't fire for bridged frames, so we use PHY-level traces instead
    phy->TraceConnectWithoutContext("PhyTxBegin",
        MakeCallback(&WifiCcaMonitor::PhyTxBeginTrace, this));

    phy->TraceConnectWithoutContext("PhyRxEnd",
        MakeCallback(&WifiCcaMonitor::PhyRxEndTrace, this));

    // Keep MAC traces for backward compatibility (unused for byte tracking now)
    mac->TraceConnectWithoutContext("MacTx",
        MakeCallback(&WifiCcaMonitor::MacTxTrace, this));

    mac->TraceConnectWithoutContext("MacRx",
        MakeCallback(&WifiCcaMonitor::MacRxTrace, this));
}

void
WifiCcaMonitor::SetWindowSize(Time windowSize)
{
    NS_LOG_FUNCTION(this << windowSize);
    m_windowSize = windowSize;
}

void
WifiCcaMonitor::SetUpdateInterval(Time interval)
{
    NS_LOG_FUNCTION(this << interval);
    m_updateInterval = interval;
}

void
WifiCcaMonitor::Start()
{
    NS_LOG_FUNCTION(this);

    if (m_started)
    {
        return;
    }

    m_started = true;
    m_windowStartTime = Simulator::Now();
    ResetWindow();

    // Schedule first update
    m_updateEvent = Simulator::Schedule(m_updateInterval,
                                        &WifiCcaMonitor::CalculateAndTraceUtilization, this);
}

void
WifiCcaMonitor::Stop()
{
    NS_LOG_FUNCTION(this);

    m_started = false;

    if (m_updateEvent.IsPending())
    {
        Simulator::Cancel(m_updateEvent);
    }
}

void
WifiCcaMonitor::PhyStateTrace(Time start, Time duration, WifiPhyState state)
{
    if (!m_started)
    {
        return;
    }

    switch(state)
    {
        case WifiPhyState::IDLE:
            m_windowIdleTime += duration.GetSeconds();
            break;
        case WifiPhyState::CCA_BUSY:
            m_windowCcaBusyTime += duration.GetSeconds();
            break;
        case WifiPhyState::TX:
            m_windowTxTime += duration.GetSeconds();
            break;
        case WifiPhyState::RX:
            m_windowRxTime += duration.GetSeconds();
            break;
        case WifiPhyState::SWITCHING:
            m_windowCcaBusyTime += duration.GetSeconds();
            break;
        default:
            break;
    }
}

void
WifiCcaMonitor::MacTxTrace(Ptr<const Packet> packet)
{
    if (!m_started)
    {
        return;
    }

    // NOTE: This is kept for backward compatibility but not used for byte tracking
    // PHY-level traces are used instead to capture bridged traffic
    // m_windowBytesSent += packet->GetSize();
}

void
WifiCcaMonitor::MacRxTrace(Ptr<const Packet> packet)
{
    if (!m_started)
    {
        return;
    }

    // NOTE: This is kept for backward compatibility but not used for byte tracking
    // PHY-level traces are used instead to capture bridged traffic
    // m_windowBytesReceived += packet->GetSize();
}

void
WifiCcaMonitor::PhyTxBeginTrace(Ptr<const Packet> packet, double txPowerW)
{
    if (!m_started)
    {
        return;
    }

    // Track bytes at PHY level (includes ALL frames: data, management, control, bridged traffic)
    // This captures the actual over-the-air bytes including PHY/MAC overhead
    uint32_t packetSize = packet->GetSize();
    m_windowBytesSent += packetSize;

    // Debug logging (compile with NS_LOG=WifiCcaMonitor to see)
    NS_LOG_DEBUG("Node " << m_nodeId << " PHY TX: " << packetSize << " bytes, "
                 << "Total sent in window: " << m_windowBytesSent << " bytes");
}

void
WifiCcaMonitor::PhyRxEndTrace(Ptr<const Packet> packet)
{
    if (!m_started)
    {
        return;
    }

    // Track bytes at PHY level (includes ALL frames: data, management, control, bridged traffic)
    // This captures the actual over-the-air bytes including PHY/MAC overhead
    uint32_t packetSize = packet->GetSize();
    m_windowBytesReceived += packetSize;

    // Debug logging (compile with NS_LOG=WifiCcaMonitor to see)
    NS_LOG_DEBUG("Node " << m_nodeId << " PHY RX: " << packetSize << " bytes, "
                 << "Total received in window: " << m_windowBytesReceived << " bytes");
}

void
WifiCcaMonitor::CcaBusyTypeTrace(Time start, Time duration, bool wifiCaused)
{
    if (!m_started)
    {
        return;
    }

    // Track WiFi vs non-WiFi CCA busy time separately
    if (wifiCaused)
    {
        m_windowWifiCcaBusyTime += duration.GetSeconds();
    }
    else
    {
        m_windowNonWifiCcaBusyTime += duration.GetSeconds();
    }
}

void
WifiCcaMonitor::CalculateAndTraceUtilization()
{
    NS_LOG_FUNCTION(this);

    if (!m_started)
    {
        return;
    }

    Time now = Simulator::Now();
    double timestamp = now.GetSeconds();
    double windowDuration = m_windowSize.GetSeconds();

    // Calculate utilization percentages
    double txUtil = (m_windowTxTime / windowDuration) * 100.0;
    double rxUtil = (m_windowRxTime / windowDuration) * 100.0;
    double wifiCcaUtil = (m_windowWifiCcaBusyTime / windowDuration) * 100.0;
    double nonWifiCcaUtil = (m_windowNonWifiCcaBusyTime / windowDuration) * 100.0;

    // Query Virtual Interferer Environment for additional non-WiFi interference
    double virtualInterfererUtil = 0.0;
    if (m_device)
    {
        Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(m_device);
        if (wifiDevice)
        {
            Ptr<Node> node = wifiDevice->GetNode();
            Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
            Ptr<WifiPhy> phy = wifiDevice->GetPhy();

            if (mobility && phy)
            {
                Vector position = mobility->GetPosition();
                uint8_t channel = phy->GetChannelNumber();

                // Get virtual interferer effects at this position/channel
                auto env = VirtualInterfererEnvironment::Get();
                if (env && !VirtualInterfererEnvironment::IsBeingDestroyed())
                {
                    InterferenceEffect effect = env->GetAggregateEffect(position, channel);
                    virtualInterfererUtil = effect.nonWifiCcaPercent;
                }
            }
        }
    }

    // Calculate raw WiFi utilization from measured PHY data
    double rawWifiUtil = txUtil + rxUtil + wifiCcaUtil;
    double rawNonWifiUtil = nonWifiCcaUtil;
    double rawTotal = rawWifiUtil + rawNonWifiUtil;

    // Apply proportional scaling with VI interference
    // VI interference "takes space" from existing utilization proportionally
    // This ensures: wifiUtil + nonWifiUtil = totalUtil (always consistent)
    double wifiUtil = rawWifiUtil;
    double nonWifiUtil = rawNonWifiUtil;
    double totalUtil = rawTotal;

    if (virtualInterfererUtil > 0.0)
    {
        // VI takes priority - calculate available space after VI
        double availableSpace = std::max(0.0, 100.0 - virtualInterfererUtil);

        if (rawTotal > 0.0 && availableSpace < rawTotal)
        {
            // Need to scale down original values to fit
            double scaleFactor = availableSpace / rawTotal;
            wifiUtil = rawWifiUtil * scaleFactor;
            nonWifiUtil = rawNonWifiUtil * scaleFactor + virtualInterfererUtil;
        }
        else
        {
            // Enough space - just add VI to non-WiFi
            nonWifiUtil = rawNonWifiUtil + virtualInterfererUtil;
        }

        // Total is always sum of components (guaranteed consistent)
        totalUtil = wifiUtil + nonWifiUtil;
    }
    else
    {
        // No VI - just cap at 100%
        wifiUtil = std::min(100.0, rawWifiUtil);
        double remainingCapacity = std::max(0.0, 100.0 - wifiUtil);
        nonWifiUtil = std::min(remainingCapacity, rawNonWifiUtil);
        totalUtil = wifiUtil + nonWifiUtil;
    }

    // Final safety clamp
    totalUtil = std::min(100.0, totalUtil);
    nonWifiUtil = std::min(100.0, nonWifiUtil);
    wifiUtil = std::min(100.0, wifiUtil);

    // Calculate throughput (Mbps)
    double throughput = (m_windowBytesSent * 8.0) / (windowDuration * 1e6);

    // Debug logging for byte tracking verification
    NS_LOG_INFO("Node " << m_nodeId << " @ " << timestamp << "s: "
                << "BytesSent=" << m_windowBytesSent << ", "
                << "BytesRecv=" << m_windowBytesReceived << ", "
                << "Throughput=" << throughput << " Mbps, "
                << "TotalUtil=" << totalUtil << "%, "
                << "WiFiUtil=" << wifiUtil << "%, "
                << "NonWiFiUtil=" << nonWifiUtil << "%");

    // Fire trace with all metrics
    // Signature: (nodeId, timestamp, totalUtil%, wifiUtil%, nonWifiUtil%, txUtil%, rxUtil%,
    //             wifiCcaUtil%, nonWifiCcaUtil%, txTime, rxTime, wifiCcaTime, nonWifiCcaTime,
    //             idleTime, bytesSent, bytesReceived, throughputMbps)
    m_channelUtilizationTrace(m_nodeId, timestamp, totalUtil, wifiUtil, nonWifiUtil,
                              txUtil, rxUtil, wifiCcaUtil, nonWifiCcaUtil,
                              m_windowTxTime, m_windowRxTime,
                              m_windowWifiCcaBusyTime, m_windowNonWifiCcaBusyTime,
                              m_windowIdleTime, m_windowBytesSent, m_windowBytesReceived, throughput);

    // Reset window for next period
    ResetWindow();
    m_windowStartTime = now;

    // Schedule next update
    ScheduleNextUpdate();
}

void
WifiCcaMonitor::ResetWindow()
{
    NS_LOG_FUNCTION(this);

    m_windowIdleTime = 0.0;
    m_windowTxTime = 0.0;
    m_windowRxTime = 0.0;
    m_windowCcaBusyTime = 0.0;
    m_windowWifiCcaBusyTime = 0.0;
    m_windowNonWifiCcaBusyTime = 0.0;
    m_windowBytesSent = 0;
    m_windowBytesReceived = 0;
}

void
WifiCcaMonitor::ScheduleNextUpdate()
{
    NS_LOG_FUNCTION(this);

    if (m_started)
    {
        m_updateEvent = Simulator::Schedule(m_updateInterval,
                                            &WifiCcaMonitor::CalculateAndTraceUtilization, this);
    }
}

double
WifiCcaMonitor::GetChannelUtilization() const
{
    double windowDuration = m_windowSize.GetSeconds();
    if (windowDuration == 0.0)
    {
        return 0.0;
    }

    double busyTime = m_windowTxTime + m_windowRxTime + m_windowCcaBusyTime;
    return (busyTime / windowDuration) * 100.0;
}

double
WifiCcaMonitor::GetIdleTime() const
{
    return m_windowIdleTime;
}

double
WifiCcaMonitor::GetTxTime() const
{
    return m_windowTxTime;
}

double
WifiCcaMonitor::GetRxTime() const
{
    return m_windowRxTime;
}

double
WifiCcaMonitor::GetCcaBusyTime() const
{
    return m_windowCcaBusyTime;
}

uint64_t
WifiCcaMonitor::GetBytesSent() const
{
    return m_windowBytesSent;
}

uint64_t
WifiCcaMonitor::GetBytesReceived() const
{
    return m_windowBytesReceived;
}

} // namespace ns3
