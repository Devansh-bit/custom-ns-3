#include "load-balance-helper.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("LoadBalanceHelper");
NS_OBJECT_ENSURE_REGISTERED(LoadBalanceHelper);

TypeId
LoadBalanceHelper::GetTypeId()
{
    static TypeId tid = TypeId("ns3::LoadBalanceHelper")
        .SetParent<Object>()
        .SetGroupName("Wifi")
        .AddConstructor<LoadBalanceHelper>();
    return tid;
}

LoadBalanceHelper::LoadBalanceHelper()
    : m_running(false),
      m_sniffer(nullptr),
      m_bssTmHelpers(nullptr),
      m_apDevices(nullptr),
      m_staDevices(nullptr),
      m_apMetrics(nullptr),
      m_staRssiTracker(nullptr),
      m_simEventProducer(nullptr),
      m_simNodeIdToConfigNodeId(nullptr),
      m_apSimNodeIdToConfigNodeId(nullptr)
{
}

LoadBalanceHelper::~LoadBalanceHelper() = default;

void
LoadBalanceHelper::SetConfig(const LoadBalanceConfig& config)
{
    m_config = config;
}

void
LoadBalanceHelper::SetRssiThreshold(double rssiDbm)
{
    m_config.rssiThreshold = rssiDbm;
}

void
LoadBalanceHelper::SetDualPhySniffer(DualPhySnifferHelper* sniffer)
{
    m_sniffer = sniffer;
}

void
LoadBalanceHelper::SetBssTmHelpers(std::vector<Ptr<BssTm11vHelper>>* helpers)
{
    m_bssTmHelpers = helpers;
}

void
LoadBalanceHelper::SetApDevices(NetDeviceContainer* devices)
{
    m_apDevices = devices;
}

void
LoadBalanceHelper::SetStaDevices(NetDeviceContainer* devices)
{
    m_staDevices = devices;
}

void
LoadBalanceHelper::SetApMetrics(std::map<uint32_t, ApMetrics>* metrics)
{
    m_apMetrics = metrics;
}

void
LoadBalanceHelper::SetStaRssiTracker(
    std::map<Mac48Address, std::map<Mac48Address, StaRssiTracker>>* tracker)
{
    m_staRssiTracker = tracker;
}

void
LoadBalanceHelper::SetSimEventProducer(Ptr<SimulationEventProducer> producer)
{
    m_simEventProducer = producer;
}

void
LoadBalanceHelper::SetNodeIdMappings(std::map<uint32_t, uint32_t>* staMap,
                                      std::map<uint32_t, uint32_t>* apMap)
{
    m_simNodeIdToConfigNodeId = staMap;
    m_apSimNodeIdToConfigNodeId = apMap;
}

void
LoadBalanceHelper::Start(Time delay)
{
    m_running = true;
    Simulator::Schedule(delay, &LoadBalanceHelper::PeriodicCheck, this);
    std::cout << "[Load Balance] Started, first check at t="
              << (Simulator::Now() + delay).GetSeconds() << "s (interval="
              << m_config.intervalSec << "s, threshold="
              << (m_config.channelUtilThreshold * 100) << "%)" << std::endl;
}

void
LoadBalanceHelper::Stop()
{
    m_running = false;
}

double
LoadBalanceHelper::GetCandidateUtilization(Mac48Address candidateBssid)
{
    // Use real-time utilization from g_apMetrics
    if (m_apMetrics)
    {
        for (const auto& [nodeId, metrics] : *m_apMetrics)
        {
            if (metrics.bssid == candidateBssid)
            {
                return metrics.channelUtilization;
            }
        }
    }
    return 1.0; // Assume full if not found
}

