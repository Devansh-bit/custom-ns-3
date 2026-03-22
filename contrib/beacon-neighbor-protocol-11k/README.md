# Beacon and Neighbor Reporting Protocol (IEEE 802.11k)

An ns-3 implementation of the Neighbor Report and Beacon Report protocols defined in IEEE 802.11k-2016. These protocols enable WiFi stations to discover neighboring access points and measure their link quality for intelligent roaming decisions.

## Features

### Neighbor Report Protocol
- **Neighbor Report Requests**: STAs can query their associated AP for a list of neighboring APs
- **Neighbor Report Responses**: APs respond with BSSID, channel, regulatory class, PHY type, and BSSID info
- **Multi-channel Discovery**: Optional dual-PHY sniffer integration for automatic neighbor discovery across channels
- **IEEE 802.11k Compliance**: Follows standard frame format (Section 7.4.7)

### Beacon Report Protocol
- **Beacon Requests**: APs can request STAs to measure beacon frames from specific or all neighbors
- **Beacon Reports**: STAs respond with detailed measurements including:
  - RCPI (Received Channel Power Indicator)
  - RSNI (Received Signal to Noise Indicator)
  - Measurement duration and timing
  - Antenna identifiers
  - PHY and frame type information
- **Multi-channel Scanning**: Dual-PHY sniffer support for measuring beacons on different channels
- **IEEE 802.11k Compliance**: Follows standard frame format (Section 7.4.5)

### Utility Functions
- `RssiToRcpi()` / `RcpiToRssi()` - Convert between RSSI (dBm) and RCPI format
- `SnrToRsni()` / `RsniToSnr()` - Convert between SNR (dB) and RSNI format

## Model Components

### NeighborProtocolHelper
Main helper class for Neighbor Report protocol operations.

**Key Methods:**
- `SetNeighborTable(std::vector<ApInfo>)` - Configure static neighbor table for AP
- `SetDualPhySniffer(DualPhySnifferHelper*)` - Enable multi-channel neighbor discovery
- `InstallOnAp(Ptr<WifiNetDevice>)` - Install protocol on AP
- `InstallOnSta(Ptr<WifiNetDevice>)` - Install protocol on STA
- `SendNeighborReportRequest(Ptr<WifiNetDevice>, Mac48Address)` - STA requests neighbor list

**Trace Sources:**
- `NeighborReportReceived(Mac48Address staAddr, Mac48Address apAddr, std::vector<NeighborReportData>)` - Fires when STA receives neighbor report

### BeaconProtocolHelper
Main helper class for Beacon Report protocol operations.

**Key Methods:**
- `SetNeighborList(std::set<Mac48Address>)` - Configure which BSSIDs to measure
- `SetDualPhySniffer(DualPhySnifferHelper*)` - Enable multi-channel beacon scanning
- `InstallOnAp(Ptr<WifiNetDevice>)` - Install protocol on AP
- `InstallOnSta(Ptr<WifiNetDevice>)` - Install protocol on STA
- `SendBeaconRequest(Ptr<WifiNetDevice>, Mac48Address)` - AP requests beacon measurements from STA

**Trace Sources:**
- `BeaconReportReceived(Mac48Address apAddr, Mac48Address staAddr, std::vector<BeaconReportData>)` - Fires when AP receives beacon report

### Data Structures

**ApInfo**: Neighbor AP information
```cpp
struct ApInfo {
    Mac48Address bssid;
    std::string ssid;
    uint8_t channel;
    uint8_t regulatoryClass;
    uint8_t phyType;
    Vector position;
    uint8_t load;
    Time lastDiscovered;
};
```

**NeighborReportData**: Neighbor report element contents
```cpp
struct NeighborReportData {
    Mac48Address bssid;
    uint32_t bssidInfo;
    uint8_t channel;
    uint8_t regulatoryClass;
    uint8_t phyType;
};
```

**BeaconReportData**: Beacon measurement report contents
```cpp
struct BeaconReportData {
    Mac48Address bssid;
    uint8_t channel;
    uint8_t regulatoryClass;
    uint8_t rcpi;  // RSSI in 0.5 dBm units
    uint8_t rsni;  // SNR in 0.5 dB units
    uint8_t reportedFrameInfo;
    uint16_t measurementDuration;
    uint64_t actualMeasurementStartTime;
    uint8_t antennaID;
    uint32_t parentTSF;
};
```

## Example Usage

The `beacon-neighbor-protocol-11k-example.cc` demonstrates a complete roaming scenario:
- Multiple APs on different channels (36, 40, 44)
- Mobile STA moving away from associated AP
- Connection quality monitoring based on RSSI threshold
- Automatic neighbor discovery via Neighbor Report protocol
- Beacon measurements of discovered neighbors
- Multi-channel scanning using dual-PHY sniffer

**Run the example:**
```bash
./ns3 run beacon-neighbor-protocol-11k-example
```

**Command-line parameters:**
- `--nAPs` - Number of access points (default: 5)
- `--apDistance` - Distance between APs in meters (default: 5.0)
- `--rssi` - RSSI threshold for triggering neighbor discovery in dBm (default: -60.0)
- `--time` - Simulation duration in seconds (default: 10.0)
- `--verbose` - Enable detailed logging

