/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Unified PHY Sniffer
 *
 * Helper class for installing UnifiedPhySniffer on nodes
 */

#ifndef UNIFIED_PHY_SNIFFER_HELPER_H
#define UNIFIED_PHY_SNIFFER_HELPER_H

#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/unified-phy-sniffer.h"

#include <map>

namespace ns3
{

class Node;
class WifiNetDevice;

/**
 * \brief Helper for installing UnifiedPhySniffer on WiFi devices
 *
 * This helper provides convenient methods for installing the unified
 * sniffer on nodes and retrieving existing instances.
 *
 * Usage:
 * \code
 * UnifiedPhySnifferHelper helper;
 * auto sniffer = helper.Install(wifiDevice);
 *
 * // Or get existing / install new:
 * auto sniffer = UnifiedPhySnifferHelper::GetOrInstall(node);
 * \endcode
 */
class UnifiedPhySnifferHelper
{
  public:
    UnifiedPhySnifferHelper();
    ~UnifiedPhySnifferHelper();

    /**
     * \brief Install sniffer on a single WiFi device
     * \param device The WiFi net device
     * \return The installed UnifiedPhySniffer
     */
    Ptr<UnifiedPhySniffer> Install(Ptr<WifiNetDevice> device);

    /**
     * \brief Install sniffer on a node (first WiFi device)
     * \param node The node
     * \return The installed UnifiedPhySniffer
     */
    Ptr<UnifiedPhySniffer> Install(Ptr<Node> node);

    /**
     * \brief Install sniffer on multiple devices
     * \param devices Container of WiFi net devices
     */
    void Install(NetDeviceContainer devices);

    /**
     * \brief Install sniffer on multiple nodes
     * \param nodes Container of nodes
     */
    void Install(NodeContainer nodes);

    /**
     * \brief Get existing sniffer for a node, or nullptr if not installed
     * \param node The node
     * \return Existing UnifiedPhySniffer or nullptr
     */
    static Ptr<UnifiedPhySniffer> Get(Ptr<Node> node);

    /**
     * \brief Get existing sniffer for a node, or install a new one
     * \param node The node
     * \return UnifiedPhySniffer (existing or newly installed)
     */
    static Ptr<UnifiedPhySniffer> GetOrInstall(Ptr<Node> node);

    /**
     * \brief Get existing sniffer for a device, or install a new one
     * \param device The WiFi net device
     * \return UnifiedPhySniffer (existing or newly installed)
     */
    static Ptr<UnifiedPhySniffer> GetOrInstall(Ptr<WifiNetDevice> device);

    /**
     * \brief Print statistics for all installed sniffers
     */
    static void PrintStatistics();

  private:
    /**
     * \brief Find WiFi device on a node
     * \param node The node
     * \return First WiFi net device found, or nullptr
     */
    static Ptr<WifiNetDevice> FindWifiDevice(Ptr<Node> node);

    //!< Global map of node ID to sniffer
    static std::map<uint32_t, Ptr<UnifiedPhySniffer>> s_sniffers;
};

} // namespace ns3

#endif /* UNIFIED_PHY_SNIFFER_HELPER_H */
