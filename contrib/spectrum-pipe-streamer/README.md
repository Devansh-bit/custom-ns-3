# Spectrum Pipe Streamer: Intelligent Dual-Band Channel Selection

Real-time spectrum sensing and adaptive channel selection system combining NS-3 network simulation, CNN-based interference detection, and Multi-Armed Bandit (MAB) reinforcement learning for dynamic spectrum access.

## Overview

This module implements a complete pipeline for intelligent wireless channel selection:

- **NS-3 Simulation**: Generates realistic WiFi/Bluetooth/Zigbee/Microwave/Radar interference across 2.4 GHz and 5 GHz bands
- **Real-Time Streaming**: Streams Power Spectral Density (PSD) data via named pipes with blocking flow control
- **EWMA Monitoring**: Detects spectrum activity changes using dual Exponentially Weighted Moving Average
- **CNN Detection**: Classifies interference technologies (WiFi, Bluetooth, Zigbee, Microwave, Radar) from spectrograms
- **MAB/PPO Learning**: Thompson Sampling selects optimal (band, channel) pairs; PPO optimizes dwell times

### Key Features

- **Dual-band monitoring**: Simultaneous 2.4 GHz and 5 GHz spectrum analysis
- **Blocking named pipes**: Ensures data integrity and simulation synchronization
- **EWMA-triggered CNN**: Runs inference only when spectrum activity changes (efficient)
- **Pairwise MAB**: 28 arms (2 bands × 14 channels) with Beta Thompson Sampling
- **PPO dwell control**: Learns sensing duration dynamically (50-200ms range)
- **Thread-safe processing**: Independent handlers for dual-band streams

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    NS-3 SIMULATION (C++)                                │
│  spectrum-pipe-streamer-example.cc                                      │
│  ├─ Generates WiFi/BT/Zigbee/Microwave/Radar interference              │
│  ├─ 2.4 GHz: 2.4-2.5 GHz (1001 bins @ 100 kHz resolution)             │
│  └─ 5 GHz: 5.15-6.15 GHz (1001 bins @ 1 MHz resolution)               │
└─────────────────────────────────────────────────────────────────────────┘
                  │ (Named Pipes - Blocking I/O)
                  │ /tmp/ns3-spectrum-2.4ghz/node-0.pipe
                  │ /tmp/ns3-spectrum-5ghz/node-0.pipe
                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              PIPE READER & CANVAS BUILDER (Python)                      │
│  main_live_handler.py                                                   │
│  ├─ Reads binary PSD data from dual-band pipes (10ms intervals)        │
│  ├─ Builds 50-frame canvases (50ms worth of spectrum data)             │
│  ├─ Performs EWMA-based change detection on selected channel           │
│  ├─ Triggers CNN inference on EWMA crossover (20-frame window)         │
│  └─ Manages dwell timer and channel selection lifecycle                │
└─────────────────────────────────────────────────────────────────────────┘
                  │ (TCP Socket: Port 65430)
                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    MAB/PPO AGENT (Python)                               │
│  receiver.py                                                            │
│  ├─ Receives dual-band canvases (50 × 1001 each)                       │
│  ├─ Thompson Sampling selects (band, channel) pair                     │
│  ├─ PPO selects dwell time (50-200ms)                                  │
│  └─ Sends decision back via feedback socket (Port 65431)               │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Execution Order (CRITICAL)

The pipeline components must be started in this exact order:

### Step 1: Start MAB/PPO Receiver (FIRST)

```bash
cd contrib/spectrum-pipe-streamer/examples
python3 receiver.py
```

**Output:**
```
Waiting for canvas producer @ 65430...
Waiting for feedback consumer @ 65431...
```

The receiver listens on two sockets:
- **Port 65430**: Receives dual-band canvases from main_live_handler
- **Port 65431**: Sends MAB decisions back to main_live_handler

### Step 2: Start Main Live Handler (SECOND)

```bash
python3 main_live_handler.py
```

**Output:**
```
Connected to RL engine (canvas sender)
Connected to RL engine (feedback receiver)
[DUAL-BAND] Starting 2.4 GHz and 5 GHz pipe servers...
[2.4GHz] NS3 pipe streaming started at /tmp/ns3-spectrum-2.4ghz
[5GHz] NS3 pipe streaming started at /tmp/ns3-spectrum-5ghz
```

This component:
1. Connects to receiver.py sockets
2. Creates named pipes for NS-3 data
3. Waits for NS-3 simulation to begin streaming

### Step 3: Start NS-3 Simulation (LAST)

```bash
./ns3 run spectrum-pipe-streamer-example
```

