# ns-3.46.1 Source Code Modifications

This document outlines the key changes and APIs implemented in the ns-3.46.1 WiFi module source code.

## Summary of Major Changes

### Automatic Channel Width with Explicit Band Specification (Latest)

**All WiFi MAC classes now use explicit band specification with conditional width handling:**

**Old Approach (Manual Band and Width):**
```cpp
// Required manual decoding of channel number to determine band and width
if (channelNumber >= 1 && channelNumber <= 14) {
    band = WIFI_PHY_BAND_2_4GHZ;
    width = 20;
} else if (channelNumber == 50 || channelNumber == 114) {
    band = WIFI_PHY_BAND_5GHZ;
    width = 160;
}
// ... 30+ more lines of width decoding logic
```

**New Approach (Explicit Band + Conditional Width):**
```cpp
// Determine band from channel number
// For 2.4 GHz: must specify width=20 explicitly to avoid ambiguity (DSSS 22MHz, OFDM 20MHz, OFDM 40MHz)
// For 5 GHz: can use width=0 for auto-width detection
WifiPhyBand band;
uint16_t width;
if (channelNumber >= 1 && channelNumber <= 14)
{
    band = WIFI_PHY_BAND_2_4GHZ;
    width = 20; // Must be explicit for 2.4 GHz
}
else
{
    band = WIFI_PHY_BAND_5GHZ;
    width = 0; // Auto-width for 5 GHz
}

std::ostringstream oss;
oss << "{" << +channelNumber << ", " << width << ", ";
oss << (band == WIFI_PHY_BAND_2_4GHZ ? "BAND_2_4GHZ" : "BAND_5GHZ");
oss << ", 0}";
phy->SetAttribute("ChannelSettings", StringValue(oss.str()));
```

**Critical Fix: 2.4 GHz Channel Ambiguity**
- **Problem:** 2.4 GHz channels (1-14) have multiple definitions in ns-3:
  - DSSS 22 MHz (e.g., `{11, 2462 MHz, 22 MHz, WIFI_PHY_BAND_2_4GHZ, DSSS}`)
  - OFDM 20 MHz (e.g., `{11, 2462 MHz, 20 MHz, WIFI_PHY_BAND_2_4GHZ, OFDM}`)
  - OFDM 40 MHz (e.g., `{11, 2462 MHz, 40 MHz, WIFI_PHY_BAND_2_4GHZ, OFDM}`)
- **Issue:** Using `width=0` for 2.4 GHz causes runtime error: `"WifiPhyOperatingChannel: No unique channel found given the specified criteria"`
- **Solution:** Explicitly specify `width=20` for 2.4 GHz channels to select OFDM 20 MHz variant
- **5 GHz:** No ambiguity - each channel number uniquely identifies band+frequency+width, so `width=0` works perfectly

**Benefits:**
- Eliminates ~30 lines of manual width decoding logic per implementation
- **Explicit band prevents ambiguity** (e.g., channel 1 could be 2.4 or 6 GHz in WiFi 6E)
- **Explicit width for 2.4 GHz prevents multi-definition ambiguity**
- Auto-detects channel width for 5 GHz channels (20/40/80/160 MHz)
- Consistent with ns-3's internal channel database (IEEE 802.11 compliant)
- Enables runtime cross-band switching (5 GHz ↔ 2.4 GHz) without crashes
- Future-proof for WiFi 7 320 MHz channels

**Why Not BAND_UNSPECIFIED?**
- WiFi 6E introduces 6 GHz band with overlapping channel numbers
- BAND_UNSPECIFIED could select wrong band for ambiguous channels
- Explicit band ensures 2.4/5 GHz distinction while keeping width handling optimal

**Modified Files:**
- `src/wifi/model/wifi-mac.cc` - Base `SwitchChannel()` implementation
- `src/wifi/model/ap-wifi-mac.cc` - AP channel switching with STA propagation
- `src/wifi/model/sta-wifi-mac.cc` - STA roaming and association channel switching
- `contrib/dual-phy-sniffer/helper/dual-phy-sniffer-helper.cc` - Scanning radio channel hopping
- `contrib/sta-channel-hopping/model/sta-channel-hopping-manager.cc` - Automatic roaming

