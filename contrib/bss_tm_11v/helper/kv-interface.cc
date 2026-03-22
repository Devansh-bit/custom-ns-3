#include "ns3/kv-interface.h"
#include "bss_tm_11v-helper.h"
#include "ns3/kv-interface-model.h"
#include "ns3/mac48-address.h"
#include <algorithm>  // For std::sort, std::min, std::max

namespace ns3
{

    NS_LOG_COMPONENT_DEFINE("rankListManager");
    NS_OBJECT_ENSURE_REGISTERED(rankListManager);

    TypeId
    rankListManager::GetTypeId (void)
    {
      static TypeId tid =
          TypeId ("ns3::rankListManager")
              .SetParent<Object> ()
              .SetGroupName ("Wifi");
      return tid;
    }

    BssTmParameters 
    rankListManager::convertToBssTmParameters(const rankedAPs& ranked, 
            uint8_t dialogToken,
            uint16_t disassociationTimer,
            uint8_t validityInterval,
            uint16_t terminationDuration,
            BssTmParameters::ReasonCode reasonCode)
    {
    BssTmParameters params;

    // Set the basic parameters
    params.dialogToken = dialogToken;
    params.disassociationTimer = disassociationTimer;
    params.validityInterval = validityInterval;
    params.terminationDuration = terminationDuration;
    params.reasonCode = reasonCode;

    // Convert rankedCandidates to CandidateAP vector
    for (const auto& rankedCandidate : ranked.candidates) {
    BssTmParameters::CandidateAP candidate;

    candidate.operatingClass = rankedCandidate.operatingClass;
    candidate.channel = rankedCandidate.channel;
    candidate.phyType = 0; // You'll need to determine this based on your requirements
    candidate.preference = rankedCandidate.preference;

    // Copy BSSID from std::array to C-style array
    std::copy(rankedCandidate.BSSID.begin(), 
    rankedCandidate.BSSID.end(), 
    candidate.BSSID);

    params.candidates.push_back(candidate);
    }

    return params;
    }

    candidateAPs 
    rankListManager::filterLoadedAP(const candidateAPs &cands){
        candidateAPs filteredAPs;

        NS_LOG_DEBUG ("Starting filterLoadedAP. Total candidates: " << cands.candidates.size());

        const double MAX_COUNT_VALUE = 45.0; // Maximum Number of Clients Connected, if more, doesn't matter as the score will still be 10 itself
        const double LOAD_FILTER_THRESHOLD = 6.0;
        const double COUNT_NORMALISATION_DIVISOR = 4.5;
        const double MAX_UTIL_VALUE = 75.0;
        const double UTIL_NORMALISATION_DIVISOR = 7.5;
        const double ALPHA = 0.7;


        for (const auto& ap : cands.candidates)
        {
            double clientValue = std::min((double)ap.clientCount, MAX_COUNT_VALUE);
            double clientScore = clientValue / COUNT_NORMALISATION_DIVISOR;

            double utilValue = std::min((double)ap.channelUtil, MAX_UTIL_VALUE);
            double utilScore = utilValue/ UTIL_NORMALISATION_DIVISOR;

            double totalScore = (ALPHA*utilScore + (1-ALPHA)*clientScore);

            NS_LOG_INFO ("AP (Ch: " << (uint32_t)ap.channel
                                     << ", Client: " << (uint32_t)ap.clientCount
                                     << ", Util: " << (uint32_t)ap.channelUtil
                                     << "): Score = " << totalScore);

            if (true)
            {
                filteredAPs.candidates.push_back(ap);
                NS_LOG_DEBUG (" --> KEPT (Score < " << LOAD_FILTER_THRESHOLD << ")");
            }
            else
            {
                // AP is considered loaded (score >= 6.0) and is filtered out.
                NS_LOG_DEBUG (" --> FILTERED OUT (Score >= " << LOAD_FILTER_THRESHOLD << ")");
            }
        }

        NS_LOG_INFO ("Filter complete. Kept candidates: " << filteredAPs.candidates.size());

        return filteredAPs;
    }

