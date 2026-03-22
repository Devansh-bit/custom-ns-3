# LeverApi Module Documentation

## Overview

The LeverApi module (`contrib/lever-api/`) provides a unified interface for dynamic WiFi PHY configuration in ns-3 simulations. It enables runtime changes to transmission power, CCA thresholds, RX sensitivity, and channel settings through a trace-driven architecture that delegates channel operations to the ns-3 WiFi MAC layer.

**Key Features:**
- Runtime PHY parameter adjustment without device recreation
- Automatic channel switching with band/width detection
- AP-initiated channel changes propagate automatically to all connected STAs
- Trace-driven configuration updates
- Compatible with both AP and STA nodes
- Supports multiple APs and STAs in a single simulation

## Architecture

### Core Components

Located in `contrib/lever-api/`:
- `model/lever-api.{h,cc}` - Core LeverConfig and LeverApi classes
- `helper/lever-api-helper.{h,cc}` - Helper class for easy installation
- `examples/` - Comprehensive usage examples

### Two-Layer Design

**1. LeverConfig (Configuration State)**
- Stores desired PHY configuration (TxPower, CCA, RxSensitivity, ChannelSettings)
- Fires traces when configuration changes
- Provides getter methods for current state
- No direct PHY access

**2. LeverApi (Application Layer)**
- ns-3 Application installed on nodes
- Listens to LeverConfig trace callbacks
- Applies configuration changes to actual WiFi PHY
- Delegates channel switching to WifiMac layer
- Handles PHY discovery and updates

**Delegation Architecture:**
```
LeverConfig::SwitchChannel()
    ↓ (fires trace)
LeverApi::OnChannelSettingsChanged()
    ↓ (gets WifiMac)
WifiMac::SwitchChannel()
    ↓ (if ApWifiMac)
ApWifiMac::SwitchChannel()
    ↓ (propagates to all STAs)
WifiPhy::ConfigureStandard()
```

## Core APIs

### LeverConfig Class

Configuration storage and trace source.

#### Creation and Basic Configuration

```cpp
// Create configuration object
Ptr<LeverConfig> config = CreateObject<LeverConfig>();

// Set transmission power (both Start and End set together)
config->SetTxPower(20.0);  // dBm

// Set CCA Energy Detection threshold
config->SetCcaEdThreshold(-82.0);  // dBm

// Set RX sensitivity
config->SetRxSensitivity(-93.0);  // dBm
```

#### Channel Switching (Recommended Method)

```cpp
// Smart channel switching - automatically determines band and width
// For 2.4 GHz (channels 1-14): Always 20 MHz
config->SwitchChannel(6);   // Ch 6, 20 MHz, 2.4 GHz

// For 5 GHz: Channel number encodes width
config->SwitchChannel(36);  // Ch 36, 20 MHz, 5 GHz
config->SwitchChannel(38);  // Ch 38, 40 MHz, 5 GHz (bonds 36+40)
config->SwitchChannel(42);  // Ch 42, 80 MHz, 5 GHz (bonds 36+40+44+48)
config->SwitchChannel(50);  // Ch 50, 160 MHz, 5 GHz
```

**Channel Number to Width Mapping:**
- **2.4 GHz (1-14)**: Always 20 MHz
- **5 GHz 20 MHz**: 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, etc.
- **5 GHz 40 MHz**: 38, 46, 54, 62, 102, 110, 118, 126, 134, 142, 151, 159
- **5 GHz 80 MHz**: 42, 58, 106, 122, 138, 155
- **5 GHz 160 MHz**: 50, 114

#### Trace Connection

```cpp
// Connect to configuration change traces
config->TraceConnectWithoutContext(
    "TxPowerStart",
    MakeCallback(&OnTxPowerChanged)
);

config->TraceConnectWithoutContext(
    "ChannelSettings",
    MakeCallback(&OnChannelChanged)
);

// Callback signatures
void OnTxPowerChanged(double oldValue, double newValue);
void OnChannelChanged(uint8_t channel, uint16_t width,
                     WifiPhyBandType band, uint8_t primary20Index);
```