**Output:**
```
╔═══════════════════════════════════════════════════════════╗
║  Complete RRM Simulation with Named Pipe Streaming       ║
╚═══════════════════════════════════════════════════════════╝

Installing spectrum analyzer with NAMED PIPE STREAMING...
  2.4 GHz Pipe base path: /tmp/ns3-spectrum-2.4ghz
  5 GHz Pipe base path: /tmp/ns3-spectrum-5ghz
Starting simulation for 10.0 seconds...
```

The simulation streams PSD data every 10ms to the named pipes.

---

## Component Details

### 1. NS-3 Simulation (spectrum-pipe-streamer-example.cc)

**Purpose**: Generates realistic multi-technology interference scenarios.

**Interference Sources:**

| Technology | Band | Frequency | Characteristics |
|------------|------|-----------|-----------------|
| WiFi | 2.4 GHz | Channel 6 (2.437 GHz) | 20 MHz bandwidth, periodic beacons |
| Bluetooth | 2.4 GHz | Frequency hopping | 79 channels, 1 MHz each, rapid switching |
| Microwave | 2.4 GHz | 2.45 GHz | Pulsed interference, high power |
| Zigbee | 2.4 GHz | Channel 15 (2.425 GHz) | 2 MHz bandwidth, low power |
| Cordless Phone | 2.4 GHz | Frequency hopping | DECT-like hopping pattern |
| WiFi | 5 GHz | Variable channels | 20/40/80 MHz bandwidth |
| Radar | 5 GHz | 5.25-5.725 GHz | Long continuous segments + periodic bursts |

**Streaming Configuration:**

```cpp
// 2.4 GHz Band
Start: 2.400 GHz
Resolution: 100 kHz
Bins: 1001
Span: 100.1 MHz

// 5 GHz Band
Start: 5.150 GHz
Resolution: 1 MHz
Bins: 1001
Span: 1000 MHz

Streaming Interval: 10 ms (configurable via --streamInterval)
Simulation Time: 10 seconds (configurable via --simTime)
```

**Binary Data Format:**

Each pipe write contains:
```
Header (16 bytes):
  [node_id: uint32_t (4 bytes)]
  [timestamp: double (8 bytes)]
  [num_values: uint32_t (4 bytes)]

Payload:
  [psd_value_0: double (8 bytes)]
  [psd_value_1: double (8 bytes)]
  ...
  [psd_value_1000: double (8 bytes)]
```

### 2. Main Live Handler (main_live_handler.py)

**Purpose**: Bridge between NS-3 simulation and MAB/PPO agents.

**Key Functions:**

#### a) Dual-Band Pipe Reading
- Reads from `/tmp/ns3-spectrum-2.4ghz/node-0.pipe` (2.4 GHz)
- Reads from `/tmp/ns3-spectrum-5ghz/node-0.pipe` (5 GHz)
- Independent threads for each band
- Binary unpacking: `struct.unpack('Idi', header)` → (node_id, timestamp, num_values)

#### b) Canvas Building
- Buffers 50 PSD frames per band (50ms @ 1ms effective resolution)
- Creates numpy arrays: `canvas_2_4.shape = (50, 1001)`, `canvas_5.shape = (50, 1001)`
- Combines into dictionary: `{0: canvas_2_4, 1: canvas_5}`
- Sends to receiver.py via socket (pickle serialization with 4-byte length header)

#### c) EWMA Monitoring
Once MAB selects a channel, monitors it continuously:

```python
# Dual EWMA parameters
alpha_fast = 0.25   # Fast-moving average
alpha_slow = 0.05   # Slow-moving average
baseline = -96.92   # Noise floor (dBm)
offset = 5          # Detection sensitivity

# For each PSD frame:
rssi = 10 * log10(sum(psd[f_start:f_end] - noise_floor) * 1000)
fast_ewma = alpha_fast × rssi + (1 - alpha_fast) × fast_ewma
slow_ewma = alpha_slow × (rssi + offset) + (1 - alpha_slow) × slow_ewma

# Trigger CNN when:
if (rssi > baseline) and (fast_ewma > slow_ewma):
    run_cnn_inference(collect_20_frames())
```

#### d) CNN Triggering
- Collects 20 PSD frames (20ms window)
- Generates time-frequency-power TR file
- Creates 256×256 spectrogram using `image_one.py`
- Runs CNN inference with band-specific model:
  - 2.4 GHz: `bay2.pt`
  - 5 GHz: `5ghz.pt`
- Decodes detections: technology, center_freq, bandwidth, confidence, duty_cycle

#### e) Dwell Timer Management
- Enforces minimum dwell time: 1 second
- Counts down each PSD frame (10ms per frame)
- Resets EWMA state when dwell expires
- Waits for next MAB decision

### 3. MAB/PPO Receiver (receiver.py)

**Purpose**: Intelligent (band, channel) selection and dwell time optimization.

**Architecture:**

