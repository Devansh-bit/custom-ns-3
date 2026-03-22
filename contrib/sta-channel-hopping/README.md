# STA Channel Hopping Module Documentation

## Overview

The STA Channel Hopping module (`contrib/sta-channel-hopping/`) provides automatic multi-channel roaming capabilities for WiFi STAs in ns-3 simulations. It monitors STA disassociation events and automatically reconnects to the best available AP across multiple channels using signal quality metrics.

## Architecture

### Key Components

Located in `contrib/sta-channel-hopping/`:
- `helper/sta-channel-hopping-helper.{h,cc}` - Helper class for installation and configuration
- `model/sta-channel-hopping-manager.{h,cc}` - Core manager class handling roaming logic
- `examples/sta-channel-hopping-example.cc` - Complete usage example
- `examples/multi-sta-channel-hopping-example.cc` - Multi-STA scenario example

### Dependencies

**Required Module**: `DualPhySnifferHelper` (contrib/dual-phy-sniffer/)

The STA Channel Hopping Manager relies on the Dual-PHY Sniffer to:
- Track beacons from APs across multiple channels
- Build a cache of available APs with signal quality metrics
- Provide channel information for roaming decisions

### WiFi PHY API Compatibility

This module is **API-agnostic** and works with both ns-3 WiFi PHY implementations:

- **SpectrumWifiPhyHelper** (Spectrum-based): More detailed frequency modeling, demonstrated in the included examples
- **YansWifiPhyHelper** (Yans-based): Simpler propagation model, also supported

The core components (StaChannelHoppingManager and StaChannelHoppingHelper) work transparently with either API through the DualPhySnifferHelper abstraction layer. Users can choose either implementation based on their simulation needs:

- **Use SpectrumWifi** for: Detailed spectrum modeling, interference analysis, multi-frequency scenarios
- **Use YansWifi** for: Simpler setups, faster simulations, basic propagation modeling

**Note**: The included examples (`sta-channel-hopping-example.cc`, `multi-sta-channel-hopping-example.cc`) use SpectrumWifiPhyHelper. YansWifi works identically - just replace the channel setup code with YansWifiChannelHelper as shown in the DualPhySniffer documentation.

## How It Works

### 1. Disassociation Detection

The manager connects to the STA's `DeAssoc` and `Assoc` trace sources to monitor association state changes.

**On Disassociation:**
1. Clear the beacon cache for fresh AP discovery
2. Schedule roaming attempt after `ScanningDelay` period
3. During delay, DualPhySnifferHelper scans channels and caches beacons

**On Association:**
1. Cancel any pending roaming attempts
2. Update current BSSID tracking

### 2. AP Selection

After the scanning delay, the manager queries the DualPhySnifferHelper's beacon cache:

```cpp
std::vector<BeaconInfo> beacons = m_dualPhySniffer->GetBeaconsReceivedBy(m_dualPhyOperatingMac);
```

**Selection Criteria:**
- Excludes current AP (if still associated)
- Filters APs below minimum SNR threshold
- Selects AP with highest SNR value

### 3. Roaming Execution

The manager uses StaWifiMac's roaming APIs with explicit band specification:

```cpp
// Determine band from channel number
WifiPhyBand targetBand;
if (targetInfo->channel >= 1 && targetInfo->channel <= 14)
{
    targetBand = WIFI_PHY_BAND_2_4GHZ;  // 2.4 GHz band
}
else
{
    targetBand = WIFI_PHY_BAND_5GHZ;    // 5 GHz band
}

if (staMac->IsAssociated())
{
    // Use InitiateRoaming for seamless reassociation
    staMac->InitiateRoaming(targetBssid, targetInfo->channel, targetBand);
}
else
{
    // Use AssociateToAp for initial association
    staMac->AssociateToAp(targetBssid, targetInfo->channel, targetBand);
}
```