**Example with custom parameters:**
```bash
./ns3 run "beacon-neighbor-protocol-11k-example --nAPs=3 --rssi=-70 --time=15 --verbose=1"
```

## Integration

### Basic Usage (Static Neighbor Table)

```cpp
#include "ns3/neighbor-protocol-11k-helper.h"
#include "ns3/beacon-protocol-11k-helper.h"

// Create protocol helpers
Ptr<NeighborProtocolHelper> neighborHelper = CreateObject<NeighborProtocolHelper>();
Ptr<BeaconProtocolHelper> beaconHelper = CreateObject<BeaconProtocolHelper>();

// Configure neighbor table (optional - can use dynamic discovery instead)
std::vector<ApInfo> neighborTable;
ApInfo ap;
ap.bssid = Mac48Address("00:00:00:00:00:02");
ap.channel = 40;
ap.regulatoryClass = 115;
ap.phyType = 7;
neighborTable.push_back(ap);
neighborHelper->SetNeighborTable(neighborTable);

// Install protocols
neighborHelper->InstallOnAp(apDevice);
neighborHelper->InstallOnSta(staDevice);
beaconHelper->InstallOnAp(apDevice);
beaconHelper->InstallOnSta(staDevice);

// Connect trace sources
neighborHelper->TraceConnectWithoutContext(
    "NeighborReportReceived",
    MakeCallback(&OnNeighborReportReceived));

beaconHelper->TraceConnectWithoutContext(
    "BeaconReportReceived",
    MakeCallback(&OnBeaconReportReceived));

// Request neighbor list
neighborHelper->SendNeighborReportRequest(staDevice, apMacAddress);
```

### Advanced Usage (Multi-channel Dynamic Discovery)

```cpp
#include "ns3/dual-phy-sniffer-helper.h"

// Create SHARED DualPhySnifferHelper for multi-channel scanning
DualPhySnifferHelper dualPhySniffer;

// Channel-agnostic - works with both Yans and Spectrum channels
// For Spectrum:
dualPhySniffer.SetChannel(Ptr<SpectrumChannel>(spectrumChannel));
// For Yans:
// dualPhySniffer.SetChannel(yansChannel);

dualPhySniffer.SetScanningChannels({36, 40, 44, 48});
dualPhySniffer.SetHopInterval(Seconds(0.5));

// Install on AP node (for neighbor discovery)
dualPhySniffer.Install(apDevice->GetNode(), 36, apDevice->GetMac()->GetAddress());
dualPhySniffer.StartChannelHopping();

// Connect DualPhySnifferHelper to protocol helpers
neighborHelper->SetDualPhySniffer(&dualPhySniffer);
beaconHelper->SetDualPhySniffer(&dualPhySniffer);

// Install protocols
neighborHelper->InstallOnAp(apDevice);
neighborHelper->InstallOnSta(staDevice);
beaconHelper->InstallOnAp(apDevice);
beaconHelper->InstallOnSta(staDevice);

// Protocols will now automatically discover neighbors across channels
```

### Trace Callback Handlers

```cpp
void OnNeighborReportReceived(Mac48Address staAddr,
                               Mac48Address apAddr,
                               std::vector<NeighborReportData> neighbors)
{
    for (const auto& neighbor : neighbors) {
        std::cout << "Neighbor: " << neighbor.bssid
                  << " on channel " << (int)neighbor.channel << "\n";
    }

    // Update beacon helper to measure these neighbors
    beaconHelper->SetNeighborList(neighborHelper->GetNeighborList());
    beaconHelper->SendBeaconRequest(apDevice, staAddr);
}

void OnBeaconReportReceived(Mac48Address apAddr,
                             Mac48Address staAddr,
                             std::vector<BeaconReportData> reports)
{
    for (const auto& report : reports) {
        double rssi = RcpiToRssi(report.rcpi);
        double snr = RsniToSnr(report.rsni);
        std::cout << "Beacon from " << report.bssid
                  << " RSSI: " << rssi << " dBm"
                  << " SNR: " << snr << " dB\n";
    }
}
```

## Protocol Sequence

1. **Connection Monitoring**: Application monitors RSSI of connected AP
2. **Neighbor Discovery**: When RSSI drops below threshold, STA sends Neighbor Report Request
3. **Neighbor Response**: AP responds with list of neighboring APs
4. **Beacon Measurement**: AP sends Beacon Request to STA for specific neighbors
5. **Beacon Scanning**: STA measures beacon frames (optionally across multiple channels)
6. **Beacon Reporting**: STA sends Beacon Report with measurements back to AP
7. **Roaming Decision**: Application uses measurements to select best target AP

## Integration with Other Modules

This module works well with:
- **dual-phy-sniffer**: For multi-channel scanning capabilities
- **bss_tm_11v**: For seamless BSS transition after neighbor selection
- **auto-roaming-kv**: For complete automated roaming solution

## References

- IEEE 802.11k-2016: Radio Resource Measurement
- Section 7.4.7: Neighbor Report Request/Response frames
- Section 7.4.5: Measurement Request/Report frames
- Section 7.3.2.21.6: Beacon Request element
- Section 7.3.2.22.6: Beacon Report element
- Section 7.3.2.37: Neighbor Report element