#### Getter Methods

```cpp
// Query current configuration state
double txPower = config->GetTxPowerStart();      // dBm
double ccaThreshold = config->GetCcaEdThreshold(); // dBm
double rxSens = config->GetRxSensitivity();      // dBm
uint8_t channel = config->GetChannelNumber();
uint16_t width = config->GetChannelWidth();      // MHz
WifiPhyBandType band = config->GetBand();
uint8_t primary20 = config->GetPrimary20Index();
```

### LeverApi Class

Application that applies LeverConfig to WiFi PHY.

#### Manual Usage (Advanced)

```cpp
// Get LeverApi from installed application
Ptr<LeverApi> leverApi =
    leverApp.Get(0)->GetObject<LeverApi>();

// Direct channel switch (delegates to WifiMac)
leverApi->SwitchChannel(channelNumber, linkId);

// Query WiFi device
Ptr<WifiNetDevice> device = leverApi->GetWifiNetDevice();
```

### LeverApiHelper Class

Simplified installation interface.

#### Basic Installation

```cpp
// Create configuration
Ptr<LeverConfig> config = CreateObject<LeverConfig>();
config->SetTxPower(20.0);
config->SetCcaEdThreshold(-82.0);
config->SetRxSensitivity(-93.0);
config->SwitchChannel(36);

// Create helper and install
LeverApiHelper leverHelper(config);
ApplicationContainer leverApp = leverHelper.Install(node);
leverApp.Start(Seconds(0.0));
leverApp.Stop(Seconds(simulationTime));
```

## Channel Switching Implementation

### Delegation to WifiMac Layer

LeverApi delegates all channel switching operations to `WifiMac::SwitchChannel()`, which provides:

1. **Automatic Band/Width Detection**: Channel number uniquely encodes frequency and width
2. **PHY Reconfiguration**: Handles all PHY-dependent parameters
3. **AP Propagation**: ApWifiMac automatically propagates to all associated STAs
4. **Station Manager Reset**: Ensures proper operation after channel change
5. **Standard Compliance**: Uses ns-3's internal IEEE 802.11 channel database

### Implementation Details

**LeverConfig::SwitchChannel()** (`lever-api.cc:115-191`):
```cpp
void LeverConfig::SwitchChannel(uint16_t channelNumber)
{
    // Decode channel number to determine band and width
    uint8_t actualChannel;
    uint16_t width;
    WifiPhyBandType band;

    if (channelNumber >= 1 && channelNumber <= 14)
    {
        // 2.4 GHz: Always 20 MHz
        actualChannel = channelNumber;
        width = 20;
        band = BAND_2_4GHZ;
    }
    else
    {
        // 5 GHz: Width encoded in channel number
        band = BAND_5GHZ;

        // Determine width from channel number
        if (channelNumber == 50 || channelNumber == 114)
            width = 160;  // 160 MHz channels
        else if (channelNumber == 42 || channelNumber == 58 ||
                 channelNumber == 106 || channelNumber == 122 ||
                 channelNumber == 138 || channelNumber == 155)
            width = 80;   // 80 MHz channels
        else if (channelNumber == 38 || channelNumber == 46 ||
                 channelNumber == 54 || channelNumber == 62 ||
                 channelNumber == 102 || channelNumber == 110 ||
                 channelNumber == 118 || channelNumber == 126 ||
                 channelNumber == 134 || channelNumber == 142 ||
                 channelNumber == 151 || channelNumber == 159)
            width = 40;   // 40 MHz channels
        else
            width = 20;   // 20 MHz channels (default)

        actualChannel = channelNumber;
    }

    // Update internal state
    m_channelNumber = actualChannel;
    m_channelWidth = width;
    m_band = band;
    m_primary20Index = 0;

    // Fire trace to trigger callback
    m_channelSettingsTrace(m_channelNumber, m_channelWidth,
                          m_band, m_primary20Index);
}
```