    rankedAPs
    rankListManager::rankCandidates(const candidateAPs &cands){
        candidateAPs filteredCandidates = filterLoadedAP(cands);

        NS_LOG_DEBUG("Starting Ranking of Candidate APs. Total Candidates: " << cands.candidates.size());

        if (filteredCandidates.candidates.empty())
        {
            NS_LOG_DEBUG("No candidates remaining after filtering. Returning empty ranked list.");
            return rankedAPs();
        }

        const double SIGNAL_SCORE_ALPHA = 0.8;
        std::vector<double> signalScores;
        std::vector<size_t> indices;

        for (size_t i = 0; i < filteredCandidates.candidates.size(); ++i)
        {
            const auto& ap = filteredCandidates.candidates[i];

            double signalScore = (SIGNAL_SCORE_ALPHA * (double)ap.SNR) + ((1.0 - SIGNAL_SCORE_ALPHA) * (double)ap.RSSI);

            signalScores.push_back(signalScore);
            indices.push_back(i);

            NS_LOG_DEBUG ("AP Index " << i << " (Ch: " << (uint32_t)ap.channel
                                     << ", RSSI: " << (uint32_t)ap.RSSI
                                     << ", SNR: " << (uint32_t)ap.SNR
                                     << "): Signal Score = " << signalScore);
        }

        std::sort(indices.begin(), indices.end(), 
            [&signalScores](size_t a, size_t b) {
                return signalScores[a] > signalScores[b];
            }
        );

        rankedAPs finalRanks;
        uint8_t preference = 255;

        for (size_t sortedIndex : indices)
        {
            const auto& ap = filteredCandidates.candidates[sortedIndex];
            
            rankedCandidates rc;
            rc.channel = ap.channel;
            rc.operatingClass = ap.operatingClass;
            rc.BSSID = ap.BSSID;
            rc.preference = preference--;

            finalRanks.candidates.push_back(rc);

            NS_LOG_DEBUG ("Rank " << (uint32_t)rc.preference
                                 << ": Ch " << (uint32_t)rc.channel
                                 << " (Score: " << signalScores[sortedIndex] << ")");
        }

        NS_LOG_INFO ("Ranking complete. Total ranked APs: " << finalRanks.candidates.size());

        return finalRanks;
    }

    rankedAPs
    rankListManager::rankCandidatesFromBeaconCache(const std::vector<BeaconInfo>& beacons,
                                                   Mac48Address excludeBssid)
    {
        NS_LOG_INFO("Ranking candidates from beacon cache using 60% signal + 40% load formula");

        // Scoring weights
        const double SIGNAL_WEIGHT = 0.6;
        const double LOAD_WEIGHT = 0.4;

        // Signal scoring params (within signal component)
        const double SIGNAL_SNR_WEIGHT = 0.8;   // 80% SNR
        const double SIGNAL_RSSI_WEIGHT = 0.2;  // 20% RSSI

        // Load scoring params
        const double MAX_STA_COUNT = 50.0;      // Normalize station count
        const double MAX_CHANNEL_UTIL = 255.0;  // Max channel utilization value

        std::vector<std::pair<double, BeaconInfo>> scoredCandidates;

        for (const auto& beacon : beacons)
        {
            // Skip the current AP (don't recommend staying)
            if (beacon.bssid == excludeBssid)
            {
                NS_LOG_DEBUG("Excluding current AP: " << beacon.bssid);
                continue;
            }

            // Calculate signal score (higher is better)
            // RSSI: typically -90 to -30 dBm, normalize to 0-1
            double rssiNorm = (beacon.rssi + 90.0) / 60.0;  // -90 = 0, -30 = 1
            rssiNorm = std::max(0.0, std::min(1.0, rssiNorm));

            // SNR: typically 0-40 dB, normalize to 0-1
            double snrNorm = beacon.snr / 40.0;
            snrNorm = std::max(0.0, std::min(1.0, snrNorm));

            double signalScore = (SIGNAL_SNR_WEIGHT * snrNorm) + (SIGNAL_RSSI_WEIGHT * rssiNorm);

            // Calculate load score (lower load = higher score, so invert)
            double staCountNorm = std::min((double)beacon.staCount, MAX_STA_COUNT) / MAX_STA_COUNT;
            double utilNorm = beacon.channelUtilization / MAX_CHANNEL_UTIL;

            // Load score: 1 - (weighted avg of sta count and utilization)
            // Lower load = higher loadScore
            double loadScore = 1.0 - (0.5 * staCountNorm + 0.5 * utilNorm);

            // Combined score: 60% signal + 40% load
            double totalScore = (SIGNAL_WEIGHT * signalScore) + (LOAD_WEIGHT * loadScore);

            scoredCandidates.push_back({totalScore, beacon});

            NS_LOG_INFO("Candidate: BSSID=" << beacon.bssid
                        << ", RSSI=" << beacon.rssi << "dBm"
                        << ", SNR=" << beacon.snr << "dB"
                        << ", STAs=" << beacon.staCount
                        << ", ChUtil=" << (int)beacon.channelUtilization
                        << " => SignalScore=" << signalScore
                        << ", LoadScore=" << loadScore
                        << ", TotalScore=" << totalScore);
        }

        // Sort by total score (descending - highest score first)
        std::sort(scoredCandidates.begin(), scoredCandidates.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        // Build ranked result
        rankedAPs finalRanks;
        uint8_t preference = 255;

        for (const auto& [score, beacon] : scoredCandidates)
        {
            rankedCandidates rc;
            rc.channel = beacon.channel;
            // Determine operating class from channel (2.4GHz: 0x51, 5GHz: 0x80)
            rc.operatingClass = (beacon.channel <= 14) ? 0x51 : 0x80;
            beacon.bssid.CopyTo(rc.BSSID.data());
            rc.preference = preference--;

            finalRanks.candidates.push_back(rc);

            NS_LOG_INFO("Rank " << (int)(preference + 1) << ": BSSID=" << beacon.bssid
                        << ", Ch=" << (int)beacon.channel
                        << ", Score=" << score);
        }

        NS_LOG_INFO("Ranking complete. Total ranked APs: " << finalRanks.candidates.size());
        return finalRanks;
    }

} // namespace ns3