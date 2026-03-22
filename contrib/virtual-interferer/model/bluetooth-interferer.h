/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Bluetooth Interferer - Simulates Bluetooth/BLE interference
 */

#ifndef BLUETOOTH_INTERFERER_H
#define BLUETOOTH_INTERFERER_H

#include "virtual-interferer.h"

namespace ns3
{

/**
 * \brief Bluetooth/BLE interferer
 *
 * Simulates interference from Bluetooth Classic or BLE devices.
 * Characteristics:
 * - Frequency hopping: 79 channels in 2.402-2.480 GHz (std::rand())
 * - Bandwidth: 1 MHz per channel
 * - Hop interval: 625 µs (Bluetooth Classic)
 * - Lower per-channel impact due to hopping
 *
 * Realistic parameters:
 * - TX Power: Class 1 (20 dBm), Class 2 (4 dBm), Class 3 (0 dBm)
 * - Duty cycle: 10-50% depending on profile
 * - Low utilization (2-15%) due to frequency hopping
 * - Low packet loss (1-8%)
 *
 * NOTE: Aligned with SpectrogramGenerationHelper implementation
 */
class BluetoothInterferer : public VirtualInterferer
{
public:
    /**
     * \brief Device class (determines TX power)
     */
    enum DeviceClass
    {
        CLASS_1 = 1,   ///< Class 1: 100 mW (20 dBm), range ~100m
        CLASS_2 = 2,   ///< Class 2: 2.5 mW (4 dBm), range ~10m
        CLASS_3 = 3    ///< Class 3: 1 mW (0 dBm), range ~1m
    };

    /**
     * \brief Usage profile (affects duty cycle)
     */
    enum Profile
    {
        AUDIO_STREAMING = 0,   ///< Audio (A2DP): ~40-50% duty
        DATA_TRANSFER = 1,     ///< File transfer: ~30-40% duty
        HID = 2,               ///< Mouse/keyboard: ~5-15% duty
        IDLE = 3               ///< Connected but idle: ~1-5% duty
    };

    /**
     * \brief Get the type ID
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    BluetoothInterferer();
    virtual ~BluetoothInterferer();

    // ==================== BLUETOOTH-SPECIFIC CONFIGURATION ====================

    /**
     * \brief Set device class
     * \param deviceClass Class 1, 2, or 3
     */
    void SetDeviceClass(DeviceClass deviceClass);

    /**
     * \brief Get device class
     * \return Device class
     */
    DeviceClass GetDeviceClass() const;

    /**
     * \brief Set usage profile
     * \param profile Usage profile
     */
    void SetProfile(Profile profile);

    /**
     * \brief Get usage profile
     * \return Profile
     */
    Profile GetProfile() const;

    /**
     * \brief Enable/disable frequency hopping simulation
     * \param enable True to enable hopping
     */
    void SetHoppingEnabled(bool enable);

    /**
     * \brief Check if hopping is enabled
     * \return True if enabled
     */
    bool IsHoppingEnabled() const;

    /**
     * \brief Get current hop channel (for debugging)
     * \return Current channel (0-78)
     */
    uint8_t GetCurrentHopChannel() const;

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
     * \brief Update TX power based on device class
     */
    void UpdateTxPower();

    /**
     * \brief Update duty cycle based on profile
     */
    void UpdateDutyCycle();

    /**
     * \brief Perform frequency hop
     */
    void DoHop();

    /**
     * \brief Get WiFi channels that overlap with current BT channel
     * \return Set of overlapping WiFi channels
     */
    std::set<uint8_t> GetOverlappingWifiChannels() const;

    // Bluetooth-specific state
    DeviceClass m_deviceClass;
    Profile m_profile;
    bool m_hoppingEnabled;
    uint8_t m_currentHopChannel;  // 0-78

    // Hopping timing
    Time m_lastHopTime;
    EventId m_hopEvent;

    // Realistic parameter ranges
    static constexpr double TX_POWER_CLASS1_DBM = 20.0;
    static constexpr double TX_POWER_CLASS2_DBM = 4.0;
    static constexpr double TX_POWER_CLASS3_DBM = 0.0;

    static constexpr double BT_START_FREQ_MHZ = 2402.0;
    static constexpr double BT_CHANNEL_STEP_MHZ = 1.0;
    static constexpr double BT_CHANNEL_BW_MHZ = 1.0;
    static constexpr int BT_NUM_CHANNELS = 79;

    static constexpr double HOP_INTERVAL_US = 625.0;

    static constexpr double DUTY_AUDIO = 0.45;
    static constexpr double DUTY_DATA = 0.35;
    static constexpr double DUTY_HID = 0.10;
    static constexpr double DUTY_IDLE = 0.03;

    static constexpr double BASE_UTIL_MIN_PERCENT = 2.0;
    static constexpr double BASE_UTIL_MAX_PERCENT = 15.0;

    static constexpr double PACKET_LOSS_MIN = 0.01;
    static constexpr double PACKET_LOSS_MAX = 0.08;

    // CCA threshold for effect calculation (WiFi 2.4 GHz standard)
    static constexpr double CCA_THRESHOLD_DBM = -82.0;
};

} // namespace ns3

#endif /* BLUETOOTH_INTERFERER_H */
