STA Channel Hopping Module Documentation
=========================================

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ------------- Chapter
   ************* Section (#.#)
   ============= Subsection (#.#.#)
   ############# Paragraph (no number)

This document describes the STA Channel Hopping module for |ns3|.

The STA Channel Hopping module provides automatic multi-channel roaming capabilities for WiFi STAs in |ns3| simulations. It monitors STA disassociation events and automatically reconnects to the best available AP across multiple channels using signal quality metrics (SNR).

The source code for the STA Channel Hopping module lives in the directory ``contrib/sta-channel-hopping``.

Overview
********

The STA Channel Hopping module models standard WiFi client behavior. When a STA becomes disassociated from its current AP, the module automatically scans available channels, evaluates candidate APs based on signal quality, and reconnects to the best option.

This module focuses on client-side behavior and operates independently of network-controlled roaming mechanisms.

**Key Features:**

- **Event-Driven Roaming**: Automatic roaming triggered by disassociation events
- **Multi-Channel Scanning**: Scans multiple WiFi channels (2.4 GHz and 5 GHz) to find available APs
- **SNR-Based Selection**: Selects target AP based on Signal-to-Noise Ratio (SNR) measurements
- **Configurable Scanning Delay**: Allows time for multi-channel beacon discovery before roaming
- **Minimum SNR Threshold**: Filters APs below acceptable signal quality
- **Cross-Band Roaming**: Supports roaming between 2.4 GHz and 5 GHz bands
- **Trace Sources**: Provides detailed roaming event notifications

**Use Cases:**

- Modeling client-side roaming behavior in WiFi networks
- Simulating STA reconnection after disassociation events
- Testing multi-channel AP discovery and selection
- Evaluating roaming performance in mobile scenarios

Scope and Limitations
*********************

What the Module Can Do
=======================

The STA Channel Hopping module models client-side roaming:

- **Automatic Reconnection**: Detect disassociation events and trigger roaming to a new AP
- **Multi-Channel Discovery**: Scan multiple WiFi channels to find available APs
- **Signal Quality Selection**: Choose APs based on SNR measurements from beacon frames
- **Cross-Band Support**: Roam between 2.4 GHz and 5 GHz bands
- **Configurable Parameters**: Set minimum SNR thresholds and scanning delays
- **Event Tracing**: Monitor roaming events and AP selection decisions

Architecture and Design
***********************

The STA Channel Hopping module consists of two main components:

1. **StaChannelHoppingManager** (``model/sta-channel-hopping-manager.{h,cc}``): Core logic for roaming decisions
2. **StaChannelHoppingHelper** (``helper/sta-channel-hopping-helper.{h,cc}``): Helper class for installation and configuration

Component Architecture
======================

::

    StaChannelHoppingHelper
        │
        ├─── Creates and configures StaChannelHoppingManager
        ├─── Sets attributes (ScanningDelay, MinimumSnr, Enabled)
        └─── Installs on WifiNetDevice

    StaChannelHoppingManager
        │
        ├─── Monitors STA association state (DeAssoc/Assoc traces)
        ├─── Queries DualPhySnifferHelper for beacon cache
        ├─── Selects best AP based on SNR
        └─── Initiates roaming via StaWifiMac API

    DualPhySnifferHelper (External Dependency)
        │
        ├─── Provides multi-channel beacon monitoring
        ├─── Maintains beacon cache with SNR measurements
        └─── Scans multiple channels via channel hopping

Dependencies
============

**Required Module**: DualPhySnifferHelper (``contrib/dual-phy-sniffer/``)

The StaChannelHoppingManager relies on DualPhySnifferHelper to:

- Track beacons from APs across multiple channels
- Build a cache of available APs with signal quality metrics (SNR, RSSI)
- Provide channel information for roaming decisions

**WiFi PHY API Compatibility**: This module is **API-agnostic** and works with both:

- **SpectrumWifiPhyHelper**: More detailed frequency modeling (used in examples)
- **YansWifiPhyHelper**: Simpler propagation model (used in production simulations)

The module works transparently with either API through the DualPhySnifferHelper abstraction layer.

Design Patterns
===============

Event-Driven Architecture
#########################

The module uses an event-driven design:

1. **Disassociation Event** → Clear beacon cache, schedule scanning delay
2. **Scanning Period** → DualPhySniffer collects beacons from multiple channels
3. **Roaming Trigger** → Query beacon cache, select best AP, initiate roaming
4. **Association Event** → Cancel pending roaming, update state

State Management
################

The manager tracks:

- Current association state (associated/disassociated)
- Current BSSID (if associated)
- Pending roaming timer
- DualPhySniffer instance for beacon queries

How It Works
************

The roaming process follows these steps:

1. Disassociation Detection
===========================

The manager connects to the STA's ``DeAssoc`` and ``Assoc`` trace sources:

::

    // Connect to association state changes
    m_device->GetMac()->TraceConnectWithoutContext(
        "DeAssoc",
        MakeCallback(&StaChannelHoppingManager::OnDeAssoc, this));

**On Disassociation:**

1. Log the disassociation event
2. Clear DualPhySniffer's beacon cache to allow fresh AP discovery
3. Schedule roaming attempt after ``ScanningDelay`` period
4. During the delay, DualPhySniffer continuously scans channels and caches beacons

**On Association:**

1. Cancel any pending roaming attempts
2. Update internal BSSID tracking
3. Log successful association

2. Beacon Discovery Phase
=========================

During the scanning delay:

- DualPhySnifferHelper continuously hops between configured channels
- Beacon frames from all APs are captured and cached
- Each beacon is tagged with:
  - BSSID (AP MAC address)
  - Channel number
  - SSID
  - SNR (Signal-to-Noise Ratio)
  - Timestamp

3. AP Selection
===============

After the scanning delay expires, the manager queries the beacon cache:

::

    std::vector<BeaconInfo> beacons =
        m_dualPhySniffer->GetBeaconsReceivedBy(m_dualPhyOperatingMac);

**Selection Criteria:**

1. **Exclude Current AP**: If still associated, exclude current BSSID
2. **SNR Filtering**: Exclude APs with SNR below ``MinimumSnr`` threshold
3. **Best Signal**: Select AP with highest SNR among remaining candidates

**Selection Logic:**

::

    BeaconInfo* bestAp = nullptr;
    double bestSnr = m_minimumSnr;

    for (auto& beacon : beacons)
    {
        // Skip current AP
        if (beacon.bssid == currentBssid)
            continue;

        // Find highest SNR
        if (beacon.snr > bestSnr)
        {
            bestSnr = beacon.snr;
            bestAp = &beacon;
        }
    }

4. Roaming Execution
====================

The manager uses StaWifiMac's roaming APIs with explicit band specification:

::

    // Determine band from channel number
    WifiPhyBand targetBand;
    if (targetChannel >= 1 && targetChannel <= 14)
    {
        targetBand = WIFI_PHY_BAND_2_4GHZ;  // 2.4 GHz
    }
    else
    {
        targetBand = WIFI_PHY_BAND_5GHZ;    // 5 GHz
    }

    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_device->GetMac());

    if (staMac->IsAssociated())
    {
        // Seamless reassociation (roaming between APs)
        staMac->InitiateRoaming(targetBssid, targetChannel, targetBand);
    }
    else
    {
        // Initial association (from disassociated state)
        staMac->AssociateToAp(targetBssid, targetChannel, targetBand);
    }

**Band and Width Specification:**

- **2.4 GHz (channels 1-14)**: Explicit band specification required
- **5 GHz (other channels)**: Explicit band specification required
- **Channel width**: Automatically determined by |ns3| from channel number

Usage
*****

This section demonstrates how to use the STA Channel Hopping module in simulations.

Helpers
=======

StaChannelHoppingHelper
#######################

The helper class simplifies installation and configuration.

**Basic Setup:**

::

    #include "ns3/sta-channel-hopping-helper.h"
    #include "ns3/dual-phy-sniffer-helper.h"

    // 1. Setup DualPhySniffer first
    DualPhySnifferHelper* dualPhySniffer = new DualPhySnifferHelper();
    dualPhySniffer->SetChannel(spectrumChannel);
    dualPhySniffer->SetScanningChannels({1, 6, 11});  // 2.4 GHz channels
    dualPhySniffer->SetHopInterval(Seconds(1.0));
    dualPhySniffer->SetSsid(ssid);
    dualPhySniffer->Install(staNode, operatingChannel, staMacAddress);
    dualPhySniffer->StartChannelHopping();

    // 2. Setup StaChannelHoppingHelper
    StaChannelHoppingHelper channelHoppingHelper;
    channelHoppingHelper.SetDualPhySniffer(dualPhySniffer);
    channelHoppingHelper.SetAttribute("ScanningDelay", TimeValue(Seconds(5.0)));
    channelHoppingHelper.SetAttribute("MinimumSnr", DoubleValue(10.0));
    channelHoppingHelper.SetAttribute("Enabled", BooleanValue(true));

    // 3. Install on STA WiFi device
    Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(0));
    Ptr<StaChannelHoppingManager> manager = channelHoppingHelper.Install(staDevice);

