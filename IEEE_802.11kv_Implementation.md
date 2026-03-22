# IEEE 802.11k/v Radio Resource Management Implementation for ns-3

## 1. Introduction

This document presents a comprehensive IEEE 802.11k/v Radio Resource Management (RRM) implementation for ns-3. The implementation was developed with direct reference to the official IEEE 802.11-2016 specification, specifically:

- **IEEE 802.11k (Radio Resource Measurement):** Link Measurement, Neighbor Report, Beacon Report
- **IEEE 802.11v (Wireless Network Management):** BSS Transition Management

**Scope of Implementation:**
- Link Measurement Request/Report protocol
- Neighbor Report Request/Response protocol  
- Beacon Measurement Request and Beacon Report
- BSS Transition Management Request/Response (client steering)

**Validation:** 16 unit tests across 5 modules, all passing.

---

## 2. Protocol Modules

### 2.1 beacon-neighbor-protocol-11k

Implements Neighbor Report and Beacon Report protocols as defined in IEEE 802.11-2016.

**Components:**

**NeighborReportElement** - WifiInformationElement for neighbor AP data:
```cpp
class NeighborReportElement : public WifiInformationElement
{
  public:
    NeighborReportElement();
    
    // WifiInformationElement interface
    WifiInformationElementId ElementId() const override;  // Returns IE_NEIGHBOR_REPORT (52)
    void Print(std::ostream& os) const override;
    
    // Accessors
    void SetBssid(Mac48Address bssid);
    Mac48Address GetBssid() const;
    void SetBssidInfo(uint32_t info);        // 32-bit capability field (Figure 9-196)
    uint32_t GetBssidInfo() const;
    void SetOperatingClass(uint8_t operatingClass);  // Annex E regulatory class
    uint8_t GetOperatingClass() const;
    void SetChannelNumber(uint8_t channel);
    uint8_t GetChannelNumber() const;
    void SetPhyType(uint8_t phyType);        // Table 9-176: 4=DSSS, 7=HT, 8=VHT
    uint8_t GetPhyType() const;

  private:
    uint16_t GetInformationFieldSize() const override;
    void SerializeInformationField(Buffer::Iterator start) const override;
    uint16_t DeserializeInformationField(Buffer::Iterator start, uint16_t length) override;

    Mac48Address m_bssid;          // BSSID of neighbor AP (6 octets)
    uint32_t m_bssidInfo{0};       // BSSID Information field (4 octets)
    uint8_t m_operatingClass{0};   // Operating Class (1 octet)
    uint8_t m_channelNumber{0};    // Channel Number (1 octet)
    uint8_t m_phyType{0};          // PHY Type (1 octet)
};
```

**BeaconReportElement** - WifiInformationElement for beacon measurement data:
```cpp
class BeaconReportElement : public WifiInformationElement
{
  public:
    BeaconReportElement();
    
    WifiInformationElementId ElementId() const override;  // Returns IE_MEASUREMENT_REPORT (39)
    void Print(std::ostream& os) const override;
    
    // Accessors
    void SetOperatingClass(uint8_t operatingClass);
    uint8_t GetOperatingClass() const;
    void SetChannelNumber(uint8_t channel);
    uint8_t GetChannelNumber() const;
    void SetActualMeasurementStartTime(uint64_t tsf);  // 64-bit TSF timestamp
    uint64_t GetActualMeasurementStartTime() const;
    void SetMeasurementDuration(uint16_t duration);    // Duration in TUs
    uint16_t GetMeasurementDuration() const;
    void SetReportedFrameInfo(uint8_t info);
    uint8_t GetReportedFrameInfo() const;
    void SetRcpi(uint8_t rcpi);                        // RCPI = 2*(RSSI+110)
    uint8_t GetRcpi() const;
    void SetRsni(uint8_t rsni);                        // RSNI = 2*SNR
    uint8_t GetRsni() const;
    void SetBssid(Mac48Address bssid);
    Mac48Address GetBssid() const;
    void SetAntennaId(uint8_t antennaId);
    uint8_t GetAntennaId() const;
    void SetParentTsf(uint32_t parentTsf);
    uint32_t GetParentTsf() const;

  private:
    uint16_t GetInformationFieldSize() const override;
    void SerializeInformationField(Buffer::Iterator start) const override;
    uint16_t DeserializeInformationField(Buffer::Iterator start, uint16_t length) override;

    uint8_t m_operatingClass{0};
    uint8_t m_channelNumber{0};
    uint64_t m_actualMeasurementStartTime{0};  // 64-bit TSF
    uint16_t m_measurementDuration{0};
    uint8_t m_reportedFrameInfo{0};
    uint8_t m_rcpi{0};
    uint8_t m_rsni{0};
    Mac48Address m_bssid;
    uint8_t m_antennaId{0};
    uint32_t m_parentTsf{0};
};
```

