# Simulation Helper Module

The Simulation Helper module provides high-level utility functions for building complex WiFi roaming simulations with IEEE 802.11k/v protocols.

## Features

- **Modular Protocol Setup**: One-function setup for Neighbor Protocol, Beacon Protocol, BSS TM, and Auto-Roaming
- **DualPhySniffer Integration**: Simplified multi-channel scanning setup for APs and STAs
- **WiFi Device Installation**: Batch installation helpers for APs and STAs with per-device channel configuration
- **Protocol Orchestration**: Automatic wiring of protocol dependencies (Link Measurement → Neighbor → Beacon → BSS TM)
- **Metrics & Tracing**: Helper functions for batch metrics initialization and trace connections
- **Multi-AP, Multi-STA Support**: Handles complex topologies with minimal code

## Module Components

### SimulationHelper Class

Static utility class providing factory methods for:

- **`SetupAPDualPhySniffer()`** - DualPhySniffer for APs (used by Neighbor Protocol for neighbor discovery)
- **`SetupSTADualPhySniffer()`** - DualPhySniffer for STAs (used by Beacon Protocol for beacon scanning)
- **`SetupNeighborProtocol()`** - Neighbor Protocol for APs and STAs (802.11k neighbor reports)
- **`SetupBeaconProtocol()`** - Beacon Protocol for APs and STAs (802.11k beacon reports)
- **`SetupBssTmHelper()`** - BSS Transition Management for APs and STAs (802.11v)
- **`SetupAutoRoamingKvHelperMulti()`** - Complete roaming orchestration for multiple STAs
- **`InstallApDevices()`** - Batch AP device installation with per-AP channel configuration
- **`InstallStaDevices()`** - Batch STA device installation with per-STA channel configuration
- **`SplitProtocolVector()`** - Utility to split combined AP+STA protocol vectors

### MetricsHelper Class

Utilities for metrics management:

- **`InitializeApMetrics()`** - Initialize metrics for AP devices
- **`InitializeStaMetrics()`** - Initialize metrics for STA devices
- **`FindMetricByPredicate()`** - Search metrics using custom predicates
- **`UpdateMetric()`** - Update existing metric values
- **`BuildKafkaIntegration()`** - Wire metrics to Kafka producer
- **`BuildLeverApiIntegration()`** - Wire metrics to LeverAPI

### TraceHelper Class

Batch trace connection utilities:

- **`ConnectLinkMeasurementTraces()`** - Connect link measurement traces for multiple protocols
- **`ConnectAssociationTraces()`** - Connect WiFi association/disassociation traces

## Quick Start

### Basic Setup Example

