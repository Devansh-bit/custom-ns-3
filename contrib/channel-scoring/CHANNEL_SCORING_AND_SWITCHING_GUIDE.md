# Channel Scoring and Automatic Channel Switching Guide

## Overview

This guide documents the complete implementation of **Automatic Channel Selection (ACS)** and **Dynamic Channel Switching (DCS)** in the ns-3 simulation, including integration with the Virtual Interferer system.

The system enables:
- **Channel Scoring**: Multi-factor evaluation of channel quality
- **Non-WiFi Triggered Switching**: Automatic channel switch when interference exceeds threshold
- **DFS Radar Handling**: Immediate channel evacuation on radar detection
- **Sequential AP Allocation**: Coordinated channel assignment across multiple APs

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        EVENT SOURCES                                     │
├─────────────────────────────────────────────────────────────────────────┤
│  VirtualInterferer        WifiCcaMonitor         RadarInterferer        │
│  (nonWifiCca effect)    (CCA measurements)      (DFS trigger)           │
└───────────┬─────────────────┬─────────────────────────┬─────────────────┘
            │                 │                         │
            ▼                 ▼                         ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                       OnChannelUtilization()                             │
│  - Combines RF CCA + Virtual Interferer effects                         │
│  - Updates BSS Load IE (beacons)                                        │
│  - Checks non-WiFi threshold                                            │
└───────────────────────────────────┬─────────────────────────────────────┘
                                    │
            ┌───────────────────────┼───────────────────────┐
            │                       │                       │
            ▼                       │                       ▼
┌───────────────────────┐           │         ┌─────────────────────────┐
│  QueueApForScoring()  │           │         │ HandleDfsRadarDetection │
│  (Per-AP reactive)    │           │         │ (Immediate evacuation)  │
└───────────┬───────────┘           │         └───────────┬─────────────┘
            │                       │                     │
            ▼                       ▼                     ▼
┌───────────────────────┐   ┌───────────────────┐   ┌─────────────────┐
│ ProcessApScoringQueue │   │ PerformChannel    │   │ DFS Blacklist   │
│ (Sequential)          │   │ Scoring           │   │ Management      │
└───────────┬───────────┘   │ (All APs)         │   └────────┬────────┘
            │               └─────────┬─────────┘            │
            ▼                         │                      │
┌───────────────────────┐             │                      │
│ PerformChannelScoring │◄────────────┘                      │
│ ForAp()               │                                    │
└───────────┬───────────┘                                    │
            │                                                │
            ▼                                                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     ChannelScoringHelper                                 │
│  - Calculates scores for all channels                                   │
│  - Factors: BSSID, RSSI, Non-WiFi, Overlap                              │
│  - Returns sorted channel list                                          │
└───────────────────────────────────┬─────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     ApWifiMac::SwitchChannel()                           │
│  - Executes the actual channel switch                                   │
│  - Updates g_apMetrics                                                  │
│  - Sets cooldown timer                                                  │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Key Data Structures

### Global State Variables

```cpp
// Channel scoring configuration and helper
ChannelScoringConfig g_channelScoringConfig;
ChannelScoringHelper g_channelScoringHelper;
bool g_channelScoringEnabled = true;

// Non-WiFi trigger threshold
double g_nonWifiThreshold = 65.0;  // Trigger when non-WiFi > 65%

// DFS blacklist management
Time g_dfsBlacklistDuration = Seconds(30);  // Blacklist duration after radar
std::set<uint8_t> g_dfsChannels5GHz = {52, 56, 60, 64, 100, 104, ...};

// Per-AP cooldown tracking
std::map<uint32_t, Time> g_lastChannelSwitch;  // Last switch time per AP
Time g_channelSwitchCooldown = Seconds(15);    // Wait for beacon cache refresh

// Scoring queue for sequential processing
std::queue<uint32_t> g_apScoringQueue;
std::set<uint8_t> g_pendingAllocations;  // Channels already allocated in batch
```