**NeighborProtocolHelper** - Handles Neighbor Report frame exchange:
```cpp
class NeighborProtocolHelper : public Object
{
  public:
    static TypeId GetTypeId();
    
    // Configuration
    void SetNeighborTable(std::vector<ApInfo> table);
    void SetChannel(Ptr<YansWifiChannel> channel);
    void SetScanningChannels(const std::vector<uint8_t>& channels);
    void SetHopInterval(Time interval);
    void SetDualPhySniffer(DualPhySnifferHelper* sniffer);
    
    // Installation
    void InstallOnAp(Ptr<WifiNetDevice> apDevice);
    void InstallOnSta(Ptr<WifiNetDevice> staDevice);
    
    // Protocol
    void SendNeighborReportRequest(Ptr<WifiNetDevice> staDevice, Mac48Address apAddress);
    
    // Trace source
    TracedCallback<Mac48Address, Mac48Address, std::vector<NeighborReportData>> m_neighborReportReceivedTrace;
    
    // Accessors
    std::set<Mac48Address> GetNeighborList() const;
};
```

**Unit Tests:**
| Test | Verification |
|------|--------------|
| NeighborReportElementSerializationTestCase | Serializes element, deserializes, verifies all fields match |
| BeaconReportElementSerializationTestCase | Validates 64-bit TSF handling and RCPI/RSNI storage |
| RcpiRsniConversionTestCase | Verifies RCPI=2*(RSSI+110) and RSNI=2*SNR formulas |

---

### 2.2 link-protocol-11k

Implements Link Measurement protocol as defined in IEEE 802.11-2016.

**LinkMeasurementProtocol:**
```cpp
class LinkMeasurementProtocol : public Object
{
  public:
    static TypeId GetTypeId();
    LinkMeasurementProtocol();
    ~LinkMeasurementProtocol() override;
    
    // Installation
    void Install(Ptr<WifiNetDevice> device);
    void InstallWithSniffer(Ptr<WifiNetDevice> device, Ptr<UnifiedPhySniffer> sniffer);
    
    // Protocol
    void SendLinkMeasurementRequest(Mac48Address to,
                                    int8_t transmitPowerUsed,
                                    int8_t maxTransmitPower,
                                    bool includeEsseData = false);
    
    // Trace sources
    TracedCallback<Mac48Address, const LinkMeasurementRequest&> m_requestReceivedTrace;
    TracedCallback<Mac48Address, const LinkMeasurementReport&> m_reportReceivedTrace;
};
```

**LinkMeasurementRequest:**
```cpp
struct LinkMeasurementRequest
{
    Mac48Address from;
    Mac48Address to;
    uint8_t dialogToken;
    int8_t transmitPowerUsed;    // dBm
    int8_t maxTransmitPower;     // dBm
    Ptr<Packet> packet;
    
    LinkMeasurementRequest(Mac48Address from, Mac48Address to, uint8_t dialogToken,
                           int8_t txPower, int8_t maxTxPower, Ptr<Packet> packet);
    
    Mac48Address GetFrom() const;
    Mac48Address GetTo() const;
    uint8_t GetDialogToken() const;
    int8_t GetTransmitPowerUsed() const;
    int8_t GetMaxTransmitPower() const;
    Ptr<Packet> GetPacket() const;
};
```

