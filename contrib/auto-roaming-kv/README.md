# Auto-Roaming KV Module Documentation

## Overview

The Auto-Roaming KV module (`contrib/auto-roaming-kv/`) provides an IEEE 802.11k/v-based automatic roaming orchestration system for ns-3 WiFi simulations. It integrates Link Measurement (802.11k), Neighbor Reports (802.11k), Beacon Reports (802.11k), and BSS Transition Management (802.11v) protocols to enable intelligent, standards-based AP roaming triggered by link quality degradation.

**Key Feature**: This module orchestrates the roaming *decision and trigger process* using 802.11k/v protocols, while the actual channel switching and reassociation is handled by the underlying `StaWifiMac` implementation (which supports cross-band 2.4 GHz ↔ 5 GHz roaming).

## Architecture

### Key Components

Located in `contrib/auto-roaming-kv/`:
- `helper/auto-roaming-kv-helper.{h,cc}` - Main helper class for setup and orchestration
- `model/auto-roaming-kv.{h,cc}` - Core model (template/placeholder)
- `examples/auto-roaming-kv-example.cc` - Complete usage example with mobility (uses SpectrumWifiPhy)

### Dependencies

**Required Modules:**
- `contrib/link-measurement-protocol/` - IEEE 802.11k Link Measurement
- `contrib/neighbor-protocol-11k-helper/` - IEEE 802.11k Neighbor Report Protocol
- `contrib/beacon-protocol-11k-helper/` - IEEE 802.11k Beacon Report Protocol
- `contrib/bss_tm_11v/` - IEEE 802.11v BSS Transition Management
- `contrib/dual-phy-sniffer/` - Multi-channel beacon detection for beacon reports
- `src/wifi/model/sta-wifi-mac.{h,cc}` - Extended roaming APIs with cross-band support

### Instance Management Requirements

**CRITICAL**: Understanding which components need separate instances vs. shared instances is essential for correct operation.

#### AutoRoamingKvHelper - ONE Instance for ONE STA

```cpp
// ✓ CORRECT: One helper per STA
AutoRoamingKvHelper helper;
helper.InstallSta(staDevices, neighborProtocol, beaconProtocol, bssTmHelper);
```

```cpp
// ✗ INCORRECT: Do not share helper across multiple STAs
AutoRoamingKvHelper sharedHelper;
sharedHelper.InstallSta(sta1Devices, ...);  // Will overwrite internal state!
sharedHelper.InstallSta(sta2Devices, ...);  // Previous STA state lost!
```

**Why**: The helper maintains STA-specific state:
- `m_staDevice` - Single STA device pointer
- `m_staAddress` - Single STA MAC address
- `m_lastAssociatedAp` - Last associated AP BSSID
- `m_neighborRequestTriggered` - Per-STA trigger flag
- `m_lastNeighborReport` / `m_lastBeaconReport` - Per-STA cached data

**Solution for Multiple STAs**: Create separate `AutoRoamingKvHelper` instances:
```cpp
AutoRoamingKvHelper helperSta1;
AutoRoamingKvHelper helperSta2;
helperSta1.InstallSta(sta1Devices, neighborProtocol1, beaconProtocol1, bssTmHelper1);
helperSta2.InstallSta(sta2Devices, neighborProtocol2, beaconProtocol2, bssTmHelper2);
```

#### Protocol Helpers - SEPARATE Instances per Device

All protocol helpers (`NeighborProtocolHelper`, `BeaconProtocolHelper`, `BssTm11vHelper`) require **separate instances for each AP and each STA**:

```cpp
// ✓ CORRECT: Separate instances for each device
Ptr<NeighborProtocolHelper> neighborProtocolAp1 = CreateObject<NeighborProtocolHelper>();
Ptr<NeighborProtocolHelper> neighborProtocolAp2 = CreateObject<NeighborProtocolHelper>();
Ptr<NeighborProtocolHelper> neighborProtocolSta = CreateObject<NeighborProtocolHelper>();

Ptr<BeaconProtocolHelper> beaconProtocolAp1 = CreateObject<BeaconProtocolHelper>();
Ptr<BeaconProtocolHelper> beaconProtocolAp2 = CreateObject<BeaconProtocolHelper>();
Ptr<BeaconProtocolHelper> beaconProtocolSta = CreateObject<BeaconProtocolHelper>();

Ptr<BssTm11vHelper> bssTmHelperAp1 = CreateObject<BssTm11vHelper>();
Ptr<BssTm11vHelper> bssTmHelperAp2 = CreateObject<BssTm11vHelper>();
Ptr<BssTm11vHelper> bssTmHelperSta = CreateObject<BssTm11vHelper>();

neighborProtocolAp1->InstallOnAp(ap1NetDevice);
neighborProtocolAp2->InstallOnAp(ap2NetDevice);
neighborProtocolSta->InstallOnSta(staNetDevice);
```

