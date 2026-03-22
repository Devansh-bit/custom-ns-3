#include "sta-channel-hopping-manager.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/double.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-phy-band.h"
#include "ns3/string.h"
#include "ns3/node-list.h"
#include "ns3/mobility-model.h"
#include "ns3/ap-wifi-mac.h"

#include <sstream>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("StaChannelHoppingManager");

NS_OBJECT_ENSURE_REGISTERED(StaChannelHoppingManager);

TypeId
StaChannelHoppingManager::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::StaChannelHoppingManager")
            .SetParent<Object>()
            .SetGroupName("Wifi")
            .AddConstructor<StaChannelHoppingManager>()
            .AddAttribute("ScanningDelay",
                          "Delay before attempting reconnection after disassociation",
                          TimeValue(Seconds(5.0)),
                          MakeTimeAccessor(&StaChannelHoppingManager::m_scanningDelay),
                          MakeTimeChecker())
            .AddAttribute("MinimumSnr",
                          "Minimum SNR threshold for AP selection (dB)",
                          DoubleValue(0.0),
                          MakeDoubleAccessor(&StaChannelHoppingManager::m_minimumSnr),
                          MakeDoubleChecker<double>())
            .AddAttribute("Enabled",
                          "Enable automatic reconnection after disassociation",
                          BooleanValue(true),
                          MakeBooleanAccessor(&StaChannelHoppingManager::m_enabled),
                          MakeBooleanChecker())
            .AddTraceSource("RoamingTriggered",
                            "Trace source fired when roaming is initiated",
                            MakeTraceSourceAccessor(&StaChannelHoppingManager::m_roamingTriggeredTrace),
                            "ns3::StaChannelHoppingManager::RoamingTriggeredCallback");
    return tid;
}

StaChannelHoppingManager::StaChannelHoppingManager()
    : m_staDevice(nullptr),
      m_dualPhySniffer(nullptr),
      m_apDevices(nullptr),
      m_scanningDelay(Seconds(5.0)),
      m_minimumSnr(0.0),
      m_enabled(true),
      m_lastEmergencyReconnect(Seconds(0)),
      m_lastDisassociation(Seconds(0)),
      m_emergencyReconnectCooldown(Seconds(5.0)),
      m_consecutiveHighLossCount(0),
      m_maxConsecutiveHighLoss(5)
{
    NS_LOG_FUNCTION(this);
}

StaChannelHoppingManager::~StaChannelHoppingManager()
{
    NS_LOG_FUNCTION(this);
    if (m_roamingEvent.IsPending())
    {
        Simulator::Cancel(m_roamingEvent);
    }
}

void
StaChannelHoppingManager::SetStaDevice(Ptr<WifiNetDevice> staDevice)
{
    NS_LOG_FUNCTION(this << staDevice);
    m_staDevice = staDevice;

    // Connect to disassociation trace
    Ptr<WifiMac> mac = staDevice->GetMac();
    mac->TraceConnectWithoutContext("DeAssoc",
                                    MakeCallback(&StaChannelHoppingManager::OnDisassociation,
                                                 this));
    mac->TraceConnectWithoutContext("Assoc",
                                    MakeCallback(&StaChannelHoppingManager::OnAssociation, this));
    
    NS_LOG_INFO("StaChannelHoppingManager installed on STA " << mac->GetAddress());
    
    // Connection validation triggered from metrics collection
    NS_LOG_INFO("  └─ Connection validation triggered from metrics collection");
}

void
StaChannelHoppingManager::SetDualPhySniffer(DualPhySnifferHelper* sniffer,
                                            Mac48Address operatingMac)
{
    NS_LOG_FUNCTION(this << sniffer << operatingMac);
    m_dualPhySniffer = sniffer;
    m_dualPhyOperatingMac = operatingMac;
}

void
StaChannelHoppingManager::SetScanningDelay(Time delay)
{
    NS_LOG_FUNCTION(this << delay);
    m_scanningDelay = delay;
}

void
StaChannelHoppingManager::SetMinimumSnr(double minSnr)
{
    NS_LOG_FUNCTION(this << minSnr);
    m_minimumSnr = minSnr;
}

