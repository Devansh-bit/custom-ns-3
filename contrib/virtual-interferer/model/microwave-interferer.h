/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Microwave Interferer - Simulates microwave oven interference
 */

#ifndef MICROWAVE_INTERFERER_H
#define MICROWAVE_INTERFERER_H

#include "virtual-interferer.h"

namespace ns3
{

/**
 * \brief Microwave oven interferer
 *
 * Simulates the broadband interference from a microwave oven.
 * Characteristics:
 * - Center frequency: 2.45 GHz (ISM band center)
 * - Bandwidth: 50-100 MHz (broadband noise)
 * - Duty cycle: ~50% (follows AC power cycle)
 * - Affects ALL 2.4 GHz WiFi channels
 *
 * Realistic parameters based on FCC measurements:
 * - Leakage power: -30 to -10 dBm at 1m distance
 * - High channel utilization impact (30-70%)
 * - Significant packet loss (10-40%)
 */
class MicrowaveInterferer : public VirtualInterferer
{
public:
    /**
     * \brief Power level presets
     */
    enum PowerLevel
    {
        LOW = 0,      ///< Low power microwave (~700W)
        MEDIUM = 1,   ///< Medium power (~900W)
        HIGH = 2      ///< High power (~1200W)
    };

    /**
     * \brief Get the type ID
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    MicrowaveInterferer();
    virtual ~MicrowaveInterferer();

    // ==================== MICROWAVE-SPECIFIC CONFIGURATION ====================

    /**
     * \brief Set power level preset
     * \param level Power level (LOW, MEDIUM, HIGH)
     */
    void SetPowerLevel(PowerLevel level);

    /**
     * \brief Get power level preset
     * \return Power level
     */
    PowerLevel GetPowerLevel() const;

    /**
     * \brief Set AC frequency (affects duty cycle pattern)
     * \param freqHz AC frequency (50 or 60 Hz)
     */
    void SetAcFrequency(double freqHz);

    /**
     * \brief Get AC frequency
     * \return AC frequency in Hz
     */
    double GetAcFrequency() const;

    /**
     * \brief Set door leakage factor
     * \param factor Leakage factor (0-1, higher = more leakage)
     */
    void SetDoorLeakage(double factor);

    /**
     * \brief Get door leakage factor
     * \return Leakage factor
     */
    double GetDoorLeakage() const;

    // ==================== VIRTUALS FROM BASE CLASS ====================

    std::string GetInterfererType() const override;
    std::set<uint8_t> GetAffectedChannels() const override;
    double GetBandwidthMhz() const override;
    double GetCenterFrequencyMhz() const override;

    InterferenceEffect CalculateEffect(
        const Vector& receiverPos,
        uint8_t receiverChannel,
        double distanceM,
        double rxPowerDbm) const override;

protected:
    void DoInitialize() override;
    void DoUpdate(Time now) override;

private:
    /**
     * \brief Update TX power based on power level and leakage
     */
    void UpdateTxPower();

    // Microwave-specific state
    PowerLevel m_powerLevel;
    double m_acFrequencyHz;
    double m_doorLeakage;
    double m_bandwidthMhz;

    // Realistic parameter ranges (from measurements)
    static constexpr double TX_POWER_LOW_DBM = -35.0;
    static constexpr double TX_POWER_MEDIUM_DBM = -25.0;
    static constexpr double TX_POWER_HIGH_DBM = -15.0;

    static constexpr double CENTER_FREQ_MHZ = 2450.0;
    static constexpr double DEFAULT_BANDWIDTH_MHZ = 80.0;

    static constexpr double BASE_UTIL_MIN_PERCENT = 30.0;
    static constexpr double BASE_UTIL_MAX_PERCENT = 70.0;

    static constexpr double PACKET_LOSS_MIN = 0.10;
    static constexpr double PACKET_LOSS_MAX = 0.40;

    // CCA threshold for effect calculation (WiFi 2.4 GHz standard)
    static constexpr double CCA_THRESHOLD_DBM = -82.0;
};

} // namespace ns3

#endif /* MICROWAVE_INTERFERER_H */
