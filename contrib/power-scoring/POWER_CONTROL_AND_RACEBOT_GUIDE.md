# Power Control and RACEBOT Algorithm Guide

## Overview

This guide documents the implementation of **Dynamic TX Power Control** based on the RACEBOT algorithm, integrated with Virtual Interferer support for non-WiFi interference handling.

The system provides:
- **OBSS-based Power Control**: Adjusts TX power based on overlapping BSS detection
- **Two-Loop Architecture**: Slow loop (goal calculation) + Fast loop (power adjustment)
- **Non-WiFi Mode**: Special handling when non-WiFi interference is high
- **802.11ax Integration**: Uses HE-SIG-A for BSS Color detection

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          DATA SOURCES                                    │
├─────────────────────────────────────────────────────────────────────────┤
│   HeSigA Callback          PhyRxPayloadBegin       VirtualInterferer    │
│   (RSSI + BSS Color)       (Actual MCS)            (nonWifiCcaPercent)  │
└───────────┬─────────────────────┬───────────────────────┬───────────────┘
            │                     │                       │
            ▼                     ▼                       │
┌───────────────────────────────────────────────┐         │
│        PowerScoringHelper                      │         │
│  ┌─────────────────────────────────────────┐  │         │
│  │          ObssTracker                     │  │         │
│  │  - rssiCounts (EWMA histogram)          │  │         │
│  │  - bssRssiEwma (intra-BSS RSSI)         │  │         │
│  │  - mcsEwma (MCS tracking)               │  │         │
│  │  - obssFrameCount                       │  │         │
│  └─────────────────────────────────────────┘  │         │
└───────────────────────────────────────────────┘         │
            │                                             │
            ▼                                             │
┌───────────────────────────────────────────────────────────────────────┐
│                    PerformPowerScoring() [Periodic]                    │
│  ┌──────────────────────────────────────────────────────────────────┐ │
│  │                      GetNonWifiForAp()                            │ │
│  │  - Gets nonWifiChannelUtilization from g_apMetrics                │◄┘
│  │  - Includes VI effect via OnChannelUtilization                    │
│  └──────────────────────────────────────────────────────────────────┘ │
│                              │                                         │
│                              ▼                                         │
│  ┌──────────────────────────────────────────────────────────────────┐ │
│  │                    Mode Decision                                  │ │
│  │  if (nonWifi > threshold) → NON_WIFI_MODE                        │ │
│  │  if (nonWifi < threshold - hysteresis) → RACEBOT_MODE            │ │
│  └──────────────────────────────────────────────────────────────────┘ │
│                    │                               │                   │
│                    ▼                               ▼                   │
│  ┌──────────────────────────┐     ┌──────────────────────────────────┐│
│  │   RunNonWifiMode()       │     │     RunRacebotAlgorithm()        ││
│  │   (Increase power)       │     │     (OBSS/PD-based control)      ││
│  └──────────────────────────┘     └──────────────────────────────────┘│
│                    │                               │                   │
│                    └───────────────┬───────────────┘                   │
│                                    ▼                                   │
│  ┌──────────────────────────────────────────────────────────────────┐ │
│  │                  Apply TX Power to PHY                            │ │
│  │  phy->SetTxPowerStart/End(result.txPowerDbm)                     │ │
│  └──────────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────────────┘
```

---

## Key Data Structures

### PowerScoringConfig

```cpp
struct PowerScoringConfig {
    // RACEBOT algorithm parameters
    double margin = 3.0;               // M parameter (dBm)
    double gamma = 0.7;                // MCS change threshold
    double alpha = 0.3;                // EWMA smoothing factor
    double ofcThreshold = 500.0;       // OFC threshold (frame count)

    // OBSS/PD bounds (802.11ax)
    double obsspdMinDbm = -82.0;       // Minimum OBSS/PD (CCA)
    double obsspdMaxDbm = -62.0;       // Maximum OBSS/PD

    // TX Power bounds
    double txPowerRefDbm = 33.0;       // Reference/Maximum TX Power
    double txPowerMinDbm = 10.0;       // Minimum TX Power

    // Non-WiFi mode parameters
    double nonWifiThresholdPercent = 50.0;  // Enter non-WiFi mode threshold
    double nonWifiHysteresis = 10.0;        // Hysteresis for mode transitions

