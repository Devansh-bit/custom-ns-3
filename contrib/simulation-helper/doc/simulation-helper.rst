Simulation Helper Module Documentation
======================================

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ------------- Chapter
   ************* Section (#.#)
   ============= Subsection (#.#.#)
   ############# Paragraph (no number)

This document describes the Simulation Helper module for |ns3|.

The Simulation Helper module provides high-level utility functions for building complex WiFi roaming simulations with IEEE 802.11k/v protocols. It simplifies the setup of multi-AP, multi-STA networks with automatic protocol wiring and integration.

The source code for the Simulation Helper module lives in the directory ``contrib/simulation-helper``.

Overview
********

The Simulation Helper module is a utility module that provides factory-style methods for creating and wiring together complex protocol stacks for WiFi roaming simulations. Instead of manually creating and connecting protocols across multiple files and hundreds of lines of code, users can set up complete roaming scenarios with just a few function calls.

**Key Features:**

- One-function setup for common WiFi RRM protocols (Neighbor, Beacon, BSS TM)
- Modular design: use only the features you need
- Automatic protocol wiring and integration
- Support for multiple APs and STAs with minimal code
- Handles complex protocol dependencies automatically
- Includes helpers for metrics management and trace connections

Scope and Limitations
*********************

What the Module Can Do
=======================

The Simulation Helper module can:

- **Setup Protocol Stacks**: Create and wire Neighbor Protocol, Beacon Protocol, BSS TM, and Auto-Roaming helpers in a single function call
- **Manage DualPhySniffer**: Configure dual-PHY beacon monitoring with channel hopping for both APs and STAs
- **Install WiFi Devices**: Batch installation of AP and STA devices with per-device channel configuration
- **Split Protocol Vectors**: Utility functions to separate combined AP+STA protocol vectors
- **Initialize Metrics**: Batch metrics initialization for APs and STAs with Kafka and LeverAPI integration
- **Connect Traces**: Batch trace connection utilities for link measurement and association events

What the Module Cannot Do
==========================

The Simulation Helper module:

- **Does Not Implement Protocols**: It is a helper/utility module only. It uses protocols from other modules (beacon-neighbor-protocol-11k, bss_tm_11v, link-protocol-11k, etc.)
- **Does Not Provide MAC/PHY**: It uses |ns3|'s WiFi module for MAC/PHY functionality
- **Does Not Handle Mobility**: Users must configure mobility models separately
- **Does Not Perform Simulation Logic**: It only sets up the infrastructure; simulation behavior is determined by the underlying protocols

Architecture and Design
***********************

The Simulation Helper module consists of three main helper classes:

1. **SimulationHelper**: Protocol setup and WiFi device installation
2. **MetricsHelper**: Metrics management and initialization
3. **TraceHelper**: Batch trace connection utilities

SimulationHelper Class
======================

The SimulationHelper class is a static utility class (no instance creation needed) that provides factory methods for:

Protocol Setup Methods
######################

- ``SetupAPDualPhySniffer()`` - Configure DualPhySniffer for APs (used by Neighbor Protocol)
- ``SetupSTADualPhySniffer()`` - Configure DualPhySniffer for STAs (used by Beacon Protocol)
- ``SetupNeighborProtocol()`` - Setup Neighbor Protocol for APs and STAs
- ``SetupBeaconProtocol()`` - Setup Beacon Protocol for APs and STAs
- ``SetupBssTmHelper()`` - Setup BSS Transition Management for APs and STAs
- ``SetupAutoRoamingKvHelperMulti()`` - Setup complete roaming orchestration for multiple STAs

WiFi Device Installation Methods
#################################

- ``InstallApDevices()`` - Batch AP device installation with per-AP channel configuration
- ``InstallStaDevices()`` - Batch STA device installation with per-STA channel configuration

Utility Methods
###############

- ``GetWifiNetDevices()`` - Convert NetDeviceContainer to vector of WifiNetDevice pointers
- ``SplitProtocolVector()`` - Template function to split combined AP+STA protocol vectors

Protocol Chain Architecture
############################

The SimulationHelper sets up the following protocol chain::

    AutoRoamingKvHelper (Top-level Orchestration)
        │
        ├─── Link Measurement Protocol (RSSI/RCPI/RSNI monitoring)
        │      │
        │      └─── Triggers roaming decision when RSSI drops
        │
        ├─── Neighbor Protocol (802.11k Neighbor Reports)
        │      │
        │      ├─── APs: Use DualPhySniffer (apSniffer) for neighbor discovery
        │      │         Scan neighboring channels to discover other APs
        │      │
        │      └─── STAs: Passive (no sniffer)
        │               Request neighbor reports from associated AP
        │
        ├─── Beacon Protocol (802.11k Beacon Reports)
        │      │
        │      ├─── APs: Passive (no sniffer)
        │      │         Request beacon reports from associated STAs
        │      │
        │      └─── STAs: Use DualPhySniffer (staSniffer) for beacon scanning
        │                Scan neighboring channels to discover APs
        │
        └─── BSS TM (802.11v BSS Transition Management)
               │
               ├─── APs: Send transition requests to STAs
               │
               └─── STAs: Process requests and initiate roaming

Two-Instance DualPhySniffer Architecture
#########################################

The module uses a **two-instance** DualPhySniffer architecture for optimal separation of concerns:

**Instance 1: AP Sniffer** (``apDualPhySniffer``):

- Installed on all APs
- Used by Neighbor Protocol on APs
- Scans neighboring channels to discover other APs
- Builds neighbor tables automatically via beacon cache

**Instance 2: STA Sniffer** (``staDualPhySniffer``):

- Installed on all STAs
- Used by Beacon Protocol on STAs
- Scans neighboring channels to discover APs
- Generates beacon reports for roaming decisions

This separation provides:

- Independent operation of Neighbor Protocol (AP-side) and Beacon Protocol (STA-side)
- Each protocol gets its own dedicated scanning radio
- No interference between AP neighbor discovery and STA beacon scanning
- Clean separation of concerns

MetricsHelper Class
===================

Provides utilities for metrics management:

- Initialize metrics for APs and STAs
- Search metrics using custom predicates
- Update metric values
- Wire metrics to Kafka producer
- Wire metrics to LeverAPI for dynamic configuration

TraceHelper Class
=================

Provides batch trace connection utilities:

- Connect link measurement traces for multiple protocol instances
- Connect WiFi association/disassociation traces

Usage
*****

Helpers
=======

SimulationHelper Methods
########################

SetupAPDualPhySniffer
~~~~~~~~~~~~~~~~~~~~~

Sets up beacon monitoring with channel hopping for multiple APs. Used by Neighbor Protocol for dynamic neighbor discovery.

::

    #include "ns3/simulation-helper.h"

    // Setup AP DualPhySniffer
    std::vector<uint8_t> scanChannels = {36, 40, 44, 48};
    DualPhySnifferHelper* apSniffer = SimulationHelper::SetupAPDualPhySniffer(
        apDevices,           // NetDeviceContainer with all AP WiFi devices
        apMacs,              // Vector of AP MAC addresses
        spectrumChannel,     // Shared Ptr<SpectrumChannel>
        apChannels,          // Operating channel for each AP
        scanChannels,        // Channels to scan
        Seconds(0.3));       // Hop interval

SetupSTADualPhySniffer
~~~~~~~~~~~~~~~~~~~~~~

Sets up beacon monitoring with channel hopping for multiple STAs. Used by Beacon Protocol for multi-channel beacon scanning.

::

    // Setup STA DualPhySniffer
    DualPhySnifferHelper* staSniffer = SimulationHelper::SetupSTADualPhySniffer(
        staDevices,          // NetDeviceContainer with all STA WiFi devices
        staMacs,             // Vector of STA MAC addresses
        spectrumChannel,     // Shared Ptr<SpectrumChannel>
        staChannels,         // Operating channel for each STA
        scanChannels,        // Channels to scan
        Seconds(0.3));       // Hop interval

SetupNeighborProtocol
~~~~~~~~~~~~~~~~~~~~~

Sets up Neighbor Protocol (802.11k) for both APs and STAs.

::

    // Build neighbor table
    std::vector<ApInfo> neighborTable;
    for (uint32_t i = 0; i < apDevices.GetN(); i++) {
        ApInfo info;
        info.bssid = apMacs[i];
        info.channel = apChannels[i];
        info.ssid = ssid;
        neighborTable.push_back(info);
    }

    // Setup Neighbor Protocol (returns combined AP+STA vector)
    auto neighborProtocols = SimulationHelper::SetupNeighborProtocol(
        apDevices,           // AP WiFi devices
        staDevices,          // STA WiFi devices
        apMacs,              // AP MAC addresses
        neighborTable,       // Pre-configured neighbor table
        apSniffer);          // AP sniffer (for APs), nullptr for STAs

SetupBeaconProtocol
~~~~~~~~~~~~~~~~~~~

Sets up Beacon Protocol (802.11k) for both APs and STAs.

::

    // Setup Beacon Protocol (returns combined AP+STA vector)
    auto beaconProtocols = SimulationHelper::SetupBeaconProtocol(
        apDevices,           // AP WiFi devices
        staDevices,          // STA WiFi devices
        staSniffer);         // STA sniffer (for STAs), nullptr for APs

SetupBssTmHelper
~~~~~~~~~~~~~~~~

Sets up BSS Transition Management (802.11v) for both APs and STAs.

::

    // Setup BSS TM (returns combined AP+STA vector)
    auto bssTmHelpers = SimulationHelper::SetupBssTmHelper(
        apDevices,           // AP WiFi devices
        staDevices);         // STA WiFi devices

SetupAutoRoamingKvHelperMulti
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Sets up complete roaming orchestration for multiple STAs. This is the highest-level method that wires together the entire protocol chain.

::

    // First, split protocol vectors into AP and STA instances
    auto [neighborAps, neighborStas] = SimulationHelper::SplitProtocolVector(
        neighborProtocols, apDevices.GetN());

    auto [beaconAps, beaconStas] = SimulationHelper::SplitProtocolVector(
        beaconProtocols, apDevices.GetN());

    auto [bssTmAps, bssTmStas] = SimulationHelper::SplitProtocolVector(
        bssTmHelpers, apDevices.GetN());

    // Setup auto-roaming orchestration
    auto roamingContainer = SimulationHelper::SetupAutoRoamingKvHelperMulti(
        apDevices,           // AP WiFi devices
        staDevices,          // STA WiFi devices
        neighborStas,        // Neighbor protocol instances for STAs
        beaconStas,          // Beacon protocol instances for STAs
        beaconAps,           // Beacon protocol instances for APs
        bssTmStas,           // BSS TM instances for STAs
        Seconds(1.0),        // Link measurement interval
        -65.0);              // RSSI threshold for roaming

The returned ``AutoRoamingKvHelperContainer`` contains:

- ``helpers`` - Vector of AutoRoamingKvHelper pointers (one per STA)
- ``apProtocols`` - Vector of Link Measurement Protocol instances for APs (per helper)
- ``staProtocols`` - Vector of Link Measurement Protocol instances for STAs (per helper)

InstallApDevices
~~~~~~~~~~~~~~~~

Batch installation of AP WiFi devices with per-AP channel configuration.

::

    // Configure WiFi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager");

    SpectrumWifiPhyHelper phyHelper;
    phyHelper.SetChannel(spectrumChannel);

    WifiMacHelper macHelper;
    Ssid ssid = Ssid("roaming-network");

    // Install APs on different channels
    std::vector<uint8_t> apChannels = {36, 40, 44, 48};
    NetDeviceContainer apDevices = SimulationHelper::InstallApDevices(
        wifi,                // WifiHelper (pre-configured)
        phyHelper,           // SpectrumWifiPhyHelper (pre-configured)
        macHelper,           // WifiMacHelper (will be configured as ApWifiMac)
        apNodes,             // NodeContainer with AP nodes
        apChannels,          // Channel number for each AP
        ssid,                // Network SSID
        20.0,                // TX power (dBm)
        20,                  // Channel width (MHz)
        WIFI_PHY_BAND_5GHZ); // WiFi band

InstallStaDevices
~~~~~~~~~~~~~~~~~

Batch installation of STA WiFi devices with per-STA channel configuration.

::

    // Install STAs (initially on channel 36)
    std::vector<uint8_t> staChannels(10, 36);  // All STAs start on channel 36
    NetDeviceContainer staDevices = SimulationHelper::InstallStaDevices(
        wifi,                // WifiHelper (pre-configured)
        phyHelper,           // SpectrumWifiPhyHelper (pre-configured)
        macHelper,           // WifiMacHelper (will be configured as StaWifiMac)
        staNodes,            // NodeContainer with STA nodes
        staChannels,         // Channel number for each STA
        ssid,                // Network SSID
        20,                  // Channel width (MHz)
        WIFI_PHY_BAND_5GHZ); // WiFi band

SplitProtocolVector
~~~~~~~~~~~~~~~~~~~

Template function to split combined AP+STA protocol vectors.

::

    // Many setup functions return combined vectors: [AP0, AP1, AP2, STA0, STA1, STA2]
    std::vector<Ptr<NeighborProtocolHelper>> allProtocols =
        SimulationHelper::SetupNeighborProtocol(...);

    // Split at index 3 (number of APs)
    auto [apProtos, staProtos] = SimulationHelper::SplitProtocolVector(
        allProtocols,  // Combined vector
        3);            // Number of APs (split point)

    // Now: apProtos = [AP0, AP1, AP2]
    //      staProtos = [STA0, STA1, STA2]

GetWifiNetDevices
~~~~~~~~~~~~~~~~~

Convert NetDeviceContainer to vector of WifiNetDevice pointers.

::

    std::vector<Ptr<WifiNetDevice>> wifiDevices =
        SimulationHelper::GetWifiNetDevices(devices);

TraceHelper Methods
###################

ConnectLinkMeasurementTraces
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Connect link measurement traces for multiple protocol instances.

::

    #include "ns3/roaming-trace-helper.h"

    // Callback function
    void OnLinkMeasurementReport(Mac48Address from, LinkMeasurementReport report) {
        std::cout << "Report from " << from
                  << " RCPI: " << report.GetRcpiDbm() << " dBm\n";
    }

    // Connect traces for all STA link measurement protocols
    TraceHelper::ConnectLinkMeasurementTraces(
        staLinkProtocols,  // Vector of LinkMeasurementProtocol instances
        MakeCallback(&OnLinkMeasurementReport));

Attributes
==========

Not applicable. The Simulation Helper module does not define any attributes. It is a pure helper module that creates and configures instances of other protocols.

The underlying protocols (Neighbor Protocol, Beacon Protocol, BSS TM, Link Measurement Protocol) have their own attributes documented in their respective modules.

Traces
======

Not applicable. The Simulation Helper module does not define trace sources. However, it provides the TraceHelper class for batch connection of traces from the underlying protocols.

Examples and Tests
******************

simulation-helper-example.cc
=============================

Location: ``contrib/simulation-helper/examples/simulation-helper-example.cc``

This example demonstrates the complete usage of the Simulation Helper module:

**Scenario:**

- 4 APs on channels 36, 40, 44, 48
- 3 STAs initially on channel 36
- Complete protocol chain: Neighbor → Beacon → BSS TM → Auto-Roaming
- STAs move and trigger roaming when RSSI drops below -65 dBm

**What it demonstrates:**

1. Multi-AP, multi-STA topology setup using SimulationHelper
2. DualPhySniffer configuration for both APs and STAs
3. Complete protocol chain setup (Neighbor, Beacon, BSS TM, Auto-Roaming)
4. Protocol vector splitting to separate AP and STA instances
5. Roaming orchestration with RSSI thresholds
6. Trace connection for link measurement events
7. Statistics collection and reporting

**Running the Example:**

.. sourcecode:: bash

    ./ns3 run simulation-helper-example

Test Suite
==========

The Simulation Helper module includes a test suite in ``test/simulation-helper-test-suite.cc``.

**Current Status:** The test currently contains only a placeholder test.

**Future Work:** Real tests should be added to verify protocol setup, device installation, and utility functions.

Validation
**********

Production Usage
================

The Simulation Helper module is actively used in production simulations:

**config-simulation.cc**

Location: ``contrib/final-simulation/examples/config-simulation.cc``

This is a large-scale roaming simulation with:

- 4 APs and 10 STAs
- Complete protocol chain integration
- Kafka metrics export
- LeverAPI dynamic configuration
- 24+ hour simulation runs

The module has been validated through extensive use in this production simulation.

Integration Testing
===================

The module is integration-tested through:

1. **simulation-helper-example.cc**: Basic functionality test
2. **config-simulation.cc**: Production-scale integration test with all features
3. **Manual testing**: Roaming scenarios with varying topologies and parameters

Test Coverage
=============

**Current Coverage:**

- ✓ Protocol setup functions (tested in examples)
- ✓ WiFi device installation (tested in examples)
- ✓ Protocol vector splitting (tested in examples)
- ✓ DualPhySniffer integration (tested in examples)
- ✓ Auto-roaming orchestration (tested in config-simulation)
- ⚠ Unit tests (placeholder only - needs implementation)

References
**********

[1] **IEEE 802.11k-2008**: Radio Resource Measurement (RRM) including Neighbor Reports, Beacon Reports, and Link Measurement

[2] **IEEE 802.11v-2011**: Wireless Network Management including BSS Transition Management

[3] **IEEE 802.11-2020**: Complete WiFi standard (consolidates all amendments)

[4] |ns3| **WiFi Module**: https://www.nsnam.org/docs/models/html/wifi.html

[5] |ns3| **Spectrum Module**: https://www.nsnam.org/docs/models/html/spectrum.html
