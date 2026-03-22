#ifndef LOAD_BALANCE_HELPER_H
#define LOAD_BALANCE_HELPER_H

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/wifi-net-device.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/net-device-container.h"
#include "ns3/dual-phy-sniffer-helper.h"
#include "ns3/bss_tm_11v-helper.h"
#include "ns3/kafka-producer.h"
#include "ns3/simulation-event-producer.h"

#include <map>
#include <set>
#include <vector>

namespace ns3
{

/**
 * STA RSSI tracking structure
 * Tracks RSSI measurements for each STA on each AP
 */
struct StaRssiTracker
{
    double currentRssi = 0.0;
    double ewmaRssi = 0.0;
    Time lastUpdate;
    bool initialized = false;
};

/**
 * Configuration for load balancing
 */
struct LoadBalanceConfig
{
    bool enabled = true;
    double channelUtilThreshold = 0.70;  // 0-1 scale (70%)
    double intervalSec = 60.0;           // Check interval in seconds
    double cooldownSec = 120.0;          // Cooldown between triggers (2 minutes)
    double rssiThreshold = -70.0;        // Min RSSI for candidate AP (dBm)
};

/**
 * \brief Load Balance Helper - Manages load-based STA offloading
 *
 * Monitors AP channel utilization and triggers BSS TM requests
 * to offload STAs to less loaded APs when utilization exceeds threshold.
 *
 * Algorithm:
 * 1. Periodically check each AP's channel utilization
 * 2. For overloaded APs, find STAs with lowest RSSI
 * 3. Query STA's beacon cache for candidate APs
 * 4. Validate candidate has good RSSI and lower utilization
 * 5. Trigger BSS TM Request for load balancing
 */
class LoadBalanceHelper : public Object
{
public:
    static TypeId GetTypeId();
    LoadBalanceHelper();
    virtual ~LoadBalanceHelper();

    // Configuration
    void SetConfig(const LoadBalanceConfig& config);
    void SetRssiThreshold(double rssiDbm);

    // Dependencies - must be set before Start()
    void SetDualPhySniffer(DualPhySnifferHelper* sniffer);
    void SetBssTmHelpers(std::vector<Ptr<BssTm11vHelper>>* helpers);
    void SetApDevices(NetDeviceContainer* devices);
    void SetStaDevices(NetDeviceContainer* devices);
    void SetApMetrics(std::map<uint32_t, ApMetrics>* metrics);
    void SetStaRssiTracker(std::map<Mac48Address, std::map<Mac48Address, StaRssiTracker>>* tracker);
    void SetSimEventProducer(Ptr<SimulationEventProducer> producer);
    void SetNodeIdMappings(std::map<uint32_t, uint32_t>* staMap, std::map<uint32_t, uint32_t>* apMap);

    // Control
    void Start(Time delay = Seconds(30));
    void Stop();

private:
    void PeriodicCheck();
    void TriggerBssTm(uint32_t apIndex, Mac48Address staMac, Mac48Address apBssid, Mac48Address targetBssid);
    double GetCandidateUtilization(Mac48Address candidateBssid);

    LoadBalanceConfig m_config;
    bool m_running;

    // Cooldown tracking: (AP BSSID, STA MAC) -> last trigger time
    std::map<std::pair<Mac48Address, Mac48Address>, Time> m_cooldown;

    // External dependencies (pointers to simulation globals)
    DualPhySnifferHelper* m_sniffer;
    std::vector<Ptr<BssTm11vHelper>>* m_bssTmHelpers;
    NetDeviceContainer* m_apDevices;
    NetDeviceContainer* m_staDevices;
    std::map<uint32_t, ApMetrics>* m_apMetrics;
    std::map<Mac48Address, std::map<Mac48Address, StaRssiTracker>>* m_staRssiTracker;
    Ptr<SimulationEventProducer> m_simEventProducer;
    std::map<uint32_t, uint32_t>* m_simNodeIdToConfigNodeId;
    std::map<uint32_t, uint32_t>* m_apSimNodeIdToConfigNodeId;
};

} // namespace ns3

#endif // LOAD_BALANCE_HELPER_H
