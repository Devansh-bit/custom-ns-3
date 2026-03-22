# BSS Transition Management (IEEE 802.11v) Module

An ns-3 implementation of the BSS Transition Management (BSS TM) protocol defined in IEEE 802.11v-2011. This protocol enables network-controlled WiFi roaming by allowing Access Points to suggest or direct stations to transition to better candidate APs based on network-wide optimization criteria.

## Features

### BSS Transition Management Protocol
- **BSS TM Requests**: APs can send transition requests to STAs with ranked candidate AP lists
- **BSS TM Responses**: STAs respond with accept/reject status and target BSSID
- **Candidate Ranking**: Integrated ranking algorithms via `rankListManager` inheritance
- **Network-Controlled Roaming**: Override signal-strength decisions for load balancing or QoS
- **IEEE 802.11v Compliance**: Follows standard frame format (Section 7.4.8)

### Integration Capabilities
- **Standalone Operation**: Works with hardcoded candidate lists
- **802.11k Integration**: Can use beacon reports from `beacon-neighbor-protocol-11k` module
- **Channel Switching**: Integrates with `lever-api` for seamless channel transitions
- **Distribution System**: Supports bridged multi-AP deployments with CSMA backhaul

## Model Components

### BssTm11vHelper
Main helper class for BSS TM protocol operations.

**Key Methods:**
- `InstallOnAp(Ptr<WifiNetDevice>)` - Install protocol on AP (can send BSS TM requests)
- `InstallOnSta(Ptr<WifiNetDevice>)` - Install protocol on STA (can respond to requests)
- `SendBssTmRequest(Ptr<WifiNetDevice>, Mac48Address, std::vector<Mac48Address>)` - Manually trigger BSS TM request
- `SetRankingAlgorithm(...)` - Configure AP ranking/filtering algorithm (via rankListManager)

**Trace Sources:**
- `BssTmRequestSent(Mac48Address apAddr, Mac48Address staAddr, std::vector<Mac48Address> candidates)` - Fires when AP sends BSS TM request
- `BssTmResponseReceived(Mac48Address staAddr, Mac48Address apAddr, uint8_t statusCode, Mac48Address targetBssid)` - Fires when AP receives BSS TM response

### Data Structures

**BssTmParameters**: BSS TM request frame parameters
```cpp
struct BssTmParameters {
    uint8_t requestMode;           // Request mode flags (bit field)
    uint16_t disassociationTimer;  // Time until disassociation (0 = not specified)
    uint8_t validityInterval;      // Candidate list validity period
    Mac48Address targetBssid;      // Preferred target BSSID (optional)
    std::vector<Mac48Address> candidateList;  // Ranked list of candidate APs
};
```

**Request Mode Flags:**
- `Bit 0`: Preferred Candidate List Included
- `Bit 1`: Abridged
- `Bit 2`: Disassociation Imminent
- `Bit 3`: BSS Termination Included
- `Bit 4`: ESS Disassociation Imminent

**Status Codes** (in BSS TM Response):
- `0`: Accept
- `1`: Reject - Unspecified
- `2`: Reject - Insufficient beacon reports
- `6`: Reject - Candidate list provided but no suitable candidate
- `7`: Reject - No suitable candidates in list or scan results

## Example Usage

The module provides three example scenarios demonstrating different BSS TM use cases.

### Example 1: Basic BSS TM with Mobility

**File:** `bss_tm_11v-example.cc`

Simple scenario with mobility-triggered roaming:
- Single AP, single STA
- STA moves away using waypoint mobility
- Hardcoded candidate list (no 802.11k integration)
- Demonstrates basic BSS TM request/response exchange

**Run the example:**
```bash
./ns3 run bss_tm_11v-example
```

### Example 2: Distribution System with Seamless Roaming

**File:** `bss-tm-dummy-example.cc`

Advanced scenario with multi-AP infrastructure:
- 3 APs connected via 1Gbps CSMA backbone (Distribution System)
- Bridge devices connecting WiFi ↔ Wired segments
- UDP data transmission during roaming (demonstrates seamless handoff)
- Backend server on wired network
- Shows traffic continuity during BSS transitions

**Run the example:**
```bash
./ns3 run bss-tm-dummy-example
```

**Architecture:**
```
[Server] <--CSMA--> [Bridge-AP0] <--WiFi--> [STA]
                         |
                    CSMA Backbone
                    (1Gbps Switch)
                         |
           [Bridge-AP1]   [Bridge-AP2]
```

### Example 3: Network-Controlled Roaming (Override Signal Strength)

**File:** `bss-tm-force-low-rssi-roaming.cc`

Demonstrates BSS TM overriding signal-strength decisions:
- 3 APs on same channel (6, 2.4GHz) in linear placement
- Forces STA to roam to AP with WORSE RSSI
- Shows network control vs. autonomous roaming
- Useful for load balancing scenarios

**Run the example:**
```bash
./ns3 run bss-tm-force-low-rssi-roaming
```

**Command-line parameters** (example 3):
- `--nAps` - Number of access points (default: 3)
- `--nStas` - Number of stations (default: 1)
- `--simTime` - Simulation duration in seconds (default: 20.0)

## Integration

### Basic Usage (Standalone with Hardcoded Candidates)

