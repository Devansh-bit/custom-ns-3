/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Virtual Interferer Environment - Singleton manager for all interferers
 */

#ifndef VIRTUAL_INTERFERER_ENVIRONMENT_H
#define VIRTUAL_INTERFERER_ENVIRONMENT_H

#include "virtual-interferer.h"
#include "virtual-interferer-error-model.h"

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include "ns3/wifi-net-device.h"
#include "ns3/node-container.h"
#include "ns3/mac48-address.h"
#include "ns3/traced-callback.h"

#include <vector>
#include <map>
#include <functional>
#include <memory>

namespace ns3
{

// Forward declarations
class ErrorModel;

/**
 * \brief Configuration for the VirtualInterfererEnvironment
 */
struct VirtualInterfererEnvironmentConfig
{
    Time updateInterval = MilliSeconds(100);         ///< How often to recalculate effects
    Time injectionInterval = MilliSeconds(100);      ///< How often to inject into monitors

    double pathLossExponent = 3.0;                   ///< Default path loss exponent
    double referenceDistanceM = 1.0;                 ///< Reference distance (m)
    double referenceLossDb = 40.0;                   ///< Loss at reference distance (dB)

    double ccaEdThresholdDbm = -62.0;                ///< CCA energy detect threshold
    double ccaSensitivityDbm = -82.0;                ///< CCA preamble detect threshold

    bool enableNonWifiCca = true;                    ///< Enable non-WiFi CCA injection
    bool enablePacketLoss = true;                    ///< Enable packet loss injection
    bool enableDfs = true;                           ///< Enable DFS trigger

    double maxNonWifiUtilPercent = 100.0;            ///< Cap for non-WiFi utilization
    double maxPacketLossProb = 1.0;                  ///< Cap for packet loss probability
};

/**
 * \brief Information about a registered WiFi receiver
 */
struct WifiReceiverInfo
{
    uint32_t nodeId = 0;                             ///< Node ID
    Ptr<WifiNetDevice> device;                       ///< WiFi net device
    Mac48Address bssid;                              ///< BSSID

    std::function<Vector()> getPosition;             ///< Function to get current position
    std::function<uint8_t()> getChannel;             ///< Function to get current channel

    // Accumulated effects (reset each injection cycle)
    double accumulatedNonWifiCca = 0.0;
    double accumulatedPacketLoss = 0.0;
    bool dfsTriggerPending = false;

    // Error model for packet loss injection
    Ptr<VirtualInterfererErrorModel> errorModel;
};

/**
 * \brief Singleton manager for virtual interferers
 *
 * This class manages all virtual interferers and WiFi receivers.
 * It periodically calculates interference effects and injects them
 * into the appropriate components.
 *
 * Usage:
 *   auto env = VirtualInterfererEnvironment::Get();
 *   env->SetConfig(config);
 *   env->AutoRegisterWifiDevices(nodes);
 *   env->Start();
 */
class VirtualInterfererEnvironment : public Object
{
public:
    /**
     * \brief Get the type ID
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * \brief Get singleton instance
     * \return Pointer to the singleton environment
     */
    static Ptr<VirtualInterfererEnvironment> Get();

    /**
     * \brief Destroy the singleton (for cleanup between simulations)
     */
    static void Destroy();

    VirtualInterfererEnvironment();
    virtual ~VirtualInterfererEnvironment();

    // ==================== CONFIGURATION ====================

    /**
     * \brief Set environment configuration
     * \param config Configuration struct
     */
    void SetConfig(const VirtualInterfererEnvironmentConfig& config);

    /**
     * \brief Get current configuration
     * \return Configuration struct
     */
    VirtualInterfererEnvironmentConfig GetConfig() const;

    // ==================== INTERFERER MANAGEMENT ====================

    /**
     * \brief Register an interferer (called by VirtualInterferer::Install)
     * \param interferer Pointer to the interferer
     */
    void RegisterInterferer(Ptr<VirtualInterferer> interferer);

