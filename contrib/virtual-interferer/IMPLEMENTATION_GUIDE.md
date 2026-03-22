# Virtual Interferer Implementation Guide

## Overview

The Virtual Interferer system simulates the **EFFECTS** of non-WiFi devices on WiFi networks without performing actual spectrum calculations. This approach provides:

- **Performance Optimization**: No SpectrumWifiPhy overhead
- **Realistic Interference Modeling**: Microwave, Bluetooth, ZigBee, and Radar
- **PHY Layer Injection**: Packet loss and CCA metrics
- **DFS Radar Detection**: Triggers channel switching on 5 GHz DFS channels
- **Channel Scoring Integration**: Non-WiFi utilization affects channel selection

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                VirtualInterfererEnvironment (Singleton)          │
│  - Manages all interferers and WiFi receivers                    │
│  - Periodic effect calculation (default: 100ms)                  │
│  - Injects effects into WiFi devices                             │
└──────────────────────────┬──────────────────────────────────────┘
                           │
     ┌─────────────────────┼─────────────────────┐
     │                     │                     │
     ▼                     ▼                     ▼
┌──────────────┐   ┌──────────────┐      ┌──────────────┐
│  Microwave   │   │  Bluetooth   │      │    Radar     │
│  Interferer  │   │  Interferer  │ ...  │  Interferer  │
└──────┬───────┘   └──────┬───────┘      └──────┬───────┘
       │                  │                     │
       └──────────────────┴─────────────────────┘
                          │
                          ▼
              ┌───────────────────────┐
              │  InterferenceEffect   │
              │  - nonWifiCcaPercent  │
              │  - packetLossProbability │
              │  - signalPowerDbm     │
              │  - triggersDfs        │
              └───────────────────────┘
                          │
      ┌───────────────────┼───────────────────┐
      ▼                   ▼                   ▼
┌───────────┐     ┌───────────────┐    ┌────────────┐
│ BSS Load  │     │  Error Model  │    │ DFS Trigger│
│    IE     │     │ (Packet Loss) │    │  Callback  │
└───────────┘     └───────────────┘    └────────────┘
```

---

## Core Components

### 1. VirtualInterferer (Base Class)

**Location**: `model/virtual-interferer.h/.cc`

The abstract base class for all interferers. Key features:

```cpp
class VirtualInterferer : public Object
{
    // MUST OVERRIDE in subclasses:
    virtual std::string GetInterfererType() const = 0;
    virtual std::set<uint8_t> GetAffectedChannels() const = 0;
    virtual double GetBandwidthMhz() const = 0;
    virtual double GetCenterFrequencyMhz() const = 0;
    virtual InterferenceEffect CalculateEffect(...) const = 0;

    // Provided functionality:
    void Install();      // Register with environment
    void TurnOn/Off();   // Control state
    void SetSchedule(Time onDuration, Time offDuration);  // Periodic on/off
    double CalculateRxPower(double distanceM) const;      // Path loss model
    double GetChannelOverlapFactor(uint8_t wifiChannel) const;
};
```

**Path Loss Model** (Log-distance):
```
PL(d) = PL(d0) + 10 * n * log10(d/d0)
```
- Default reference distance: 1.0m
- Default reference loss: 40 dB
- Default path loss exponent: 3.0

### 2. InterferenceEffect Structure

**Location**: `model/virtual-interferer.h`

```cpp
struct InterferenceEffect
{
    double nonWifiCcaPercent = 0.0;      // 0-100%
    double packetLossProbability = 0.0;  // 0-1
    double signalPowerDbm = -200.0;      // dBm
    bool triggersDfs = false;            // DFS trigger

    // Aggregation: effects combine properly
    InterferenceEffect& operator+=(const InterferenceEffect& other);
};
```

**Effect Aggregation Rules**:
- `nonWifiCcaPercent`: Summed, capped at 100%
- `packetLossProbability`: P(loss) = 1 - (1-p1)(1-p2)
- `signalPowerDbm`: Max (closest interferer dominates)
- `triggersDfs`: OR operation

### 3. VirtualInterfererEnvironment (Singleton)

**Location**: `model/virtual-interferer-environment.h/.cc`

Central manager that:
1. Tracks all registered interferers
2. Tracks all WiFi receivers
3. Runs periodic updates
4. Calculates and injects effects

```cpp
// Get singleton
auto env = VirtualInterfererEnvironment::Get();

