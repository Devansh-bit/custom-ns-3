/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "virtual-interferer-environment.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/node.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-mac.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/mobility-model.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <unistd.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("VirtualInterfererEnvironment");
NS_OBJECT_ENSURE_REGISTERED(VirtualInterfererEnvironment);

// Singleton instance
Ptr<VirtualInterfererEnvironment> VirtualInterfererEnvironment::s_instance = nullptr;
bool VirtualInterfererEnvironment::s_destroying = false;

TypeId
VirtualInterfererEnvironment::GetTypeId()
{
    static TypeId tid = TypeId("ns3::VirtualInterfererEnvironment")
        .SetParent<Object>()
        .SetGroupName("VirtualInterferer")
        .AddConstructor<VirtualInterfererEnvironment>()
        .AddTraceSource("DfsTrigger",
                        "Fired when DFS radar is detected on a receiver's channel",
                        MakeTraceSourceAccessor(&VirtualInterfererEnvironment::m_dfsTriggerTrace),
                        "ns3::VirtualInterfererEnvironment::DfsTriggerCallback");
    return tid;
}

Ptr<VirtualInterfererEnvironment>
VirtualInterfererEnvironment::Get()
{
    if (!s_instance)
    {
        s_instance = CreateObject<VirtualInterfererEnvironment>();
    }
    return s_instance;
}

void
VirtualInterfererEnvironment::Destroy()
{
    if (s_instance)
    {
        s_destroying = true;  // Prevent interferers from calling back during cleanup

        s_instance->Stop();
        s_instance->m_interferers.clear();
        s_instance->m_receivers.clear();

        s_instance = nullptr;
        s_destroying = false;
    }
}

bool
VirtualInterfererEnvironment::IsBeingDestroyed()
{
    return s_destroying;
}

VirtualInterfererEnvironment::VirtualInterfererEnvironment()
    : m_running(false),
      m_updateCount(0),
      m_injectionCount(0)
{
    NS_LOG_FUNCTION(this);
}

VirtualInterfererEnvironment::~VirtualInterfererEnvironment()
{
    NS_LOG_FUNCTION(this);
}

void
VirtualInterfererEnvironment::DoDispose()
{
    NS_LOG_FUNCTION(this);

    Stop();
    m_interferers.clear();
    m_receivers.clear();

    Object::DoDispose();
}

// ==================== CONFIGURATION ====================

void
VirtualInterfererEnvironment::SetConfig(const VirtualInterfererEnvironmentConfig& config)
{
    NS_LOG_FUNCTION(this);
    m_config = config;
}

VirtualInterfererEnvironmentConfig
VirtualInterfererEnvironment::GetConfig() const
{
    return m_config;
}

// ==================== INTERFERER MANAGEMENT ====================

void
VirtualInterfererEnvironment::RegisterInterferer(Ptr<VirtualInterferer> interferer)
{
    NS_LOG_FUNCTION(this << interferer);

    // Check if already registered
    auto it = std::find(m_interferers.begin(), m_interferers.end(), interferer);
    if (it != m_interferers.end())
    {
        NS_LOG_WARN("Interferer " << interferer->GetId() << " already registered");
        return;
    }

    m_interferers.push_back(interferer);
    NS_LOG_INFO("Registered interferer " << interferer->GetId()
                << " (" << interferer->GetInterfererType() << ")"
                << ", total: " << m_interferers.size());
}

void
VirtualInterfererEnvironment::UnregisterInterferer(Ptr<VirtualInterferer> interferer)
{
    NS_LOG_FUNCTION(this << interferer);

    auto it = std::find(m_interferers.begin(), m_interferers.end(), interferer);
    if (it != m_interferers.end())
    {
        m_interferers.erase(it);
        NS_LOG_INFO("Unregistered interferer " << interferer->GetId()
                    << ", remaining: " << m_interferers.size());
    }
}

std::vector<Ptr<VirtualInterferer>>
VirtualInterfererEnvironment::GetInterferers() const
{
    return m_interferers;
}

size_t
VirtualInterfererEnvironment::GetInterfererCount() const
{
    return m_interferers.size();
}