**LeverApi::SwitchChannel()** (`lever-api.cc:277-311`):
```cpp
void LeverApi::SwitchChannel(uint16_t channelNumber, uint8_t linkId)
{
    // Get WiFi device and MAC
    Ptr<WifiNetDevice> device = GetWifiNetDevice();
    Ptr<WifiMac> mac = device->GetMac();

    // Delegate to WifiMac::SwitchChannel
    // This handles all PHY configuration automatically
    mac->SwitchChannel(channelNumber, linkId);

    // Update LeverConfig state for getter consistency
    if (m_config)
    {
        m_config->SwitchChannel(channelNumber);
    }
}
```

### ApWifiMac Automatic STA Propagation

When `SwitchChannel()` is called on an ApWifiMac:

1. **AP Changes Channel**: PHY reconfigured to new channel
2. **Disconnects All STAs**: Clears association list (STAs will reassociate)
3. **Propagates to STAs**: Each associated STA's PHY switched to same channel
4. **Automatic Reassociation**: STAs detect AP on new channel and reassociate

**Result**: Entire BSS moves to new channel atomically.

## Usage Examples

### Single AP with Multiple STAs

```cpp
#include "ns3/lever-api-helper.h"

// Create nodes
NodeContainer apNode;
apNode.Create(1);
NodeContainer staNodes;
staNodes.Create(3);

// Setup WiFi (spectrum channel, PHY, MAC)
// ... standard WiFi setup code ...

// Create shared LeverConfig for AP
Ptr<LeverConfig> apConfig = CreateObject<LeverConfig>();
apConfig->SetTxPower(20.0);
apConfig->SetCcaEdThreshold(-82.0);
apConfig->SetRxSensitivity(-93.0);
apConfig->SwitchChannel(36);  // Start on 5 GHz channel 36

// Install LeverApi on AP
LeverApiHelper leverHelper(apConfig);
ApplicationContainer apLeverApp = leverHelper.Install(apNode.Get(0));
apLeverApp.Start(Seconds(0.0));
apLeverApp.Stop(Seconds(simTime));

// Get LeverApi pointer for runtime control
Ptr<LeverApi> apLeverApi = apLeverApp.Get(0)->GetObject<LeverApi>();

// Schedule channel switches - STAs automatically follow AP
Simulator::Schedule(Seconds(5.0), [apLeverApi]() {
    apLeverApi->SwitchChannel(42);  // Move to 80 MHz channel
    // All associated STAs automatically switch to channel 42
});

Simulator::Schedule(Seconds(10.0), [apLeverApi]() {
    apLeverApi->SwitchChannel(6);   // Move to 2.4 GHz
    // All associated STAs automatically switch to channel 6
});
```

### Multiple Independent APs

```cpp
// Create 5 APs on different channels
NodeContainer apNodes;
apNodes.Create(5);

std::vector<uint8_t> apChannels = {36, 40, 44, 48, 52};
std::vector<double> apTxPowers = {20.0, 20.0, 20.0, 20.0, 20.0};
std::vector<Ptr<LeverConfig>> apConfigs;
std::vector<ApplicationContainer> leverApps;

// Install LeverApi on each AP
for (uint32_t i = 0; i < apNodes.GetN(); i++)
{
    // Create unique LeverConfig for each AP
    Ptr<LeverConfig> config = CreateObject<LeverConfig>();
    config->SetTxPower(apTxPowers[i]);
    config->SetCcaEdThreshold(-82.0);
    config->SetRxSensitivity(-93.0);
    config->SwitchChannel(apChannels[i]);
    apConfigs.push_back(config);

    // Connect traces for monitoring
    std::string apName = "AP" + std::to_string(i);
    config->TraceConnectWithoutContext("TxPowerStart",
        MakeBoundCallback(&OnTxPowerChanged, apName));
    config->TraceConnectWithoutContext("ChannelSettings",
        MakeBoundCallback(&OnChannelChanged, apName));

    // Install LeverApi
    LeverApiHelper leverHelper(config);
    ApplicationContainer app = leverHelper.Install(apNodes.Get(i));
    app.Start(Seconds(0.0));
    app.Stop(Seconds(simTime));
    leverApps.push_back(app);
}

// Runtime control of individual APs
Simulator::Schedule(Seconds(5.0), [&apConfigs]() {
    // Reduce AP0's power
    apConfigs[0]->SetTxPower(10.0);

    // Move AP2 to different channel
    apConfigs[2]->SwitchChannel(100);
});
```

