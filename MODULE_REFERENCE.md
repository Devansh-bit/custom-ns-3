# NS-3 WiFi Optimization Platform - Module Reference

A comprehensive WiFi network optimization platform combining ns-3 simulation with reinforcement learning for intelligent network management.

---

## System Overview

This platform provides an end-to-end framework for WiFi 6 (802.11ax) network optimization through high-fidelity ns-3 packet-level simulation, IEEE 802.11k/v protocol implementation for intelligent roaming, reinforcement learning (PPO + GAT) for channel/power optimization, Apache Kafka for real-time closed-loop control, and virtual interferer framework for realistic RF environment simulation.

---

## Module Reference

### 1. lever-api

Dynamic WiFi PHY configuration API for runtime parameter changes. The LeverConfig class provides traced configuration for TX power, channel, CCA threshold, and receive sensitivity. The LeverApi application listens to LeverConfig changes and applies them to the WiFi PHY in real-time. Supports smart channel switching that auto-detects band and width from channel number.

---

### 2. dual-phy-sniffer

Dual-radio architecture mimicking enterprise WiFi deployments. One radio handles data transmission while a dedicated scanning radio monitors beacons across multiple channels. Provides channel hopping, beacon caching, and multi-channel spectrum awareness without interrupting client service.

---

### 3. unified-phy-sniffer

Centralized MonitorSnifferRx dispatcher that consolidates multiple PHY callbacks into a single efficient callback. Provides early frame filtering (checks only 2 bytes before processing), lazy parsing (only parses needed fields), and object pooling to eliminate per-packet memory allocations. Subscribers can register for beacons, action frames, or data frames with optional destination filtering.

---

### 4. beacon-neighbor-protocol-11k

IEEE 802.11k Beacon and Neighbor Report protocol implementation. The Neighbor Protocol enables APs to build neighbor tables by scanning and STAs to request neighbor reports from their AP. The Beacon Protocol allows STAs to measure and report beacon information from neighboring APs. Includes RSSI-to-RCPI and SNR-to-RSNI conversion utilities per IEEE specification.

---

### 5. link-protocol-11k

IEEE 802.11k Link Measurement protocol for peer-to-peer link quality assessment. Enables measurement of RCPI (received channel power), RSNI (signal-to-noise), transmit power, and link margin between any two WiFi devices. Used to monitor current link quality and trigger roaming when degradation is detected.

---

### 6. bss_tm_11v

IEEE 802.11v BSS Transition Management for network-controlled roaming. Allows APs to request stations to transition to a different AP. Supports candidate AP lists with preference values, disassociation timers, and reason codes (LOW_RSSI, HIGH_LOAD, AP_UNAVAILABLE, ESS_DISASSOCIATION). Enables load balancing and intelligent client steering.

---

### 7. auto-roaming-kv

Automated roaming orchestrator that chains IEEE 802.11k/v protocols together. Continuously monitors link quality, triggers neighbor discovery when RSSI drops below threshold, collects beacon measurements from candidates, and initiates BSS transition to the best available AP. Provides hands-free intelligent roaming without manual intervention.

---

### 8. sta-channel-hopping

STA connection recovery for orphaned clients. Monitors disassociation events and automatically reconnects to the best available AP based on SNR. Queries the beacon cache to find APs across all channels, handles emergency reconnection for high packet loss scenarios, and includes cooldown to prevent thrashing.

---

### 9. wifi-cca-monitor

WiFi PHY state and channel utilization monitoring. Tracks time spent in IDLE, TX, RX, and CCA_BUSY states using a sliding window. Classifies CCA busy events as WiFi-caused (preamble detected) or non-WiFi interference. Provides separate WiFi and non-WiFi utilization percentages for intelligent optimization decisions.

---

### 10. power-scoring

Dynamic TX power control using the RACEBOT algorithm with OBSS (Overlapping BSS) detection. Uses BSS Color from 802.11ax to distinguish intra-BSS from inter-BSS frames. Tracks RSSI histograms and MCS EWMA per AP. Adjusts TX power and OBSS/PD threshold based on interference levels. Integrates with RL by accepting power bounds from the slow loop while RACEBOT handles fast adaptation within those bounds.

---

### 11. channel-scoring

Automatic/Dynamic Channel Selection (ACS/DCS) scoring algorithm. Evaluates channels based on four factors: BSSID count (neighbor density), average RSSI (interference strength), non-WiFi interference percentage, and spectral overlap with neighbors. Produces ranked channel recommendations. Integrates with RL by filtering scores to channels valid for the assigned width (20/40/80 MHz).

---

### 12. virtual-interferer

Simulates non-WiFi interference sources without computationally expensive spectrum calculations. Base VirtualInterferer class with subclasses for specific device types: MicrowaveInterferer (2.4 GHz, 50% duty cycle at 60Hz), BluetoothInterferer (frequency hopping), RadarInterferer (5 GHz, triggers DFS), CordlessInterferer, and ZigbeeInterferer. Calculates interference effects including CCA percentage, packet loss probability, and DFS triggers based on distance and channel overlap.

