/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Virtual Interferer - Base class for simulating non-WiFi interference
 * without using SpectrumWifiPhy for performance optimization.
 */

#ifndef VIRTUAL_INTERFERER_H
#define VIRTUAL_INTERFERER_H

#include "ns3/object.h"
#include "ns3/vector.h"
#include "ns3/nstime.h"
#include "ns3/mobility-model.h"
#include "ns3/random-variable-stream.h"
#include "ns3/event-id.h"
#include "ns3/traced-callback.h"

#include <set>
#include <string>
#include <functional>

namespace ns3
{

/**
 * \brief Structure containing the calculated interference effect
 */
struct InterferenceEffect
{
    double nonWifiCcaPercent = 0.0;      ///< Contribution to non-WiFi CCA (0-100%)
    double packetLossProbability = 0.0;  ///< Additional packet drop probability (0-1)
    double signalPowerDbm = -200.0;      ///< Received signal power at receiver (dBm)
    bool triggersDfs = false;            ///< Should trigger DFS channel switch?

    /**
     * \brief Combine two effects (aggregate)
     */
    InterferenceEffect& operator+=(const InterferenceEffect& other);
};

/**
 * \brief Base class for all virtual interferers
 *
 * Virtual interferers simulate the EFFECTS of non-WiFi devices
 * without doing actual spectrum calculations. Once installed,
 * they automatically affect nearby WiFi devices.
 *
 * Usage:
 *   auto intf = CreateObject<MicrowaveInterferer>();
 *   intf->SetPosition(Vector(5, 5, 1));
 *   intf->Install();  // Registers with environment, starts affecting WiFi
 */
class VirtualInterferer : public Object
{
public:
    /**
     * \brief Get the type ID
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    VirtualInterferer();
    virtual ~VirtualInterferer();

    // ==================== INSTALLATION ====================

    /**
     * \brief Install this interferer (register with environment)
     *
     * Once installed, the interferer will automatically affect
     * nearby WiFi devices based on its characteristics.
     */
    void Install();

    /**
     * \brief Uninstall this interferer (remove from environment)
     */
    void Uninstall();

    /**
     * \brief Check if this interferer is installed
     * \return true if installed
     */
    bool IsInstalled() const;

    // ==================== POSITION ====================

    /**
     * \brief Set the position of this interferer
     * \param position 3D position vector (meters)
     */
    void SetPosition(const Vector& position);

    /**
     * \brief Get the current position
     * \return 3D position vector
     */
    Vector GetPosition() const;

    /**
     * \brief Set mobility model for moving interferers
     * \param mobility Pointer to mobility model
     */
    void SetMobilityModel(Ptr<MobilityModel> mobility);

    /**
     * \brief Get the mobility model
     * \return Pointer to mobility model (may be null)
     */
    Ptr<MobilityModel> GetMobilityModel() const;

    // ==================== STATE CONTROL ====================

    /**
     * \brief Turn the interferer on
     */
    void TurnOn();

    /**
     * \brief Turn the interferer off
     */
    void TurnOff();

    /**
     * \brief Check if interferer is currently active
     * \return true if active
     */
    bool IsActive() const;

    /**
     * \brief Set a periodic on/off schedule
     * \param onDuration How long to stay on
     * \param offDuration How long to stay off
     */
    void SetSchedule(Time onDuration, Time offDuration);

    /**
     * \brief Clear any scheduled on/off pattern
     */
    void ClearSchedule();

    /**
     * \brief Update internal state (called by environment)
     * \param now Current simulation time
     */
    void Update(Time now);

    // ==================== CAPABILITY QUERIES (Override in subclasses) ====================

    /**
     * \brief Get the type name of this interferer
     * \return Type string (e.g., "microwave", "bluetooth")
     */
    virtual std::string GetInterfererType() const = 0;

    /**
     * \brief Get WiFi channels affected by this interferer
     * \return Set of channel numbers
     */
    virtual std::set<uint8_t> GetAffectedChannels() const = 0;

    /**
     * \brief Get the signal bandwidth
     * \return Bandwidth in MHz
     */
    virtual double GetBandwidthMhz() const = 0;

    /**
     * \brief Get the center frequency
     * \return Center frequency in MHz
     */
    virtual double GetCenterFrequencyMhz() const = 0;

    // ==================== EFFECT CALCULATION (Override in subclasses) ====================

    /**
     * \brief Calculate interference effect at a receiver
     * \param receiverPos Position of the WiFi receiver
     * \param receiverChannel WiFi channel number
     * \param distanceM Distance to receiver (meters)
     * \param rxPowerDbm Received power at receiver (dBm)
     * \return Calculated interference effect
     */
    virtual InterferenceEffect CalculateEffect(
        const Vector& receiverPos,
        uint8_t receiverChannel,
        double distanceM,
        double rxPowerDbm) const = 0;

