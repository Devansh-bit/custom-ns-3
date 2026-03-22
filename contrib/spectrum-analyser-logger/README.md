# Spectrum Analyser Logger Module

A modular, plug-and-play spectrum analyzer logging helper for ns-3 simulations.

## Overview

This module provides an easy-to-use API for adding spectrum analysis capabilities to any ns-3 simulation. It automatically creates spectrum analyzers on specified nodes (e.g., APs) and logs time-based Power Spectral Density (PSD) data across frequency bands.

## Features

- **Plug-and-Play**: Install spectrum analyzers on any node with just a few lines of code
- **Per-Node Logging**: Automatically creates separate log files for each node/AP
- **Configurable Parameters**:
  - Frequency range (start frequency, resolution, number of bins)
  - Noise floor
  - Logging interval
  - Output file locations
- **Multiple Output Formats**: Console output and/or file logging
- **Easy Integration**: Works with any existing simulation setup

## Quick Start

### Basic Usage

```cpp
#include "ns3/spectrum-analyser-logger-helper.h"

// Create helper
SpectrumAnalyserLoggerHelper spectrumLogger;

// Configure parameters
spectrumLogger.SetChannel (channel);
spectrumLogger.SetFrequencyRange (2.4e9, 100e3, 1000);  // 2.4 GHz, 100 kHz res, 1000 bins
spectrumLogger.SetNoiseFloor (-140.0);                   // -140 dBm/Hz
spectrumLogger.SetLoggingInterval (MilliSeconds(100));   // Log every 100ms

// Install on nodes - that's it!
spectrumLogger.InstallOnNodes (apNodes, "output-prefix");
```

### Installation Options

```cpp
// Install on all nodes in a container
spectrumLogger.InstallOnNodes (apNodes, "output");

// Install on a single node
spectrumLogger.InstallOnNode (apNodes.Get(0), "output");

// Enable console output
spectrumLogger.EnableConsoleOutput (true);
```

### Accessing Loggers

```cpp
// Get logger for specific node
Ptr<SpectrumAnalyserLogger> logger = spectrumLogger.GetLogger(nodeId);

// Get all loggers
auto loggers = spectrumLogger.GetAllLoggers();
```

## Output Format

Each node generates a separate trace file: `<prefix>-node<id>.tr`

Format: `Timestamp(s) Frequency(Hz) PSD(W/Hz)`

Example:
```
# Spectrum Analyzer Logger Data
# NodeId: 0
# Format: Timestamp(s) Frequency(Hz) PSD(W/Hz)
0.1 2400000000 1.5e-12
0.1 2400100000 1.6e-12
0.1 2400200000 1.4e-12
...
```

## Configuration Parameters

### Frequency Range
```cpp
SetFrequencyRange(startFreq, resolution, numBins)
```
- `startFreq`: Starting frequency in Hz (e.g., 2.4e9 for 2.4 GHz)
- `resolution`: Frequency resolution in Hz (e.g., 100e3 for 100 kHz)
- `numBins`: Number of frequency bins (e.g., 1000 for 100 MHz span)

### Noise Floor
```cpp
SetNoiseFloor(noisePowerDbm)
```
- Thermal noise power in dBm/Hz (default: -140.0)

### Logging Interval
```cpp
SetLoggingInterval(interval)
```
- Time interval between consecutive logs (default: 100 ms)

### Analyzer Resolution
```cpp
SetAnalyzerResolution(resolution)
```
- Spectrum analyzer sampling interval (default: 1 ms)

## Example

See `examples/spectrum-analyser-logger-example.cc` for a complete working example.

### Building and Running

```bash
# Build
./ns3 build

# Run example
./ns3 run spectrum-analyser-logger-example

# With parameters
./ns3 run "spectrum-analyser-logger-example --numAPs=5 --simTime=3.0"
```

## Integration with Existing Simulations

To add spectrum analysis to an existing simulation:

1. Include the header:
```cpp
#include "ns3/spectrum-analyser-logger-helper.h"
```

2. Create and configure the helper (after creating the spectrum channel):
```cpp
SpectrumAnalyserLoggerHelper spectrumLogger;
spectrumLogger.SetChannel (channel);
spectrumLogger.SetFrequencyRange (2.4e9, 100e3, 1000);
spectrumLogger.SetNoiseFloor (-140.0);
spectrumLogger.SetLoggingInterval (MilliSeconds(100));
```

3. Install on your APs/nodes:
```cpp
spectrumLogger.InstallOnNodes (apNodes, "spectrum-output");
```

That's it! The spectrum data will be automatically logged during simulation.

## Module Structure

```
spectrum-analyser-logger/
├── model/
│   ├── spectrum-analyser-logger.h        # Core logger class
│   └── spectrum-analyser-logger.cc
├── helper/
│   ├── spectrum-analyser-logger-helper.h # Plug-and-play helper API
│   └── spectrum-analyser-logger-helper.cc
├── examples/
│   └── spectrum-analyser-logger-example.cc
└── CMakeLists.txt
```

## Dependencies

- ns3::core
- ns3::spectrum
- ns3::network
- ns3::mobility

## License

This module is licensed under the GNU General Public License version 2.