**Band and Width Specification:**
- Channels 1-14: Explicitly set to `WIFI_PHY_BAND_2_4GHZ` with `width=20` (explicit to avoid DSSS/OFDM ambiguity)
- Other channels: Explicitly set to `WIFI_PHY_BAND_5GHZ` with `width=0` (auto-detected by ns-3 from channel number)
- **2.4 GHz Fix**: Explicit width=20 prevents runtime error from multiple channel definitions (DSSS 22MHz, OFDM 20MHz, OFDM 40MHz)
- **5 GHz Auto-Width**: Channel width automatically determined for 5 GHz channels (20/40/80/160 MHz)

## Core APIs

### StaChannelHoppingHelper Class

The helper class simplifies installation and configuration of the channel hopping manager.

#### Setup Methods

```cpp
// Constructor
StaChannelHoppingHelper();

// Set attributes on the manager
void SetAttribute(std::string name, const AttributeValue& value);

// Set DualPhySniffer to use for beacon cache queries (REQUIRED)
void SetDualPhySniffer(DualPhySnifferHelper* sniffer);

// Install manager on a single STA device
Ptr<StaChannelHoppingManager> Install(Ptr<WifiNetDevice> staDevice);

// Install manager on multiple STA devices
std::vector<Ptr<StaChannelHoppingManager>> Install(NetDeviceContainer staDevices);
```

### StaChannelHoppingManager Class

The manager class handles all roaming logic and state management.

#### Configuration Methods

```cpp
// Set the STA device to monitor
void SetStaDevice(Ptr<WifiNetDevice> staDevice);

// Set the DualPhySniffer and operating MAC for beacon queries
void SetDualPhySniffer(DualPhySnifferHelper* sniffer, Mac48Address operatingMac);

// Set delay before attempting reconnection (default: 5 seconds)
void SetScanningDelay(Time delay);

// Set minimum SNR threshold for AP selection (default: 0.0 dB)
void SetMinimumSnr(double minSnr);

// Enable or disable automatic reconnection
void Enable(bool enable);

// Check if automatic reconnection is enabled
bool IsEnabled() const;
```

#### Configurable Attributes

```cpp
// Via SetAttribute():
"ScanningDelay"  // Time delay before roaming attempt (default: 5.0s)
"MinimumSnr"     // Minimum SNR threshold in dB (default: 0.0)
"Enabled"        // Enable/disable automatic reconnection (default: true)
```

#### Trace Sources

```cpp
// RoamingTriggered trace source
TracedCallback<Time, Mac48Address, Mac48Address, Mac48Address, double>
    m_roamingTriggeredTrace;

// Callback signature:
typedef void (*RoamingTriggeredCallback)(
    Time time,                  // Current simulation time
    Mac48Address staAddress,    // STA MAC address
    Mac48Address oldBssid,      // Previous AP BSSID
    Mac48Address newBssid,      // Target AP BSSID
    double snr);                // SNR of target AP in dB
```

## Usage Example

### Basic Setup

```cpp
#include "ns3/sta-channel-hopping-helper.h"
#include "ns3/dual-phy-sniffer-helper.h"

// 1. Create nodes
NodeContainer apNodes, staNodes;
apNodes.Create(3);   // 3 APs on different channels
staNodes.Create(1);  // 1 STA

// 2. Setup spectrum channel (shared by all devices)
Ptr<MultiModelSpectrumChannel> spectrumChannel =
    CreateObject<MultiModelSpectrumChannel>();

// 3. Create APs on different channels (e.g., channels 1, 6, 11)
SpectrumWifiPhyHelper wifiPhy;
wifiPhy.SetChannel(spectrumChannel);
// ... configure APs on channels 1, 6, 11 ...

// 4. Create STA device
// ... configure STA on channel 1 initially ...

// 5. Setup DualPhySnifferHelper for multi-channel scanning
DualPhySnifferHelper snifferHelper;
snifferHelper.SetSpectrumChannel(spectrumChannel);
snifferHelper.SetScanningChannels({1, 6, 11});  // Scan all AP channels
snifferHelper.SetHopInterval(Seconds(0.5));     // Hop every 500ms

// Install scanning radio on STA node
Mac48Address staBssid = staDevice->GetMac()->GetAddress();
NetDeviceContainer scanDevice =
    snifferHelper.Install(staNodes.Get(0), 1, staBssid);

// Start channel hopping
snifferHelper.StartChannelHopping();

// 6. Setup StaChannelHoppingHelper
StaChannelHoppingHelper chHelper;
chHelper.SetDualPhySniffer(&snifferHelper);
chHelper.SetAttribute("ScanningDelay", TimeValue(Seconds(5.0)));
chHelper.SetAttribute("MinimumSnr", DoubleValue(5.0));  // 5 dB minimum

// Install manager on STA
Ptr<StaChannelHoppingManager> manager = chHelper.Install(staDevice);

// Optional: Connect to roaming trace
manager->TraceConnectWithoutContext(
    "RoamingTriggered",
    MakeCallback(&RoamingTriggeredCallback));
```