void
StaChannelHoppingManager::SetApDevices(NetDeviceContainer* apDevices)
{
    NS_LOG_FUNCTION(this << apDevices);
    m_apDevices = apDevices;
}

void
StaChannelHoppingManager::Enable(bool enable)
{
    NS_LOG_FUNCTION(this << enable);
    m_enabled = enable;
}

bool
StaChannelHoppingManager::IsEnabled() const
{
    return m_enabled;
}

void
StaChannelHoppingManager::OnDisassociation(Mac48Address bssid)
{
    NS_LOG_FUNCTION(this << bssid);

    if (!m_enabled)
    {
        NS_LOG_INFO("Automatic reconnection disabled, ignoring disassociation");
        return;
    }

    m_lastBssid = bssid;
    m_lastDisassociation = Simulator::Now();  // Record disassociation time for grace period
    NS_LOG_INFO("STA disassociated from " << bssid << ", scheduling roaming attempt in "
                                          << m_scanningDelay.As(Time::S));

    // Cancel any pending roaming event
    if (m_roamingEvent.IsPending())
    {
        Simulator::Cancel(m_roamingEvent);
    }

    // Schedule roaming attempt after scanning delay
    m_roamingEvent =
        Simulator::Schedule(m_scanningDelay, &StaChannelHoppingManager::InitiateRoaming, this);
}

void
StaChannelHoppingManager::OnAssociation(Mac48Address bssid)
{
    NS_LOG_FUNCTION(this << bssid);
    m_lastBssid = bssid;
    NS_LOG_INFO("STA successfully associated with " << bssid);

    // Cancel any pending roaming event
    if (m_roamingEvent.IsPending())
    {
        Simulator::Cancel(m_roamingEvent);
    }
}

Mac48Address
StaChannelHoppingManager::GetCurrentBssid() const
{
    NS_LOG_FUNCTION(this);

    if (!m_staDevice)
    {
        NS_LOG_ERROR("STA device not set");
        return Mac48Address();
    }

    Ptr<WifiMac> staMac = m_staDevice->GetMac();
    Ptr<StaWifiMac> staWifiMac = DynamicCast<StaWifiMac>(staMac);

    if (!staWifiMac)
    {
        NS_LOG_ERROR("Device is not a StaWifiMac");
        return Mac48Address();
    }

    if (!staWifiMac->IsAssociated())
    {
        NS_LOG_DEBUG("STA is not currently associated");
        return Mac48Address();
    }

    return staMac->GetBssid(0);
}

Mac48Address
StaChannelHoppingManager::SelectBestAp()
{
    NS_LOG_FUNCTION(this);

    if (!m_dualPhySniffer)
    {
        NS_LOG_ERROR("DualPhySniffer not configured");
        return Mac48Address();
    }

    // Get STA's current band - ns-3 PHY cannot switch bands at runtime
    Ptr<WifiPhy> staPhy = m_staDevice->GetPhy();
    WifiPhyBand staBand = staPhy->GetPhyBand();

    // Query beacon cache for all APs seen by this receiver
    std::vector<BeaconInfo> beacons = m_dualPhySniffer->GetBeaconsReceivedBy(m_dualPhyOperatingMac);

    NS_LOG_INFO("Found " << beacons.size() << " APs in beacon cache");

    // Find AP with best SNR
    Mac48Address bestBssid;
    double bestSnr = m_minimumSnr;
    uint8_t bestChannel = 0;

    for (const auto& beacon : beacons)
    {
        NS_LOG_DEBUG("AP " << beacon.bssid << " on channel " << (int)beacon.channel << ": SNR="
                           << beacon.snr << " dB, RSSI=" << beacon.rssi << " dBm, beacons="
                           << beacon.beaconCount);

        // BAND FILTERING: Skip APs on different band (ns-3 PHY cannot switch bands)
        WifiPhyBand beaconBand = (beacon.channel >= 1 && beacon.channel <= 14) ?
                                  WIFI_PHY_BAND_2_4GHZ : WIFI_PHY_BAND_5GHZ;
        if (beaconBand != staBand)
        {
            NS_LOG_DEBUG("Skipping AP " << beacon.bssid << " on channel " << (int)beacon.channel
                         << " - different band");
            continue;
        }

        // Skip current AP (if still associated)
        Mac48Address currentBssid = GetCurrentBssid();
        if (!(currentBssid == Mac48Address()) && beacon.bssid == currentBssid)
        {
            NS_LOG_DEBUG("Skipping current AP " << beacon.bssid);
            continue;
        }

        // Skip APs below minimum SNR threshold
        if (beacon.snr < m_minimumSnr)
        {
            NS_LOG_DEBUG("AP " << beacon.bssid << " SNR " << beacon.snr
                               << " dB below minimum threshold " << m_minimumSnr << " dB");
            continue;
        }

        // Select AP with highest SNR
        if (beacon.snr > bestSnr)
        {
            bestSnr = beacon.snr;
            bestBssid = beacon.bssid;
            bestChannel = beacon.channel;
        }
    }

    if (bestBssid == Mac48Address())
    {
        NS_LOG_WARN("No suitable AP found for roaming");
        return Mac48Address();
    }

    NS_LOG_INFO("Selected best AP: " << bestBssid << " on channel " << (int)bestChannel
                                     << " with SNR " << bestSnr << " dB");

    return bestBssid;
}