---

### 13. kafka-producer

Streams simulation metrics to Apache Kafka for external optimization. Periodically collects per-AP metrics including channel, utilization (WiFi vs non-WiFi), TX power, BSS color, and per-STA connection metrics (latency, jitter, throughput, RSSI, MCS). Also includes scanning radio data with per-channel neighbor information. Serializes everything to JSON for the RL agent.

---

### 14. kafka-consumer

Receives optimization commands from the external RL agent via Apache Kafka. Parses JSON messages containing per-AP parameters (channel, width, TX power). Triggers registered callbacks to apply configuration changes to the simulation. Optionally resets FlowMonitor statistics after applying changes to measure impact cleanly.

---

### 15. simulation-events

Simulation event logging and Kafka streaming for observability. Logs roaming events, BSS-TM requests/responses, channel changes, power changes, and other significant simulation events. Streams events to Kafka for external monitoring, debugging, and analysis.

---

### 16. simulation-helper

Factory methods and helper utilities for rapid scenario development. Provides static methods to setup dual-PHY sniffers, neighbor/beacon protocols, BSS-TM helpers, and auto-roaming with proper dependency wiring. Handles the complexity of installing protocols across multiple APs and STAs with correct sniffer sharing.

---

### 17. final-simulation

Complete simulation infrastructure including the LoadBalanceHelper and AdaptiveUdpApplication. LoadBalanceHelper monitors AP channel utilization and triggers BSS-TM to offload low-RSSI STAs to less loaded APs when utilization exceeds threshold. Tracks per-STA RSSI using EWMA smoothing. AdaptiveUdpApplication provides UDP traffic with TCP-like congestion control for realistic traffic patterns.

---

### 18. waypoint-simulation

Configuration-driven mobility and scenario definition. Parses JSON configuration files specifying AP positions, channels, and STA waypoints. Provides waypoint-based mobility where STAs move between predetermined points for reproducible movement patterns. Enables rapid iteration on network topologies without code changes.

---

### 19. spectrogram-generation

Spectrum visualization and analysis. Generates spectrum heatmaps showing power spectral density across frequency and time. Useful for visualizing interference patterns and channel conditions.

---

### 20. spectrum-analyser-logger

Spectrum power logging and analysis. Logs Power Spectral Density measurements to files for offline analysis. Captures detailed spectrum data for research and debugging.

---

### 21. spectrum-pipe-streamer

Real-time spectrum data streaming via named pipes. Enables external visualization tools to consume live spectrum data from the running simulation.

---

### 22. spectrum-shadow

Parallel spectrum-focused simulation. Provides spectrum-level observations without full protocol execution overhead. SpectrumKafkaProducer streams spectrum measurements to Kafka for external processing.

---

### 23. rollback-manager

Automatic network state rollback when optimization degrades performance. Saves network state (TX power, OBSS/PD, channel, network score) before any change. After a configurable evaluation period, compares new score to saved score. If performance dropped more than 90% (configurable), automatically triggers rollback via registered callback. Includes cooldown to prevent oscillation and grace period at simulation start.

---

## Architecture Layers

**Intelligence Layer (External Python)**: PPO + GAT RL Agent with Graph Attention Network encoder and dual-head actor for channel and power decisions. EWMA state provider for metric smoothing and dynamic graph coloring for interference-aware channel assignment.

**Messaging Layer (Apache Kafka)**: ns3-metrics topic carries simulation metrics to the RL agent. optimization-commands topic carries channel/power decisions back to the simulation.

**Simulation Layer (ns-3 C++)**: All contrib modules above plus modified WiFi MAC layer (StaWifiMac, ApWifiMac) for programmatic roaming and coordinated channel switching. Spectrum optimizations using LRU caching and batch processing.

---

## Slow-Fast Loop Architecture

**Fast Loop (Milliseconds)**: ns-3 protocols and RACEBOT power control handle protocol decisions and immediate responses within the simulation.

**Slow Loop (Seconds)**: RL Agent via Kafka makes strategic optimization decisions based on aggregated metrics, learning optimal policies over time.

---

## Key Integration Patterns

**Protocol Chain (802.11k/v Roaming)**: Link Measurement detects degradation, Neighbor Request gets candidate list, Beacon Request measures candidates, BSS-TM initiates transition, STA roams to new AP.

**Power Optimization Chain**: HeSigA callback captures RSSI and BSS Color, OBSS Tracker maintains histograms, RACEBOT algorithm calculates optimal power, PHY applies TX power adjustment.

**Channel Optimization Chain**: Scanning radio collects beacon data, Channel Scoring calculates per-channel scores, RL width constraints filter valid channels, best channel is selected and applied.

**Rollback Protection Chain**: SaveState captures pre-change state, change is applied, EvaluateAndRollback checks score after stabilization, rollback triggered if score dropped severely.