### Multi-AP Coordinated Channel Switch

```cpp
// Move all APs to new channel simultaneously
void CoordinatedChannelSwitch(
    std::vector<Ptr<LeverConfig>>& apConfigs,
    uint8_t newChannel)
{
    for (auto& config : apConfigs)
    {
        config->SwitchChannel(newChannel);
        // Each AP's associated STAs automatically follow
    }
}

// Schedule coordinated switch
Simulator::Schedule(Seconds(10.0),
    &CoordinatedChannelSwitch,
    std::ref(apConfigs),
    42  // All APs move to channel 42 (80 MHz)
);
```

### Independent STA Configuration

```cpp
// Create separate configs for AP and STAs
Ptr<LeverConfig> apConfig = CreateObject<LeverConfig>();
Ptr<LeverConfig> sta1Config = CreateObject<LeverConfig>();
Ptr<LeverConfig> sta2Config = CreateObject<LeverConfig>();

// Configure each independently
apConfig->SetTxPower(20.0);
apConfig->SwitchChannel(36);

sta1Config->SetTxPower(15.0);
sta1Config->SwitchChannel(36);  // Must match AP initially

sta2Config->SetTxPower(18.0);
sta2Config->SwitchChannel(36);

// Install on respective nodes
LeverApiHelper apHelper(apConfig);
apHelper.Install(apNode.Get(0)).Start(Seconds(0.0));

LeverApiHelper sta1Helper(sta1Config);
sta1Helper.Install(staNodes.Get(0)).Start(Seconds(0.0));

LeverApiHelper sta2Helper(sta2Config);
sta2Helper.Install(staNodes.Get(1)).Start(Seconds(0.0));

// STAs can independently change power (but channel changes disconnect them)
Simulator::Schedule(Seconds(5.0), [sta1Config]() {
    sta1Config->SetTxPower(10.0);  // OK - just power change
});

Simulator::Schedule(Seconds(8.0), [sta1Config]() {
    sta1Config->SwitchChannel(40);  // WARNING - disconnects from AP!
});
```

### Dynamic Power Control Based on Conditions

```cpp
// Adaptive power control callback
void AdaptivePowerControl(
    Ptr<LeverConfig> config,
    Ptr<Node> node,
    double targetRssi)
{
    // Get current stats (hypothetical - implement based on your needs)
    double currentRssi = GetAverageRssi(node);
    double currentPower = config->GetTxPowerStart();

    if (currentRssi < targetRssi - 5.0)
    {
        // Increase power
        double newPower = std::min(currentPower + 2.0, 30.0);
        config->SetTxPower(newPower);
        NS_LOG_INFO("Node " << node->GetId()
                    << ": Increasing power to " << newPower << " dBm");
    }
    else if (currentRssi > targetRssi + 5.0)
    {
        // Decrease power
        double newPower = std::max(currentPower - 2.0, 0.0);
        config->SetTxPower(newPower);
        NS_LOG_INFO("Node " << node->GetId()
                    << ": Decreasing power to " << newPower << " dBm");
    }

    // Schedule next check
    Simulator::Schedule(Seconds(1.0),
        &AdaptivePowerControl, config, node, targetRssi);
}

// Start adaptive control
Simulator::Schedule(Seconds(1.0),
    &AdaptivePowerControl, apConfig, apNode.Get(0), -50.0);
```

### Channel Width Selection Based on User Preference

