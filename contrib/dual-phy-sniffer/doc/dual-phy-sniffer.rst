Dual-PHY Sniffer Module Documentation
======================================

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ============= Module Name
   ------------- Section (#.#)
   ~~~~~~~~~~~~~ Subsection (#.#.#)

The Dual-PHY Sniffer module provides passive WiFi beacon monitoring capabilities across multiple channels for |ns3| simulations. Despite its name, the module creates a single scanning radio (not dual-PHY) that performs channel hopping to detect beacons from access points operating on different channels. The collected beacon information is stored in a queryable cache with RSSI/RCPI measurements, timestamps, and channel information.

The module is channel-agnostic, supporting both ``YansWifiChannel`` and ``SpectrumChannel`` propagation models seamlessly. It serves as a core component for Radio Resource Management (RRM), IEEE 802.11k neighbor/beacon reporting, and intelligent roaming decision systems.

The source code for the module lives in the directory ``contrib/dual-phy-sniffer``.

Scope and Limitations
---------------------

**What the module can do:**

* Passive beacon monitoring across multiple WiFi channels
* Channel-agnostic operation (supports both YansWifiChannel and SpectrumChannel)
* Channel hopping with configurable intervals
* RSSI/RCPI measurement collection per beacon
* Beacon cache with queryable API (GetAllBeacons, GetBeaconsReceivedBy, etc.)
* Support for 2.4 GHz and 5 GHz bands
* Support for variable channel widths (20/40/80/160 MHz)
* Integration with 802.11k neighbor/beacon report protocols
* RRM (Radio Resource Management) measurement matrices

**What the module cannot do:**

* Does not create a true dual-PHY setup (name is historical/misleading)
* Does not perform active scanning or probe requests
* Does not transmit any packets (listen-only mode)
* Does not create the operating WiFi interface (assumes it already exists)
* Does not perform channel quality analysis (only collects measurements)
* Does not make roaming decisions (provides data for decision-making)

Architecture
------------

Design Overview
~~~~~~~~~~~~~~~

The DualPhySnifferHelper creates a single scanning radio per ``Install()`` call using the following architecture:

1. **Scanning Radio Creation**

   * Creates WiFi NetDevice with AdhocWifiMac
   * Disables transmission (listen-only mode)
   * Configures initial channel and band

2. **Channel Hopping Mechanism**

   * Uses MAC-level ``SwitchChannel()`` method (not PHY reconfiguration)
   * Configurable hop interval (default: 500ms)
   * Explicit band specification (BAND_2_4GHZ or BAND_5GHZ)
   * Conditional width: width=20 for 2.4 GHz, width=0 (auto-detect) for 5 GHz

3. **Beacon Detection**

   * Monitors all beacons via ``MonitorSnifferRx`` callback
   * Extracts BSSID, SSID, channel, RSSI from beacon frames
   * Stores measurements in per-BSSID cache

4. **Measurement Storage**

   * Beacon cache organized by BSSID (Mac48Address → BeaconInfo map)
   * Each BSSID stores: SSID, list of measurements (RCPI, channel, timestamp)
   * Configurable cache limits (max age, max measurements per BSSID)

Channel-Agnostic Implementation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The module supports both channel models through overloaded ``SetChannel()`` methods:

::

    // For YansWifiChannel-based simulations
    void SetChannel(Ptr<YansWifiChannel> channel)

    // For SpectrumChannel-based simulations
    void SetChannel(Ptr<SpectrumChannel> channel)

The ``Install()`` method auto-detects which channel type was configured and uses the appropriate PHY helper (``YansWifiPhyHelper`` or ``SpectrumWifiPhyHelper``).

Channel Switching Strategy
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Channel switching uses modern |ns3| WiFi APIs with explicit band and conditional width:

* **2.4 GHz**: ``{channel, 20, BAND_2_4GHZ, 0}`` - fixed 20 MHz width
* **5 GHz**: ``{channel, 0, BAND_5GHZ, 0}`` - width=0 for auto-detection (20/40/80/160 MHz)

This approach avoids deprecated ``ChannelNumber`` and ``Frequency`` attributes while supporting variable channel widths in 5 GHz band.

Key Components
~~~~~~~~~~~~~~

* ``helper/dual-phy-sniffer-helper.{h,cc}`` - Main helper class for sniffer setup
* ``model/dual-phy-sniffer.{h,cc}`` - Model files (currently unused/empty)
* ``examples/dual-phy-sniffer-example.cc`` - Basic YansWifiChannel example
* ``examples/spectrum-dual-radio-example.cc`` - Advanced SpectrumWifiPhy RRM example

Usage
-----

Basic Setup (YansWifiChannel)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Simple beacon monitoring scenario:

::

    #include "ns3/dual-phy-sniffer-helper.h"

    // Create YansWifiChannel
    YansWifiChannelHelper channelHelper = YansWifiChannelHelper::Default();
    Ptr<YansWifiChannel> channel = channelHelper.Create();

    // Create DualPhySnifferHelper
    DualPhySnifferHelper sniffer;

    // Channel-agnostic: Set YansWifiChannel
    sniffer.SetChannel(channel);

    // Configure scanning channels (2.4 GHz)
    sniffer.SetScanningChannels({1, 6, 11});

    // Set channel hopping interval
    sniffer.SetHopInterval(Seconds(0.5));  // 500ms per channel

    // Install on monitoring node
    sniffer.Install(monitorNode, 1, Mac48Address("AA:BB:CC:DD:EE:FF"));

    // Start channel hopping
    sniffer.StartChannelHopping();

    // Query beacon cache
    auto allBeacons = sniffer.GetAllBeacons();
    for (const auto& beaconEntry : allBeacons) {
        const Mac48Address& bssid = beaconEntry.first;
        const BeaconInfo& info = beaconEntry.second;
        std::cout << "BSSID: " << bssid << " SSID: " << info.ssid << "\n";
    }

Advanced Setup (SpectrumChannel)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For spectrum-based simulations with RRM measurements:

::

    #include "ns3/dual-phy-sniffer-helper.h"

    // Create SpectrumChannel
    Ptr<MultiModelSpectrumChannel> spectrumChannel =
        CreateObject<MultiModelSpectrumChannel>();

    // Add propagation models
    Ptr<LogDistancePropagationLossModel> lossModel =
        CreateObject<LogDistancePropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(lossModel);

    // Create DualPhySnifferHelper
    DualPhySnifferHelper sniffer;

    // Channel-agnostic: Set SpectrumChannel (with explicit cast)
    sniffer.SetChannel(Ptr<SpectrumChannel>(spectrumChannel));

    // Configure scanning channels (5 GHz)
    sniffer.SetScanningChannels({36, 40, 44, 48});

    // Set channel hopping interval
    sniffer.SetHopInterval(Seconds(1.0));

    // Install on monitoring node
    sniffer.Install(monitorNode, 36, Mac48Address("BB:CC:DD:EE:FF:00"));

    // Start channel hopping
    sniffer.StartChannelHopping();

    // Query beacons received by specific transmitter
    Mac48Address targetBssid("00:00:00:00:00:01");
    auto beaconInfo = sniffer.GetBeaconsReceivedBy(targetBssid);

Querying Beacon Cache
~~~~~~~~~~~~~~~~~~~~~~

The module provides several methods to query the beacon cache:

::

    // Get all discovered beacons
    std::map<Mac48Address, BeaconInfo> allBeacons = sniffer.GetAllBeacons();

    // Get beacons from specific BSSID
    BeaconInfo info = sniffer.GetBeaconsReceivedBy(Mac48Address("00:00:00:00:00:01"));

    // Iterate through measurements
    for (const auto& measurement : info.measurements) {
        std::cout << "Channel: " << (int)measurement.channel << "\n";
        std::cout << "RCPI: " << measurement.rcpi << " dBm\n";
        std::cout << "Timestamp: " << measurement.timestamp.GetSeconds() << "s\n";
    }

Helpers
~~~~~~~

**DualPhySnifferHelper**

Main helper class for setting up passive beacon monitoring:

::

    DualPhySnifferHelper sniffer;

    // Channel configuration (choose one)
    sniffer.SetChannel(Ptr<YansWifiChannel> channel);           // For Yans
    sniffer.SetChannel(Ptr<SpectrumChannel> channel);           // For Spectrum

    // Scanning configuration
    sniffer.SetScanningChannels(std::vector<uint8_t> channels); // Required
    sniffer.SetHopInterval(Time interval);                       // Optional (default: 500ms)

    // Install scanning radio
    sniffer.Install(Ptr<Node> node, uint8_t initialChannel, Mac48Address address);

    // Start/stop channel hopping
    sniffer.StartChannelHopping();
    sniffer.StopChannelHopping();

    // Query beacon cache
    std::map<Mac48Address, BeaconInfo> allBeacons = sniffer.GetAllBeacons();
    BeaconInfo info = sniffer.GetBeaconsReceivedBy(Mac48Address bssid);

**Important Notes:**

* Create **ONE SHARED** instance per simulation (not per device)
* Call ``Install()`` for each node that needs a scanning radio
* Must call ``SetChannel()`` before ``Install()``
* Must call ``SetScanningChannels()`` before ``Install()``
* Call ``StartChannelHopping()`` after all ``Install()`` calls

Attributes
~~~~~~~~~~

The DualPhySnifferHelper does not expose ns-3 attributes. Configuration is done via method calls:

**Configuration Methods:**

* ``SetChannel()`` - Set YansWifiChannel or SpectrumChannel (required)
* ``SetScanningChannels()`` - Set list of channels to scan (required)
* ``SetHopInterval()`` - Set channel hopping interval (optional, default: 500ms)

**Beacon Cache Configuration:**

Currently hardcoded in implementation:

* Maximum measurements per BSSID: Unlimited (all measurements stored)
* Measurement expiration: No automatic cleanup (manual management needed)

Traces
~~~~~~

**MonitorSnifferRx** (Internal)

The module uses WiFi PHY's ``MonitorSnifferRx`` callback internally to detect beacons. This is not a user-facing trace source.

**No User-Facing Traces**

The module currently does not provide trace sources for:

* Beacon detected events
* Channel hop events
* Cache update events

Users can monitor beacon detection by periodically querying the cache via ``GetAllBeacons()``.

Data Structures
~~~~~~~~~~~~~~~

**DualPhyMeasurement**

Stores a single beacon measurement:

::

    struct DualPhyMeasurement {
        double rcpi;           // RSSI in dBm
        uint8_t channel;       // Channel number where beacon was detected
        Time timestamp;        // When the beacon was received
    };

**BeaconInfo**

Stores all measurements for a single BSSID:

::

    struct BeaconInfo {
        std::string ssid;                           // Network name
        std::vector<DualPhyMeasurement> measurements; // All measurements for this BSSID
    };

**Beacon Cache**

The complete beacon cache is organized as:

::

    std::map<Mac48Address, BeaconInfo>  // BSSID → BeaconInfo map

Examples and Tests
------------------

dual-phy-sniffer-example.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Basic example demonstrating beacon monitoring with YansWifiChannel:

* **Scenario**: 3 APs on different 2.4 GHz channels (1, 6, 11)
* **Monitoring**: Single node with scanning radio
* **Channel Hopping**: 500ms per channel (1 → 6 → 11 → repeat)
* **Output**: Beacon cache printed at t=2s, t=5s, t=8s
* **Purpose**: Demonstrates basic API usage and beacon detection
* **Verification**: Checks that all 3 APs are discovered

Running the example:

::

    ./ns3 run "dual-phy-sniffer-example"

Command-line parameters:

::

    ./ns3 run "dual-phy-sniffer-example --simTime=15 --hopInterval=1.0 --verbose=1"

* ``--simTime`` - Simulation duration in seconds (default: 10.0)
* ``--hopInterval`` - Channel hopping interval in seconds (default: 0.5)
* ``--verbose`` - Enable verbose logging (default: false)

Expected output:

* Setup messages showing 3 APs created on channels 1, 6, 11
* DualPhySnifferHelper configuration summary
* Beacon cache status at t=2s, t=5s, t=8s
* Final summary showing all discovered APs with RSSI measurements

spectrum-dual-radio-example.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Advanced example demonstrating RRM measurements with SpectrumWifiPhy:

* **Scenario**: 5 APs with scanning radios, all on different 5 GHz channels
* **Measurements**: Each AP scans all channels to build 3D RCPI matrix
* **Matrix Dimensions**: [channel][transmitter][receiver] = RCPI
* **Channel Hopping**: 1 second per channel across {36, 40, 44, 48, 52}
* **Output**: CSV files with RCPI matrices for offline analysis
* **Purpose**: Demonstrates RRM use case and Spectrum channel support
* **Verification**: Checks matrix completeness and measurement accuracy

Running the example:

::

    ./ns3 run "spectrum-dual-radio-example"

Expected output:

* AP setup on channels 36, 40, 44, 48, 52
* Channel hopping progress messages
* RRM matrix construction
* CSV export: ``rcpi_matrix_ch36.csv``, ``rcpi_matrix_ch40.csv``, etc.
* Verification: "All expected beacons detected" message

Test Suite Status
~~~~~~~~~~~~~~~~~~

**Current Status**: Stub only (no functional tests)

The test suite (``test/dual-phy-sniffer-test-suite.cc``) currently contains only a placeholder test case. Comprehensive unit tests are needed for:

* Beacon detection accuracy
* Channel hopping correctness
* Beacon cache operations
* RCPI calculation validation
* Both YansWifiChannel and SpectrumChannel support

Integration
-----------

The DualPhySnifferHelper is widely integrated across the contrib/ modules:

Integration with 802.11k Protocols
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**beacon-neighbor-protocol-11k module:**

* NeighborProtocolHelper uses DualPhySnifferHelper for automatic neighbor discovery
* BeaconProtocolHelper uses it for beacon report measurements
* Shared instance pattern via ``SetDualPhySniffer()`` method

::

    // Example from beacon-neighbor-protocol-11k
    DualPhySnifferHelper dualPhySniffer;
    dualPhySniffer.SetChannel(channel);
    dualPhySniffer.SetScanningChannels({36, 40, 44, 48});
    dualPhySniffer.Install(apNode, 36, apMac);
    dualPhySniffer.StartChannelHopping();

    // Share with protocol helpers
    neighborHelper->SetDualPhySniffer(&dualPhySniffer);
    beaconHelper->SetDualPhySniffer(&dualPhySniffer);

Integration with Auto-Roaming-KV
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**auto-roaming-kv module:**

Uses DualPhySnifferHelper for roaming decision support by:

* Discovering neighboring APs via beacon monitoring
* Collecting RSSI measurements across channels
* Providing data for BSS Transition Management (802.11v) decisions

Integration with SimulationHelper
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**simulation-helper module:**

Provides wrapper functions for common setups:

* ``SetupAPDualPhySniffer()`` - Configure sniffer for AP nodes
* ``SetupSTADualPhySniffer()`` - Configure sniffer for STA nodes
* Used extensively in final-simulation/config-simulation.cc

Validation
----------

The module has been validated through:

1. **Production Use**

   * Deployed in final-simulation as core dependency
   * Used for 802.11k/v roaming protocol chain
   * Tested with both YansWifiChannel and SpectrumChannel

2. **Integration Testing**

   * Verified with beacon-neighbor-protocol-11k module
   * Verified with auto-roaming-kv module
   * Tested in multi-AP scenarios (4+ APs)
   * Tested in both 2.4 GHz and 5 GHz bands

3. **Functional Validation**

   * Beacon detection accuracy verified via spectrum-dual-radio-example
   * Channel hopping verified to visit all configured channels
   * RCPI measurements verified against expected propagation loss
   * Beacon cache querying verified to return correct data

4. **Performance Testing**

   * Tested with up to 10 scanning radios simultaneously
   * Tested with channel lists up to 8 channels
   * Tested with hop intervals from 100ms to 2 seconds
   * No performance degradation observed

**Limitations:**

* No formal unit test suite (only stub tests)
* No validation against real hardware measurements
* No comparison with other beacon monitoring implementations

References
----------

[`1 <https://ieeexplore.ieee.org/document/7786995>`_] IEEE Standard for Information technology--Telecommunications and information exchange between systems Local and metropolitan area networks--Specific requirements - Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) Specifications, IEEE Std 802.11-2020.

[`2 <https://ieeexplore.ieee.org/document/4769102>`_] IEEE Standard for Information technology--Telecommunications and information exchange between systems--Local and metropolitan area networks--Specific requirements Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) specifications Amendment 1: Radio Resource Measurement of Wireless LANs, IEEE Std 802.11k-2008.

[`3 <https://www.nsnam.org/docs/models/html/wifi.html>`_] |ns3| WiFi Module Documentation.