**LinkMeasurementReport:**
```cpp
struct LinkMeasurementReport
{
    Mac48Address from;
    Mac48Address to;
    uint8_t dialogToken;
    int8_t transmitPower;
    int8_t linkMargin;
    uint8_t rxAntennaId;
    uint8_t txAntennaId;
    uint16_t rcpi;
    uint16_t rsni;
    
    // Utility functions
    static double ConvertRcpiToDbm(uint8_t rcpi);  // dBm = (RCPI/2) - 110
    static double ConvertRsniToDb(uint8_t rsni);   // dB = RSNI/2
    
    uint8_t GetDialogToken() const;
    uint8_t GetReceiveAntennaId() const;
    uint8_t GetTransmitAntennaId() const;
    uint16_t GetRcpi() const;
    uint16_t GetRsni() const;
    int8_t GetLinkMarginDb() const;
};
```

**Unit Tests:**
| Test | Verification |
|------|--------------|
| LinkMeasurementRequestTestCase | Validates constructor and all accessor methods |
| LinkMeasurementReportTestCase | Validates TPC element, antenna IDs, RCPI/RSNI storage |
| RcpiRsniConversionTestCase | Tests ConvertRcpiToDbm() and ConvertRsniToDb() functions |

---

### 2.3 bss_tm_11v

Implements BSS Transition Management as defined in IEEE 802.11-2016.

**BssTm11vHelper:**
```cpp
class BssTm11vHelper : public rankListManager
{
  public:
    static TypeId GetTypeId();
    BssTm11vHelper();
    virtual ~BssTm11vHelper();
    
    void sendRankedCandidates(Ptr<WifiNetDevice> apDevice, 
                              Mac48Address apAddress, 
                              Mac48Address staAddress, 
                              std::vector<BeaconReportData> reports);
    
    void SendDynamicBssTmRequest(Ptr<WifiNetDevice> apDevice,
                                 const BssTmParameters& params,
                                 Mac48Address staAddress);
    
    void HandleRead(Ptr<WifiNetDevice> staDevice,
                    Ptr<const Packet> packet,
                    Mac48Address apAddress);
    
    void SendDynamicBssTmResponse(Ptr<WifiNetDevice> staDevice,
                                  const BssTmResponseParameters& params,
                                  Mac48Address apAddress);
    
    // Configuration
    void SetBeaconSniffer(DualPhySnifferHelper* sniffer);
    void SetStaMac(Mac48Address staMac);
    void SetBeaconCache(const std::vector<BeaconInfo>& beacons);
    void SetCooldown(Time duration);
    Time GetCooldown() const;
    
    // Installation
    void InstallOnAp(Ptr<WifiNetDevice> apDevice);
    void InstallOnSta(Ptr<WifiNetDevice> staDevice);
};
```

**BssTmParameters:**
```cpp
struct BssTmParameters
{
    uint8_t dialogToken;
    uint16_t disassociationTimer;     // Time Units (TUs)
    uint8_t validityInterval;
    
    enum class ReasonCode : uint8_t
    {
        UNSPECIFIED = 0,
        LOW_RSSI = 5,
        HIGH_LOAD = 4,
        BETTER_AP_FOUND = 6
    };
    ReasonCode reasonCode;
    
    struct CandidateAP
    {
        uint8_t BSSID[6];
        uint8_t operatingClass;
        uint8_t channel;
        uint8_t phyType;
        uint8_t preference;           // 0-255, higher is better
    };
    std::vector<CandidateAP> candidates;
};

std::string ReasonCodeToString(BssTmParameters::ReasonCode code);
```

**BssTmResponseParameters:**
```cpp
struct BssTmResponseParameters
{
    uint8_t dialogToken;
    uint8_t statusCode;               // 0=Accept, 1-5=Reject reasons
    uint8_t terminationDelay;
    uint8_t targetBSSID[6];
    uint8_t channel;
};
```

**Unit Tests:**
| Test | Verification |
|------|--------------|
| BssTmParametersDefaultTestCase | Validates default initialization of all fields |
| BssTmParametersCandidatesTestCase | Tests candidate AP list with preference ranking |
| BssTmResponseParametersTestCase | Validates status code and target BSSID handling |
| ReasonCodeToStringTestCase | Tests enum to string conversion |

---

### 2.4 unified-phy-sniffer

Efficient centralized frame processing infrastructure.

