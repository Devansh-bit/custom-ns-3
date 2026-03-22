#ifndef LEVER_API_H
#define LEVER_API_H

#include "ns3/application.h"
#include "ns3/traced-callback.h"
#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include "ns3/wifi-phy-band.h"

namespace ns3
{

class WifiNetDevice;

/**
 * @defgroup lever-api Lever API for dynamic WiFi PHY configuration
 */

/**
 * @brief WiFi frequency band enumeration
 */
enum WifiPhyBandType
{
    BAND_2_4GHZ = 0,
    BAND_5GHZ = 1,
    BAND_6GHZ = 2
};

/**
 * @ingroup lever-api
 * @brief Configuration structure with traced parameters for WiFi PHY
 *
 * This class holds configuration parameters that can be changed at runtime.
 * Each parameter has a TracedCallback that fires when its value is changed.
 */
class LeverConfig : public Object
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    LeverConfig();
    ~LeverConfig() override;

    // PHY Power Configuration
    /**
     * @brief Set both transmit power start and end values together
     * @param txPowerDbm Transmit power in dBm
     *
     * This method sets both TxPowerStart and TxPowerEnd to the same value
     * before firing trace callbacks, avoiding intermediate states.
     */
    void SetTxPower(double txPowerDbm);

    /**
     * @brief Set the CCA energy detection threshold
     * @param thresholdDbm CCA threshold in dBm
     */
    void SetCcaEdThreshold(double thresholdDbm);

    /**
     * @brief Set the receive sensitivity
     * @param sensitivityDbm Receive sensitivity in dBm
     */
    void SetRxSensitivity(double sensitivityDbm);

    // Channel Configuration
    /**
     * @brief Smart channel switching that automatically determines band, width, and primary20 from channel number
     * @param channelNumber Channel number following IEEE 802.11 standard
     *
     * For 2.4 GHz (channels 1-14): Always uses 20 MHz width
     * For 5 GHz: Channel number encodes the center frequency and width:
     *   - 20 MHz channels: 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, etc.
     *   - 40 MHz channels: 38, 46, 54, 62, 102, 110, 118, 126, 134, 142, 151, 159
     *   - 80 MHz channels: 42, 58, 106, 122, 138, 155
     *   - 160 MHz channels: 50, 114
     *
     * Examples:
     *   - SwitchChannel(36): 5GHz channel 36, 20 MHz
     *   - SwitchChannel(38): 5GHz channel 38, 40 MHz (bonds channels 36+40)
     *   - SwitchChannel(42): 5GHz channel 42, 80 MHz (bonds channels 36+40+44+48)
     *   - SwitchChannel(50): 5GHz channel 50, 160 MHz (bonds channels 36-64)
     */
    void SwitchChannel(uint16_t channelNumber);

    // Getters for PHY parameters
    /**
     * @brief Get the current transmit power start value
     * @return Transmit power in dBm
     */
    double GetTxPowerStart() const;

    /**
     * @brief Get the current transmit power end value
     * @return Transmit power in dBm
     */
    double GetTxPowerEnd() const;

    /**
     * @brief Get the current CCA threshold
     * @return CCA threshold in dBm
     */
    double GetCcaEdThreshold() const;

    /**
     * @brief Get the current receive sensitivity
     * @return Receive sensitivity in dBm
     */
    double GetRxSensitivity() const;

    // Getters for Channel parameters
    /**
     * @brief Get the current channel number
     * @return Channel number
     */
    uint8_t GetChannelNumber() const;

    /**
     * @brief Get the current channel width
     * @return Channel width in MHz
     */
    uint16_t GetChannelWidth() const;

    /**
     * @brief Get the current frequency band
     * @return WiFi frequency band
     */
    WifiPhyBandType GetBand() const;

    /**
     * @brief Get the current primary20 index
     * @return Primary 20 MHz channel index
     */
    uint8_t GetPrimary20Index() const;

    /**
     * @brief TracedCallback signature for double parameters
     */
    typedef void (*DoubleTracedCallback)(double oldValue, double newValue);

    /**
     * @brief TracedCallback signature for uint8_t parameters
     */
    typedef void (*Uint8TracedCallback)(uint8_t oldValue, uint8_t newValue);

    /**
     * @brief TracedCallback signature for uint16_t parameters
     */
    typedef void (*Uint16TracedCallback)(uint16_t oldValue, uint16_t newValue);

    /**
     * @brief TracedCallback signature for channel settings
     * @param channelNumber Channel number
     * @param widthMhz Channel width in MHz
     * @param band Frequency band
     * @param primary20Index Primary 20 MHz channel index
     */
    typedef void (*ChannelSettingsTracedCallback)(uint8_t channelNumber, uint16_t widthMhz, WifiPhyBandType band, uint8_t primary20Index);

  private:
    // PHY parameters
    double m_txPowerStart;      //!< Transmit power start in dBm
    double m_txPowerEnd;        //!< Transmit power end in dBm
    double m_ccaEdThreshold;    //!< CCA energy detection threshold in dBm
    double m_rxSensitivity;     //!< Receive sensitivity in dBm

    // Channel parameters
    uint8_t m_channelNumber;     //!< Channel number
    uint16_t m_channelWidth;     //!< Channel width in MHz
    WifiPhyBandType m_band;      //!< Frequency band
    uint8_t m_primary20Index;    //!< Primary 20 MHz channel index