// ==================== WIFI RECEIVER MANAGEMENT ====================

void
VirtualInterfererEnvironment::RegisterWifiReceiver(const WifiReceiverInfo& info)
{
    NS_LOG_FUNCTION(this << info.nodeId);

    m_receivers.push_back(info);
    NS_LOG_INFO("Registered WiFi receiver node " << info.nodeId
                << ", total: " << m_receivers.size());
}

void
VirtualInterfererEnvironment::AutoRegisterWifiDevices(NodeContainer nodes)
{
    NS_LOG_FUNCTION(this << nodes.GetN());

    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<Node> node = nodes.Get(i);

        // Find WiFi devices on this node
        for (uint32_t d = 0; d < node->GetNDevices(); ++d)
        {
            Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(d));
            if (!wifiDev)
            {
                continue;
            }

            WifiReceiverInfo info;
            info.nodeId = node->GetId();
            info.device = wifiDev;

            // Get BSSID from MAC (linkId=0 for primary link)
            Ptr<WifiMac> mac = wifiDev->GetMac();
            if (mac)
            {
                info.bssid = mac->GetBssid(0);
            }

            // Position getter using mobility model
            Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
            if (mobility)
            {
                info.getPosition = [mobility]() { return mobility->GetPosition(); };
            }
            else
            {
                info.getPosition = []() { return Vector(0, 0, 0); };
            }

            // Channel getter using PHY
            Ptr<WifiPhy> phy = wifiDev->GetPhy();
            if (phy)
            {
                info.getChannel = [phy]() { return phy->GetChannelNumber(); };

                // Install error model for packet loss injection
                if (m_config.enablePacketLoss)
                {
                    info.errorModel = CreateObject<VirtualInterfererErrorModel>();
                    phy->SetPostReceptionErrorModel(info.errorModel);
                    NS_LOG_INFO("Installed VirtualInterfererErrorModel on node " << info.nodeId);
                }
            }
            else
            {
                info.getChannel = []() { return uint8_t(0); };
            }

            RegisterWifiReceiver(info);
        }
    }
}

size_t
VirtualInterfererEnvironment::GetReceiverCount() const
{
    return m_receivers.size();
}

// ==================== LIFECYCLE ====================

void
VirtualInterfererEnvironment::Start()
{
    NS_LOG_FUNCTION(this);

    if (m_running)
    {
        NS_LOG_WARN("VirtualInterfererEnvironment already running");
        return;
    }

    m_running = true;

    // Schedule first update
    m_updateEvent = Simulator::Schedule(m_config.updateInterval,
                                         &VirtualInterfererEnvironment::DoPeriodicUpdate, this);

    NS_LOG_INFO("VirtualInterfererEnvironment started with "
                << m_interferers.size() << " interferers and "
                << m_receivers.size() << " receivers");
}

void
VirtualInterfererEnvironment::Stop()
{
    NS_LOG_FUNCTION(this);

    if (!m_running)
    {
        return;
    }

    m_running = false;

    if (m_updateEvent.IsPending())
    {
        Simulator::Cancel(m_updateEvent);
    }
    if (m_injectionEvent.IsPending())
    {
        Simulator::Cancel(m_injectionEvent);
    }

    NS_LOG_INFO("VirtualInterfererEnvironment stopped. Updates: " << m_updateCount
                << ", Injections: " << m_injectionCount);
}

bool
VirtualInterfererEnvironment::IsRunning() const
{
    return m_running;
}

void
VirtualInterfererEnvironment::ForceUpdate()
{
    NS_LOG_FUNCTION(this);
    DoPeriodicUpdate();
}

// ==================== PERIODIC CALLBACKS ====================