    // Two-loop timing architecture
    double t1IntervalSec = 10.0;       // Slow loop: goal recalculation
    double t2IntervalSec = 2.0;        // Fast loop: power change cooldown

    // Update interval
    double updateIntervalSec = 1.0;    // How often to run algorithm
};
```

### ObssTracker (Per-AP State)

```cpp
struct ObssTracker {
    std::map<int, double> rssiCounts;  // RSSI histogram (EWMA counts)
    std::map<int, int> rawCounts;      // Raw RSSI counts
    double bssRssiEwma = -70.0;        // EWMA of intra-BSS RSSI
    double mcsEwma = 7.0;              // EWMA of MCS
    double prevMcsEwma = 7.0;          // Previous MCS EWMA
    uint32_t frameCount = 0;           // Total frames received
    uint32_t obssFrameCount = 0;       // OBSS frames received
};
```

### ApPowerState

```cpp
struct ApPowerState {
    uint32_t nodeId = 0;
    uint8_t myBssColor = 0;            // This AP's BSS Color (802.11ax)
    ObssTracker tracker;               // RSSI/MCS tracker
    double currentTxPowerDbm = 21.0;   // Current TX power
    double currentObsspdDbm = -82.0;   // Current OBSS/PD level
    double goalObsspdDbm = -82.0;      // Goal OBSS/PD level
    bool inNonWifiMode = false;        // Non-WiFi mode flag
    double lastNonWifiPercent = 0.0;   // Last non-WiFi measurement

    // Timer tracking
    Time lastPowerChange;              // Last time TX power changed
    Time lastGoalRecalculation;        // Last time goal was recalculated
    bool initialized = false;
};
```

### PowerResult

```cpp
struct PowerResult {
    uint32_t nodeId = 0;
    double txPowerDbm = 21.0;          // Calculated TX power
    double obsspdLevelDbm = -82.0;     // Calculated OBSS/PD level
    double goalObsspdDbm = -82.0;      // Target OBSS/PD level
    bool inNonWifiMode = false;        // True if in non-WiFi mode
    bool powerChanged = false;         // True if power changed significantly
    std::string reason;                // Reason for power change
};
```

---

## RACEBOT Algorithm

RACEBOT (Rate Adaptation and Channel Estimation Based Optimization for Throughput) uses a two-loop architecture:

### Stage 1 & 2: Goal Calculation (Slow Loop - t1)

Runs every `t1IntervalSec` (default 10s). Calculates the **goal OBSS/PD threshold**:

```cpp
void RecalculateGoal(ApPowerState& state) {
    double newGoal = obsspdMinDbm;  // Start at minimum (-82 dBm)

    // For each RSSI level in histogram
    for (const auto& [rssi, count] : state.tracker.rssiCounts) {
        // If enough frames observed at this RSSI (OFC threshold)
        if (count > ofcThreshold && rssi > newGoal) {
            // Candidate 1: RSSI + margin (allow ignoring weak OBSS)
            double candidate1 = rssi + margin;
            // Candidate 2: Intra-BSS RSSI - margin (protect own clients)
            double candidate2 = state.tracker.bssRssiEwma - margin;
            // Take the more conservative (lower) of the two
            newGoal = min(candidate1, candidate2);
        }
    }

    // Apply bounds
    newGoal = clamp(newGoal, obsspdMinDbm, obsspdMaxDbm);
    state.goalObsspdDbm = newGoal;
}
```

**Logic Explanation**:
- **OFC Threshold**: Only consider RSSI levels with enough observations (default 500 frames)
- **Candidate 1**: Allow ignoring OBSS frames weaker than `rssi + margin`
- **Candidate 2**: Don't raise OBSS/PD higher than `intra-BSS RSSI - margin` to protect own clients
- **Result**: Goal OBSS/PD is the more conservative value

### Stage 3: Power Adjustment (Fast Loop - t2)

Runs when power change cooldown (`t2IntervalSec`, default 2s) has elapsed:

```cpp
void RunRacebotAlgorithm(ApPowerState& state) {
    // Detect MCS changes
    bool mcsDecreased = state.tracker.prevMcsEwma * gamma > state.tracker.mcsEwma;
    bool mcsIncreased = state.tracker.mcsEwma > state.tracker.prevMcsEwma * 1.1;

    if (mcsDecreased) {
        // Link quality degraded - be more conservative
        double newObsspd = (currentObsspdDbm + bssRssiEwma - margin) / 2.0;
        state.currentObsspdDbm = max(newObsspd, obsspdMinDbm);
        // Also reduce goal
        state.goalObsspdDbm = (currentObsspdDbm + goalObsspdDbm) / 2.0;
    }
    else if (mcsIncreased && currentObsspdDbm < goalObsspdDbm - 1.0) {
        // Link quality good - can be more aggressive
        double step = min(2.0, (goalObsspdDbm - currentObsspdDbm) / 2.0);
        state.currentObsspdDbm = min(currentObsspdDbm + step, obsspdMaxDbm);
    }
    else {
        // Gradual movement toward goal (1% per step)
        double newObsspd = currentObsspdDbm -
                          (0.01 * (currentObsspdDbm - goalObsspdDbm));
        state.currentObsspdDbm = min(newObsspd, obsspdMaxDbm);
    }

    // Calculate TX power from OBSS/PD using 802.11ax equation:
    // TxPower = obsspdMin + txPowerRef - OBSS/PD
    double targetTxPower = obsspdMinDbm + txPowerRefDbm - currentObsspdDbm;
    targetTxPower = clamp(targetTxPower, txPowerMinDbm, txPowerRefDbm);

    // Gradual power adjustment (max 1 dBm per step)
    if (abs(targetTxPower - currentTxPowerDbm) > 0.5) {
        if (targetTxPower > currentTxPowerDbm) {
            currentTxPowerDbm = min(currentTxPowerDbm + 1.0, targetTxPower);
        } else {
            currentTxPowerDbm = max(currentTxPowerDbm - 1.0, targetTxPower);
        }
    }
}
```

### OBSS/PD to TX Power Relationship (802.11ax)

```
TX Power (dBm) = OBSS/PD_min + TX Power_ref - OBSS/PD_current

