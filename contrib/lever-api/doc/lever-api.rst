LeverApi Module Documentation
=============================

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ============= Module Name
   ------------- Section (#.#)
   ~~~~~~~~~~~~~ Subsection (#.#.#)

The LeverApi module provides a unified interface for dynamic WiFi PHY configuration in |ns3| simulations. It enables runtime modification of critical PHY parameters including transmission power, CCA energy detection threshold, RX sensitivity, and channel settings (channel number, width, band).

The module implements a two-layer architecture: **LeverConfig** for configuration state management and **LeverApi** for applying changes to WiFi devices. A key innovation is delegating channel switching to the ``WifiMac`` layer, which automatically determines band and width from IEEE 802.11 channel numbers and propagates changes from APs to associated STAs.

The source code for the module lives in the directory ``contrib/lever-api``.

Scope and Limitations
---------------------

**What the module can do:**

* Runtime modification of WiFi PHY transmission power (TxPowerStart/TxPowerEnd)
* Dynamic adjustment of CCA energy detection threshold
* Dynamic adjustment of RX sensitivity
* Smart channel switching using IEEE 802.11 standard channel numbers
* Automatic band and width detection from channel numbers
* Automatic AP-to-STA channel change propagation
* Support for 2.4 GHz and 5 GHz bands
* Support for variable channel widths (20/40/80/160 MHz)
* TracedCallbacks for monitoring all parameter changes
* Integration with external optimization systems (e.g., Kafka consumers)

**What the module cannot do:**

* Does not provide channel quality assessment or spectrum sensing
* Does not implement automatic channel selection algorithms
* Does not perform neighbor discovery (use dual-phy-sniffer or beacon-neighbor-protocol-11k)
* Does not implement IEEE 802.11h DFS (Dynamic Frequency Selection)
* Does not support 6 GHz band (802.11ax) - limited to 2.4/5 GHz
* Cannot modify other PHY parameters (e.g., antenna configuration, modulation schemes)

Architecture
------------

Two-Layer Design
~~~~~~~~~~~~~~~~~

The module uses a clean separation of concerns with two main components:

1. **LeverConfig** (Configuration State Layer)

   * Stores desired PHY configuration parameters
   * Fires TracedCallbacks when parameters change
   * No direct PHY access (pure state management)
   * Can be shared across multiple devices or used per-device
   * Provides getters for all parameters

2. **LeverApi** (Application Layer)

   * ns-3 Application installed on WiFi nodes
   * Listens to LeverConfig trace callbacks
   * Applies configuration changes to actual ``WifiPhy`` and ``WifiMac``
   * Delegates channel switching to ``WifiMac::SwitchChannel()``
   * One LeverApi application per WiFi device

Delegation to WifiMac
~~~~~~~~~~~~~~~~~~~~~

A key architectural decision is delegating channel switching to the MAC layer:

* ``LeverApi::SwitchChannel()`` calls ``WifiMac::SwitchChannel()``
* MAC layer automatically determines:

  * WiFi band (2.4 GHz / 5 GHz) from channel number
  * Channel width (20/40/80/160 MHz) from channel number
  * Primary20 channel for bonded channels

* **For APs**: MAC automatically propagates channel changes to all associated STAs
* **For STAs**: MAC follows AP channel changes

This ensures IEEE 802.11 compliance and proper multi-link operation.

Channel Number Encoding
~~~~~~~~~~~~~~~~~~~~~~~~

The module uses IEEE 802.11 standard channel numbers which encode both frequency and width:

**2.4 GHz Band (Channels 1-14)**: Always 20 MHz

* Channels: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
* Width: Fixed at 20 MHz
* Example: ``SwitchChannel(6)`` → Channel 6, 20 MHz, 2.4 GHz

**5 GHz Band**: Channel number encodes width

* **20 MHz channels**: 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165
* **40 MHz channels**: 38, 46, 54, 62, 102, 110, 118, 126, 134, 142, 151, 159
* **80 MHz channels**: 42, 58, 106, 122, 138, 155
* **160 MHz channels**: 50, 114

**Channel Bonding Examples**::

    config->SwitchChannel(36);   // Channel 36, 20 MHz, 5 GHz
    config->SwitchChannel(38);   // Channel 38, 40 MHz (bonds 36+40)
    config->SwitchChannel(42);   // Channel 42, 80 MHz (bonds 36+40+44+48)
    config->SwitchChannel(50);   // Channel 50, 160 MHz (bonds 36+40+44+48+52+56+60+64)

Usage
-----

Basic Setup (Single AP)
~~~~~~~~~~~~~~~~~~~~~~~

Simple scenario with one AP using dynamic PHY configuration::

    #include "ns3/lever-api-helper.h"
    #include "ns3/lever-config.h"

    // Create LeverConfig with desired PHY parameters
    Ptr<LeverConfig> leverConfig = CreateObject<LeverConfig>();
    leverConfig->SetTxPower(20.0);                  // 20 dBm
    leverConfig->SetCcaEdThreshold(-82.0);          // -82 dBm
    leverConfig->SetRxSensitivity(-93.0);           // -93 dBm
    leverConfig->SwitchChannel(36);                 // 5 GHz, channel 36, 20 MHz

    // Install LeverApi application on AP
    LeverApiHelper leverHelper(leverConfig);
    ApplicationContainer leverApp = leverHelper.Install(apNode);
    leverApp.Start(Seconds(0.0));
    leverApp.Stop(Seconds(100.0));

Advanced Setup (Multi-AP with Per-AP Configuration)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Complex scenario with multiple APs, each with independent PHY configuration::

    #include "ns3/lever-api-helper.h"
    #include "ns3/lever-config.h"
    #include <map>

    // Storage for AP configurations
    std::map<uint32_t, Ptr<LeverConfig>> apLeverConfigs;
    std::map<uint32_t, Ptr<LeverApi>> apLeverApis;

    // Create per-AP configurations
    std::vector<uint8_t> apChannels = {36, 40, 44, 48};  // Different channels

    for (uint32_t i = 0; i < apNodes.GetN(); i++)
    {
        // Create unique LeverConfig per AP
        Ptr<LeverConfig> leverConfig = CreateObject<LeverConfig>();
        leverConfig->SetTxPower(20.0);
        leverConfig->SetCcaEdThreshold(-82.0);
        leverConfig->SetRxSensitivity(-93.0);
        leverConfig->SwitchChannel(apChannels[i]);

        // Install LeverApi
        LeverApiHelper leverHelper(leverConfig);
        ApplicationContainer leverApp = leverHelper.Install(apNodes.Get(i));
        leverApp.Start(Seconds(0.0));

        // Store for later runtime control
        apLeverConfigs[i] = leverConfig;
        apLeverApis[i] = leverApp.Get(0)->GetObject<LeverApi>();
    }

Runtime PHY Parameter Modification
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Dynamically change PHY parameters during simulation::

    // Schedule transmission power change at t=5s
    Simulator::Schedule(Seconds(5.0), [&leverConfig]() {
        leverConfig->SetTxPower(15.0);  // Reduce to 15 dBm
    });

    // Schedule CCA threshold adjustment at t=10s
    Simulator::Schedule(Seconds(10.0), [&leverConfig]() {
        leverConfig->SetCcaEdThreshold(-75.0);  // More aggressive CCA
    });

    // Schedule channel switch at t=15s
    Simulator::Schedule(Seconds(15.0), [&leverConfig]() {
        leverConfig->SwitchChannel(44);  // Move to channel 44
    });

Integration with External Optimization
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Example from final-simulation showing Kafka-based remote control::

    // Kafka callback for remote PHY optimization
    void OnKafkaChannelSwitch(Mac48Address apBssid, uint16_t newChannel)
    {
        // Find LeverConfig for this AP
        auto it = g_bssidToLeverConfig.find(apBssid);
        if (it != g_bssidToLeverConfig.end())
        {
            Ptr<LeverConfig> config = it->second;

            // Apply channel switch from remote optimizer
            config->SwitchChannel(newChannel);

            NS_LOG_INFO("AP " << apBssid << " switching to channel "
                        << newChannel << " via Kafka command");
        }
    }

    // Connect Kafka consumer to callback
    kafkaConsumer->SetChannelSwitchCallback(
        MakeCallback(&OnKafkaChannelSwitch));

Helpers
-------

LeverApiHelper
~~~~~~~~~~~~~~

The main helper class for installing LeverApi applications on nodes::

    LeverApiHelper(Ptr<LeverConfig> config);
    ApplicationContainer Install(Ptr<Node> node);
    ApplicationContainer Install(NodeContainer nodes);

**Constructor**:
  Takes a ``Ptr<LeverConfig>`` that defines the PHY configuration to apply.

**Install Methods**:
  * ``Install(Ptr<Node>)`` - Install on single node
  * ``Install(NodeContainer)`` - Install on multiple nodes

**Usage Example**::

    // Create config
    Ptr<LeverConfig> config = CreateObject<LeverConfig>();
    config->SetTxPower(20.0);
    config->SwitchChannel(36);

    // Create helper and install
    LeverApiHelper helper(config);
    ApplicationContainer apps = helper.Install(apNodes);
    apps.Start(Seconds(0.0));
    apps.Stop(simTime);

**Important Notes:**

* One LeverConfig can be shared across multiple devices (same configuration)
* Or create unique LeverConfig per device for independent control
* LeverApi application must be started/stopped like any ns-3 Application
* Changes to LeverConfig are applied automatically to all connected LeverApi instances

Attributes
----------

LeverConfig Class Attributes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``LeverConfig`` class stores the following PHY parameters:

**TxPower** (double)
  * Transmission power in dBm
  * Getter: ``double GetTxPower() const``
  * Setter: ``void SetTxPower(double dbm)``
  * Typical range: 0-30 dBm
  * TracedCallback: ``TxPowerChanged``

**CcaEdThreshold** (double)
  * CCA Energy Detection threshold in dBm
  * Getter: ``double GetCcaEdThreshold() const``
  * Setter: ``void SetCcaEdThreshold(double dbm)``
  * Typical range: -100 to -50 dBm
  * TracedCallback: ``CcaEdThresholdChanged``

**RxSensitivity** (double)
  * Receiver sensitivity in dBm
  * Getter: ``double GetRxSensitivity() const``
  * Setter: ``void SetRxSensitivity(double dbm)``
  * Typical range: -100 to -50 dBm
  * TracedCallback: ``RxSensitivityChanged``

**Channel** (uint16_t)
  * Current channel number (IEEE 802.11 encoding)
  * Getter: ``uint16_t GetChannel() const``
  * Setter: ``void SwitchChannel(uint16_t channelNumber)``
  * Valid values: 1-14 (2.4 GHz), 36-165 (5 GHz with encoding)
  * TracedCallback: ``ChannelChanged``

LeverApi Class Attributes
~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``LeverApi`` application does not expose ns-3 TypeId attributes. Configuration is done via the attached ``LeverConfig`` object.

**Configuration Method**:
  * ``void SetConfig(Ptr<LeverConfig> config)`` - Attach to a LeverConfig

**Device Access**:
  * ``Ptr<WifiNetDevice> GetWifiNetDevice() const`` - Get underlying WiFi device

Traces
------

LeverConfig TracedCallbacks
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All PHY parameter changes in ``LeverConfig`` fire TracedCallbacks:

**TxPowerChanged**
  * Signature: ``void (double oldValue, double newValue)``
  * Fired when: ``SetTxPower()`` is called with a different value
  * Parameters:

    * ``oldValue`` - Previous transmission power in dBm
    * ``newValue`` - New transmission power in dBm

  * Example::

      config->TraceConnectWithoutContext("TxPowerChanged",
          MakeCallback(&OnTxPowerChanged));

      void OnTxPowerChanged(double oldPower, double newPower)
      {
          std::cout << "TX Power: " << oldPower << " → "
                    << newPower << " dBm\n";
      }

**CcaEdThresholdChanged**
  * Signature: ``void (double oldValue, double newValue)``
  * Fired when: ``SetCcaEdThreshold()`` is called with a different value
  * Parameters:

    * ``oldValue`` - Previous CCA threshold in dBm
    * ``newValue`` - New CCA threshold in dBm

**RxSensitivityChanged**
  * Signature: ``void (double oldValue, double newValue)``
  * Fired when: ``SetRxSensitivity()`` is called with a different value
  * Parameters:

    * ``oldValue`` - Previous RX sensitivity in dBm
    * ``newValue`` - New RX sensitivity in dBm

**ChannelChanged**
  * Signature: ``void (uint16_t oldChannel, uint16_t newChannel)``
  * Fired when: ``SwitchChannel()`` is called with a different channel
  * Parameters:

    * ``oldChannel`` - Previous channel number
    * ``newChannel`` - New channel number (IEEE 802.11 encoding)

  * Example::

      config->TraceConnectWithoutContext("ChannelChanged",
          MakeCallback(&OnChannelChanged));

      void OnChannelChanged(uint16_t oldCh, uint16_t newCh)
      {
          std::cout << "Channel: " << oldCh << " → " << newCh << "\n";
      }

Examples and Tests
------------------

The module provides 6 working examples demonstrating various use cases:

lever-api-example.cc
~~~~~~~~~~~~~~~~~~~~

Basic dynamic PHY configuration with spectrum analyzer visualization.

* **Scenario**: Single AP with channel hopping
* **Features**:

  * Spectrum analyzer for visualization
  * Channel hopping: 2.4 GHz channels 1 → 6 → 11
  * Transmission power changes: 20 → 15 → 10 dBm
  * CCA threshold adjustments

* **Purpose**: Introduction to LeverApi basics
* **Run**: ``./ns3 run lever-api-example``

multi-ap-roaming-example.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Multi-AP scenario with roaming and dynamic power control.

* **Scenario**: 3 APs on 5 GHz, 1 mobile STA
* **Features**:

  * Per-AP independent PHY configuration
  * Dynamic transmission power adjustment
  * STA mobility with roaming
  * FriisPropagationLossModel for realistic path loss

* **Purpose**: Demonstrates multi-AP coordination
* **Run**: ``./ns3 run multi-ap-roaming-example``

lever-api-comprehensive-test.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Comprehensive test suite for all PHY parameters and channel widths.

* **Scenario**: Single AP with systematic parameter testing
* **Test Coverage**:

  * Transmission power: 20, 15, 10, 5 dBm
  * CCA threshold: -82, -75, -70 dBm
  * RX sensitivity: -93, -90, -85 dBm
  * Channel widths: 20, 40, 80, 160 MHz
  * Band switching: 2.4 GHz ↔ 5 GHz

* **Purpose**: Validation and regression testing
* **Run**: ``./ns3 run lever-api-comprehensive-test``
* **Output**: CSV file with all parameter changes and timestamps

channel-switch-demo.cc
~~~~~~~~~~~~~~~~~~~~~~

Smart channel switching demonstration with automatic width detection.

* **Scenario**: Single AP demonstrating all channel widths
* **Features**:

  * Tests all 5 GHz channel widths (20/40/80/160 MHz)
  * Validates automatic band/width detection
  * Logs PHY configuration after each switch

* **Purpose**: Channel encoding validation
* **Run**: ``./ns3 run channel-switch-demo``

lever-api-channel-switch-example.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Demonstrates ``LeverApi::SwitchChannel()`` delegation to WifiMac.

* **Scenario**: AP with direct channel switching
* **Features**:

  * Shows delegation to ``WifiMac::SwitchChannel()``
  * Demonstrates AP-to-STA channel propagation
  * Tests linkId parameter handling

* **Purpose**: Architecture demonstration
* **Run**: ``./ns3 run lever-api-channel-switch-example``

sta-channel-following-example.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Demonstrates STAs automatically following AP channel changes.

* **Scenario**: 1 AP + 2 STAs, dynamic channel switching
* **Features**:

  * AP switches channels: 36 → 40 → 44 → 48
  * STAs automatically follow AP channel changes
  * Validates association is maintained during switches

* **Purpose**: AP-to-STA channel propagation validation
* **Run**: ``./ns3 run sta-channel-following-example``

Test Suite
~~~~~~~~~~

**File**: ``test/lever-api-test-suite.cc``

**Status**: Minimal placeholder tests

The test suite currently contains only basic initialization tests. Comprehensive functional tests are needed for:

* LeverConfig parameter setting and getter consistency
* TracedCallback firing verification
* LeverApi PHY application correctness
* Channel encoding validation
* AP-to-STA channel propagation

**Run tests**::

    ./ns3 build
    ./test.py --suite=lever-api

Validation
----------

The module has been validated through:

1. **Example Testing**

   * All 6 examples compile and run without errors
   * lever-api-comprehensive-test validates all parameter ranges
   * channel-switch-demo validates all channel widths (20/40/80/160 MHz)
   * sta-channel-following-example validates AP-to-STA propagation

2. **Integration Testing**

   * Deployed in final-simulation (config-simulation.cc) for production use
   * Integrated with Kafka consumer for remote control
   * Tested with beacon-neighbor-protocol-11k and auto-roaming-kv modules
   * Tested in multi-AP scenarios (4+ APs)

3. **Functional Validation**

   * Transmission power changes verified via ``WifiPhy::GetTxPowerStart()``
   * CCA threshold changes verified via ``WifiPhy::GetCcaEdThreshold()``
   * RX sensitivity changes verified via ``WifiPhy::GetRxSensitivity()``
   * Channel switches verified via ``WifiPhy::GetChannelNumber()`` and ``GetChannelWidth()``
   * Band detection verified for 2.4 GHz ↔ 5 GHz switches
   * Width detection verified for 20/40/80/160 MHz channels

4. **Performance Testing**

   * Tested with up to 10 APs with independent configurations
   * Tested rapid parameter changes (every 100ms)
   * Tested channel hopping across all 5 GHz channels
   * No performance degradation observed

5. **Limitations**

   * No formal unit test suite (only placeholder tests)
   * No validation against real hardware measurements
   * No comparison with other dynamic PHY configuration implementations
   * No stress testing with 50+ concurrent parameter changes

References
----------

[`1 <https://ieeexplore.ieee.org/document/7786995>`_] IEEE Standard for Information technology--Telecommunications and information exchange between systems Local and metropolitan area networks--Specific requirements - Part 11: Wireless LAN Medium Access Control (MAC) and Physical Layer (PHY) Specifications, IEEE Std 802.11-2020.

[`2 <https://www.nsnam.org/docs/models/html/wifi.html>`_] |ns3| WiFi Module Documentation.

[`3 <https://www.nsnam.org/docs/models/html/spectrum.html>`_] |ns3| Spectrum Module Documentation.