// Configure
VirtualInterfererEnvironmentConfig config;
config.updateInterval = MilliSeconds(100);
config.enablePacketLoss = true;
config.enableNonWifiCca = true;
config.enableDfs = true;
env->SetConfig(config);

// Start periodic updates
env->Start();

// Cleanup (important!)
VirtualInterfererEnvironment::Destroy();
```

**Configuration Options**:
| Parameter | Default | Description |
|-----------|---------|-------------|
| `updateInterval` | 100ms | Effect recalculation period |
| `injectionInterval` | 100ms | BSS Load IE update period |
| `ccaEdThresholdDbm` | -62 dBm | CCA energy detect threshold |
| `ccaSensitivityDbm` | -82 dBm | CCA preamble detect threshold |
| `enableNonWifiCca` | true | Inject non-WiFi CCA into BSS Load |
| `enablePacketLoss` | true | Install error models on PHY |
| `enableDfs` | true | Enable DFS radar detection |
| `maxNonWifiUtilPercent` | 100% | Cap for non-WiFi utilization |
| `maxPacketLossProb` | 1.0 | Cap for packet loss probability |

---

## PHY Layer Injection Mechanism

### Packet Loss Injection

**Location**: `model/virtual-interferer-error-model.h/.cc`

The `VirtualInterfererErrorModel` is installed on each WiFi PHY's post-reception error model:

```cpp
// In helper/environment:
Ptr<WifiPhy> phy = wifiDev->GetPhy();
info.errorModel = CreateObject<VirtualInterfererErrorModel>();
phy->SetPostReceptionErrorModel(info.errorModel);

// During periodic injection:
receiver.errorModel->SetPacketLossRate(receiver.accumulatedPacketLoss);
```

**How it works**:
1. Environment calculates aggregate packet loss probability per receiver
2. Updates error model's `m_packetLossRate`
3. For each received packet, `DoCorrupt()` is called
4. Returns `true` (drop) if `random() < m_packetLossRate`

### Non-WiFi CCA Injection (BSS Load IE)

The system updates the AP's BSS Load Information Element:

```cpp
// In InjectNonWifiCca():
if (apMac) {
    uint8_t nonWifiScaled = static_cast<uint8_t>(
        std::min(255.0, receiver.accumulatedNonWifiCca * 2.55));
    ApWifiMac::SetNonWifiChannelUtilization(receiver.bssid, nonWifiScaled);
}
```

This allows:
- STAs to see non-WiFi interference via beacon frames
- Channel scoring to factor in non-WiFi utilization
- ACS/DCS algorithms to avoid high-interference channels

---

## Interferer Types

### 1. MicrowaveInterferer

**Location**: `model/microwave-interferer.h/.cc`

Simulates microwave oven interference in 2.4 GHz.

**Characteristics**:
- Center frequency: 2450 MHz
- Bandwidth: 80 MHz (broadband noise)
- Duty cycle: ~50% (follows AC power cycle, 60Hz)
- Affects ALL 2.4 GHz channels (1-14)

**Power Levels**:
| Level | TX Power |
|-------|----------|
| LOW | -35 dBm |
| MEDIUM | -25 dBm |
| HIGH | -15 dBm |

**Usage**:
```cpp
auto microwave = CreateObject<MicrowaveInterferer>();
microwave->SetPosition(Vector(5, 5, 1));
microwave->SetPowerLevel(MicrowaveInterferer::MEDIUM);
microwave->SetDoorLeakage(0.5);  // 0-1, adds up to 10 dB
microwave->SetAcFrequency(60.0);  // 50 or 60 Hz
microwave->Install();
```

**Effect Calculation**:
```cpp
// CCA utilization: 30-70% base, scaled by power and overlap
double powerAboveThreshold = rxPowerDbm - CCA_THRESHOLD_DBM;
double powerFactor = min(1.0, powerAboveThreshold / 30.0);
double baseUtil = 30 + 40 * powerFactor;  // 30-70%
nonWifiCcaPercent = baseUtil * dutyCycle * overlapFactor;

