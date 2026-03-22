# Dual-PHY Sniffer Module Documentation

## Quick Start

Minimal example to get started with beacon monitoring:

```cpp
#include "ns3/dual-phy-sniffer-helper.h"

// Create and configure sniffer
DualPhySnifferHelper sniffer;
sniffer.SetChannel(channel);                    // YansWifiChannel or SpectrumChannel
sniffer.SetScanningChannels({36, 40, 44, 48}); // 5 GHz channels to scan
sniffer.SetHopInterval(Seconds(1.0));           // Hop every 1 second

// Install on monitoring node
sniffer.Install(node, 36, Mac48Address("AA:BB:CC:DD:EE:FF"));
sniffer.StartChannelHopping();

// Query results
auto beacons = sniffer.GetAllBeacons();
```

See **[Usage Examples](#usage-examples)** section below for complete working examples.

## Overview

The Dual-PHY Sniffer module (`contrib/dual-phy-sniffer/`) provides multi-channel WiFi monitoring and Radio Resource Management (RRM) measurement capabilities for ns-3 simulations. Despite its name suggesting "dual-PHY", the actual implementation creates a single scanning radio per node that monitors beacons from existing APs using MAC-level channel switching.

### Channel Model Support

**The module is channel-agnostic** and supports both WiFi channel models:
- **YansWifiChannel** - Traditional Yans propagation model (used by config-simulation.cc)
- **SpectrumChannel** - Frequency-aware spectrum model (used by auto-roaming-kv-example.cc)

The helper automatically detects which channel type is set and uses the appropriate PHY helper internally. This allows the same API to work seamlessly with different propagation models.

## Architecture

### Actual Implementation

**Important**: The implementation differs from what the header comments suggest. Here's what actually happens:

1. **Single Scanning Radio Per Node**
   - Creates ONE scanning radio per Install() call
   - Uses AdhocWifiMac in listen-only mode
   - TX power set to -100 dBm to disable transmission
   - Auto-assigned MAC address by ns-3

2. **No Operating PHY Created**
   - The "operating MAC" is just the desired BSSID passed to Install()
   - Assumes an AP with that BSSID already exists elsewhere in the simulation
   - The helper stores this BSSID for tracking but doesn't create the AP

3. **MAC-Level Channel Switching** (Updated)
   - Uses `WifiMac::SwitchChannel(channelNumber)` for channel hopping
   - Automatically determines channel width from channel number
   - Supports variable channel widths (20/40/80/160 MHz)
   - More robust than direct PHY attribute manipulation

### Key Components

Located in `contrib/dual-phy-sniffer/`:
- `helper/dual-phy-sniffer-helper.{h,cc}` - Main helper class for setup and management
- `model/dual-phy-sniffer.{h,cc}` - Core model (currently template only)
- `examples/spectrum-dual-radio-example.cc` - Complete usage example

## Core APIs

### DualPhySnifferHelper Class

The main interface for creating and managing scanning radios.

#### Setup Methods

```cpp
// Set shared channel (REQUIRED - choose one)
// For Yans-based simulations:
void SetChannel(Ptr<YansWifiChannel> channel)
// For Spectrum-based simulations:
void SetChannel(Ptr<SpectrumChannel> channel)

// Configure channels to scan
// Can use width-encoded channel numbers:
// - 20 MHz: 36, 40, 44, 48, etc.
// - 40 MHz: 38, 46, 54, 62, etc.
// - 80 MHz: 42, 58, 106, 122, etc.
// - 160 MHz: 50, 114
void SetScanningChannels(const std::vector<uint8_t>& channels)

// Set channel hop interval (default: 1 second)
void SetHopInterval(Time interval)

// Set SSID (not actually used by scanning radio)
void SetSsid(Ssid ssid)

// Set callback for real-time beacon processing (optional)
void SetMeasurementCallback(MeasurementCallback callback)
```

#### Installation

```cpp
// Install scanning radio on a node
// Returns NetDeviceContainer with the scanning device
// desiredBssid is stored but no AP is created
NetDeviceContainer Install(Ptr<Node> node,
                           uint8_t operatingChannel,  // Not used for scanning
                           Mac48Address desiredBssid)  // Stored for tracking

// Start channel hopping for all scanning radios
void StartChannelHopping()
```

#### MAC Address Retrieval

```cpp
// Get the stored "operating" MAC (the desiredBssid passed to Install)
Mac48Address GetOperatingMac(uint32_t nodeId)

// Get the actual scanning radio's MAC address
Mac48Address GetScanningMac(uint32_t nodeId)
```

#### Beacon Cache Management

```cpp
// Get all stored beacon information
// Key: pair<receiverBssid, transmitterBssid>
const std::map<std::pair<Mac48Address, Mac48Address>, BeaconInfo>& GetAllBeacons()

// Get beacons received by specific receiver
std::vector<BeaconInfo> GetBeaconsReceivedBy(Mac48Address receiverBssid)

// Get beacons from specific transmitter
std::vector<BeaconInfo> GetBeaconsFrom(Mac48Address transmitterBssid)

// Clear beacon cache
void ClearBeaconCache()
void ClearBeaconsReceivedBy(Mac48Address receiverBssid)

// Configure cache limits
void SetBeaconMaxAge(Time maxAge)           // Default: 0 (no expiration)
void SetBeaconMaxEntries(uint32_t maxEntries) // Default: 0 (no limit)
```

## Data Structures

### DualPhyMeasurement

Real-time measurement data captured by scanning radio:
```cpp
struct DualPhyMeasurement {
    Mac48Address receiverBssid;    // The desiredBssid passed to Install()
    Mac48Address transmitterBssid; // Actual beacon transmitter
    uint8_t channel;               // Channel where detected
    double rcpi;                   // RCPI value (0-220)
    double rssi;                   // Raw RSSI in dBm
    double timestamp;              // Simulation time in seconds
};
```

### BeaconInfo

Cached beacon information:
```cpp
struct BeaconInfo {
    Mac48Address bssid;        // Transmitting AP's BSSID
    Mac48Address receivedBy;   // Receiver identifier (desiredBssid)
    double rssi;               // RSSI in dBm (latest)
    double rcpi;               // RCPI (0-220)
    double snr;                // Signal-to-Noise Ratio in dB
    uint8_t channel;           // Channel number
    WifiPhyBand band;          // Frequency band (2.4/5/6 GHz)
    Time timestamp;            // Last reception time
    uint32_t beaconCount;      // Total beacons received
};
```

## Channel Hopping Implementation

### MAC-Level SwitchChannel with Explicit Band and Conditional Width

The module uses `WifiMac::SwitchChannel(channelNumber)` for channel hopping with explicit band specification and conditional width handling:

**Installation** (`dual-phy-sniffer-helper.cc:114-134`):
```cpp
// Configure scanning radio with explicit band and conditional width
// For 2.4 GHz: must specify width=20 explicitly to avoid ambiguity (DSSS 22MHz, OFDM 20MHz, OFDM 40MHz)
// For 5 GHz: can use width=0 for auto-width detection
uint8_t firstChannel = m_scanningChannels[0];
std::string bandStr;
uint16_t width;
if (firstChannel >= 1 && firstChannel <= 14)
{
    bandStr = "BAND_2_4GHZ";
    width = 20; // Must be explicit for 2.4 GHz
}
else
{
    bandStr = "BAND_5GHZ";
    width = 0; // Auto-width for 5 GHz
}

std::ostringstream scanChStr;
scanChStr << "{" << (int)firstChannel << ", " << width << ", " << bandStr << ", 0}";
scanPhy.Set("ChannelSettings", StringValue(scanChStr.str()));
```

**Channel Hopping** (`dual-phy-sniffer-helper.cc:347-377`):
```cpp
void HopChannel(Ptr<WifiMac> scanMac, Ptr<WifiPhy> scanPhy, Mac48Address apBssid)
{
    // Get next channel in rotation
    uint32_t& idx = m_hopIndex[scanMac];
    idx = (idx + 1) % m_scanningChannels.size();
    uint16_t nextChannel = m_scanningChannels[idx];

    // Use MAC-level SwitchChannel which automatically determines channel width
    // SwitchChannel internally determines band from channel number
    scanMac->SwitchChannel(nextChannel, 0);  // linkId = 0 for single link
}
```

### Explicit Band with Conditional Width Approach

**Key Implementation Details:**
- **Explicit band specification**: Channels 1-14 use `WIFI_PHY_BAND_2_4GHZ`, others use `WIFI_PHY_BAND_5GHZ`
- **Conditional width specification**:
  - **2.4 GHz (1-14)**: Must use `width=20` explicitly to avoid multi-definition ambiguity
  - **5 GHz (36+)**: Uses `width=0` for automatic width detection
- **2.4 GHz ambiguity fix**: Each 2.4 GHz channel has multiple definitions (DSSS 22MHz, OFDM 20MHz, OFDM 40MHz); explicit width=20 selects OFDM 20MHz variant
- **5 GHz auto-detection**: Channel width determined from channel number encoding (no ambiguity)
- **Consistent with source changes**: Matches implementation in `wifi-mac.cc`, `ap-wifi-mac.cc`, `sta-wifi-mac.cc`
- **Prevents 6 GHz ambiguity**: Explicit band avoids conflicts with WiFi 6E channel numbers
- **Enables cross-band scanning**: Safe runtime switching between 2.4 GHz and 5 GHz bands

### Channel Width Support

Channel numbers encode both frequency and width per IEEE 802.11:
- **2.4 GHz (1-14)**: Always 20 MHz
- **5 GHz 20 MHz**: 36, 40, 44, 48, 52, 56, 60, 64...
- **5 GHz 40 MHz**: 38, 46, 54, 62, 102, 110, 118, 126...
- **5 GHz 80 MHz**: 42, 58, 106, 122, 138, 155
- **5 GHz 160 MHz**: 50, 114

### Advantages of Explicit Band + Conditional Width Approach

1. **2.4 GHz Ambiguity Resolution**: Explicit width=20 avoids DSSS/OFDM/40MHz channel ambiguity
2. **Automatic Width Detection for 5 GHz**: ns-3 determines width from channel number for 5 GHz
3. **Explicit Band Clarity**: Band specified based on channel number (2.4/5 GHz)
4. **Prevents 6 GHz Ambiguity**: Avoids conflicts with WiFi 6E overlapping channel numbers
5. **Cross-Band Scanning Support**: Enables safe runtime switching between 2.4 GHz and 5 GHz
6. **Proper PHY Reconfiguration**: Handles all PHY-dependent parameters correctly
7. **Station Manager Reset**: Ensures proper operation after channel change
8. **Standard Compliance**: Uses ns-3's internal channel database (IEEE 802.11 compliant)
9. **Future-Proof**: Compatible with future channel width extensions (e.g., 320 MHz)
10. **Code Simplification**: Eliminates ~30 lines of manual width decoding logic for 5 GHz
11. **Consistency**: Matches approach used throughout modified ns-3 WiFi stack

## Usage Examples

### Basic Setup with Variable Channel Widths

```cpp
// Create nodes
NodeContainer apNodes;
apNodes.Create(5);

// Setup spectrum channel
Ptr<MultiModelSpectrumChannel> spectrumChannel =
    CreateObject<MultiModelSpectrumChannel>();

// Configure sniffer helper
DualPhySnifferHelper snifferHelper;
snifferHelper.SetSpectrumChannel(spectrumChannel);

// Mix of channel widths for scanning:
// 36, 40: 20 MHz channels
// 38: 40 MHz channel (36+40 bonded)
// 42: 80 MHz channel (36+40+44+48 bonded)
snifferHelper.SetScanningChannels({36, 40, 38, 42});
snifferHelper.SetHopInterval(Seconds(2.0));

// Install scanning radios
for (uint32_t i = 0; i < apNodes.GetN(); i++) {
    Mac48Address apBssid = /* get from existing AP */;
    NetDeviceContainer scanDevice =
        snifferHelper.Install(apNodes.Get(i), 36, apBssid);
}

// Start channel hopping
snifferHelper.StartChannelHopping();
```

### Processing Results

```cpp
// After simulation
const auto& allBeacons = snifferHelper.GetAllBeacons();

// Build RCPI matrix with channel width information
for (const auto& entry : allBeacons) {
    Mac48Address txBssid = entry.second.bssid;
    Mac48Address rxId = entry.second.receivedBy;
    double rcpi = entry.second.rcpi;
    uint8_t channel = entry.second.channel;

    // Channel number indicates width used during measurement
    std::string width = "20MHz";  // Default
    if (channel == 38 || channel == 46 || channel == 54 || channel == 62)
        width = "40MHz";
    else if (channel == 42 || channel == 58 || channel == 106 || channel == 122)
        width = "80MHz";
    else if (channel == 50 || channel == 114)
        width = "160MHz";

    std::cout << "Measurement: " << txBssid << " -> " << rxId
              << " RCPI=" << rcpi << " Channel=" << (int)channel
              << " Width=" << width << std::endl;
}
```

## Working Examples

The module includes two complete working examples demonstrating different use cases:

### 1. dual-phy-sniffer-example.cc (Basic - YansWifiChannel)

Simple beacon monitoring scenario using YansWifiChannel:
- **Scenario**: 3 APs on 2.4 GHz channels (1, 6, 11)
- **Monitor**: Single node with scanning radio
- **Channel Model**: YansWifiChannel (traditional propagation)
- **Output**: Beacon cache printed at t=2s, t=5s, t=8s

**Run the example:**
```bash
./ns3 run dual-phy-sniffer-example

# With parameters:
./ns3 run "dual-phy-sniffer-example --simTime=15 --hopInterval=1.0 --verbose=1"
```

**Command-line options:**
- `--simTime` - Simulation duration in seconds (default: 10.0)
- `--hopInterval` - Channel hopping interval in seconds (default: 0.5)
- `--verbose` - Enable verbose logging (default: false)

### 2. spectrum-dual-radio-example.cc (Advanced - SpectrumChannel)

Advanced RRM measurements using SpectrumWifiPhy:
- **Scenario**: 5 APs with scanning radios on different 5 GHz channels
- **Channel Model**: SpectrumChannel (frequency-aware)
- **Measurements**: Builds 3D RCPI matrix [channel][transmitter][receiver]
- **Output**: CSV files for offline analysis

**Run the example:**
```bash
./ns3 run spectrum-dual-radio-example
```

Expected output includes CSV files: `rcpi_matrix_ch36.csv`, `rcpi_matrix_ch40.csv`, etc.

### Building and Testing

Both examples are now **enabled by default** in the build. To build and test:

```bash
# Build the project
./ns3 build

# Run basic example
./ns3 run dual-phy-sniffer-example

# Run advanced example
./ns3 run spectrum-dual-radio-example
```

If you encounter build issues, ensure all dependencies are installed and rebuild from clean:
```bash
./ns3 clean
./ns3 configure --enable-examples
./ns3 build
```

## How It Actually Works

### Installation Process

1. `Install()` creates a single scanning radio with:
   - AdhocWifiMac (no beacons, no association)
   - TX power = -100 dBm (effectively disabled)
   - AMPDU sizes = 0 (further TX disabling)

2. Stores the `desiredBssid` as "operating MAC" for tracking
3. Gets auto-assigned MAC for scanning radio
4. Stores both MAC and PHY pointers for channel hopping
5. Connects trace callback to capture received packets

### Channel Hopping Mechanism

1. Scanning radio starts on first channel in list
2. Every `hopInterval`, calls `ScheduleNextHop()`
3. `HopChannel()` uses `scanMac->SwitchChannel(nextChannel)`
4. MAC layer determines width, band, and primary20 from channel number
5. PHY settings updated automatically
6. Round-robin through all configured channels

### Beacon Detection

1. `MonitorSnifferRx` trace fires for all received packets
2. Filters for beacon frames only
3. Ignores self-beacons (where TX BSSID equals stored desiredBssid)
4. Converts RSSI to RCPI: `RCPI = (RSSI_dBm + 110) × 2`
5. Stores in beacon cache with metadata

## Implementation Details

### Channel Number to Configuration Mapping

When using explicit band with `width=0`, ns-3's `WifiPhyOperatingChannel` automatically decodes width:

**Band Determination:**
- **Channels 1-14**: Explicitly set to 2.4 GHz band
- **Other channels**: Explicitly set to 5 GHz band (36, 38, 40, 42, etc.)

**Width Auto-Detection:**
- **Channel 1-14**: 2.4 GHz band, 20 MHz width
- **Channel 36, 40, 44, 48...**: 5 GHz band, 20 MHz width
- **Channel 38, 46, 54, 62...**: 5 GHz band, 40 MHz width
- **Channel 42, 58, 106, 122...**: 5 GHz band, 80 MHz width
- **Channel 50, 114**: 5 GHz band, 160 MHz width

This mapping is performed by ns-3's internal channel database, ensuring IEEE 802.11 compliance.

### Querying PHY Configuration

After installation or channel switching, you can query the PHY for actual configuration:

```cpp
Ptr<WifiPhy> phy = wifiDevice->GetPhy();
uint8_t actualChannel = phy->GetChannelNumber();
uint16_t actualWidth = phy->GetChannelWidth();      // In MHz
WifiPhyBand actualBand = phy->GetPhyBand();          // WIFI_PHY_BAND_2_4GHZ, etc.
uint16_t centerFreq = phy->GetFrequency();           // In MHz
```

**Example Output:**
```
AP0: BSSID=00:00:00:00:00:01 Channel=36 Width=20MHz Band=5GHz
AP1: BSSID=00:00:00:00:00:02 Channel=38 Width=40MHz Band=5GHz
AP2: BSSID=00:00:00:00:00:03 Channel=42 Width=80MHz Band=5GHz
AP3: BSSID=00:00:00:00:00:04 Channel=44 Width=20MHz Band=5GHz
AP4: BSSID=00:00:00:00:00:05 Channel=50 Width=160MHz Band=5GHz
```

This confirms that `BAND_UNSPECIFIED` correctly auto-detects all channel parameters.

### RCPI Calculation
- RCPI (Received Channel Power Indicator): 0-220 scale
- Formula: `RCPI = (RSSI_dBm + 110) × 2`
- Clamped to valid range [0, 220]

### Beacon Filtering
- Filters out beacons where transmitter equals receiver (self-beacons)
- Only processes management frames of beacon type
- No MAC address even/odd filtering (contrary to old comments)

## Use Cases

### Radio Resource Management (RRM)
- Monitor beacon signal strength across different channel widths
- Build interference matrices considering channel bonding
- Evaluate impact of wide channels on coverage

### Dynamic Channel Width Optimization
- Test different channel width configurations
- Measure interference at various bandwidths
- Optimize channel assignment with width consideration

### Network Monitoring
- Track beacon reception across channels and widths
- Monitor wide channel utilization patterns
- Detect bandwidth-dependent interference

### Research Applications
- Study impact of channel bonding on beacon propagation
- Analyze coexistence of different channel widths
- Evaluate dynamic bandwidth allocation strategies

## Limitations

### Current Implementation
- **No actual dual-PHY**: Only creates scanning radio, not operating radio
- **Passive only**: Cannot transmit probe requests or any frames
- **Fixed hop sequence**: Round-robin only, no adaptive hopping
- **No operating AP creation**: Must create APs separately
- **Misleading naming**: "Dual-PHY" name doesn't match implementation

### Design Constraints
- Requires existing APs to monitor
- All radios must share same spectrum channel
- Cannot modify hop sequence during simulation
- No active scanning capability

## Performance Considerations

### Memory Usage
- Beacon cache grows linearly with unique TX-RX pairs
- Each entry stores ~100 bytes of data
- Use cache limits for long simulations

### Processing Overhead
- MAC-level switching adds minimal overhead
- PHY reconfiguration happens automatically
- Channel switching time depends on PHY model

### Channel Overlap and Interference

**Important**: Wide channels can cause interference with narrow channels, affecting beacon transmission rates.

**Example from Testing:**
```
Configuration: 5 APs on channels 36 (20MHz), 38 (40MHz), 42 (80MHz), 44 (20MHz), 50 (160MHz)

Beacon Reception Counts (10s simulation):
AP0 (Ch 36, 20MHz):  79 beacons transmitted
AP1 (Ch 38, 40MHz):  79 beacons transmitted
AP2 (Ch 42, 80MHz):  78 beacons transmitted
AP3 (Ch 44, 20MHz):  19 beacons transmitted  ← Significantly reduced!
AP4 (Ch 50, 160MHz): 77 beacons transmitted
```

**Why AP3 has reduced beacons:**
- Channel 44 (5210-5230 MHz) is **completely inside** Channel 42's 80 MHz bandwidth (5170-5250 MHz)
- Channel 44 is **also inside** Channel 50's 160 MHz bandwidth (5170-5330 MHz)
- When AP2 or AP4 transmit, their wide signals cause Clear Channel Assessment (CCA) to detect channel 44 as busy
- AP3 must defer transmission, reducing beacon transmission rate to ~25% of expected

**Frequency Overlaps:**
- Ch 36 (20MHz): 5170-5190 MHz
- Ch 38 (40MHz): 5170-5210 MHz - overlaps with Ch 36
- Ch 42 (80MHz): 5170-5250 MHz - overlaps with Ch 36, 38, **44**
- Ch 44 (20MHz): 5210-5230 MHz - overlapped by Ch 42, 50
- Ch 50 (160MHz): 5170-5330 MHz - overlaps with ALL channels

This behavior is **realistic** and matches real-world WiFi operation with overlapping channels.

### Recommended Settings
- Hop interval: 1-3 seconds (based on beacon interval)
- Mix channel widths for comprehensive coverage
- **Use non-overlapping channels when possible** (e.g., 36, 52, 100, 116, 132, 149)
- Consider channel overlap effects on beacon reception rates
- Cache max age: 30-60 seconds for dynamic scenarios
- Cache max entries: 1000-5000 for large networks

## Debugging and Troubleshooting

### Enable Logging

```bash
# Basic debugging
NS_LOG="DualPhySnifferHelper=level_all" ./ns3 run example

# Detailed WiFi MAC debugging
NS_LOG="DualPhySnifferHelper=level_all:WifiMac=info" ./ns3 run example

# Full WiFi stack debugging
NS_LOG="DualPhySnifferHelper=level_all:WifiPhy=info:WifiMac=info" ./ns3 run example

# Channel switching debugging
NS_LOG="DualPhySnifferHelper=level_all:WifiPhyOperatingChannel=info" ./ns3 run example
```

### Common Issues and Solutions

#### 1. **No beacons detected**

**Symptoms:**
- `GetAllBeacons()` returns empty map
- Beacon cache shows 0 entries

**Possible causes and fixes:**
- ✓ **APs not broadcasting**: Verify APs have `BeaconGeneration=true` and `BeaconInterval` set
  ```cpp
  mac.SetType("ns3::ApWifiMac",
              "BeaconGeneration", BooleanValue(true),
              "BeaconInterval", TimeValue(MicroSeconds(102400)));
  ```
- ✓ **Channel mismatch**: Ensure scanning channels match AP operating channels
  ```cpp
  sniffer.SetScanningChannels({36, 40, 44});  // Must match AP channels
  ```
- ✓ **Shared channel not configured**: All devices must use the SAME spectrum/Yans channel instance
  ```cpp
  Ptr<YansWifiChannel> channel = channelHelper.Create();
  sniffer.SetChannel(channel);  // Same instance as APs
  ```
- ✓ **Channel hopping not started**: Must call `StartChannelHopping()` after all `Install()` calls
  ```cpp
  sniffer.Install(node, 36, bssid);
  sniffer.StartChannelHopping();  // ← Don't forget this!
  ```
- ✓ **Simulation too short**: Allow enough time for channel hopping to visit all channels
  ```cpp
  // If hopInterval=1s and 5 channels, need at least 5+ seconds
  Simulator::Stop(Seconds(10.0));
  ```

#### 2. **Channel width mismatch or detection issues**

**Symptoms:**
- Beacons detected on wrong channel numbers
- Channel width doesn't match expected value

**Fixes:**
- ✓ **Use correct channel numbers** for desired width (see Channel Width Support section)
  ```cpp
  // 20 MHz: 36, 40, 44, 48
  // 40 MHz: 38, 46, 54, 62
  // 80 MHz: 42, 58, 106, 122
  sniffer.SetScanningChannels({36, 40, 44, 48});  // All 20 MHz
  ```
- ✓ **Check AP PHY configuration** matches scanning expectations
  ```cpp
  // Query AP configuration
  Ptr<WifiPhy> apPhy = apDevice->GetPhy();
  std::cout << "AP Channel: " << (int)apPhy->GetChannelNumber() << "\n";
  std::cout << "AP Width: " << apPhy->GetChannelWidth() << " MHz\n";
  ```

#### 3. **Unexpected receiver BSSIDs in beacon cache**

**Symptoms:**
- BeaconInfo shows unexpected `receivedBy` addresses
- Confusion about MAC address assignment

**Understanding:**
- ⚠️ **receiverBssid** is the `desiredBssid` parameter passed to `Install()` - it's just an identifier
- ⚠️ **NOT** the scanning radio's actual MAC address
- ⚠️ Used for tracking which "virtual receiver" detected the beacon

**Correct usage:**
```cpp
// desiredBssid is arbitrary identifier (use AP's BSSID for clarity)
Mac48Address apBssid = apDevice->GetMac()->GetAddress();
sniffer.Install(node, 36, apBssid);  // apBssid used as receiver ID

// Later, query beacons "received by" this identifier
auto beacons = sniffer.GetBeaconsReceivedBy(apBssid);
```

#### 4. **Build errors after enabling examples**

**Symptoms:**
- Compilation fails with linker errors
- Missing library dependencies

**Fixes:**
- ✓ **Rebuild from clean** if switching between configurations
  ```bash
  ./ns3 clean
  ./ns3 configure --enable-examples --enable-tests
  ./ns3 build
  ```
- ✓ **Check dependencies** are installed (mobility, propagation, spectrum modules)
- ✓ **Verify CMakeLists.txt** has correct library links
  ```cmake
  LIBRARIES_TO_LINK ${libdual-phy-sniffer}
  ```

#### 5. **Reduced beacon reception on some APs**

**Symptoms:**
- Some APs show significantly fewer beacons than others
- Uneven beacon counts across APs

**This is likely NORMAL behavior** due to channel overlap (see Performance Considerations section):
- Wide channels (40/80/160 MHz) cause CCA-based deferral on overlapping narrow channels
- Example: Channel 44 (20 MHz) inside Channel 42 (80 MHz) sees ~75% reduction
- **Solution**: Use non-overlapping channels when possible (36, 52, 100, 116, 132, 149)

#### 6. **Channel-agnostic API confusion**

**Symptoms:**
- Unsure which `SetChannel()` to use
- Ambiguous overload errors

**Fixes:**
- ✓ **For YansWifiChannel**: `sniffer.SetChannel(Ptr<YansWifiChannel>)`
- ✓ **For SpectrumChannel**: `sniffer.SetChannel(Ptr<SpectrumChannel>)`
- ✓ **Use explicit cast if needed**:
  ```cpp
  Ptr<MultiModelSpectrumChannel> mmChannel = CreateObject<MultiModelSpectrumChannel>();
  sniffer.SetChannel(Ptr<SpectrumChannel>(mmChannel));  // Explicit cast
  ```

### Verification Checklist

Before running your simulation, verify:

- [ ] Created YansWifiChannel OR SpectrumChannel
- [ ] Called `SetChannel()` on sniffer helper
- [ ] Called `SetScanningChannels()` with valid channel numbers
- [ ] Created APs with `BeaconGeneration=true`
- [ ] APs and sniffer use the SAME channel instance
- [ ] Called `Install()` for each monitoring node
- [ ] Called `StartChannelHopping()` after all installations
- [ ] Simulation runs long enough for multiple channel hops
- [ ] Scanning channels match AP operating channels

## Integration Notes

### With Existing WiFi Networks
- Install scanning radios after creating APs
- Use same spectrum channel for all devices
- Can scan APs using different channel widths
- Compatible with standard WiFi traffic

### Data Export
- Beacon cache provides complete measurement history
- Channel width information preserved in measurements
- Export to CSV/JSON for external analysis
- Real-time callbacks for streaming applications

## Testing Status

### Unit Tests

**Current Status**: ⚠️ Stub tests only

The test suite (`test/dual-phy-sniffer-test-suite.cc`) currently contains only placeholder test cases. Comprehensive unit tests are needed for:

- [ ] Beacon detection accuracy across different channel models
- [ ] Channel hopping correctness and timing
- [ ] Beacon cache operations (add, query, clear)
- [ ] RCPI calculation validation
- [ ] Both YansWifiChannel and SpectrumChannel support
- [ ] MAC address tracking and retrieval
- [ ] Multi-node scenarios

**Running existing tests:**
```bash
./ns3 build
./test.py --suite=dual-phy-sniffer
```

### Integration Testing

**Status**: ✅ Validated through production use

The module has been extensively validated through integration with other modules:

- ✅ **beacon-neighbor-protocol-11k**: Dynamic neighbor discovery
- ✅ **auto-roaming-kv**: Roaming decision support
- ✅ **final-simulation**: Multi-AP production scenarios
- ✅ **config-simulation**: Large-scale network testing

### Example Validation

Both working examples have been manually tested:

- ✅ **dual-phy-sniffer-example.cc**: YansWifiChannel, 2.4 GHz, basic beacon detection
- ✅ **spectrum-dual-radio-example.cc**: SpectrumChannel, 5 GHz, RRM matrix generation

### Known Test Gaps

- No automated regression tests for channel hopping
- No validation against real hardware beacon timing
- No stress testing with 50+ APs
- No fuzzing of beacon cache queries
- No performance benchmarking

**Contributions welcome** for expanding test coverage!

## Future Improvements Needed

- Rename module to reflect actual functionality (e.g., "beacon-monitor")
- **Add comprehensive unit tests** (highest priority)
- Actually implement dual-PHY if needed
- Add active scanning support with probe requests
- Improve documentation accuracy
- Add adaptive channel hopping based on beacon density
- Support for channel switch announcements (CSA)
- Add automated integration tests