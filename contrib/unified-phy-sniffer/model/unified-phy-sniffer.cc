/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Unified PHY Sniffer
 */

#include "unified-phy-sniffer.h"

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy.h"

#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("UnifiedPhySniffer");
NS_OBJECT_ENSURE_REGISTERED(UnifiedPhySniffer);

TypeId
UnifiedPhySniffer::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::UnifiedPhySniffer")
            .SetParent<Object>()
            .SetGroupName("Wifi")
            .AddConstructor<UnifiedPhySniffer>();
    return tid;
}

UnifiedPhySniffer::UnifiedPhySniffer()
    : m_device(nullptr),
      m_phy(nullptr),
      m_node(nullptr),
      m_nextSubscriptionId(1),
      m_framesProcessed(0),
      m_framesDispatched(0),
      m_framesFiltered(0),
      m_beaconInterestCount(0),
      m_actionInterestCount(0),
      m_dataInterestCount(0)
{
    NS_LOG_FUNCTION(this);
}

UnifiedPhySniffer::~UnifiedPhySniffer()
{
    NS_LOG_FUNCTION(this);
}

void
UnifiedPhySniffer::DoDispose()
{
    NS_LOG_FUNCTION(this);

    m_beaconSubscribers.clear();
    m_actionSubscribers.clear();
    m_dataSubscribers.clear();
    m_allSubscribers.clear();

    m_device = nullptr;
    m_phy = nullptr;
    m_node = nullptr;

    Object::DoDispose();
}

void
UnifiedPhySniffer::Install(Ptr<WifiNetDevice> device)
{
    NS_LOG_FUNCTION(this << device);

    m_device = device;
    m_node = device->GetNode();
    m_phy = device->GetPhy();
    m_ownAddress = device->GetMac()->GetAddress();

    InstallOnPhy(m_phy, m_ownAddress);
}

void
UnifiedPhySniffer::InstallOnPhy(Ptr<WifiPhy> phy, Mac48Address ownAddress)
{
    NS_LOG_FUNCTION(this << phy << ownAddress);

    m_phy = phy;
    m_ownAddress = ownAddress;

    // Register single callback with the PHY
    phy->TraceConnectWithoutContext(
        "MonitorSnifferRx",
        MakeCallback(&UnifiedPhySniffer::OnMonitorSnifferRx, this));

    NS_LOG_INFO("UnifiedPhySniffer installed on PHY, own address: " << ownAddress);
}

Ptr<Node>
UnifiedPhySniffer::GetNode() const
{
    return m_node;
}

uint32_t
UnifiedPhySniffer::Subscribe(FrameInterest interest,
                              std::function<void(ParsedFrameContext*)> callback)
{
    NS_LOG_FUNCTION(this << static_cast<int>(interest));

    FrameSubscription sub;
    sub.id = m_nextSubscriptionId++;
    sub.interest = interest;
    sub.actionCategoryFilter = 0xFF; // All categories
    sub.filterByDestination = false;
    sub.callback = callback;
    sub.priority = 0;

    // Add to appropriate subscriber list(s)
    if (HasInterest(interest, FrameInterest::BEACON))
    {
        m_beaconSubscribers.push_back(sub);
        m_beaconInterestCount++;
    }
    if (HasInterest(interest, FrameInterest::ACTION_CAT5) ||
        HasInterest(interest, FrameInterest::ACTION_CAT10) ||
        HasInterest(interest, FrameInterest::ACTION_OTHER))
    {
        m_actionSubscribers.push_back(sub);
        m_actionInterestCount++;
    }
    if (HasInterest(interest, FrameInterest::DATA))
    {
        m_dataSubscribers.push_back(sub);
        m_dataInterestCount++;
    }
    if (HasInterest(interest, FrameInterest::ALL))
    {
        m_allSubscribers.push_back(sub);
    }

    NS_LOG_DEBUG("Subscription " << sub.id << " registered with interest "
                 << static_cast<int>(interest));

    return sub.id;
}

uint32_t
UnifiedPhySniffer::SubscribeBeacons(std::function<void(ParsedFrameContext*)> callback)
{
    return Subscribe(FrameInterest::BEACON, callback);
}