```cpp
#include "ns3/simulation-helper.h"
#include "ns3/wifi-module.h"
#include "ns3/spectrum-module.h"

using namespace ns3;

// Create nodes
NodeContainer apNodes;
apNodes.Create(4);

NodeContainer staNodes;
staNodes.Create(10);

// Create shared spectrum channel
Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
// ... configure propagation models ...

// Install WiFi devices with SimulationHelper
SpectrumWifiPhyHelper phyHelper;
phyHelper.SetChannel(spectrumChannel);

WifiHelper wifi;
wifi.SetStandard(WIFI_STANDARD_80211ax);

WifiMacHelper mac;
Ssid ssid = Ssid("roaming-network");

// Install APs on different channels
std::vector<uint8_t> apChannels = {36, 40, 44, 48};
NetDeviceContainer apDevices = SimulationHelper::InstallApDevices(
    wifi, phyHelper, mac, apNodes, apChannels, ssid);

// Install STAs (initially on channel 36)
std::vector<uint8_t> staChannels(10, 36);
NetDeviceContainer staDevices = SimulationHelper::InstallStaDevices(
    wifi, phyHelper, mac, staNodes, staChannels, ssid);

// Get MAC addresses
std::vector<Mac48Address> apMacs;
for (uint32_t i = 0; i < apDevices.GetN(); i++) {
    Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice>(apDevices.Get(i));
    apMacs.push_back(dev->GetMac()->GetAddress());
}

// Setup DualPhySniffers
std::vector<uint8_t> scanChannels = {36, 40, 44, 48};
DualPhySnifferHelper* apSniffer = SimulationHelper::SetupAPDualPhySniffer(
    apDevices, apMacs, spectrumChannel, apChannels, scanChannels, Seconds(0.3));

std::vector<Mac48Address> staMacs;
for (uint32_t i = 0; i < staDevices.GetN(); i++) {
    Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice>(staDevices.Get(i));
    staMacs.push_back(dev->GetMac()->GetAddress());
}

DualPhySnifferHelper* staSniffer = SimulationHelper::SetupSTADualPhySniffer(
    staDevices, staMacs, spectrumChannel, staChannels, scanChannels, Seconds(0.3));

// Setup protocols
std::vector<ApInfo> neighborTable;
// ... populate neighbor table ...

auto neighborProtocols = SimulationHelper::SetupNeighborProtocol(
    apDevices, staDevices, apMacs, neighborTable, apSniffer);

auto beaconProtocols = SimulationHelper::SetupBeaconProtocol(
    apDevices, staDevices, staSniffer);

auto bssTmHelpers = SimulationHelper::SetupBssTmHelper(
    apDevices, staDevices);

// Split protocols into AP and STA instances
auto [neighborAps, neighborStas] = SimulationHelper::SplitProtocolVector(
    neighborProtocols, apDevices.GetN());

auto [beaconAps, beaconStas] = SimulationHelper::SplitProtocolVector(
    beaconProtocols, apDevices.GetN());

auto [bssTmAps, bssTmStas] = SimulationHelper::SplitProtocolVector(
    bssTmHelpers, apDevices.GetN());

// Setup auto-roaming (orchestrates the full protocol chain)
auto roamingContainer = SimulationHelper::SetupAutoRoamingKvHelperMulti(
    apDevices, staDevices,
    neighborStas, beaconStas, beaconAps, bssTmStas,
    Seconds(1.0),  // measurement interval
    -65.0);        // RSSI threshold

// Run simulation
Simulator::Stop(Seconds(60.0));
Simulator::Run();
Simulator::Destroy();
```

## Protocol Architecture

The SimulationHelper sets up a complete roaming protocol stack:

```
AutoRoamingKvHelper (Roaming Orchestration)
    │
    ├─── Link Measurement Protocol
    │      └─── Monitors RSSI/RCPI/RSNI
    │
    ├─── Neighbor Protocol (802.11k)
    │      ├─── APs: DualPhySniffer for neighbor discovery
    │      └─── STAs: Request neighbor reports from AP
    │
    ├─── Beacon Protocol (802.11k)
    │      ├─── APs: Request beacon reports from STAs
    │      └─── STAs: DualPhySniffer for beacon scanning
    │
    └─── BSS TM (802.11v)
           ├─── APs: Send transition requests
           └─── STAs: Process requests and roam
```

## Two-Instance DualPhySniffer Architecture

The module uses a **two-instance** DualPhySniffer architecture for optimal separation of concerns:

1. **AP Sniffer** (`apDualPhySniffer`):
   - Installed on all APs
   - Used by **Neighbor Protocol** on APs
   - Scans neighboring channels to discover other APs
   - Builds neighbor tables automatically

2. **STA Sniffer** (`staDualPhySniffer`):
   - Installed on all STAs
   - Used by **Beacon Protocol** on STAs
   - Scans neighboring channels to discover APs
   - Generates beacon reports for roaming decisions

This separation allows:
- APs to discover neighbors independently of beacon requests
- STAs to scan for roaming candidates independently of neighbor requests
- Each protocol to operate on its own dedicated scanning radio

## Usage Patterns

### Pattern 1: Basic Neighbor Discovery (APs Only)

```cpp
// Setup DualPhySniffer for APs
DualPhySnifferHelper* apSniffer = SimulationHelper::SetupAPDualPhySniffer(
    apDevices, apMacs, channel, apChannels, scanChannels, Seconds(0.3));

// Setup Neighbor Protocol (APs will use apSniffer, STAs will be passive)
auto neighborProtocols = SimulationHelper::SetupNeighborProtocol(
    apDevices, staDevices, apMacs, neighborTable, apSniffer);
```

### Pattern 2: STA Beacon Scanning (STAs Only)