## Core WiFi Module Modifications

### StaWifiMac Enhancements

#### Manual Roaming Control APIs
The `StaWifiMac` class has been extended with comprehensive roaming control capabilities located in `src/wifi/model/sta-wifi-mac.{h,cc}`.

**Key Methods:**
- `InitiateRoaming(Mac48Address targetBssid, uint8_t channel, WifiPhyBand band)` - Triggers manual roaming to a specific AP while maintaining association
- `AssociateToAp(Mac48Address targetBssid, uint8_t channel, WifiPhyBand band)` - Associates to a specific AP when currently disassociated
- `ForcedDisassociate()` - Forces disassociation without automatic reconnection
- `EnableAutoReconnect(bool enable)` - Controls automatic scanning and reassociation behavior

**Link Quality Monitoring:**
- `GetCurrentRssi()` - Returns current RSSI from associated AP (dBm)
- `GetCurrentSnr()` - Returns current SNR from associated AP (linear scale)
- `GetCurrentBssid()` - Returns MAC address of currently associated AP

#### Background Scanning APIs
Enables neighbor discovery without disconnecting from the current AP.

**Key Methods:**
- `EnableBackgroundScanning(bool enable)` - Enables/disables periodic background scans
- `TriggerBackgroundScan()` - Initiates immediate background scan
- `GetScanResults()` - Returns cached scan results with BSSID, RSSI, SNR, channel info
- `ClearScanResults()` - Clears the scan result cache

**Internal Helpers:**
- `ScheduleBackgroundScan()` - Schedules periodic background scans
- `PerformBackgroundScan()` - Executes background scan operation
- `ProcessBackgroundScanResult()` - Updates neighbor cache with scan results
- `UpdateLinkQuality()` - Updates link quality metrics from beacons

### WifiStaticSetupHelper

Located in `src/wifi/helper/wifi-static-setup-helper.{h,cc}`, this helper bypasses management frame exchanges for faster simulation setup.

**Key Features:**
- Static association without frame exchange overhead
- Multi-link device (MLD) support
- Block ACK agreement setup
- EMLSR mode configuration

**Primary Methods:**
- `SetStaticAssociation()` - Establishes static association between AP and STAs
- `SetStaticAssocPostInit()` - Performs association after device initialization
- `GetLinkIdMap()` - Maps non-AP MLD links to AP MLD links
- `SetStaticBlockAck()` - Configures Block ACK agreements without ADDBA exchange

### WifiMac Base Class

The `WifiMac` class in `src/wifi/model/wifi-mac.{h,cc}` includes friend class declarations for `WifiStaticSetupHelper` to enable static setup functionality.

#### Channel Switching APIs

**WifiMac::SwitchChannel** (`src/wifi/model/wifi-mac.cc:2694-2730`)
```cpp
virtual void SwitchChannel(uint16_t channelNumber, uint8_t linkId = 0)
```
- Base implementation for channel switching at MAC level
- **Uses explicit band with conditional width handling:**
  ```cpp
  WifiPhyBand band;
  uint16_t width;
  if (channelNumber >= 1 && channelNumber <= 14)
  {
      band = WIFI_PHY_BAND_2_4GHZ;
      width = 20; // Must be explicit for 2.4 GHz to avoid ambiguity
  }
  else
  {
      band = WIFI_PHY_BAND_5GHZ;
      width = 0; // Auto-width for 5 GHz
  }

  std::ostringstream oss;
  oss << "{" << +channelNumber << ", " << width << ", ";
  oss << (band == WIFI_PHY_BAND_2_4GHZ ? "BAND_2_4GHZ" : "BAND_5GHZ");
  oss << ", " << +primary20 << "}";
  phy->SetAttribute("ChannelSettings", StringValue(oss.str()));
  ```
- **Band and width determination:**
  - Channels 1-14: `WIFI_PHY_BAND_2_4GHZ`, `width=20` (explicit to avoid DSSS/OFDM ambiguity)
  - Other channels: `WIFI_PHY_BAND_5GHZ`, `width=0` (auto-detect from channel number)
