#ifndef STA_CHANNEL_HOPPING_MANAGER_H
#define STA_CHANNEL_HOPPING_MANAGER_H

#include "ns3/object.h"
#include "ns3/wifi-net-device.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/mac48-address.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include "ns3/dual-phy-sniffer-helper.h"
#include "ns3/traced-callback.h"
#include "ns3/net-device-container.h"

namespace ns3
{

/**
 * \ingroup sta-channel-hopping
 * \brief Manager class that monitors STA disassociation and automatically reconnects to best SNR AP
 *
 * This class monitors a STA device for disassociation events. When disassociation occurs,
 * it queries a DualPhySnifferHelper's beacon cache to find all available APs across
 * multiple channels, selects the AP with the best SNR, and initiates roaming after
 * a configurable delay.
 */
class StaChannelHoppingManager : public Object
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * \brief Constructor
     */
    StaChannelHoppingManager();

    /**
     * \brief Destructor
     */
    virtual ~StaChannelHoppingManager();

    /**
     * \brief Set the STA device to monitor
     * \param staDevice Pointer to the STA WiFi device
     */
    void SetStaDevice(Ptr<WifiNetDevice> staDevice);

    /**
     * \brief Set the DualPhySniffer to query for available APs
     * \param sniffer Pointer to DualPhySnifferHelper (not owned)
     * \param operatingMac MAC address used for beacon cache queries
     */
    void SetDualPhySniffer(DualPhySnifferHelper* sniffer, Mac48Address operatingMac);

    /**
     * \brief Set the delay before attempting reconnection after disassociation
     * \param delay Time delay (default 5 seconds)
     */
    void SetScanningDelay(Time delay);

    /**
     * \brief Set minimum SNR threshold for AP selection
     * \param minSnr Minimum SNR in dB (default 0.0)
     */
    void SetMinimumSnr(double minSnr);

    /**
     * \brief Set the AP device container for connection recovery
     * \param apDevices Pointer to NetDeviceContainer containing all AP devices
     */
    void SetApDevices(NetDeviceContainer* apDevices);

    /**
     * \brief Enable or disable automatic reconnection
     * \param enable True to enable automatic reconnection
     */
    void Enable(bool enable);

    /**
     * \brief Check if automatic reconnection is enabled
     * \return True if enabled
     */
    bool IsEnabled() const;

    /**
     * \brief Validate connection state to detect orphaned STAs or high packet loss
     * Called externally (e.g., from metrics collection) instead of periodic timer
     * \param packetLoss Current packet loss ratio (0.0 to 1.0)
     */
    void PerformHealthCheck(double packetLoss = 0.0);

    /**
     * \brief TracedCallback signature for roaming events
     * \param time Current simulation time
     * \param staAddress STA MAC address
     * \param oldBssid Previous AP BSSID (may be null)
     * \param newBssid Target AP BSSID
     * \param snr SNR of target AP in dB
     */
    typedef void (*RoamingTriggeredCallback)(Time time,
                                              Mac48Address staAddress,
                                              Mac48Address oldBssid,
                                              Mac48Address newBssid,
                                              double snr);

    /**
     * \brief Trace source for roaming events
     */
    TracedCallback<Time, Mac48Address, Mac48Address, Mac48Address, double>
        m_roamingTriggeredTrace;

  private:
    /**
     * \brief Callback for disassociation events
     * \param bssid BSSID of the AP we disassociated from
     */
    void OnDisassociation(Mac48Address bssid);

    /**
     * \brief Callback for association events
     * \param bssid BSSID of the AP we associated with
     */
    void OnAssociation(Mac48Address bssid);

    /**
     * \brief Select the best AP from beacon cache
     * \return BSSID of best AP, or null MAC if none found
     */
    Mac48Address SelectBestAp();

    /**
     * \brief Initiate roaming to the best available AP
     */
    void InitiateRoaming();

    /**
     * \brief Get current AP BSSID
     * \return Current BSSID or null MAC if not associated
     */
    Mac48Address GetCurrentBssid() const;

    /**
     * \brief Recover connection for orphaned STAs
     * Bypasses beacon cache and connects to best available AP (excluding current)
     */
    void EmergencyReconnect();

    /**
     * \brief Complete connection recovery after deassociation frame transmission delay
     * \param targetBssid BSSID of target AP
     * \param targetChannel Channel number of target AP
     * \param targetBand WiFi band of target AP
     */
    void CompleteEmergencyReconnect(Mac48Address targetBssid,
                          uint8_t targetChannel,
                          WifiPhyBand targetBand);

    // Configuration
    Ptr<WifiNetDevice> m_staDevice;           ///< The STA device
    DualPhySnifferHelper* m_dualPhySniffer;   ///< Pointer to sniffer helper
    Mac48Address m_dualPhyOperatingMac;       ///< MAC used for sniffer queries
    NetDeviceContainer* m_apDevices;          ///< Pointer to AP device container for connection recovery
    
    Time m_scanningDelay;                     ///< Delay before roaming (default 5s)
    double m_minimumSnr;                      ///< Minimum SNR threshold
    bool m_enabled;                           ///< Whether auto-reconnect is enabled
    
    // State
    Mac48Address m_lastBssid;                 ///< Last associated BSSID
    EventId m_roamingEvent;                   ///< Scheduled roaming event
    Time m_lastEmergencyReconnect;            ///< Time of last recovery attempt (for cooldown)
    Time m_lastDisassociation;                ///< Time of last disassociation (for grace period)

    // Recovery configuration
    Time m_emergencyReconnectCooldown;        ///< Cooldown between recovery attempts (default 5s)

    // Packet loss tracking
    uint32_t m_consecutiveHighLossCount;      ///< Counter for consecutive high packet loss events
    uint32_t m_maxConsecutiveHighLoss;        ///< Threshold for consecutive high loss before triggering reconnect
};

} // namespace ns3

#endif // STA_CHANNEL_HOPPING_MANAGER_H
