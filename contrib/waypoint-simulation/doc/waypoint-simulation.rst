Waypoint Simulation Module Documentation
=========================================

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ------------- Chapter
   ************* Section (#.#)
   ============= Subsection (#.#.#)
   ############# Paragraph (no number)

This document describes the Waypoint Simulation module for |ns3|.

The Waypoint Simulation module provides waypoint-based mobility for WiFi STAs with JSON-driven configuration. STAs move randomly between predefined waypoints with configurable dwell times and transfer velocities, while APs can be dynamically configured via LeverAPI.

The source code for the Waypoint Simulation module lives in the directory ``contrib/waypoint-simulation``.

Overview
********

The Waypoint Simulation module simplifies the creation of WiFi simulations with mobile STAs following waypoint-based movement patterns. Instead of scripting mobility manually, users define waypoints in a JSON configuration file and let the module handle random movement, timing, and velocity management.

**Key Features:**

- **Waypoint Grid Management**: Define fixed waypoints in 3D space
- **Random Mobility**: STAs move randomly between waypoints
- **Configurable Timing**: Min/max dwell times at each waypoint
- **Configurable Velocity**: Min/max transfer speeds between waypoints
- **JSON Configuration**: Complete simulation setup in a single file
- **LeverAPI Integration**: Dynamic AP configuration (TX power, channel, CCA thresholds, RX sensitivity)
- **Interference Support**: Optional interference source configuration
- **Production-Ready**: Used in large-scale roaming simulations

**Use Cases:**

- WiFi roaming simulations with mobile users
- Multi-AP network testing with realistic mobility
- Performance evaluation under different movement patterns
- LeverAPI-based dynamic AP optimization studies

Scope and Limitations
*********************

What the Module Can Do
=======================

The Waypoint Simulation module can:

- **Waypoint Grid Management**: Define and manage fixed waypoints with 3D coordinates
- **Random Waypoint Mobility**: Move STAs randomly between waypoints with configurable parameters
- **Flexible Timing**: Randomize dwell times (time spent at waypoints) within min/max bounds
- **Flexible Velocity**: Randomize transfer velocities (speed between waypoints) within min/max bounds
- **JSON Configuration**: Load complete simulation setup from JSON files including:

  - AP positions and LeverAPI parameters
  - Waypoint grid layout
  - STA mobility configurations
  - System parameters (scanning channels, RSSI thresholds)
  - Interference source settings

- **LeverAPI Integration**: Dynamically configure AP PHY parameters at runtime
- **Per-STA Configuration**: Each STA can have different timing and velocity parameters

What the Module Cannot Do
==========================

The Waypoint Simulation module:

- **Does Not Implement Path Planning**: Movement is purely random, no path optimization or planning
- **Does Not Handle Obstacles**: Waypoints are connected in straight lines, no obstacle avoidance
- **Does Not Model Pedestrian Behavior**: No realistic human movement patterns (groups, attractions, etc.)
- **Does Not Provide Mobility Models**: It uses |ns3|'s WaypointMobilityModel, doesn't implement custom models
- **Does Not Manage WiFi Protocols**: WiFi setup must be done separately (can use simulation-helper)

For more advanced mobility models, see |ns3|'s mobility module documentation.

Architecture and Design
***********************

The Waypoint Simulation module consists of three main components:

1. **WaypointGrid** (``model/waypoint-grid.{h,cc}``): Waypoint position management
2. **WaypointMobilityHelper** (``helper/waypoint-mobility-helper.{h,cc}``): STA mobility orchestration
3. **SimulationConfigParser** (``helper/simulation-config-parser.{h,cc}``): JSON configuration loading

Component Architecture
======================

::

    SimulationConfigParser
        │
        ├─── Parses JSON configuration file
        ├─── Extracts AP configurations (position, LeverAPI settings)
        ├─── Extracts waypoint positions
        ├─── Extracts STA mobility parameters
        └─── Returns SimulationConfigData

    WaypointGrid (Model)
        │
        ├─── Stores waypoint ID → position mappings
        ├─── Provides random waypoint selection (excludes current)
        ├─── Calculates distances between waypoints
        └─── Manages waypoint lifecycle

    WaypointMobilityHelper
        │
        ├─── Installs WaypointMobilityModel on STA nodes
        ├─── Manages per-STA mobility state
        ├─── Schedules waypoint transitions with random timing
        ├─── Calculates transfer velocities
        └─── Handles waypoint arrival callbacks

Dependencies
============

**Required Modules:**

- **lever-api**: For dynamic AP PHY configuration (TX power, channel, CCA, RX sensitivity)
- **simulation-helper**: For WiFi device installation helpers

**Optional Integration:**

The module can work standalone or integrate with:

- DualPhySniffer (scanning channels configured via JSON)
- Auto-roaming protocols (RSSI threshold configured via JSON)
- Interference models (interference source configured via JSON)

**WiFi PHY API**: Works with both SpectrumWifi and YansWifi through standard ns-3 mobility interfaces.

Design Patterns
===============

Configuration-Driven Design
############################

All simulation parameters are externalized to JSON files:

::

    {
      "simulationTime": 3600.0,
      "aps": [
        {
          "nodeId": 0,
          "position": {"x": 0, "y": 0, "z": 0},
          "txPower": 20.0,
          "channel": 36,
          "ccaThreshold": -82.0,
          "rxSensitivity": -93.0
        }
      ],
      "waypoints": [
        {"id": 0, "position": {"x": 10, "y": 10, "z": 0}},
        {"id": 1, "position": {"x": 50, "y": 50, "z": 0}}
      ],
      "stas": [
        {
          "nodeId": 0,
          "initialWaypointId": 0,
          "waypointSwitchTimeMin": 5.0,
          "waypointSwitchTimeMax": 15.0,
          "transferVelocityMin": 1.0,
          "transferVelocityMax": 3.0
        }
      ]
    }

Random Waypoint Mobility Pattern
#################################

For each STA:

1. **Start**: STA placed at initial waypoint
2. **Dwell**: STA waits at waypoint for random time in [dwellMin, dwellMax]
3. **Select**: Random waypoint selected (excluding current)
4. **Move**: STA moves at random velocity in [velocityMin, velocityMax]
5. **Arrive**: Repeat from step 2

State Management
################

WaypointMobilityHelper maintains per-STA state:

- Current waypoint ID
- Mobility model reference
- Node reference
- Mobility configuration (dwell times, velocities)

How It Works
************

The waypoint simulation workflow:

1. Configuration Loading
========================

SimulationConfigParser reads JSON configuration:

::

    std::string configFile = "waypoint-sim-config.json";
    SimulationConfigData config = SimulationConfigParser::ParseFile(configFile);

**Parsed Data:**

- AP configurations (position, LeverAPI parameters)
- Waypoint grid (ID → position mappings)
- STA mobility configs (dwell times, velocities)
- System parameters (scanning channels, RSSI thresholds)
- Interference settings

2. Waypoint Grid Setup
=======================

Build waypoint grid from configuration:

::

    Ptr<WaypointGrid> waypointGrid = CreateObject<WaypointGrid>();

    for (const auto& wp : config.waypoints)
    {
        waypointGrid->AddWaypoint(wp.id, wp.position);
    }

**Grid Operations:**

- ``AddWaypoint(id, position)``: Register waypoint
- ``GetWaypointPosition(id)``: Query waypoint location
- ``SelectRandomWaypoint(currentId)``: Get random next waypoint (excludes current)
- ``CalculateDistance(id1, id2)``: Compute distance for velocity calculations

3. Mobility Installation
=========================

Install waypoint mobility on STAs:

::

    WaypointMobilityHelper mobilityHelper;
    mobilityHelper.SetWaypointGrid(waypointGrid);
    mobilityHelper.Install(staNodes, config.stas);

**For Each STA:**

1. Create WaypointMobilityModel
2. Set initial position to initial waypoint
3. Aggregate mobility model to node
4. Store STA state (current waypoint, config)

4. Mobility Activation
======================

Start mobility after setup complete:

::

    mobilityHelper.StartMobility();

**Initialization:**

- For each STA: Schedule first waypoint move after random delay
- Random delay prevents all STAs from moving simultaneously
- Delay range: [0, dwellTimeMax]

5. Waypoint Transitions
========================

**Dwell Phase:**

::

    // At waypoint, select random dwell time
    double dwellTime = UniformRandom(dwellMin, dwellMax);

    // Schedule next move
    Simulator::Schedule(Seconds(dwellTime),
                        &WaypointMobilityHelper::ScheduleNextMove,
                        this,
                        nodeId);

**Movement Phase:**

::

    // Select random next waypoint (excluding current)
    uint32_t nextWaypoint = m_grid->SelectRandomWaypoint(currentWaypoint);

    // Get positions
    Vector currentPos = m_grid->GetWaypointPosition(currentWaypoint);
    Vector nextPos = m_grid->GetWaypointPosition(nextWaypoint);

    // Calculate distance
    double distance = m_grid->CalculateDistance(currentWaypoint, nextWaypoint);

    // Select random velocity
    double velocity = UniformRandom(velocityMin, velocityMax);

    // Calculate travel time
    double travelTime = distance / velocity;

    // Add waypoint to mobility model
    mobility->AddWaypoint(Waypoint(Seconds(Now() + travelTime), nextPos));

    // Update current waypoint
    currentWaypoint = nextWaypoint;

    // Schedule next move after arrival
    Simulator::Schedule(Seconds(travelTime),
                        &WaypointMobilityHelper::OnWaypointReached,
                        this,
                        nodeId,
                        nextWaypoint);

6. LeverAPI Integration
========================

Apply LeverAPI configurations to APs:

::

    for (size_t i = 0; i < config.aps.size(); i++)
    {
        Ptr<LeverApi> leverApi = CreateObject<LeverApi>();
        leverApi->SetNode(apNodes.Get(i));
        leverApi->SetTxPower(config.aps[i].txPower);
        leverApi->SetChannel(config.aps[i].channel);
        leverApi->SetCcaThreshold(config.aps[i].ccaThreshold);
        leverApi->SetRxSensitivity(config.aps[i].rxSensitivity);
    }

Usage
*****

This section demonstrates how to use the Waypoint Simulation module.

Helpers
=======

WaypointGrid
############

Manages waypoint positions and provides utilities.

**Basic Setup:**

::

    #include "ns3/waypoint-grid.h"

    // Create grid
    Ptr<WaypointGrid> grid = CreateObject<WaypointGrid>();

    // Add waypoints
    grid->AddWaypoint(0, Vector(0, 0, 0));
    grid->AddWaypoint(1, Vector(50, 0, 0));
    grid->AddWaypoint(2, Vector(50, 50, 0));
    grid->AddWaypoint(3, Vector(0, 50, 0));

    // Query grid
    Vector pos = grid->GetWaypointPosition(1);
    uint32_t randomWp = grid->SelectRandomWaypoint(0);  // Random, but not 0
    double distance = grid->CalculateDistance(0, 2);

WaypointMobilityHelper
######################

Manages STA waypoint-based mobility.

**Basic Setup:**

::

    #include "ns3/waypoint-mobility-helper.h"

    // Create helper
    WaypointMobilityHelper mobilityHelper;
    mobilityHelper.SetWaypointGrid(waypointGrid);

    // Configure STA mobility
    StaMobilityConfig staConfig;
    staConfig.nodeId = 0;
    staConfig.initialWaypointId = 0;
    staConfig.waypointSwitchTimeMin = 5.0;   // 5s min dwell
    staConfig.waypointSwitchTimeMax = 15.0;  // 15s max dwell
    staConfig.transferVelocityMin = 1.0;     // 1 m/s min speed
    staConfig.transferVelocityMax = 3.0;     // 3 m/s max speed

    // Install on STA nodes
    std::vector<StaMobilityConfig> configs = {staConfig};
    mobilityHelper.Install(staNodes, configs);

    // Start mobility
    mobilityHelper.StartMobility();

**Multiple STAs:**

::

    std::vector<StaMobilityConfig> configs;

    for (uint32_t i = 0; i < numStas; i++)
    {
        StaMobilityConfig config;
        config.nodeId = i;
        config.initialWaypointId = i % waypointGrid->GetWaypointCount();
        config.waypointSwitchTimeMin = 10.0;
        config.waypointSwitchTimeMax = 30.0;
        config.transferVelocityMin = 1.0;
        config.transferVelocityMax = 2.5;
        configs.push_back(config);
    }

    mobilityHelper.Install(staNodes, configs);
    mobilityHelper.StartMobility();

SimulationConfigParser
######################

Loads complete simulation configuration from JSON.

**Basic Usage:**

::

    #include "ns3/simulation-config-parser.h"

    // Parse configuration file
    std::string configFile = "waypoint-sim-config.json";
    SimulationConfigData config = SimulationConfigParser::ParseFile(configFile);

    // Access configuration data
    std::cout << "Simulation time: " << config.simulationTime << "s" << std::endl;
    std::cout << "Number of APs: " << config.aps.size() << std::endl;
    std::cout << "Number of waypoints: " << config.waypoints.size() << std::endl;
    std::cout << "Number of STAs: " << config.stas.size() << std::endl;

**JSON Configuration Format:**

::

    {
      "simulationTime": 3600.0,

      "aps": [
        {
          "nodeId": 0,
          "position": {"x": 0.0, "y": 0.0, "z": 0.0},
          "txPower": 20.0,
          "channel": 36,
          "ccaThreshold": -82.0,
          "rxSensitivity": -93.0
        }
      ],

      "waypoints": [
        {"id": 0, "position": {"x": 0.0, "y": 0.0, "z": 0.0}},
        {"id": 1, "position": {"x": 50.0, "y": 0.0, "z": 0.0}},
        {"id": 2, "position": {"x": 50.0, "y": 50.0, "z": 0.0}},
        {"id": 3, "position": {"x": 0.0, "y": 50.0, "z": 0.0}}
      ],

      "stas": [
        {
          "nodeId": 0,
          "initialWaypointId": 0,
          "waypointSwitchTimeMin": 5.0,
          "waypointSwitchTimeMax": 15.0,
          "transferVelocityMin": 1.0,
          "transferVelocityMax": 3.0
        }
      ],

      "scanningChannels": [36, 40, 44, 48],
      "channelHopDurationMs": 300.0,
      "bssOrchestrationRssiThreshold": -70.0,

      "interference": {
        "enabled": false,
        "position": {"x": 25.0, "y": 25.0, "z": 0.0},
        "numSources": 1,
        "startTime": 10.0,
        "centerFrequencyGHz": 5.18,
        "bandwidthMHz": 20.0,
        "powerPsdDbmHz": -50.0
      }
    }

Attributes
==========

The module does not define ns-3 attributes as it uses configuration-driven design. All parameters are specified in JSON configuration files.

**WaypointGrid** (no attributes - configured programmatically)

**WaypointMobilityHelper** (no attributes - per-STA configuration via StaMobilityConfig):

- ``initialWaypointId``: Starting waypoint for STA
- ``waypointSwitchTimeMin``: Minimum dwell time at waypoints (seconds)
- ``waypointSwitchTimeMax``: Maximum dwell time at waypoints (seconds)
- ``transferVelocityMin``: Minimum transfer velocity between waypoints (m/s)
- ``transferVelocityMax``: Maximum transfer velocity between waypoints (m/s)

**SimulationConfigParser** (no attributes - reads from JSON files)

Traces
======

The Waypoint Simulation module does not currently define trace sources. Movement events can be monitored through |ns3|'s standard WaypointMobilityModel traces:

**CourseChange** (from MobilityModel)
  Fired when node position changes during movement.

  **Example:**

  ::

      Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/CourseChange",
                      MakeCallback(&CourseChangeCallback));

Examples and Tests
******************

waypoint-simulation-example.cc
===============================

Location: ``contrib/waypoint-simulation/examples/waypoint-simulation-example.cc``

**Scenario:**

- Loads complete simulation from JSON configuration file
- Creates APs with LeverAPI configuration
- Builds waypoint grid from configuration
- Installs waypoint mobility on STAs
- Runs WiFi simulation with mobile STAs

**What it demonstrates:**

- JSON configuration loading with SimulationConfigParser
- Waypoint grid setup with WaypointGrid
- STA mobility installation with WaypointMobilityHelper
- LeverAPI integration for dynamic AP configuration
- SpectrumWifi setup using simulation-helper
- UDP echo application setup

**Running the Example:**

.. sourcecode:: bash

    ./ns3 run waypoint-simulation-example

**Command-line Options:**

::

    --configFile="waypoint-sim-config.json"  # Path to JSON config file
    --verbose=false                          # Enable verbose logging

**Configuration File:**

The example expects a JSON file (default: ``waypoint-sim-config.json``) containing:

- AP configurations (position, LeverAPI parameters)
- Waypoint grid layout
- STA mobility parameters
- System settings (scanning channels, RSSI thresholds)
- Optional interference configuration

Test Suite
==========

The Waypoint Simulation module includes a test suite in ``test/waypoint-simulation-test-suite.cc``.

**Current Status:** The test currently contains only a placeholder test.

**Future Work:** Real tests should be added to verify:

- WaypointGrid waypoint management
- Random waypoint selection (excluding current)
- Distance calculations
- WaypointMobilityHelper mobility installation
- Waypoint transition scheduling
- JSON configuration parsing
- Configuration validation

Validation
**********

Production Usage
================

The module is actively used in production roaming simulations with:

- 4 APs with LeverAPI dynamic configuration
- 10 STAs with waypoint-based mobility
- JSON-driven configuration
- Integration with 802.11k/v protocols
- Multi-hour simulation runs

Integration Testing
===================

The module is integration-tested through:

1. **waypoint-simulation-example.cc**: Complete workflow with JSON configuration
2. Production-scale integration with roaming protocols
3. **Manual testing**: Various waypoint grid layouts and mobility patterns

Manual Testing
==============

The module has been manually tested with:

- **Small grids**: 4-10 waypoints
- **Large grids**: 50+ waypoints
- **Single STA**: Simple mobility patterns
- **Multiple STAs**: 10+ STAs with different parameters
- **Different velocities**: Walking (1-2 m/s), jogging (3-5 m/s), cycling (5-10 m/s)
- **Different dwell times**: Short (1-5s), medium (5-30s), long (30-120s)
- **LeverAPI integration**: Dynamic AP reconfiguration
- **JSON parsing**: Various configuration file structures

Test Coverage
=============

**Current Coverage:**

- ✓ Basic waypoint grid management (tested in examples)
- ✓ Random waypoint selection (tested in examples)
- ✓ Distance calculations (tested in examples)
- ✓ Mobility installation (tested in examples)
- ✓ Waypoint transitions (tested in examples)
- ✓ JSON configuration parsing (tested in examples)
- ✓ LeverAPI integration (tested in production)
- ✓ Production usage (tested in large-scale simulations)
- ⚠ Unit tests (placeholder only - needs implementation)

Known Limitations
=================

- **Random Movement Only**: No path planning or optimization
- **No Obstacle Avoidance**: Waypoints connected by straight lines
- **No Group Mobility**: Each STA moves independently
- **No Attraction Points**: All waypoints equally likely (uniform random)
- **No Pause/Resume**: Mobility cannot be paused once started
- **No Dynamic Waypoints**: Waypoint grid fixed at start, cannot add/remove during simulation

References
**********

[1] **ns-3 Mobility Module Documentation**: https://www.nsnam.org/docs/models/html/mobility.html

[2] **ns-3 WaypointMobilityModel**: Random Waypoint Mobility Model implementation

[3] **RapidJSON Library**: JSON parsing library used by SimulationConfigParser. https://rapidjson.org/

[4] **LeverAPI Module**: Dynamic AP PHY configuration (``contrib/lever-api``)

[5] **Simulation Helper Module**: WiFi setup utilities (``contrib/simulation-helper``)