void
VirtualInterfererEnvironment::DoPeriodicUpdate()
{
    NS_LOG_FUNCTION(this);

    if (!m_running)
    {
        return;
    }

    m_updateCount++;
    Time now = Simulator::Now();

    NS_LOG_DEBUG("DoPeriodicUpdate: updateCount=" << m_updateCount
                 << ", receivers=" << m_receivers.size()
                 << ", interferers=" << m_interferers.size());

    // Update all interferers (for channel hopping, schedule state, etc.)
    for (auto& interferer : m_interferers)
    {
        interferer->Update(now);
    }

    // Reset accumulated effects for all receivers
    for (auto& receiver : m_receivers)
    {
        receiver.accumulatedNonWifiCca = 0.0;
        receiver.accumulatedPacketLoss = 0.0;
        receiver.dfsTriggerPending = false;
    }

    // Calculate effects from all interferers to all receivers
    for (auto& receiver : m_receivers)
    {
        CalculateEffectsForReceiver(receiver);
    }

    // Perform injection
    DoPeriodicInjection();

    // BIDIRECTIONAL SYNC: Wait for spectrum-shadow to catch up before proceeding
    // This prevents basic-simulation from getting too far ahead
    // Only sync if spectrum-shadow is running (sync file exists with recent timestamp)
    {
        const double tolerance = 0.05;  // 50ms tolerance
        const double currentTime = now.GetSeconds();

        // First check if sync file exists at all
        std::ifstream checkFile("/tmp/ns3-spectrum-sync-timestamp.txt");
        if (checkFile.good())
        {
            double spectrumTime = 0.0;
            if (checkFile >> spectrumTime)
            {
                checkFile.close();
                // Only wait if spectrum-shadow has been active recently (within 5s)
                if (currentTime - spectrumTime < 5.0)
                {
                    int maxWait = 500;  // Max 5s wait (500 * 10ms)
                    for (int i = 0; i < maxWait; i++)
                    {
                        std::ifstream spectrumSyncFile("/tmp/ns3-spectrum-sync-timestamp.txt");
                        if (spectrumSyncFile >> spectrumTime)
                        {
                            spectrumSyncFile.close();
                            if (spectrumTime >= currentTime - m_config.updateInterval.GetSeconds() - tolerance)
                            {
                                break;
                            }
                        }
                        else
                        {
                            spectrumSyncFile.close();
                            break;  // File read failed, don't wait
                        }
                        usleep(10000);  // Wait 10ms and check again
                    }
                }
            }
            else
            {
                checkFile.close();
            }
        }
        else
        {
            checkFile.close();
            // No sync file - spectrum-shadow not running, proceed without waiting
        }
    }

    // Write sync timestamp for spectrum-shadow coordination (every updateInterval)
    {
        std::ofstream syncFile("/tmp/ns3-sync-timestamp.txt", std::ios::trunc);
        if (syncFile.is_open())
        {
            syncFile << std::fixed << std::setprecision(6) << now.GetSeconds();
            syncFile.close();
        }
    }

    // Schedule next update
    m_updateEvent = Simulator::Schedule(m_config.updateInterval,
                                         &VirtualInterfererEnvironment::DoPeriodicUpdate, this);
}

void
VirtualInterfererEnvironment::DoPeriodicInjection()
{
    NS_LOG_FUNCTION(this);

    m_injectionCount++;

    for (auto& receiver : m_receivers)
    {
        if (m_config.enableNonWifiCca)
        {
            InjectNonWifiCca(receiver);
        }

        if (m_config.enablePacketLoss)
        {
            InjectPacketLoss(receiver);
        }

        if (m_config.enableDfs)
        {
            CheckDfsTrigger(receiver);
        }
    }
}

// ==================== EFFECT CALCULATION ====================