Example:
- OBSS/PD_min = -82 dBm (minimum sensitivity)
- TX Power_ref = 33 dBm (maximum power)
- OBSS/PD_current = -72 dBm (raised threshold)

TX Power = -82 + 33 - (-72) = 23 dBm
```

**Relationship**:
- Higher OBSS/PD (ignore more OBSS) → Lower TX Power
- Lower OBSS/PD (listen to weaker OBSS) → Higher TX Power

---

## Non-WiFi Mode

When non-WiFi interference exceeds the threshold, the algorithm switches to a simplified mode that increases power to overcome interference.

### Mode Transition Logic

```cpp
// Check mode transitions with hysteresis
bool shouldEnterNonWifi = nonWifiPercent > nonWifiThresholdPercent;
bool shouldExitNonWifi = nonWifiPercent < (nonWifiThresholdPercent - nonWifiHysteresis);

if (!inNonWifiMode && shouldEnterNonWifi) {
    inNonWifiMode = true;   // Enter non-WiFi mode
}
else if (inNonWifiMode && shouldExitNonWifi) {
    inNonWifiMode = false;  // Exit non-WiFi mode
}
```

**Hysteresis Example** (threshold=50%, hysteresis=10%):
- Enter non-WiFi mode when: `nonWifi > 50%`
- Exit non-WiFi mode when: `nonWifi < 40%`

### Non-WiFi Mode Algorithm

```cpp
void RunNonWifiMode(ApPowerState& state) {
    // Move toward maximum power to overcome interference
    double targetPower = txPowerRefDbm;
    double step = 1.0;  // 1 dBm per update

    if (currentTxPowerDbm < targetPower) {
        currentTxPowerDbm = min(currentTxPowerDbm + step, targetPower);
    }

    // Update OBSS/PD to match
    currentObsspdDbm = obsspdMinDbm + txPowerRefDbm - currentTxPowerDbm;
}
```

---

## Event Flow: Data Collection

### HE-SIG-A Callback (RSSI + BSS Color)

Each received HE frame triggers the HE-SIG-A callback:

```cpp
// In basic-simulation.cc setup:
hePhy->SetEndOfHeSigACallback(
    [nodeId](HeSigAParameters params) {
        OnApHeSigA(nodeId, params);
    });

// Callback implementation:
void OnApHeSigA(uint32_t nodeId, HeSigAParameters params) {
    g_powerScoringHelper.ProcessHeSigA(nodeId, params.rssi, params.bssColor);
}