**UnifiedPhySniffer:**
```cpp
class UnifiedPhySniffer : public Object
{
  public:
    static TypeId GetTypeId();
    UnifiedPhySniffer();
    ~UnifiedPhySniffer() override;
    
    // Installation
    void Install(Ptr<WifiNetDevice> device);
    void InstallOnPhy(Ptr<WifiPhy> phy, Mac48Address ownAddress);
    
    // Accessors
    Ptr<Node> GetNode() const;
    
    // Subscription API
    uint32_t Subscribe(FrameInterest interest, std::function<void(ParsedFrameContext*)> callback);
    uint32_t SubscribeBeacons(std::function<void(ParsedFrameContext*)> callback);
    uint32_t SubscribeAction(uint8_t category, std::function<void(ParsedFrameContext*)> callback);
    uint32_t SubscribeData(std::function<void(ParsedFrameContext*)> callback);
    uint32_t SubscribeForDestination(FrameInterest interest, Mac48Address destination,
                                      std::function<void(ParsedFrameContext*)> callback);
    void Unsubscribe(uint32_t subscriptionId);
    
    // Statistics
    uint64_t GetFramesProcessed() const;
    uint64_t GetFramesDispatched() const;
    uint64_t GetFramesFiltered() const;
};
```

**ParsedFrameContext:**
```cpp
struct ParsedFrameContext
{
    // Zero-copy references
    Ptr<const Packet> originalPacket;
    const WifiTxVector* txVector;
    
    // Frame classification
    FrameType type;           // BEACON, ACTION, DATA, CONTROL, etc.
    uint8_t subtype;
    bool toDs;
    bool fromDs;
    
    // Pre-computed signal measurements
    double rssi;              // dBm
    double noise;             // dBm
    double snr;               // dB
    uint8_t rcpi;             // 0-220
    uint8_t rsni;             // 0-255
    
    // Channel info
    uint16_t channelFreqMhz;
    uint8_t channel;
    WifiPhyBand band;
    
    // Lazy-parsed fields
    bool addressesParsed;
    Mac48Address addr1;       // Destination
    Mac48Address addr2;       // Source/Transmitter
    Mac48Address addr3;       // BSSID
    
    bool actionParsed;
    uint8_t actionCategory;
    uint8_t actionCode;
    uint8_t dialogToken;
    
    Time timestamp;
    uint32_t nodeId;
    uint32_t deviceId;
    
    // Methods
    void Reset();
    void Initialize(Ptr<const Packet> packet, uint16_t channelFreq,
                    const WifiTxVector& txVec, double signalDbm,
                    double noiseDbm, uint16_t sta);
    void EnsureAddressesParsed();
    void EnsureActionParsed();
    Ptr<Packet> GetPayloadCopy() const;
    void ClassifyFrame(uint8_t fc0, uint8_t fc1);
    void DeriveChannelFromFreq();
    void ComputeMetrics();
};
```

**FrameInterest enum:**
```cpp
enum class FrameInterest : uint8_t
{
    NONE = 0,
    BEACON = 1 << 0,
    ACTION_CAT5 = 1 << 1,    // 802.11k Radio Measurement
    ACTION_CAT10 = 1 << 2,   // 802.11v WNM
    ACTION_OTHER = 1 << 3,
    DATA = 1 << 4,
    ALL = 0xFF
};
```

**Unit Tests:**
| Test | Verification |
|------|--------------|
| FrameInterestTestCase | Validates flag values and bitwise combinations |
| ParsedFrameContextSignalTestCase | Tests signal measurement field storage |
| FrameTypeEnumTestCase | Verifies FrameType enum value uniqueness |

---

### 2.5 dual-phy-sniffer

Multi-channel beacon scanning while maintaining association.

