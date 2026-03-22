Spectrum Pipe Streamer
======================

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ============= Module Name
   ------------- Section (#.#)
   ~~~~~~~~~~~~~ Subsection (#.#.#)

The Spectrum Pipe Streamer module enables real-time streaming of Power Spectral Density (PSD) data from |ns3| spectrum analyzers to external processes via named pipes (FIFOs). This module implements an intelligent dual-band channel selection system combining spectrum sensing, CNN-based interference detection, and Multi-Armed Bandit (MAB) reinforcement learning for dynamic spectrum access.

The complete pipeline integrates |ns3| network simulation with Python-based machine learning agents to perform adaptive channel selection in WiFi networks. Named pipes provide synchronous, low-latency data delivery with blocking flow control, ensuring no data loss and tight coupling between simulation and external processing.

The source code for the module lives in the directory ``contrib/spectrum-pipe-streamer``.

Overview
--------

The module implements an intelligent spectrum sensing and channel selection pipeline with three primary components:

1. **NS-3 Simulation** (``spectrum-pipe-streamer-example.cc``): Generates realistic WiFi/Bluetooth/Zigbee/Microwave/Radar interference across dual bands (2.4 GHz and 5 GHz) and streams PSD data via named pipes.

2. **Pipe Reader & Canvas Builder** (``main_live_handler.py``): Reads dual-band PSD streams, builds time-frequency canvases, performs EWMA-based change detection, and triggers CNN inference for interference classification.

3. **MAB/PPO Agent** (``receiver.py``): Uses Thompson Sampling to select optimal (band, channel) pairs and PPO to optimize sensing dwell times (50-200ms).

**Pipeline Architecture:**

::

    NS-3 Simulation → Named Pipes → Canvas Builder → Socket → MAB/PPO Agent
                        (10ms)      (50-frame buffer)  (TCP)   (Decision)
                                           ↓                        ↓
                                    EWMA Monitor ←──────── Feedback Socket
                                           ↓
                                    CNN Inference
                                    (WiFi/BT/ZigBee/
                                     Microwave/Radar)

Scope and Limitations
---------------------

**What the Module Can Do:**

- Stream real-time dual-band PSD data (2.4 GHz and 5 GHz simultaneously)
- Generate realistic multi-technology interference scenarios (WiFi, Bluetooth, Zigbee, Microwave, Radar)
- Build time-frequency canvases for machine learning processing
- Detect spectrum activity changes using dual EWMA (Exponentially Weighted Moving Average)
- Classify interference technologies using CNN models
- Perform intelligent channel selection using Multi-Armed Bandit (Thompson Sampling)
- Optimize sensing dwell times using Proximal Policy Optimization (PPO)
- Provide flow control through blocking I/O to prevent data loss
- Support custom interference scenarios and detection algorithms

**Behavioral Characteristics:**

- **Blocking Operation**: The simulation blocks when the pipe buffer is full, ensuring synchronization with external processing. Critical execution order: receiver.py → main_live_handler.py → NS-3 simulation.
- **Dual-Band Processing**: Independent threads handle 2.4 GHz and 5 GHz streams with separate named pipes.
- **EWMA-Triggered CNN**: Runs inference only when spectrum activity changes (efficient resource usage).
- **Local-Only Communication**: Named pipes are POSIX local IPC mechanisms. Reader must run on the same machine.
- **Pre-trained Models Required**: CNN inference requires ``bay2.pt`` (2.4 GHz) and ``5ghz.pt`` (5 GHz) models.

Design Rationale
----------------

**Why Named Pipes Over Files?**

Named pipes enable real-time streaming with automatic flow control. Unlike file-based logging, pipes deliver data as it's generated without disk I/O overhead. The blocking behavior ensures the simulation doesn't outpace external processing, maintaining synchronization critical for reinforcement learning and real-time decision systems.

**Why Blocking Behavior?**

Blocking I/O provides implicit backpressure when the reader is slower than the simulation. This prevents buffer overflow, data loss, and ensures the external process receives every PSD sample. For applications like safe reinforcement learning or RRM policy validation, this guarantee is essential.

**Why Binary Format?**

Binary serialization minimizes encoding overhead compared to text formats like JSON or CSV. PSD data consists of floating-point arrays that serialize efficiently in binary form. The fixed-size header enables fast parsing on the reader side.

**Comparison with spectrum-socket-streamer:**

+-------------------+---------------------------+-------------------------+
| Feature           | spectrum-pipe-streamer    | spectrum-socket-streamer|
+===================+===========================+=========================+
| Transport         | Named Pipes (FIFO)        | UDP Sockets             |
+-------------------+---------------------------+-------------------------+
| Blocking          | Yes                       | No                      |
+-------------------+---------------------------+-------------------------+
| Flow Control      | Automatic                 | None                    |
+-------------------+---------------------------+-------------------------+
| Data Loss         | No                        | Possible (UDP)          |
+-------------------+---------------------------+-------------------------+
| Synchronization   | Tight                     | Loose                   |
+-------------------+---------------------------+-------------------------+
| Network Support   | Local only                | Yes                     |
+-------------------+---------------------------+-------------------------+
| Performance       | Medium (blocking)         | Low (no blocking)       |
+-------------------+---------------------------+-------------------------+
| Use Case          | Real-time ML/RL           | Monitoring/visualization|
+-------------------+---------------------------+-------------------------+

Model Description
-----------------

SpectrumPipeStreamer Class
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``SpectrumPipeStreamer`` class manages streaming for a single node. Key responsibilities:

- Create and manage a named pipe at a configurable path (default: ``/tmp/ns3-spectrum/node-{id}.pipe``)
- Receive PSD callbacks from spectrum analyzer trace sources
- Rate-limit streaming based on a configurable interval (default: 100ms)
- Serialize PSD data to binary format
- Perform blocking writes to the pipe
- Track statistics (packets sent, bytes sent)

**Binary Data Format:**

Each streamed packet has the following structure:

::

    ┌──────────────┬────────────────┬──────────────┬─────────────────────┐
    │  node_id     │   timestamp    │  num_values  │   psd_values[]      │
    │  (uint32_t)  │   (double)     │  (uint32_t)  │   (double array)    │
    │  4 bytes     │   8 bytes      │  4 bytes     │   8 * num_values    │
    └──────────────┴────────────────┴──────────────┴─────────────────────┘

- **node_id**: Identifier of the node generating the data
- **timestamp**: Simulation time in seconds
- **num_values**: Number of PSD values in the array
- **psd_values**: Array of PSD values in watts/Hz (double precision)

The frequency bins are implicit and determined by the configured frequency range and resolution.

Pipe Lifecycle
~~~~~~~~~~~~~~

1. **Initialization**: The pipe is created when ``InitializePipe()`` is called, typically during simulation setup. The directory is created if it doesn't exist, and the FIFO is created using ``mkfifo()``.

2. **Opening**: The pipe is opened with ``O_WRONLY`` (write-only) mode. This call **blocks** until a reader opens the pipe for reading.

3. **Streaming**: PSD data is written to the pipe when callbacks are received and the streaming interval has elapsed. Writes may block if the pipe buffer is full.

4. **Cleanup**: The pipe file descriptor is closed when the streamer is destroyed. The FIFO file remains on disk for inspection or reuse.

Usage
-----

Execution Order (CRITICAL)
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The pipeline components must be started in this exact order:

**Step 1: Start MAB/PPO Receiver (FIRST)**

::

    cd contrib/spectrum-pipe-streamer/examples
    python3 receiver.py

The receiver listens on two TCP sockets:

- Port 65430: Receives dual-band canvases from main_live_handler
- Port 65431: Sends MAB decisions back to main_live_handler

**Step 2: Start Main Live Handler (SECOND)**

::

    python3 main_live_handler.py

This component:

1. Connects to receiver.py sockets
2. Creates named pipes for NS-3 data
3. Waits for NS-3 simulation to begin streaming

**Step 3: Start NS-3 Simulation (LAST)**

::

    ./ns3 run spectrum-pipe-streamer-example

The simulation streams PSD data every 10ms to the named pipes.

Basic C++ Configuration
~~~~~~~~~~~~~~~~~~~~~~~

::

    // Configure dual-band spectrum streaming
    SpectrumPipeStreamerHelper streamer24;
    streamer24.SetBasePath("/tmp/ns3-spectrum-2.4ghz/");
    streamer24.SetChannel(channel24ghz);
    streamer24.SetFrequencyRange(2.4e9, 100e3, 1001);
    streamer24.SetNoiseFloor(-100);
    streamer24.SetStreamInterval(MilliSeconds(10));
    streamer24.InstallOnNode(apNode);

    SpectrumPipeStreamerHelper streamer5;
    streamer5.SetBasePath("/tmp/ns3-spectrum-5ghz/");
    streamer5.SetChannel(channel5ghz);
    streamer5.SetFrequencyRange(5.15e9, 1e6, 1001);
    streamer5.SetNoiseFloor(-100);
    streamer5.SetStreamInterval(MilliSeconds(10));
    streamer5.InstallOnNode(apNode);

Dual-Band Monitoring Example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

    // Monitor both 2.4 GHz and 5 GHz bands
    SpectrumPipeStreamerHelper streamer24;
    streamer24.SetBasePath("/tmp/ns3-spectrum-2.4ghz/");
    streamer24.SetChannel(channel24ghz);
    streamer24.SetFrequencyRange(2.4e9, 100e3, 1001);
    streamer24.InstallOnNode(apNode);

    SpectrumPipeStreamerHelper streamer5;
    streamer5.SetBasePath("/tmp/ns3-spectrum-5ghz/");
    streamer5.SetChannel(channel5ghz);
    streamer5.SetFrequencyRange(5.17e9, 100e3, 3301);
    streamer5.InstallOnNode(apNode);

Python Reader Example
~~~~~~~~~~~~~~~~~~~~~

::

    import struct
    import os

    pipe_path = "/tmp/ns3-spectrum/node-0.pipe"

    # Create FIFO if it doesn't exist
    if not os.path.exists(pipe_path):
        os.mkfifo(pipe_path)

    # Open pipe for reading (blocks until writer connects)
    with open(pipe_path, 'rb') as pipe:
        while True:
            # Read header
            header = pipe.read(16)  # 4 + 8 + 4 bytes
            if not header:
                break

            node_id, timestamp, num_values = struct.unpack('Idi', header)

            # Read PSD values
            psd_data = pipe.read(8 * num_values)
            psd_values = struct.unpack(f'{num_values}d', psd_data)

            # Process data (ML inference, plotting, etc.)
            process_spectrum(node_id, timestamp, psd_values)

Helpers
~~~~~~~

**SpectrumPipeStreamerHelper Methods:**

- ``SetBasePath(std::string basePath)``: Set directory where pipe files will be created
- ``SetChannel(Ptr<SpectrumChannel> channel)``: Set the spectrum channel to monitor
- ``SetFrequencyRange(double startFreq, double resolution, uint32_t numBins)``: Configure frequency parameters
- ``SetNoiseFloor(double noisePowerDbm)``: Set noise floor for spectrum analyzer
- ``SetStreamInterval(Time interval)``: Set minimum time between PSD transmissions
- ``InstallOnNode(Ptr<Node> node)``: Install spectrum analyzer and streamer on single node
- ``InstallOnNodes(NodeContainer nodes)``: Install on multiple nodes
- ``GetStreamer(uint32_t nodeId)``: Retrieve streamer instance for specific node
- ``GetPipePath(uint32_t nodeId)``: Get pipe path for specific node

Attributes
~~~~~~~~~~

**SpectrumPipeStreamer Attributes:**

- ``BasePath`` (String): Base directory for pipe files (default: "/tmp/ns3-spectrum/")
- ``NodeId`` (UInteger): Node identifier used in pipe filename
- ``StreamInterval`` (Time): Minimum interval between PSD transmissions (default: 100ms)

Traces
~~~~~~

The module does not define new trace sources but connects to existing spectrum analyzer traces:

- ``/NodeList/[nodeId]/DeviceList/[devId]/$ns3::NonCommunicatingNetDevice/Phy/AveragePowerSpectralDensityReport``: PSD trace from SpectrumAnalyzer

Examples and Tests
------------------

spectrum-pipe-streamer-example.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Complete RRM simulation with dual-band spectrum streaming and realistic interference scenarios.

**Interference Sources:**

- **2.4 GHz Band**: WiFi (Channel 6), Bluetooth (frequency hopping), Microwave oven (pulsed @ 2.45 GHz), Zigbee (Channel 15), Cordless phone (DECT-like hopping)
- **5 GHz Band**: WiFi (variable channels/bandwidth), Radar (5.25-5.725 GHz with long continuous segments and periodic bursts)

**Streaming Configuration:**

- 2.4 GHz: 2.400-2.5001 GHz (1001 bins @ 100 kHz resolution)
- 5 GHz: 5.150-6.150 GHz (1001 bins @ 1 MHz resolution)
- Streaming interval: 10ms (configurable via ``--streamInterval``)
- Simulation time: 10 seconds (configurable via ``--simTime``)

receiver.py
~~~~~~~~~~~

MAB/PPO agent for intelligent channel selection and dwell time optimization.

**Components:**

- **Multi-Armed Bandit**: Pairwise Discounted Beta Thompson Sampling for (band, channel) selection (28 arms: 2 bands × 14 channels)
- **PPO Agent**: Proximal Policy Optimization for dynamic dwell time selection (50-200ms range)
- **Socket Server**: Dual TCP sockets for canvas reception (port 65430) and feedback transmission (port 65431)

**Decision Process:**

1. Receives dual-band canvases (50 × 1001 each)
2. Thompson Sampling selects (band, channel) pair
3. PPO selects dwell time based on selected channel window
4. Sends decision back to main_live_handler

main_live_handler.py
~~~~~~~~~~~~~~~~~~~~

Pipe reader, canvas builder, and EWMA monitor for spectrum activity detection.

**Key Functions:**

- Dual-band pipe reading from ``/tmp/ns3-spectrum-2.4ghz/`` and ``/tmp/ns3-spectrum-5ghz/``
- Canvas building: buffers 50 PSD frames per band
- EWMA monitoring: dual exponentially weighted moving average for change detection
- CNN triggering: runs inference when EWMA crossover detected
- Dwell timer management: enforces minimum 1-second dwell time

**EWMA Parameters:**

::

    alpha_fast = 0.25   # Fast-moving average
    alpha_slow = 0.05   # Slow-moving average
    baseline = -96.92   # Noise floor (dBm)
    offset = 5          # Detection sensitivity

**Output:**

- ``pipeline.log``: Main handler execution log
- ``inference_output/``: CNN detection results (JSON format)
- ``tr_files/``: Time-frequency-power data for spectrograms

spectrum-pipe-streamer-test.cc
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Basic test suite placeholder. Currently does not contain functional tests.

Validation
----------

The complete pipeline has been tested with:

- Dual-band spectrum streaming (2.4 GHz and 5 GHz simultaneously)
- Multiple interference sources (WiFi, Bluetooth, Zigbee, Radar, Microwave, Cordless Phone)
- MAB channel selection with Thompson Sampling (28 arms)
- PPO dwell time optimization (50-200ms range)
- CNN-based interference classification (bay2.pt and 5ghz.pt models)
- EWMA-triggered inference (reduces computational overhead)
- Long-running simulations (10+ seconds)

**Performance Metrics:**

- NS-3 streaming: ~100 packets/sec/band (@ 10ms interval)
- Canvas generation: 20 canvases/sec/band (@ 50ms buffer)
- MAB decisions: 20 decisions/sec
- CNN inference latency: 50-200ms (GPU-accelerated)
- Pipe read latency: <1ms (blocking I/O)

Performance Characteristics
---------------------------

**Latency:**

- Low latency for local IPC (microseconds to milliseconds)
- Pipe buffer typically 64KB on Linux (OS-dependent)
- Blocking occurs when buffer fills, slowing simulation

**Throughput:**

Example: 1001 bins at 100ms interval → ~80 KB/s per node

::

    Packet size = 16 + (1001 × 8) = 8024 bytes
    Packets/sec = 10 (at 100ms interval)
    Throughput ≈ 80 KB/s per node

**Blocking Implications:**

- If the reader is slower than the simulation, the simulation will block during pipe writes
- This ensures no data loss but may slow simulation progress
- Optimize reader performance to minimize blocking
- Adjust ``StreamInterval`` to reduce data volume if blocking is problematic

**Best Practices:**

1. Start reader processes before simulation
2. Use buffered reading on Python side
3. Process data asynchronously (separate thread for inference)
4. Monitor pipe buffer usage (``/proc/sys/fs/pipe-max-size`` on Linux)
5. Adjust streaming interval based on reader capacity
6. Use multi-processing for multiple nodes (one reader per pipe)

References
----------

[1] ns-3 Spectrum Model Documentation: https://www.nsnam.org/docs/models/html/spectrum.html

[2] POSIX Named Pipes (FIFOs): https://man7.org/linux/man-pages/man7/fifo.7.html

[3] Thompson Sampling for Multi-Armed Bandits: Agrawal, S., & Goyal, N. (2012). Analysis of Thompson Sampling for the Multi-armed Bandit Problem. COLT 2012.

[4] Proximal Policy Optimization: Schulman, J., Wolski, F., Dhariwal, P., Radford, A., & Klimov, O. (2017). Proximal Policy Optimization Algorithms. arXiv:1707.06347.

[5] Exponentially Weighted Moving Average (EWMA) for Change Detection: Roberts, S. W. (1959). Control Chart Tests Based on Geometric Moving Averages. Technometrics, 1(3), 239-250.

[6] Deep Learning for RF Interference Classification: Wong, L. J., Michaels, A. J., & Headley, W. C. (2019). Specific Emitter Identification Using Convolutional Neural Network-Based IQ Imbalance Estimators. IEEE Access.

[7] IEEE 802.11 Wireless LAN Standards: IEEE Std 802.11-2020.

[8] Related |ns3| modules:

- ``spectrum``: Core spectrum analyzer implementation
- ``spectrum-socket-streamer``: UDP-based spectrum streaming
- ``spectrogram-generation-helper``: Offline spectrogram generation
- ``final-simulation``: RRM simulation platform
