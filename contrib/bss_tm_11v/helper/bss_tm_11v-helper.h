#ifndef BSS_TM_11V_HELPER_H
#define BSS_TM_11V_HELPER_H

#include <ns3/object.h>
#include <ns3/type-id.h>
#include <ns3/socket.h>
#include <ns3/packet.h>

#include "ns3/object.h"
#include "ns3/mac48-address.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-phy-common.h"
#include "ns3/wifi-tx-vector.h"
#include "ns3/config.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/bss_tm_11v.h"
#include "ns3/kv-interface.h"
#include "ns3/beacon-neighbor-model.h"
#include "ns3/lever-api-helper.h"
#include "ns3/application-container.h"
#include "ns3/wifi-phy-band.h"
#include "ns3/wifi-mpdu.h"
#include "ns3/dual-phy-sniffer-helper.h"

namespace ns3 {

class LeverConfig;  // Forward declaration
class UnifiedPhySniffer;
struct ParsedFrameContext;

/**
 * \ingroup wifi
 * \brief IEEE 802.11v BSS Transition Management helper
 *
 * This class implements the BSS Transition Management protocol defined in
 * IEEE 802.11v-2011 (now part of IEEE 802.11-2016). It enables APs to
 * steer STAs to better APs based on various criteria.
 *
 * Key features:
 * - AP sends BSS TM Request with candidate AP list to STA
 * - STA evaluates candidates and sends BSS TM Response (accept/reject)
 * - Supports beacon report data for informed steering decisions
 * - Rate limiting to prevent excessive steering requests
 *
 * This helper extends rankListManager to provide candidate ranking
 * based on RCPI, RSNI, and channel load from beacon reports.
 */
class BssTm11vHelper : public rankListManager
{
  public:
    static TypeId GetTypeId();

    BssTm11vHelper();
    virtual ~BssTm11vHelper();

      void
      sendRankedCandidates(Ptr<WifiNetDevice> apDevice, Mac48Address apAddress, Mac48Address staAddress, std::vector<BeaconReportData> reports);

      void
      SendDynamicBssTmRequest (Ptr<WifiNetDevice> apDevice,
                                            const BssTmParameters &params,
                                            Mac48Address staAddress);

      void
      HandleRead( Ptr<WifiNetDevice> staDevice,
                                  Ptr<const Packet> packet,
                                  Mac48Address apAddress);

      void 
      SendDynamicBssTmResponse(Ptr<WifiNetDevice> staDevice,
                                              const BssTmResponseParameters& params,
                                              Mac48Address apAddress);

      void SetSocket(Ptr<Socket> socket) { m_socket = socket; }
      void SetIsAp(bool isAp) { m_isAp = isAp; }
      void SetLastRssi(double rssi) { m_lastRssi = rssi; }

      // Beacon sniffer integration (external dependency)
      void SetBeaconSniffer(DualPhySnifferHelper* sniffer);
      void SetStaMac(Mac48Address staMac);

      // Inject beacon cache with real-time load data
      void SetBeaconCache(const std::vector<BeaconInfo>& beacons);

      // Rate limiting configuration
      void SetCooldown(Time duration);
      Time GetCooldown() const;

      void InstallOnAp(Ptr<WifiNetDevice> apDevice);
      void InstallOnSta(Ptr<WifiNetDevice> staDevice);
    
    protected:
      ParsedParameters
      ParseRequestParameters(const uint8_t* buffer, uint32_t size);

      BssTmResponseParameters 
      GenerateResponseParameters(const ParsedParameters& requestParams);
    
    private:
        Ptr<Socket> m_socket;
        bool m_isAp = false;
        double m_lastRssi = -70.0;
        Address m_peer;
        Ptr<WifiNetDevice> m_apDevice;
        Ptr<WifiNetDevice> m_staDevice;
        std::map<std::string, uint32_t> m_sentRequests;

        // Rate limiting: track last request time per (AP, STA) pair
        std::map<std::pair<Mac48Address, Mac48Address>, Time> m_lastRequestTime;
        Time m_cooldownDuration;  // Default 5 minutes

        // Beacon sniffer integration (external, not owned)
        DualPhySnifferHelper* m_beaconSniffer;
        Mac48Address m_staMac;  // STA MAC for beacon cache queries

        // Injected beacon cache with real-time load data
        std::vector<BeaconInfo> m_injectedBeacons;

        // Lever-api integration for channel switching
        Ptr<LeverConfig> m_staLeverConfig;
        ApplicationContainer m_staLeverApp;

        // Helper method to determine WiFi band from channel number
        WifiPhyBand DetermineWifiPhyBand(uint8_t channel) const;

        // Callback for when BSS TM Response is acknowledged
        void OnBssTmResponseAcked(Ptr<const WifiMpdu> mpdu);

        // Pending roaming parameters (set when BSS TM Response is sent)
        Mac48Address m_pendingRoamingBssid;
        uint8_t m_pendingRoamingChannel;
        WifiPhyBand m_pendingRoamingBand;
        bool m_waitingForBssTmResponseAck;

        void
          ApSniffer(std::string context,
              Ptr<const Packet> packet,
              uint16_t channelFreq,
              WifiTxVector txVector,
              MpduInfo mpdu,
              SignalNoiseDbm signalNoise,
              uint16_t staId);

        void
          StaSniffer(std::string context,
              Ptr<const Packet> packet,
              uint16_t channelFreq,
              WifiTxVector txVector,
              MpduInfo mpdu,
              SignalNoiseDbm signalNoise,
              uint16_t staId);

        // Unified sniffer for efficient callback handling
        Ptr<UnifiedPhySniffer> m_apSniffer;         // Unified sniffer on AP
        Ptr<UnifiedPhySniffer> m_staSniffer;        // Unified sniffer on STA
        uint32_t m_apSnifferSubscriptionId;         // AP subscription ID
        uint32_t m_staSnifferSubscriptionId;        // STA subscription ID

        // Unified sniffer callbacks
        void UnifiedApActionCallback(ParsedFrameContext* ctx);   // AP: receives BSS TM responses
        void UnifiedStaActionCallback(ParsedFrameContext* ctx);  // STA: receives BSS TM requests
    };
}

#endif /* BSS_TM_11V_HELPER_H */