### Complete Workflow

1. **Initial Association**: STA associates with AP1 on channel 1
2. **Continuous Scanning**: DualPhySniffer scans channels 1, 6, 11 every 500ms
3. **Disassociation Event**: AP1 changes channel or STA moves out of range
4. **Scanning Period**: Manager waits 5 seconds while DualPhySniffer builds beacon cache
5. **AP Selection**: Manager queries cache, finds best AP (highest SNR > 5 dB)
6. **Roaming**: Manager calls `InitiateRoaming()` or `AssociateToAp()` with explicit band
7. **Association**: STA associates with selected AP on new channel

### Triggering Disassociation

```cpp
// Manual disassociation
void ForceDisassociation(Ptr<WifiNetDevice> staDevice)
{
    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(staDevice->GetMac());
    if (staMac)
    {
        staMac->ForcedDisassociate();
    }
}

// Schedule at simulation time t=10s
Simulator::Schedule(Seconds(10.0), &ForceDisassociation, staDevice);
```

### Channel Change Triggering Disassociation

```cpp
// Change AP channel (causes disassociation of associated STAs)
void ChangeApChannel(Ptr<WifiNetDevice> apDevice, uint8_t newChannel)
{
    Ptr<WifiPhy> phy = apDevice->GetPhy();
    if (phy)
    {
        // Determine band from channel number
        std::string bandStr = (newChannel >= 1 && newChannel <= 14)
                              ? "BAND_2_4GHZ" : "BAND_5GHZ";

        std::ostringstream channelSettings;
        channelSettings << "{" << (int)newChannel << ", 0, " << bandStr << ", 0}";

        phy->SetAttribute("ChannelSettings", StringValue(channelSettings.str()));
    }
}

// Schedule channel change at t=10s
Simulator::Schedule(Seconds(10.0), &ChangeApChannel, apDevice, 6);
```

## Implementation Details

### Beacon Cache Management

**Cache Clearing Strategy:**
```cpp
void OnDisassociation(Mac48Address bssid)
{
    // Clear beacon cache for fresh scan
    m_dualPhySniffer->ClearBeaconsReceivedBy(m_dualPhyOperatingMac);

    // Schedule roaming after scanning delay
    m_roamingEvent = Simulator::Schedule(m_scanningDelay,
                                         &StaChannelHoppingManager::InitiateRoaming,
                                         this);
}
```

During the scanning delay, DualPhySnifferHelper:
- Hops through configured channels
- Receives beacons from available APs
- Caches beacon information with SNR, RSSI, channel data

### Retry Logic

If no suitable AP is found:
```cpp
void InitiateRoaming()
{
    Mac48Address targetBssid = SelectBestAp();

    if (targetBssid == Mac48Address())
    {
        // No suitable AP found - clear cache and retry after another scan period
        m_dualPhySniffer->ClearBeaconsReceivedBy(m_dualPhyOperatingMac);
        m_roamingEvent = Simulator::Schedule(m_scanningDelay,
                                             &StaChannelHoppingManager::InitiateRoaming,
                                             this);
        return;
    }

    // Proceed with roaming...
}
```

### Band and Width Configuration