    /**
     * \brief Unregister an interferer
     * \param interferer Pointer to the interferer
     */
    void UnregisterInterferer(Ptr<VirtualInterferer> interferer);

    /**
     * \brief Get all registered interferers
     * \return Vector of interferer pointers
     */
    std::vector<Ptr<VirtualInterferer>> GetInterferers() const;

    /**
     * \brief Get number of registered interferers
     * \return Count
     */
    size_t GetInterfererCount() const;

    // ==================== WIFI RECEIVER MANAGEMENT ====================

    /**
     * \brief Register a WiFi receiver for effect injection
     * \param info Receiver information struct
     */
    void RegisterWifiReceiver(const WifiReceiverInfo& info);

    /**
     * \brief Auto-register all WiFi devices from nodes
     * \param nodes Node container with WiFi devices
     */
    void AutoRegisterWifiDevices(NodeContainer nodes);

    /**
     * \brief Get number of registered receivers
     * \return Count
     */
    size_t GetReceiverCount() const;

    // ==================== LIFECYCLE ====================

    /**
     * \brief Start the environment (begin periodic updates)
     */
    void Start();

    /**
     * \brief Stop the environment
     */
    void Stop();

    /**
     * \brief Check if environment is running
     * \return true if running
     */
    bool IsRunning() const;

    /**
     * \brief Force immediate update (for debugging)
     */
    void ForceUpdate();

    // ==================== EFFECT QUERIES ====================

    /**
     * \brief Get aggregate interference effect at a position/channel
     * \param position Receiver position
     * \param channel WiFi channel
     * \return Aggregate effect from all interferers
     */
    InterferenceEffect GetAggregateEffect(const Vector& position, uint8_t channel) const;

    /**
     * \brief Get effects for all channels at a position
     * \param position Receiver position
     * \return Map of channel -> effect
     */
    std::map<uint8_t, InterferenceEffect> GetAllChannelEffects(const Vector& position) const;

    // ==================== STATISTICS ====================

    /**
     * \brief Get total number of update cycles
     * \return Update count
     */
    uint64_t GetUpdateCount() const;

    /**
     * \brief Get total number of injections performed
     * \return Injection count
     */
    uint64_t GetInjectionCount() const;

    /**
     * \brief Check if environment is being destroyed (for safe cleanup)
     * \return true if destruction is in progress
     */
    static bool IsBeingDestroyed();

protected:
    virtual void DoDispose() override;

private:
    // Singleton instance
    static Ptr<VirtualInterfererEnvironment> s_instance;
    static bool s_destroying;

    // Periodic update callbacks
    void DoPeriodicUpdate();
    void DoPeriodicInjection();
    EventId m_updateEvent;
    EventId m_injectionEvent;

    // Effect calculation
    void CalculateEffectsForReceiver(WifiReceiverInfo& receiver);
    InterferenceEffect CalculateEffectFromInterferer(
        Ptr<VirtualInterferer> interferer,
        const Vector& receiverPos,
        uint8_t receiverChannel) const;

    // Injection methods
    void InjectNonWifiCca(WifiReceiverInfo& receiver);
    void InjectPacketLoss(WifiReceiverInfo& receiver);
    void CheckDfsTrigger(WifiReceiverInfo& receiver);

    // State
    VirtualInterfererEnvironmentConfig m_config;
    std::vector<Ptr<VirtualInterferer>> m_interferers;
    std::vector<WifiReceiverInfo> m_receivers;
    bool m_running;

    // Statistics
    uint64_t m_updateCount;
    uint64_t m_injectionCount;

    // DFS trace callback: nodeId, channel, dfsChannel (from radar)
    TracedCallback<uint32_t, uint8_t, uint8_t> m_dfsTriggerTrace;
};

} // namespace ns3

#endif /* VIRTUAL_INTERFERER_ENVIRONMENT_H */