```cpp
// Setup DualPhySniffer for STAs
DualPhySnifferHelper* staSniffer = SimulationHelper::SetupSTADualPhySniffer(
    staDevices, staMacs, channel, staChannels, scanChannels, Seconds(0.3));

// Setup Beacon Protocol (STAs will use staSniffer, APs will be passive)
auto beaconProtocols = SimulationHelper::SetupBeaconProtocol(
    apDevices, staDevices, staSniffer);
```

### Pattern 3: Complete Roaming Chain (Recommended)

```cpp
// Setup both sniffers
DualPhySnifferHelper* apSniffer = SimulationHelper::SetupAPDualPhySniffer(...);
DualPhySnifferHelper* staSniffer = SimulationHelper::SetupSTADualPhySniffer(...);

// Setup all protocols
auto neighborProtocols = SimulationHelper::SetupNeighborProtocol(..., apSniffer);
auto beaconProtocols = SimulationHelper::SetupBeaconProtocol(..., staSniffer);
auto bssTmHelpers = SimulationHelper::SetupBssTmHelper(...);

// Split into AP/STA instances
auto [neighborAps, neighborStas] = SimulationHelper::SplitProtocolVector(...);
auto [beaconAps, beaconStas] = SimulationHelper::SplitProtocolVector(...);
auto [bssTmAps, bssTmStas] = SimulationHelper::SplitProtocolVector(...);

// Setup auto-roaming orchestration
auto roamingContainer = SimulationHelper::SetupAutoRoamingKvHelperMulti(
    apDevices, staDevices,
    neighborStas, beaconStas, beaconAps, bssTmStas,
    Seconds(1.0), -65.0);
```

## Helper Classes

### SplitProtocolVector Template

Many setup functions return a combined vector of protocols with APs first, then STAs. Use `SplitProtocolVector()` to separate them:

```cpp
// Combined vector: [AP0, AP1, AP2, STA0, STA1, STA2]
std::vector<Ptr<NeighborProtocolHelper>> allProtocols =
    SimulationHelper::SetupNeighborProtocol(...);

// Split at index 3 (number of APs)
auto [apProtos, staProtos] = SimulationHelper::SplitProtocolVector(allProtocols, 3);
// apProtos = [AP0, AP1, AP2]
// staProtos = [STA0, STA1, STA2]
```

### GetWifiNetDevices

Convert `NetDeviceContainer` to `std::vector<Ptr<WifiNetDevice>>`:

```cpp
std::vector<Ptr<WifiNetDevice>> wifiDevices =
    SimulationHelper::GetWifiNetDevices(devices);
```

## Examples

### simulation-helper-example.cc

Complete example demonstrating:
- Multi-AP, multi-STA topology setup
- DualPhySniffer configuration for both APs and STAs
- Complete protocol chain setup (Neighbor, Beacon, BSS TM, Auto-Roaming)
- Roaming scenario with RSSI thresholds
- Trace connection and statistics collection

Run with:
```bash
./ns3 run simulation-helper-example
```

## Dependencies

The module depends on:
- `core`
- `network`
- `wifi`
- `spectrum`
- `mobility`
- `dual-phy-sniffer`
- `beacon-neighbor-protocol-11k`
- `bss_tm_11v`
- `auto-roaming-kv`
- `link-protocol-11k`
- `kafka-producer`
- `lever-api`

## API Reference

For complete API documentation, see `doc/simulation-helper.rst` or build the Sphinx documentation:

```bash
cd doc
make html
# Open build/html/index.html
```

## Integration with Other Modules

This module is designed to work with:

- **dual-phy-sniffer**: Multi-channel beacon monitoring
- **beacon-neighbor-protocol-11k**: 802.11k Neighbor Report and Beacon Report protocols
- **bss_tm_11v**: 802.11v BSS Transition Management
- **auto-roaming-kv**: Roaming orchestration and decision logic
- **link-protocol-11k**: 802.11k Link Measurement protocol
- **kafka-producer**: Metrics export to Kafka
- **lever-api**: Dynamic configuration via LeverAPI

## Production Usage

This module is actively used in:
- `contrib/final-simulation/examples/config-simulation.cc` - Large-scale roaming simulation with Kafka integration

## License

This module is part of ns-3 and follows the ns-3 license.