// Packet loss: 10-40% base
packetLossProbability = baseLoss * dutyCycle * overlapFactor;
```

### 2. BluetoothInterferer

**Location**: `model/bluetooth-interferer.h/.cc`

Simulates Bluetooth Classic and BLE interference.

**Characteristics**:
- Frequency range: 2402-2480 MHz (79 channels)
- Channel bandwidth: 1 MHz
- Frequency hopping: 625 μs intervals (uses `std::rand()`)
- Lower per-channel impact due to hopping

**Device Classes**:
| Class | TX Power | Range |
|-------|----------|-------|
| CLASS_1 | 20 dBm | ~100m |
| CLASS_2 | 4 dBm | ~10m |
| CLASS_3 | 0 dBm | ~1m |

**Profiles**:
| Profile | Duty Cycle |
|---------|------------|
| AUDIO_STREAMING | 45% |
| DATA_TRANSFER | 35% |
| HID | 10% |
| IDLE | 3% |

**Usage**:
```cpp
auto bt = CreateObject<BluetoothInterferer>();
bt->SetPosition(Vector(3, 3, 1));
bt->SetDeviceClass(BluetoothInterferer::CLASS_2);
bt->SetProfile(BluetoothInterferer::AUDIO_STREAMING);
bt->SetHoppingEnabled(true);
bt->Install();
```

**Effect Calculation** (with hopping):
```cpp
// Calculate overlap probability based on hopping
int overlappingBtChannels = /* count BT channels overlapping WiFi channel */;
double overlapProbability = overlappingBtChannels / 79.0;

// Lower base utilization due to frequency hopping
nonWifiCcaPercent = (2-15%) * dutyCycle * overlapProbability;
packetLossProbability = (1-8%) * dutyCycle * overlapProbability;
```

### 3. ZigbeeInterferer

**Location**: `model/zigbee-interferer.h/.cc`

Simulates IEEE 802.15.4 (ZigBee) interference.

**Characteristics**:
- Channels 11-26 in 2.4 GHz band
- Bandwidth: 2 MHz
- Very low duty cycle (1-5%)
- TX Power: 3 dBm

**Network Types**:
| Type | Duty Cycle |
|------|------------|
| SENSOR | 1% |
| CONTROL | 3% |
| LIGHTING | 5% |

**Usage**:
```cpp
auto zb = CreateObject<ZigbeeInterferer>();
zb->SetPosition(Vector(10, 10, 1));
zb->SetZigbeeChannel(15);  // Overlaps WiFi 1-6
zb->SetNetworkType(ZigbeeInterferer::CONTROL);
zb->Install();
```

### 4. RadarInterferer (DFS)

**Location**: `model/radar-interferer.h/.cc`

Simulates radar signals that trigger DFS channel switching in 5 GHz.

**Characteristics**:
- 5 GHz DFS bands only (channels 52-64, 100-144)
- Very low duty cycle (<1%)
- Pulse-based transmission
- **Triggers DFS channel switch**

**Radar Types**:
| Type | TX Power | Pulse Width | Pulse Interval |
|------|----------|-------------|----------------|
| WEATHER | 33 dBm | 1 μs | 1 ms |
| MILITARY | 40 dBm | 0.5 μs | 0.5 ms |
| AVIATION | 36 dBm | 2 μs | 2 ms |

**Wideband Span Configuration** (Recent Fix):
```cpp
radar->SetSpanLength(2);      // ±2 channels affected
radar->SetMaxSpanLength(4);   // Max for random span
radar->SetRandomSpan(true);   // Randomize on hop
```

**Channel Hopping**:
```cpp
radar->SetDfsChannels({52, 56, 60, 64, 100, 104, 108, 112});
radar->SetHopInterval(Seconds(10));
radar->SetRandomHopping(true);
```

**DFS Detection**:
```cpp
// In CalculateEffect():
const double DFS_DETECTION_THRESHOLD_DBM = -62.0;
if (radarRxPower > DFS_DETECTION_THRESHOLD_DBM) {
    effect.triggersDfs = true;  // Will trigger channel switch
}
```

**DFS Channels** (20 MHz):
- U-NII-2A: 52, 56, 60, 64
- U-NII-2C: 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144

**Wider Channels** also affected:
- 40 MHz: 54, 62, 102, 110, 118, 126, 134, 142
- 80 MHz: 58, 106, 122, 138
- 160 MHz: 114

---

## Channel Scoring Integration

**Location**: `contrib/channel-scoring/helper/channel-scoring-helper.h/.cc`

The channel scoring system uses non-WiFi utilization as one of four scoring factors:

### Scoring Factors

1. **BSSID Count** (weightBssid = 0.5): Number of neighbor APs
2. **RSSI** (weightRssi = 0.5): Average signal strength of neighbors
3. **Non-WiFi Utilization** (weightNonWifi = 0.5): From BSS Load IE or CCA
4. **Channel Overlap** (weightOverlap = 0.5): Spectral overlap with neighbors

### Non-WiFi Score Calculation

```cpp
double CalculateNonWifiScore(neighbors, scanningRadioNonWifiUtil, threshold, shouldDiscard) {
    if (!neighbors.empty()) {
        // Use BSS Load IE from neighbors
        maxNonWifi = max(neighbor.nonWifiUtil for all neighbors);
        nonWifiPercent = (maxNonWifi / 255.0) * 100.0;
    } else {
        // Use scanning radio's CCA measurement
        nonWifiPercent = scanningRadioNonWifiUtil * 100.0;
    }

    if (nonWifiPercent > threshold) {
        shouldDiscard = true;  // Channel rejected!
    }

    return min((nonWifiPercent / threshold) * 100.0, 100.0);
}
```

### Discard Threshold

Channels with non-WiFi utilization above `nonWifiDiscardThreshold` (default 40%) are completely rejected:

```cpp
ChannelScoringConfig config;
config.nonWifiDiscardThreshold = 40.0;  // Reject if >40% non-WiFi
```

---

## Using VirtualInterfererHelper

**Location**: `helper/virtual-interferer-helper.h/.cc`

Simplifies interferer creation and management:

```cpp
VirtualInterfererHelper helper;