    // PHY traces
    TracedCallback<double, double> m_txPowerStartTrace;   //!< Trace for TxPowerStart changes
    TracedCallback<double, double> m_txPowerEndTrace;     //!< Trace for TxPowerEnd changes
    TracedCallback<double, double> m_ccaEdThresholdTrace; //!< Trace for CcaEdThreshold changes
    TracedCallback<double, double> m_rxSensitivityTrace;  //!< Trace for RxSensitivity changes

    // Channel traces
    TracedCallback<uint8_t, uint16_t, WifiPhyBandType, uint8_t> m_channelSettingsTrace; //!< Trace for channel settings changes
};

/**
 * @ingroup lever-api
 * @brief Application that listens to LeverConfig traces and applies changes to WiFi PHY
 *
 * This application should be installed on WiFi nodes. It connects to a LeverConfig
 * object and listens to its traced parameters. When a parameter changes, it automatically
 * applies the new configuration to the node's WiFi PHY interface.
 */
class LeverApi : public Application
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    LeverApi();
    ~LeverApi() override;

    /**
     * @brief Set the configuration object to listen to
     * @param config Pointer to the LeverConfig object
     */
    void SetConfig(Ptr<LeverConfig> config);

    /**
     * @brief Smart channel switching that automatically determines band, width, and primary20 from channel number
     * @param channelNumber Channel number following IEEE 802.11 standard
     * @param linkId the ID of the link (default: 0)
     *
     * This method delegates to the underlying WifiMac::SwitchChannel() method.
     * For APs, this will automatically propagate channel changes to all connected STAs.
     *
     * For 2.4 GHz (channels 1-14): Always uses 20 MHz width
     * For 5 GHz: Channel number encodes the center frequency and width:
     *   - 20 MHz channels: 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, etc.
     *   - 40 MHz channels: 38, 46, 54, 62, 102, 110, 118, 126, 134, 142, 151, 159
     *   - 80 MHz channels: 42, 58, 106, 122, 138, 155
     *   - 160 MHz channels: 50, 114
     */
    void SwitchChannel(uint16_t channelNumber, uint8_t linkId = 0);

    // Getters for current PHY state
    /**
     * @brief Get the current channel number
     * @return Channel number
     */
    uint8_t GetChannelNumber() const;

    /**
     * @brief Get the current channel width
     * @return Channel width in MHz
     */
    uint16_t GetChannelWidth() const;

    /**
     * @brief Get the current frequency band
     * @return WiFi frequency band
     */
    WifiPhyBand GetBand() const;

    /**
     * @brief Get the current primary20 index
     * @return Primary 20 MHz channel index
     */
    uint8_t GetPrimary20Index() const;

    /**
     * @brief Get the current transmit power start value
     * @return Transmit power in dBm
     */
    double GetTxPowerStart() const;

    /**
     * @brief Get the current transmit power end value
     * @return Transmit power in dBm
     */
    double GetTxPowerEnd() const;

    /**
     * @brief Get the current CCA threshold
     * @return CCA threshold in dBm
     */
    double GetCcaEdThreshold() const;

    /**
     * @brief Get the current receive sensitivity
     * @return Receive sensitivity in dBm
     */
    double GetRxSensitivity() const;

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    // PHY parameter callbacks
    /**
     * @brief Callback when TxPowerStart changes
     * @param oldValue Previous value in dBm
     * @param newValue New value in dBm
     */
    void OnTxPowerStartChanged(double oldValue, double newValue);

    /**
     * @brief Callback when TxPowerEnd changes
     * @param oldValue Previous value in dBm
     * @param newValue New value in dBm
     */
    void OnTxPowerEndChanged(double oldValue, double newValue);

    /**
     * @brief Apply TX power to the PHY (called when Start and End match)
     * @param powerDbm Power value in dBm
     */
    void ApplyTxPowerToPhy(double powerDbm);

    /**
     * @brief Callback when CcaEdThreshold changes
     * @param oldValue Previous value in dBm
     * @param newValue New value in dBm
     */
    void OnCcaEdThresholdChanged(double oldValue, double newValue);

    /**
     * @brief Callback when RxSensitivity changes
     * @param oldValue Previous value in dBm
     * @param newValue New value in dBm
     */
    void OnRxSensitivityChanged(double oldValue, double newValue);

    // Channel configuration callback
    /**
     * @brief Callback when channel settings change
     * @param channelNumber Channel number
     * @param widthMhz Channel width in MHz
     * @param band Frequency band
     * @param primary20Index Primary 20 MHz channel index
     */
    void OnChannelSettingsChanged(uint8_t channelNumber, uint16_t widthMhz, WifiPhyBandType band, uint8_t primary20Index);

    /**
     * @brief Apply initial configuration from LeverConfig to the WiFi PHY
     * 
     * This method is called when the application starts to ensure that
     * initial values from LeverConfig are applied to the WiFi PHY,
     * since traces only fire on changes, not on initial values.
     */
    void ApplyInitialConfiguration();

    /**
     * @brief Get the WiFi net device from the node
     * @return Pointer to WifiNetDevice or nullptr if not found
     */
    Ptr<WifiNetDevice> GetWifiNetDevice() const;

    Ptr<LeverConfig> m_config; //!< Configuration object to monitor
};

} // namespace ns3

#endif // LEVER_API_H