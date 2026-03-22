#ifndef KV_INTERFACE_H
#define KV_INTERFACE_H

#include "ns3/bss_tm_11v.h"
#include "ns3/kv-interface-model.h"
#include "ns3/dual-phy-sniffer-helper.h"  // For BeaconInfo struct

namespace ns3
{

// Each class should be documented using Doxygen,
// and have an @ingroup roaming-pipeline-complete directive

class rankListManager : public Object {
    public:
        static TypeId GetTypeId (void);

        rankedAPs
        rankCandidates(const candidateAPs &cands);

        /**
         * \brief Rank candidates using beacon cache data with 60% signal + 40% load formula
         *
         * Uses real BSS Load data (staCount, channelUtilization) from BeaconInfo
         * combined with signal quality (RSSI, SNR) for optimal AP selection.
         *
         * \param beacons Vector of BeaconInfo from DualPhySnifferHelper beacon cache
         * \param excludeBssid BSSID to exclude from ranking (typically current AP)
         * \return rankedAPs sorted by combined score (highest first)
         */
        rankedAPs
        rankCandidatesFromBeaconCache(const std::vector<BeaconInfo>& beacons,
                                      Mac48Address excludeBssid = Mac48Address());

        candidateAPs
        filterLoadedAP(const candidateAPs &cands);

        BssTmParameters 
        convertToBssTmParameters(const rankedAPs& ranked, 
            uint8_t dialogToken = 1,
            uint16_t disassociationTimer = 0,
            uint8_t validityInterval = 255,
            uint16_t terminationDuration = 0,
            BssTmParameters::ReasonCode reasonCode = BssTmParameters::ReasonCode::LOW_RSSI);
};

} // namespace ns3

#endif