### GlobalChannelRanking Structure

```cpp
struct GlobalChannelRanking {
    std::vector<uint8_t> ranked5GHz;       // Best-to-worst for 5 GHz
    std::vector<uint8_t> ranked24GHz;      // Best-to-worst for 2.4 GHz
    std::set<uint8_t> dfsBlacklist;        // Channels with recent radar
    std::map<uint8_t, Time> blacklistExpiry; // When blacklist expires
    Time lastUpdate;
};
```

### ChannelScore Structure

```cpp
struct ChannelScore {
    uint8_t channel = 0;
    double bssidScore = 0.0;      // Score from neighbor count (0-100)
    double rssiScore = 0.0;       // Score from avg RSSI (0-100)
    double nonWifiScore = 0.0;    // Score from non-WiFi interference (0-100)
    double overlapScore = 0.0;    // Score from spectral overlap (0-100)
    double totalScore = 0.0;      // Weighted sum
    bool discarded = false;       // True if exceeds non-WiFi threshold
    uint32_t rank = 0;            // 1 = best
};
```

---

## Event Flow #1: Non-WiFi Threshold Triggered Switching

This is the primary mechanism for reactive channel switching when virtual interferer effects exceed the threshold.

### Step 1: CCA Monitor Callback

The `WifiCcaMonitor` periodically measures channel utilization and calls `OnChannelUtilization()`:

```cpp
void OnChannelUtilization(uint32_t nodeId, double timestamp, double totalUtil,
                          double wifiUtil, double nonWifiUtil, ...)
{
    ApMetrics& ap = g_apMetrics[nodeId];

    // Get VI non-WiFi contribution
    double viNonWifi = 0.0;
    if (g_virtualInterfererEnabled) {
        auto viEnv = VirtualInterfererEnvironment::Get();
        Vector pos = /* get AP position */;
        InterferenceEffect effect = viEnv->GetAggregateEffect(pos, ap.channel);
        viNonWifi = effect.nonWifiCcaPercent;  // 0-100%
    }

    // Proportional scaling: VI takes priority
    // effectiveNonWifi = scaled(RF nonWifi) + viNonWifi
    // effectiveTotal = effectiveWifi + effectiveNonWifi

    // Update BSS Load IE in beacons
    ApWifiMac::SetNonWifiChannelUtilization(ap.bssid, nonWifiScaled);

    // CHECK THRESHOLD - trigger scoring if exceeded
    if (g_channelScoringEnabled && effectiveNonWifi > g_nonWifiThreshold) {
        // Check cooldown first
        if (not_in_cooldown) {
            QueueApForScoring(nodeId);
        }
    }
}
```

### Step 2: AP Scoring Queue

APs are queued to prevent simultaneous scoring conflicts:

```cpp
void QueueApForScoring(uint32_t nodeId) {
    // Skip if already in queue
    if (already_queued) return;

    g_apScoringQueue.push(nodeId);

    // Start processing if queue was empty
    if (wasEmpty) {
        Simulator::ScheduleNow(&ProcessApScoringQueue);
    }
}

void ProcessApScoringQueue() {
    if (g_apScoringQueue.empty()) {
        g_pendingAllocations.clear();  // Reset for next batch
        return;
    }

    uint32_t nodeId = g_apScoringQueue.front();
    g_apScoringQueue.pop();

    PerformChannelScoringForAp(nodeId);

    // Process next AP with delay
    if (!g_apScoringQueue.empty()) {
        Simulator::Schedule(MilliSeconds(50), &ProcessApScoringQueue);
    }
}
```

### Step 3: Per-AP Channel Scoring