**Multi-Channel Setup (2.4 GHz + 5 GHz):**

::

    // Scan both 2.4 GHz and 5 GHz bands
    dualPhySniffer->SetScanningChannels({1, 6, 11, 36, 40, 44, 48});
    dualPhySniffer->SetHopInterval(Seconds(0.5));  // Faster hopping for more channels

**SpectrumWifi Example:**

::

    // Create spectrum channel (shared by all devices)
    Ptr<MultiModelSpectrumChannel> spectrumChannel =
        CreateObject<MultiModelSpectrumChannel>();
    Ptr<FriisPropagationLossModel> lossModel =
        CreateObject<FriisPropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(lossModel);
    Ptr<ConstantSpeedPropagationDelayModel> delayModel =
        CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);

    // Configure WiFi PHY with Spectrum
    SpectrumWifiPhyHelper wifiPhy;
    wifiPhy.SetChannel(spectrumChannel);

    // Setup DualPhySniffer with SpectrumChannel
    dualPhySniffer->SetChannel(spectrumChannel);

**YansWifi Example:**

::

    // Create Yans WiFi channel
    YansWifiChannelHelper channelHelper;
    channelHelper.AddPropagationLoss("ns3::FriisPropagationLossModel");
    channelHelper.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    Ptr<YansWifiChannel> channel = channelHelper.Create();

    // Configure WiFi PHY with Yans
    YansWifiPhyHelper wifiPhy;
    wifiPhy.SetChannel(channel);

    // Setup DualPhySniffer with YansChannel
    dualPhySniffer->SetChannel(channel);