- **5 GHz channel width auto-detection:**
  - 5 GHz 20 MHz: 36, 40, 44, 48, 52, 56, 60, 64, etc.
  - 5 GHz 40 MHz: 38, 46, 54, 62, 102, 110, 118, 126, 134, 142, 151, 159
  - 5 GHz 80 MHz: 42, 58, 106, 122, 138, 155
  - 5 GHz 160 MHz: 50, 114
- Updates PHY settings via `ChannelSettings` attribute
- Does NOT notify associated stations (base implementation)

**Implementation Benefits:**
- Explicit band prevents 6 GHz ambiguity
- Explicit width for 2.4 GHz prevents DSSS/OFDM/40MHz ambiguity
- No manual width decoding logic needed for 5 GHz (eliminates ~30 lines)
- Consistent with ns-3's internal channel database
- Enables safe runtime cross-band switching
- Reduces code complexity and maintenance burden

**WifiMac::NotifyChannelSwitching** (`src/wifi/model/wifi-mac.cc:701-711`)
- Virtual method called when channel switching occurs
- Reconfigures PHY-dependent parameters
- Resets remote station manager
- Can be overridden by derived classes

### ApWifiMac Channel Management

**ApWifiMac::SwitchChannel** (`src/wifi/model/ap-wifi-mac.cc:2808-2882`)
```cpp
void SwitchChannel(uint16_t channelNumber, uint8_t linkId = 0) override
```
- Overrides base WifiMac implementation
- **Atomically propagates channel changes to ALL associated STAs**
- Prevents disassociation during channel switch
- **Uses explicit band with conditional width handling:**
  ```cpp
  WifiPhyBand band;
  uint16_t width;
  if (channelNumber >= 1 && channelNumber <= 14)
  {
      band = WIFI_PHY_BAND_2_4GHZ;
      width = 20; // Must be explicit for 2.4 GHz to avoid ambiguity
  }
  else
  {
      band = WIFI_PHY_BAND_5GHZ;
      width = 0; // Auto-width for 5 GHz
  }

  std::ostringstream oss;
  oss << "{" << +channelNumber << ", " << width << ", ";
  oss << (band == WIFI_PHY_BAND_2_4GHZ ? "BAND_2_4GHZ" : "BAND_5GHZ");
  oss << ", " << +primary20 << "}";
  ```
- Implementation steps:
  1. Collects all unique STA MAC addresses from `staList`
  2. Determines band from channel number and appropriate width
  3. Creates channel settings string with explicit band and conditional width
  4. Applies settings to AP's PHY
  5. Iterates through NodeList to find matching STAs
  6. Applies same `ChannelSettings` to each STA's PHY
- Ensures synchronized channel switching across BSS
- Supports runtime cross-band switching (5 GHz ↔ 2.4 GHz)
- Eliminates ~30 lines of manual width decoding logic for 5 GHz

### StaWifiMac Channel Management

**StaWifiMac::InitiateRoaming** (`src/wifi/model/sta-wifi-mac.cc:2342-2372`)
- **Uses explicit band with conditional width handling for channel switching during roaming:**
  ```cpp
  WifiPhyBand targetBand;
  uint16_t width;
  if (channel >= 1 && channel <= 14)
  {
      targetBand = WIFI_PHY_BAND_2_4GHZ;
      width = 20; // Must be explicit for 2.4 GHz
  }
  else
  {
      targetBand = WIFI_PHY_BAND_5GHZ;
      width = 0; // Auto-width for 5 GHz
  }

  std::ostringstream oss;
  oss << "{" << +channel << ", " << width << ", ";
  oss << (targetBand == WIFI_PHY_BAND_2_4GHZ ? "BAND_2_4GHZ" : "BAND_5GHZ");
  oss << ", 0}";
  phy->SetAttribute("ChannelSettings", StringValue(oss.str()));
  ```
- Explicit band prevents ambiguity with 6 GHz channels
- Explicit width for 2.4 GHz prevents DSSS/OFDM ambiguity
- Width auto-determined by ns-3 for 5 GHz channels
- Enables cross-band roaming (5 GHz ↔ 2.4 GHz)