// In PowerScoringHelper:
void ProcessHeSigA(uint32_t nodeId, double rssiDbm, uint8_t rxBssColor) {
    ApPowerState& state = m_apStates[nodeId];

    // Detect OBSS using BSS Color (802.11ax)
    bool isObss = false;
    if (rxBssColor != 0 && state.myBssColor != 0) {
        isObss = (rxBssColor != state.myBssColor);
    }

    // Update RSSI histogram
    int rssiInt = static_cast<int>(round(rssiDbm));
    state.tracker.UpdateRssiCount(rssiInt, alpha, isObss);

    // Update intra-BSS RSSI EWMA (only for own BSS frames)
    if (!isObss) {
        state.tracker.UpdateBssRssi(rssiDbm, alpha);
    }
}
```

### PHY RX Payload Begin Callback (Actual MCS)

```cpp
// In basic-simulation.cc setup:
phy->TraceConnectWithoutContext("PhyRxPayloadBegin",
    MakeBoundCallback(&OnApPhyRxPayloadBegin, nodeId));

// Callback implementation:
void OnApPhyRxPayloadBegin(uint32_t nodeId, WifiTxVector txVector, Time psduDuration) {
    if (txVector.GetModulationClass() == WIFI_MOD_CLASS_HE) {
        uint8_t mcs = txVector.GetMode().GetMcsValue();
        g_powerScoringHelper.UpdateMcs(nodeId, mcs);
    }
}

// In PowerScoringHelper:
void UpdateMcs(uint32_t nodeId, uint8_t mcs) {
    ApPowerState& state = m_apStates[nodeId];
    state.tracker.UpdateMcs(static_cast<double>(mcs), alpha);
}
```

### Non-WiFi Data from Virtual Interferer

```
VirtualInterferer          OnChannelUtilization()       PerformPowerScoring()
      │                           │                            │
      │ GetAggregateEffect()      │                            │
      └──────────────────────────►│                            │
                                  │                            │
                                  │ effectiveNonWifi = ...     │
                                  │ + viNonWifi                │
                                  │                            │
                                  │ Store in g_apMetrics       │
                                  │ .nonWifiChannelUtilization │
                                  └────────────────────────────►│
                                                               │
                                                   GetNonWifiForAp()
                                                   → nonWifiPercent
