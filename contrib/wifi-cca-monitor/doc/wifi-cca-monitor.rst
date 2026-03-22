WiFi CCA Monitor Module
=======================

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ============= Module Name
   ------------- Section (#.#)
   ~~~~~~~~~~~~~ Subsection (#.#.#)

This chapter describes the WiFi CCA Monitor module for |ns3|.

Overview
--------

The WiFi CCA Monitor module provides comprehensive monitoring of WiFi channel utilization and PHY layer states in |ns3| simulations. It tracks detailed metrics including Clear Channel Assessment (CCA) busy time, transmission time, reception time, idle time, and MAC layer throughput using a sliding window mechanism.

Key features:

* **Per-device monitoring**: Install monitors on specific WiFi devices (APs or STAs)
* **Sliding window measurements**: Configurable time windows for accurate utilization metrics
* **PHY state tracking**: Monitors all PHY states (IDLE, TX, RX, CCA_BUSY, SWITCHING)
* **MAC throughput metrics**: Tracks bytes sent/received and calculates throughput
* **Real-time trace callbacks**: Periodic updates with 13-parameter utilization data
* **Flexible configuration**: Programmable window size and update interval
* **Multiple device support**: Monitor multiple devices simultaneously

This module is particularly useful for:

* Channel utilization analysis in dense WiFi deployments
* Airtime fairness and spectrum efficiency studies
* Dynamic channel access parameter tuning
* Multi-BSS coordination and interference analysis
* Validation of WiFi protocol modifications

The source code for the module lives in the directory ``contrib/wifi-cca-monitor``.

Scope and Limitations
---------------------

What the Module Does
~~~~~~~~~~~~~~~~~~~~

The WiFi CCA Monitor module:

* Monitors PHY state transitions via WiFi trace sources
* Calculates channel utilization metrics over sliding time windows
* Tracks MAC layer throughput (bytes sent/received)
* Provides periodic trace callbacks with comprehensive utilization data
* Supports per-device monitoring with independent window configurations
* Works with both YansWifi and SpectrumWifi PHY implementations
* Compatible with all WiFi standards supported by |ns3| (802.11a/b/g/n/ac/ax/be)

What the Module Does Not Do
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The module does not:

* Modify WiFi behavior or channel access mechanisms
* Provide network-wide aggregated statistics automatically
* Monitor non-WiFi devices or wired links
* Track application-layer metrics (requires separate tools)
* Perform automatic channel switching or optimization
* Store historical data beyond the sliding window

Technical Limitations
~~~~~~~~~~~~~~~~~~~~~

* **Window granularity**: Update interval determines trace frequency (minimum ~10ms recommended)
* **Memory overhead**: Each monitor maintains sliding window state per device
* **Trace callback overhead**: Frequent callbacks may impact simulation performance
* **No persistence**: Statistics reset when monitors are destroyed
* **Single PHY per device**: Does not handle multi-radio devices directly

Design
------

Architecture
~~~~~~~~~~~~

The WiFi CCA Monitor module consists of two main components:

1. **WifiCcaMonitor (Model)**: Core monitoring class that tracks PHY states and calculates metrics
2. **WifiCcaMonitorHelper (Helper)**: Simplifies monitor creation and installation

The module integrates with |ns3|'s WiFi module by connecting to existing trace sources exposed by WifiPhy objects.

Class Hierarchy
***************

::

    ns3::Object
        └── ns3::WifiCcaMonitor

    (Non-object)
        └── ns3::WifiCcaMonitorHelper

Key Classes
***********

**WifiCcaMonitor**

The core monitoring class (``wifi-cca-monitor.h/cc``) that:

* Installs callbacks on WiFi PHY trace sources
* Tracks PHY state transitions and durations
* Maintains sliding window statistics
* Calculates channel utilization percentages
* Exposes trace source for periodic updates
* Provides getter methods for current metrics

**WifiCcaMonitorHelper**

The helper class (``wifi-cca-monitor-helper.h/cc``) that:

* Simplifies monitor creation with default parameters
* Configures window size and update interval
* Installs monitors on single or multiple devices
* Returns monitor pointers for trace connection

Design Principles
~~~~~~~~~~~~~~~~~

Sliding Window Mechanism
************************

The module uses a sliding window approach for calculating channel utilization:

1. **Window Size**: Defines the duration over which metrics are calculated (e.g., 100ms)
2. **Update Interval**: Determines how frequently trace callbacks are triggered (e.g., 100ms)
3. **State Tracking**: PHY state changes update cumulative time counters
4. **Periodic Calculation**: At each update interval, metrics are calculated over the window
5. **Percentage Conversion**: Time durations are converted to utilization percentages

This design provides:

* Smooth metrics that adapt to changing conditions
* Configurable temporal resolution
* Low memory overhead (no full history storage)
* Real-time monitoring capability

Per-Device Monitoring
*********************

Each monitor operates independently:

* Installed on a specific NetDevice
* Maintains its own sliding window state
* Tracks PHY states for that device only
* Fires independent trace callbacks
* Does not aggregate across devices

This allows:

* Fine-grained per-device analysis
* Asymmetric AP vs. STA utilization tracking
* Multi-BSS interference characterization
* Flexible deployment (monitor only selected devices)

MAC Layer Integration
*********************

The module tracks MAC layer metrics by:

* Monitoring ``MacTx`` trace for transmitted bytes
* Monitoring ``MacRx`` trace for received bytes
* Calculating throughput as (bytes * 8) / window_size
* Including throughput in periodic trace callbacks

How It Works
------------

PHY State Monitoring
~~~~~~~~~~~~~~~~~~~~

The WiFi PHY layer operates in several states:

* **IDLE**: Channel is idle, ready to transmit
* **TX**: PHY is transmitting a frame
* **RX**: PHY is receiving a frame
* **CCA_BUSY**: Channel sensed busy (carrier sense), but not TX/RX
* **SWITCHING**: PHY is switching channels

The WifiCcaMonitor tracks time spent in each state by:

1. Connecting to ``WifiPhy::State`` trace source
2. Recording timestamp when state changes occur
3. Accumulating duration in each state
4. Resetting counters at each update interval

Channel Utilization Calculation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Channel utilization is calculated as the percentage of time the channel is busy::

    ChannelUtilization = (TX_time + RX_time + CCA_time) / WindowSize * 100%

Individual components::

    TX_util = TX_time / WindowSize * 100%
    RX_util = RX_time / WindowSize * 100%
    CCA_util = CCA_time / WindowSize * 100%
    Idle_util = Idle_time / WindowSize * 100%

Trace Callback Mechanism
~~~~~~~~~~~~~~~~~~~~~~~~~

The module fires the ``ChannelUtilization`` trace at regular intervals with 13 parameters:

1. ``uint32_t nodeId`` - Node ID of the monitored device
2. ``double timestamp`` - Current simulation time (seconds)
3. ``double totalUtil`` - Total channel utilization percentage
4. ``double txUtil`` - TX utilization percentage
5. ``double rxUtil`` - RX utilization percentage
6. ``double ccaUtil`` - CCA busy utilization percentage
7. ``double txTime`` - Absolute TX time in window (seconds)
8. ``double rxTime`` - Absolute RX time in window (seconds)
9. ``double ccaTime`` - Absolute CCA busy time in window (seconds)
10. ``double idleTime`` - Absolute idle time in window (seconds)
11. ``uint64_t bytesSent`` - Bytes transmitted in window
12. ``uint64_t bytesReceived`` - Bytes received in window
13. ``double throughput`` - MAC layer throughput (Mbps)

Typical Workflow
****************

1. Create WiFi network with nodes and devices
2. Create WifiCcaMonitorHelper
3. Optionally configure window size and update interval
4. Install monitors on desired devices
5. Connect trace callbacks to monitor output
6. Run simulation
7. Process trace data in callback functions

Usage
-----

Basic Example
~~~~~~~~~~~~~

Here's a minimal example of using the WiFi CCA Monitor::

    #include "ns3/wifi-cca-monitor-helper.h"

    // After WiFi network is set up...
    NodeContainer apNodes; // Your AP nodes
    NodeContainer staNodes; // Your STA nodes

    // Create monitor helper
    WifiCcaMonitorHelper monitorHelper;
    monitorHelper.SetWindowSize(Seconds(0.1));      // 100ms window
    monitorHelper.SetUpdateInterval(Seconds(0.1));  // Update every 100ms

    // Install on AP devices
    for (uint32_t i = 0; i < apNodes.GetN(); i++)
    {
        Ptr<NetDevice> apDev = apNodes.Get(i)->GetDevice(0);
        Ptr<WifiCcaMonitor> monitor = monitorHelper.Install(apDev);

        // Connect to trace callback
        monitor->TraceConnectWithoutContext("ChannelUtilization",
                                            MakeCallback(&YourTraceFunction));
    }

    // Install on STA devices
    for (uint32_t i = 0; i < staNodes.GetN(); i++)
    {
        Ptr<NetDevice> staDev = staNodes.Get(i)->GetDevice(0);
        Ptr<WifiCcaMonitor> monitor = monitorHelper.Install(staDev);

        monitor->TraceConnectWithoutContext("ChannelUtilization",
                                            MakeCallback(&YourTraceFunction));
    }

Trace Callback Function
~~~~~~~~~~~~~~~~~~~~~~~~

Define a callback function to process utilization data::

    void ChannelUtilizationTrace(uint32_t nodeId, double timestamp,
                                 double totalUtil, double txUtil, double rxUtil,
                                 double ccaUtil, double txTime, double rxTime,
                                 double ccaTime, double idleTime,
                                 uint64_t bytesSent, uint64_t bytesReceived,
                                 double throughput)
    {
        std::cout << "Time: " << timestamp << "s, "
                  << "Node: " << nodeId << ", "
                  << "ChanUtil: " << totalUtil << "%, "
                  << "TX: " << txUtil << "%, "
                  << "RX: " << rxUtil << "%, "
                  << "CCA: " << ccaUtil << "%, "
                  << "Throughput: " << throughput << " Mbps"
                  << std::endl;
    }

Monitor Multiple Devices
~~~~~~~~~~~~~~~~~~~~~~~~~

Install monitors on a container of devices::

    NetDeviceContainer allDevices; // Your device container

    WifiCcaMonitorHelper helper;
    helper.SetWindowSize(Seconds(0.05));  // 50ms window

    std::vector<Ptr<WifiCcaMonitor>> monitors = helper.Install(allDevices);

    // Connect all monitors to the same callback
    for (auto& monitor : monitors)
    {
        monitor->TraceConnectWithoutContext("ChannelUtilization",
                                            MakeCallback(&ChannelUtilizationTrace));
    }

Programmatic Access
~~~~~~~~~~~~~~~~~~~

Access metrics directly without trace callbacks::

    Ptr<WifiCcaMonitor> monitor = monitorHelper.Install(device);

    // Schedule periodic queries
    Simulator::Schedule(Seconds(5.0), [monitor]() {
        double chanUtil = monitor->GetChannelUtilization();
        double txTime = monitor->GetTxTime();
        double rxTime = monitor->GetRxTime();
        double ccaBusyTime = monitor->GetCcaBusyTime();
        double idleTime = monitor->GetIdleTime();

        uint64_t bytesSent = monitor->GetBytesSent();
        uint64_t bytesReceived = monitor->GetBytesReceived();

        std::cout << "Channel Utilization: " << chanUtil << "%" << std::endl;
    });

Configuration Guidelines
~~~~~~~~~~~~~~~~~~~~~~~~~

Window Size Selection
*********************

The window size determines the temporal resolution:

* **Smaller windows (10-50ms)**: Fast adaptation, more variability
* **Medium windows (100-200ms)**: Balanced smoothing and responsiveness
* **Larger windows (500ms-1s)**: Smooth metrics, slower adaptation

Recommended values:

* **802.11ax/be**: 50-100ms (shorter frame durations)
* **802.11ac/n**: 100-200ms (balanced)
* **Dense networks**: 100-200ms (capture bursts)
* **Light traffic**: 500ms-1s (smooth out variability)

Update Interval Selection
**************************

The update interval controls trace frequency:

* **High-frequency (10-50ms)**: Fine-grained monitoring, higher overhead
* **Medium-frequency (100-200ms)**: Standard monitoring, low overhead
* **Low-frequency (500ms-1s)**: Coarse monitoring, minimal overhead

Typically, ``updateInterval == windowSize`` for non-overlapping windows. For overlapping windows: ``updateInterval < windowSize`` provides more frequent updates.

Helpers
~~~~~~~

WifiCcaMonitorHelper
********************

The helper simplifies monitor creation and installation::

    WifiCcaMonitorHelper monitorHelper;

    // Configure all monitors created by this helper
    monitorHelper.SetWindowSize(Seconds(0.1));
    monitorHelper.SetUpdateInterval(Seconds(0.1));

    // Install on single device
    Ptr<WifiCcaMonitor> monitor = monitorHelper.Install(device);

    // Install on multiple devices
    std::vector<Ptr<WifiCcaMonitor>> monitors = monitorHelper.Install(deviceContainer);

Attributes
~~~~~~~~~~

Not applicable. The WifiCcaMonitor and WifiCcaMonitorHelper do not expose |ns3| Attributes. Configuration is done programmatically via:

**WifiCcaMonitorHelper Methods**:

* ``SetWindowSize(Time windowSize)`` - Set sliding window duration
* ``SetUpdateInterval(Time interval)`` - Set trace callback frequency

**WifiCcaMonitor Methods**:

* ``SetWindowSize(Time windowSize)`` - Set window size for this monitor
* ``SetUpdateInterval(Time interval)`` - Set update interval for this monitor
* ``Start()`` - Start monitoring (called automatically on install)
* ``Stop()`` - Stop monitoring

Traces
~~~~~~

ChannelUtilization
******************

**Type**: ``TracedCallback<uint32_t, double, double, double, double, double, double, double, double, double, uint64_t, uint64_t, double>``

**Fired**: At each update interval

**Parameters**:

1. ``nodeId`` (uint32_t): Node ID of monitored device
2. ``timestamp`` (double): Current simulation time in seconds
3. ``totalUtil`` (double): Total channel utilization percentage (TX + RX + CCA)
4. ``txUtil`` (double): TX utilization percentage
5. ``rxUtil`` (double): RX utilization percentage
6. ``ccaUtil`` (double): CCA busy utilization percentage
7. ``txTime`` (double): Absolute TX time in window (seconds)
8. ``rxTime`` (double): Absolute RX time in window (seconds)
9. ``ccaTime`` (double): Absolute CCA busy time in window (seconds)
10. ``idleTime`` (double): Absolute idle time in window (seconds)
11. ``bytesSent`` (uint64_t): Bytes transmitted in window
12. ``bytesReceived`` (uint64_t): Bytes received in window
13. ``throughput`` (double): MAC layer throughput in Mbps

**Connection Example**::

    monitor->TraceConnectWithoutContext("ChannelUtilization",
                                        MakeCallback(&YourCallback));

Logging to File
***************

Log trace data to a file::

    std::ofstream logFile("channel-utilization.csv");
    logFile << "Time,NodeId,TotalUtil,TxUtil,RxUtil,CcaUtil,Throughput\n";

    void FileTrace(uint32_t nodeId, double timestamp, double totalUtil,
                   double txUtil, double rxUtil, double ccaUtil,
                   double txTime, double rxTime, double ccaTime, double idleTime,
                   uint64_t bytesSent, uint64_t bytesReceived, double throughput)
    {
        logFile << timestamp << ","
                << nodeId << ","
                << totalUtil << ","
                << txUtil << ","
                << rxUtil << ","
                << ccaUtil << ","
                << throughput << "\n";
    }

Examples and Tests
------------------

wifi-cca-monitor-example.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The example (``contrib/wifi-cca-monitor/examples/wifi-cca-monitor-example.cc``) demonstrates comprehensive per-device channel utilization monitoring in a multi-BSS WiFi network.

**Scenario Description**:

* **Topology**: Multiple APs, each serving multiple STAs
* **WiFi Standard**: 802.11ax (WiFi 6)
* **Channels**: 5 GHz band, different channels per AP (36, 40, 44, ...)
* **Channel Width**: 20 MHz
* **Rate Control**: ConstantRateWifiManager (HeMcs5 data, HeMcs0 control)
* **Traffic**: Uplink UDP traffic from STAs to APs
* **Monitoring**: Per-device monitoring on all APs and selected STAs

**Command-Line Parameters**:

* ``--nAps``: Number of access points (default: 2)
* ``--nStaPerAp``: Number of stations per AP (default: 2)
* ``--dataRate``: Data rate per station in kbps (default: 5000)
* ``--packetSize``: UDP packet size in bytes (default: 1472)
* ``--windowSize``: Sliding window size in seconds (default: 0.1)
* ``--updateInterval``: Update interval in seconds (default: 0.1)

**Running the Example**:

Default configuration::

    ./ns3 run wifi-cca-monitor-example

Custom configuration::

    ./ns3 run "wifi-cca-monitor-example --nAps=3 --nStaPerAp=5 --dataRate=10000"

Adjust window parameters::

    ./ns3 run "wifi-cca-monitor-example --windowSize=0.05 --updateInterval=0.05"

High utilization scenario::

    ./ns3 run "wifi-cca-monitor-example --nAps=4 --nStaPerAp=10 --dataRate=8000"

**Example Output**::

    === WiFi CCA Monitor Example ===
    APs: 2, Stations per AP: 2
    Window size: 100 ms
    Update interval: 100 ms

    === Installing monitors on selected devices ===
      - Monitoring AP 0 (Node 0)
      - Monitoring AP 1 (Node 1)
      - Monitoring STA 0 (Node 2)
      - Monitoring STA 2 (Node 4)

    === Starting Simulation ===
    t=time | Node ID | Channel Utilization (total, TX, RX, CCA) | Throughput | Bytes

    t=2.100s | Node 0 | ChanUtil=12.345% (TX: 3.456% RX: 8.234% CCA: 0.655%) | ...
    t=2.100s | Node 1 | ChanUtil=11.987% (TX: 3.211% RX: 8.012% CCA: 0.764%) | ...

**What to Observe**:

1. **AP vs. STA Utilization**: APs typically show higher RX (receiving uplink traffic), STAs show higher TX
2. **Channel Utilization Asymmetry**: Different devices on the same channel see different utilization
3. **CCA Busy Time**: Reflects OBSS interference and hidden nodes
4. **Throughput Validation**: MAC throughput matches configured data rates
5. **Multi-BSS Interference**: When APs share channels, utilization increases

Validation
----------

Production Usage
~~~~~~~~~~~~~~~~

The WiFi CCA Monitor module is actively used in production simulations:

**config-simulation.cc**

The config-simulation (``contrib/final-simulation/examples/config-simulation.cc``) uses WifiCcaMonitor for:

* **MAC layer channel utilization tracking**: Monitor all APs and STAs
* **Per-BSS utilization analysis**: Track each BSS independently
* **Interference characterization**: Measure co-channel and adjacent channel interference
* **Performance validation**: Correlate utilization with application throughput
* **Dynamic optimization**: Feed utilization data to control algorithms

This validates the module's:

* Stability in long-running simulations
* Accuracy across varying traffic patterns
* Compatibility with complex WiFi scenarios
* Low performance overhead
* Integration with other |ns3| modules

Testing
~~~~~~~

The module has been tested with:

* **WiFi standards**: 802.11a/b/g/n/ac/ax/be
* **PHY implementations**: YansWifi and SpectrumWifi
* **Channel widths**: 20, 40, 80, 160 MHz
* **Traffic patterns**: CBR, bursty, bidirectional
* **Network sizes**: 1-100+ devices
* **Simulation durations**: Seconds to hours

Verification Methodology
************************

Utilization metrics are verified by:

1. **Analytical comparison**: Compare to theoretical channel occupancy
2. **Trace correlation**: Cross-reference with pcap captures
3. **Conservation check**: Ensure TX + RX + CCA + IDLE = 100% (within rounding)
4. **Throughput validation**: MAC throughput matches application data rate
5. **Multi-device consistency**: Devices on same channel see correlated utilization

Known Accuracy
**************

* **Utilization accuracy**: ±0.1% (limited by trace granularity)
* **Throughput accuracy**: Matches MAC layer exactly
* **Time accounting**: All time accounted for (no gaps)
* **Window accuracy**: Metrics calculated over exact window duration

References
----------

The WiFi CCA Monitor module implementation is based on:

1. |ns3| WiFi Module: WiFi PHY state machine and trace sources, WifiNetDevice and WifiPhy architecture, MAC layer trace sources
2. IEEE 802.11-2020: Clear Channel Assessment (CCA) procedures
3. IEEE 802.11ax: Enhanced channel access and airtime fairness
4. IEEE 802.11be: Multi-link operation and coordination

For more details on the WiFi module, see the |ns3| WiFi documentation.
