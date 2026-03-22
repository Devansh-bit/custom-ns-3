Beacon and Neighbor Protocol 11k Module Documentation
======================================================

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ============= Module Name
   ------------- Section (#.#)
   ~~~~~~~~~~~~~ Subsection (#.#.#)

The Beacon and Neighbor Protocol 11k module provides an IEEE 802.11k-based neighbor discovery and beacon measurement
system for |ns3| WiFi simulations. It implements the Neighbor Report Protocol (802.11k Section 7.4.7) and Beacon Report
Protocol (802.11k Section 7.4.5) to enable WiFi stations to discover neighboring access points and measure their link
quality for intelligent roaming decisions.

The module provides the *information gathering* phase of WiFi roaming, allowing STAs to discover neighboring APs and
measure their beacon signal strengths across multiple channels. This information can then be used by higher-level
protocols (such as BSS Transition Management 802.11v) to make roaming decisions.

The source code for the module lives in the directory ``contrib/beacon-neighbor-protocol-11k``.

Scope and Limitations
---------------------

**What the module can do:**

* Implement IEEE 802.11k Neighbor Report Request/Response protocol
* Implement IEEE 802.11k Beacon Report Request/Response protocol
* Support static neighbor table configuration for APs
* Support dynamic neighbor discovery using multi-channel scanning
* Measure beacon RSSI/SNR from neighboring APs on different channels
* Integrate with dual-PHY sniffer for cross-channel beacon detection
* Work with both YansWifiChannel and SpectrumChannel propagation models
* Convert between RSSI/SNR and IEEE 802.11k RCPI/RSNI formats
* Support multi-AP scenarios with different operating channels

**What the module cannot do:**

* Does not make roaming decisions (provides measurement data only)
* Does not implement BSS Transition Management (see bss_tm_11v module)
* Does not perform actual reassociation (handled by StaWifiMac)
* Does not support active scanning mode (passive beacon monitoring only)
* Does not implement 802.11k Link Measurement Protocol (see link-protocol-11k module)
* Does not support off-channel measurements during data transmission (requires dual-PHY)

Architecture
------------

Protocol Sequence
~~~~~~~~~~~~~~~~~

The module implements a two-stage information gathering process:

1. **Neighbor Discovery (Neighbor Report Protocol)**

   * STA sends Neighbor Report Request to associated AP
   * AP responds with Neighbor Report Response containing list of neighbor APs
   * Each neighbor includes: BSSID, channel, regulatory class, PHY type, BSSID info
   * If dual-PHY sniffer enabled, AP automatically discovers neighbors via beacon scanning

2. **Beacon Measurement (Beacon Report Protocol)**

   * AP sends Beacon Request to STA with list of target BSSIDs to measure
   * STA measures beacon frames from specified neighbors
   * If dual-PHY sniffer enabled, STA scans beacons on different channels
   * STA sends Beacon Report back to AP with measurements per BSSID
   * Each report includes: BSSID, channel, RCPI (RSSI), RSNI (SNR), measurement timing

Key Components
~~~~~~~~~~~~~~

* ``helper/neighbor-protocol-11k-helper.{h,cc}`` - Neighbor Report protocol helper
* ``helper/beacon-protocol-11k-helper.{h,cc}`` - Beacon Report protocol helper
* ``model/neighbor-protocol-11k.{h,cc}`` - Neighbor Report request/response frame handling
* ``model/beacon-protocol-11k.{h,cc}`` - Beacon Report request/response frame handling
* ``examples/beacon-neighbor-protocol-11k-example.cc`` - Complete usage example with mobility

Dependencies
~~~~~~~~~~~~

**Optional Modules:**

* ``contrib/dual-phy-sniffer/`` - Multi-channel beacon detection (required for cross-channel measurements)

**Note:** The module can work without dual-phy-sniffer using static neighbor tables, but multi-channel dynamic discovery
requires the dual-phy-sniffer module.

Usage
-----

Basic Setup (Static Neighbor Table)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For simple scenarios where the neighbor topology is known in advance:

::

    #include "ns3/neighbor-protocol-11k-helper.h"
    #include "ns3/beacon-protocol-11k-helper.h"

    // Create protocol helpers (SEPARATE instances per device)
    Ptr<NeighborProtocolHelper> neighborHelper = CreateObject<NeighborProtocolHelper>();
    Ptr<BeaconProtocolHelper> beaconHelper = CreateObject<BeaconProtocolHelper>();

    // Configure static neighbor table
    std::vector<ApInfo> neighborTable;
    ApInfo ap;
    ap.bssid = Mac48Address("00:00:00:00:00:02");
    ap.channel = 40;
    ap.regulatoryClass = 115;
    ap.phyType = 7;
    neighborTable.push_back(ap);
    neighborHelper->SetNeighborTable(neighborTable);

    // Install protocols on AP and STA
    neighborHelper->InstallOnAp(apDevice);
    neighborHelper->InstallOnSta(staDevice);
    beaconHelper->InstallOnAp(apDevice);
    beaconHelper->InstallOnSta(staDevice);

    // Connect trace sources
    neighborHelper->TraceConnectWithoutContext(
        "NeighborReportReceived",
        MakeCallback(&OnNeighborReportReceived));

    beaconHelper->TraceConnectWithoutContext(
        "BeaconReportReceived",
        MakeCallback(&OnBeaconReportReceived));

    // Trigger neighbor discovery
    Simulator::Schedule(Seconds(2.0),
                        &NeighborProtocolHelper::SendNeighborReportRequest,
                        PeekPointer(neighborHelper),
                        staDevice,
                        apDevice->GetMac()->GetAddress());

Advanced Setup (Multi-channel Dynamic Discovery)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For multi-channel scenarios requiring dynamic neighbor discovery:

::

    #include "ns3/neighbor-protocol-11k-helper.h"
    #include "ns3/beacon-protocol-11k-helper.h"
    #include "ns3/dual-phy-sniffer-helper.h"

    // Create SHARED DualPhySnifferHelper for multi-channel scanning
    DualPhySnifferHelper dualPhySniffer;

    // Channel-agnostic - works with both Yans and Spectrum channels
    dualPhySniffer.SetChannel(Ptr<SpectrumChannel>(spectrumChannel));  // For Spectrum
    // OR: dualPhySniffer.SetChannel(yansChannel);  // For Yans

    dualPhySniffer.SetScanningChannels({36, 40, 44, 48});
    dualPhySniffer.SetHopInterval(Seconds(0.5));

    // Install on AP node (for automatic neighbor discovery)
    dualPhySniffer.Install(apDevice->GetNode(), 36, apDevice->GetMac()->GetAddress());
    dualPhySniffer.StartChannelHopping();

    // Create protocol helpers and connect to dual-PHY sniffer
    Ptr<NeighborProtocolHelper> neighborHelper = CreateObject<NeighborProtocolHelper>();
    Ptr<BeaconProtocolHelper> beaconHelper = CreateObject<BeaconProtocolHelper>();

    neighborHelper->SetDualPhySniffer(&dualPhySniffer);
    beaconHelper->SetDualPhySniffer(&dualPhySniffer);

    // Install protocols
    neighborHelper->InstallOnAp(apDevice);
    neighborHelper->InstallOnSta(staDevice);
    beaconHelper->InstallOnAp(apDevice);
    beaconHelper->InstallOnSta(staDevice);

    // Protocols will automatically discover neighbors across channels

Trace Callback Handlers
~~~~~~~~~~~~~~~~~~~~~~~~

::

    void OnNeighborReportReceived(Mac48Address staAddr,
                                   Mac48Address apAddr,
                                   std::vector<NeighborReportData> neighbors)
    {
        std::cout << "Neighbor Report from " << apAddr << " to " << staAddr << "\n";
        std::cout << "Number of neighbors: " << neighbors.size() << "\n";

        for (const auto& neighbor : neighbors) {
            std::cout << "  BSSID: " << neighbor.bssid
                      << " Channel: " << (int)neighbor.channel
                      << " Regulatory Class: " << (int)neighbor.regulatoryClass
                      << " PHY Type: " << (int)neighbor.phyType << "\n";
        }

        // Update beacon helper to measure these neighbors
        beaconHelper->SetNeighborList(neighborHelper->GetNeighborList());

        // Trigger beacon measurement
        Simulator::Schedule(MilliSeconds(50),
                            &BeaconProtocolHelper::SendBeaconRequest,
                            PeekPointer(beaconHelper),
                            apDevice,
                            staAddr);
    }

    void OnBeaconReportReceived(Mac48Address apAddr,
                                 Mac48Address staAddr,
                                 std::vector<BeaconReportData> reports)
    {
        std::cout << "Beacon Report from " << staAddr << " to " << apAddr << "\n";
        std::cout << "Number of reports: " << reports.size() << "\n";

        for (const auto& report : reports) {
            // Convert IEEE 802.11k RCPI/RSNI to RSSI/SNR
            double rssi = RcpiToRssi(report.rcpi);
            double snr = RsniToSnr(report.rsni);

            std::cout << "  BSSID: " << report.bssid
                      << " Channel: " << (int)report.channel
                      << " RSSI: " << rssi << " dBm"
                      << " SNR: " << snr << " dB\n";
        }

        // Use measurements for roaming decision...
    }

Helpers
~~~~~~~

**NeighborProtocolHelper**

Main helper class for Neighbor Report protocol operations:

::

    Ptr<NeighborProtocolHelper> neighborHelper = CreateObject<NeighborProtocolHelper>();

    // Optional: Configure static neighbor table
    neighborHelper->SetNeighborTable(neighborTable);

    // Optional: Enable multi-channel dynamic discovery
    neighborHelper->SetDualPhySniffer(&dualPhySniffer);

    // Install on devices
    neighborHelper->InstallOnAp(apDevice);
    neighborHelper->InstallOnSta(staDevice);

    // Trigger neighbor discovery
    neighborHelper->SendNeighborReportRequest(staDevice, apMacAddress);

**BeaconProtocolHelper**

Main helper class for Beacon Report protocol operations:

::

    Ptr<BeaconProtocolHelper> beaconHelper = CreateObject<BeaconProtocolHelper>();

    // Configure which BSSIDs to measure
    beaconHelper->SetNeighborList(neighborSet);

    // Optional: Enable multi-channel beacon scanning
    beaconHelper->SetDualPhySniffer(&dualPhySniffer);

    // Install on devices
    beaconHelper->InstallOnAp(apDevice);
    beaconHelper->InstallOnSta(staDevice);

    // Trigger beacon measurement
    beaconHelper->SendBeaconRequest(apDevice, staAddress);

**Important Instance Management:**

* Create **SEPARATE** ``NeighborProtocolHelper`` and ``BeaconProtocolHelper`` instances per device
* Create **ONE SHARED** ``DualPhySnifferHelper`` instance for all devices (if using multi-channel scanning)
* Connect trace callbacks to appropriate helper instances

Attributes
~~~~~~~~~~

**NeighborProtocolHelper Attributes:**

This helper does not expose configurable attributes. Configuration is done via methods:

* ``SetNeighborTable()`` - Configure static neighbor list
* ``SetDualPhySniffer()`` - Enable multi-channel dynamic discovery

**BeaconProtocolHelper Attributes:**

This helper does not expose configurable attributes. Configuration is done via methods:

* ``SetNeighborList()`` - Configure which BSSIDs to measure
* ``SetDualPhySniffer()`` - Enable multi-channel beacon scanning

Traces
~~~~~~

**NeighborProtocolHelper Trace Sources:**

* **NeighborReportReceived** - Fired when STA receives neighbor report from AP

  * Callback signature: ``void (Mac48Address staAddr, Mac48Address apAddr, std::vector<NeighborReportData> neighbors)``
  * Parameters:

    * ``staAddr`` - MAC address of STA receiving the report
    * ``apAddr`` - MAC address of AP sending the report
    * ``neighbors`` - Vector of neighbor AP information

**BeaconProtocolHelper Trace Sources:**

* **BeaconReportReceived** - Fired when AP receives beacon report from STA

  * Callback signature: ``void (Mac48Address apAddr, Mac48Address staAddr, std::vector<BeaconReportData> reports)``
  * Parameters:

    * ``apAddr`` - MAC address of AP receiving the report
    * ``staAddr`` - MAC address of STA sending the report
    * ``reports`` - Vector of beacon measurements

Examples and Tests
------------------

beacon-neighbor-protocol-11k-example.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Complete example demonstrating IEEE 802.11k neighbor and beacon reporting with multi-channel scanning:

* **Scenario**: 5 APs on different channels (36, 40, 44 in 5 GHz band), 1 mobile STA
* **Mobility**: STA moves away from AP at constant velocity (waypoint model)
* **Protocols**: Full 802.11k Neighbor Report → Beacon Report chain
* **Multi-channel**: Uses DualPhySnifferHelper for cross-channel beacon detection
* **Channel Model**: Uses SpectrumWifiPhy with MultiModelSpectrumChannel
* **Monitoring**: Connection quality sniffer triggers neighbor discovery when RSSI drops
* **Verification**: Checks that neighbor reports are received and beacon measurements are collected

Running the example:

::

    ./ns3 run "beacon-neighbor-protocol-11k-example"

Command-line parameters:

::

    ./ns3 run "beacon-neighbor-protocol-11k-example --nAPs=3 --rssi=-70 --time=15 --verbose=1"

* ``--nAPs`` - Number of access points (default: 5)
* ``--apDistance`` - Distance between APs in meters (default: 5.0)
* ``--rssi`` - RSSI threshold for triggering neighbor discovery in dBm (default: -60.0)
* ``--time`` - Simulation duration in seconds (default: 10.0)
* ``--verbose`` - Enable detailed logging

Expected output includes:

* Connection quality monitoring showing RSSI degradation as STA moves away
* Neighbor report triggered when RSSI crosses threshold
* Neighbor report response with list of discovered APs
* Beacon request sent to STA
* Beacon report with RSSI/SNR measurements per neighbor
* Protocol sequence completion message

Validation
----------

The module has been validated through:

1. **Protocol Compliance Testing**

   * Neighbor Report Request/Response frame formats verified against IEEE 802.11k-2016 Section 7.4.7
   * Beacon Report Request/Response frame formats verified against IEEE 802.11k-2016 Section 7.4.5
   * RCPI/RSNI encoding verified against IEEE 802.11 specifications

2. **Functional Testing**

   * Verified neighbor discovery works with static neighbor tables
   * Verified multi-channel neighbor discovery with dual-PHY sniffer
   * Verified beacon measurements collected from neighbors on different channels
   * Verified RSSI/SNR conversion to/from RCPI/RSNI format
   * Verified trace callbacks fire with correct parameters

3. **Integration Testing**

   * Tested with both YansWifiChannel and SpectrumChannel propagation models
   * Tested integration with DualPhySnifferHelper for multi-channel scanning
   * Tested in multi-AP scenarios with different operating channels (36, 40, 44 in 5 GHz)
   * Verified compatibility with mobility models (waypoint, constant velocity)

4. **Stress Testing**

   * Multiple concurrent neighbor/beacon report exchanges
   * Rapid RSSI changes triggering frequent neighbor requests
   * Large neighbor lists (5+ APs)

No formal analytical validation against real-world 802.11k implementations has been performed.

References
----------

[`1 <https://ieeexplore.ieee.org/document/7786995>`_] IEEE Standard for Information technology--Telecommunications and
information exchange between systems Local and metropolitan area networks--Specific requirements - Part 11: Wireless LAN
Medium Access Control (MAC) and Physical Layer (PHY) Specifications, IEEE Std 802.11-2016.

[`2 <https://ieeexplore.ieee.org/document/4769102>`_] IEEE Standard for Information technology--Telecommunications and
information exchange between systems--Local and metropolitan area networks--Specific requirements Part 11: Wireless LAN
Medium Access Control (MAC) and Physical Layer (PHY) specifications Amendment 1: Radio Resource Measurement of Wireless
LANs, IEEE Std 802.11k-2008.

[`3 <https://www.nsnam.org/docs/models/html/wifi.html>`_] |ns3| WiFi Module Documentation.
