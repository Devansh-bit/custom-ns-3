BSS Transition Management Module Documentation
==============================================

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ============= Module Name
   ------------- Section (#.#)
   ~~~~~~~~~~~~~ Subsection (#.#.#)

The BSS Transition Management (BSS TM) module provides an IEEE 802.11v-based network-controlled roaming system for |ns3| WiFi simulations. It implements the BSS Transition Management protocol (802.11v Section 7.4.8) to enable Access Points to suggest or direct stations to transition to better candidate APs based on network-wide optimization criteria rather than signal strength alone.

The module provides the *roaming decision and trigger* phase of WiFi roaming, allowing APs to guide STAs to optimal target APs using ranked candidate lists. This complements the *information gathering* provided by 802.11k protocols (neighbor/beacon reports) and enables network-controlled load balancing, QoS optimization, and coordinated handoffs.

The source code for the module lives in the directory ``contrib/bss_tm_11v``.

Scope and Limitations
---------------------

**What the module can do:**

* Implement IEEE 802.11v BSS Transition Management Request/Response protocol
* Send BSS TM requests from APs to STAs with ranked candidate lists
* Process BSS TM responses with accept/reject status
* Support network-controlled roaming (override signal-strength decisions)
* Integrate with rankListManager for candidate AP ranking algorithms
* Work standalone with hardcoded candidate lists
* Integrate with IEEE 802.11k beacon reports for dynamic candidate discovery
* Coordinate with lever-api for channel switching during transitions
* Support disassociation timers and validity intervals
* Handle multiple request modes (preferred list, disassociation imminent, etc.)

**What the module cannot do:**

* Does not perform the actual reassociation process (handled by ``StaWifiMac``)
* Does not discover neighboring APs (use beacon-neighbor-protocol-11k module)
* Does not measure link quality (use link-protocol-11k module)
* Does not implement 802.11r Fast BSS Transition (FT) protocol
* Does not support multi-link operation (802.11be)
* Does not implement BSS Termination or ESS Disassociation extensions
* STA cannot initiate BSS TM requests (AP-to-STA direction only in current implementation)

Architecture
------------

BSS TM Request/Response Flow
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The module implements the IEEE 802.11v BSS TM protocol exchange:

1. **Trigger Decision (AP)**

   * AP determines STA should roam based on network criteria:

     * Link quality degradation (from Link Measurement reports)
     * Load balancing requirements
     * Channel utilization optimization
     * QoS policy enforcement

2. **Candidate Ranking (AP)**

   * AP ranks potential target APs using rankListManager:

     * Signal strength (from Beacon Reports)
     * Channel load
     * Backhaul capacity
     * QoS capabilities
     * Custom ranking algorithms

3. **BSS TM Request (AP → STA)**

   * AP sends BSS Transition Management Request frame:

     * Ranked candidate list (BSSIDs in priority order)
     * Request mode flags (preferred list, disassociation imminent, etc.)
     * Disassociation timer (optional deadline)
     * Validity interval (how long candidate list is valid)

4. **Roaming Decision (STA)**

   * STA evaluates BSS TM request:

     * Checks candidate list availability
     * May perform additional scanning
     * Selects target BSSID from candidate list
     * STA retains final decision authority

5. **BSS TM Response (STA → AP)**

   * STA sends BSS Transition Management Response frame:

     * Status code (accept/reject)
     * Target BSSID (if accepting)
     * Reject reason (if rejecting)

6. **Reassociation (STA)**

   * STA initiates reassociation with target AP (via ``StaWifiMac::InitiateRoaming()``)
   * Channel switching coordinated via lever-api
   * Traffic continues on new AP

Key Components
~~~~~~~~~~~~~~

* ``helper/bss_tm_11v-helper.{h,cc}`` - Main helper class for BSS TM protocol setup
* ``model/bss-tm-11v.{h,cc}`` - BSS TM frame handling and protocol state machine
* ``model/kv-interface.h`` - rankListManager interface for candidate ranking
* ``examples/bss_tm_11v-example.cc`` - Basic BSS TM with mobility
* ``examples/bss-tm-dummy-example.cc`` - Distribution System with seamless roaming
* ``examples/bss-tm-force-low-rssi-roaming.cc`` - Network-controlled roaming override

Dependencies
~~~~~~~~~~~~

**Required Modules:**

* ``src/wifi/`` - WiFi module with StaWifiMac roaming APIs

**Optional Modules (for advanced integration):**

* ``contrib/beacon-neighbor-protocol-11k/`` - For dynamic candidate discovery via beacon reports
* ``contrib/link-protocol-11k/`` - For link quality monitoring
* ``contrib/lever-api/`` - For coordinated channel switching

Usage
-----

Basic Setup (Hardcoded Candidate Lists)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For simple scenarios where candidate APs are known in advance:

::

    #include "ns3/bss_tm_11v-helper.h"

    // Create BSS TM helper
    Ptr<BssTm11vHelper> bssTmHelper = CreateObject<BssTm11vHelper>();

    // Install on AP and STA
    bssTmHelper->InstallOnAp(apDevice);
    bssTmHelper->InstallOnSta(staDevice);

    // Connect trace sources
    bssTmHelper->TraceConnectWithoutContext(
        "BssTmRequestSent",
        MakeCallback(&OnBssTmRequestSent));

    bssTmHelper->TraceConnectWithoutContext(
        "BssTmResponseReceived",
        MakeCallback(&OnBssTmResponseReceived));

    // Manually trigger BSS TM request with ranked candidates
    std::vector<Mac48Address> candidates;
    candidates.push_back(Mac48Address("00:00:00:00:00:02"));  // Best choice
    candidates.push_back(Mac48Address("00:00:00:00:00:03"));  // Fallback

    Simulator::Schedule(Seconds(5.0),
                        &BssTm11vHelper::SendBssTmRequest,
                        PeekPointer(bssTmHelper),
                        apDevice,
                        staAddress,
                        candidates);

Advanced Setup (Integration with 802.11k)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For scenarios using dynamic neighbor discovery and beacon measurements:

::

    #include "ns3/bss_tm_11v-helper.h"
    #include "ns3/beacon-protocol-11k-helper.h"
    #include "ns3/neighbor-protocol-11k-helper.h"

    // Setup 802.11k neighbor and beacon protocols
    Ptr<NeighborProtocolHelper> neighborHelper = CreateObject<NeighborProtocolHelper>();
    Ptr<BeaconProtocolHelper> beaconHelper = CreateObject<BeaconProtocolHelper>();

    neighborHelper->InstallOnAp(apDevice);
    neighborHelper->InstallOnSta(staDevice);
    beaconHelper->InstallOnAp(apDevice);
    beaconHelper->InstallOnSta(staDevice);

    // Setup BSS TM
    Ptr<BssTm11vHelper> bssTmHelper = CreateObject<BssTm11vHelper>();
    bssTmHelper->InstallOnAp(apDevice);
    bssTmHelper->InstallOnSta(staDevice);

    // Chain protocols: Beacon Report → Ranking → BSS TM Request
    void OnBeaconReportReceived(Mac48Address apAddr,
                                 Mac48Address staAddr,
                                 std::vector<BeaconReportData> reports)
    {
        // Rank candidates by RSSI (convert RCPI to dBm)
        std::vector<std::pair<Mac48Address, double>> ranked;

        for (const auto& report : reports) {
            double rssi = RcpiToRssi(report.rcpi);
            ranked.push_back({report.bssid, rssi});
        }

        // Sort by RSSI descending
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto& a, const auto& b) {
                      return a.second > b.second;
                  });

        // Extract ranked BSSID list
        std::vector<Mac48Address> candidates;
        for (const auto& entry : ranked) {
            candidates.push_back(entry.first);
        }

        // Send BSS TM request with ranked candidates
        Simulator::Schedule(MilliSeconds(50),
                            &BssTm11vHelper::SendBssTmRequest,
                            PeekPointer(bssTmHelper),
                            apDevice,
                            staAddr,
                            candidates);
    }

    beaconHelper->TraceConnectWithoutContext(
        "BeaconReportReceived",
        MakeCallback(&OnBeaconReportReceived));