```cpp
void PerformChannelScoringForAp(uint32_t triggeredNodeId) {
    // Check cooldown
    if (in_cooldown) return;

    // Update scan data
    ProcessBeaconMeasurements();

    // Calculate scores
    auto scores = g_channelScoringHelper.CalculateScores(
        apMetrics.scanningChannelData, apMetrics.channelWidth);

    // Find best available channel
    uint8_t bestCh = 0;
    for (const auto& score : scores) {
        if (score.discarded) continue;
        if (!IsChannelInBand(score.channel, apMetrics.band)) continue;
        if (g_globalRanking.dfsBlacklist.count(score.channel) > 0) continue;
        bestCh = score.channel;
        break;
    }

    // Execute switch
    if (bestCh != 0 && bestCh != apMetrics.channel) {
        g_pendingAllocations.insert(bestCh);

        Simulator::Schedule(MilliSeconds(100), [...]() {
            apMac->SwitchChannel(bestCh);
            apMetrics.channel = bestCh;
            g_lastChannelSwitch[nodeId] = Simulator::Now();
        });
    }
}
```

---

## Event Flow #2: DFS Radar Detection

When radar is detected on a DFS channel, immediate evacuation is required.

### Step 1: DFS Trigger from Virtual Interferer

The radar interferer triggers DFS detection via trace callback:

```cpp
// In setup:
viEnv->TraceConnectWithoutContext("DfsTrigger",
    MakeCallback(&HandleDfsRadarDetection));
```

### Step 2: DFS Handler

```cpp
void HandleDfsRadarDetection(uint32_t nodeId, uint8_t receiverChannel,
                              uint8_t dfsChannel)
{
    // Check cooldown for this channel
    if (recently_detected) return;
    g_lastDfsDetection[receiverChannel] = now;

    // Get ALL affected channels (wideband radar)
    std::set<uint8_t> affectedChannels;
    if (g_radarInterferer) {
        affectedChannels = g_radarInterferer->GetCurrentlyAffectedChannels();
    } else {
        affectedChannels.insert(dfsChannel);
    }

    // Add all affected channels to blacklist
    for (uint8_t ch : affectedChannels) {
        g_globalRanking.dfsBlacklist.insert(ch);
        g_globalRanking.blacklistExpiry[ch] = now + g_dfsBlacklistDuration;
    }

    // Check ALL APs - switch any on affected channels
    for (auto& [apNodeId, apMetrics] : g_apMetrics) {
        if (affectedChannels.count(apMetrics.channel) > 0) {
            // Get optimal backup using THIS AP's scan data
            uint8_t backupCh = GetOptimalBackupForAp(apNodeId,
                                                      g_globalRanking.dfsBlacklist);
            if (backupCh != 0) {
                // IMMEDIATE switch (no delay)
                apMac->SwitchChannel(backupCh);
                apMetrics.channel = backupCh;
                g_lastChannelSwitch[apNodeId] = now;
            }
        }
    }

    // Trigger full channel scoring to update rankings
    Simulator::ScheduleNow(&PerformChannelScoring);
}
```

### Step 3: Backup Channel Selection

```cpp
uint8_t GetOptimalBackupForAp(uint32_t apNodeId,
                               const std::set<uint8_t>& blacklistedChannels)
{
    ApMetrics& apMetrics = g_apMetrics[apNodeId];

    // Calculate scores for this AP
    auto scores = g_channelScoringHelper.CalculateScores(
        apMetrics.scanningChannelData, apMetrics.channelWidth);

    // Find best channel not in blacklist
    for (const auto& score : scores) {
        if (score.discarded) continue;
        if (!IsChannelInBand(score.channel, apMetrics.band)) continue;
        if (blacklistedChannels.count(score.channel) > 0) continue;
        if (score.channel == apMetrics.channel) continue;
        return score.channel;
    }

    // Fallback: try non-DFS channels
    for (uint8_t ch : g_nonDfsChannels5GHz) {
        if (blacklistedChannels.count(ch) == 0 &&
            IsChannelInBand(ch, apMetrics.band)) {
            return ch;
        }
    }

    return 0;  // No backup available
}
```

---

## Event Flow #3: Periodic Channel Scoring (Optional)