void
VirtualInterfererEnvironment::CalculateEffectsForReceiver(WifiReceiverInfo& receiver)
{
    Vector receiverPos = receiver.getPosition();
    uint8_t receiverChannel = receiver.getChannel();

    NS_LOG_DEBUG("CalculateEffectsForReceiver: node=" << receiver.nodeId
                 << ", channel=" << (int)receiverChannel
                 << ", pos=(" << receiverPos.x << "," << receiverPos.y << "," << receiverPos.z << ")");

    if (receiverChannel == 0)
    {
        NS_LOG_DEBUG("  Skipping: invalid channel 0");
        return; // Invalid channel
    }

    InterferenceEffect aggregateEffect;

    for (const auto& interferer : m_interferers)
    {
        if (!interferer->IsActive())
        {
            NS_LOG_DEBUG("  Interferer " << interferer->GetInterfererType() << " not active, skipping");
            continue;
        }

        NS_LOG_DEBUG("  Calling CalculateEffectFromInterferer for " << interferer->GetInterfererType());

        InterferenceEffect effect = CalculateEffectFromInterferer(
            interferer, receiverPos, receiverChannel);

        NS_LOG_DEBUG("    Result: nonWifiCca=" << effect.nonWifiCcaPercent << "%");

        aggregateEffect += effect;
    }

    // Apply caps
    aggregateEffect.nonWifiCcaPercent = std::min(
        aggregateEffect.nonWifiCcaPercent, m_config.maxNonWifiUtilPercent);
    aggregateEffect.packetLossProbability = std::min(
        aggregateEffect.packetLossProbability, m_config.maxPacketLossProb);

    // Store accumulated effects
    receiver.accumulatedNonWifiCca = aggregateEffect.nonWifiCcaPercent;
    receiver.accumulatedPacketLoss = aggregateEffect.packetLossProbability;
    receiver.dfsTriggerPending = aggregateEffect.triggersDfs;

    if (aggregateEffect.nonWifiCcaPercent > 0)
    {
        NS_LOG_DEBUG("Receiver " << receiver.nodeId << " ch " << (int)receiverChannel
                     << ": nonWifiCca=" << aggregateEffect.nonWifiCcaPercent
                     << "%, packetLoss=" << aggregateEffect.packetLossProbability);
    }
}

InterferenceEffect
VirtualInterfererEnvironment::CalculateEffectFromInterferer(
    Ptr<VirtualInterferer> interferer,
    const Vector& receiverPos,
    uint8_t receiverChannel) const
{
    // Check channel overlap
    if (!interferer->ChannelOverlaps(receiverChannel))
    {
        return InterferenceEffect();
    }

    // Calculate distance
    Vector intfPos = interferer->GetPosition();
    double dx = receiverPos.x - intfPos.x;
    double dy = receiverPos.y - intfPos.y;
    double dz = receiverPos.z - intfPos.z;
    double distanceM = std::sqrt(dx*dx + dy*dy + dz*dz);

    if (distanceM < 0.1)
    {
        distanceM = 0.1; // Minimum 10cm
    }

    // Calculate received power
    double rxPowerDbm = interferer->CalculateRxPower(distanceM);

    // Let interferer calculate its specific effect
    return interferer->CalculateEffect(receiverPos, receiverChannel, distanceM, rxPowerDbm);
}

// ==================== INJECTION METHODS ====================

void
VirtualInterfererEnvironment::InjectNonWifiCca(WifiReceiverInfo& receiver)
{
    if (receiver.accumulatedNonWifiCca <= 0)
    {
        return;
    }

    // Convert percentage to time within the update interval (for future CCA injection)
    double windowSec = m_config.updateInterval.GetSeconds();
    double nonWifiTimeSec = (receiver.accumulatedNonWifiCca / 100.0) * windowSec;
    (void)nonWifiTimeSec;  // Currently unused, placeholder for CCA injection

    // Update AP's BSS Load IE if this is an AP
    if (receiver.device)
    {
        Ptr<WifiMac> mac = receiver.device->GetMac();
        Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(mac);
        if (apMac)
        {
            // Scale to 0-255 for BSS Load IE
            uint8_t nonWifiScaled = static_cast<uint8_t>(
                std::min(255.0, receiver.accumulatedNonWifiCca * 2.55));

            ApWifiMac::SetNonWifiChannelUtilization(receiver.bssid, nonWifiScaled);

            NS_LOG_DEBUG("Updated BSS Load IE for " << receiver.bssid
                         << ": nonWifiUtil=" << (int)nonWifiScaled);
        }
    }
}