void
LoadBalanceHelper::PeriodicCheck()
{
    if (!m_running || !m_config.enabled)
    {
        Simulator::Schedule(Seconds(m_config.intervalSec), &LoadBalanceHelper::PeriodicCheck, this);
        return;
    }

    std::cout << "\n[LOAD-BALANCE] " << Simulator::Now().GetSeconds()
              << "s: Checking AP utilization..." << std::endl;

    for (uint32_t apIndex = 0; apIndex < m_apDevices->GetN(); apIndex++)
    {
        auto metricsIt = m_apMetrics->find(apIndex);
        if (metricsIt == m_apMetrics->end())
            continue;

        ApMetrics& apMetrics = metricsIt->second;

        // Check if utilization exceeds threshold
        if (apMetrics.channelUtilization < m_config.channelUtilThreshold)
            continue;

        std::cout << "[LOAD-BALANCE] AP " << apIndex << " (" << apMetrics.bssid
                  << "): Util=" << std::fixed << std::setprecision(1)
                  << (apMetrics.channelUtilization * 100) << "% > "
                  << (m_config.channelUtilThreshold * 100) << "%" << std::endl;

        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(m_apDevices->Get(apIndex));
        if (!apDev)
            continue;

        Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDev->GetMac());
        if (!apMac)
            continue;

        Mac48Address apBssid = apMac->GetAddress();
        const auto& staList = apMac->GetStaList(0);

        if (staList.empty())
        {
            std::cout << "wi   └─ No associated STAs" << std::endl;
            continue;
        }

        // Build list of STAs sorted by RSSI (lowest first)
        std::vector<std::pair<Mac48Address, double>> stasByRssi;
        auto apRssiIt = m_staRssiTracker->find(apBssid);
        if (apRssiIt != m_staRssiTracker->end())
        {
            for (const auto& [aid, staMac] : staList)
            {
                auto staRssiIt = apRssiIt->second.find(staMac);
                if (staRssiIt != apRssiIt->second.end() && staRssiIt->second.initialized)
                {
                    stasByRssi.push_back(std::make_pair(staMac, staRssiIt->second.ewmaRssi));
                }
            }
        }

        if (stasByRssi.empty())
        {
            std::cout << "[LOAD-BALANCE]   └─ No RSSI data for STAs" << std::endl;
            continue;
        }

        // Sort by RSSI ascending (lowest first - weakest clients to offload first)
        std::sort(stasByRssi.begin(), stasByRssi.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });

        // Try to offload STAs until utilization drops below threshold
        std::set<Mac48Address> offloadedThisRound;
        for (const auto& [staMac, rssi] : stasByRssi)
        {
            // Re-check utilization (may have dropped from previous offloads)
            if (apMetrics.channelUtilization < m_config.channelUtilThreshold)
                break;

            // Check cooldown
            auto cooldownKey = std::make_pair(apBssid, staMac);
            auto cooldownIt = m_cooldown.find(cooldownKey);
            if (cooldownIt != m_cooldown.end())
            {
                Time elapsed = Simulator::Now() - cooldownIt->second;
                if (elapsed < Seconds(m_config.cooldownSec))
                {
                    continue; // Still in cooldown
                }
            }

            // Query STA's beacon cache
            std::vector<BeaconInfo> beacons = m_sniffer->GetBeaconsReceivedBy(staMac);

            // Find best candidate (highest RSSI, excluding self)
            Mac48Address bestCandidate;
            double bestRssi = -200.0;
            for (const auto& beacon : beacons)
            {
                if (beacon.bssid == apBssid)
                    continue;
                if (beacon.rssi > bestRssi)
                {
                    bestRssi = beacon.rssi;
                    bestCandidate = beacon.bssid;
                }
            }

            if (bestCandidate == Mac48Address())
            {
                std::cout << "[LOAD-BALANCE]   STA " << staMac
                          << ": No candidate APs in beacon cache" << std::endl;
                continue;
            }

            // Check RSSI threshold
            if (bestRssi < m_config.rssiThreshold)
            {
                std::cout << "[LOAD-BALANCE]   STA " << staMac << ": candidate RSSI "
                          << std::fixed << std::setprecision(1) << bestRssi
                          << " dBm < threshold " << m_config.rssiThreshold << " dBm" << std::endl;
                continue;
            }

            // Check candidate utilization (real-time from g_apMetrics)
            double candidateUtil = GetCandidateUtilization(bestCandidate);
            if (candidateUtil >= m_config.channelUtilThreshold)
            {
                std::cout << "[LOAD-BALANCE]   STA " << staMac << ": candidate "
                          << bestCandidate << " util " << std::fixed << std::setprecision(1)
                          << (candidateUtil * 100) << "% >= threshold" << std::endl;
                continue;
            }

            // Trigger BSS TM
            m_cooldown[cooldownKey] = Simulator::Now();
            offloadedThisRound.insert(staMac);

            std::cout << "[LOAD-BALANCE]   └─ Offloading STA " << staMac << " (RSSI="
                      << std::fixed << std::setprecision(1) << rssi << " dBm) to "
                      << bestCandidate << " (util=" << (candidateUtil * 100) << "%)" << std::endl;

            TriggerBssTm(apIndex, staMac, apBssid, bestCandidate);

            // Estimate utilization reduction (~1/numSTAs)
            double reduction = apMetrics.channelUtilization / staList.size();
            apMetrics.channelUtilization -= reduction;
        }

        // Record load balance check event
        if (m_simEventProducer && m_apSimNodeIdToConfigNodeId)
        {
            uint32_t apConfigId = m_apSimNodeIdToConfigNodeId->count(apIndex)
                                      ? (*m_apSimNodeIdToConfigNodeId)[apIndex]
                                      : apIndex;
            m_simEventProducer->RecordLoadBalanceCheck(
                apConfigId,
                metricsIt->second.channelUtilization,
                m_config.channelUtilThreshold,
                staList.size(),
                offloadedThisRound.size());
        }
    }

    Simulator::Schedule(Seconds(m_config.intervalSec), &LoadBalanceHelper::PeriodicCheck, this);
}