**Explicit Band Specification** (Updated Implementation):
```cpp
// Channels 1-14: 2.4 GHz band
// Other channels: 5 GHz band (36, 38, 40, 42, 44, 48, 50, etc.)
WifiPhyBand targetBand;
if (targetInfo->channel >= 1 && targetInfo->channel <= 14)
{
    targetBand = WIFI_PHY_BAND_2_4GHZ;
}
else
{
    targetBand = WIFI_PHY_BAND_5GHZ;
}

// Width is auto-detected by ns-3 from channel number:
// - Ch 36, 40, 44, 48: 20 MHz
// - Ch 38, 46, 54, 62: 40 MHz
// - Ch 42, 58, 106, 122: 80 MHz
// - Ch 50, 114: 160 MHz
```

**Why Explicit Band?**
- WiFi 6E introduces 6 GHz band support
- Channel numbers can be ambiguous (e.g., channel 1 could be 2.4 or 6 GHz)
- Explicit band prevents ambiguity while preserving auto-width detection
- 6 GHz support not implemented in this version

### Association State Handling

```cpp
if (staMac->IsAssociated())
{
    // Still associated (rare race condition) - use InitiateRoaming
    // This triggers reassociation to new AP while maintaining data connection
    staMac->InitiateRoaming(targetBssid, targetInfo->channel, targetBand);
}
else
{
    // Not associated - use AssociateToAp
    // This triggers initial association sequence
    staMac->AssociateToAp(targetBssid, targetInfo->channel, targetBand);
}
```

## Configuration Parameters

### ScanningDelay

**Default**: 5.0 seconds

**Purpose**: Time to wait after disassociation before attempting roaming

**Considerations:**
- Too short: Beacon cache may be incomplete
- Too long: Extended disconnection time
- Should be at least 2-3 × `HopInterval` × number of channels

**Recommended Values:**
- 3 channels × 0.5s hop = 1.5s minimum scan time
- Add buffer for beacon reception: 3-5 seconds total

### MinimumSnr

**Default**: 0.0 dB

**Purpose**: Minimum SNR threshold for AP selection

**Usage:**
- Filters out weak APs
- Prevents roaming to marginal connections
- Higher values = more selective roaming

**Recommended Values:**
- Permissive: 0-5 dB (accept most APs)
- Moderate: 10-15 dB (good signal quality)
- Conservative: 20+ dB (excellent signal only)

### Enabled

**Default**: true

**Purpose**: Enable/disable automatic reconnection

**Usage:**
```cpp
// Temporarily disable auto-roaming
manager->Enable(false);

// Re-enable later
manager->Enable(true);
```

## Integration with Modified ns-3 WiFi Stack

### Using StaWifiMac Roaming APIs

The manager leverages the custom `InitiateRoaming()` and `AssociateToAp()` methods:

```cpp
// From src/wifi/model/sta-wifi-mac.{h,cc}
void InitiateRoaming(Mac48Address targetBssid,
                     uint8_t channel,
                     WifiPhyBand band);

void AssociateToAp(Mac48Address targetBssid,
                   uint8_t channel,
                   WifiPhyBand band);
```

These methods:
- Switch STA PHY to target channel with explicit band
- Use width=0 for automatic channel width detection
- Initiate association/reassociation process
- Handle all 802.11 management frame exchanges

### Channel Settings Format

```cpp
// Format: {channel, width, band, primary20Index}
// width=0 triggers auto-detection
// band explicitly specified to avoid 6 GHz ambiguity

// Example: Channel 38 (40 MHz) in 5 GHz
"{38, 0, BAND_5GHZ, 0}"

// Example: Channel 6 (20 MHz) in 2.4 GHz
"{6, 0, BAND_2_4GHZ, 0}"
```

## Use Cases

### Scenario 1: Dynamic Frequency Selection (DFS)

When an AP detects radar on its current channel:
1. AP performs channel switch
2. Associated STAs get disassociated
3. StaChannelHoppingManager detects disassociation
4. Scans all channels via DualPhySniffer
5. Finds AP on new channel
6. Automatically roams to new channel

### Scenario 2: Load Balancing

Multiple APs with overlapping coverage:
1. STA initially connects to closest AP
2. AP becomes congested (simulated via forced disassociation)
3. Manager scans for alternative APs
4. Selects less-loaded AP with good SNR
5. Roams to alternative AP

### Scenario 3: Mobility Scenarios

