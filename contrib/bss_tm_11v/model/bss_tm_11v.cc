#include "bss_tm_11v.h"

namespace ns3{

BssTmParameters::CandidateAP::CandidateAP()
    : operatingClass(0x80),
      channel(36),
      phyType(0x09),
      preference(255)
{
    BSSID[0] = 0x00; BSSID[1] = 0x11;
    BSSID[2] = 0x22; BSSID[3] = 0x33;
    BSSID[4] = 0x44; BSSID[5] = 0x55;
}

BssTmParameters::BssTmParameters()
    : dialogToken(0x03),
      disassociationTimer(100),
      validityInterval(0x05),
      terminationDuration(0x00),
      reasonCode(ReasonCode::LOW_RSSI)
{
}

ParsedParameters::ParsedParameters()
    : dialogToken(0x03),
      disassociationTimer(100),
      reasonCode(BssTmParameters::ReasonCode::LOW_RSSI)
{
}

BssTmResponseParameters::BssTmResponseParameters()
    : dialogToken(0x03),
      statusCode(0x00),
      terminationDelay(0x00),
      channel(0x00)
{
    targetBSSID[0] = 0x00; targetBSSID[1] = 0x11;
    targetBSSID[2] = 0x22; targetBSSID[3] = 0x33;
    targetBSSID[4] = 0x44; targetBSSID[5] = 0x55;
}

std::string ReasonCodeToString(BssTmParameters::ReasonCode rc)
{
    switch(rc) {
        case BssTmParameters::ReasonCode::LOW_RSSI: return "LOW_RSSI";
        case BssTmParameters::ReasonCode::AP_UNAVAILABLE: return "AP_UNAVAILABLE";
        case BssTmParameters::ReasonCode::HIGH_LOAD: return "HIGH_LOAD";
        case BssTmParameters::ReasonCode::ESS_DISASSOCIATION: return "ESS_DISASSOCIATION";
        default: return "UNKNOWN";
    }
}

}