When enabled, periodic scoring evaluates all APs:

```cpp
void PerformChannelScoring() {
    // Debounce
    if (now - g_lastScoringRun < g_scoringDebounce) return;
    g_lastScoringRun = now;

    ProcessBeaconMeasurements();

    std::set<uint8_t> allocatedChannels;

    for (const auto& [nodeId, apMetrics] : g_apMetrics) {
        // Skip if in cooldown
        if (in_cooldown) {
            allocatedChannels.insert(apMetrics.channel);
            continue;
        }

        // Calculate and display scores
        auto scores = g_channelScoringHelper.CalculateScores(...);

        // Find best available (exclude already allocated AND blacklisted)
        uint8_t bestCh = FindBestAvailable(scores, apMetrics.band,
                                           allocatedChannels,
                                           g_globalRanking.dfsBlacklist);

        allocatedChannels.insert(bestCh);

        if (bestCh != apMetrics.channel) {
            // Schedule staggered switch
            uint32_t delayMs = 100 + (nodeId * 50);
            Simulator::Schedule(MilliSeconds(delayMs), [...]() {
                apMac->SwitchChannel(bestCh);
            });
        }
    }

    // Update global ranking
    UpdateGlobalRanking(scores);

    // Check for expired blacklist entries
    CheckDfsBlacklistExpiry();
}
```

---

## Channel Scoring Algorithm

### Scoring Factors

| Factor | Weight (Default) | Description |
|--------|------------------|-------------|
| BSSID Count | 0.5 | Number of neighbor APs on channel |
| RSSI | 0.5 | Average signal strength of neighbors |
| Non-WiFi | 0.5 | Non-WiFi interference percentage |
| Overlap | 0.5 | Spectral overlap with neighbors |

### Score Calculation

```cpp
// BSSID Score: More neighbors = worse
double CalculateBssidScore(uint32_t bssidCount) {
    return min(bssidCount * 50.0, 100.0);  // 0->0, 1->50, 2+->100
}

// RSSI Score: Stronger neighbors = worse
double CalculateRssiScore(neighbors) {
    double avgRssi = sum(rssi) / count;
    return ((avgRssi + 90.0) / 60.0) * 100.0;  // -90dBm->0, -30dBm->100
}

// Non-WiFi Score: More interference = worse
double CalculateNonWifiScore(neighbors, scanningRadioNonWifiUtil,
                              threshold, &shouldDiscard) {
    double nonWifiPercent;
    if (!neighbors.empty()) {
        // Use BSS Load IE from neighbors
        nonWifiPercent = max(neighbor.nonWifiUtil) / 255.0 * 100.0;
    } else {
        // Use scanning radio CCA measurement
        nonWifiPercent = scanningRadioNonWifiUtil * 100.0;
    }

    if (nonWifiPercent > threshold) {
        shouldDiscard = true;  // Channel rejected!
    }

    return min((nonWifiPercent / threshold) * 100.0, 100.0);
}

// Overlap Score: More spectral overlap = worse
double CalculateOverlapScore(targetChannel, targetWidth, allChannelData) {
    // 2.4 GHz: channels 5+ apart = no overlap
    // 5 GHz: frequency-based overlap calculation
    return totalOverlap / 2.0 * 100.0;
}

// Total Score (lower = better)
totalScore = weightBssid * bssidScore +
             weightRssi * rssiScore +
             weightNonWifi * nonWifiScore +
             weightOverlap * overlapScore;
```

### Discard Threshold

Channels with non-WiFi utilization above `nonWifiDiscardThreshold` (default 40%) are completely rejected:

```cpp
ChannelScoringConfig config;
config.nonWifiDiscardThreshold = 40.0;  // Reject if >40% non-WiFi
```

---

## Virtual Interferer Integration

### How VI Effects Flow to Channel Scoring

