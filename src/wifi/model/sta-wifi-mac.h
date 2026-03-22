/*
 * Copyright (c) 2006, 2009 INRIA
 * Copyright (c) 2009 MIRKO BANCHI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 *          Mirko Banchi <mk.banchi@gmail.com>
 */

#ifndef STA_WIFI_MAC_H
#define STA_WIFI_MAC_H

#include "wifi-mac.h"

#include "ns3/eht-configuration.h"

#include <set>
#include <variant>

class AmpduAggregationTest;
class MultiLinkOperationsTestBase;
class ProbeExchTest;

namespace ns3
{

class SupportedRates;
class CapabilityInformation;
class RandomVariableStream;
class WifiAssocManager;
class EmlsrManager;

/**
 * @ingroup wifi
 *
 * Type of association performed by this device (provided that it is supported by the standard
 * configured for this device).
 */
enum class WifiAssocType : uint8_t
{
    LEGACY = 0,
    ML_SETUP
};

/**
 * @ingroup wifi
 *
 * Scan type (active or passive)
 */
enum class WifiScanType : uint8_t
{
    ACTIVE = 0,
    PASSIVE
};

/**
 * @ingroup wifi
 *
 * Structure holding scan parameters
 */
struct WifiScanParams
{
    /**
     * Struct identifying a channel to scan.
     * A channel number equal to zero indicates to scan all the channels;
     * an unspecified band (WIFI_PHY_BAND_UNSPECIFIED) indicates to scan
     * all the supported PHY bands.
     */
    struct Channel
    {
        uint16_t number{0};                          ///< channel number
        WifiPhyBand band{WIFI_PHY_BAND_UNSPECIFIED}; ///< PHY band
    };

    /// typedef for a list of channels
    using ChannelList = std::list<Channel>;

    WifiScanType type;                    ///< indicates either active or passive scanning
    Ssid ssid;                            ///< desired SSID or wildcard SSID
    std::vector<ChannelList> channelList; ///< list of channels to scan, for each link
    Time probeDelay;                      ///< delay prior to transmitting a Probe Request
    Time minChannelTime;                  ///< minimum time to spend on each channel
    Time maxChannelTime;                  ///< maximum time to spend on each channel
};

/**
 * @ingroup wifi
 *
 * Enumeration for power management modes
 */
enum WifiPowerManagementMode : uint8_t
{
    WIFI_PM_ACTIVE = 0,
    WIFI_PM_SWITCHING_TO_PS,
    WIFI_PM_POWERSAVE,
    WIFI_PM_SWITCHING_TO_ACTIVE
};

/**
 * @ingroup wifi
 *
 * The Wifi MAC high model for a non-AP STA in a BSS. The state
 * machine is as follows:
 *
   \verbatim
   ┌───────────┐            ┌────────────────┐                           ┌─────────────┐
   │   Start   │      ┌─────┤   Associated   ◄───────────────────┐    ┌──►   Refused   │
   └─┬─────────┘      │     └────────────────┘                   │    │  └─────────────┘
     │                │                                          │    │
     │                │ ┌─────────────────────────────────────┐  │    │
     │                │ │                                     │  │    │
     │  ┌─────────────▼─▼──┐       ┌──────────────┐       ┌───┴──▼────┴───────────────────┐
     └──►   Unassociated   ├───────►   Scanning   ├───────►   Wait Association Response   │
        └──────────────────┘       └──────┬──▲────┘       └───────────────┬──▲────────────┘
                                          │  │                            │  │
                                          │  │                            │  │
                                          └──┘                            └──┘
   \endverbatim
 *
 * Notes:
 * 1. The state 'Start' is not included in #MacState and only used
 *    for illustration purpose.
 * 2. The Unassociated state is a transient state before STA starts the
 *    scanning procedure which moves it into the Scanning state.
 * 3. In Scanning, STA is gathering beacon or probe response frames from APs,
 *    resulted in a list of candidate AP. After the timeout, it then tries to
 *    associate to the best AP, which is indicated by the Association Manager.
 *    STA will restart the scanning procedure if SetActiveProbing() called.
 * 4. In the case when AP responded to STA's association request with a
 *    refusal, STA will try to associate to the next best AP until the list
 *    of candidate AP is exhausted which sends STA to Refused state.
 *    - Note that this behavior is not currently tested since ns-3 does not
  *     implement association refusal at present.
 * 5. The transition from Wait Association Response to Unassociated
 *    occurs if an association request fails without explicit
 *    refusal (i.e., the AP fails to respond).
 * 6. The transition from Associated to Wait Association Response
 *    occurs when STA's PHY capabilities changed. In this state, STA
 *    tries to reassociate with the previously associated AP.
 * 7. The transition from Associated to Unassociated occurs if the number
 *    of missed beacons exceeds the threshold.
 */
class StaWifiMac : public WifiMac
{
  public:
    /// Allow test cases to access private members
    friend class ::AmpduAggregationTest;
    friend class ::MultiLinkOperationsTestBase;
    friend class ::ProbeExchTest;
    friend class WifiStaticSetupHelper;