void
VirtualInterfererEnvironment::InjectPacketLoss(WifiReceiverInfo& receiver)
{
    // Update error model with current packet loss rate
    if (receiver.errorModel)
    {
        double oldRate = receiver.errorModel->GetPacketLossRate();
        receiver.errorModel->SetPacketLossRate(receiver.accumulatedPacketLoss);

        // Log significant changes (> 1%)
        if (std::abs(receiver.accumulatedPacketLoss - oldRate) > 0.01)
        {
            NS_LOG_DEBUG("Node " << receiver.nodeId << " packet loss rate: "
                         << oldRate << " -> " << receiver.accumulatedPacketLoss);

            // Log for non-trivial interference (> 2%)
            if (receiver.accumulatedPacketLoss > 0.02)
            {
                NS_LOG_DEBUG("[VI-LOSS] Node " << receiver.nodeId
                             << " Ch" << (int)receiver.getChannel()
                             << " packet loss: " << (receiver.accumulatedPacketLoss * 100.0)
                             << "% at t=" << Simulator::Now().GetSeconds() << "s");
            }
        }
    }
    else if (receiver.accumulatedPacketLoss > 0.001)
    {
        // Warn if we have packet loss but no error model
        NS_LOG_DEBUG("[VI-LOSS-WARN] Node " << receiver.nodeId
                     << " has packet loss " << (receiver.accumulatedPacketLoss * 100.0)
                     << "% but NO error model installed!");
    }
}

void
VirtualInterfererEnvironment::CheckDfsTrigger(WifiReceiverInfo& receiver)
{
    uint8_t receiverChannel = receiver.getChannel();

    // Debug: Log DFS checking for receivers on DFS channels
    static std::set<uint8_t> dfsChannels = {52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140};
    if (dfsChannels.count(receiverChannel) > 0 && Simulator::Now().GetSeconds() < 10.0)
    {
        NS_LOG_DEBUG("CheckDfsTrigger: node=" << receiver.nodeId
                     << " on DFS channel " << (int)receiverChannel
                     << " dfsTriggerPending=" << receiver.dfsTriggerPending);
    }

    if (!receiver.dfsTriggerPending)
    {
        return;
    }

    NS_LOG_INFO("DFS trigger pending for node " << receiver.nodeId
                << " on channel " << (int)receiverChannel);

    // Fire DFS trace callback: nodeId, receiverChannel, dfsChannel (same for now)
    m_dfsTriggerTrace(receiver.nodeId, receiverChannel, receiverChannel);

    NS_LOG_DEBUG("[DFS-RADAR] Radar detected on channel " << (int)receiverChannel
                 << " affecting node " << receiver.nodeId << " at t="
                 << Simulator::Now().GetSeconds() << "s");
}

// ==================== EFFECT QUERIES ====================

InterferenceEffect
VirtualInterfererEnvironment::GetAggregateEffect(const Vector& position, uint8_t channel) const
{
    InterferenceEffect aggregate;

    for (const auto& interferer : m_interferers)
    {
        if (!interferer->IsActive())
        {
            continue;
        }

        InterferenceEffect effect = CalculateEffectFromInterferer(
            interferer, position, channel);
        aggregate += effect;
    }

    return aggregate;
}

std::map<uint8_t, InterferenceEffect>
VirtualInterfererEnvironment::GetAllChannelEffects(const Vector& position) const
{
    std::map<uint8_t, InterferenceEffect> effects;

    // 2.4 GHz channels 1-14
    for (uint8_t ch = 1; ch <= 14; ++ch)
    {
        effects[ch] = GetAggregateEffect(position, ch);
    }

    // 5 GHz channels (common ones)
    std::vector<uint8_t> fiveGhzChannels = {
        36, 40, 44, 48, 52, 56, 60, 64,
        100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
        149, 153, 157, 161, 165
    };
    for (uint8_t ch : fiveGhzChannels)
    {
        effects[ch] = GetAggregateEffect(position, ch);
    }

    return effects;
}

// ==================== STATISTICS ====================

uint64_t
VirtualInterfererEnvironment::GetUpdateCount() const
{
    return m_updateCount;
}

uint64_t
VirtualInterfererEnvironment::GetInjectionCount() const
{
    return m_injectionCount;
}

} // namespace ns3
