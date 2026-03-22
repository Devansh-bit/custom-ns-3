#ifndef BEACON_PROTOCOL_11K_HELPER_H
#define BEACON_PROTOCOL_11K_HELPER_H

#include "../model/beacon-neighbor-model.h"

#include "ns3/object.h"
#include "ns3/traced-callback.h"
#include "ns3/dual-phy-sniffer-helper.h"
#include "ns3/yans-wifi-channel.h"

namespace ns3
{

class UnifiedPhySniffer;
struct ParsedFrameContext;

/**
 * \ingroup wifi
 * \brief IEEE 802.11k Beacon Report Protocol helper
 *
 * This class implements the Beacon Report protocol defined in IEEE 802.11k.
 * It allows APs to request beacon measurements from STAs, which can be used
 * for radio resource management and roaming optimization.
 *
 * The protocol workflow:
 * 1. AP sends Beacon Measurement Request to STA
 * 2. STA scans for beacons from neighbor APs (optionally using dual-PHY)
 * 3. STA sends Beacon Report with RCPI/RSNI measurements
 * 4. AP uses the data for steering or RRM decisions
 *
 * Beacon measurements can use single-radio or dual-radio (off-channel) scanning.
 */
class BeaconProtocolHelper : public Object
{
  public:
    static TypeId GetTypeId();

    BeaconProtocolHelper();
    virtual ~BeaconProtocolHelper();

    // ===== Configuration =====
    void SetNeighborList(std::set<Mac48Address> neighborList);

    // NEW: Dual-PHY sniffer configuration for multi-channel beacon measurement
    void SetChannel(Ptr<YansWifiChannel> channel);
    void SetScanningChannels(const std::vector<uint8_t>& channels);
    void SetHopInterval(Time interval);

    // NEW: Set external dual-PHY sniffer instance (for sharing across protocols)
    void SetDualPhySniffer(DualPhySnifferHelper* sniffer);

    // ===== Installation =====
    void InstallOnAp(Ptr<WifiNetDevice> apDevice);
    void InstallOnSta(Ptr<WifiNetDevice> staDevice);

    // ===== Protocol Methods (called from neighbor helper) =====
    void SendBeaconRequest(Ptr<WifiNetDevice> apDevice, Mac48Address staAddress);

    // ===== Trace Sources =====
    /**
     * \brief Fired when AP receives Beacon Report
     * \param apAddress MAC of AP
     * \param staAddress MAC of STA
     * \param reports Vector of raw beacon report data
     */
    TracedCallback<Mac48Address, Mac48Address, std::vector<BeaconReportData>> m_beaconReportReceivedTrace;

  private:
    // ===== Sniffers =====
    void ApSniffer(std::string context,
                   Ptr<const Packet> packet,
                   uint16_t channelFreq,
                   WifiTxVector txVector,
                   MpduInfo mpdu,
                   SignalNoiseDbm signalNoise,
                   uint16_t staId);

    void StaSniffer(std::string context,
                    Ptr<const Packet> packet,
                    uint16_t channelFreq,
                    WifiTxVector txVector,
                    MpduInfo mpdu,
                    SignalNoiseDbm signalNoise,
                    uint16_t staId);

    // ===== Protocol Handlers =====
    void ReceiveBeaconRequest(Ptr<WifiNetDevice> staDevice,
                              Ptr<const Packet> packet,
                              Mac48Address apAddress);

    void SendBeaconReport(Ptr<WifiNetDevice> staDevice,
                          Mac48Address apAddress,
                          uint8_t dialogToken);

    void ReceiveBeaconReport(Ptr<const Packet> packet, Mac48Address staAddress);

    void StartMeasurement(Time duration);
    void StopMeasurement();

    bool IsMeasuring() const
    {
        return m_isMeasuring;
    }

    std::map<Mac48Address, BeaconMeasurement> GetBeaconCache() const
    {
        return m_beaconCache;
    }

    // ===== State =====
    Ptr<WifiNetDevice> m_apDevice;
    Ptr<WifiNetDevice> m_staDevice;
    std::map<Mac48Address, BeaconMeasurement> m_beaconCache;
    std::set<Mac48Address> m_neighborList;
    bool m_isMeasuring;              // NEW
    Time m_measurementStartTime;     // NEW
    Time m_measurementDuration;      // NEW
    ns3::EventId m_measurementTimer; // NEW

    // NEW: Dual-PHY sniffer for multi-channel beacon measurement
    DualPhySnifferHelper* m_dualPhySniffer;
    bool m_useDualPhySniffer;        // Flag to enable/disable dual-PHY mode
    bool m_externalDualPhy;          // Flag to indicate external dual-PHY instance (don't delete on destructor)
    Mac48Address m_dualPhyOperatingMac;  // Operating MAC assigned by dual-PHY sniffer

    // Unified sniffer for efficient callback handling
    Ptr<UnifiedPhySniffer> m_apSniffer;        // Unified sniffer on AP
    Ptr<UnifiedPhySniffer> m_staSniffer;       // Unified sniffer on STA
    uint32_t m_apSnifferSubscriptionId;        // AP subscription ID
    uint32_t m_staActionSubscriptionId;        // STA action frame subscription ID
    uint32_t m_staBeaconSubscriptionId;        // STA beacon subscription ID

    // Unified sniffer callbacks
    void UnifiedApActionCallback(ParsedFrameContext* ctx);      // AP: receives beacon reports
    void UnifiedStaActionCallback(ParsedFrameContext* ctx);     // STA: receives beacon requests
    void UnifiedStaBeaconCallback(ParsedFrameContext* ctx);     // STA: receives beacons
};

} // namespace ns3

#endif // BEACON_PROTOCOL_HELPER_H