```

```cpp
// In basic-simulation.cc:
double GetNonWifiForAp(uint32_t nodeId) {
    auto it = g_apMetrics.find(nodeId);
    if (it == g_apMetrics.end()) return 0.0;
    // Return as percentage (0-100) for RACEBOT
    return it->second.nonWifiChannelUtilization * 100.0;
}
```

---

## Event Flow: Power Calculation

### PerformPowerScoring() - Periodic Callback

```cpp
void PerformPowerScoring() {
    if (!g_powerScoringEnabled) {
        // Reschedule even if disabled (in case enabled later)
        Simulator::Schedule(Seconds(updateIntervalSec), &PerformPowerScoring);
        return;
    }

    for (const auto& [nodeId, apMetrics] : g_apMetrics) {
        // Get or create AP state with BSS Color
        auto& state = g_powerScoringHelper.GetOrCreateApState(nodeId, apMetrics.bssColor);

        // Get non-WiFi percentage (includes VI effect)
        double nonWifiPercent = GetNonWifiForAp(nodeId);
        double prevPower = state.currentTxPowerDbm;

        // Calculate new power
        PowerResult result = g_powerScoringHelper.CalculatePower(nodeId, nonWifiPercent);

        // Apply to PHY
        if (apDev) {
            phy->SetTxPowerStart(dBm_u{result.txPowerDbm});
            phy->SetTxPowerEnd(dBm_u{result.txPowerDbm});
        }

        // Update metrics
        apMetrics.txPowerDbm = result.txPowerDbm;
        apMetrics.obsspdLevelDbm = result.obsspdLevelDbm;
    }

    // Schedule next run
    Simulator::Schedule(Seconds(updateIntervalSec), &PerformPowerScoring);
}
```

### CalculatePower() - Core Algorithm

```cpp
PowerResult CalculatePower(uint32_t nodeId, double nonWifiPercent) {
    PowerResult result;
    ApPowerState& state = m_apStates[nodeId];
    Time now = Simulator::Now();

    // Initialize timestamps on first run
    if (!state.initialized) {
        state.lastPowerChange = now;
        state.lastGoalRecalculation = now;
        state.initialized = true;
    }

    double prevPower = state.currentTxPowerDbm;
    bool wasInNonWifiMode = state.inNonWifiMode;

    // Update non-WiFi measurement
    state.lastNonWifiPercent = nonWifiPercent;

    // SLOW LOOP (t1): Recalculate goal every t1 seconds
    if ((now - state.lastGoalRecalculation).GetSeconds() >= t1IntervalSec) {
        RecalculateGoal(state);
        state.lastGoalRecalculation = now;
    }

    // Mode transition with hysteresis
    CheckModeTransition(state, nonWifiPercent);

    // FAST LOOP (t2): Power change cooldown
    bool inCooldown = ((now - state.lastPowerChange).GetSeconds() < t2IntervalSec);

    if (!inCooldown) {
        if (state.inNonWifiMode) {
            RunNonWifiMode(state);
        } else {
            RunRacebotAlgorithm(state);
        }

        // Check if power changed significantly (> 0.5 dBm)
        if (abs(state.currentTxPowerDbm - prevPower) > 0.5) {
            state.lastPowerChange = now;
            result.powerChanged = true;
        }
    }

    // Build result
    result.txPowerDbm = state.currentTxPowerDbm;
    result.obsspdLevelDbm = state.currentObsspdDbm;
    result.goalObsspdDbm = state.goalObsspdDbm;
    result.inNonWifiMode = state.inNonWifiMode;
    result.reason = DeterminePowerChangeReason(state, prevPower, wasInNonWifiMode);

    return result;
}
```

---

## Virtual Interferer Integration

### How VI Effects Flow to Power Scoring

```
VirtualInterferer               VirtualInterfererEnvironment
     │                                    │
     │ CalculateEffect()                  │
     └───────────────────────────────────►│
                                          │
     WifiCcaMonitor ──────────────────────┤
           │                              │
           │ OnChannelUtilization()       │
           │         │                    │
           │         ▼                    │
           │    viNonWifi = viEnv->GetAggregateEffect(pos, channel)
           │         │
           │         ▼
           │    Proportional Scaling
           │    effectiveNonWifi = scaled(RF) + viNonWifi
           │         │
           │         ▼
           │    g_apMetrics[nodeId].nonWifiChannelUtilization = effectiveNonWifi / 100
           │
           │
     PerformPowerScoring() ◄──────────────┤
           │
           │ GetNonWifiForAp(nodeId)
           │ → nonWifiPercent = g_apMetrics[nodeId].nonWifiChannelUtilization * 100
           │
           ▼
     g_powerScoringHelper.CalculatePower(nodeId, nonWifiPercent)
           │
           │ // Mode check
           │ if (nonWifiPercent > threshold) → NON_WIFI_MODE
           │
           ▼
     TX Power Applied to PHY
```

### Interferer Type Impact on Power Control

| Interferer | Impact | Mode Transition |
|------------|--------|-----------------|
| **Microwave** | High non-WiFi (30-70%) | Often triggers NON_WIFI_MODE |
| **Bluetooth** | Low-medium non-WiFi (2-15%) | Usually stays in RACEBOT_MODE |
| **ZigBee** | Very low non-WiFi (1-5%) | Rarely affects mode |
| **Radar** | Transient (triggers DFS, not power) | No direct impact on power |

---

## Two-Loop Architecture Summary

```
                    ┌─────────────────────────────────┐
                    │         SLOW LOOP (t1)          │
                    │    Default: Every 10 seconds    │
                    │                                 │
                    │  RecalculateGoal():             │
                    │  - Analyzes RSSI histogram      │
                    │  - Updates goalObsspdDbm        │
                    │  - Uses OFC threshold           │
                    └─────────────────┬───────────────┘
                                      │
                                      │ goalObsspdDbm
                                      ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           FAST LOOP (t2)                                 │