// Configure environment
VirtualInterfererEnvironmentConfig config;
config.updateInterval = MilliSeconds(100);
config.enablePacketLoss = true;
helper.SetEnvironmentConfig(config);

// Register WiFi devices (installs error models)
helper.RegisterWifiDevices(allWifiDevices);
// OR auto-discover:
helper.AutoRegisterWifiDevices(allNodes);

// Create interferers
auto mw = helper.CreateMicrowave(Vector(5,5,1), MicrowaveInterferer::MEDIUM);
auto bt = helper.CreateBluetooth(Vector(3,3,1), BluetoothInterferer::CLASS_2);
auto radar = helper.CreateRadar(Vector(100,100,10), 52, RadarInterferer::WEATHER);

// Random placement
helper.CreateBluetoothsRandom(5, Vector(0,0,0), Vector(20,20,3));

// Scenarios
helper.CreateHomeScenario(Vector(10,10,0), 20.0, 3, true);
helper.CreateOfficeScenario(Vector(10,10,0), 30.0, 10);
helper.CreateDfsTestScenario(Vector(100,100,10), 52, RadarInterferer::WEATHER);

// Scheduling
helper.SetSchedule(mw, Seconds(30), Seconds(30));  // 30s on, 30s off
helper.ScheduleTurnOn(bt, Seconds(5));
helper.ScheduleTurnOff(bt, Seconds(60));

// Cleanup
helper.UninstallAll();
helper.Clear();
```

---

## Complete Usage Example

```cpp
#include "ns3/virtual-interferer.h"
#include "ns3/virtual-interferer-environment.h"
#include "ns3/virtual-interferer-helper.h"
#include "ns3/microwave-interferer.h"
#include "ns3/bluetooth-interferer.h"
#include "ns3/radar-interferer.h"

// 1. Set up WiFi network (standard ns-3)
NodeContainer wifiApNode, wifiStaNodes;
// ... (create nodes, install WiFi, etc.)

// 2. Configure virtual interferer environment
VirtualInterfererEnvironmentConfig envConfig;
envConfig.updateInterval = MilliSeconds(100);
envConfig.enablePacketLoss = true;
envConfig.enableNonWifiCca = true;
envConfig.enableDfs = true;

VirtualInterfererHelper helper;
helper.SetEnvironmentConfig(envConfig);

// 3. Register WiFi devices
helper.RegisterWifiDevices(allWifiDevices);

// 4. Create interferers
auto microwave = helper.CreateMicrowave(
    Vector(3, 3, 1),
    MicrowaveInterferer::MEDIUM
);
helper.SetSchedule(microwave, Seconds(30), Seconds(30));

helper.CreateBluetooth(
    Vector(6, 8, 1),
    BluetoothInterferer::CLASS_2,
    BluetoothInterferer::AUDIO_STREAMING
);

// For DFS testing (requires 5 GHz channel on AP):
auto radar = helper.CreateRadar(
    Vector(100, 100, 10),
    52,  // DFS channel
    RadarInterferer::WEATHER
);
radar->SetHopInterval(Seconds(15));
radar->SetSpanLength(2);

// 5. Connect DFS trace (optional)
auto env = VirtualInterfererEnvironment::Get();
env->TraceConnect("DfsTrigger", "",
    MakeCallback(&YourDfsHandler));

// 6. Run simulation
Simulator::Stop(Seconds(simTime));
Simulator::Run();