```cpp
// ✗ INCORRECT: Sharing instances overwrites internal device pointers!
Ptr<BssTm11vHelper> sharedBssTm = CreateObject<BssTm11vHelper>();
sharedBssTm->InstallOnAp(ap1NetDevice);  // Sets m_apDevice = ap1
sharedBssTm->InstallOnAp(ap2NetDevice);  // Overwrites m_apDevice = ap2 (ap1 lost!)
sharedBssTm->InstallOnSta(staNetDevice); // Overwrites m_apDevice = nullptr!
```

**Why**: Each helper stores device-specific pointers (`m_apDevice`, `m_staDevice`) that get overwritten when installed on multiple devices.

#### DualPhySnifferHelper - SHARED Across All Devices

The `DualPhySnifferHelper` is designed to be **shared** across all protocol helpers and manages multiple receivers internally:

```cpp
// ✓ CORRECT: Create ONE shared instance
DualPhySnifferHelper dualPhySniffer;
// Channel-agnostic - works with both Yans and Spectrum channels
dualPhySniffer.SetChannel(Ptr<SpectrumChannel>(spectrumChannel)); // For Spectrum
// OR: dualPhySniffer.SetChannel(yansChannel);  // For Yans
dualPhySniffer.SetScanningChannels({1, 6, 11, 36, 40, 44});
dualPhySniffer.SetHopInterval(MilliSeconds(100));

// Share with ALL protocol helpers
beaconProtocolAp1->SetDualPhySniffer(&dualPhySniffer);
beaconProtocolAp2->SetDualPhySniffer(&dualPhySniffer);
beaconProtocolSta->SetDualPhySniffer(&dualPhySniffer);

neighborProtocolAp1->SetDualPhySniffer(&dualPhySniffer);
neighborProtocolAp2->SetDualPhySniffer(&dualPhySniffer);
neighborProtocolSta->SetDualPhySniffer(&dualPhySniffer);
```

**Why**: The dual-PHY sniffer maintains an internal map of receivers by MAC address and manages channel hopping globally. Sharing ensures all protocols see the same beacon cache.

#### Beacon Report Callback - MUST Connect to ALL AP Instances

**CRITICAL BUG FIX**: When using `AutoRoamingKvHelper`, you **must** connect the helper's beacon report callback to **all AP beacon protocol instances**, not just the initially associated AP:

```cpp
// ✓ CORRECT: Connect to ALL APs (STA can roam between them)
beaconProtocolAp1->m_beaconReportReceivedTrace.ConnectWithoutContext(
    MakeCallback(&AutoRoamingKvHelper::OnBeaconReportReceived, &helper));
beaconProtocolAp2->m_beaconReportReceivedTrace.ConnectWithoutContext(
    MakeCallback(&AutoRoamingKvHelper::OnBeaconReportReceived, &helper));
```

```cpp
// ✗ INCORRECT: Only connecting to initially associated AP
beaconProtocolAp1->m_beaconReportReceivedTrace.ConnectWithoutContext(
    MakeCallback(&AutoRoamingKvHelper::OnBeaconReportReceived, &helper));
// After STA roams to AP2, beacon reports will NOT trigger BSS TM requests!
```

**Why**: Beacon reports are received by the **currently associated AP**, not by the STA. After roaming from AP1 to AP2, beacon reports are received by `beaconProtocolAp2`. If the callback is only connected to AP1, the second roaming attempt will fail silently.

**Fix Location**: `contrib/auto-roaming-kv/examples/auto-roaming-kv-example.cc:650-656`

#### Summary Table

| Component | Instance Management | Reason |
|-----------|-------------------|---------|
| `AutoRoamingKvHelper` | **ONE per STA** | Stores STA-specific state (device, address, trigger flags) |
| `NeighborProtocolHelper` | **SEPARATE per device** | Stores device pointer (`m_apDevice` or `m_staDevice`) |
| `BeaconProtocolHelper` | **SEPARATE per device** | Stores device pointer (`m_apDevice` or `m_staDevice`) |
| `BssTm11vHelper` | **SEPARATE per device** | Stores device pointer (`m_apDevice` or `m_staDevice`) |
| `LinkMeasurementProtocol` | **SEPARATE per device** | Installed via `AutoRoamingKvHelper::InstallAp/Sta()` |
| `DualPhySnifferHelper` | **SHARED globally** | Manages receiver map internally by MAC address |