    // ==================== PARAMETER ACCESS ====================

    /**
     * \brief Get transmit power
     * \return TX power in dBm
     */
    double GetTxPowerDbm() const;

    /**
     * \brief Set transmit power
     * \param txPowerDbm TX power in dBm
     */
    void SetTxPowerDbm(double txPowerDbm);

    /**
     * \brief Get duty cycle
     * \return Duty cycle (0-1)
     */
    double GetDutyCycle() const;

    /**
     * \brief Set duty cycle
     * \param dutyCycle Duty cycle (0-1)
     */
    void SetDutyCycle(double dutyCycle);

    /**
     * \brief Get duty cycle period
     * \return Duty period in seconds (e.g., 0.01667 for 60Hz)
     */
    double GetDutyPeriod() const;

    /**
     * \brief Set duty cycle period
     * \param dutyPeriod Period in seconds
     */
    void SetDutyPeriod(double dutyPeriod);

    /**
     * \brief Get duty cycle ratio
     * \return Duty ratio (0-1, fraction of period that device is ON)
     */
    double GetDutyRatio() const;

    /**
     * \brief Set duty cycle ratio
     * \param dutyRatio Ratio (0-1)
     */
    void SetDutyRatio(double dutyRatio);

    /**
     * \brief Get unique ID
     * \return Interferer ID
     */
    uint32_t GetId() const;

    /**
     * \brief Calculate received power at a distance
     * \param distanceM Distance in meters
     * \return Received power in dBm
     */
    double CalculateRxPower(double distanceM) const;

    /**
     * \brief Check if this interferer's frequency overlaps a WiFi channel
     * \param wifiChannel WiFi channel number
     * \return true if overlap exists
     * Note: Virtual to allow derived classes (e.g., RadarInterferer) to use span-based detection
     */
    virtual bool ChannelOverlaps(uint8_t wifiChannel) const;

    /**
     * \brief Get channel overlap factor (0-1)
     * \param wifiChannel WiFi channel number
     * \return Overlap factor (0 = no overlap, 1 = full overlap)
     */
    double GetChannelOverlapFactor(uint8_t wifiChannel) const;

protected:
    // Override from Object
    virtual void DoInitialize() override;
    virtual void DoDispose() override;

    /**
     * \brief Called periodically to update internal state
     * Subclasses can override for time-varying behavior
     * \param now Current simulation time
     */
    virtual void DoUpdate(Time now);

    // ==================== HELPER METHODS FOR SUBCLASSES ====================

    /**
     * \brief Calculate path loss using configured model
     * \param distanceM Distance in meters
     * \return Path loss in dB
     */
    double CalculatePathLoss(double distanceM) const;

    /**
     * \brief Get WiFi channel center frequency
     * \param channel WiFi channel number
     * \return Center frequency in MHz
     */
    static double GetWifiChannelCenterMhz(uint8_t channel);

    /**
     * \brief Get WiFi channel bandwidth
     * \param channel WiFi channel number
     * \return Bandwidth in MHz (typically 20)
     */
    static double GetWifiChannelBandwidthMhz(uint8_t channel);

    // ==================== STATE ====================

    uint32_t m_id;                         ///< Unique ID
    Vector m_position;                     ///< Current position
    Ptr<MobilityModel> m_mobility;         ///< Mobility model (optional)

    double m_txPowerDbm;                   ///< Transmit power (dBm)
    double m_dutyCycle;                    ///< Duty cycle (0-1)
    double m_dutyPeriod;                   ///< Duty cycle period in seconds (e.g., 0.01667 for 60Hz)
    double m_dutyRatio;                    ///< Duty cycle ratio (0-1, fraction of period ON)
    bool m_active;                         ///< Currently active?
    bool m_installed;                      ///< Registered with environment?

    // Path loss parameters
    double m_pathLossExponent;             ///< Path loss exponent (default: 3.0)
    double m_referenceDistanceM;           ///< Reference distance (default: 1.0 m)
    double m_referenceLossDb;              ///< Reference loss (default: 40 dB)

    // Schedule
    bool m_hasSchedule;                    ///< Has on/off schedule?
    Time m_onDuration;                     ///< On duration in schedule
    Time m_offDuration;                    ///< Off duration in schedule
    EventId m_scheduleEvent;               ///< Schedule event

    // Randomization
    Ptr<UniformRandomVariable> m_random;   ///< Random variable for variation

private:
    static uint32_t s_nextId;              ///< Next available ID

    /**
     * \brief Handle schedule toggle
     */
    void DoScheduleToggle();
};

} // namespace ns3

#endif /* VIRTUAL_INTERFERER_H */