│                      Default: 2 second cooldown                          │
│                                                                          │
│  if (NOT in cooldown):                                                   │
│    ┌──────────────────────────┐   ┌────────────────────────────────────┐│
│    │   NON_WIFI_MODE          │   │        RACEBOT_MODE                ││
│    │   (nonWifi > threshold)  │   │    (nonWifi < threshold)           ││
│    │                          │   │                                    ││
│    │  Move toward max power   │   │  Stage 3:                          ││
│    │  +1 dBm per step         │   │  - Check MCS trends                ││
│    │                          │   │  - Adjust OBSS/PD toward goal      ││
│    └──────────────────────────┘   │  - Calculate TX power from OBSS/PD ││
│                                   │  - Apply gradual change (±1 dBm)   ││
│                                   └────────────────────────────────────┘│
│                                                                          │
│  Update lastPowerChange if power changed > 0.5 dBm                       │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Configuration

### JSON Configuration Example

```json
{
  "powerScoring": {
    "enabled": true,
    "updateInterval": 1.0,
    "t1IntervalSec": 10.0,
    "t2IntervalSec": 2.0,
    "margin": 3.0,
    "gamma": 0.7,
    "alpha": 0.3,
    "ofcThreshold": 500.0,
    "obsspdMinDbm": -82.0,
    "obsspdMaxDbm": -62.0,
    "txPowerRefDbm": 33.0,
    "txPowerMinDbm": 10.0,
    "nonWifiThresholdPercent": 50.0,
    "nonWifiHysteresis": 10.0
  }
}
```

### Parameter Reference

| Parameter | Default | Description |
|-----------|---------|-------------|
| `margin` | 3.0 dBm | M parameter - margin for OBSS/PD calculation |
| `gamma` | 0.7 | MCS decrease detection threshold |
| `alpha` | 0.3 | EWMA smoothing factor (0-1) |
| `ofcThreshold` | 500 | Frames needed before considering RSSI level |
| `obsspdMinDbm` | -82 dBm | Minimum OBSS/PD (most sensitive) |
| `obsspdMaxDbm` | -62 dBm | Maximum OBSS/PD (least sensitive) |
| `txPowerRefDbm` | 33 dBm | Reference/Maximum TX power |
| `txPowerMinDbm` | 10 dBm | Minimum TX power |
| `nonWifiThresholdPercent` | 50% | Enter non-WiFi mode threshold |
| `nonWifiHysteresis` | 10% | Hysteresis for mode transitions |
| `t1IntervalSec` | 10s | Slow loop interval (goal recalculation) |
| `t2IntervalSec` | 2s | Fast loop cooldown (power change) |
| `updateIntervalSec` | 1s | How often to run algorithm |

---

## Debugging and Logging

### Log Output Example

```
========== POWER SCORING (t=45.0s) ==========
--- AP Node 0 --- Inputs: BSS_RSSI=-65.3dBm, MCS=8.2, NonWifi=25.3%
  -> No change (TxPower=21.0dBm, micro-delta=+0.02dBm, Gradual adjustment toward goal OBSS/PD)

--- AP Node 1 --- Inputs: BSS_RSSI=-58.7dBm, MCS=9.1, NonWifi=62.5%
  -> POWER CHANGE: 24.0 -> 25.0 dBm (delta=+1.0dBm, Mode: RACEBOT -> NON_WIFI (non-WiFi=62%))

--- AP Node 2 --- Inputs: BSS_RSSI=-72.1dBm, MCS=6.5, NonWifi=15.0%
  -> POWER CHANGE: 28.0 -> 27.0 dBm (delta=-1.0dBm, MCS decreased (7.2 -> 6.5))
```

### Reason Strings

| Reason | Meaning |
|--------|---------|
| `Mode: RACEBOT -> NON_WIFI (non-WiFi=X%)` | Entered non-WiFi mode |
| `Mode: NON_WIFI -> RACEBOT (non-WiFi=X%)` | Exited non-WiFi mode |
| `MCS decreased (X -> Y)` | Link quality degraded, being more conservative |
| `MCS increased, moving toward goal` | Link quality good, being more aggressive |
| `Gradual adjustment toward goal OBSS/PD` | Normal slow convergence |
| `In t2 cooldown (Xs remaining)` | Waiting for cooldown |
| `No significant change` | Power delta < 0.5 dBm |
| `NON_WIFI mode adjustment` | Power increased in non-WiFi mode |

---

## Complete Implementation Checklist

### 1. Include Headers

```cpp
#include "ns3/power-scoring-helper.h"
#include "ns3/virtual-interferer.h"
#include "ns3/virtual-interferer-environment.h"
```

### 2. Declare Globals