#### Multi-Armed Bandit (MAB)
- **Algorithm**: Pairwise Discounted Beta Thompson Sampling
- **Action Space**: 28 arms (2 bands × 14 channels)
- **Channel Formulation**:
  ```python
  num_channels = 14  # IEEE WiFi channels
  channel_size = 20  # frequency bins per channel
  # Total spectrum divided into 14 × 20 = 280 bins
  ```
- **Pairwise Comparison**: 364 pairs (28 arms × 26 comparisons each / 2)
- **Beta Distribution**: Maintains (alpha, beta) for each pair
- **Thompson Sampling**: Samples Beta(alpha, beta) and selects arm with highest sample

#### Proximal Policy Optimization (PPO)
- **State**: Selected channel's 20-bin × 20-frame window (flattened to 400-dim vector)
- **Action Space**: 20 discrete actions → dwell times
  - Action 0 → 1 → 50ms
  - Action 19 → 20 → 200ms
  - Linear mapping: `dwell_ms = 50 + (action - 1) × 7.89`
- **Network**: ActorCritic with 2-layer MLP (128, 128 hidden units)
- **Hyperparameters**:
  - Learning rate: 3e-4
  - Gamma: 0.99
  - Lambda (GAE): 0.95
  - Clip epsilon: 0.2
  - Training epochs: 8
  - Batch size: 64

#### Decision Process
1. Receive dual-band canvas from main_live_handler
2. Thompson Sampling selects (band, channel_idx)
3. Extract selected channel window (20 bins × 50 frames)
4. PPO selects dwell_time (1-20 action space)
5. Compute reward from sensed energy: `reward = sum(psd_values)`
6. Update both agents
7. Send decision:
   ```python
   {
       "band": 0 or 1,
       "channel_idx": 0-13,
       "dwell_time": 1-20,
       "channel_range": (start_bin, end_bin)
   }
   ```

---

## Data Flow Summary

```
NS-3 (10ms) → Named Pipe → main_live_handler
                               ↓
                        Buffer 50 frames
                               ↓
                        Build Canvas (50 × 1001)
                               ↓
                        Send to receiver.py
                               ↓
                        MAB + PPO Decision
                               ↓
                        Feedback to main_live_handler
                               ↓
                        EWMA Monitor Selected Channel
                               ↓
                        Trigger CNN on Crossover
                               ↓
                        Log Detections
```

---

## Configuration & Parameters

### NS-3 Simulation
```bash
./ns3 run "spectrum-pipe-streamer-example --simTime=10 --streamInterval=10"
```

| Parameter | Default | Description |
|-----------|---------|-------------|
| `simTime` | 10.0 | Simulation duration (seconds) |
| `streamInterval` | 10 | Pipe write interval (milliseconds) |

### Canvas Building
```python
T = 50                 # Frames per canvas
TIME_RES = 0.001      # 1 ms time resolution
NUM_CHANNELS = 14     # IEEE WiFi channels
CHANNEL_SIZE = 20     # Bins per channel
```

### EWMA Detection
```python
baseline = -96.92     # Noise floor (dBm)
offset = 5            # Detection threshold offset
alpha_fast = 0.25     # Fast EWMA coefficient
alpha_slow = 0.05     # Slow EWMA coefficient
PAUSE_DURATION = 0.1  # CNN trigger cooldown (seconds)
```

### MAB/PPO Training
```python
# MAB
epsilon = 0.05        # Epsilon-greedy exploration

# PPO
state_dim = 400       # 20 bins × 20 frames
action_dim = 20       # Discrete dwell times
lr = 3e-4             # Learning rate
gamma = 0.99          # Discount factor
clip_eps = 0.2        # PPO clipping
```

---

## Dependencies

### Python Packages
```bash
pip install numpy torch scipy matplotlib pillow
```

### NS-3 Modules
```
core, network, spectrum, mobility
spectrum-pipe-streamer (this module)
```

### Pre-trained Models
Required for CNN inference:
- `bay2.pt`: 2.4 GHz interference detection (10.7 MB)
- `5ghz.pt`: 5 GHz interference detection (10.6 MB)

Place models in `contrib/spectrum-pipe-streamer/examples/` directory.

---

## Output & Logging

### Log Files
```
pipeline.log                    # Main handler execution log
spectrum_pipe_reader_csv.log    # Pipe reader debug log
mab_eval_output.log            # MAB evaluation metrics
```

### Inference Output
```
inference_output/
├── tr_files/              # Time-frequency-power data
│   └── cnn_2.4ghz_*.tr
├── output_*.json          # Detection results
└── tensors/
    └── *.npy             # Raw CNN predictions
```

### Detection Results Format
```json
{
  "detections": [
    {
      "technology": "wifi",
      "center_frequency_hz": 2437000000,
      "bandwidth_hz": 20000000,
      "band": "2.4GHz",
      "confidence": 0.92,
      "duty_cycle": 0.85
    }
  ]
}
```

