#ifndef BSS_TM_11V_H
#define BSS_TM_11V_H

/**
 * \defgroup bss_tm_11v IEEE 802.11v BSS Transition Management
 */

//  #include "beacon-neighbor-model.h"
// #include "ns3/socket.h"
 #include "ns3/net-device.h"
 #include "ns3/mac48-address.h"
 #include <cstdint>
 #include <vector>
 #include <array>
 #include <string>
 
 namespace ns3
 {
 
     struct CandidateInfo
     {
         uint8_t BSSID[6];
         uint8_t operatingClass;
         uint8_t channel;
         uint8_t preference;
     };
 
     struct BssTmParameters
     {
         enum class ReasonCode {
             LOW_RSSI,
             AP_UNAVAILABLE,
             HIGH_LOAD,
             ESS_DISASSOCIATION
         };
 
         struct CandidateAP {
             uint8_t operatingClass;
             uint8_t channel;
             uint8_t phyType;
             uint8_t preference;
             uint8_t BSSID[6];
 
             CandidateAP();
         };
 
         uint8_t dialogToken;
         uint16_t disassociationTimer;
         uint8_t validityInterval;
         uint16_t terminationDuration;
         ReasonCode reasonCode;
         std::vector<CandidateAP> candidates;
 
         BssTmParameters();
     };
 
     struct ParsedParameters
     {
         uint8_t dialogToken;
         uint16_t disassociationTimer;
         BssTmParameters::ReasonCode reasonCode;
         std::vector<CandidateInfo> candidates;
 
         ParsedParameters();
     };
 
     /**
     * BSS Transition Response parameters
     */
     struct BssTmResponseParameters
     {
         uint8_t dialogToken;
         uint8_t statusCode;
         uint8_t terminationDelay;
         uint8_t channel;
         uint8_t targetBSSID[6];
 
         BssTmResponseParameters();
     };

     /** Utility converter */
     std::string ReasonCodeToString(BssTmParameters::ReasonCode rc);
 
 }
 
#endif /* BSS_TM_11V_H */