```cpp
PowerScoringConfig g_powerScoringConfig;
PowerScoringHelper g_powerScoringHelper;
bool g_powerScoringEnabled = false;
```

### 3. Configure from JSON

```cpp
g_powerScoringEnabled = config.powerScoring.enabled;
if (g_powerScoringEnabled) {
    g_powerScoringConfig.margin = config.powerScoring.margin;
    g_powerScoringConfig.gamma = config.powerScoring.gamma;
    // ... other parameters
    g_powerScoringHelper.SetConfig(g_powerScoringConfig);
}
```

### 4. Initialize Per-AP State

```cpp
for (each AP) {
    uint8_t bssColor = heConfig->m_bssColor;
    auto& powerState = g_powerScoringHelper.GetOrCreateApState(nodeId, bssColor);
    powerState.currentTxPowerDbm = initialTxPower;
}
```

### 5. Connect HE-SIG-A Callback

```cpp
auto hePhy = std::dynamic_pointer_cast<HePhy>(phy->GetPhyEntity(WIFI_MOD_CLASS_HE));
if (hePhy) {
    hePhy->SetEndOfHeSigACallback(
        [nodeId](HeSigAParameters params) {
            g_powerScoringHelper.ProcessHeSigA(nodeId, params.rssi, params.bssColor);
        });
}
```

### 6. Connect PHY RX Payload Trace

```cpp
phy->TraceConnectWithoutContext("PhyRxPayloadBegin",
    MakeBoundCallback(&OnApPhyRxPayloadBegin, nodeId));
```

### 7. Schedule Periodic Power Scoring

```cpp
if (g_powerScoringEnabled) {
    Simulator::Schedule(Seconds(g_powerScoringConfig.updateIntervalSec), &PerformPowerScoring);
}
```

### 8. Implement Callbacks

```cpp
void OnApHeSigA(uint32_t nodeId, HeSigAParameters params) {
    g_powerScoringHelper.ProcessHeSigA(nodeId, params.rssi, params.bssColor);
}

void OnApPhyRxPayloadBegin(uint32_t nodeId, WifiTxVector txVector, Time psduDuration) {
    if (txVector.GetModulationClass() == WIFI_MOD_CLASS_HE) {
        uint8_t mcs = txVector.GetMode().GetMcsValue();
        g_powerScoringHelper.UpdateMcs(nodeId, mcs);
    }
}

double GetNonWifiForAp(uint32_t nodeId) {
    return g_apMetrics[nodeId].nonWifiChannelUtilization * 100.0;
}
```

### 9. Implement PerformPowerScoring()

```cpp
void PerformPowerScoring() {
    for (const auto& [nodeId, apMetrics] : g_apMetrics) {
        auto& state = g_powerScoringHelper.GetOrCreateApState(nodeId, apMetrics.bssColor);
        double nonWifiPercent = GetNonWifiForAp(nodeId);

        PowerResult result = g_powerScoringHelper.CalculatePower(nodeId, nonWifiPercent);

        // Apply to PHY
        Ptr<WifiNetDevice> apDev = DynamicCast<WifiNetDevice>(g_apWifiDevices.Get(nodeId));
        if (apDev) {
            apDev->GetPhy()->SetTxPowerStart(dBm_u{result.txPowerDbm});
            apDev->GetPhy()->SetTxPowerEnd(dBm_u{result.txPowerDbm});
        }
    }

    Simulator::Schedule(Seconds(g_powerScoringConfig.updateIntervalSec), &PerformPowerScoring);
}
```

---

## Summary

The Power Scoring system provides:

1. **RACEBOT-based OBSS/PD control** using 802.11ax BSS Color
2. **Two-loop architecture** for stability (slow goal + fast adjustment)
3. **Non-WiFi mode** for high interference scenarios
4. **Virtual Interferer integration** via non-WiFi CCA percentage
5. **Gradual power changes** (max ±1 dBm per step)

Key flows:
- **Data Collection**: HE-SIG-A (RSSI + BSS Color) + PhyRxPayloadBegin (MCS)
- **Non-WiFi Input**: OnChannelUtilization → g_apMetrics → GetNonWifiForAp
- **Power Calculation**: PerformPowerScoring → CalculatePower → Apply to PHY

Mode transitions:
- **RACEBOT_MODE**: Normal OBSS-based power control
- **NON_WIFI_MODE**: Increase power to overcome non-WiFi interference
