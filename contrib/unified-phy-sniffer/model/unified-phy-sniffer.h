/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Unified PHY Sniffer
 *
 * Centralized MonitorSnifferRx dispatcher for efficient packet processing
 */

#ifndef UNIFIED_PHY_SNIFFER_H
#define UNIFIED_PHY_SNIFFER_H

#include "object-pool.h"
#include "parsed-frame-context.h"

#include "ns3/callback.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-types.h"

#include <functional>
#include <map>
#include <vector>

namespace ns3
{

class WifiNetDevice;
class WifiPhy;
class Node;

/**
 * \brief Subscription entry for frame handlers
 */
struct FrameSubscription
{
    uint32_t id;                                             //!< Subscription ID
    FrameInterest interest;                                  //!< Frame types of interest
    uint8_t actionCategoryFilter;                            //!< For action frames: 0xFF = all
    Mac48Address destinationFilter;                          //!< Optional destination filter
    bool filterByDestination;                                //!< Enable destination filtering
    std::function<void(ParsedFrameContext*)> callback;       //!< Handler callback
    int priority;                                            //!< Higher = called first
};

/**
 * \ingroup wifi
 * \brief Centralized MonitorSnifferRx dispatcher
 *
 * UnifiedPhySniffer consolidates multiple MonitorSnifferRx callbacks into
 * a single callback per PHY. It provides:
 * - Early frame filtering (2 bytes only) before any processing
 * - Single packet copy (or zero-copy) with lazy parsing
 * - Pre-computed signal measurements shared across all subscribers
 * - Object pooling to eliminate per-packet memory allocations
 *
 * Usage:
 * \code
 * auto sniffer = UnifiedPhySnifferHelper::GetOrInstall(node);
 * sniffer->SubscribeBeacons([](ParsedFrameContext* ctx) {
 *     ctx->EnsureAddressesParsed();
 *     // Process beacon using ctx->addr2, ctx->rssi, etc.
 * });
 * \endcode
 */
class UnifiedPhySniffer : public Object
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    UnifiedPhySniffer();
    ~UnifiedPhySniffer() override;

    /**
     * \brief Install the sniffer on a WiFi device's PHY
     * \param device The WiFi net device
     */
    void Install(Ptr<WifiNetDevice> device);

    /**
     * \brief Install the sniffer on a specific PHY
     * \param phy The WiFi PHY
     * \param ownAddress The device's own MAC address (for filtering)
     */
    void InstallOnPhy(Ptr<WifiPhy> phy, Mac48Address ownAddress);

    /**
     * \brief Get the node this sniffer is installed on
     * \return The node pointer
     */
    Ptr<Node> GetNode() const;

    // ===== Subscription API =====

    /**
     * \brief Subscribe to frames with a specific interest filter
     * \param interest Frame types to receive
     * \param callback Handler function
     * \return Subscription ID (for later unsubscribe)
     */
    uint32_t Subscribe(FrameInterest interest,
                       std::function<void(ParsedFrameContext*)> callback);

    /**
     * \brief Subscribe to beacon frames only
     * \param callback Handler function
     * \return Subscription ID
     */
    uint32_t SubscribeBeacons(std::function<void(ParsedFrameContext*)> callback);

    /**
     * \brief Subscribe to action frames of a specific category
     * \param category Action category code (e.g., 5 for 802.11k, 0x0A for 802.11v)
     * \param callback Handler function
     * \return Subscription ID
     */
    uint32_t SubscribeAction(uint8_t category,
                             std::function<void(ParsedFrameContext*)> callback);

    /**
     * \brief Subscribe to data frames
     * \param callback Handler function
     * \return Subscription ID
     */
    uint32_t SubscribeData(std::function<void(ParsedFrameContext*)> callback);

    /**
     * \brief Subscribe with destination address filtering
     * \param interest Frame types to receive
     * \param destination Only receive frames destined to this address
     * \param callback Handler function
     * \return Subscription ID
     */
    uint32_t SubscribeForDestination(FrameInterest interest,
                                      Mac48Address destination,
                                      std::function<void(ParsedFrameContext*)> callback);

    /**
     * \brief Unsubscribe a handler
     * \param subscriptionId ID returned from Subscribe*()
     */
    void Unsubscribe(uint32_t subscriptionId);

    // ===== Statistics =====

    /**
     * \brief Get count of frames processed
     * \return Total frames received
     */
    uint64_t GetFramesProcessed() const;

    /**
     * \brief Get count of frames dispatched to handlers
     * \return Total dispatches
     */
    uint64_t GetFramesDispatched() const;

    /**
     * \brief Get count of frames filtered out early
     * \return Frames rejected by early filter
     */
    uint64_t GetFramesFiltered() const;

  protected:
    void DoDispose() override;

  private:
    /**
     * \brief Main MonitorSnifferRx callback
     *
     * This is the single callback registered with WifiPhy.
     * It performs early filtering, lazy parsing, and dispatch to subscribers.
     */
    void OnMonitorSnifferRx(Ptr<const Packet> packet,
                            uint16_t channelFreqMhz,
                            WifiTxVector txVector,
                            MpduInfo mpdu,
                            SignalNoiseDbm signalNoise,
                            uint16_t staId);

    /**
     * \brief Check if any subscriber wants this frame type
     * \param type The frame type
     * \return true if at least one subscriber is interested
     */
    bool HasSubscribers(FrameType type) const;

    /**
     * \brief Get the interest flags for a frame type
     * \param type The frame type
     * \param actionCategory For action frames, the category
     * \return Matching FrameInterest flags
     */
    FrameInterest GetInterestForFrame(FrameType type, uint8_t actionCategory) const;

    /**
     * \brief Dispatch parsed frame to interested subscribers
     * \param ctx The parsed frame context
     */
    void DispatchFrame(ParsedFrameContext* ctx);

    // ===== Member variables =====

    Ptr<WifiNetDevice> m_device;                    //!< Associated device
    Ptr<WifiPhy> m_phy;                             //!< Associated PHY
    Ptr<Node> m_node;                               //!< Associated node
    Mac48Address m_ownAddress;                      //!< Device's own MAC address

    std::vector<FrameSubscription> m_beaconSubscribers;     //!< Beacon handlers
    std::vector<FrameSubscription> m_actionSubscribers;     //!< Action frame handlers
    std::vector<FrameSubscription> m_dataSubscribers;       //!< Data frame handlers
    std::vector<FrameSubscription> m_allSubscribers;        //!< All-frame handlers

    ObjectPool<ParsedFrameContext, 64> m_contextPool;       //!< Pre-allocated context pool

    uint32_t m_nextSubscriptionId;                  //!< Next subscription ID to assign

    // Statistics
    uint64_t m_framesProcessed;                     //!< Total frames received
    uint64_t m_framesDispatched;                    //!< Total dispatches
    uint64_t m_framesFiltered;                      //!< Frames rejected early

    // Interest bitmask for early filtering
    uint8_t m_beaconInterestCount;                  //!< Number of beacon subscribers
    uint8_t m_actionInterestCount;                  //!< Number of action subscribers
    uint8_t m_dataInterestCount;                    //!< Number of data subscribers
};

} // namespace ns3

#endif /* UNIFIED_PHY_SNIFFER_H */