**Connecting Trace Callbacks:**

::

    // Callback function
    void RoamingTriggeredCallback(Time time, Mac48Address staAddr,
                                   Mac48Address oldBssid, Mac48Address newBssid,
                                   double snr)
    {
        std::cout << time.As(Time::S) << " STA roaming from "
                  << oldBssid << " to " << newBssid
                  << " (SNR: " << snr << " dB)" << std::endl;
    }

    // Connect trace
    manager->TraceConnectWithoutContext(
        "RoamingTriggered",
        MakeCallback(&RoamingTriggeredCallback));

Attributes
==========

StaChannelHoppingManager provides the following attributes:

**ScanningDelay** (Time, default: 5 seconds)
  Time to wait after disassociation before attempting to reconnect. During this period, the DualPhySniffer scans channels to discover available APs.

  ::

      channelHoppingHelper.SetAttribute("ScanningDelay", TimeValue(Seconds(3.0)));

**MinimumSnr** (double, default: 0.0 dB)
  Minimum SNR threshold for AP selection. APs with SNR below this value are excluded from consideration.

  ::

      channelHoppingHelper.SetAttribute("MinimumSnr", DoubleValue(15.0));

**Enabled** (bool, default: true)
  Enable or disable automatic roaming. When disabled, the manager does not respond to disassociation events.

  ::

      channelHoppingHelper.SetAttribute("Enabled", BooleanValue(false));

Traces
======

StaChannelHoppingManager provides the following trace sources:

**RoamingTriggered**
  Fired when the manager initiates roaming to a new AP.

  **Signature:**

  ::

      void (*RoamingTriggeredCallback)(Time time,
                                        Mac48Address staAddress,
                                        Mac48Address oldBssid,
                                        Mac48Address newBssid,
                                        double targetSnr);

  **Parameters:**

  - ``time``: Simulation time when roaming was triggered
  - ``staAddress``: MAC address of the roaming STA
  - ``oldBssid``: BSSID of the previous AP (or ``Mac48Address::GetBroadcast()`` if none)
  - ``newBssid``: BSSID of the target AP
  - ``targetSnr``: SNR of the target AP in dB

  **Example:**

  ::

      manager->TraceConnectWithoutContext(
          "RoamingTriggered",
          MakeCallback(&RoamingTriggeredCallback));

Examples and Tests
******************

sta-channel-hopping-example.cc
===============================

Location: ``contrib/sta-channel-hopping/examples/sta-channel-hopping-example.cc``

**Scenario:**