```cpp
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

// Manually send BSS TM request with candidate list
std::vector<Mac48Address> candidates;
candidates.push_back(Mac48Address("00:00:00:00:00:02"));  // Candidate AP 1
candidates.push_back(Mac48Address("00:00:00:00:00:03"));  // Candidate AP 2

Simulator::Schedule(Seconds(5.0),
                    &BssTm11vHelper::SendBssTmRequest,
                    PeekPointer(bssTmHelper),
                    apDevice,
                    staAddress,
                    candidates);
```

### Advanced Usage (Integration with 802.11k Protocols)

```cpp
#include "ns3/bss_tm_11v-helper.h"
#include "ns3/beacon-protocol-11k-helper.h"
#include "ns3/neighbor-protocol-11k-helper.h"

// Setup 802.11k protocols first
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

// Chain the protocols: Neighbor Report → Beacon Report → BSS TM
void OnBeaconReportReceived(Mac48Address apAddr,
                             Mac48Address staAddr,
                             std::vector<BeaconReportData> reports)
{
    // Rank candidates based on RSSI/SNR from beacon reports
    std::vector<Mac48Address> rankedCandidates;

    // Sort by RSSI (convert RCPI to RSSI first)
    std::sort(reports.begin(), reports.end(),
              [](const BeaconReportData& a, const BeaconReportData& b) {
                  return a.rcpi > b.rcpi;  // Higher RCPI = better signal
              });

    for (const auto& report : reports) {
        rankedCandidates.push_back(report.bssid);
    }

    // Send BSS TM request with ranked candidates
    bssTmHelper->SendBssTmRequest(apDevice, staAddr, rankedCandidates);
}

beaconHelper->TraceConnectWithoutContext(
    "BeaconReportReceived",
    MakeCallback(&OnBeaconReportReceived));
```

### Trace Callback Handlers

```cpp
void OnBssTmRequestSent(Mac48Address apAddr,
                        Mac48Address staAddr,
                        std::vector<Mac48Address> candidates)
{
    std::cout << "BSS TM Request sent from " << apAddr
              << " to " << staAddr << "\n";
    std::cout << "Candidate APs (" << candidates.size() << "):\n";

    for (size_t i = 0; i < candidates.size(); i++) {
        std::cout << "  " << (i + 1) << ". " << candidates[i] << "\n";
    }
}

void OnBssTmResponseReceived(Mac48Address staAddr,
                              Mac48Address apAddr,
                              uint8_t statusCode,
                              Mac48Address targetBssid)
{
    std::cout << "BSS TM Response from " << staAddr
              << " to " << apAddr << "\n";

    if (statusCode == 0) {
        std::cout << "  Status: ACCEPT\n";
        std::cout << "  Target BSSID: " << targetBssid << "\n";
    } else {
        std::cout << "  Status: REJECT (code " << (int)statusCode << ")\n";
    }
}
```

## Protocol Sequence

### Standalone BSS TM Roaming
1. **Trigger**: Application detects need for roaming (RSSI threshold, timer, etc.)
2. **BSS TM Request**: AP sends ranked candidate list to STA
3. **BSS TM Response**: STA accepts or rejects, indicates target BSSID
4. **Reassociation**: STA initiates reassociation with target AP (handled by `StaWifiMac`)

### Integrated 802.11k/v Roaming Chain
1. **Link Monitoring**: STA monitors connection quality (via Link Measurement)
2. **Neighbor Discovery**: When RSSI drops, STA requests neighbor list
3. **Beacon Measurement**: AP requests beacon reports for discovered neighbors
4. **Ranking**: AP analyzes beacon reports and ranks candidates
5. **BSS TM Request**: AP sends ranked candidate list to STA
6. **Roaming Decision**: STA selects best candidate and initiates reassociation

## BSS TM vs. Autonomous Roaming

### Autonomous Roaming (802.11 Default)
- STA makes roaming decision based on local information (RSSI/SNR)
- No network coordination
- May cause load imbalance (all STAs roam to strongest AP)
- Cannot consider network-wide metrics

### Network-Controlled Roaming (802.11v BSS TM)
- AP provides optimization guidance based on network-wide view
- Can balance load across APs
- Can consider QoS requirements, channel utilization, backhaul capacity
- STA retains final decision (can reject suggestions)

## Integration with Other Modules

This module works well with:
- **beacon-neighbor-protocol-11k**: For dynamic neighbor discovery and beacon measurements
- **link-protocol-11k**: For continuous link quality monitoring
- **auto-roaming-kv**: For complete automated 802.11k/v roaming orchestration
- **lever-api**: For channel switching capabilities
- **dual-phy-sniffer**: For multi-channel scanning (when integrated with 802.11k)

## Dependencies

**Required Modules:**
- `src/wifi/` - WiFi module with StaWifiMac roaming support
- `src/mobility/` - For mobility models in examples

**Optional Integration:**
- `contrib/beacon-neighbor-protocol-11k/` - For dynamic candidate discovery
- `contrib/lever-api/` - For channel switching coordination

## Architecture Notes

### rankListManager Inheritance
The `BssTm11vHelper` inherits from `rankListManager` class, which provides:
- Candidate AP ranking algorithms
- Filtering logic based on multiple criteria
- Extensible architecture for custom ranking strategies

### Channel Switching
Integrates with `lever-api` for coordinated channel transitions during BSS TM:
- Pre-transition channel setup
- Synchronization between AP and STA
- Seamless handoff with minimal packet loss

## References

- IEEE 802.11v-2011: Wireless Network Management
- Section 7.4.8: BSS Transition Management Request/Response frames
- Section 7.3.2.62: BSS Transition Management Request element
- Section 10.23.12: BSS Transition Management procedures
