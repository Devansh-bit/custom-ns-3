#include "metrics-helper.h"

#include "ns3/node-list.h"
#include "ns3/wifi-net-device.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MetricsHelper");

std::map<uint32_t, ApMetrics>
MetricsHelper::InitializeApMetrics(const NetDeviceContainer& apDevices,
                                    const std::vector<uint8_t>& channels,
                                    WifiPhyBand band)
{
    NS_LOG_FUNCTION_NOARGS();

    std::map<uint32_t, ApMetrics> apMetrics;

    NS_ASSERT_MSG(apDevices.GetN() == channels.size(),
                  "Number of AP devices must match number of channels");

    for (uint32_t i = 0; i < apDevices.GetN(); i++)
    {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(apDevices.Get(i));
        NS_ASSERT_MSG(apDev, "Device is not a WifiNetDevice");

        uint32_t nodeId = apDev->GetNode()->GetId();

        ApMetrics metrics;
        metrics.nodeId = nodeId;
        metrics.bssid = Mac48Address::ConvertFrom(apDev->GetAddress());
        metrics.channel = channels[i];
        metrics.band = band;
        metrics.associatedClients = 0;
        metrics.channelUtilization = 0.0;
        metrics.throughputMbps = 0.0;
        metrics.bytesSent = 0;
        metrics.bytesReceived = 0;
        metrics.lastUpdate = Seconds(0);

        apMetrics[nodeId] = metrics;

        NS_LOG_INFO("[MetricsHelper] Initialized AP Node " << nodeId
                    << ": " << metrics.bssid << " (CH" << (int)metrics.channel << ")");
    }

    NS_LOG_INFO("[MetricsHelper] Initialized " << apMetrics.size() << " AP metrics");
    return apMetrics;
}

std::map<std::string, Ptr<LeverConfig>>
MetricsHelper::BuildBssidToLeverConfigMap(
    const std::map<uint32_t, ApMetrics>& apMetrics,
    const std::map<uint32_t, Ptr<LeverConfig>>& leverConfigs)
{
    NS_LOG_FUNCTION_NOARGS();

    std::map<std::string, Ptr<LeverConfig>> bssidToLeverConfig;

    for (const auto& apEntry : apMetrics)
    {
        uint32_t nodeId = apEntry.first;
        const ApMetrics& metrics = apEntry.second;

        // Convert BSSID to string
        std::ostringstream oss;
        oss << metrics.bssid;
        std::string bssidStr = oss.str();

        // Find corresponding LeverConfig
        auto leverIt = leverConfigs.find(nodeId);
        if (leverIt != leverConfigs.end())
        {
            bssidToLeverConfig[bssidStr] = leverIt->second;
            NS_LOG_INFO("[MetricsHelper] Mapped BSSID " << bssidStr
                        << " (Node " << nodeId << ") → LeverConfig");
        }
        else
        {
            NS_LOG_WARN("[MetricsHelper] No LeverConfig found for AP Node " << nodeId);
        }
    }

    NS_LOG_INFO("[MetricsHelper] Built " << bssidToLeverConfig.size()
                << " BSSID→LeverConfig mappings");
    return bssidToLeverConfig;
}

std::vector<Mac48Address>
MetricsHelper::ExtractMacAddresses(const NetDeviceContainer& devices)
{
    NS_LOG_FUNCTION_NOARGS();

    std::vector<Mac48Address> macAddresses;
    macAddresses.reserve(devices.GetN());

    for (uint32_t i = 0; i < devices.GetN(); i++)
    {
        Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice>(devices.Get(i));
        NS_ASSERT_MSG(dev, "Device is not a WifiNetDevice");

        Mac48Address mac = Mac48Address::ConvertFrom(dev->GetAddress());
        macAddresses.push_back(mac);
        NS_LOG_DEBUG("[MetricsHelper] Extracted MAC: " << mac);
    }

    NS_LOG_INFO("[MetricsHelper] Extracted " << macAddresses.size() << " MAC addresses");
    return macAddresses;
}