- 5 APs on mixed 2.4 GHz and 5 GHz bands with different channel widths
- 1 STA with DualPhySniffer tracking all channels
- STA initially associates with AP1 on channel 1 (2.4 GHz, 20 MHz)
- At t=15s: Forced disassociation triggers roaming
- STA scans all channels (2.4 GHz + 5 GHz) and selects best AP
- At t=30s: AP2 switches bands (ch36 5GHz → ch11 2.4GHz)

**What it demonstrates:**

- Multi-band (2.4 GHz + 5 GHz) roaming
- Multi-width channel support (20/40/80/160 MHz)
- Automatic cross-band roaming
- Dynamic channel change tracking
- SpectrumWifiPhyHelper usage

**Running the Example:**

.. sourcecode:: bash

    ./ns3 run sta-channel-hopping-example

**Command-line Options:**

::

    --simTime=50.0           # Total simulation time (seconds)
    --scanningDelay=5.0      # Delay before reconnection (seconds)
    --minimumSnr=0.0         # Minimum SNR threshold (dB)
    --verbose=false          # Enable verbose logging

multi-sta-channel-hopping-example.cc
=====================================

Location: ``contrib/sta-channel-hopping/examples/multi-sta-channel-hopping-example.cc``

**Scenario:**

- 3 APs: AP1 (ch1), AP2 (ch6), AP3 (ch11)
- 2 STAs with DualPhySniffer tracking channels 1, 6, 11
- Both STAs initially associate with AP1
- At t=10s: AP1 changes to ch6, STA1 moves to AP2, STA2 moves to AP3
- Both STAs automatically roam to different APs based on signal quality

**What it demonstrates:**

- Multiple STAs with independent roaming decisions
- Spatial separation and signal quality differences
- Simultaneous roaming events
- Per-STA beacon caching and AP selection

**Running the Example:**

.. sourcecode:: bash

    ./ns3 run multi-sta-channel-hopping-example

Test Suite
==========

The STA Channel Hopping module includes a test suite in ``test/sta-channel-hopping-test-suite.cc``.

**Current Status:** The test currently contains only a placeholder test.

**Future Work:** Real tests should be added to verify:

- Roaming trigger on disassociation
- AP selection logic (SNR-based)
- Beacon cache querying
- Cross-band roaming
- Attribute configuration

Validation
**********

Integration Testing
===================

The module is integration-tested through:

1. **sta-channel-hopping-example.cc**: Multi-band roaming with 5 APs
2. **multi-sta-channel-hopping-example.cc**: Multi-STA scenario with 3 APs
3. Production-scale simulations with 10+ STAs in real-world roaming scenarios

Manual Testing
==============

The module has been manually tested with:

- **2.4 GHz only scenarios**: Channels 1, 6, 11
- **5 GHz only scenarios**: Channels 36, 40, 44, 48
- **Mixed band scenarios**: 2.4 GHz + 5 GHz combinations
- **Cross-band roaming**: Verified roaming from 2.4 GHz to 5 GHz and vice versa
- **Multi-width channels**: 20/40/80/160 MHz channel widths
- **Dynamic channel changes**: APs changing channels at runtime

Test Coverage
=============

**Current Coverage:**

- ✓ Basic roaming functionality (tested in examples)
- ✓ Multi-channel scanning (tested in examples)
- ✓ SNR-based AP selection (tested in examples)
- ✓ Cross-band roaming (tested in sta-channel-hopping-example.cc)
- ✓ Multi-STA independence (tested in multi-sta-channel-hopping-example.cc)
- ✓ Production usage (tested in config-simulation.cc)
- ⚠ Unit tests (placeholder only - needs implementation)

Behavioral Characteristics
===========================

This module models client-side roaming behavior:

- **Reactive Operation**: Triggered by disassociation events
- **Signal-Based Selection**: AP selection based on SNR measurements
- **Standard Association**: Uses normal WiFi association procedures
- **Independent Operation**: Functions without network-side coordination

References
**********

[1] **IEEE 802.11-2020**: IEEE Standard for Information technology—Telecommunications and information exchange between systems Local and metropolitan area networks—Specific requirements Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) Specifications. https://ieeexplore.ieee.org/document/9363693

[2] **ns-3 WiFi Module Documentation**: https://www.nsnam.org/docs/models/html/wifi.html

[3] **ns-3 Spectrum Module Documentation**: https://www.nsnam.org/docs/models/html/spectrum.html

[4] **IEEE 802.11k-2008**: Radio Resource Measurement (RRM) - for more advanced roaming with Neighbor Reports and Beacon Reports

[5] **IEEE 802.11v-2011**: Wireless Network Management - for BSS Transition Management (not used by this module, see auto-roaming-kv)
