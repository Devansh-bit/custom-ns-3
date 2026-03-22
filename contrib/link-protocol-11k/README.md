# Link Measurement Protocol (IEEE 802.11k)

An ns-3 implementation of the Link Measurement protocol defined in IEEE 802.11k-2016. This module enables WiFi stations and access points to measure and report link quality metrics for radio resource management.

## Features

- **Link Measurement Requests**: STAs can request link quality information from peers (AP or other STAs)
- **Link Measurement Reports**: Automatic generation of reports containing:
  - RCPI (Received Channel Power Indicator)
  - RSNI (Received Signal to Noise Indicator)
  - Transmit power (dBm)
  - Link margin (dB)
  - Antenna identifiers
- **Trace Sources**: Monitor requests and reports via trace callbacks
- **IEEE 802.11k Compliance**: Follows standard format for measurement frames and TPC Report elements

## Model Components

### LinkMeasurementProtocol
Main protocol class that installs on WiFi devices. Handles sending/receiving measurement frames and computing link metrics from PHY layer information.

**Key Methods:**
- `Install(Ptr<WifiNetDevice>)` - Install protocol on a WiFi device
- `SendLinkMeasurementRequest(Mac48Address, int8_t, int8_t)` - Initiate measurement exchange

**Trace Sources:**
- `LinkMeasurementRequestReceived` - Fires when a request is received
- `LinkMeasurementReportReceived` - Fires when a report is received

### LinkMeasurementRequest
Structure representing a Link Measurement Request frame with transmit power parameters.

### LinkMeasurementReport
Structure representing a Link Measurement Report frame, including TPC Report element and optional RCPI/RSNI values.

**Conversion Methods:**
- `GetRcpiDbm()` - Convert RCPI to dBm
- `GetRsniDb()` - Convert RSNI to dB
- `GetTransmitPowerDbm()` - Get transmit power in dBm

## Example Usage

The `link-protocol-11k-example.cc` demonstrates a basic scenario with:
- One AP and one STA
- Mobile STA using RandomWalk2d mobility model
- Periodic link measurement requests from STA to AP
- Statistics collection and reporting

**Run the example:**
```bash
./ns3 run link-protocol-11k-example
```

**Command-line parameters:**
- `--numMeasurements` - Number of measurements to perform (default: 10)
- `--measurementInterval` - Interval between measurements in seconds (default: 1.5)
- `--distance` - Initial distance between AP and STA in meters (default: 10.0)
- `--simTime` - Total simulation time in seconds (default: 20.0)
- `--verbose` - Enable detailed logging

**Example with custom parameters:**
```bash
./ns3 run "link-protocol-11k-example --numMeasurements=20 --distance=50 --verbose=1"
```

## Integration

To use in your simulation:

```cpp
#include "ns3/link-measurement-protocol.h"
#include "ns3/link-measurement-report.h"

// Create and install protocol
Ptr<LinkMeasurementProtocol> protocol = CreateObject<LinkMeasurementProtocol>();
protocol->Install(wifiDevice);

// Connect to trace source
protocol->TraceConnectWithoutContext(
    "LinkMeasurementReportReceived",
    MakeCallback(&OnReportReceived));

// Send measurement request
protocol->SendLinkMeasurementRequest(peerMac, txPower, maxTxPower);
```

## References

- IEEE 802.11k-2016: Radio Resource Measurement
- Section 7.4.5: Link Measurement frames
- Section 7.3.2.18: TPC Report element