## How It Works

### Roaming Decision Flow

The module implements a standards-based roaming decision process:

```
1. Link Monitoring (Continuous)
   └─> STA sends Link Measurement Requests to current AP periodically
       └─> AP responds with Link Measurement Reports (RSSI, SNR, etc.)

2. Threshold Detection (When RSSI < threshold)
   └─> STA triggers Neighbor Report Request to current AP
       └─> AP responds with list of neighbor APs (BSSIDs + channels)

3. Active Scanning (After neighbor report)
   └─> AP sends Beacon Request to STA (lists neighbor APs to scan)
       └─> STA's Dual-PHY Sniffer collects beacons from neighbors
       └─> STA sends Beacon Report back to AP (RSSI/SNR per neighbor)

4. Roaming Decision (After beacon report)
   └─> AP analyzes beacon reports and ranks candidate APs
       └─> AP sends BSS TM Request to STA with ranked candidate list
           └─> STA uses StaWifiMac::InitiateRoaming() to switch to best AP

5. Association & Channel Switching
   └─> StaWifiMac handles channel switch (supports cross-band 2.4 GHz ↔ 5 GHz)
       └─> Reassociation frames exchanged
       └─> Traffic resumes on new AP
```

### 1. Continuous Link Monitoring

The helper installs Link Measurement Protocol on both APs and STAs:

```cpp
AutoRoamingKvHelper roamingHelper;
roamingHelper.SetMeasurementInterval(Seconds(1.0));  // Check link every 1 second
roamingHelper.SetRssiThreshold(-75.0);                // Trigger roaming at -75 dBm

// Install on APs (respond to link measurement requests)
auto apProtocols = roamingHelper.InstallAp(apDevices);

// Install on STA (send periodic link measurement requests)
auto staProtocols = roamingHelper.InstallSta(staDevices,
                                               neighborProtocol,
                                               beaconProtocol,
                                               bssTmHelper);
```

**What happens:**
- STA automatically sends Link Measurement Requests to currently associated AP
- AP responds with Link Measurement Reports containing RSSI, SNR, link margin
- Helper monitors RSSI from reports and compares against threshold

### 2. Neighbor Discovery (802.11k)

When RSSI drops below threshold, the helper automatically triggers:

```cpp
void OnLinkMeasurementReport(Mac48Address from, LinkMeasurementReport report)
{
    double rcpiDbm = report.GetRcpiDbm();

    if (rcpiDbm < m_rssiThreshold && !m_neighborRequestTriggered)
    {
        // Trigger neighbor report request to current AP
        m_neighborProtocol->SendNeighborReportRequest(m_staDevice, currentApBssid);
        m_neighborRequestTriggered = true;
    }
}
```

**What happens:**
- STA sends Neighbor Report Request (802.11k) to current AP
- AP responds with list of neighboring APs (BSSIDs + channels)
- Helper stores neighbor list for subsequent beacon request

### 3. Active Beacon Scanning (802.11k)

After receiving neighbor report, the helper schedules a Beacon Request:

```cpp
void OnNeighborReportReceived(Mac48Address staAddress,
                               Mac48Address apAddress,
                               std::vector<NeighborReportData> neighbors)
{
    m_lastNeighborReport = neighbors;

    // Schedule beacon request after configured delay
    Simulator::Schedule(m_beaconRequestDelay,
                        &AutoRoamingKvHelper::SendBeaconRequest,
                        this);
}
```

**What happens:**
- AP sends Beacon Request (802.11k) to STA with neighbor list
- STA's Dual-PHY Sniffer scans channels and collects beacons from neighbors
- STA sends Beacon Report back to AP with RSSI/SNR measurements per neighbor
- Supports cross-band scanning (2.4 GHz and 5 GHz APs detected)

### 4. BSS Transition Management (802.11v)

After receiving beacon report, the helper triggers BSS TM steering:

```cpp
void OnBeaconReportReceived(Mac48Address apAddress,
                             Mac48Address staAddress,
                             std::vector<BeaconReportData> reports)
{
    m_lastBeaconReport = reports;

    if (!reports.empty())
    {
        Simulator::Schedule(m_bssTmRequestDelay,
                            &AutoRoamingKvHelper::SendBssTmRequest,
                            this);
    }
}

void SendBssTmRequest()
{
    // Use BSS TM helper to rank candidates and send request
    m_bssTmHelper->sendRankedCandidates(currentApDevice,
                                         currentApBssid,
                                         m_staAddress,
                                         m_lastBeaconReport);
}
```

