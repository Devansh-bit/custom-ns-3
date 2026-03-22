#ifndef ROAMING_PIPELINE_COMPLETE_H
#define ROAMING_PIPELINE_COMPLETE_H

// Add a doxygen group for this module.
// If you have more than one file, this should be in only one of them.
/**
 * @defgroup roaming-pipeline-complete Description of the roaming-pipeline-complete
 */

#include <vector>
#include <array>
#include <cstdint>

namespace ns3
{

// Each class should be documented using Doxygen,
// and have an @ingroup roaming-pipeline-complete directive

/* ... */
    struct unrankedCandidates{
        uint8_t operatingClass;
        uint8_t channel;
        uint8_t RSSI;
        uint8_t SNR;
        uint8_t clientCount;
        uint8_t channelUtil;
        std::array<uint8_t, 6> BSSID;

        unrankedCandidates();
    };

    struct candidateAPs{
        std::vector<unrankedCandidates> candidates;

        candidateAPs();
    };

    struct rankedCandidates{
        uint8_t channel;
        uint8_t operatingClass;
        uint8_t preference;
        std::array<uint8_t, 6> BSSID;

        rankedCandidates();
    };

    struct rankedAPs{
        std::vector<rankedCandidates> candidates;

        rankedAPs();
    };
} // namespace ns3

#endif // ROAMING_PIPELINE_COMPLETE_H