```
VirtualInterferer               VirtualInterfererEnvironment
     │                                    │
     │ CalculateEffect()                  │
     └───────────────────────────────────►│
                                          │
                                          │ GetAggregateEffect(pos, channel)
     OnChannelUtilization() ◄─────────────┤
           │                              │
           │ viNonWifi = effect.nonWifiCcaPercent
           │
           ▼
     Proportional Scaling
           │
           │ effectiveNonWifi = scaled(RF) + viNonWifi
           │
           ▼
     BSS Load IE Update
     ApWifiMac::SetNonWifiChannelUtilization()
           │
           ▼
     Threshold Check
           │
           │ if (effectiveNonWifi > g_nonWifiThreshold)
           │     QueueApForScoring(nodeId)
           │
           ▼
     ProcessBeaconMeasurements()
           │
           │ // For each channel in scan:
           │ viNonWifi = viEnv->GetAggregateEffect(apPos, channel)
           │ scanData.nonWifiChannelUtilization = viNonWifi
           │
           ▼
     ChannelScoringHelper::CalculateScores()
           │
           │ nonWifiScore = CalculateNonWifiScore(scanData.nonWifiChannelUtilization)
           │
           ▼
     Channel Switch Decision
```

### Proportional Scaling Logic

Virtual Interferer effects "take space" from measured utilization:

```cpp
if (viNonWifi > 0.0) {
    // VI takes priority - calculate available space after VI
    double availableSpace = max(0.0, 100.0 - viNonWifi);

    // Scale down original values to fit
    double origTotal = max(wifiUtil + nonWifiUtil, totalUtil);

    if (availableSpace < origTotal) {
        double scaleFactor = availableSpace / origTotal;
        effectiveWifi = wifiUtil * scaleFactor;
        effectiveNonWifi = nonWifiUtil * scaleFactor + viNonWifi;
    } else {
        // Enough space - just add VI to non-WiFi
        effectiveNonWifi = nonWifiUtil + viNonWifi;
    }

    effectiveTotal = effectiveWifi + effectiveNonWifi;
}
```

---

## Interferer Types and Their Impact

### 1. MicrowaveInterferer

**Impact on Channel Scoring**:
- Affects ALL 2.4 GHz channels (1-14)
- High non-WiFi CCA contribution (30-70%)
- Often causes channel discard due to high threshold exceedance

```cpp
// Typical effect:
nonWifiCcaPercent = 30-70% * dutyCycle * overlapFactor
// e.g., 50% * 0.5 * 1.0 = 25% base, with variations
```

### 2. BluetoothInterferer

**Impact on Channel Scoring**:
- Affects 2.4 GHz channels probabilistically (frequency hopping)
- Lower per-channel impact due to hopping spread
- Rarely causes discard, but adds to non-WiFi score

```cpp
// Effect scaled by overlap probability:
overlapProbability = overlappingBtChannels / 79;  // ~25% for 20MHz WiFi
nonWifiCcaPercent = (2-15%) * dutyCycle * overlapProbability
```

### 3. ZigbeeInterferer

**Impact on Channel Scoring**:
- Affects specific WiFi channels based on ZigBee channel
- Very low duty cycle (1-5%)
- Minimal impact unless multiple ZigBee devices

### 4. RadarInterferer (DFS)

**Impact on Channel Scoring**:
- **DOES NOT affect non-WiFi score** (radar is transient)
- Triggers **immediate channel blacklisting**
- Wideband span affects multiple channels simultaneously

```cpp
// DFS impact:
if (radarDetected) {
    for (ch in affectedChannels) {
        g_globalRanking.dfsBlacklist.insert(ch);
    }
    // All APs on affected channels MUST switch immediately
}
```

---

## Configuration

### JSON Configuration Example