void
StaChannelHoppingManager::InitiateRoaming()
{
    NS_LOG_FUNCTION(this);

    if (!m_staDevice)
    {
        NS_LOG_ERROR("STA device not set");
        return;
    }

    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());
    if (!staMac)
    {
        NS_LOG_ERROR("Failed to cast to StaWifiMac");
        return;
    }

    // Check if already associated (e.g., BSS TM or another mechanism reconnected during delay)
    if (staMac->IsAssociated())
    {
        NS_LOG_INFO("STA already associated with " << staMac->GetBssid(0) << ", skipping roaming");
        return;
    }

    // Select best AP
    Mac48Address targetBssid = SelectBestAp();

    if (targetBssid == Mac48Address())
    {
        NS_LOG_WARN("No suitable AP found, will retry after another scanning delay of "
                    << m_scanningDelay.As(Time::S));

        // Schedule another roaming attempt after scanning delay
        m_roamingEvent =
            Simulator::Schedule(m_scanningDelay, &StaChannelHoppingManager::InitiateRoaming, this);
        return;
    }

    // Get target AP info from beacon cache
    std::vector<BeaconInfo> beacons = m_dualPhySniffer->GetBeaconsReceivedBy(m_dualPhyOperatingMac);

    BeaconInfo* targetInfo = nullptr;
    for (auto& beacon : beacons)
    {
        if (beacon.bssid == targetBssid)
        {
            targetInfo = &beacon;
            break;
        }
    }

    if (!targetInfo)
    {
        NS_LOG_ERROR("Target AP " << targetBssid << " not found in beacon cache");
        return;
    }

    // Fire trace
    m_roamingTriggeredTrace(Simulator::Now(),
                            m_staDevice->GetMac()->GetAddress(),
                            m_lastBssid,
                            targetBssid,
                            targetInfo->snr);

    NS_LOG_INFO("Initiating roaming to " << targetBssid << " on channel "
                                         << (int)targetInfo->channel << " (SNR=" << targetInfo->snr
                                         << " dB)");

    // Get target channel info
    uint8_t targetChannel = targetInfo->channel;
    WifiPhyBand targetBand = (targetChannel >= 1 && targetChannel <= 14) ?
                             WIFI_PHY_BAND_2_4GHZ : WIFI_PHY_BAND_5GHZ;

    // Safety check: Verify target AP is on same band as STA
    // ns-3 PHY cannot switch bands at runtime
    Ptr<WifiPhy> staPhy = m_staDevice->GetPhy();
    WifiPhyBand staBand = staPhy->GetPhyBand();
    if (targetBand != staBand)
    {
        NS_LOG_ERROR("Target AP " << targetBssid << " on channel " << (int)targetChannel
                     << " is on different band - cannot switch bands at runtime!");
        // Schedule another roaming attempt
        m_roamingEvent =
            Simulator::Schedule(m_scanningDelay, &StaChannelHoppingManager::InitiateRoaming, this);
        return;
    }

    // Switch STA to target channel BEFORE initiating association
    // SwitchChannel handles band/width automatically from channel number
    if (staPhy && staPhy->GetChannelNumber() != targetChannel)
    {
        NS_LOG_INFO("Switching STA from channel " << (int)staPhy->GetChannelNumber()
                    << " to " << (int)targetChannel);
        staMac->SwitchChannel(targetChannel);
    }

    // STA is not associated (we checked earlier), use AssociateToAp
    staMac->AssociateToAp(targetBssid, targetChannel, targetBand);
}