**What happens:**
- AP ranks candidate APs based on RSSI, SNR, channel utilization
- AP sends BSS TM Request (802.11v) to STA with ranked candidate list
- STA evaluates candidates and selects best AP
- STA initiates roaming using `StaWifiMac::InitiateRoaming(targetBssid, channel, band)`

### 5. Channel Switching & Reassociation

The actual roaming is handled by `StaWifiMac`:

```cpp
// Inside StaWifiMac::InitiateRoaming()
WifiPhyBand targetBand;
uint16_t width;
if (channel >= 1 && channel <= 14)
{
    targetBand = WIFI_PHY_BAND_2_4GHZ;
    width = 20; // Must be explicit for 2.4 GHz to avoid ambiguity
}
else
{
    targetBand = WIFI_PHY_BAND_5GHZ;
    width = 0; // Auto-width for 5 GHz
}

// Switch channel and reassociate
phy->SetAttribute("ChannelSettings", StringValue(channelSettings));
// ... reassociation frame exchange ...
```

**Key Features:**
- **Cross-band support**: Can roam from 2.4 GHz → 5 GHz or vice versa
- **Multi-width support**: Auto-detects 5 GHz channel width (20/40/80/160 MHz)
- **Seamless handoff**: Maintains association state during channel switch
- **Standards-compliant**: Uses proper 802.11 reassociation procedure

## Core APIs

### AutoRoamingKvHelper Class

The main interface for setting up automatic roaming.

#### Setup Methods

```cpp
// Constructor
AutoRoamingKvHelper();

// Configure link monitoring interval
void SetMeasurementInterval(Time interval);  // Default: 1 second

// Set RSSI threshold for triggering roaming
void SetRssiThreshold(double threshold);     // Default: -75 dBm

// Set delay between neighbor report and beacon request
void SetBeaconRequestDelay(Time delay);      // Default: 50 ms

// Set delay between beacon report and BSS TM request
void SetBssTmRequestDelay(Time delay);       // Default: 50 ms
```

#### Installation

```cpp
// Install on APs (enables link measurement response)
std::vector<Ptr<LinkMeasurementProtocol>> InstallAp(NetDeviceContainer apDevices);

// Install on STA (enables full roaming orchestration)
std::vector<Ptr<LinkMeasurementProtocol>> InstallSta(
    NetDeviceContainer staDevices,
    Ptr<NeighborProtocolHelper> neighborProtocol,
    Ptr<BeaconProtocolHelper> beaconProtocol,
    Ptr<BssTm11vHelper> bssTmHelper);
```

#### Callbacks

```cpp
// Called when neighbor report is received (stores neighbors)
void OnNeighborReportReceived(Mac48Address staAddress,
                               Mac48Address apAddress,
                               std::vector<NeighborReportData> neighbors);

// Called when beacon report is received (triggers BSS TM)
void OnBeaconReportReceived(Mac48Address apAddress,
                             Mac48Address staAddress,
                             std::vector<BeaconReportData> reports);
```

## Data Structures

### Link Measurement Report

Contains link quality metrics from current AP:

```cpp
LinkMeasurementReport
{
    double GetRcpiDbm();      // RSSI in dBm
    double GetRsniDb();       // SNR in dB
    int8_t GetTransmitPowerDbm();  // AP TX power
    uint8_t GetLinkMarginDb();     // Link margin
};
```

### Neighbor Report Data

Contains information about neighboring APs:

```cpp
struct NeighborReportData
{
    Mac48Address bssid;       // Neighbor AP BSSID
    uint8_t channel;          // Operating channel
    WifiPhyBand band;         // Frequency band (2.4/5 GHz)
};
```

### Beacon Report Data

Contains beacon measurements from scanning:

```cpp
struct BeaconReportData
{
    Mac48Address bssid;       // Detected AP BSSID
    uint8_t channel;          // Channel where detected
    uint8_t rcpi;             // RCPI (0-220 scale)
    uint8_t rsni;             // RSNI (0-255 scale)
};
```

## Usage Examples

### Basic Setup