**StaWifiMac::AssociateToAp** (`src/wifi/model/sta-wifi-mac.cc:2413-2460`)
- **Uses explicit band with conditional width handling for channel switching during association:**
  ```cpp
  WifiPhyBand targetBand;
  uint16_t width;
  if (channel >= 1 && channel <= 14)
  {
      targetBand = WIFI_PHY_BAND_2_4GHZ;
      width = 20; // Must be explicit for 2.4 GHz
  }
  else
  {
      targetBand = WIFI_PHY_BAND_5GHZ;
      width = 0; // Auto-width for 5 GHz
  }

  std::ostringstream oss;
  oss << "{" << +channel << ", " << width << ", ";
  oss << (targetBand == WIFI_PHY_BAND_2_4GHZ ? "BAND_2_4GHZ" : "BAND_5GHZ");
  oss << ", 0}";
  phy->SetAttribute("ChannelSettings", StringValue(oss.str()));
  ```
- Consistent with SwitchChannel and InitiateRoaming implementations
- Supports association to APs on either 2.4 GHz or 5 GHz bands

**StaWifiMac::NotifyChannelSwitching** (`src/wifi/model/sta-wifi-mac.cc:2270-2283`)
- Overrides base WifiMac implementation
- Triggers **disassociation** if currently associated
- Notifies association manager of channel switch
- Note: StaWifiMac does not override `SwitchChannel` - uses base implementation

## Contributed Modules

### Lever API (`contrib/lever-api/`)
Provides advanced control and monitoring capabilities for WiFi simulations.

### Auto-Roaming KV (`contrib/auto-roaming-kv/`)
Implements automatic roaming algorithms with key-value store integration.

### BSS Transition Management 11v (`contrib/bss_tm_11v/`)
Implements IEEE 802.11v BSS transition management features.

### Dual PHY Sniffer (`contrib/dual-phy-sniffer/`)
Enables concurrent monitoring of multiple PHY interfaces.

### STA Channel Hopping (`contrib/sta-channel-hopping/`)
Implements channel hopping capabilities for STAs.

### WiFi CCA Monitor (`contrib/wifi-cca-monitor/`)
Monitors Clear Channel Assessment (CCA) states and channel utilization.

### Spectrum Analysis Tools
- `spectrum-analyser-logger` - Logs spectrum analyzer data
- `spectrum-socket-streamer` - Streams spectrum data via sockets
- `spectrogram-generation` - Generates spectrograms from simulation data

### RRM and 11k Support
- `beacon-protocol-11k-helper` - IEEE 802.11k beacon measurement support
- `neighbor-protocol-11k-helper` - Neighbor report protocol implementation
- `link-measurement-protocol` - Link measurement capabilities

## Usage Considerations

### Roaming API Limitations
- `InitiateRoaming()` only works when already associated
- `AssociateToAp()` only works when disassociated
- Background scanning may impact performance during active data transmission
- Scan results are cached with configurable maximum age

### Static Setup Benefits
- Significantly reduces simulation initialization time
- Eliminates management frame processing overhead
- Ideal for large-scale simulations where association details are predetermined
- Supports complex multi-link scenarios

### Compatibility Notes
- All modifications maintain backward compatibility with standard ns-3.46.1 APIs
- Static setup and dynamic association can coexist in the same simulation
- Friend class declarations enable internal access without breaking encapsulation

## TracedCallback Additions

New trace sources for monitoring roaming and link quality:
- `m_roamingInitiatedLogger` - Logs roaming initiation events
- `m_roamingCompletedLogger` - Logs roaming completion with success/failure
- `m_backgroundScanCompleteLogger` - Logs background scan results
- `m_linkQualityUpdateLogger` - Periodic link quality metrics

## Configuration Parameters

New attributes added to StaWifiMac:
- Background scan interval (default: 5 seconds)
- Scan result maximum age (default: 30 seconds)
- Auto-reconnect enable/disable flag
- Background scanning enable/disable flag

These modifications enhance ns-3's WiFi simulation capabilities with realistic roaming behaviors, efficient setup mechanisms, and comprehensive monitoring tools while maintaining full compatibility with the base ns-3.46.1 framework.