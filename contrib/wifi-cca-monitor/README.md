# WiFi CCA Monitor Module for ns-3

A comprehensive module for monitoring WiFi Clear Channel Assessment (CCA) and channel utilization in ns-3 simulations.

## Features

- ✅ Automatic monitoring of all WiFi devices
- ✅ Tracks PHY states: IDLE, TX, RX, CCA_BUSY, SWITCHING
- ✅ Measures MAC layer throughput
- ✅ Per-node and per-BSS statistics
- ✅ Support for multiple BSSs on different channels
- ✅ Command-line configurable parameters
- ✅ Easy-to-use helper class

## Installation

The wifi-cca-monitor module is located in `contrib/wifi-cca-monitor/` with the following structure:

```
contrib/wifi-cca-monitor/
├── model/
│   ├── wifi-cca-monitor.h
│   └── wifi-cca-monitor.cc
├── helper/
│   ├── wifi-cca-monitor-helper.h
│   └── wifi-cca-monitor-helper.cc
├── examples/
│   ├── wifi-cca-monitor-example.cc
│   └── CMakeLists.txt
├── doc/
│   └── wifi-cca-monitor.rst
└── CMakeLists.txt
```

### Build the Module

```bash
# Build ns-3 (includes wifi-cca-monitor)
./ns3 build

# Run the example
./ns3 run wifi-cca-monitor-example
```

## Quick Start

### Basic Usage in Your Simulation

```cpp
#include "ns3/wifi-cca-monitor-helper.h"

// ... (setup your WiFi network) ...

// Install CCA monitor
WifiCcaMonitorHelper monitorHelper;
Ptr<WifiCcaMonitor> monitor = monitorHelper.Install();

// Run simulation
Simulator::Run();

// Print statistics
std::vector<uint8_t> channels = {36, 40, 44};  // Your channel list
monitor->PrintStatistics(nAps, nStaPerAp, channels);

Simulator::Destroy();
```

### Running the Example

#### Default Configuration
```bash
./ns3 run wifi-cca-monitor-example
```
- 3 APs, 30 stations per AP
- 5 Mbps per station
- ~70-80% channel utilization

#### Custom Configuration

**Low Utilization (~20-30%)**
```bash
./ns3 run "wifi-cca-monitor-example --dataRate=1000"
```

**Medium Utilization (~40-50%)**
```bash
./ns3 run "wifi-cca-monitor-example --dataRate=2500 --nStaPerAp=20"
```

**Different Number of APs**
```bash
./ns3 run "wifi-cca-monitor-example --nAps=5 --nStaPerAp=15 --dataRate=3000"
```

### Command-Line Parameters

| Parameter | Description | Default | Unit |
|-----------|-------------|---------|------|
| `--nAps` | Number of access points | 3 | - |
| `--nStaPerAp` | Stations per AP | 30 | - |
| `--dataRate` | Data rate per station | 5000 | kbps |
| `--packetSize` | UDP packet size | 1472 | bytes |

## Understanding the Output

### Per-Node Statistics
```
Node  0 | AP  | Idle: 25.3% | TX: 15.2% | RX: 45.8% | CCA: 13.7% | BUSY: 74.7% | TX: 45.2Mbps RX:135.6Mbps
```

- **Idle%**: Percentage of time PHY was idle
- **TX%**: Percentage of time PHY was transmitting
- **RX%**: Percentage of time PHY was receiving
- **CCA%**: Percentage of time channel was sensed busy (no TX/RX)
- **BUSY%**: Total channel utilization (TX + RX + CCA)

### Per-BSS Summary
```
BSS 0 (Channel 36):
  AP Busy: 74.7%
  AP TX Throughput: 45.2 Mbps
  AP RX Throughput: 135.6 Mbps
  Average STA Busy: 32.4%
```

## Controlling Channel Utilization

Channel utilization can be controlled by adjusting the `dataRate` parameter:

### Formula
```
Expected Utilization ≈ (2 × dataRate × nStaPerAp) / 200 Mbps
```

Where:
- `2×` accounts for uplink + downlink traffic
- `200 Mbps` is approximate usable throughput for WiFi 6 HeMcs5 on 20 MHz

### Target Utilization Guide

| Target | Command |
|--------|---------|
| 10% | `--dataRate=500 --nStaPerAp=20` |
| 20% | `--dataRate=1000 --nStaPerAp=20` |
| 30% | `--dataRate=1500 --nStaPerAp=20` |
| 40% | `--dataRate=2000 --nStaPerAp=20` |
| 50% | `--dataRate=2500 --nStaPerAp=20` |
| 70% | `--dataRate=3500 --nStaPerAp=20` |

## API Documentation

### WifiCcaMonitor Class

**Key Methods:**
- `InstallAll()`: Install monitoring on all WiFi devices
- `GetIdleTime(nodeId)`: Get idle time for a specific node
- `GetTxTime(nodeId)`: Get transmission time
- `GetRxTime(nodeId)`: Get reception time
- `GetCcaBusyTime(nodeId)`: Get CCA busy time
- `GetBytesSent(nodeId)`: Get bytes sent
- `GetBytesReceived(nodeId)`: Get bytes received
- `PrintStatistics(nAps, nStaPerAp, channels)`: Print formatted output
- `ResetStatistics()`: Clear all statistics

### WifiCcaMonitorHelper Class

**Key Methods:**
- `Install()`: Create and install a WifiCcaMonitor

## File Structure Explanation

### Model Files
- **wifi-cca-monitor.h/cc**: Core monitoring class
  - Tracks PHY states via trace callbacks
  - Stores per-node statistics
  - Provides getter methods and formatted output

### Helper Files
- **wifi-cca-monitor-helper.h/cc**: Simplifies installation
  - Creates WifiCcaMonitor objects
  - Connects to trace sources

### Example File
- **wifi-cca-monitor-example.cc**: Complete working example
  - Multi-BSS WiFi 6 setup
  - Bidirectional UDP traffic
  - Configurable parameters

## Technical Details

### Measured PHY States
1. **IDLE**: Channel is idle, ready to transmit
2. **TX**: PHY is transmitting a frame
3. **RX**: PHY is receiving a frame
4. **CCA_BUSY**: Channel sensed busy (carrier sense)
5. **SWITCHING**: PHY is switching channels (counted as busy)

### Measurement Period
- Traffic starts at t=2.0s
- Traffic stops at t=10.0s
- Measurement period: 8.0 seconds
- Statistics calculated over this period

### Why Use This Module?

1. **Accurate Measurements**: Tracks all PHY states, not just CCA_BUSY
2. **Easy Integration**: Single helper call to install
3. **Comprehensive Output**: Per-node and per-BSS statistics
4. **Flexible**: Works with any WiFi configuration
5. **Well-Tested**: Includes unit tests and example

## Troubleshooting

### Issue: No output or zero statistics
**Solution**: Ensure monitor is installed AFTER WiFi devices are created

### Issue: Build errors
**Solution**: Check that all required modules are linked in CMakeLists.txt

### Issue: Runtime errors about missing trace sources
**Solution**: Verify you're using a compatible ns-3 version (3.36+)

## Contributing

This module follows ns-3 coding standards:
- Use ns-3 naming conventions
- Include Doxygen documentation
- Add unit tests for new features
- Update documentation

## License

This module is provided under the same license as ns-3.

## Citation

If you use this module in your research, please cite:
```
[Your citation information here]
```

## Contact

For questions, issues, or contributions, please [contact information].