    /**
     * Struct to hold information regarding observed AP through
     * active/passive scanning
     */
    struct ApInfo
    {
        /**
         * Information about links to setup
         */
        struct SetupLinksInfo
        {
            uint8_t localLinkId; ///< local link ID
            uint8_t apLinkId;    ///< AP link ID
            Mac48Address bssid;  ///< BSSID
        };

        Mac48Address m_bssid;  ///< BSSID
        Mac48Address m_apAddr; ///< AP MAC address
        double m_snr;          ///< SNR in linear scale
        MgtFrameType m_frame;  ///< The body of the management frame used to update AP info
        WifiScanParams::Channel m_channel; ///< The channel the management frame was received on
        uint8_t m_linkId;                  ///< ID of the link used to communicate with the AP
        std::list<SetupLinksInfo>
            m_setupLinks; ///< information about the links to setup between MLDs
    };

    /**
     * Struct to hold scan result information for neighbor APs
     */
    struct ScanResult
    {
        Mac48Address bssid;            ///< BSSID of the AP
        Ssid ssid;                     ///< SSID of the network
        double rssi;                   ///< RSSI in dBm
        double snr;                    ///< SNR in linear scale
        uint8_t channel;               ///< Channel number
        WifiPhyBand band;              ///< Frequency band
        Time timestamp;                ///< Time when this result was obtained
        uint32_t beaconInterval;       ///< Beacon interval in TUs
    };

    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    StaWifiMac();
    ~StaWifiMac() override;

    bool CanForwardPacketsTo(Mac48Address to) const override;
    int64_t AssignStreams(int64_t stream) override;

    /**
     * @param phys the physical layers attached to this MAC.
     */
    void SetWifiPhys(const std::vector<Ptr<WifiPhy>>& phys) override;

    /**
     * Set the Association Manager.
     *
     * @param assocManager the Association Manager
     */
    void SetAssocManager(Ptr<WifiAssocManager> assocManager);

    /**
     * Set the EMLSR Manager.
     *
     * @param emlsrManager the EMLSR Manager
     */
    void SetEmlsrManager(Ptr<EmlsrManager> emlsrManager);

    /**
     * @return the EMLSR Manager
     */
    Ptr<EmlsrManager> GetEmlsrManager() const;

    /**
     * Get the frame body of the Probe Request to transmit on the given link.
     *
     * @param linkId the ID of the given link
     * @return the Probe Request frame body
     */
    MgtProbeRequestHeader GetProbeRequest(uint8_t linkId) const;

    /**
     * Get the frame body of the Multi-Link Probe Request to transmit on the given link.
     *
     * @param linkId the ID of the given link
     * @param apLinkIds ID of the links on which the requested APs, affiliated with the
     *                  AP MLD, operate
     * @param apMldId the AP MLD ID to include in the Common Info field
     * @return the Multi-Link Probe Request frame body
     */
    MgtProbeRequestHeader GetMultiLinkProbeRequest(uint8_t linkId,
                                                   const std::vector<uint8_t>& apLinkIds,
                                                   std::optional<uint8_t> apMldId) const;