uint32_t
UnifiedPhySniffer::SubscribeAction(uint8_t category,
                                    std::function<void(ParsedFrameContext*)> callback)
{
    NS_LOG_FUNCTION(this << static_cast<int>(category));

    FrameSubscription sub;
    sub.id = m_nextSubscriptionId++;
    sub.actionCategoryFilter = category;
    sub.filterByDestination = false;
    sub.callback = callback;
    sub.priority = 0;

    // Set interest based on category
    if (category == 5)
    {
        sub.interest = FrameInterest::ACTION_CAT5;
    }
    else if (category == 0x0A)
    {
        sub.interest = FrameInterest::ACTION_CAT10;
    }
    else
    {
        sub.interest = FrameInterest::ACTION_OTHER;
    }

    m_actionSubscribers.push_back(sub);
    m_actionInterestCount++;

    NS_LOG_DEBUG("Action subscription " << sub.id << " registered for category "
                 << static_cast<int>(category));

    return sub.id;
}

uint32_t
UnifiedPhySniffer::SubscribeData(std::function<void(ParsedFrameContext*)> callback)
{
    return Subscribe(FrameInterest::DATA, callback);
}

uint32_t
UnifiedPhySniffer::SubscribeForDestination(FrameInterest interest,
                                            Mac48Address destination,
                                            std::function<void(ParsedFrameContext*)> callback)
{
    NS_LOG_FUNCTION(this << static_cast<int>(interest) << destination);

    FrameSubscription sub;
    sub.id = m_nextSubscriptionId++;
    sub.interest = interest;
    sub.actionCategoryFilter = 0xFF;
    sub.destinationFilter = destination;
    sub.filterByDestination = true;
    sub.callback = callback;
    sub.priority = 0;

    // Add to appropriate lists
    if (HasInterest(interest, FrameInterest::BEACON))
    {
        m_beaconSubscribers.push_back(sub);
        m_beaconInterestCount++;
    }
    if (HasInterest(interest, FrameInterest::ACTION_CAT5) ||
        HasInterest(interest, FrameInterest::ACTION_CAT10) ||
        HasInterest(interest, FrameInterest::ACTION_OTHER))
    {
        m_actionSubscribers.push_back(sub);
        m_actionInterestCount++;
    }
    if (HasInterest(interest, FrameInterest::DATA))
    {
        m_dataSubscribers.push_back(sub);
        m_dataInterestCount++;
    }

    return sub.id;
}

void
UnifiedPhySniffer::Unsubscribe(uint32_t subscriptionId)
{
    NS_LOG_FUNCTION(this << subscriptionId);

    auto removeFromList = [subscriptionId](std::vector<FrameSubscription>& list) {
        list.erase(
            std::remove_if(list.begin(),
                           list.end(),
                           [subscriptionId](const FrameSubscription& sub) {
                               return sub.id == subscriptionId;
                           }),
            list.end());
    };

    size_t beaconBefore = m_beaconSubscribers.size();
    size_t actionBefore = m_actionSubscribers.size();
    size_t dataBefore = m_dataSubscribers.size();

    removeFromList(m_beaconSubscribers);
    removeFromList(m_actionSubscribers);
    removeFromList(m_dataSubscribers);
    removeFromList(m_allSubscribers);

    m_beaconInterestCount = m_beaconSubscribers.size();
    m_actionInterestCount = m_actionSubscribers.size();
    m_dataInterestCount = m_dataSubscribers.size();

    NS_LOG_DEBUG("Unsubscribed " << subscriptionId
                 << " (beacon: " << beaconBefore << "->" << m_beaconSubscribers.size()
                 << ", action: " << actionBefore << "->" << m_actionSubscribers.size()
                 << ", data: " << dataBefore << "->" << m_dataSubscribers.size() << ")");
}

bool
UnifiedPhySniffer::HasSubscribers(FrameType type) const
{
    switch (type)
    {
    case FrameType::BEACON:
        return m_beaconInterestCount > 0 || !m_allSubscribers.empty();
    case FrameType::ACTION:
        return m_actionInterestCount > 0 || !m_allSubscribers.empty();
    case FrameType::DATA:
        return m_dataInterestCount > 0 || !m_allSubscribers.empty();
    default:
        return !m_allSubscribers.empty();
    }
}