```json
{
  "channelScoring": {
    "enabled": true,
    "weightBssid": 0.5,
    "weightRssi": 0.5,
    "weightNonWifi": 0.5,
    "weightOverlap": 0.5,
    "nonWifiDiscardThreshold": 40.0,
    "nonWifiTriggerThreshold": 65.0,
    "dfsBlacklistDurationSec": 30.0,
    "channelSwitchCooldownSec": 15.0
  },
  "virtualInterferers": {
    "enabled": true,
    "updateInterval": 0.1,
    "microwaves": [
      {"x": 5, "y": 5, "z": 1, "txPowerDbm": -20, "dutyCycle": 0.5, "startTime": 10}
    ],
    "bluetooths": [
      {"x": 3, "y": 3, "z": 1, "txPowerDbm": 4, "profile": "AUDIO_STREAMING", "startTime": 5}
    ],
    "radars": [
      {"x": 100, "y": 100, "z": 10, "dfsChannel": 52, "radarType": "WEATHER",
       "spanLength": 2, "startTime": 25}
    ]
  }
}
```

### Key Thresholds

| Parameter | Default | Description |
|-----------|---------|-------------|
| `nonWifiDiscardThreshold` | 40% | Channel rejected if non-WiFi > this |
| `nonWifiTriggerThreshold` | 65% | Trigger per-AP scoring when > this |
| `channelSwitchCooldownSec` | 15s | Wait after switch for beacon refresh |
| `dfsBlacklistDurationSec` | 30s | How long to blacklist after radar |
| `scoringDebounce` | 1s | Min time between scoring runs |

---

## Cooldown and Debounce Mechanisms

### 1. Channel Switch Cooldown (Per-AP)

After switching, an AP must wait for beacon cache to refresh:

```cpp
Time g_channelSwitchCooldown = Seconds(15);

// On switch:
g_lastChannelSwitch[nodeId] = Simulator::Now();

// On next trigger check:
if (now - g_lastChannelSwitch[nodeId] < g_channelSwitchCooldown) {
    // Skip this AP - still in cooldown
}
```

### 2. Scoring Debounce (Global)

Prevents rapid successive scoring runs:

```cpp
Time g_scoringDebounce = Seconds(1);

void PerformChannelScoring() {
    if (now - g_lastScoringRun < g_scoringDebounce) {
        return;  // Too soon
    }
    g_lastScoringRun = now;
    // ... proceed
}
```

### 3. DFS Detection Cooldown (Per-Channel)

Prevents spam from repeated radar detections:

```cpp
Time g_dfsDetectionCooldown = Seconds(5);

void HandleDfsRadarDetection(...) {
    if (now - g_lastDfsDetection[channel] < g_dfsDetectionCooldown) {
        return;  // Already handled recently
    }
}
```

### 4. Backup Channel Usage Cooldown

Prevents ping-pong between channels during rapid DFS:

```cpp
Time g_backupUsageCooldown = Seconds(30);

// When selecting backup:
if (g_backupUsageTime[ch] + g_backupUsageCooldown > now) {
    // Skip this channel - recently used as backup
}
```

---

## DFS Channel Management

### DFS Channels in 5 GHz

```cpp
// UNII-2A (requires DFS)
{52, 56, 60, 64}

// UNII-2C (requires DFS)
{100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144}

// Non-DFS fallbacks
const std::vector<uint8_t> g_nonDfsChannels5GHz =
    {36, 40, 44, 48, 149, 153, 157, 161, 165};
```

### Blacklist Expiry

```cpp
void CheckDfsBlacklistExpiry() {
    Time now = Simulator::Now();
    std::vector<uint8_t> expired;

    for (auto& [ch, expiry] : g_globalRanking.blacklistExpiry) {
        if (now >= expiry) {
            expired.push_back(ch);
        }
    }

    for (uint8_t ch : expired) {
        g_globalRanking.dfsBlacklist.erase(ch);
        g_globalRanking.blacklistExpiry.erase(ch);
        // Channel can be used again
    }
}
```

---

## Debugging and Logging

### Log Prefixes