**DualPhySnifferHelper:**
```cpp
class DualPhySnifferHelper
{
  public:
    typedef Callback<void, const DualPhyMeasurement&> MeasurementCallback;
    
    DualPhySnifferHelper();
    ~DualPhySnifferHelper();
    
    // Channel configuration
    void SetChannel(Ptr<YansWifiChannel> channel);
    void SetChannel(Ptr<SpectrumChannel> channel);
    void SetScanningChannels(const std::vector<uint8_t>& channels);
    void SetHopInterval(Time interval);
    
    // Callbacks
    void SetMeasurementCallback(MeasurementCallback callback);
    
    // Configuration
    void SetSsid(Ssid ssid);
    void SetValidApBssids(const std::set<Mac48Address>& apBssids);
    
    // Installation
    NetDeviceContainer Install(Ptr<Node> node, uint8_t operatingChannel, Mac48Address desiredBssid);
    
    // MAC address retrieval
    Mac48Address GetOperatingMac(uint32_t nodeId) const;
    Mac48Address GetScanningMac(uint32_t nodeId) const;
    
    // Control
    void StartChannelHopping();
    
    // Beacon cache
    const std::map<std::pair<Mac48Address, Mac48Address>, BeaconInfo>& GetAllBeacons() const;
    std::vector<BeaconInfo> GetBeaconsReceivedBy(Mac48Address receiverBssid) const;
    std::vector<BeaconInfo> GetBeaconsFrom(Mac48Address transmitterBssid) const;
    void ClearBeaconCache();
    void ClearBeaconsReceivedBy(Mac48Address receiverBssid);
    
    // Cache configuration
    void SetBeaconMaxAge(Time maxAge);
    void SetBeaconMaxEntries(uint32_t maxEntries);
};
```

**DualPhyMeasurement:**
```cpp
struct DualPhyMeasurement
{
    Mac48Address receiverBssid;     // Device that captured the beacon
    Mac48Address transmitterBssid;  // AP that transmitted the beacon
    uint8_t channel;                // Channel where beacon was detected
    double rcpi;                    // RCPI value (0-220)
    double rssi;                    // Raw RSSI in dBm
    double timestamp;               // Simulation time
};
```

**BeaconInfo:**
```cpp
struct BeaconInfo
{
    Mac48Address bssid;             // Transmitting AP
    Mac48Address receivedBy;        // Receiving device
    double rssi;                    // dBm
    double rcpi;                    // 0-220
    double snr;                     // dB
    uint8_t channel;
    WifiPhyBand band;
    Time timestamp;
    uint32_t beaconCount;
    
    // From Information Elements
    uint16_t channelWidth;          // MHz (from HT/VHT/HE Capability)
    uint16_t staCount;              // From BSS Load IE
    uint8_t channelUtilization;     // 0-255 from BSS Load IE
    uint8_t wifiUtilization;        // WiFi portion
    uint8_t nonWifiUtilization;     // Non-WiFi portion
};
```

**Unit Tests:**
| Test | Verification |
|------|--------------|
| DualPhyMeasurementTestCase | Tests measurement struct field initialization |
| BeaconInfoTestCase | Validates all beacon cache fields including load metrics |
| DualPhySnifferHelperConfigTestCase | Tests helper configuration methods |

---

## 3. Multi-Channel Scanning Infrastructure

### 3.1 The Problem

IEEE 802.11k Beacon Reports require scanning multiple channels to discover neighbor APs. Standard ns-3 provides `MonitorSnifferRx` callbacks for passive monitoring, but a single-radio STA cannot scan other channels without disassociating.

The required sequence without infrastructure support:
1. Disassociate from current AP
2. Switch channel
3. Listen for beacons
4. Switch to next channel
5. Repeat for all channels
6. Return to original channel
7. Reassociate

This approach disrupts active connections and does not model real enterprise WiFi behavior.

### 3.2 Dual-PHY Solution

Real enterprise APs and STAs use dual-radio or off-channel scanning. The `dual-phy-sniffer` module creates a second PHY:

```
    Node
    ├── Operating PHY (Device 0)
    │   └── Fixed on serving channel
    │   └── Handles data traffic
    │   └── Maintains association
    │
    └── Scanning PHY (Device 1)
        └── Hops through configured channels
        └── Passive beacon monitoring
        └── Reports measurements to protocols
```

**Design:**
- Operating PHY uses even MAC addresses (02:xx:xx:xx:xx:xx)
- Scanning PHY uses odd MAC addresses (01:xx:xx:xx:xx:xx)
- Self-beacons are filtered by MAC address comparison
- Channel hopping is configurable via `SetScanningChannels()` and `SetHopInterval()`

### 3.3 Unified Callback Architecture

Initially, each protocol registered separate `MonitorSnifferRx` callbacks. With multiple protocols, this caused redundant processing. The `UnifiedPhySniffer` consolidates:

- Single callback per PHY
- Frame classified once from 2-byte frame control
- Signal measurements (RCPI, RSNI) computed once
- Interested subscribers receive pre-parsed context

---

## 4. Source Code Modifications

### 4.1 StaWifiMac Roaming APIs

**Location:** `src/wifi/model/sta-wifi-mac.{h,cc}`

```cpp
void InitiateRoaming(Mac48Address targetBssid, uint8_t channel, WifiPhyBand band);
void AssociateToAp(Mac48Address targetBssid, uint8_t channel, WifiPhyBand band);
void ForcedDisassociate();
void EnableAutoReconnect(bool enable);
```

**Reasoning:** IEEE 802.11v BSS Transition Management requires programmatic roaming. When an AP sends a BSS TM Request with a candidate list, the STA must be able to roam to the specified target. Without these APIs, there is no mechanism for the simulation to act on steering commands.

### 4.2 Link Quality Accessors

**Location:** `src/wifi/model/sta-wifi-mac.{h,cc}`

```cpp
double GetCurrentRssi() const;
double GetCurrentSnr() const;
std::optional<Mac48Address> GetCurrentBssid() const;
```

**Reasoning:** IEEE 802.11k Link Measurement Reports require the STA to report its current signal quality. The existing StaWifiMac tracks RSSI internally but does not expose it. These read-only accessors enable RCPI/RSNI calculation.

### 4.3 Background Scanning APIs

**Location:** `src/wifi/model/sta-wifi-mac.{h,cc}`

```cpp
void EnableBackgroundScanning(bool enable);
void TriggerBackgroundScan();
std::vector<ScanResult> GetScanResults() const;
void ClearScanResults();
```

**Reasoning:** IEEE 802.11k Beacon Reports require the STA to scan for neighbor beacons. Standard ns-3 scanning causes disassociation. Background scanning maintains association while discovering neighbors.

### 4.4 Roaming TracedCallbacks

**Location:** `src/wifi/model/sta-wifi-mac.{h,cc}`

```cpp
TracedCallback<Time, Mac48Address, Mac48Address, double, double> m_roamingInitiatedLogger;
TracedCallback<Time, Mac48Address, Mac48Address, double, bool> m_roamingCompletedLogger;
TracedCallback<...> m_backgroundScanCompleteLogger;
TracedCallback<...> m_linkQualityUpdateLogger;
```

**Reasoning:** Research into roaming algorithms requires detailed event logging. These follow ns-3's TracedCallback pattern.

### 4.5 2.4 GHz Channel Specification Fix

**Location:** `sta-wifi-mac.cc`, `ap-wifi-mac.cc`, `wifi-mac.cc`

**Issue:** Setting channel with `width=0` fails for 2.4 GHz due to multiple channel definitions (DSSS 22MHz, OFDM 20MHz, OFDM 40MHz).

**Solution:**
```cpp
if (channelNumber >= 1 && channelNumber <= 14) {
    band = WIFI_PHY_BAND_2_4GHZ;
    width = 20;  // Explicit width required
} else {
    band = WIFI_PHY_BAND_5GHZ;
    width = 0;   // Auto-detect works
}
```

**Reasoning:** This is a bug fix. Any simulation switching to 2.4 GHz at runtime encounters this error.

---

## 5. Areas Requiring Further Development

### 5.1 AP Channel Switching Mechanism

**Current Implementation:** When an AP switches channels, the implementation applies the new channel to associated STAs by iterating through the node list and modifying PHY settings directly.

**IEEE 802.11 Specification:** The standard defines Channel Switch Announcement (CSA) frames. The AP should broadcast CSA elements indicating target channel and countdown.

**Path Forward:** Implementing CSA frame generation and handling would make this functionality fully standards-compliant.

### 5.2 Coordinated Channel Switch Flag

**Current Implementation:** `SetCoordinatedChannelSwitch(bool)` prevents STAs from disassociating during AP-initiated channel changes.

**IEEE 802.11 Specification:** STAs receiving CSA frames understand the channel change is coordinated. Without CSA, STAs would legitimately lose the AP.

**Path Forward:** This flag models the end result of CSA. Once CSA is implemented, this flag becomes unnecessary.