STA moving through environment:
1. Signal quality degrades as STA moves away from AP1
2. Eventually drops below threshold → disassociation
3. Manager scans for nearby APs
4. Finds AP2 with better signal
5. Seamlessly roams to AP2

### Scenario 4: Multi-Channel Networks

Enterprise deployment with APs on non-overlapping channels:
- 2.4 GHz: Channels 1, 6, 11
- 5 GHz: Channels 36, 44, 52, 60, etc.

Manager enables:
- Automatic AP selection across all channels
- Band switching (2.4 GHz ↔ 5 GHz)
- Width adaptation (20 ↔ 40 ↔ 80 ↔ 160 MHz)

## Limitations

### Current Implementation

1. **SNR-Only Selection**: Only considers SNR, not load, latency, or throughput
2. **Single STA Focus**: Each manager handles one STA device
3. **Reactive Only**: Triggers on disassociation, not proactive handoff
4. **No Hysteresis**: No mechanism to prevent ping-ponging between APs
5. **Manual Scanning Channel Config**: Must specify all AP channels in DualPhySniffer
6. **No 6 GHz Support**: Only 2.4 GHz and 5 GHz bands supported

### Design Constraints

- Requires DualPhySnifferHelper for beacon cache
- All devices must share same spectrum channel
- ScanningDelay introduces disconnection period
- Cannot roam if no APs exceed MinimumSnr threshold

## Performance Considerations

### Memory Usage

- One manager instance per STA (~1 KB)
- Beacon cache managed by DualPhySnifferHelper
- Negligible overhead for typical simulations

### CPU Overhead

- Event-driven (only active during roaming)
- Beacon cache query: O(N) where N = number of cached APs
- Minimal impact on simulation performance

### Network Impact

- No additional traffic generated
- Uses existing beacon frames from APs
- Association/reassociation frames part of standard 802.11

## Debugging

### Enable Logging

```bash
NS_LOG="StaChannelHoppingManager=level_all:DualPhySnifferHelper=info" \
    ./ns3 run sta-channel-hopping-example
```

### Common Issues

**1. Manager never triggers roaming**
- Check: Is DualPhySnifferHelper installed and started?
- Check: Does DualPhySnifferHelper scan all AP channels?
- Check: Are beacons being received? (Enable DualPhySniffer logging)

**2. No suitable AP found**
- Check: MinimumSnr threshold too high?
- Check: ScanningDelay too short for complete scan?
- Check: Are APs actually broadcasting beacons?

**3. Roaming to wrong AP**
- Check: SNR calculation in beacon cache
- Check: Is current AP being properly excluded?
- Check: Are all APs within DualPhySniffer scanning range?

**4. Association fails after roaming**
- Check: Target AP channel/band configuration
- Check: StaWifiMac roaming API implementation
- Check: PHY channel switching logs

### Trace Analysis

```cpp
// Connect to all relevant traces
staMac->TraceConnectWithoutContext("Assoc",
                                    MakeCallback(&AssocCallback));
staMac->TraceConnectWithoutContext("DeAssoc",
                                    MakeCallback(&DeAssocCallback));
manager->TraceConnectWithoutContext("RoamingTriggered",
                                    MakeCallback(&RoamingCallback));
```

## Future Enhancements

Potential improvements for future versions:

1. **Multi-Criteria Selection**: Consider load, latency, throughput in addition to SNR
2. **Proactive Handoff**: Trigger roaming based on signal quality thresholds
3. **Hysteresis Mechanism**: Prevent frequent switching between similar APs
4. **Fast BSS Transition (802.11r)**: Reduce roaming latency
5. **802.11k/v Integration**: Use RRM reports and BSS transition management
6. **Machine Learning**: Predict optimal roaming decisions based on patterns
7. **6 GHz Band Support**: Add WiFi 6E compatibility with proper band handling

## References

- Dual-PHY Sniffer Documentation: `docs/dual-phy-sniffer.md`
- Source Code Changes: `docs/source-changes.md`
- StaWifiMac Roaming APIs: `src/wifi/model/sta-wifi-mac.{h,cc}`
- Example: `contrib/sta-channel-hopping/examples/sta-channel-hopping-example.cc`
