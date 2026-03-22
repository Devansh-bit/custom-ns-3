/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Unified PHY Sniffer
 */

#include "unified-phy-sniffer-helper.h"

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/wifi-net-device.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("UnifiedPhySnifferHelper");

// Static member initialization
std::map<uint32_t, Ptr<UnifiedPhySniffer>> UnifiedPhySnifferHelper::s_sniffers;

UnifiedPhySnifferHelper::UnifiedPhySnifferHelper()
{
    NS_LOG_FUNCTION(this);
}

UnifiedPhySnifferHelper::~UnifiedPhySnifferHelper()
{
    NS_LOG_FUNCTION(this);
}

Ptr<UnifiedPhySniffer>
UnifiedPhySnifferHelper::Install(Ptr<WifiNetDevice> device)
{
    NS_LOG_FUNCTION(this << device);

    if (!device)
    {
        NS_LOG_WARN("Null device provided to Install");
        return nullptr;
    }

    Ptr<Node> node = device->GetNode();
    uint32_t nodeId = node->GetId();

    // Check if already installed
    auto it = s_sniffers.find(nodeId);
    if (it != s_sniffers.end())
    {
        NS_LOG_DEBUG("Sniffer already installed on node " << nodeId);
        return it->second;
    }

    // Create and install new sniffer
    Ptr<UnifiedPhySniffer> sniffer = CreateObject<UnifiedPhySniffer>();
    sniffer->Install(device);

    // Store in global map
    s_sniffers[nodeId] = sniffer;

    // Aggregate to node for easy retrieval
    node->AggregateObject(sniffer);

    NS_LOG_INFO("UnifiedPhySniffer installed on node " << nodeId);

    return sniffer;
}

Ptr<UnifiedPhySniffer>
UnifiedPhySnifferHelper::Install(Ptr<Node> node)
{
    NS_LOG_FUNCTION(this << node);

    Ptr<WifiNetDevice> device = FindWifiDevice(node);
    if (!device)
    {
        NS_LOG_WARN("No WiFi device found on node " << node->GetId());
        return nullptr;
    }

    return Install(device);
}

void
UnifiedPhySnifferHelper::Install(NetDeviceContainer devices)
{
    NS_LOG_FUNCTION(this);

    for (uint32_t i = 0; i < devices.GetN(); i++)
    {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(devices.Get(i));
        if (wifiDev)
        {
            Install(wifiDev);
        }
    }
}

void
UnifiedPhySnifferHelper::Install(NodeContainer nodes)
{
    NS_LOG_FUNCTION(this);

    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        Install(nodes.Get(i));
    }
}

Ptr<UnifiedPhySniffer>
UnifiedPhySnifferHelper::Get(Ptr<Node> node)
{
    if (!node)
    {
        return nullptr;
    }

    // First try the global map
    auto it = s_sniffers.find(node->GetId());
    if (it != s_sniffers.end())
    {
        return it->second;
    }

    // Fall back to aggregated object
    return node->GetObject<UnifiedPhySniffer>();
}

Ptr<UnifiedPhySniffer>
UnifiedPhySnifferHelper::GetOrInstall(Ptr<Node> node)
{
    NS_LOG_FUNCTION(node);

    if (!node)
    {
        return nullptr;
    }

    // Check if already installed
    Ptr<UnifiedPhySniffer> existing = Get(node);
    if (existing)
    {
        return existing;
    }

    // Install new
    UnifiedPhySnifferHelper helper;
    return helper.Install(node);
}

Ptr<UnifiedPhySniffer>
UnifiedPhySnifferHelper::GetOrInstall(Ptr<WifiNetDevice> device)
{
    NS_LOG_FUNCTION(device);

    if (!device)
    {
        return nullptr;
    }

    Ptr<Node> node = device->GetNode();

    // Check if already installed
    Ptr<UnifiedPhySniffer> existing = Get(node);
    if (existing)
    {
        return existing;
    }

    // Install new
    UnifiedPhySnifferHelper helper;
    return helper.Install(device);
}

Ptr<WifiNetDevice>
UnifiedPhySnifferHelper::FindWifiDevice(Ptr<Node> node)
{
    if (!node)
    {
        return nullptr;
    }

    for (uint32_t i = 0; i < node->GetNDevices(); i++)
    {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(i));
        if (wifiDev)
        {
            return wifiDev;
        }
    }

    return nullptr;
}

void
UnifiedPhySnifferHelper::PrintStatistics()
{
    NS_LOG_FUNCTION_NOARGS();

    std::cout << "\n=== UnifiedPhySniffer Statistics ===" << std::endl;
    std::cout << "Installed sniffers: " << s_sniffers.size() << std::endl;

    uint64_t totalProcessed = 0;
    uint64_t totalDispatched = 0;
    uint64_t totalFiltered = 0;

    for (const auto& pair : s_sniffers)
    {
        Ptr<UnifiedPhySniffer> sniffer = pair.second;
        uint64_t processed = sniffer->GetFramesProcessed();
        uint64_t dispatched = sniffer->GetFramesDispatched();
        uint64_t filtered = sniffer->GetFramesFiltered();

        std::cout << "  Node " << pair.first << ": "
                  << "processed=" << processed << ", "
                  << "dispatched=" << dispatched << ", "
                  << "filtered=" << filtered << std::endl;

        totalProcessed += processed;
        totalDispatched += dispatched;
        totalFiltered += filtered;
    }

    std::cout << "Total: "
              << "processed=" << totalProcessed << ", "
              << "dispatched=" << totalDispatched << ", "
              << "filtered=" << totalFiltered << std::endl;

    if (totalProcessed > 0)
    {
        double filterRate = 100.0 * totalFiltered / totalProcessed;
        std::cout << "Early filter rate: " << filterRate << "%" << std::endl;
    }

    std::cout << "==================================\n" << std::endl;
}

} // namespace ns3