Trace Callback Handlers
~~~~~~~~~~~~~~~~~~~~~~~~

::

    void OnBssTmRequestSent(Mac48Address apAddr,
                            Mac48Address staAddr,
                            std::vector<Mac48Address> candidates)
    {
        std::cout << "BSS TM Request: " << apAddr << " → " << staAddr << "\n";
        std::cout << "Candidate APs (" << candidates.size() << "):\n";

        for (size_t i = 0; i < candidates.size(); i++) {
            std::cout << "  " << (i + 1) << ". " << candidates[i]
                      << " (priority " << (i + 1) << ")\n";
        }
    }

    void OnBssTmResponseReceived(Mac48Address staAddr,
                                  Mac48Address apAddr,
                                  uint8_t statusCode,
                                  Mac48Address targetBssid)
    {
        std::cout << "BSS TM Response: " << staAddr << " → " << apAddr << "\n";

        const char* statusText;
        switch (statusCode) {
            case 0: statusText = "ACCEPT"; break;
            case 1: statusText = "REJECT - Unspecified"; break;
            case 2: statusText = "REJECT - Insufficient beacon reports"; break;
            case 6: statusText = "REJECT - No suitable candidate"; break;
            case 7: statusText = "REJECT - No candidates in list"; break;
            default: statusText = "UNKNOWN"; break;
        }

        std::cout << "  Status: " << statusText << " (" << (int)statusCode << ")\n";

        if (statusCode == 0) {
            std::cout << "  Target BSSID: " << targetBssid << "\n";
        }
    }