FrameInterest
UnifiedPhySniffer::GetInterestForFrame(FrameType type, uint8_t actionCategory) const
{
    switch (type)
    {
    case FrameType::BEACON:
        return FrameInterest::BEACON;
    case FrameType::ACTION:
        if (actionCategory == 5)
        {
            return FrameInterest::ACTION_CAT5;
        }
        else if (actionCategory == 0x0A)
        {
            return FrameInterest::ACTION_CAT10;
        }
        return FrameInterest::ACTION_OTHER;
    case FrameType::DATA:
        return FrameInterest::DATA;
    default:
        return FrameInterest::NONE;
    }
}

void
UnifiedPhySniffer::OnMonitorSnifferRx(Ptr<const Packet> packet,
                                       uint16_t channelFreqMhz,
                                       WifiTxVector txVector,
                                       MpduInfo mpdu,
                                       SignalNoiseDbm signalNoise,
                                       uint16_t staId)
{
    m_framesProcessed++;

    // ===== EARLY FILTER: Read only 2 bytes =====
    if (packet->GetSize() < 2)
    {
        m_framesFiltered++;
        return;
    }

    uint8_t fc[2];
    packet->CopyData(fc, 2); // Only 2 bytes, NOT full copy

    // Quick frame type classification
    uint8_t frameType = (fc[0] >> 2) & 0x03;
    uint8_t subtype = (fc[0] >> 4) & 0x0F;

    FrameType type;
    switch (frameType)
    {
    case 0: // Management
        if (subtype == 8)
        {
            type = FrameType::BEACON;
        }
        else if (subtype == 13)
        {
            type = FrameType::ACTION;
        }
        else
        {
            type = FrameType::MANAGEMENT_OTHER;
        }
        break;
    case 2: // Data
        type = FrameType::DATA;
        break;
    default:
        type = FrameType::UNKNOWN;
        break;
    }

    // Skip if no subscribers for this frame type
    if (!HasSubscribers(type))
    {
        m_framesFiltered++;
        return;
    }

    // ===== ACQUIRE CONTEXT FROM POOL (no heap allocation) =====
    ParsedFrameContext* ctx = m_contextPool.Acquire();

    // Initialize with basic info
    ctx->Initialize(packet, channelFreqMhz, txVector, signalNoise.signal, signalNoise.noise, staId);

    // Set node/device info if available
    if (m_node)
    {
        ctx->nodeId = m_node->GetId();
    }
    if (m_device)
    {
        ctx->deviceId = m_device->GetIfIndex();
    }

    // ===== DISPATCH TO SUBSCRIBERS =====
    DispatchFrame(ctx);

    // ===== RELEASE CONTEXT BACK TO POOL =====
    ctx->Reset();
    m_contextPool.Release(ctx);
}

void
UnifiedPhySniffer::DispatchFrame(ParsedFrameContext* ctx)
{
    std::vector<FrameSubscription>* subscribers = nullptr;

    switch (ctx->type)
    {
    case FrameType::BEACON:
        subscribers = &m_beaconSubscribers;
        break;
    case FrameType::ACTION:
        subscribers = &m_actionSubscribers;
        break;
    case FrameType::DATA:
        subscribers = &m_dataSubscribers;
        break;
    default:
        break;
    }

    // Dispatch to type-specific subscribers
    if (subscribers)
    {
        for (auto& sub : *subscribers)
        {
            // Check action category filter
            if (ctx->type == FrameType::ACTION && sub.actionCategoryFilter != 0xFF)
            {
                ctx->EnsureActionParsed();
                if (ctx->actionCategory != sub.actionCategoryFilter)
                {
                    continue;
                }
            }

            // Check destination filter
            if (sub.filterByDestination)
            {
                ctx->EnsureAddressesParsed();
                if (ctx->addr1 != sub.destinationFilter)
                {
                    continue;
                }
            }

            // Invoke callback
            sub.callback(ctx);
            m_framesDispatched++;
        }
    }

    // Dispatch to ALL subscribers
    for (auto& sub : m_allSubscribers)
    {
        if (sub.filterByDestination)
        {
            ctx->EnsureAddressesParsed();
            if (ctx->addr1 != sub.destinationFilter)
            {
                continue;
            }
        }
        sub.callback(ctx);
        m_framesDispatched++;
    }
}

uint64_t
UnifiedPhySniffer::GetFramesProcessed() const
{
    return m_framesProcessed;
}

uint64_t
UnifiedPhySniffer::GetFramesDispatched() const
{
    return m_framesDispatched;
}

uint64_t
UnifiedPhySniffer::GetFramesFiltered() const
{
    return m_framesFiltered;
}

} // namespace ns3