| Prefix | Meaning |
|--------|---------|
| `[NON-WIFI-TRIGGER]` | Non-WiFi threshold exceeded |
| `[QUEUE]` | AP scoring queue operations |
| `[AP-SCORING]` | Per-AP channel scoring |
| `[CHANNEL-SWITCH]` | Actual channel switch execution |
| `[DFS-BLACKLIST]` | Radar detection, channels blacklisted |
| `[DFS-SWITCH]` | DFS-triggered channel evacuation |
| `[DFS-EXPIRY]` | Blacklist entry expired |
| `[ScanData]` | Beacon/scan data processing |

### Sample Log Output

```
[NON-WIFI-TRIGGER] AP 0 (Ch 36) non-WiFi=72.3% > threshold=65% -> Queuing for scoring
[QUEUE] AP 0 added to scoring queue (size: 1)
========== PER-AP CHANNEL SCORING (t=45.2s) ==========
[AP 0] 00:00:00:00:00:01 (Current Ch 36 - 5GHz)
Trigger: Non-WiFi threshold exceeded - scoring ONLY this AP
  5GHz Channel Scores (sorted best->worst):
    Ch  40 | Score:  25.3 (B:  0.0 R:  0.0 N: 25.3 O:  0.0)
    Ch  36 | Score:  72.3 (B:  0.0 R:  0.0 N: 72.3 O:  0.0) [CUR]
    Ch  44 | Score:  85.0 (B: 50.0 R: 35.0 N:  0.0 O:  0.0)
  -> SWITCHING: Ch 36 -> Ch 40
[CHANNEL-SWITCH] Node 0 switched to Ch 40 (cooldown: 15s)
```

---

## Complete Implementation Checklist

1. **Include Headers**:
   ```cpp
   #include "ns3/channel-scoring-helper.h"
   #include "ns3/virtual-interferer.h"
   #include "ns3/virtual-interferer-environment.h"
   #include "ns3/virtual-interferer-helper.h"
   ```

2. **Initialize Globals**:
   ```cpp
   ChannelScoringConfig g_channelScoringConfig;
   ChannelScoringHelper g_channelScoringHelper;
   VirtualInterfererHelper g_viHelper;
   ```

3. **Configure Channel Scoring**:
   ```cpp
   g_channelScoringConfig.weightNonWifi = 0.5;
   g_channelScoringConfig.nonWifiDiscardThreshold = 40.0;
   g_channelScoringHelper.SetConfig(g_channelScoringConfig);
   ```

4. **Setup Virtual Interferers**:
   ```cpp
   g_viHelper.SetEnvironmentConfig(envConfig);
   // Create interferers...
   g_viHelper.RegisterWifiDevices(allWifiDevices);
   VirtualInterfererEnvironment::Get()->Start();
   ```

5. **Connect DFS Callback**:
   ```cpp
   viEnv->TraceConnectWithoutContext("DfsTrigger",
       MakeCallback(&HandleDfsRadarDetection));
   ```

6. **Install CCA Monitors**:
   ```cpp
   monitor->TraceConnectWithoutContext("ChannelUtilization",
       MakeCallback(&OnChannelUtilization));
   ```

7. **Cleanup**:
   ```cpp
   g_viHelper.UninstallAll();
   VirtualInterfererEnvironment::Destroy();
   ```

---

## Summary

The channel scoring and switching system provides:

1. **Multi-factor channel evaluation** with configurable weights
2. **Reactive switching** triggered by non-WiFi interference threshold
3. **DFS compliance** with immediate evacuation and blacklisting
4. **Sequential allocation** to prevent AP conflicts
5. **Cooldown mechanisms** to prevent oscillation
6. **Virtual Interferer integration** for realistic interference modeling

Key flows:
- **Non-WiFi Trigger**: CCA Monitor → OnChannelUtilization → QueueApForScoring → PerformChannelScoringForAp → ApWifiMac::SwitchChannel
- **DFS Radar**: RadarInterferer → DfsTrigger trace → HandleDfsRadarDetection → Blacklist + Immediate Switch