    /**
     * Enqueue the given probe request packet for transmission on the given link.
     *
     * @param probeReq the given Probe Request frame body
     * @param linkId the ID of the given link
     * @param addr1 the MAC address for the Address1 field
     * @param addr3 the MAC address for the Address3 field
     */
    void EnqueueProbeRequest(const MgtProbeRequestHeader& probeReq,
                             uint8_t linkId,
                             const Mac48Address& addr1 = Mac48Address::GetBroadcast(),
                             const Mac48Address& addr3 = Mac48Address::GetBroadcast());

    /**
     * This method is called after wait beacon timeout or wait probe request timeout has
     * occurred. This will trigger association process from beacons or probe responses
     * gathered while scanning.
     *
     * @param bestAp the info about the best AP to associate with, if one was found
     */
    void ScanningTimeout(const std::optional<ApInfo>& bestAp);

    /**
     * Return whether we are associated with an AP.
     *
     * @return true if we are associated with an AP, false otherwise
     */
    bool IsAssociated() const;

    /**
     * Get the IDs of the setup links (if any).
     *
     * @return the IDs of the setup links
     */
    std::set<uint8_t> GetSetupLinkIds() const;

    /**
     * Return the association ID.
     *
     * @return the association ID
     */
    uint16_t GetAssociationId() const;

    /// @return the type of association procedure performed by this device
    WifiAssocType GetAssocType() const;

    /**
     * Enable or disable Power Save mode on the given link.
     *
     * @param enableLinkIdPair a pair indicating whether to enable or not power save mode on
     *                         the link with the given ID
     */
    void SetPowerSaveMode(const std::pair<bool, uint8_t>& enableLinkIdPair);

    /**
     * @param linkId the ID of the given link
     * @return the current Power Management mode of the STA operating on the given link
     */
    WifiPowerManagementMode GetPmMode(uint8_t linkId) const;

    /**
     * Set the Power Management mode of the setup links after association.
     *
     * @param linkId the ID of the link used to establish association
     */
    void SetPmModeAfterAssociation(uint8_t linkId);

    /**
     * Notify that the MPDU we sent was successfully received by the receiver
     * (i.e. we received an Ack from the receiver).
     *
     * @param mpdu the MPDU that we successfully sent
     */
    void TxOk(Ptr<const WifiMpdu> mpdu);

    void NotifyChannelSwitching(uint8_t linkId) override;

    /**
     * Notify the MAC that EMLSR mode has changed on the given set of links.
     *
     * @param linkIds the IDs of the links that are now EMLSR links (EMLSR mode is disabled
     *                on other links)
     */
    void NotifyEmlsrModeChanged(const std::set<uint8_t>& linkIds);

    /**
     * @param linkId the ID of the given link
     * @return whether the EMLSR mode is enabled on the given link
     */
    bool IsEmlsrLink(uint8_t linkId) const;

    /**
     * Notify that the given PHY switched channel to operate on another EMLSR link.
     *
     * @param phy the given PHY
     * @param linkId the ID of the EMLSR link on which the given PHY operates after
     *               the channel switch
     * @param delay the delay after which the channel switch will be completed
     */
    void NotifySwitchingEmlsrLink(Ptr<WifiPhy> phy, uint8_t linkId, Time delay);

    /**
     * Cancel any scheduled event for connecting the given PHY to an EMLSR link.
     *
     * @param phyId the ID of the given PHY
     */
    void CancelEmlsrPhyConnectEvent(uint8_t phyId);

    /**
     * Block transmissions on the given link for the given reason.
     *
     * @param linkId the ID of the given link
     * @param reason the reason for blocking transmissions on the given link
     */
    void BlockTxOnLink(uint8_t linkId, WifiQueueBlockedReason reason);