Helpers
~~~~~~~

**BssTm11vHelper**

Main helper class for BSS Transition Management protocol operations:

::

    Ptr<BssTm11vHelper> bssTmHelper = CreateObject<BssTm11vHelper>();

    // Install on AP (can send BSS TM requests)
    bssTmHelper->InstallOnAp(apDevice);

    // Install on STA (can respond to BSS TM requests)
    bssTmHelper->InstallOnSta(staDevice);

    // Manually send BSS TM request
    bssTmHelper->SendBssTmRequest(apDevice, staAddress, candidateList);

    // Connect trace sources
    bssTmHelper->TraceConnectWithoutContext("BssTmRequestSent", callback1);
    bssTmHelper->TraceConnectWithoutContext("BssTmResponseReceived", callback2);

**Important Notes:**

* Create **SEPARATE** ``BssTm11vHelper`` instances per device (avoid pointer overwrites)
* BSS TM requests are AP-initiated (AP → STA direction)
* STA automatically responds to valid BSS TM requests
* Helper inherits from ``rankListManager`` for extensible candidate ranking

Multi-Device Setup (Multiple APs/STAs)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When using multiple APs and STAs, you **MUST** create separate helper instances for each device.
Reusing a single helper causes internal sniffer pointer overwrites, leading to segmentation faults.

::

    // Store helpers in vectors
    std::vector<Ptr<BssTm11vHelper>> apHelpers;
    std::vector<Ptr<BssTm11vHelper>> staHelpers;

    // Install on all APs - one helper per AP
    for (uint32_t i = 0; i < numAps; i++)
    {
        Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(apWifiDevices.Get(i));
        Ptr<BssTm11vHelper> apHelper = CreateObject<BssTm11vHelper>();
        apHelper->SetBeaconSniffer(&dualPhySniffer);  // Optional: for beacon cache ranking
        apHelper->SetCooldown(Minutes(5));            // Rate limiting
        apHelper->InstallOnAp(apDevice);
        apHelpers.push_back(apHelper);
    }

    // Install on all STAs - one helper per STA
    for (uint32_t i = 0; i < numStas; i++)
    {
        Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staWifiDevices.Get(i));
        Ptr<BssTm11vHelper> staHelper = CreateObject<BssTm11vHelper>();
        staHelper->SetBeaconSniffer(&dualPhySniffer);
        staHelper->SetCooldown(Minutes(5));
        staHelper->InstallOnSta(staDevice);
        staHelpers.push_back(staHelper);
    }

    // To send BSS TM request, use the corresponding AP's helper:
    // apHelpers[apIndex]->sendRankedCandidates(apDevice, apBssid, staAddress, {});

Attributes
~~~~~~~~~~

**BssTmParameters Structure:**

The BSS TM request is configured via ``BssTmParameters`` structure:

* **requestMode** (uint8_t) - Request mode bit field:

  * Bit 0: Preferred Candidate List Included
  * Bit 1: Abridged
  * Bit 2: Disassociation Imminent
  * Bit 3: BSS Termination Included
  * Bit 4: ESS Disassociation Imminent

* **disassociationTimer** (uint16_t) - Time in TUs until disassociation (0 = not specified)
* **validityInterval** (uint8_t) - Candidate list validity in TUs (0 = indefinite)
* **targetBssid** (Mac48Address) - Preferred target BSSID (optional hint)
* **candidateList** (std::vector<Mac48Address>) - Ranked list of candidate APs

**Response Status Codes:**

* **0** - Accept (STA will transition to target BSSID)
* **1** - Reject - Unspecified reason
* **2** - Reject - Insufficient beacon reports available
* **6** - Reject - Candidate list provided but no suitable candidate
* **7** - Reject - No suitable candidates in list or scan results

Traces
~~~~~~

**BssTm11vHelper Trace Sources:**

* **BssTmRequestSent** - Fired when AP sends BSS TM request to STA

  * Callback signature: ``void (Mac48Address apAddr, Mac48Address staAddr, std::vector<Mac48Address> candidates)``
  * Parameters:

    * ``apAddr`` - MAC address of AP sending the request
    * ``staAddr`` - MAC address of target STA
    * ``candidates`` - Ranked list of candidate AP BSSIDs (priority order)

* **BssTmResponseReceived** - Fired when AP receives BSS TM response from STA

  * Callback signature: ``void (Mac48Address staAddr, Mac48Address apAddr, uint8_t statusCode, Mac48Address targetBssid)``
  * Parameters:

    * ``staAddr`` - MAC address of responding STA
    * ``apAddr`` - MAC address of requesting AP
    * ``statusCode`` - Response status (0=accept, 1-7=reject variants)
    * ``targetBssid`` - Selected target BSSID (if statusCode == 0)

