# Waypoint Simulation Guide

## Overview

The waypoint simulation is a configurable WiFi network simulator where STAs (stations) move randomly between predefined waypoints while APs (access points) remain stationary with dynamically configurable PHY parameters via LeverAPI.

## What It Does

- **Mobile STAs**: Stations move between waypoints with randomized dwell times and velocities
- **Stationary APs**: Access points positioned at fixed locations with configurable WiFi parameters
- **Dynamic Configuration**: AP PHY parameters (TX power, channel, CCA threshold, OBSS PD) controlled via LeverAPI
- **UDP Traffic**: Simple UDP echo client/server applications for testing connectivity

## Running the Simulation

### Basic Usage

```bash
./ns3 run "waypoint-simulation"
```

### With Custom Config File

```bash
./ns3 run "waypoint-simulation --configFile=my-config.json"
```

### With Verbose Logging

```bash
./ns3 run "waypoint-simulation --verbose=true"
```

## Configuration File Structure

The simulation uses a JSON configuration file (default: `waypoint-sim-config.json`) with the following structure:

### Root Parameters

- `simulationTime` (double): Total simulation duration in seconds

### APs Configuration

Each AP in the `aps` array contains:

```json
{
  "nodeId": 0,
  "position": {"x": 10.0, "y": 25.0, "z": 3.0},
  "leverConfig": {
    "txPower": 20.0,           // TX power in dBm
    "channel": 36,             // WiFi channel number
    "ccaThreshold": -82.0,     // CCA-ED threshold in dBm
    "obsspdThreshold": -72.0   // RX sensitivity in dBm
  }
}
```

### Waypoints Configuration

Each waypoint in the `waypoints` array defines a position STAs can move to:

```json
{"id": 0, "x": 0.0, "y": 0.0, "z": 1.5}
```

### STAs Configuration

Each STA in the `stas` array contains:

```json
{
  "nodeId": 0,
  "initialWaypointId": 0,           // Starting waypoint
  "waypointSwitchTimeMin": 5.0,     // Min dwell time (seconds)
  "waypointSwitchTimeMax": 15.0,    // Max dwell time (seconds)
  "transferVelocityMin": 1.0,       // Min velocity (m/s)
  "transferVelocityMax": 2.5        // Max velocity (m/s)
}
```

## Key Parameters Explained

### STA Mobility Behavior

- **Initial Waypoint**: Where the STA starts at simulation begin
- **Dwell Time**: Random time between [min, max] that STA stays at a waypoint before moving
- **Transfer Velocity**: Random speed between [min, max] used when moving between waypoints
- **Random Selection**: STAs randomly select their next waypoint (different from current)

### AP LeverAPI Configuration

- **txPower**: Transmission power in dBm (typical: 15-20 dBm)
- **channel**: 5GHz channel number (e.g., 36, 40, 44, 48, etc.)
- **ccaThreshold**: Clear Channel Assessment threshold in dBm (typical: -82 dBm)
- **obsspdThreshold**: Used for RX sensitivity setting in dBm (typical: -72 dBm)

## Example Scenario

The default configuration creates:
- 3 APs positioned along y=25.0 at x=10, 25, 40 meters
- 25 waypoints in a 5x5 grid (50m x 50m area)
- 5 STAs moving randomly with varying mobility patterns
- 200 second simulation duration

## Network Details

- **WiFi Standard**: 802.11ac
- **Rate Control**: ConstantRateWifiManager (VhtMcs8 for data, VhtMcs0 for control)
- **SSID**: WaypointNetwork
- **Channel Width**: 20 MHz
- **Band**: 5 GHz
- **IP Addressing**: 10.1.1.0/24 subnet
- **Application**: UDP echo server on AP[0], echo clients on all STAs

## Logging Output

When run with `--verbose=true`, the simulation logs:
- Configuration parsing details
- Node creation and positioning
- STA waypoint movements (departure, arrival, dwell times)
- LeverAPI configuration applications

## File Location

- Source: `contrib/waypoint-simulation/examples/waypoint-simulation-example.cc`
- Config: `waypoint-sim-config.json` (default)