---

## Performance Characteristics

### Throughput
- NS-3 streaming: ~100 packets/sec/band (@ 10ms interval)
- Canvas generation: 20 canvases/sec/band (@ 50ms buffer)
- MAB decisions: 20 decisions/sec
- CNN inference: Variable (EWMA-triggered)

### Latency
- Pipe read: <1ms (blocking I/O)
- Canvas socket RTT: 2-5ms
- EWMA computation: <0.1ms per frame
- CNN inference: 50-200ms (GPU-accelerated)
- MAB/PPO decision: 5-10ms

### Memory
- Canvas buffer: ~800 KB (2 bands × 50 frames × 1001 bins × 8 bytes)
- PSD frames: ~8 KB per frame
- CNN models: ~11 MB each

---

## Troubleshooting

### Simulation hangs at startup
**Cause**: Named pipes block until readers connect.
**Solution**: Start receiver.py and main_live_handler.py BEFORE running NS-3 simulation.

### "Broken pipe" error
**Cause**: Reader stopped while simulation was writing.
**Solution**: Restart receiver.py and main_live_handler.py, then run simulation again.

### Slow simulation
**Cause**: Reader not consuming data fast enough.
**Solution**:
- Increase `--streamInterval` (e.g., 50ms instead of 10ms)
- Use GPU for CNN inference
- Reduce canvas size (fewer frames)

### Socket connection refused
**Cause**: receiver.py not started first.
**Solution**: Follow execution order: receiver.py → main_live_handler.py → NS-3

### CNN model not found
**Cause**: Pre-trained models missing.
**Solution**: Download `bay2.pt` and `5ghz.pt` and place in `examples/` directory.

---

## Advanced Usage

### Custom Interference Scenarios

Edit `spectrum-pipe-streamer-example.cc` to modify interference:

```cpp
// Add custom WiFi interferer
WifiHelper wifi;
wifi.SetStandard(WIFI_STANDARD_80211ax);
NetDeviceContainer wifiDevices = wifi.Install(wifiNode);

// Configure frequency and bandwidth
YansWifiPhyHelper wifiPhy;
wifiPhy.Set("ChannelSettings", StringValue("{36, 80, BAND_5GHZ, 0}"));
```

### Custom MAB Algorithms

Modify `receiver.py` to implement different strategies:

```python
# Replace Thompson Sampling with UCB
def ucb_selection(data, t):
    ucb_values = [mean[i] + sqrt(2 * log(t) / count[i]) for i in range(28)]
    return argmax(ucb_values)
```

### Custom EWMA Parameters

Adjust detection sensitivity in `main_live_handler.py`:

```python
# More sensitive detection
alpha_fast = 0.35    # Faster response
offset = 3           # Lower threshold

# Less sensitive (fewer false positives)
alpha_fast = 0.15
offset = 8
```

---

## API Reference

### SpectrumPipeStreamer (C++)

```cpp
void SetBasePath(std::string basePath);
void SetNodeId(uint32_t nodeId);
void SetFrequencyMetadata(double startFreq, double resolution, uint32_t numBins);
void SetStreamInterval(Time interval);
void PsdCallback(Ptr<const SpectrumValue> psd);
std::string GetPipePath();
```

### SpectrumPipeStreamerHelper (C++)

```cpp
void SetBasePath(std::string basePath);
void SetChannel(Ptr<SpectrumChannel> channel);
void SetFrequencyRange(double startFreq, double resolution, uint32_t numBins);
void SetNoiseFloor(double noisePowerDbm);
void SetStreamInterval(Time interval);
void InstallOnNode(Ptr<Node> node);
void InstallOnNodes(NodeContainer nodes);
Ptr<SpectrumPipeStreamer> GetStreamer(uint32_t nodeId);
std::string GetPipePath(uint32_t nodeId);
```

---

## Building

```bash
# Configure NS-3
./ns3 configure --enable-examples --enable-tests

# Build module
./ns3 build spectrum-pipe-streamer

# Run example
./ns3 run spectrum-pipe-streamer-example
```

---

## License

This module follows the NS-3 license (GNU GPLv2).

---

## References

- IEEE 802.11 Wireless LAN Standards
- Thompson Sampling for Multi-Armed Bandits
- Proximal Policy Optimization (Schulman et al., 2017)
- Exponentially Weighted Moving Average (EWMA) for Change Detection
- Deep Learning for RF Interference Classification

---

## See Also

- `spectrum-socket-streamer`: UDP socket based streaming (non-blocking)
- `spectrum-analyzer`: NS-3 spectrum analyzer module
- `spectrogram-generation-helper`: Generate interferer signals
- `contrib/final-simulation`: RRM simulation platform