```cpp
// Create nodes
NodeContainer apNodes;
apNodes.Create(2);  // AP1 and AP2
NodeContainer staNodes;
staNodes.Create(1);

// Create spectrum channel
Ptr<MultiModelSpectrumChannel> spectrumChannel =
    CreateObject<MultiModelSpectrumChannel>();

// Setup WiFi with 2.4 GHz and 5 GHz APs
SpectrumWifiPhyHelper phyHelper;
phyHelper.SetChannel(spectrumChannel);

WifiHelper wifi;
wifi.SetStandard(WIFI_STANDARD_80211ax);

// AP1 on channel 1 (2.4 GHz, 20 MHz)
phyHelper.Set("ChannelSettings", StringValue("{1, 20, BAND_2_4GHZ, 0}"));
NetDeviceContainer ap1Device = wifi.Install(phyHelper, macHelper, apNodes.Get(0));

// AP2 on channel 36 (5 GHz, auto-width)
phyHelper.Set("ChannelSettings", StringValue("{36, 0, BAND_5GHZ, 0}"));
NetDeviceContainer ap2Device = wifi.Install(phyHelper, macHelper, apNodes.Get(1));

// STA starts on 2.4 GHz
phyHelper.Set("ChannelSettings", StringValue("{1, 20, BAND_2_4GHZ, 0}"));
NetDeviceContainer staDevice = wifi.Install(phyHelper, macHelper, staNodes.Get(0));

// CRITICAL: Create SEPARATE protocol instances for each AP and STA
// (Sharing instances will overwrite internal device pointers!)

// Neighbor Protocol - 3 separate instances
Ptr<NeighborProtocolHelper> neighborProtocolAp1 = CreateObject<NeighborProtocolHelper>();
Ptr<NeighborProtocolHelper> neighborProtocolAp2 = CreateObject<NeighborProtocolHelper>();
Ptr<NeighborProtocolHelper> neighborProtocolSta = CreateObject<NeighborProtocolHelper>();

neighborProtocolAp1->InstallOnAp(ap1Device.Get(0));
neighborProtocolAp2->InstallOnAp(ap2Device.Get(0));
neighborProtocolSta->InstallOnSta(staDevice.Get(0));

// Beacon Protocol - 3 separate instances
Ptr<BeaconProtocolHelper> beaconProtocolAp1 = CreateObject<BeaconProtocolHelper>();
Ptr<BeaconProtocolHelper> beaconProtocolAp2 = CreateObject<BeaconProtocolHelper>();
Ptr<BeaconProtocolHelper> beaconProtocolSta = CreateObject<BeaconProtocolHelper>();

// Setup Dual-PHY Sniffer ONCE and share with all beacon protocols
DualPhySnifferHelper dualPhySniffer;
dualPhySniffer.SetSpectrumChannel(spectrumChannel);
dualPhySniffer.SetScanningChannels({1, 6, 11, 36, 40, 44});  // Both 2.4 GHz and 5 GHz
dualPhySniffer.SetHopInterval(MilliSeconds(100));

// Share the dual-PHY sniffer with ALL beacon protocol instances
beaconProtocolAp1->SetDualPhySniffer(&dualPhySniffer);
beaconProtocolAp2->SetDualPhySniffer(&dualPhySniffer);
beaconProtocolSta->SetDualPhySniffer(&dualPhySniffer);

beaconProtocolAp1->InstallOnAp(ap1Device.Get(0));
beaconProtocolAp2->InstallOnAp(ap2Device.Get(0));
beaconProtocolSta->InstallOnSta(staDevice.Get(0));

// BSS TM Protocol - 3 separate instances
Ptr<BssTm11vHelper> bssTmHelperAp1 = CreateObject<BssTm11vHelper>();
Ptr<BssTm11vHelper> bssTmHelperAp2 = CreateObject<BssTm11vHelper>();
Ptr<BssTm11vHelper> bssTmHelperSta = CreateObject<BssTm11vHelper>();

bssTmHelperAp1->InstallOnAp(ap1Device.Get(0));
bssTmHelperAp2->InstallOnAp(ap2Device.Get(0));
bssTmHelperSta->InstallOnSta(staDevice.Get(0));

// Setup Auto-Roaming KV (ONE instance per STA)
AutoRoamingKvHelper roamingHelper;
roamingHelper.SetMeasurementInterval(Seconds(1.0));
roamingHelper.SetRssiThreshold(-75.0);
roamingHelper.SetBeaconRequestDelay(MilliSeconds(50));
roamingHelper.SetBssTmRequestDelay(MilliSeconds(50));

// Install on APs
NetDeviceContainer apDevices;
apDevices.Add(ap1Device);
apDevices.Add(ap2Device);
roamingHelper.InstallAp(apDevices);

// Install on STA (pass STA-specific protocol instances)
roamingHelper.InstallSta(staDevice, neighborProtocolSta, beaconProtocolSta, bssTmHelperSta);

// CRITICAL: Connect helper's beacon callback to BOTH AP beacon protocol instances
// (Beacon reports are received by the currently associated AP, not by the STA)
beaconProtocolAp1->m_beaconReportReceivedTrace.ConnectWithoutContext(
    MakeCallback(&AutoRoamingKvHelper::OnBeaconReportReceived, &roamingHelper));
beaconProtocolAp2->m_beaconReportReceivedTrace.ConnectWithoutContext(
    MakeCallback(&AutoRoamingKvHelper::OnBeaconReportReceived, &roamingHelper));
```