```cpp
// Helper function to select appropriate channel based on width
uint8_t SelectChannelForWidth(uint16_t desiredWidth)
{
    if (desiredWidth == 20)
        return 36;   // 5 GHz, 20 MHz
    else if (desiredWidth == 40)
        return 38;   // 5 GHz, 40 MHz (bonds 36+40)
    else if (desiredWidth == 80)
        return 42;   // 5 GHz, 80 MHz (bonds 36+40+44+48)
    else if (desiredWidth == 160)
        return 50;   // 5 GHz, 160 MHz
    else
        return 36;   // Default to 20 MHz
}

// Configure based on command line argument
uint16_t channelWidth = 40;  // From command line
CommandLine cmd;
cmd.AddValue("channelWidth", "Channel width (20/40/80/160)", channelWidth);
cmd.Parse(argc, argv);

uint8_t channel = SelectChannelForWidth(channelWidth);
apConfig->SwitchChannel(channel);
NS_LOG_INFO("Configured AP on channel " << (int)channel
            << " with " << channelWidth << " MHz bandwidth");
```

## Important Usage Notes

### Things to Be Careful Of

#### 1. Channel Changes Cause Disconnection

**Problem**: When a STA changes channel independently, it disconnects from AP.

```cpp
// BAD: STA changes channel independently
sta1Config->SwitchChannel(40);  // Disconnects from AP on channel 36!
```

**Solution**: Only AP should initiate channel changes (propagates to STAs automatically).

```cpp
// GOOD: AP changes channel, STAs follow automatically
apLeverApi->SwitchChannel(40);  // AP and all STAs move together
```

#### 2. Configuration Must Match Initial PHY Settings

**Problem**: LeverConfig should match initial PHY configuration.

```cpp
// Configure PHY during WiFi setup
phy.Set("ChannelSettings", StringValue("{36, 20, BAND_5GHZ, 0}"));

// BAD: LeverConfig doesn't match
config->SwitchChannel(6);  // Mismatch with PHY!

// GOOD: LeverConfig matches initial PHY
config->SwitchChannel(36);  // Matches initial PHY settings
```

#### 3. Application Start/Stop Timing

**Problem**: LeverApi must be running when configuration changes occur.

```cpp
// BAD: Config change before app starts
config->SetTxPower(15.0);  // At t=0s
leverApp.Start(Seconds(1.0));  // Starts at t=1s - misses change!

// GOOD: App starts first
leverApp.Start(Seconds(0.0));
Simulator::Schedule(Seconds(1.0), [config]() {
    config->SetTxPower(15.0);  // App is running, will apply
});
```

#### 4. Spectrum Channel Must Be Shared

**Problem**: All WiFi devices must use same spectrum channel for communication.

```cpp
// BAD: Different spectrum channels
Ptr<SpectrumChannel> channel1 = CreateObject<MultiModelSpectrumChannel>();
Ptr<SpectrumChannel> channel2 = CreateObject<MultiModelSpectrumChannel>();

apPhy.SetChannel(channel1);
staPhy.SetChannel(channel2);  // Different channels - no communication!

// GOOD: Shared spectrum channel
Ptr<SpectrumChannel> sharedChannel = CreateObject<MultiModelSpectrumChannel>();

apPhy.SetChannel(sharedChannel);
staPhy.SetChannel(sharedChannel);  // Same channel - can communicate
```

#### 5. 2.4 GHz Channel Width is Always 20 MHz

**Problem**: Attempting to use non-20 MHz widths in 2.4 GHz.

```cpp
// BAD: 2.4 GHz doesn't support 40 MHz in this implementation
config->SwitchChannel(6);  // Always 20 MHz, regardless of intent

// Channel numbers 1-14 are hardcoded to 20 MHz bandwidth
```

**Note**: If you need 2.4 GHz 40 MHz channels, you must modify the channel decoding logic in `LeverConfig::SwitchChannel()`.

#### 6. Channel Number Must Be Valid

**Problem**: Invalid channel numbers cause undefined behavior.

```cpp
// BAD: Invalid channel numbers
config->SwitchChannel(15);   // Not a valid 2.4 GHz channel (1-14)
config->SwitchChannel(39);   // Not a valid 5 GHz channel

// GOOD: Use valid channel numbers
config->SwitchChannel(11);   // Valid 2.4 GHz
config->SwitchChannel(36);   // Valid 5 GHz 20 MHz
config->SwitchChannel(38);   // Valid 5 GHz 40 MHz
```

