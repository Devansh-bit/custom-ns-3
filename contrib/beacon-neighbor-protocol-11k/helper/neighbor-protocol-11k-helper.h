#ifndef NEIGHBOR_PROTOCOL_11K_HELPER_H
#define NEIGHBOR_PROTOCOL_11K_HELPER_H

#include "../model/beacon-neighbor-model.h"

#include "ns3/object.h"
#include "ns3/traced-callback.h"
#include "ns3/dual-phy-sniffer-helper.h"  // NEW: For multi-channel neighbor discovery
#include "ns3/yans-wifi-channel.h"

namespace ns3
{

class UnifiedPhySniffer;
struct ParsedFrameContext;

/**
 * \ingroup wifi
 * \brief IEEE 802.11k Neighbor Report Protocol helper
 *
 * This class implements the Neighbor Report protocol defined in IEEE 802.11k.
 * It allows STAs to request neighbor information from their associated AP,
 * which can be used for roaming decisions.
 *
 * The protocol installs on both AP and STA devices:
 * - AP: Responds to neighbor report requests with a list of known neighbor APs
 * - STA: Sends neighbor report requests and processes the responses
 *
 * Neighbor information can be derived from:
 * - Static configuration (SetNeighborTable)
 * - Dynamic discovery via dual-PHY scanning
 */
class NeighborProtocolHelper : public Object
{
  public:
    static TypeId GetTypeId();

    NeighborProtocolHelper();
    virtual ~NeighborProtocolHelper();

    // ===== Configuration =====
    void SetNeighborTable(std::vector<ApInfo> table);

    // NEW: Configuration for dual-PHY sniffer
    void SetChannel(Ptr<YansWifiChannel> channel);
    void SetScanningChannels(const std::vector<uint8_t>& channels);
    void SetHopInterval(Time interval);

    /**
     * \brief Set an external dual-PHY sniffer to use instead of creating a new one
     * \param sniffer Pointer to existing DualPhySnifferHelper instance
     *
     * Use this when APs are already created with dual-PHY sniffer to avoid
     * creating duplicate devices on the same node.
     */
    void SetDualPhySniffer(DualPhySnifferHelper* sniffer);

    // ===== Installation =====
    void InstallOnAp(Ptr<WifiNetDevice> apDevice);
    void InstallOnSta(Ptr<WifiNetDevice> staDevice);

    // ===== Protocol Methods (called from main) =====
    void SendNeighborReportRequest(Ptr<WifiNetDevice> staDevice, Mac48Address apAddress);

    // ===== Trace Sources =====
    /**
     * \brief Fired when STA receives Neighbor Report Response
     * \param staAddress MAC of STA
     * \param apAddress MAC of AP
     * \param neighbors Vector of neighbor report data
     */
    TracedCallback<Mac48Address, Mac48Address, std::vector<NeighborReportData>> m_neighborReportReceivedTrace;

    // ===== Accessors =====
    std::set<Mac48Address> GetNeighborList() const
    {
        return m_neighborList;
    }

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
    void ProcessNeighborReportRequest(Ptr<WifiNetDevice> apDevice,
                                      Ptr<const Packet> packet,
                                      Mac48Address staAddress);

    void SendNeighborReportResponse(Ptr<WifiNetDevice> apDevice,
                                    Mac48Address staAddress,
                                    uint8_t dialogToken);

    void ReceiveNeighborReportResponse(Ptr<const Packet> packet, Mac48Address apAddress);

    void StoreNeighborList(std::vector<Mac48Address> neighbors);

    // ===== State =====
    Ptr<WifiNetDevice> m_apDevice;
    Ptr<WifiNetDevice> m_staDevice;
    std::vector<ApInfo> m_neighborTable;
    std::set<Mac48Address> m_neighborList;

    std::map<Mac48Address, ApInfo> m_discoveredNeighbors;

    std::map<Mac48Address, uint8_t> m_neighborApLoadMap;

    // Dual-PHY sniffer for multi-channel neighbor discovery
    DualPhySnifferHelper* m_dualPhySniffer;
    Mac48Address m_dualPhyOperatingMac;  // Operating MAC assigned by dual-PHY sniffer
    bool m_externalDualPhy;  // True if dual-PHY provided externally, false if created internally

    // Unified sniffer for efficient callback handling
    Ptr<UnifiedPhySniffer> m_apSniffer;         // Unified sniffer on AP
    Ptr<UnifiedPhySniffer> m_staSniffer;        // Unified sniffer on STA
    uint32_t m_apSnifferSubscriptionId;         // AP subscription ID
    uint32_t m_staSnifferSubscriptionId;        // STA subscription ID

    // Unified sniffer callbacks
    void UnifiedApActionCallback(ParsedFrameContext* ctx);   // AP: receives neighbor report requests
    void UnifiedStaActionCallback(ParsedFrameContext* ctx);  // STA: receives neighbor report responses
};

} // namespace ns3

#endif // NEIGHBOR_PROTOCOL_HELPER_H