### Triggering Roaming with Mobility

```cpp
// STA starts close to AP1 (2.4 GHz)
MobilityHelper mobility;
Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
positionAlloc->Add(Vector(0.0, 0.0, 0.0));    // AP1 at origin
positionAlloc->Add(Vector(50.0, 0.0, 0.0));   // AP2 at 50m
positionAlloc->Add(Vector(5.0, 0.0, 0.0));    // STA starts near AP1
mobility.SetPositionAllocator(positionAlloc);
mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
mobility.Install(apNodes);

// STA moves away from AP1 toward AP2
mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
mobility.Install(staNodes);
Ptr<ConstantVelocityMobilityModel> staMobility =
    staNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();
staMobility->SetVelocity(Vector(2.0, 0.0, 0.0));  // 2 m/s toward AP2

// As STA moves away from AP1:
// 1. RSSI drops below -75 dBm (after ~10 seconds)
// 2. Neighbor report request triggered automatically
// 3. Beacon request sent, STA scans for AP2 (5 GHz)
// 4. BSS TM request sent with AP2 as candidate
// 5. STA roams from AP1 (ch1, 2.4GHz) to AP2 (ch36, 5GHz)
```

### Monitoring Roaming Events

```cpp
// Connect to StaWifiMac trace sources
Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/Assoc",
                MakeCallback(&OnAssociation));
Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/DeAssoc",
                MakeCallback(&OnDisassociation));

void OnAssociation(std::string context, Mac48Address bssid)
{
    std::cout << Simulator::Now().As(Time::S)
              << " STA associated with AP " << bssid << std::endl;
}

void OnDisassociation(std::string context, Mac48Address bssid)
{
    std::cout << Simulator::Now().As(Time::S)
              << " STA disassociated from AP " << bssid << std::endl;
}
```

## Protocol Interaction Flow

### Complete Roaming Sequence

```
Time  Event
────  ──────────────────────────────────────────────────────────────
0.0s  STA associates with AP1 (ch1, 2.4 GHz)
      Traffic begins (UDP client → server via AP1)

1.0s  Link Measurement Request: STA → AP1
1.0s  Link Measurement Report:  AP1 → STA (RSSI: -65 dBm) ✓

2.0s  Link Measurement Request: STA → AP1
2.0s  Link Measurement Report:  AP1 → STA (RSSI: -68 dBm) ✓

...   [STA continues moving away from AP1]

10.5s Link Measurement Request: STA → AP1
10.5s Link Measurement Report:  AP1 → STA (RSSI: -76 dBm) ⚠️

10.5s TRIGGER: RSSI < threshold (-75 dBm)
      → Neighbor Report Request: STA → AP1

10.6s Neighbor Report Response: AP1 → STA
      Neighbors: [{BSSID: AP2, Channel: 36, Band: 5GHz}]

10.65s Beacon Request: AP1 → STA
       Scan list: [AP2 on ch36]
       → Dual-PHY Sniffer activates
       → STA scans channel 36 (5 GHz)
       → Detects AP2 beacon (RSSI: -62 dBm)

10.8s Beacon Report: STA → AP1
      Results: [{BSSID: AP2, Ch: 36, RCPI: 96, RSNI: 200}]

10.85s BSS TM Request: AP1 → STA
       Candidate list (ranked):
       [1] AP2 (ch36, 5GHz) - RSSI: -62 dBm, Preference: 255

10.9s BSS TM Response: STA → AP1 (Accept)
      → STA initiates roaming to AP2

10.95s Channel switch: ch1 (2.4 GHz) → ch36 (5 GHz)
       Reassociation Request: STA → AP2
11.0s  Reassociation Response: AP2 → STA (Success)
       STA associated with AP2 (ch36, 5 GHz) ✓

11.0s+ Traffic resumes via AP2
       UDP packets flow through new AP without interruption
```

## Implementation Details