#### 7. Power Limits

**Problem**: TX power outside valid range.

```cpp
// BAD: Unrealistic power values
config->SetTxPower(-10.0);   // Negative power
config->SetTxPower(100.0);   // Too high

// GOOD: Realistic power ranges
config->SetTxPower(10.0);    // 10 dBm (10 mW)
config->SetTxPower(20.0);    // 20 dBm (100 mW) - typical for 5 GHz
config->SetTxPower(30.0);    // 30 dBm (1 W) - maximum for some scenarios
```

#### 8. Multiple LeverApi Instances Per Node

**Problem**: Installing multiple LeverApi instances on same node.

```cpp
// BAD: Multiple LeverApi on one node
LeverApiHelper helper1(config1);
helper1.Install(node);

LeverApiHelper helper2(config2);
helper2.Install(node);  // Second instance may conflict!

// GOOD: One LeverApi per node, update existing config
LeverApiHelper helper(config);
ApplicationContainer app = helper.Install(node);

// Later, update the same config
config->SetTxPower(15.0);
config->SwitchChannel(40);
```

### Best Practices

#### 1. Separate Configs for Each Node

```cpp
// Create unique LeverConfig for each node
std::vector<Ptr<LeverConfig>> configs;
for (uint32_t i = 0; i < nodes.GetN(); i++)
{
    Ptr<LeverConfig> config = CreateObject<LeverConfig>();
    config->SetTxPower(20.0);
    config->SwitchChannel(36);
    configs.push_back(config);

    LeverApiHelper helper(config);
    helper.Install(nodes.Get(i)).Start(Seconds(0.0));
}
```

#### 2. Use Trace Callbacks for Monitoring

```cpp
// Track all configuration changes
config->TraceConnectWithoutContext("TxPowerStart",
    MakeCallback(&OnTxPowerChanged));
config->TraceConnectWithoutContext("CcaEdThreshold",
    MakeCallback(&OnCcaChanged));
config->TraceConnectWithoutContext("RxSensitivity",
    MakeCallback(&OnRxSensChanged));
config->TraceConnectWithoutContext("ChannelSettings",
    MakeCallback(&OnChannelChanged));
```

#### 3. Coordinate Multi-AP Systems

```cpp
// Use a central controller for coordinated changes
class NetworkController
{
public:
    void AddAp(Ptr<LeverConfig> config) {
        m_apConfigs.push_back(config);
    }

    void ChangeAllChannels(uint8_t newChannel) {
        for (auto& config : m_apConfigs) {
            config->SwitchChannel(newChannel);
        }
    }

    void AdaptivePowerControl() {
        // Implement inter-AP power coordination
        // to minimize interference
    }

private:
    std::vector<Ptr<LeverConfig>> m_apConfigs;
};
```

#### 4. Initial Configuration at t=0

```cpp
// Apply initial configuration before starting simulation
Ptr<LeverConfig> config = CreateObject<LeverConfig>();
config->SetTxPower(20.0);
config->SetCcaEdThreshold(-82.0);
config->SetRxSensitivity(-93.0);
config->SwitchChannel(36);

// Install and start immediately
LeverApiHelper helper(config);
ApplicationContainer app = helper.Install(node);
app.Start(Seconds(0.0));  // Start at simulation start

// Configuration is applied when app starts
```

## Performance Considerations

### Memory Usage

- LeverConfig: ~200 bytes per instance
- LeverApi: ~500 bytes per instance (includes Application overhead)
- Trace connections: ~100 bytes per callback

**Recommendation**: One LeverConfig + LeverApi per WiFi node is efficient.

### Computational Overhead

- Configuration changes: Minimal overhead (trace callback + PHY update)
- Channel switching: Moderate overhead (PHY reconfiguration + station manager reset)
- AP channel propagation: O(n) where n = number of associated STAs

### Channel Switching Time

- Actual switching is instantaneous in simulation (no PHY switching delay modeled)
- STAs reassociate after AP channel change (~50-100 ms in real time, but depends on beacon interval)