// 7. Cleanup (CRITICAL!)
helper.UninstallAll();
VirtualInterfererEnvironment::Destroy();
Simulator::Destroy();
```

---

## Recent Fixes and Critical Notes

### 1. DFS and Power Change Fix (commit 0991a18b)

- Added `ChannelOverlaps()` override in RadarInterferer for span-based detection
- Radar now correctly affects all channels within its span, not just center
- Fixed power calculations in radar effect

### 2. Fast Loop Fix (commit 49c2f113)

- Fixed timing issues in rapid update loops
- Improved scheduler stability

### 3. Bidirectional Sync

The environment supports synchronization with spectrum-shadow simulations:

```cpp
// Reads from: /tmp/ns3-spectrum-sync-timestamp.txt
// Writes to:  /tmp/ns3-sync-timestamp.txt
```

### 4. Critical Cleanup Requirements

**ALWAYS call these before Simulator::Destroy():**

```cpp
helper.UninstallAll();              // Unregister all interferers
helper.Clear();                      // Release references
VirtualInterfererEnvironment::Destroy();  // Destroy singleton
```

Failure to cleanup causes crashes due to dangling pointers during ns-3 shutdown.

### 5. IsBeingDestroyed() Safety

The environment tracks destruction state to prevent crashes:

```cpp
void VirtualInterferer::Uninstall() {
    if (!VirtualInterfererEnvironment::IsBeingDestroyed()) {
        auto env = VirtualInterfererEnvironment::Get();
        env->UnregisterInterferer(this);
    }
}
```

---

## Debugging

Enable logging:

```bash
NS_LOG="VirtualInterfererEnvironment=level_debug:MicrowaveInterferer=level_debug:BluetoothInterferer=level_debug:RadarInterferer=level_debug" ./ns3 run your-simulation
```

Key log prefixes:
- `[VI Schedule]` - Interferer on/off scheduling
- `[VI-LOSS]` - Packet loss injection
- `[DFS-RADAR]` - Radar detection events
- `[DFS-HOP]` - Radar channel hopping

---

## Performance Considerations

1. **Update Interval**: Default 100ms is reasonable. Faster updates increase accuracy but cost more CPU.

2. **Number of Interferers**: The system is O(interferers × receivers) per update cycle.

3. **Bluetooth Hopping**: Runs at 625 μs intervals per device. Many Bluetooth devices can create scheduling overhead.

4. **Error Model**: One per WiFi PHY. Minimal per-packet overhead.

---

## File Structure

```
contrib/virtual-interferer/
├── model/
│   ├── virtual-interferer.h/.cc          # Base class
│   ├── virtual-interferer-environment.h/.cc  # Singleton manager
│   ├── virtual-interferer-error-model.h/.cc  # PHY error model
│   ├── microwave-interferer.h/.cc        # Microwave oven
│   ├── bluetooth-interferer.h/.cc        # Bluetooth/BLE
│   ├── zigbee-interferer.h/.cc           # IEEE 802.15.4
│   ├── radar-interferer.h/.cc            # DFS radar
│   └── cordless-interferer.h/.cc         # Cordless phone
├── helper/
│   └── virtual-interferer-helper.h/.cc   # Easy setup helper
├── examples/
│   └── virtual-interferer-example.cc     # Usage example
├── test/
│   └── virtual-interferer-test-suite.cc  # Unit tests
└── CMakeLists.txt
```

---

## Integration Points

1. **BSS Load IE**: `ApWifiMac::SetNonWifiChannelUtilization()`
2. **PHY Error Model**: `WifiPhy::SetPostReceptionErrorModel()`
3. **Channel Scoring**: `ChannelScoringHelper::CalculateNonWifiScore()`
4. **DFS Trace**: `VirtualInterfererEnvironment::m_dfsTriggerTrace`
5. **CCA Monitor**: `WifiCcaMonitor` can aggregate virtual interferer effects

---

## Summary

The Virtual Interferer system provides a lightweight, performant way to simulate non-WiFi interference:

- **No spectrum calculations** - effects-based approach
- **PHY injection** - realistic packet loss
- **BSS Load IE** - affects channel selection
- **DFS support** - radar triggers channel switching
- **Easy API** - helper class simplifies setup
- **Configurable** - scheduling, power levels, profiles

For implementation elsewhere, ensure you:
1. Create the singleton environment with proper config
2. Register WiFi devices before starting
3. Install interferers with `Install()` call
4. Start the environment with `Start()`
5. **Always cleanup** before destroying simulator