### Automatic AP Tracking

The helper automatically detects which AP the STA is currently associated with:

```cpp
Mac48Address GetCurrentApBssid() const
{
    Ptr<StaWifiMac> staWifiMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());

    if (!staWifiMac->IsAssociated())
    {
        return Mac48Address();  // Not associated
    }

    return staWifiMac->GetBssid(0);  // Get BSSID for link 0
}
```

**Key Features:**
- No manual AP specification required
- Dynamically follows STA as it roams
- Resets trigger flag after detecting new association
- Enables continuous monitoring across multiple roaming events

### One-Time Trigger Mechanism

To prevent repeated neighbor requests, the helper uses a trigger flag:

```cpp
if (rcpiDbm < m_rssiThreshold && !m_neighborRequestTriggered)
{
    // Trigger neighbor request
    m_neighborRequestTriggered = true;
}

// Reset when new AP association detected
if (currentApBssid != m_lastAssociatedAp)
{
    m_neighborRequestTriggered = false;  // Allow new trigger on new AP
}
```

**Benefits:**
- Prevents flooding with neighbor requests
- One roaming trigger per RSSI drop below threshold
- Resets after successful roaming to enable future roaming
- Allows monitoring to continue on new AP

### Cross-Band Channel Switching

The module leverages `StaWifiMac`'s cross-band roaming support:

**2.4 GHz Channel Handling:**
```cpp
// For 2.4 GHz channels (1-14)
width = 20;  // Must be explicit to avoid DSSS/OFDM/40MHz ambiguity
channelSettings = "{channel, 20, BAND_2_4GHZ, 0}"
```

**5 GHz Channel Handling:**
```cpp
// For 5 GHz channels (36+)
width = 0;   // Auto-detect width from channel number
channelSettings = "{channel, 0, BAND_5GHZ, 0}"
// Supports 20/40/80/160 MHz automatically
```

**Why This Matters:**
- 2.4 GHz channels have multiple definitions in ns-3 (DSSS 22MHz, OFDM 20MHz, OFDM 40MHz)
- Using `width=0` for 2.4 GHz causes: `"No unique channel found"` runtime error
- Explicit `width=20` selects the OFDM 20 MHz variant
- 5 GHz channels have unique definitions, so `width=0` works perfectly
- Enables seamless cross-band roaming (e.g., ch1 → ch36 or ch36 → ch11)

## Use Cases

### Enterprise WiFi with Multi-Band APs

```cpp
// Deploy mixed 2.4 GHz and 5 GHz APs
// STAs automatically roam to best AP regardless of band
// Ideal for high-density deployments with band steering
```

### Mobility Scenarios

```cpp
// STAs move through coverage areas
// Automatic roaming maintains connectivity
// Tests handoff latency and packet loss during roaming
```

### Load Balancing

```cpp
// BSS TM with channel utilization metrics
// APs can proactively offload STAs to less congested APs
// Works across 2.4 GHz and 5 GHz bands
```

### Coverage Optimization

```cpp
// Test AP placement with roaming simulation
// Measure roaming trigger points
// Optimize coverage with multi-band deployment
```

## Configuration Parameters

### Link Monitoring

```cpp
roamingHelper.SetMeasurementInterval(Seconds(1.0));  // How often to check link quality
roamingHelper.SetRssiThreshold(-75.0);                // When to trigger roaming (dBm)
```

**Recommendations:**
- **Measurement interval**: 0.5 - 2.0 seconds (balance between responsiveness and overhead)
- **RSSI threshold**: -70 to -80 dBm (lower = earlier roaming)

### Protocol Delays

```cpp
roamingHelper.SetBeaconRequestDelay(MilliSeconds(50));   // Delay after neighbor report
roamingHelper.SetBssTmRequestDelay(MilliSeconds(50));    // Delay after beacon report
```

**Recommendations:**
- **Beacon request delay**: 50-200 ms (allow time for neighbor report processing)
- **BSS TM request delay**: 50-200 ms (allow time for beacon scanning completion)

### Dual-PHY Sniffer

```cpp
snifferHelper.SetHopInterval(Seconds(0.5));  // How fast to scan channels
snifferHelper.SetScanningChannels({1, 6, 11, 36, 40, 44});  // Channels to scan
```

**Recommendations:**
- **Hop interval**: 0.2 - 1.0 seconds (faster = quicker AP discovery)
- **Scanning channels**: Include all AP channels (both 2.4 and 5 GHz)

## Trace Sources

### Link Measurement Protocol

