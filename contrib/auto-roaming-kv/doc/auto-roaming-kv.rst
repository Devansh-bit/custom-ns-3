Auto-Roaming KV Module Documentation
=====================================

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ============= Module Name
   ------------- Section (#.#)
   ~~~~~~~~~~~~~ Subsection (#.#.#)

The Auto-Roaming KV module provides an IEEE 802.11k/v-based automatic roaming orchestration system for |ns3| WiFi simulations. It integrates Link Measurement (802.11k), Neighbor Reports (802.11k), Beacon Reports (802.11k), and BSS Transition Management (802.11v) protocols to enable intelligent, standards-based AP roaming triggered by link quality degradation.

The module orchestrates the roaming *decision and trigger process* using 802.11k/v protocols, while the actual channel switching and reassociation is handled by the underlying ``StaWifiMac`` implementation (which supports cross-band 2.4 GHz ↔ 5 GHz roaming).

The source code for the module lives in the directory ``contrib/auto-roaming-kv``.

Scope and Limitations
---------------------

**What the module can do:**

* Monitor link quality using IEEE 802.11k Link Measurement Protocol
* Trigger neighbor discovery when RSSI falls below configurable threshold
* Request and process Neighbor Reports (802.11k) from current AP
* Perform active beacon scanning on neighboring channels using dual-PHY sniffer
* Generate and analyze Beacon Reports (802.11k) with RSSI/SNR measurements
* Trigger BSS Transition Management (802.11v) requests to guide STA roaming
* Orchestrate complete roaming chain automatically based on link quality
* Support both YansWifiChannel and SpectrumChannel propagation models
* Work with cross-band roaming (2.4 GHz ↔ 5 GHz)

**What the module cannot do:**

* Does not implement the actual reassociation process (handled by ``StaWifiMac``)
* Does not provide load balancing algorithms (focuses on link quality only)
* Does not support roaming in infrastructure-less (ad-hoc) networks
* Does not implement 802.11r Fast BSS Transition (FT) protocol
* Does not support multi-link operation (802.11be)

Architecture
------------

Roaming Decision Flow
~~~~~~~~~~~~~~~~~~~~~

The module implements a standards-based roaming decision process:

1. **Link Monitoring (Continuous)**

   * STA sends Link Measurement Requests to current AP periodically
   * AP responds with Link Measurement Reports containing RSSI, SNR, link margin

2. **Threshold Detection (When RSSI < threshold)**

   * STA triggers Neighbor Report Request to current AP
   * AP responds with list of neighbor APs (BSSIDs + channels)

3. **Active Scanning (After neighbor report)**

   * AP sends Beacon Request to STA (lists neighbor APs to scan)
   * STA's Dual-PHY Sniffer collects beacons from neighbors on different channels
   * STA sends Beacon Report back to AP with RSSI/SNR per neighbor

4. **Roaming Decision (After beacon report)**

   * AP analyzes beacon reports and ranks candidate APs
   * AP sends BSS TM Request to STA with ranked candidate list
   * STA uses ``StaWifiMac::InitiateRoaming()`` to switch to best AP

5. **Association & Channel Switching**

   * ``StaWifiMac`` handles channel switch (supports cross-band 2.4 GHz ↔ 5 GHz)
   * Reassociation frames exchanged with new AP
   * Link measurement resumes with new AP

Key Components
~~~~~~~~~~~~~~

* ``helper/auto-roaming-kv-helper.{h,cc}`` - Main helper class for setup and orchestration
* ``model/auto-roaming-kv.{h,cc}`` - Core model (template/placeholder)
* ``examples/auto-roaming-kv-example.cc`` - Complete usage example with mobility

Dependencies
~~~~~~~~~~~~

**Required Modules:**

* ``contrib/link-protocol-11k/`` - IEEE 802.11k Link Measurement
* ``contrib/beacon-neighbor-protocol-11k/`` - IEEE 802.11k Neighbor & Beacon Report Protocol
* ``contrib/bss_tm_11v/`` - IEEE 802.11v BSS Transition Management
* ``contrib/dual-phy-sniffer/`` - Multi-channel beacon detection for beacon reports
* ``src/wifi/model/sta-wifi-mac.{h,cc}`` - Extended roaming APIs with cross-band support

Usage
-----

Basic Setup
~~~~~~~~~~~

The Auto-Roaming KV module requires careful setup of multiple protocol helpers. Here's a basic example:

::

    // 1. Create nodes and WiFi devices
    NodeContainer apNodes, staNodes;
    apNodes.Create(2);
    staNodes.Create(1);

    // 2. Setup WiFi with shared channel (Yans or Spectrum)
    SpectrumWifiPhyHelper spectrumPhy;
    Ptr<MultiModelSpectrumChannel> channel = CreateObject<MultiModelSpectrumChannel>();
    // ... configure channel ...
    spectrumPhy.SetChannel(channel);

    // Install WiFi on APs and STAs
    NetDeviceContainer apDevices = wifi.Install(spectrumPhy, wifiMac, apNodes);
    NetDeviceContainer staDevices = wifi.Install(spectrumPhy, wifiMac, staNodes);

    // 3. Create SHARED dual-PHY sniffer for beacon scanning
    DualPhySnifferHelper dualPhySniffer;
    dualPhySniffer.SetChannel(Ptr<SpectrumChannel>(channel));  // Channel-agnostic
    dualPhySniffer.SetScanningChannels({1, 6, 11});  // 2.4 GHz channels
    dualPhySniffer.SetHopInterval(MilliSeconds(100));

    // Install on APs (for neighbor discovery)
    dualPhySniffer.Install(apNodes.Get(0), 1, ap1Mac);
    dualPhySniffer.Install(apNodes.Get(1), 6, ap2Mac);
    dualPhySniffer.StartChannelHopping();

    // 4. Setup protocol helpers (SEPARATE instances per device)
    Ptr<NeighborProtocolHelper> neighborProtocolAp1 = CreateObject<NeighborProtocolHelper>();
    Ptr<NeighborProtocolHelper> neighborProtocolAp2 = CreateObject<NeighborProtocolHelper>();
    Ptr<NeighborProtocolHelper> neighborProtocolSta = CreateObject<NeighborProtocolHelper>();

    neighborProtocolAp1->SetDualPhySniffer(&dualPhySniffer);
    neighborProtocolAp2->SetDualPhySniffer(&dualPhySniffer);

    neighborProtocolAp1->InstallOnAp(apDevices.Get(0));
    neighborProtocolAp2->InstallOnAp(apDevices.Get(1));
    neighborProtocolSta->InstallOnSta(staDevices.Get(0));

    // 5. Setup beacon protocol helpers (SEPARATE instances)
    Ptr<BeaconProtocolHelper> beaconProtocolAp1 = CreateObject<BeaconProtocolHelper>();
    Ptr<BeaconProtocolHelper> beaconProtocolAp2 = CreateObject<BeaconProtocolHelper>();
    Ptr<BeaconProtocolHelper> beaconProtocolSta = CreateObject<BeaconProtocolHelper>();

    beaconProtocolAp1->SetDualPhySniffer(&dualPhySniffer);
    beaconProtocolAp2->SetDualPhySniffer(&dualPhySniffer);
    beaconProtocolSta->SetDualPhySniffer(&dualPhySniffer);

    beaconProtocolAp1->InstallOnAp(apDevices.Get(0));
    beaconProtocolAp2->InstallOnAp(apDevices.Get(1));
    beaconProtocolSta->InstallOnSta(staDevices.Get(0));

    // 6. Setup BSS TM helpers (SEPARATE instances)
    Ptr<BssTm11vHelper> bssTmHelperAp1 = CreateObject<BssTm11vHelper>();
    Ptr<BssTm11vHelper> bssTmHelperAp2 = CreateObject<BssTm11vHelper>();
    Ptr<BssTm11vHelper> bssTmHelperSta = CreateObject<BssTm11vHelper>();

    bssTmHelperAp1->InstallOnAp(apDevices.Get(0));
    bssTmHelperAp2->InstallOnAp(apDevices.Get(1));
    bssTmHelperSta->InstallOnSta(staDevices.Get(0));

    // 7. Setup AutoRoamingKvHelper (ONE per STA)
    AutoRoamingKvHelper helper;
    helper.SetMeasurementInterval(Seconds(1.0));
    helper.SetRssiThreshold(-60.0);  // dBm

    // Install on APs
    std::vector<Ptr<LinkMeasurementProtocol>> apProtocols = helper.InstallAp(apDevices);

    // Install on STA
    std::vector<Ptr<LinkMeasurementProtocol>> staProtocols =
        helper.InstallSta(staDevices, neighborProtocolSta, beaconProtocolSta, bssTmHelperSta);

    // 8. CRITICAL: Connect beacon report callbacks to ALL APs
    beaconProtocolAp1->m_beaconReportReceivedTrace.ConnectWithoutContext(
        MakeCallback(&AutoRoamingKvHelper::OnBeaconReportReceived, &helper));
    beaconProtocolAp2->m_beaconReportReceivedTrace.ConnectWithoutContext(
        MakeCallback(&AutoRoamingKvHelper::OnBeaconReportReceived, &helper));

Helpers
~~~~~~~

**AutoRoamingKvHelper**

Main orchestration helper that coordinates the roaming protocol chain:

::

    AutoRoamingKvHelper helper;

    // Configuration
    helper.SetMeasurementInterval(Seconds(1.0));  // Link measurement interval
    helper.SetRssiThreshold(-60.0);                // RSSI threshold in dBm

    // Installation
    std::vector<Ptr<LinkMeasurementProtocol>> apProtocols = helper.InstallAp(apDevices);
    std::vector<Ptr<LinkMeasurementProtocol>> staProtocols =
        helper.InstallSta(staDevices, neighborProtocol, beaconProtocol, bssTmHelper);

**Important Instance Management:**

* Create **ONE** ``AutoRoamingKvHelper`` per STA (stores STA-specific state)
* Create **SEPARATE** protocol helper instances per AP and STA
* Create **ONE SHARED** ``DualPhySnifferHelper`` for all devices
* Connect beacon report callbacks to **ALL AP** instances (not just initial AP)

Attributes
~~~~~~~~~~

**AutoRoamingKvHelper Attributes:**

* **MeasurementInterval** (Time) - Interval between link measurement requests (default: 1.0s)
* **RssiThreshold** (double) - RSSI threshold in dBm for triggering roaming (default: -65.0)
* **BeaconRequestDelay** (Time) - Delay before sending beacon request after neighbor report (default: 50ms)
* **BssTmRequestDelay** (Time) - Delay before sending BSS TM request after beacon report (default: 50ms)
* **EnableClientSteering** (bool) - Enable automatic BSS TM request generation (default: true)

**LinkMeasurementProtocol Attributes:**

* **TransmitPowerDbm** (int8_t) - Transmit power used for link measurement (default: 16 dBm)
* **LinkMarginDb** (uint8_t) - Link margin for measurement reports (default: 10 dB)

Traces
~~~~~~

**AutoRoamingKvHelper Trace Sources:**

* **NeighborRequestTriggered** - Fired when neighbor request is triggered due to low RSSI

  * Callback signature: ``void (Mac48Address staAddress, Mac48Address apAddress, double rssi)``

* **BeaconRequestSent** - Fired when beacon request is sent to STA

  * Callback signature: ``void (Mac48Address apAddress, Mac48Address staAddress, std::vector<Mac48Address> targetBssids)``

* **BssTmRequestSent** - Fired when BSS TM request is sent to STA

  * Callback signature: ``void (Mac48Address apAddress, Mac48Address staAddress, std::vector<Mac48Address> candidates)``

**LinkMeasurementProtocol Trace Sources:**

* **LinkMeasurementRequestReceived** - Fired when AP receives link measurement request

  * Callback signature: ``void (Mac48Address from, LinkMeasurementRequest request)``

* **LinkMeasurementReportReceived** - Fired when STA receives link measurement report

  * Callback signature: ``void (Mac48Address from, LinkMeasurementReport report)``

Examples and Tests
------------------

auto-roaming-kv-example.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~

Complete example demonstrating IEEE 802.11k/v roaming with mobility:

* **Scenario**: 2 APs on different channels (1 and 11 in 2.4 GHz), 1 mobile STA
* **Mobility**: STA moves from AP1 to AP2 at constant velocity
* **Protocols**: Full 802.11k/v chain (Link Measurement → Neighbor → Beacon → BSS TM)
* **Traffic**: UDP application running during roaming to verify traffic continuity
* **Channel Model**: Uses SpectrumWifiPhy with MultiModelSpectrumChannel
* **Verification**: Checks that link measurements are exchanged, roaming occurs, and traffic continues
* **Output**: Spectrum analyzer trace file (``spectrum-2.4ghz-band-0-0.tr``)

Running the example:

::

    ./ns3 run "auto-roaming-kv-example --simTime=50 --rssiThreshold=-60"

Expected output includes:

* Link measurement exchanges between STA and APs
* Neighbor report triggered when RSSI drops below threshold
* Beacon request/report exchange
* BSS TM request triggering roaming
* Successful association with new AP
* "VERIFICATION: SUCCESS" message at end

Validation
----------

The module has been validated through:

1. **Protocol Compliance Testing**

   * Link Measurement Request/Report frame formats verified against IEEE 802.11k
   * Neighbor Report Request/Response formats verified against IEEE 802.11k
   * Beacon Report Request/Response formats verified against IEEE 802.11k
   * BSS TM Request/Response formats verified against IEEE 802.11v

2. **Functional Testing**

   * Verified roaming trigger occurs when RSSI crosses threshold
   * Verified complete protocol chain executes in correct sequence
   * Verified traffic continuity during and after roaming
   * Verified cross-channel roaming (2.4 GHz channel 1 ↔ channel 11)
   * Verified dual-PHY sniffer correctly scans neighboring channels

3. **Integration Testing**

   * Tested with both YansWifiChannel and SpectrumChannel propagation models
   * Tested compatibility with TCP/UDP applications
   * Tested with CSMA bridge setup (Distribution System)
   * Verified compatibility with spectrum analyzer tools

4. **Stress Testing**

   * Multiple concurrent STAs roaming between APs
   * High-rate UDP traffic (100 packets/sec) during roaming
   * Rapid RSSI changes causing frequent roaming triggers

No formal analytical validation against real-world 802.11k/v implementations has been performed.

References
----------

[`1 <https://ieeexplore.ieee.org/document/7786995>`_] IEEE Standard for Information technology--Telecommunications and information exchange between systems Local and metropolitan area networks--Specific requirements - Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) Specifications, IEEE Std 802.11-2016.

[`2 <https://ieeexplore.ieee.org/document/4769102>`_] IEEE Standard for Information technology--Telecommunications and information exchange between systems--Local and metropolitan area networks--Specific requirements Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) specifications Amendment 1: Radio Resource Measurement of Wireless LANs, IEEE Std 802.11k-2008.

[`3 <https://ieeexplore.ieee.org/document/4769102>`_] IEEE Standard for Information technology--Telecommunications and information exchange between systems--Local and metropolitan area networks--Specific requirements Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) specifications Amendment 2: Wireless Network Management, IEEE Std 802.11v-2011.

[`4 <https://www.nsnam.org/docs/models/html/wifi.html>`_] |ns3| WiFi Module Documentation.