## Integration with Other Modules

### With RRM Controllers

```cpp
// RRM controller uses LeverApi for dynamic channel assignment
void RrmController::UpdateApChannel(uint32_t apId, uint8_t newChannel)
{
    m_apConfigs[apId]->SwitchChannel(newChannel);
    // All STAs associated with this AP automatically follow
}
```

### With Interference Management

```cpp
// Interference mitigation through power control
void InterferenceManager::MitigateInterference(
    std::vector<Ptr<LeverConfig>>& apConfigs,
    const InterferenceMatrix& matrix)
{
    for (uint32_t i = 0; i < apConfigs.size(); i++)
    {
        double optimalPower = CalculateOptimalPower(i, matrix);
        apConfigs[i]->SetTxPower(optimalPower);
    }
}
```

### With Mobility Models

```cpp
// Adapt power based on node positions
void MobilityBasedPowerControl(
    Ptr<Node> node,
    Ptr<LeverConfig> config,
    Vector targetPosition)
{
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    Vector currentPos = mobility->GetPosition();
    double distance = CalculateDistance(currentPos, targetPosition);

    // Adjust power based on distance
    double requiredPower = CalculateRequiredPower(distance);
    config->SetTxPower(requiredPower);
}
```

## Debugging

### Enable Logging

```bash
# Full LeverApi logging
NS_LOG="LeverApi=level_all:LeverConfig=level_all" ./ns3 run example

# With WifiMac to see channel switching
NS_LOG="LeverApi=info:ApWifiMac=info:StaWifiMac=info" ./ns3 run example

# With WifiPhy for PHY-level details
NS_LOG="LeverApi=info:WifiPhy=info" ./ns3 run example
```

### Common Issues

**1. Configuration changes not applied**
- Check that LeverApi is running (Start time < change time < Stop time)
- Verify WiFi device exists on node
- Check trace connection is established

**2. STAs don't follow AP channel change**
- Verify using ApWifiMac (not AdhocWifiMac)
- Ensure STAs are associated before channel change
- Check spectrum channel is shared

**3. Invalid channel configuration**
- Use valid channel numbers for desired band/width
- Verify 2.4 GHz channels are 1-14
- Check 5 GHz channel width encoding

## Examples

The module includes several comprehensive examples:

1. **channel-switch-demo.cc**: Smart channel switching demonstration
   - Shows automatic band/width detection
   - Demonstrates AP-to-STA propagation
   - Tests various channel widths (20/40/80 MHz)

2. **lever-api-example.cc**: Basic dynamic configuration
   - Shows PHY parameter changes
   - Demonstrates channel hopping
   - Includes spectrum analyzer integration

3. **multi-ap-roaming-example.cc**: Multi-AP scenario
   - Multiple APs with independent configurations
   - STA roaming between APs
   - Dynamic power and channel control

4. **lever-api-comprehensive-test.cc**: Comprehensive testing
   - Tests all PHY parameters
   - Validates trace callbacks
   - Measures packet delivery under various configurations

## Future Enhancements

Potential improvements for future development:

1. **Channel Switch Announcement (CSA)**: Implement IEEE 802.11h CSA for graceful channel changes
2. **DFS Support**: Dynamic Frequency Selection for 5 GHz radar avoidance
3. **Multi-Link Operation (MLO)**: Support for WiFi 7 MLO configurations
4. **Power Save**: Integration with WiFi power save mechanisms
5. **6 GHz Support**: Add WiFi 6E 6 GHz band support
6. **Coordinated Spatial Reuse**: OBSS-PD threshold configuration for WiFi 6

## References

- **Source Changes**: See `docs/source-changes.md` for WiFi MAC layer modifications
- **Channel Switching**: Implementation in `src/wifi/model/wifi-mac.cc`, `ap-wifi-mac.cc`
- **IEEE 802.11 Standard**: Channel numbering and frequency assignments
- **ns-3 WifiPhy**: `src/wifi/model/wifi-phy.{h,cc}` for PHY operations