void
LoadBalanceHelper::TriggerBssTm(uint32_t apIndex,
                                 Mac48Address staMac,
                                 Mac48Address apBssid,
                                 Mac48Address targetBssid)
{
    if (!m_bssTmHelpers || apIndex >= m_bssTmHelpers->size())
        return;

    // Get beacons and inject real-time data
    std::vector<BeaconInfo> beacons = m_sniffer->GetBeaconsReceivedBy(staMac);
    for (auto& beacon : beacons)
    {
        for (uint32_t i = 0; i < m_apDevices->GetN(); i++)
        {
            Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice>(m_apDevices->Get(i));
            if (!dev)
                continue;
            Ptr<ApWifiMac> mac = DynamicCast<ApWifiMac>(dev->GetMac());
            if (mac && mac->GetAddress() == beacon.bssid)
            {
                beacon.staCount = mac->GetStaList(0).size();
                beacon.channel = dev->GetPhy()->GetChannelNumber();
                break;
            }
        }
    }

    // Record Kafka event
    if (m_simEventProducer && m_simNodeIdToConfigNodeId && m_apSimNodeIdToConfigNodeId)
    {
        uint32_t apConfigId = m_apSimNodeIdToConfigNodeId->count(apIndex)
                                  ? (*m_apSimNodeIdToConfigNodeId)[apIndex]
                                  : apIndex;
        uint32_t staConfigId = 0;
        for (uint32_t i = 0; i < m_staDevices->GetN(); i++)
        {
            Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice>(m_staDevices->Get(i));
            if (dev && dev->GetMac()->GetAddress() == staMac)
            {
                staConfigId = m_simNodeIdToConfigNodeId->count(i)
                                  ? (*m_simNodeIdToConfigNodeId)[i]
                                  : i;
                break;
            }
        }

        std::ostringstream targetStr;
        targetStr << targetBssid;

        auto metricsIt = m_apMetrics->find(apIndex);
        double util =
            metricsIt != m_apMetrics->end() ? metricsIt->second.channelUtilization * 100.0 : 0.0;

        m_simEventProducer->RecordBssTmRequestSent(
            apConfigId, staConfigId, "LOAD_BALANCING", util, targetStr.str());
    }

    // Send BSS TM
    Ptr<BssTm11vHelper> helper = (*m_bssTmHelpers)[apIndex];
    Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(m_apDevices->Get(apIndex));

    helper->SetStaMac(staMac);
    helper->SetBeaconCache(beacons);

    std::vector<BeaconReportData> emptyReports;
    Simulator::ScheduleNow(
        &BssTm11vHelper::sendRankedCandidates, helper, apDevice, apBssid, staMac, emptyReports);
}

} // namespace ns3