    /**
     * Unblock transmissions on the given links for the given reason.
     *
     * @param linkIds the IDs of the given links
     * @param reason the reason for unblocking transmissions on the given links
     */
    void UnblockTxOnLink(std::set<uint8_t> linkIds, WifiQueueBlockedReason reason);

    /**
     * @name Manual Roaming Control APIs
     * @{
     */

    /**
     * Manually initiate roaming to a specific AP.
     * This method will trigger reassociation with the target AP.
     *
     * @param targetBssid the BSSID of the target AP
     * @param channel the channel number of the target AP
     * @param band the frequency band of the target AP
     */
    void InitiateRoaming(const Mac48Address& targetBssid, uint8_t channel, WifiPhyBand band);

    /**
     * Associate to a specific AP when currently disassociated.
     * This method can be called when the STA is not associated to trigger
     * association to a specific BSSID on a specific channel/band.
     * If already associated, use InitiateRoaming() instead.
     *
     * @param targetBssid the BSSID to associate with
     * @param channel the channel number of the target AP
     * @param band the frequency band of the target AP
     */
    void AssociateToAp(const Mac48Address& targetBssid, uint8_t channel, WifiPhyBand band);

    /**
     * Force disassociation from the current AP without automatic reconnection.
     * Call EnableAutoReconnect(true) to restart scanning.
     */
    void ForcedDisassociate();

    /**
     * Send a Disassociation frame to the specified AP.
     * Used to notify the AP that the STA is leaving before switching channels or roaming.
     *
     * @param apAddress the MAC address of the AP to disassociate from
     * @param reasonCode the IEEE 802.11 reason code (e.g., 8 = leaving BSS)
     */
    void SendDeassociation(const Mac48Address& apAddress, uint16_t reasonCode);

    /**
     * Enable or disable automatic reconnection after disassociation.
     *
     * @param enable true to enable automatic scanning and reassociation, false to disable
     */
    void EnableAutoReconnect(bool enable);

    /**
     * Get the current RSSI from the associated AP.
     *
     * @return the RSSI in dBm, or 0 if not associated
     */
    double GetCurrentRssi() const;

    /**
     * Get the current SNR from the associated AP.
     *
     * @return the SNR in linear scale, or 0 if not associated
     */
    double GetCurrentSnr() const;

    /**
     * Enable or disable background scanning while associated.
     *
     * @param enable true to enable background scanning, false to disable
     */
    void EnableBackgroundScanning(bool enable);

    /**
     * Trigger a background scan immediately (if associated).
     * This will scan for neighbor APs without disconnecting from the current AP.
     */
    void TriggerBackgroundScan();

    /**
     * Get the cached scan results from background scanning.
     *
     * @return a map of BSSID to ScanResult
     */
    std::map<Mac48Address, ScanResult> GetScanResults() const;

    /**
     * Clear the cached scan results.
     */
    void ClearScanResults();

    /**
     * Get the MAC address of the currently associated AP.
     *
     * @return the BSSID of the current AP, or an empty optional if not associated
     */
    std::optional<Mac48Address> GetCurrentBssid() const;

    /** @} */

  protected:
    /**
     * Structure holding information specific to a single link. Here, the meaning of
     * "link" is that of the 11be amendment which introduced multi-link devices. For
     * previous amendments, only one link can be created.
     */
    struct StaLinkEntity : public WifiMac::LinkEntity
    {
        /// Destructor (a virtual method is needed to make this struct polymorphic)
        ~StaLinkEntity() override;

        bool sendAssocReq;                 //!< whether this link is used to send the
                                           //!< Association Request frame
        std::optional<Mac48Address> bssid; //!< BSSID of the AP to associate with over this link
        WifiPowerManagementMode pmMode{WIFI_PM_ACTIVE}; /**< the current PM mode, if the STA is
                                                             associated, or the PM mode to switch
                                                             to upon association, otherwise */
        bool emlsrEnabled{false}; //!< whether EMLSR mode is enabled on this link
    };