// ============================================================================
// HEALTH CHECK IMPLEMENTATION
// ============================================================================



void
StaChannelHoppingManager::PerformHealthCheck(double packetLoss)
{
    NS_LOG_FUNCTION(this << packetLoss);

    if (!m_enabled || !m_staDevice)
    {
        return;  // Don't reschedule if disabled
    }

    // Grace period: Skip health checks during initial association phase
    // STAs need time to complete their initial association (scanning, auth, assoc)
    if (Simulator::Now() < m_scanningDelay)
    {
        NS_LOG_DEBUG("Health check skipped: within initial grace period (t="
                     << Simulator::Now().GetSeconds() << "s < "
                     << m_scanningDelay.GetSeconds() << "s)");
        return;
    }

    // NEW APPROACH: Check if STA can hear the AP by querying RSSI
    // If GetMostRecentRssi() returns nullopt, STA hasn't heard from AP recently
    // This is shown as "STA View=0.00 dBm" in metrics - the orphan signature!

    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());
    if (!staMac)
    {
        return;
    }

    // Check if STA is associated
    if (!staMac->IsAssociated())
    {
        // Grace period after disassociation: Let normal roaming (InitiateRoaming) happen first
        // Only trigger connection recovery if normal roaming hasn't worked
        Time gracePeriod = m_scanningDelay + Seconds(2.0);  // scanningDelay + buffer for association
        if (Simulator::Now() - m_lastDisassociation < gracePeriod)
        {
            NS_LOG_DEBUG("Health check: STA " << m_staDevice->GetMac()->GetAddress()
                         << " not associated, but within post-disassociation grace period ("
                         << (Simulator::Now() - m_lastDisassociation).GetSeconds() << "s < "
                         << gracePeriod.GetSeconds() << "s) - letting normal roaming proceed");
            return;
        }

        // Configurable cooldown for association to complete
        if (Simulator::Now() - m_lastEmergencyReconnect < m_emergencyReconnectCooldown)
        {
            NS_LOG_DEBUG("Health check: STA " << m_staDevice->GetMac()->GetAddress()
                         << " not associated, but within recovery cooldown ("
                         << (Simulator::Now() - m_lastEmergencyReconnect).GetSeconds() << "s < "
                         << m_emergencyReconnectCooldown.GetSeconds() << "s) - skipping");
            return;
        }

        NS_LOG_DEBUG("Health check: STA " << m_staDevice->GetMac()->GetAddress()
                     << " is not associated, triggering connection recovery");

        // If STA is not associated, it's effectively orphaned.
        // We must force it to try and connect.
        EmergencyReconnect();
        return;
    }

    // Get the AP this STA is associated with
    Mac48Address apBssid = staMac->GetBssid(0);  // Link ID 0

    // Query STA's RemoteStationManager for RSSI of the AP
    Ptr<WifiRemoteStationManager> staStationMgr = m_staDevice->GetRemoteStationManager();
    if (!staStationMgr)
    {
        return;
    }

    auto staRssiOpt = staStationMgr->GetMostRecentRssi(apBssid);

    // ORPHAN DETECTION: If no RSSI available, STA cannot hear AP!
    if (!staRssiOpt.has_value())
    {
        // Configurable cooldown period
        if (Simulator::Now() - m_lastEmergencyReconnect < m_emergencyReconnectCooldown)
        {
            NS_LOG_DEBUG("Health check: STA " << m_staDevice->GetMac()->GetAddress()
                         << " orphaned but within reconnect cooldown - skipping");
            return;
        }

        // STA is associated but cannot hear the AP
        // This is the "STA View=0.00 dBm" condition
        NS_LOG_WARN("Health check: STA " << m_staDevice->GetMac()->GetAddress()
                    << " cannot hear AP " << apBssid << " (No RSSI) - ORPHANED!");
        NS_LOG_WARN("╔════════════════════════════════════════════════════════════╗");
        NS_LOG_WARN("║  ORPHANED STA DETECTED - INITIATING RECOVERY               ║");
        NS_LOG_WARN("╠════════════════════════════════════════════════════════════╣");
        NS_LOG_WARN("║  STA: " << m_staDevice->GetMac()->GetAddress());
        NS_LOG_WARN("║  Associated to AP: " << apBssid);
        NS_LOG_WARN("║  STA View RSSI: NONE (cannot hear AP!)");
        NS_LOG_WARN("║  Initiating connection recovery...");
        NS_LOG_WARN("╚════════════════════════════════════════════════════════════╝");

        // Trigger connection recovery
        EmergencyReconnect();
    }
    else
    {
        // STA can hear AP - check packet loss
        double rssi = static_cast<double>(staRssiOpt.value());

        // STALE CONNECTION DETECTION: High packet loss despite valid RSSI
        // User requirement: "constantly exactly 100%"
        // Using 0.999 to account for floating point epsilon and EWMA convergence
        if (packetLoss > 0.999) // > 99.9% loss (effectively 100%)
        {
            m_consecutiveHighLossCount++;
            NS_LOG_WARN("Health check: STA " << m_staDevice->GetMac()->GetAddress()
                        << " high packet loss detected: " << (packetLoss * 100.0) << "% "
                        << "(Count: " << m_consecutiveHighLossCount << "/" << m_maxConsecutiveHighLoss << ")");

            if (m_consecutiveHighLossCount >= m_maxConsecutiveHighLoss)
            {
                // Configurable cooldown period
                if (Simulator::Now() - m_lastEmergencyReconnect < m_emergencyReconnectCooldown)
                {
                    NS_LOG_DEBUG("Health check: STA " << m_staDevice->GetMac()->GetAddress()
                                 << " stale connection but within reconnect cooldown - skipping");
                    return;
                }

                NS_LOG_WARN("╔════════════════════════════════════════════════════════════╗");
                NS_LOG_WARN("║  STALE CONNECTION DETECTED - INITIATING RECOVERY           ║");
                NS_LOG_WARN("╠════════════════════════════════════════════════════════════╣");
                NS_LOG_WARN("║  STA: " << m_staDevice->GetMac()->GetAddress());
                NS_LOG_WARN("║  Associated to AP: " << apBssid);
                NS_LOG_WARN("║  Packet Loss: " << (packetLoss * 100.0) << "% (Persistent)");
                NS_LOG_WARN("║  Initiating connection recovery...");
                NS_LOG_WARN("╚════════════════════════════════════════════════════════════╝");

                EmergencyReconnect();
                m_consecutiveHighLossCount = 0; // Reset after triggering
            }
        }
        else
        {
            if (m_consecutiveHighLossCount > 0)
            {
                NS_LOG_DEBUG("Health check: STA " << m_staDevice->GetMac()->GetAddress()
                             << " packet loss recovered (" << (packetLoss * 100.0) << "%), resetting counter");
            }
            m_consecutiveHighLossCount = 0;

            NS_LOG_DEBUG("Health check: STA " << m_staDevice->GetMac()->GetAddress()
                         << " → AP " << apBssid
                         << " RSSI: " << rssi << " dBm, Loss: " << (packetLoss * 100.0) << "% (healthy)");
        }
    }

    // NOTE: No longer rescheduling - health check is now triggered from metrics collection
}