Examples and Tests
------------------

bss_tm_11v-example.cc
~~~~~~~~~~~~~~~~~~~~~

Basic BSS Transition Management example with mobility:

* **Scenario**: 1 AP, 1 STA with waypoint mobility
* **Mobility**: STA moves away from AP at constant velocity
* **Trigger**: Hardcoded BSS TM request at specific simulation time
* **Candidates**: Hardcoded candidate list (dummy beacon reports)
* **Purpose**: Demonstrates basic BSS TM request/response exchange
* **Verification**: Checks BSS TM request sent and response received

Running the example:

::

    ./ns3 run bss_tm_11v-example

Expected output includes:

* BSS TM Request sent with ranked candidate list
* BSS TM Response from STA (accept with target BSSID)
* Trace callbacks showing protocol exchange

bss-tm-dummy-example.cc
~~~~~~~~~~~~~~~~~~~~~~~

Advanced BSS TM example with Distribution System (bridged infrastructure):

* **Scenario**: 3 APs connected via 1Gbps CSMA backbone
* **Topology**: Distribution System with bridge devices connecting WiFi ↔ Wired segments
* **Traffic**: UDP data transmission from backend server to STA during roaming
* **Purpose**: Demonstrates seamless handoff with traffic continuity
* **Verification**: Checks packet delivery before, during, and after BSS transition

Architecture:

::

    [Server] <--CSMA--> [Bridge-AP0] <--WiFi--> [STA]
                             |
                        CSMA Backbone
                        (1Gbps Switch)
                             |
               [Bridge-AP1]   [Bridge-AP2]

Running the example:

::

    ./ns3 run bss-tm-dummy-example

Expected output includes:

* Bridge/CSMA setup messages
* UDP traffic transmission
* BSS TM request triggering roaming
* Traffic continuity during handoff
* Successful packet delivery to STA on new AP

bss-tm-force-low-rssi-roaming.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Network-controlled roaming example (overrides signal strength):

* **Scenario**: 3 APs on same channel (6, 2.4GHz) in linear placement
* **Unique Aspect**: Forces STA to roam to AP with WORSE RSSI
* **Purpose**: Demonstrates network control vs. autonomous roaming
* **Use Case**: Load balancing scenarios where signal strength shouldn't dictate roaming
* **Verification**: Confirms STA roamed to non-strongest AP per BSS TM directive

Running the example:

::

    ./ns3 run "bss-tm-force-low-rssi-roaming --nAps=3 --simTime=20"

Command-line parameters:

* ``--nAps`` - Number of access points (default: 3)
* ``--nStas`` - Number of stations (default: 1)
* ``--simTime`` - Simulation duration in seconds (default: 20.0)

Expected output includes:

* RSSI measurements showing AP signal strengths
* BSS TM request forcing roaming to weaker AP
* STA accepting BSS TM request despite better signal elsewhere
* Successful reassociation to network-selected AP

Validation
----------

The module has been validated through:

1. **Protocol Compliance Testing**

   * BSS Transition Management Request/Response frame formats verified against IEEE 802.11v-2011 Section 7.4.8
   * Request mode flags and parameter encoding verified
   * Status code handling verified against standard

2. **Functional Testing**

   * Verified BSS TM request/response exchange works correctly
   * Verified candidate list ranking and priority ordering
   * Verified STA accepts valid BSS TM requests and transitions to target AP
   * Verified STA can reject BSS TM requests with appropriate status codes
   * Verified disassociation timer handling

3. **Integration Testing**

   * Tested standalone operation with hardcoded candidate lists
   * Tested integration with beacon-neighbor-protocol-11k for dynamic discovery
   * Tested with Distribution System (bridges + CSMA backbone)
   * Tested network-controlled roaming overriding signal-strength decisions
   * Verified traffic continuity during BSS transitions

4. **Stress Testing**

   * Multiple STAs roaming simultaneously
   * Rapid BSS TM requests (frequent candidate list updates)
   * Large candidate lists (5+ APs)

No formal analytical validation against real-world 802.11v implementations has been performed.

References
----------

[`1 <https://ieeexplore.ieee.org/document/7786995>`_] IEEE Standard for Information technology--Telecommunications and information exchange between systems Local and metropolitan area networks--Specific requirements - Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) Specifications, IEEE Std 802.11-2016.

[`2 <https://ieeexplore.ieee.org/document/5594698>`_] IEEE Standard for Information technology--Telecommunications and information exchange between systems--Local and metropolitan area networks--Specific requirements Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) specifications Amendment 3: Wireless Network Management, IEEE Std 802.11v-2011.

[`3 <https://www.nsnam.org/docs/models/html/wifi.html>`_] |ns3| WiFi Module Documentation.