    /**
     * Get a reference to the link associated with the given ID.
     *
     * @param linkId the given link ID
     * @return a reference to the link associated with the given ID
     */
    StaLinkEntity& GetLink(uint8_t linkId) const;

    /**
     * Cast the given LinkEntity object to StaLinkEntity.
     *
     * @param link the given LinkEntity object
     * @return a reference to the object casted to StaLinkEntity
     */
    StaLinkEntity& GetStaLink(const std::unique_ptr<WifiMac::LinkEntity>& link) const;

  public:
    /**
     * The current MAC state of the STA.
     */
    enum MacState
    {
        ASSOCIATED,
        SCANNING,
        WAIT_ASSOC_RESP,
        UNASSOCIATED,
        REFUSED,
        ROAMING  ///< Roaming to a new AP
    };

  private:
    void DoCompleteConfig() override;

    /**
     * Enable or disable active probing.
     *
     * @param enable enable or disable active probing
     */
    void SetActiveProbing(bool enable);
    /**
     * Return whether active probing is enabled.
     *
     * @return true if active probing is enabled, false otherwise
     */
    bool GetActiveProbing() const;

    /**
     * Determine whether the supported rates indicated in a given Beacon frame or
     * Probe Response frame fit with the configured membership selector.
     *
     * @param frame the given Beacon or Probe Response frame
     * @param linkId ID of the link the mgt frame was received over
     * @return whether the the supported rates indicated in the given management
     *         frame fit with the configured membership selector
     */
    bool CheckSupportedRates(std::variant<MgtBeaconHeader, MgtProbeResponseHeader> frame,
                             uint8_t linkId);

    void Receive(Ptr<const WifiMpdu> mpdu, uint8_t linkId) override;
    std::unique_ptr<LinkEntity> CreateLinkEntity() const override;
    Mac48Address DoGetLocalAddress(const Mac48Address& remoteAddr) const override;
    void Enqueue(Ptr<WifiMpdu> mpdu, Mac48Address to, Mac48Address from) override;
    void NotifyDropPacketToEnqueue(Ptr<Packet> packet, Mac48Address to) override;

    /**
     * Process the Beacon frame received on the given link.
     *
     * @param mpdu the MPDU containing the Beacon frame
     * @param linkId the ID of the given link
     */
    void ReceiveBeacon(Ptr<const WifiMpdu> mpdu, uint8_t linkId);

    /**
     * Process the Probe Response frame received on the given link.
     *
     * @param mpdu the MPDU containing the Probe Response frame
     * @param linkId the ID of the given link
     */
    void ReceiveProbeResp(Ptr<const WifiMpdu> mpdu, uint8_t linkId);

    /**
     * Process the (Re)Association Response frame received on the given link.
     *
     * @param mpdu the MPDU containing the (Re)Association Response frame
     * @param linkId the ID of the given link
     */
    void ReceiveAssocResp(Ptr<const WifiMpdu> mpdu, uint8_t linkId);

    /**
     * Update operations information from the given management frame.
     *
     * @param frame the body of the given management frame
     * @param addr MAC address of the sender
     * @param linkId ID of the link the management frame was received over
     */
    void RecordOperations(const MgtFrameType& frame, const Mac48Address& addr, uint8_t linkId);

    /**
     * Update operational settings based on associated AP's information provided by the given
     * management frame (Beacon, Probe Response or Association Response).
     *
     * @param frame the body of the given management frame
     * @param apAddr MAC address of the AP
     * @param bssid MAC address of BSSID
     * @param linkId ID of the link the management frame was received over
     */
    void ApplyOperationalSettings(const MgtFrameType& frame,
                                  const Mac48Address& apAddr,
                                  const Mac48Address& bssid,
                                  uint8_t linkId);

