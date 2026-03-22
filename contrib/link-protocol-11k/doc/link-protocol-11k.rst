Link Measurement Protocol (IEEE 802.11k) Module Documentation
=============================================================

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ============= Module Name
   ------------- Section (#.#)
   ~~~~~~~~~~~~~ Subsection (#.#.#)

The Link Measurement Protocol module implements the IEEE 802.11k Link Measurement feature for |ns3| WiFi simulations. This protocol enables WiFi stations and access points to measure and report link quality metrics, providing essential data for Radio Resource Management (RRM), roaming decisions, and network optimization.

Link measurement is a bidirectional protocol where either STAs or APs can request link quality measurements from their peers. The responding device measures the received signal quality and reports back metrics including RCPI (signal strength), RSNI (signal-to-noise ratio), transmit power, and link margin.

The source code for the module lives in the directory ``contrib/link-protocol-11k``.

Scope and Limitations
---------------------

**What the module can do:**

* Implement IEEE 802.11k Link Measurement Request/Report protocol
* Bidirectional link quality measurements (STA↔AP and AP↔STA)
* Collect RCPI (Received Channel Power Indicator) measurements
* Collect RSNI (Received Signal to Noise Indicator) measurements
* Report transmit power and link margin
* Provide trace sources for request/report monitoring
* Automatic response generation when link measurement request is received
* Dialog token management for request/response pairing
* Standard IEEE 802.11 management action frame format compliance

**What the module cannot do:**

* Does not perform the roaming decision (use auto-roaming-kv or bss_tm_11v modules)
* Does not discover neighboring APs (use beacon-neighbor-protocol-11k module)
* Does not implement other IEEE 802.11k features (beacon reports, neighbor reports)
* Does not provide automatic periodic measurement scheduling
* Does not implement measurement caching or historical tracking
* Does not support multi-link operation (802.11be)

Architecture
------------

IEEE 802.11k Link Measurement Protocol
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The module implements the Link Measurement feature defined in IEEE 802.11k-2008, which enables devices to measure and report link quality between WiFi stations. This is part of the Radio Resource Measurement (RRM) framework.

**Protocol Flow**:

1. **Request Initiation**

   * Requesting device (STA or AP) calls ``SendLinkMeasurementRequest()``
   * Generates Link Measurement Request action frame (Category 5, Action 2)
   * Includes dialog token, TX power used, max TX power capability

2. **Request Reception**

   * Receiving device captures request via management frame reception
   * Fires ``LinkMeasurementRequestReceived`` trace source
   * Automatically prepares response

3. **Measurement Collection**

   * Receiving device monitors PHY layer via ``MonitorSnifferRx`` callback
   * Captures signal strength (RSSI) and noise from incoming packet
   * Converts to IEEE 802.11k standard RCPI and RSNI values

4. **Report Generation**

   * Creates Link Measurement Report action frame (Category 5, Action 3)
   * Includes RCPI, RSNI, antenna IDs, TPC report (TX power)
   * Calculates link margin (difference between RX power and sensitivity)

5. **Report Transmission**

   * Sends report back to requesting device
   * Uses same dialog token for request/response pairing

6. **Report Reception**

   * Requesting device receives report
   * Fires ``LinkMeasurementReportReceived`` trace source
   * Application processes metrics for roaming/optimization decisions

Management Action Frame Format
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The module uses IEEE 802.11 management action frames with proper encapsulation:

**Link Measurement Request Frame**::

    | Category (5) | Action (2) | Dialog Token | TPC Report (TX Power, Max TX) |

**Link Measurement Report Frame**::

    | Category (5) | Action (3) | Dialog Token | TPC Report | RCPI | RSNI |
    | Antenna ID | Parent TSF |

**Key Fields**:

* **Category**: 5 (Radio Measurement)
* **Action**: 2 (Link Measurement Request), 3 (Link Measurement Report)
* **Dialog Token**: Unique identifier for pairing requests with responses
* **TPC Report**: Transmit Power Control report (TX power used, max capable)
* **RCPI**: Received Channel Power Indicator (0-220 scale, 0.5 dB steps)
* **RSNI**: Received Signal to Noise Indicator (0-255 scale, 0.5 dB steps)

Link Quality Metrics
~~~~~~~~~~~~~~~~~~~~~

**RCPI (Received Channel Power Indicator)**

* IEEE 802.11 standard metric for received signal strength
* Scale: 0-220 (values 221-255 reserved)
* Resolution: 0.5 dB
* Formula: ``RCPI = (RSSI_dBm + 110) × 2``
* Conversion: ``RSSI_dBm = (RCPI / 2) - 110``
* Example: RCPI = 140 → RSSI = -40 dBm

**RSNI (Received Signal to Noise Indicator)**

* IEEE 802.11 standard metric for signal-to-noise ratio
* Scale: 0-255
* Resolution: 0.5 dB
* Formula: ``RSNI = (SNR_dB + 10) × 2``
* Conversion: ``SNR_dB = (RSNI / 2) - 10``
* Example: RSNI = 60 → SNR = 20 dB

**Link Margin**

* Difference between received power and sensitivity threshold
* Indicates how much signal degradation can be tolerated
* Formula: ``Link Margin = RSSI - RX Sensitivity``
* Positive value indicates good link quality
* Approaching zero indicates link at risk

**Transmit Power Report**

* Current TX power used for transmission (dBm)
* Maximum TX power capability (dBm)
* Reported in TPC (Transmit Power Control) report element

Key Components
~~~~~~~~~~~~~~

* ``model/link-measurement-protocol.{h,cc}`` - Main protocol implementation
* ``model/link-measurement-request.{h,cc}`` - Request frame structure
* ``model/link-measurement-report.{h,cc}`` - Report frame structure
* ``examples/link-protocol-11k-example.cc`` - Usage demonstration

Usage
-----

Basic Setup (STA requests measurements from AP)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Simple scenario where STA periodically requests link measurements from the AP::

    #include "ns3/link-measurement-protocol.h"

    // Create WiFi devices (AP and STA)
    // ... (WiFi network setup code) ...

    // Create and install link measurement protocol on both devices
    Ptr<LinkMeasurementProtocol> apProtocol = CreateObject<LinkMeasurementProtocol>();
    apProtocol->Install(apDevice);

    Ptr<LinkMeasurementProtocol> staProtocol = CreateObject<LinkMeasurementProtocol>();
    staProtocol->Install(staDevice);

    // Get AP's MAC address for targeting
    Mac48Address apAddress = apDevice->GetMac()->GetAddress();

    // STA sends link measurement request to AP
    // Parameters: target MAC, TX power used, max TX power
    staProtocol->SendLinkMeasurementRequest(apAddress, 20, 30);

Advanced Setup with Periodic Measurements
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Automated periodic link measurements for continuous monitoring::

    #include "ns3/link-measurement-protocol.h"

    // Install protocol on devices
    Ptr<LinkMeasurementProtocol> staProtocol = CreateObject<LinkMeasurementProtocol>();
    staProtocol->Install(staDevice);

    Mac48Address apAddress = apDevice->GetMac()->GetAddress();

    // Schedule periodic measurements every 1 second
    void SendPeriodicMeasurement()
    {
        staProtocol->SendLinkMeasurementRequest(apAddress, 20, 30);

        // Reschedule for next measurement
        Simulator::Schedule(Seconds(1.0), &SendPeriodicMeasurement);
    }

    // Start periodic measurements at t=1s
    Simulator::Schedule(Seconds(1.0), &SendPeriodicMeasurement);

Handling Link Measurement Reports
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Processing received reports to monitor link quality::

    // Callback for when link measurement report is received
    void OnLinkMeasurementReport(Mac48Address from, LinkMeasurementReport report)
    {
        // Extract metrics
        double rcpiDbm = report.GetRcpiDbm();
        double rsniDb = report.GetRsniDb();
        uint8_t txPower = report.GetTransmitPowerDbm();
        uint8_t linkMargin = report.GetLinkMarginDb();

        std::cout << "Link Measurement Report from " << from << ":\n";
        std::cout << "  RCPI: " << rcpiDbm << " dBm\n";
        std::cout << "  RSNI: " << rsniDb << " dB\n";
        std::cout << "  TX Power: " << (int)txPower << " dBm\n";
        std::cout << "  Link Margin: " << (int)linkMargin << " dB\n";

        // Make roaming decision based on metrics
        if (rcpiDbm < -75.0)
        {
            std::cout << "  WARNING: Weak signal, consider roaming\n";
        }
    }

    // Connect callback to trace source
    staProtocol->TraceConnectWithoutContext("LinkMeasurementReportReceived",
                                             MakeCallback(&OnLinkMeasurementReport));

Bidirectional Measurements (AP measures STA)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

AP-initiated measurements for load balancing or client monitoring::

    // Install protocol on AP
    Ptr<LinkMeasurementProtocol> apProtocol = CreateObject<LinkMeasurementProtocol>();
    apProtocol->Install(apDevice);

    // Get STA's MAC address
    Mac48Address staAddress = staDevice->GetMac()->GetAddress();

    // AP sends link measurement request to STA
    apProtocol->SendLinkMeasurementRequest(staAddress, 20, 30);

    // AP processes report from STA
    void OnApReceivesReport(Mac48Address from, LinkMeasurementReport report)
    {
        double rcpiDbm = report.GetRcpiDbm();

        std::cout << "AP received report from STA " << from << "\n";
        std::cout << "  STA's received power: " << rcpiDbm << " dBm\n";

        // AP can use this for load balancing decisions
        if (rcpiDbm < -80.0)
        {
            std::cout << "  STA has weak uplink, may need BSS transition\n";
        }
    }

    apProtocol->TraceConnectWithoutContext("LinkMeasurementReportReceived",
                                            MakeCallback(&OnApReceivesReport));

Helpers
-------

Not applicable.

The module does not provide helper classes. Protocol instances are created directly using ``CreateObject<LinkMeasurementProtocol>()`` and installed via the ``Install(Ptr<WifiNetDevice>)`` method.

Helpers for specific use cases are provided by consuming modules (e.g., ``ApLinkMeasurementHelper`` in the auto-roaming-kv module).

Attributes
----------

LinkMeasurementProtocol Class
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``LinkMeasurementProtocol`` class does not expose ns-3 TypeId attributes. Configuration is done via method calls.

**Installation**:
  * ``void Install(Ptr<WifiNetDevice> device)`` - Install protocol on WiFi device

**Request Transmission**:
  * ``void SendLinkMeasurementRequest(Mac48Address to, int8_t txPower, int8_t maxTxPower)``

    * ``to`` - Target MAC address (AP or STA)
    * ``txPower`` - TX power used for this transmission (dBm)
    * ``maxTxPower`` - Maximum TX power capability (dBm)

LinkMeasurementRequest Structure
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Fields**:

* ``uint8_t dialogToken`` - Unique identifier for request/response pairing
* ``int8_t transmitPowerUsed`` - TX power used in 2's complement dBm
* ``int8_t maxTransmitPower`` - Max TX power capable in 2's complement dBm

**Methods**:

* ``uint8_t GetDialogToken() const``
* ``int8_t GetTransmitPowerUsedDbm() const`` - Returns TX power in dBm
* ``int8_t GetMaxTransmitPowerDbm() const`` - Returns max TX power in dBm

LinkMeasurementReport Structure
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Fields**:

* ``uint8_t dialogToken`` - Matches request's dialog token
* ``uint8_t transmitPowerDbm`` - Responder's current TX power (dBm)
* ``uint8_t linkMarginDb`` - Link margin in dB
* ``uint8_t rcpi`` - RCPI value (0-220 scale)
* ``uint8_t rsni`` - RSNI value (0-255 scale)
* ``uint8_t rxAntennaId`` - Receive antenna identifier
* ``uint8_t txAntennaId`` - Transmit antenna identifier
* ``uint64_t parentTsf`` - Timing Synchronization Function value

**Methods**:

* ``uint8_t GetDialogToken() const``
* ``double GetRcpiDbm() const`` - Returns RCPI as RSSI in dBm
* ``double GetRsniDb() const`` - Returns RSNI as SNR in dB
* ``uint8_t GetTransmitPowerDbm() const`` - Returns TX power in dBm
* ``uint8_t GetLinkMarginDb() const`` - Returns link margin in dB
* ``uint8_t GetRxAntennaId() const``
* ``uint8_t GetTxAntennaId() const``

Traces
------

LinkMeasurementProtocol Trace Sources
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**LinkMeasurementRequestReceived**

* Signature: ``void (Mac48Address from, LinkMeasurementRequest request)``
* Fired when: Device receives a Link Measurement Request
* Parameters:

  * ``from`` - MAC address of requesting device
  * ``request`` - LinkMeasurementRequest object containing request details

* Example::

    void OnRequestReceived(Mac48Address from, LinkMeasurementRequest request)
    {
        int8_t txPower = request.GetTransmitPowerUsedDbm();
        std::cout << "Request from " << from
                  << " using TX power " << (int)txPower << " dBm\n";
    }

    protocol->TraceConnectWithoutContext("LinkMeasurementRequestReceived",
                                          MakeCallback(&OnRequestReceived));

**LinkMeasurementReportReceived**

* Signature: ``void (Mac48Address from, LinkMeasurementReport report)``
* Fired when: Device receives a Link Measurement Report
* Parameters:

  * ``from`` - MAC address of reporting device
  * ``report`` - LinkMeasurementReport object containing measurements

* Example::

    void OnReportReceived(Mac48Address from, LinkMeasurementReport report)
    {
        double rcpi = report.GetRcpiDbm();
        double rsni = report.GetRsniDb();

        std::cout << "Report from " << from << ":\n";
        std::cout << "  RCPI: " << rcpi << " dBm\n";
        std::cout << "  RSNI: " << rsni << " dB\n";

        // Make roaming decision
        if (rcpi < -70.0)
        {
            std::cout << "  Initiating roaming procedure\n";
        }
    }

    protocol->TraceConnectWithoutContext("LinkMeasurementReportReceived",
                                          MakeCallback(&OnReportReceived));

Examples and Tests
------------------

link-protocol-11k-example.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Comprehensive link measurement demonstration with mobile STA.

* **Scenario**: 1 AP (stationary) + 1 STA (RandomWalk2d mobility)
* **Features**:

  * STA moves in 100m × 100m area around AP
  * Periodic link measurements every 100ms
  * RCPI, RSNI, link margin, TX power reporting
  * Distance tracking with signal quality correlation
  * Statistics collection (min/max/average RCPI, RSNI)

* **Purpose**: Demonstrates link measurement protocol operation and typical usage patterns
* **Run**: ``./ns3 run link-protocol-test``

**Command-line Parameters**::

    ./ns3 run "link-protocol-test --distance=20 --simTime=30"

* ``--distance`` - Initial distance between AP and STA in meters (default: 10.0)
* ``--simTime`` - Simulation duration in seconds (default: 60.0)
* ``--txPower`` - AP transmit power in dBm (default: 20.0)
* ``--requestInterval`` - Time between link measurement requests in seconds (default: 0.1)

**Expected Output**:

* Link measurement requests sent by STA
* Link measurement reports received from AP
* RCPI, RSNI, link margin, distance for each measurement
* Summary statistics at simulation end:

  * Total requests sent
  * Total reports received
  * Average RCPI, RSNI, link margin
  * Min/Max RCPI values
  * Final distance from AP

**Sample Output**::

    [0.5s] Request TX to 00:00:00:00:00:01 | Dialog: 0 | TX: 20 dBm | Max: 30 dBm
    [0.5s] Report RX from 00:00:00:00:00:01
           RCPI: -45.2 dBm | RSNI: 25.3 dB | Link Margin: 48 dB
           TX Power: 20 dBm | Distance: 10.0 m

    ============ Simulation Complete ============
    Total Requests Sent: 600
    Total Reports Received: 600
    Average RCPI: -52.3 dBm
    Average RSNI: 23.1 dB
    Average Link Margin: 41 dB
    Min RCPI: -68.2 dBm
    Max RCPI: -42.1 dBm
    Final Distance: 45.3 m

Test Suite
~~~~~~~~~~

**Status**: No automated test suite currently exists.

**Test File**: ``test/link-protocol-11k-test-suite.cc`` (not yet created)

**Needed Tests**:

* RCPI/RSNI conversion accuracy
* Dialog token uniqueness
* Request/Report pairing correctness
* Bidirectional measurement (STA↔AP and AP↔STA)
* Signal quality correlation with distance

**Run tests** (when created)::

    ./ns3 build
    ./test.py --suite=link-protocol-11k

Validation
----------

The module has been validated through:

1. **IEEE 802.11k Standard Compliance**

   * Management action frame format verified against IEEE 802.11k-2008 Section 7.4.6
   * RCPI/RSNI encoding verified (0.5 dB resolution, correct ranges)
   * TPC Report element format compliance
   * Dialog token management per standard

2. **Functional Validation**

   * Link measurement request/response exchange verified
   * Bidirectional measurements (STA→AP and AP→STA) tested
   * RCPI values correlate with distance and propagation loss
   * RSNI values reflect signal-to-noise ratio accurately
   * Automatic response generation confirmed

3. **Integration Testing**

   * Successfully integrated with auto-roaming-kv module for roaming decisions
   * Used in final-simulation for RRM measurements
   * Tested with SpectrumWifiPhy and propagation loss models
   * Verified with RandomWalk2d mobility model

4. **Signal Quality Validation**

   * RCPI measurements match expected path loss curves
   * RSNI values consistent with noise floor settings
   * Link margin calculations verified against RX sensitivity
   * Signal degradation with distance follows LogDistancePropagationLossModel

5. **Production Use**

   * Deployed in auto-roaming-kv module via ApLinkMeasurementHelper
   * Used for real-time roaming decision support
   * Tested in multi-AP scenarios
   * Handles concurrent measurements from multiple STAs

6. **Limitations**

   * No formal unit test suite (only example-based validation)
   * No validation against real WiFi hardware measurements
   * No stress testing with 50+ concurrent measurement sessions
   * No comparison with other 802.11k implementations

References
----------

[`1 <https://ieeexplore.ieee.org/document/4769102>`_] IEEE Standard for Information technology--Telecommunications and information exchange between systems--Local and metropolitan area networks--Specific requirements Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) specifications Amendment 1: Radio Resource Measurement of Wireless LANs, IEEE Std 802.11k-2008.

[`2 <https://ieeexplore.ieee.org/document/7786995>`_] IEEE Standard for Information technology--Telecommunications and information exchange between systems Local and metropolitan area networks--Specific requirements - Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) Specifications, IEEE Std 802.11-2020 (includes 802.11k).

[`3 <https://www.nsnam.org/docs/models/html/wifi.html>`_] |ns3| WiFi Module Documentation.
