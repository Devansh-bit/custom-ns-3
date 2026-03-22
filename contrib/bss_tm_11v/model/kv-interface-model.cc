#include "kv-interface-model.h"

namespace ns3
{

    unrankedCandidates::unrankedCandidates()
    : operatingClass(0x00),
      channel(0x00),
      RSSI(0x00),
      SNR(0x00),
      clientCount(0x00),
      channelUtil(0x00)
  {
      BSSID = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  }
  
  candidateAPs::candidateAPs() {}
  
  rankedCandidates::rankedCandidates()
    : channel(0x00),
      operatingClass(0x00),
      preference(0x00)
  {
      BSSID = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  }
  
  rankedAPs::rankedAPs() {}

} // namespace ns3