    /**
     * Get the (Re)Association Request frame to send on a given link. The returned frame
     * never includes a Multi-Link Element.
     *
     * @param isReassoc whether a Reassociation Request has to be returned
     * @param linkId the ID of the given link
     * @return the (Re)Association Request frame
     */
    std::variant<MgtAssocRequestHeader, MgtReassocRequestHeader> GetAssociationRequest(
        bool isReassoc,
        uint8_t linkId) const;

    /**
     * Forward an association or reassociation request packet to the DCF.
     * The standard is not clear on the correct queue for management frames if QoS is supported.
     * We always use the DCF.
     *
     * @param isReassoc flag whether it is a reassociation request
     *
     */
    void SendAssociationRequest(bool isReassoc);
    /**
     * Try to ensure that we are associated with an AP by taking an appropriate action
     * depending on the current association status.
     */
    void TryToEnsureAssociated();
    /**
     * This method is called after the association timeout occurred. We switch the state to
     * WAIT_ASSOC_RESP and re-send an association request.
     */
    void AssocRequestTimeout();
    /**
     * Start the scanning process which trigger active or passive scanning based on the
     * active probing flag.
     */
    void StartScanning();
    /**
     * Return whether we are waiting for an association response from an AP.
     *
     * @return true if we are waiting for an association response from an AP, false otherwise
     */
    bool IsWaitAssocResp() const;

    /**
     * This method is called after we have not received a beacon from the AP on any link.
     */
    void MissedBeacons();
    /**
     * Restarts the beacon timer.
     *
     * @param delay the delay before the watchdog fires
     */
    void RestartBeaconWatchdog(Time delay);
    /**
     * Set the state to unassociated and try to associate again.
     */
    void Disassociated();

    /**
     * @name Background Scanning and Roaming Helper Methods
     * @{
     */

    /**
     * Schedule the next background scan.
     */
    void ScheduleBackgroundScan();

    /**
     * Perform a background scan (while associated).
     * This scans neighbor channels without disconnecting from the current AP.
     */
    void PerformBackgroundScan();

    /**
     * Process background scan results and update the neighbor cache.
     *
     * @param apInfo information about the discovered AP
     */
    void ProcessBackgroundScanResult(const ApInfo& apInfo);

    /**
     * Complete the roaming process after reassociation.
     */
    void CompleteRoaming();

    /**
     * Update link quality metrics from received beacons/frames.
     *
     * @param bssid the BSSID of the AP
     * @param rssi the RSSI value in dBm
     * @param snr the SNR value in linear scale
     */
    void UpdateLinkQuality(const Mac48Address& bssid, double rssi, double snr);

    /**
     * Internal implementation of roaming after deassociation is sent.
     * This performs the actual channel switch and reassociation.
     *
     * @param targetBssid the BSSID of the target AP
     * @param channel the channel number of the target AP
     * @param band the frequency band of the target AP
     */
    void DoInitiateRoaming(const Mac48Address& targetBssid, uint8_t channel, WifiPhyBand band);

    /** @} */

    /**
     * Return an instance of SupportedRates that contains all rates that we support
     * including HT rates.
     *
     * @param linkId the ID of the link for which the request is made
     * @return SupportedRates all rates that we support
     */
    AllSupportedRates GetSupportedRates(uint8_t linkId) const;
    /**
     * Return the Basic Multi-Link Element to include in the management frames transmitted
     * on the given link
     *
     * @param isReassoc whether the Basic Multi-Link Element is included in a Reassociation Request
     * @param linkId the ID of the given link
     * @return the Basic Multi-Link Element
     */
    MultiLinkElement GetBasicMultiLinkElement(bool isReassoc, uint8_t linkId) const;

    /**
     * Return the Probe Request Multi-Link Element to include in the management frames to transmit.
     *
     * @param apLinkIds ID of the links on which the requested APs operate
     * @param apMldId the AP MLD ID to include in the Common Info field
     * @return the Probe Request Multi-Link Element
     */
    MultiLinkElement GetProbeReqMultiLinkElement(const std::vector<uint8_t>& apLinkIds,
                                                 std::optional<uint8_t> apMldId) const;