```cpp
protocol->TraceConnectWithoutContext(
    "LinkMeasurementRequestSent",
    MakeCallback(&OnRequestSent));

protocol->TraceConnectWithoutContext(
    "LinkMeasurementReportReceived",
    MakeCallback(&OnReportReceived));
```

### StaWifiMac Roaming

```cpp
Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/Assoc",
                MakeCallback(&OnAssociation));

Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/DeAssoc",
                MakeCallback(&OnDisassociation));
```

### Protocol Helpers

```cpp
neighborProtocol->m_neighborReportReceivedTrace.ConnectWithoutContext(
    MakeCallback(&OnNeighborReport));

beaconProtocol->m_beaconReportReceivedTrace.ConnectWithoutContext(
    MakeCallback(&OnBeaconReport));
```

## Limitations

### Current Implementation

- **Single STA support**: Helper is designed for one STA per instance
- **No MLD support**: Single-link operation only (no multi-link devices)
- **Fixed thresholds**: No dynamic threshold adjustment
- **One-time trigger**: Only one roaming attempt per RSSI drop (resets after roaming)
- **No failed roaming retry**: If roaming fails, waits for next RSSI trigger

### Design Constraints

- Requires all dependent protocols (11k/11v) to be installed
- STAs must share spectrum channel with APs for beacon scanning
- Channel switching speed depends on PHY model implementation
- No support for concurrent multi-band operation (STA on both 2.4 and 5 GHz)

## Performance Considerations

### Protocol Overhead

- Link Measurement: ~2 frames per measurement interval
- Neighbor Report: ~2 frames (request + response)
- Beacon Request/Report: ~2+ frames (scales with number of neighbors)
- BSS TM: ~2 frames (request + response)

**Typical overhead**: ~10-15 management frames per roaming event

### Roaming Latency

Expected timeline for complete roaming:
- Link quality detection: 1-2 seconds (measurement interval)
- Neighbor report: 10-50 ms
- Beacon scanning: 100-500 ms (depends on hop interval)
- BSS TM decision: 50-100 ms
- Channel switch + reassociation: 50-200 ms

**Total latency**: ~200-900 ms from trigger to completion

### Traffic Impact

- UDP/TCP traffic continues during roaming (may experience packet loss)
- Typical packet loss: 0-5% during handoff
- No disruption to ongoing connections if roaming succeeds
- Application-layer retransmissions handle packet loss

## Debugging

### Enable Logging

```bash
NS_LOG="AutoRoamingKvHelper=level_all:LinkMeasurementProtocol=info:StaWifiMac=info" \
  ./ns3 run auto-roaming-kv-example
```

### Common Issues

**1. Roaming not triggered**
- Check RSSI threshold (may need adjustment)
- Verify link measurement interval (not too long)
- Ensure STA is actually experiencing signal degradation
- Check if trigger flag is stuck (shouldn't happen with proper AP tracking)

**2. No neighbor APs found**
- Verify neighbor protocol is installed on current AP
- Check that target APs are in range
- Ensure neighbor list is properly configured on APs

**3. Beacon reports empty**
- Verify Dual-PHY Sniffer is installed and started
- Check scanning channels include target AP channels
- Ensure hop interval allows beacon detection
- Verify spectrum channel is shared between all devices

**4. Channel switch fails**
- Check 2.4 GHz channel width specification (must use width=20)
- Verify 5 GHz channels use width=0 for auto-detection
- Ensure target AP is on correct channel/band
- Check PHY supports target band (2.4 GHz or 5 GHz)

**5. BSS TM request ignored**
- Verify BSS TM helper is installed on both AP and STA
- Check that candidate list is not empty
- Ensure STA MAC supports BSS TM responses
- Verify BSS TM response timeout is adequate

## Future Improvements

Potential enhancements:
- Multi-STA support with per-STA helper instances
- Dynamic RSSI threshold adjustment based on environment
- Retry mechanism for failed roaming attempts
- Predictive roaming based on velocity/trajectory
- Multi-link device (MLD) support for WiFi 7
- Load-based roaming (not just RSSI-based)
- Integration with machine learning for roaming decisions

## Standards Compliance

This module implements:
- **IEEE 802.11k-2008**: Radio Resource Measurement
  - Link Measurement Protocol (clause 11.11.9)
  - Neighbor Report Protocol (clause 11.11.10)
  - Beacon Report Protocol (clause 11.11.9.1)
- **IEEE 802.11v-2011**: Wireless Network Management
  - BSS Transition Management (clause 11.11.10)

All protocols follow standard frame formats and procedures.