ApMetrics*
MetricsHelper::FindApByBssid(std::map<uint32_t, ApMetrics>& metricsMap, Mac48Address bssid)
{
    NS_LOG_FUNCTION_NOARGS();

    for (auto& [nodeId, metrics] : metricsMap)
    {
        if (metrics.bssid == bssid)
        {
            NS_LOG_DEBUG("[MetricsHelper] Found AP by BSSID: " << bssid << " (Node " << nodeId << ")");
            return &metrics;
        }
    }

    NS_LOG_DEBUG("[MetricsHelper] AP not found by BSSID: " << bssid);
    return nullptr;
}

ApMetrics*
MetricsHelper::FindApByNodeId(std::map<uint32_t, ApMetrics>& metricsMap, uint32_t nodeId)
{
    NS_LOG_FUNCTION_NOARGS();

    auto it = metricsMap.find(nodeId);
    if (it != metricsMap.end())
    {
        NS_LOG_DEBUG("[MetricsHelper] Found AP by Node ID: " << nodeId);
        return &(it->second);
    }

    NS_LOG_DEBUG("[MetricsHelper] AP not found by Node ID: " << nodeId);
    return nullptr;
}

// ============================================================================
// SPECIALIZED UPDATE FUNCTIONS
// ============================================================================

bool
MetricsHelper::UpdateApClientList(std::map<uint32_t, ApMetrics>& apMetrics,
                                  Mac48Address bssid,
                                  Mac48Address clientMac,
                                  bool isAdding)
{
    NS_LOG_FUNCTION_NOARGS();

    ApMetrics* ap = FindApByBssid(apMetrics, bssid);
    if (!ap)
    {
        NS_LOG_WARN("[MetricsHelper] UpdateApClientList: AP not found for BSSID " << bssid);
        return false;
    }

    if (isAdding)
    {
        ap->clientList.insert(clientMac);
        NS_LOG_INFO("[MetricsHelper] Added client " << clientMac << " to AP " << bssid);
    }
    else
    {
        ap->clientList.erase(clientMac);
        NS_LOG_INFO("[MetricsHelper] Removed client " << clientMac << " from AP " << bssid);
    }

    ap->associatedClients = ap->clientList.size();
    ap->lastUpdate = Simulator::Now();

    return true;
}


ConnectionMetrics
MetricsHelper::BuildConnectionMetrics(const std::string& srcMac,
                                      const std::string& dstMac,
                                      double meanLatency,
                                      double jitter,
                                      uint32_t packetCount,
                                      double throughputMbps)
{
    NS_LOG_FUNCTION_NOARGS();

    ConnectionMetrics conn;
    conn.staAddress = srcMac;
    conn.APAddress = dstMac;
    conn.meanRTTLatency = meanLatency;
    conn.jitterMs = jitter;
    conn.packetCount = packetCount;
    conn.uplinkThroughputMbps = 0.0;      // Will be set by caller
    conn.downlinkThroughputMbps = 0.0;    // Will be set by caller
    conn.packetLossRate = 0.0;            // Will be set by caller
    conn.MACRetryRate = 0.0;              // Will be set by caller
    conn.apViewRSSI = 0.0;                // Will be set by caller
    conn.apViewSNR = 0.0;                 // Will be set by caller
    conn.lastUpdate = Simulator::Now();

    return conn;
}

uint32_t
MetricsHelper::ExtractNodeIdFromContext(const std::string& context)
{
    NS_LOG_FUNCTION(context);

    // Context format: "/NodeList/3/$ns3::Node/DeviceList/0/..."
    size_t nodeListPos = context.find("/NodeList/");
    if (nodeListPos == std::string::npos)
    {
        NS_LOG_WARN("[MetricsHelper] ExtractNodeIdFromContext: Invalid context string");
        return 0;
    }

    size_t startPos = nodeListPos + 10;  // Length of "/NodeList/"
    size_t endPos = context.find("/", startPos);
    if (endPos == std::string::npos)
    {
        NS_LOG_WARN("[MetricsHelper] ExtractNodeIdFromContext: Could not find end of node ID");
        return 0;
    }

    std::string nodeIdStr = context.substr(startPos, endPos - startPos);
    uint32_t nodeId = std::stoi(nodeIdStr);

    NS_LOG_DEBUG("[MetricsHelper] Extracted Node ID: " << nodeId << " from context: " << context);
    return nodeId;
}

} // namespace ns3
