# custom-ns-3

IEEE 802.11k/v Radio Resource Management platform built on ns-3.46.1. Implements the full 802.11k/v protocol stack, dual-radio scanning infrastructure, virtual interferers, and a closed-loop reinforcement learning optimization pipeline over Kafka.

23 contrib modules. ~86k lines of C++. 16 test suites. Modifications to `src/wifi/` for programmatic roaming and channel switching.

Built for Inter IIT Tech Meet 14.0 (Gold Medal, Arista Networks WiFi RRM problem statement).

## What was implemented

**IEEE 802.11k (Radio Resource Measurement)**
- Link Measurement Request/Report with TPC elements, RCPI/RSNI conversion per IEEE 802.11-2016
- Neighbor Report Request/Response with `NeighborReportElement` (`WifiInformationElement`, IE 52)
- Beacon Measurement Request and Beacon Report with `BeaconReportElement` (IE 39)
- Core information elements upstreamed to ns-3-dev as MRs !2788--!2791 (part of issue #1311)

**IEEE 802.11v (Wireless Network Management)**
- BSS Transition Management Request/Response with candidate AP lists, preference values, disassociation timers
- Reason codes: LOW_RSSI, HIGH_LOAD, BETTER_AP_FOUND, AP_UNAVAILABLE
- Auto-roaming orchestrator chaining 802.11k measurement with 802.11v steering

**Scanning infrastructure**
- Dual-PHY architecture: dedicated scanning radio hops channels while operating radio maintains association. Models real enterprise off-channel scanning without disrupting data
- UnifiedPhySniffer: single `MonitorSnifferRx` callback per PHY with subscription-based dispatch. Early 2-byte frame classification, lazy parsing, object pooling. Eliminates redundant processing when multiple protocols share a PHY

**StaWifiMac source modifications** (`src/wifi/model/sta-wifi-mac.{h,cc}`)
- Programmatic roaming: `InitiateRoaming()`, `AssociateToAp()`, `ForcedDisassociate()`
- Link quality accessors: `GetCurrentRssi()`, `GetCurrentSnr()`, `GetCurrentBssid()`
- Background scanning: `EnableBackgroundScanning()`, `TriggerBackgroundScan()`, `GetScanResults()`
- TracedCallbacks for roaming initiation/completion, background scan, link quality updates
- 2.4 GHz channel ambiguity fix: explicit `width=20` for channels 1--14 to resolve DSSS/OFDM/40MHz multi-definition conflict in ns-3's channel database

**RRM and optimization**
- Channel scoring (ACS/DCS): ranks channels by BSSID count, average RSSI, non-WiFi interference, spectral overlap
- Power scoring (RACEBOT + OBSS/PD): BSS Color-aware TX power control with fast adaptation within RL-set bounds
- Load balancing: monitors per-AP channel utilization, triggers BSS-TM to offload low-RSSI STAs
- Rollback manager: saves network state before changes, auto-reverts if score drops below threshold

**Virtual interferers**
- Microwave, Bluetooth, radar (DFS trigger), cordless phone, ZigBee
- Computes CCA percentage, packet loss probability, and DFS events from distance and channel overlap
- No `SpectrumChannel` overhead -- analytical model with configurable duty cycles and schedules

**RL integration**
- PPO + Graph Attention Network for channel and power decisions
- Kafka closed-loop: `ns3-metrics` topic (simulation to agent), `optimization-commands` topic (agent to simulation)
- Slow/fast loop architecture: RL handles strategic decisions (seconds), RACEBOT handles fast adaptation (milliseconds)

**Simulation infrastructure**
- JSON-driven scenario configuration: AP positions, STA waypoints, interferer schedules, scoring weights
- Waypoint mobility with configurable velocity and dwell times
- Spectrum analysis: spectrogram generation, spectrum logging, real-time pipe streaming
- Visualization dashboard (Next.js frontend + FastAPI backend)

## Contrib modules

| Module | Purpose | ns-3 patterns |
|--------|---------|---------------|
| `beacon-neighbor-protocol-11k` | Neighbor Report and Beacon Report protocols | `WifiInformationElement`, `TypeId`, `TracedCallback` |
| `link-protocol-11k` | Link Measurement protocol (RCPI, RSNI, link margin) | `Object`, `TypeId`, `TracedCallback` |
| `bss_tm_11v` | BSS Transition Management (client steering) | `Object`, `TypeId`, action frame serialization |
| `unified-phy-sniffer` | Centralized PHY callback dispatcher with subscription API | `Object`, `TypeId`, bitwise `FrameInterest` flags |
| `dual-phy-sniffer` | Dual-radio off-channel beacon scanning | `NetDeviceContainer`, `WifiHelper`, channel hopping |
| `auto-roaming-kv` | Automated roaming orchestrator chaining 802.11k/v | `Object`, `TypeId`, `Simulator::Schedule` |
| `lever-api` | Runtime PHY parameter control (TX power, channel, CCA) | `Application`, `TypeId`, traced attributes |
| `channel-scoring` | ACS/DCS channel ranking algorithm | Scoring weights, width-aware filtering |
| `power-scoring` | RACEBOT TX power control with OBSS/PD | BSS Color via HE-SIG-A, RSSI histograms, MCS EWMA |
| `virtual-interferer` | Non-WiFi interference sources (microwave, BT, radar, etc.) | Subclass hierarchy, analytical RF model |
| `wifi-cca-monitor` | PHY state and channel utilization tracking | `WifiPhy` state callbacks, sliding window |
| `kafka-producer` | Streams per-AP/per-STA metrics to Kafka as JSON | `Application`, periodic scheduling |
| `kafka-consumer` | Receives optimization commands from RL agent via Kafka | `Application`, JSON parsing, callbacks |
| `simulation-events` | Event logging and Kafka streaming for observability | Roaming, BSS-TM, channel/power change events |
| `simulation-helper` | Factory methods for wiring protocols across APs/STAs | Static helpers, dependency injection |
| `final-simulation` | LoadBalanceHelper + AdaptiveUdpApplication | `Application`, EWMA smoothing, BSS-TM triggers |
| `waypoint-simulation` | JSON-driven mobility and scenario definition | `WaypointMobilityModel`, config parsing |
| `sta-channel-hopping` | Orphaned STA recovery (reconnect to best AP by SNR) | Disassociation callbacks, beacon cache lookup |
| `rollback-manager` | Auto-revert when optimization degrades performance | State snapshots, score comparison, cooldown |
| `spectrogram-generation` | Spectrum heatmap generation | PSD visualization |
| `spectrum-analyser-logger` | Spectrum power logging to files | Offline analysis |
| `spectrum-pipe-streamer` | Real-time spectrum data via named pipes | External tool integration |
| `spectrum-shadow` | Parallel spectrum-level observation | `SpectrumChannel`, Kafka streaming |

## ns-3 source modifications

Changes to `src/wifi/` with full backward compatibility:

| File | Change | Why |
|------|--------|-----|
| `sta-wifi-mac.{h,cc}` | Roaming APIs, link quality accessors, background scanning, TracedCallbacks | 802.11v BSS-TM requires programmatic roaming; 802.11k Link Measurement needs signal quality access |
| `ap-wifi-mac.cc` | `SwitchChannel()` override propagating channel changes to all associated STAs | Coordinated channel switching without CSA (models the end result) |
| `wifi-mac.{h,cc}` | Base `SwitchChannel()` with explicit band/width handling; friend declaration for `WifiStaticSetupHelper` | 2.4 GHz channels have multiple definitions (DSSS 22MHz, OFDM 20MHz, OFDM 40MHz); `width=0` causes runtime error |
| `wifi-static-setup-helper.{h,cc}` | Static association bypassing management frame exchange | Fast simulation setup for large topologies |

## Testing

16 test suites following ns-3's test framework (`TestSuite`, `TestCase`):

- **Serialization tests**: `NeighborReportElement`, `BeaconReportElement`, `TpcReportElement` round-trip serialization/deserialization
- **Protocol tests**: BSS-TM parameters, candidate lists, response handling, reason code conversion
- **Signal conversion**: RCPI = 2*(RSSI+110), RSNI = 2*SNR, `ConvertRcpiToDbm()`, `ConvertRsniToDb()`
- **Infrastructure tests**: `FrameInterest` bitwise flags, `ParsedFrameContext` signal fields, `FrameType` enum uniqueness
- **Module tests**: Dual-PHY configuration, virtual interferer models, channel hopping, CCA monitoring, waypoint parsing

See [`IEEE_802.11kv_Implementation.md`](IEEE_802.11kv_Implementation.md) for detailed per-module test tables.

## Build

```bash
./ns3 configure --enable-examples
./ns3 build
```

Requires ns-3.46.1 dependencies plus `librdkafka-dev` and `rapidjson-dev` for the Kafka modules.

For the full pipeline (Kafka, RL agent, visualization), see [`RUN_INSTRUCTIONS.md`](RUN_INSTRUCTIONS.md).

## Project context

This platform was built for the **Inter IIT Tech Meet 14.0** (Arista Networks problem statement on WiFi Radio Resource Management). Won Gold Medal.

The simulation dataset generated using ns-3's `MultiModelSpectrumChannel` was used in the research paper "HERMES: A Unified Sensing Orchestrator for Third-Radio WiFi Sensing" (submitted to ICIP 2026, under review).

## Further documentation

- [`IEEE_802.11kv_Implementation.md`](IEEE_802.11kv_Implementation.md) -- protocol implementation details with full API reference
- [`MODULE_REFERENCE.md`](MODULE_REFERENCE.md) -- module descriptions and architecture overview
- [`RUN_INSTRUCTIONS.md`](RUN_INSTRUCTIONS.md) -- step-by-step instructions for running the full pipeline