    /**
     * @param apNegSupport the negotiation type supported by the AP MLD
     * @return the TID-to-Link Mapping element(s) to include in Association Request frame.
     */
    std::vector<TidToLinkMapping> GetTidToLinkMappingElements(
        WifiTidToLinkMappingNegSupport apNegSupport);

    /**
     * Set the current MAC state.
     *
     * @param value the new state
     */
    void SetState(MacState value);

    /**
     * EDCA Parameters
     */
    struct EdcaParams
    {
        AcIndex ac;     //!< the access category
        uint32_t cwMin; //!< the minimum contention window size
        uint32_t cwMax; //!< the maximum contention window size
        uint8_t aifsn;  //!< the number of slots that make up an AIFS
        Time txopLimit; //!< the TXOP limit
    };

    /**
     * Set the EDCA parameters for the given link.
     *
     * @param params the EDCA parameters
     * @param linkId the ID of the given link
     */
    void SetEdcaParameters(const EdcaParams& params, uint8_t linkId);

    /**
     * MU EDCA Parameters
     */
    struct MuEdcaParams
    {
        AcIndex ac;       //!< the access category
        uint32_t cwMin;   //!< the minimum contention window size
        uint32_t cwMax;   //!< the maximum contention window size
        uint8_t aifsn;    //!< the number of slots that make up an AIFS
        Time muEdcaTimer; //!< the MU EDCA timer
    };

    /**
     * Set the MU EDCA parameters for the given link.
     *
     * @param params the MU EDCA parameters
     * @param linkId the ID of the given link
     */
    void SetMuEdcaParameters(const MuEdcaParams& params, uint8_t linkId);

    /**
     * Return the Capability information for the given link.
     *
     * @param linkId the ID of the given link
     * @return the Capability information that we support
     */
    CapabilityInformation GetCapabilities(uint8_t linkId) const;

    /**
     * Indicate that PHY capabilities have changed.
     */
    void PhyCapabilitiesChanged();

    /**
     * Get the current primary20 channel used on the given link as a
     * (channel number, PHY band) pair.
     *
     * @param linkId the ID of the given link
     * @return a (channel number, PHY band) pair
     */
    WifiScanParams::Channel GetCurrentChannel(uint8_t linkId) const;

    void DoInitialize() override;
    void DoDispose() override;

    MacState m_state;                             ///< MAC state
    uint16_t m_aid;                               ///< Association AID
    Ptr<WifiAssocManager> m_assocManager;         ///< Association Manager
    WifiAssocType m_assocType;                    ///< type of association
    Ptr<EmlsrManager> m_emlsrManager;             ///< EMLSR Manager
    Time m_waitBeaconTimeout;                     ///< wait beacon timeout
    Time m_probeRequestTimeout;                   ///< probe request timeout
    Time m_assocRequestTimeout;                   ///< association request timeout
    EventId m_assocRequestEvent;                  ///< association request event
    uint32_t m_maxMissedBeacons;                  ///< maximum missed beacons
    EventId m_beaconWatchdog;                     //!< beacon watchdog
    Time m_beaconWatchdogEnd{0};                  //!< beacon watchdog end
    bool m_enableScanning;                        //!< enable channel scanning
    bool m_activeProbing;                         ///< active probing
    Ptr<RandomVariableStream> m_probeDelay;       ///< RandomVariable used to randomize the time
                                                  ///< of the first Probe Response on each channel
    Time m_pmModeSwitchTimeout;                   ///< PM mode switch timeout
    std::map<uint8_t, EventId> m_emlsrLinkSwitch; ///< maps PHY ID to the event scheduled to switch
                                                  ///< the corresponding PHY to a new EMLSR link

    /// store the DL TID-to-Link Mapping included in the Association Request frame
    WifiTidLinkMapping m_dlTidLinkMappingInAssocReq;
    /// store the UL TID-to-Link Mapping included in the Association Request frame
    WifiTidLinkMapping m_ulTidLinkMappingInAssocReq;