void
StaChannelHoppingManager::EmergencyReconnect()
{
    NS_LOG_FUNCTION(this);

    if (!m_staDevice)
    {
        NS_LOG_ERROR("EmergencyReconnect: No STA device set");
        return;
    }

    // Record timestamp for cooldown tracking
    m_lastEmergencyReconnect = Simulator::Now();

    // Get STA MAC
    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());
    if (!staMac)
    {
        NS_LOG_ERROR("EmergencyReconnect: Could not get STA MAC");
        return;
    }

    // Get current BSSID to exclude from candidates
    Mac48Address currentBssid = staMac->GetBssid(0);
    bool hasCurrentBssid = !(currentBssid == Mac48Address());

    std::cout << "[STA-RECOVERY] " << Simulator::Now().GetSeconds() << "s: "
              << "STA " << m_staDevice->GetMac()->GetAddress()
              << " initiating connection recovery";
    if (hasCurrentBssid)
    {
        std::cout << " (excluding current AP " << currentBssid << ")";
    }
    std::cout << std::endl;

    // Structure to hold AP candidate info
    struct ApCandidate {
        Mac48Address bssid;
        double distance;
        double rssi;        // From beacon cache if available
        uint8_t channel;
        WifiPhyBand band;
        Ptr<WifiNetDevice> device;
    };
    std::vector<ApCandidate> apCandidates;

    Ptr<Node> staNode = m_staDevice->GetNode();
    Ptr<MobilityModel> staMobility = staNode->GetObject<MobilityModel>();

    if (!staMobility)
    {
        NS_LOG_ERROR("EmergencyReconnect: STA has no mobility model");
        return;
    }

    Vector staPos = staMobility->GetPosition();

    // Get STA's current band - ns-3 PHY cannot switch bands at runtime
    Ptr<WifiPhy> staPhy = m_staDevice->GetPhy();
    WifiPhyBand staBand = staPhy->GetPhyBand();
    std::string staBandStr = (staBand == WIFI_PHY_BAND_2_4GHZ) ? "2.4GHz" :
                             (staBand == WIFI_PHY_BAND_5GHZ) ? "5GHz" : "6GHz";

    std::cout << "[STA-RECOVERY]   └─ STA band: " << staBandStr
              << " (only considering APs on same band)" << std::endl;

    // Use direct AP device container instead of NodeList (no fallback - require it to be set)
    if (!m_apDevices || m_apDevices->GetN() == 0)
    {
        std::cout << "[STA-RECOVERY]   └─ FATAL: No AP device container set! "
                  << "Call SetApDevices() before simulation." << std::endl;
        return;
    }

    // Iterate through the known AP device container (much more efficient than NodeList)
    uint32_t skippedExcluded = 0;
    uint32_t skippedNoMobility = 0;
    uint32_t skippedWrongBand = 0;

    for (uint32_t i = 0; i < m_apDevices->GetN(); i++)
    {
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(m_apDevices->Get(i));
        if (!apDev) continue;

        Mac48Address apBssid = apDev->GetMac()->GetAddress();

        // Exclude current AP from candidates
        if (hasCurrentBssid && apBssid == currentBssid)
        {
            skippedExcluded++;
            continue;
        }

        // Get channel info first for band filtering
        Ptr<WifiPhy> apPhy = apDev->GetPhy();
        uint8_t channel = apPhy->GetChannelNumber();
        WifiPhyBand band = apPhy->GetPhyBand();

        // BAND FILTERING: Skip APs on different band (ns-3 PHY cannot switch bands)
        if (band != staBand)
        {
            skippedWrongBand++;
            NS_LOG_DEBUG("  Skipping AP " << apBssid << " on channel " << (int)channel
                         << " - different band");
            continue;
        }

        // Get AP position from node's mobility model
        Ptr<Node> apNode = apDev->GetNode();
        Ptr<MobilityModel> apMobility = apNode->GetObject<MobilityModel>();
        if (!apMobility)
        {
            skippedNoMobility++;
            continue;
        }

        // Calculate distance (primary selection criterion)
        Vector apPos = apMobility->GetPosition();
        double distance = CalculateDistance(staPos, apPos);

        // Distance-based selection doesn't need RSSI, but include for logging
        ApCandidate candidate = {apBssid, distance, -100.0, channel, band, apDev};
        apCandidates.push_back(candidate);

        NS_LOG_DEBUG("  Found AP " << apBssid << " distance=" << distance
                     << "m, channel=" << (int)channel);
    }

    std::cout << "[STA-RECOVERY]   └─ Searched " << m_apDevices->GetN()
              << " APs, found " << apCandidates.size() << " candidates"
              << " (excluded=" << skippedExcluded << ", wrongBand=" << skippedWrongBand
              << ", noMobility=" << skippedNoMobility << ")"
              << std::endl;

    if (apCandidates.empty())
    {
        std::cout << "[STA-RECOVERY]   └─ ERROR: No suitable APs found!" << std::endl;
        if (hasCurrentBssid)
        {
            std::cout << "[STA-RECOVERY]   └─ All APs excluded (only current AP available). "
                      << "Will retry on next health check." << std::endl;
        }
        return;
    }

    // Sort by DISTANCE (closest first) - distance-based selection
    std::sort(apCandidates.begin(), apCandidates.end(),
              [](const ApCandidate& a, const ApCandidate& b) {
                  return a.distance < b.distance;  // Closest AP first
              });

    ApCandidate& bestAp = apCandidates[0];

    std::cout << "[STA-RECOVERY]   └─ Selected AP: " << bestAp.bssid
              << " (distance=" << std::fixed << std::setprecision(1) << bestAp.distance << "m)"
              << std::endl;
    std::cout << "[STA-RECOVERY]   └─ Target channel: " << (uint32_t)bestAp.channel
              << std::endl;

    // Configure association attributes for recovery scenarios
    staMac->SetAttribute("AssocRequestTimeout", TimeValue(Seconds(2.0)));
    staMac->SetAttribute("MaxMissedBeacons", UintegerValue(20));

    // IEEE 802.11 disassociation: notify old AP before switching channels
    if (staMac->IsAssociated() && hasCurrentBssid)
    {
        NS_LOG_UNCOND("[STA-RECOVERY]   └─ Sending deassociation to old AP "
                      << currentBssid << " (best-effort)");
        // Reason code 8 = "Disassociated because sending STA is leaving (or has left) BSS"
        staMac->SendDeassociation(currentBssid, 8);
    }

    // Schedule the rest after 50ms delay for deassoc frame transmission
    // This matches regular roaming behavior (sta-wifi-mac.cc:2354-2356)
    Simulator::Schedule(MilliSeconds(50),
                        &StaChannelHoppingManager::CompleteEmergencyReconnect,
                        this,
                        bestAp.bssid,
                        bestAp.channel,
                        bestAp.band);
}