    // Roaming and background scanning members
    bool m_enableBackgroundScanning{false};       ///< enable background scanning while associated
    bool m_autoReconnect{true};                   ///< enable automatic reconnection after disassociation
    Time m_backgroundScanInterval{Seconds(5.0)};  ///< interval between background scans
    EventId m_backgroundScanEvent;                ///< scheduled background scan event
    std::map<Mac48Address, ScanResult> m_scanResults; ///< cached scan results (neighbor cache)
    Time m_scanResultMaxAge{Seconds(30.0)};      ///< maximum age of cached scan results

    // Roaming state tracking
    Mac48Address m_roamingTargetBssid;           ///< target BSSID for roaming
    Mac48Address m_previousBssid;                 ///< BSSID before roaming
    Time m_roamingStartTime;                      ///< time when roaming was initiated

    // Link quality tracking
    double m_currentRssi{0.0};                    ///< current RSSI from associated AP (dBm)
    double m_currentSnr{0.0};                     ///< current SNR from associated AP (linear)

    TracedCallback<Mac48Address> m_assocLogger;             ///< association logger
    TracedCallback<uint8_t, Mac48Address> m_setupCompleted; ///< link setup completed logger
    TracedCallback<Mac48Address> m_deAssocLogger;           ///< disassociation logger
    TracedCallback<Time> m_beaconArrival;                   ///< beacon arrival logger
    TracedCallback<ApInfo> m_beaconInfo;                    ///< beacon info logger
    TracedCallback<uint8_t, Ptr<WifiPhy>, bool>
        m_emlsrLinkSwitchLogger; ///< EMLSR link switch logger

    // Roaming and link quality trace sources
    TracedCallback<Time, Mac48Address, Mac48Address, double, double>
        m_roamingInitiatedLogger; ///< roaming initiated logger (time, oldBssid, newBssid, rssi, snr)
    TracedCallback<Time, Mac48Address, Time, bool>
        m_roamingCompletedLogger; ///< roaming completed logger (time, newBssid, latency, success)
    TracedCallback<uint32_t, std::map<Mac48Address, double>>
        m_backgroundScanCompleteLogger; ///< background scan complete logger (apCount, rssiMap)
    TracedCallback<Time, Mac48Address, double, double, uint32_t, double>
        m_linkQualityUpdateLogger; ///< link quality update logger (time, bssid, rssi, snr, retries, throughput)

    /// TracedCallback signature for link setup completed/canceled events
    using LinkSetupCallback = void (*)(uint8_t /* link ID */, Mac48Address /* AP address */);

    /// TracedCallback signature for EMLSR link switch events
    using EmlsrLinkSwitchCallback = void (*)(uint8_t /* link ID */, Ptr<WifiPhy> /* PHY */);

    /// TracedCallback signature for roaming initiated events
    using RoamingInitiatedCallback = void (*)(Time /* time */, Mac48Address /* oldBssid */,
                                              Mac48Address /* newBssid */, double /* rssi */, double /* snr */);

    /// TracedCallback signature for roaming completed events
    using RoamingCompletedCallback = void (*)(Time /* time */, Mac48Address /* newBssid */,
                                              Time /* latency */, bool /* success */);

    /// TracedCallback signature for background scan complete events
    using BackgroundScanCompleteCallback = void (*)(uint32_t /* apCount */,
                                                    std::map<Mac48Address, double> /* rssiMap */);

    /// TracedCallback signature for link quality update events
    using LinkQualityUpdateCallback = void (*)(Time /* time */, Mac48Address /* bssid */,
                                               double /* rssi */, double /* snr */,
                                               uint32_t /* retries */, double /* throughput */);
};

/**
 * @brief Stream insertion operator.
 *
 * @param os the output stream
 * @param apInfo the AP information
 * @returns a reference to the stream
 */
std::ostream& operator<<(std::ostream& os, const StaWifiMac::ApInfo& apInfo);

} // namespace ns3

#endif /* STA_WIFI_MAC_H */