void
StaChannelHoppingManager::CompleteEmergencyReconnect(Mac48Address targetBssid,
                                                      uint8_t targetChannel,
                                                      WifiPhyBand targetBand)
{
    NS_LOG_FUNCTION(this << targetBssid << (uint32_t)targetChannel << targetBand);

    if (!m_staDevice)
    {
        NS_LOG_ERROR("CompleteEmergencyReconnect: No STA device set");
        return;
    }

    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());
    if (!staMac)
    {
        NS_LOG_ERROR("CompleteEmergencyReconnect: Could not get STA MAC");
        return;
    }

    // Safety check: Verify target AP is on same band as STA
    // ns-3 PHY cannot switch bands at runtime
    Ptr<WifiPhy> staPhy = m_staDevice->GetPhy();
    WifiPhyBand staBand = staPhy->GetPhyBand();
    if (targetBand != staBand)
    {
        std::cout << "[STA-RECOVERY]   └─ ERROR: Target AP on different band - cannot switch!"
                  << std::endl;
        NS_LOG_ERROR("CompleteEmergencyReconnect: Target band mismatch - STA on "
                     << (staBand == WIFI_PHY_BAND_2_4GHZ ? "2.4GHz" : "5GHz")
                     << ", target on "
                     << (targetBand == WIFI_PHY_BAND_2_4GHZ ? "2.4GHz" : "5GHz"));
        return;
    }

    // Switch STA to target channel
    if (staPhy && staPhy->GetChannelNumber() != targetChannel)
    {
        std::cout << "[STA-RECOVERY]   └─ Switching STA from channel "
                  << (uint32_t)staPhy->GetChannelNumber() << " to " << (uint32_t)targetChannel
                  << std::endl;
        staMac->SwitchChannel(targetChannel);
    }

    // Use the appropriate method based on association state:
    // - If associated: use InitiateRoaming() which handles the associated→roaming transition
    // - If not associated: use AssociateToAp() for direct association
    // Note: SendDeassociation() only sends a frame, doesn't change internal state!
    if (staMac->IsAssociated())
    {
        std::cout << "[STA-RECOVERY]   └─ Roaming to " << targetBssid << std::endl;
        staMac->InitiateRoaming(targetBssid, targetChannel, targetBand);
    }
    else
    {
        std::cout << "[STA-RECOVERY]   └─ Associating to " << targetBssid << std::endl;
        staMac->AssociateToAp(targetBssid, targetChannel, targetBand);
    }
    m_lastBssid = targetBssid;
}

} // namespace ns3
