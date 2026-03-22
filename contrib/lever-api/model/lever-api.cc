#include "lever-api.h"

#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-mac.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/node-list.h"

#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("LeverApi");

// =============================================================================
// LeverConfig Implementation
// =============================================================================

NS_OBJECT_ENSURE_REGISTERED(LeverConfig);

TypeId
LeverConfig::GetTypeId()
{
    static TypeId tid = TypeId("ns3::LeverConfig")
                            .SetParent<Object>()
                            .SetGroupName("LeverApi")
                            .AddConstructor<LeverConfig>()
                            .AddTraceSource("TxPowerStart",
                                           "Trace fired when TxPowerStart changes",
                                           MakeTraceSourceAccessor(&LeverConfig::m_txPowerStartTrace),
                                           "ns3::LeverConfig::DoubleTracedCallback")
                            .AddTraceSource("TxPowerEnd",
                                           "Trace fired when TxPowerEnd changes",
                                           MakeTraceSourceAccessor(&LeverConfig::m_txPowerEndTrace),
                                           "ns3::LeverConfig::DoubleTracedCallback")
                            .AddTraceSource("CcaEdThreshold",
                                           "Trace fired when CcaEdThreshold changes",
                                           MakeTraceSourceAccessor(&LeverConfig::m_ccaEdThresholdTrace),
                                           "ns3::LeverConfig::DoubleTracedCallback")
                            .AddTraceSource("RxSensitivity",
                                           "Trace fired when RxSensitivity changes",
                                           MakeTraceSourceAccessor(&LeverConfig::m_rxSensitivityTrace),
                                           "ns3::LeverConfig::DoubleTracedCallback")
                            .AddTraceSource("ChannelSettings",
                                           "Trace fired when channel settings change",
                                           MakeTraceSourceAccessor(&LeverConfig::m_channelSettingsTrace),
                                           "ns3::LeverConfig::ChannelSettingsTracedCallback");
    return tid;
}

LeverConfig::LeverConfig()
    : m_txPowerStart(16.0),
      m_txPowerEnd(16.0),
      m_ccaEdThreshold(-82.0),
      m_rxSensitivity(-93.0),
      m_channelNumber(36),
      m_channelWidth(20),
      m_band(BAND_5GHZ),
      m_primary20Index(0)
{
    NS_LOG_FUNCTION(this);
}

LeverConfig::~LeverConfig()
{
    NS_LOG_FUNCTION(this);
}

// PHY Power Configuration Setters
void
LeverConfig::SetTxPower(double txPowerDbm)
{
    NS_LOG_FUNCTION(this << txPowerDbm);
    double oldStart = m_txPowerStart;
    double oldEnd = m_txPowerEnd;

    // Set both values before firing any traces
    m_txPowerStart = txPowerDbm;
    m_txPowerEnd = txPowerDbm;

    // Now fire traces - when callbacks execute, both values will already match
    m_txPowerStartTrace(oldStart, m_txPowerStart);
    m_txPowerEndTrace(oldEnd, m_txPowerEnd);

    NS_LOG_INFO("TxPower changed from Start=" << oldStart << " End=" << oldEnd
                << " to " << m_txPowerStart << " dBm (both)");
}

void
LeverConfig::SetCcaEdThreshold(double thresholdDbm)
{
    NS_LOG_FUNCTION(this << thresholdDbm);
    double oldValue = m_ccaEdThreshold;
    m_ccaEdThreshold = thresholdDbm;
    m_ccaEdThresholdTrace(oldValue, m_ccaEdThreshold);
    NS_LOG_INFO("CcaEdThreshold changed from " << oldValue << " to " << m_ccaEdThreshold << " dBm");
}

void
LeverConfig::SetRxSensitivity(double sensitivityDbm)
{
    NS_LOG_FUNCTION(this << sensitivityDbm);
    double oldValue = m_rxSensitivity;
    m_rxSensitivity = sensitivityDbm;
    m_rxSensitivityTrace(oldValue, m_rxSensitivity);
    NS_LOG_INFO("RxSensitivity changed from " << oldValue << " to " << m_rxSensitivity << " dBm");
}

// Channel Configuration Setters
void
LeverConfig::SwitchChannel(uint16_t channelNumber)
{
    NS_LOG_FUNCTION(this << channelNumber);

    uint8_t actualChannel;
    uint16_t width;
    WifiPhyBandType band;
    uint8_t primary20 = 0;  // Default primary20 index

    // Decode the channel number following IEEE 802.11 standard
    if (channelNumber >= 1 && channelNumber <= 14)
    {
        // 2.4 GHz band: channels 1-14, always 20 MHz
        actualChannel = channelNumber;
        width = 20;
        band = BAND_2_4GHZ;
        NS_LOG_INFO("2.4 GHz channel " << +actualChannel << ", 20 MHz");
    }
    else
    {
        // 5 GHz band: determine width from channel number
        band = BAND_5GHZ;

        // 160 MHz channels: 50, 114
        if (channelNumber == 50 || channelNumber == 114)
        {
            // Use the center channel number as-is with the width
            // primary20=0 means use lowest 20MHz subchannel as primary
            actualChannel = channelNumber;
            width = 160;
            NS_LOG_INFO("5 GHz channel " << +actualChannel << ", 160 MHz (center)");
        }
        // 80 MHz channels: 42, 58, 106, 122, 138, 155
        else if (channelNumber == 42 || channelNumber == 58 || channelNumber == 106 ||
                 channelNumber == 122 || channelNumber == 138 || channelNumber == 155)
        {
            // Use the center channel number as-is with the width
            // primary20=0 means use lowest 20MHz subchannel as primary
            actualChannel = channelNumber;
            width = 80;
            NS_LOG_INFO("5 GHz channel " << +actualChannel << ", 80 MHz (center)");
        }
        // 40 MHz channels: 38, 46, 54, 62, 102, 110, 118, 126, 134, 142, 151, 159
        else if (channelNumber == 38 || channelNumber == 46 || channelNumber == 54 ||
                 channelNumber == 62 || channelNumber == 102 || channelNumber == 110 ||
                 channelNumber == 118 || channelNumber == 126 || channelNumber == 134 ||
                 channelNumber == 142 || channelNumber == 151 || channelNumber == 159)
        {
            // Use the center channel number as-is with the width
            // primary20=0 means use lowest 20MHz subchannel as primary
            actualChannel = channelNumber;
            width = 40;
            NS_LOG_INFO("5 GHz channel " << +actualChannel << ", 40 MHz (center)");
        }
        // 20 MHz channels: 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, etc.
        else
        {
            actualChannel = channelNumber;
            width = 20;
            NS_LOG_INFO("5 GHz channel " << +actualChannel << ", 20 MHz");
        }
    }

    // Update internal state
    m_channelNumber = actualChannel;
    m_channelWidth = width;
    m_band = band;
    m_primary20Index = primary20;

    // Fire trace to trigger OnChannelSettingsChanged callback
    m_channelSettingsTrace(m_channelNumber, m_channelWidth, m_band, m_primary20Index);

    NS_LOG_UNCOND("SwitchChannel(" << channelNumber << ") -> {channel=" << +actualChannel
                << ", width=" << width << " MHz, band=" << (band == BAND_2_4GHZ ? "2.4GHz" : "5GHz")
                << ", primary20=" << +primary20 << "}");
}

// PHY Getters
double
LeverConfig::GetTxPowerStart() const
{
    return m_txPowerStart;
}

double
LeverConfig::GetTxPowerEnd() const
{
    return m_txPowerEnd;
}

double
LeverConfig::GetCcaEdThreshold() const
{
    return m_ccaEdThreshold;
}

double
LeverConfig::GetRxSensitivity() const
{
    return m_rxSensitivity;
}

// Channel Getters
uint8_t
LeverConfig::GetChannelNumber() const
{
    return m_channelNumber;
}

uint16_t
LeverConfig::GetChannelWidth() const
{
    return m_channelWidth;
}

WifiPhyBandType
LeverConfig::GetBand() const
{
    return m_band;
}

uint8_t
LeverConfig::GetPrimary20Index() const
{
    return m_primary20Index;
}

// =============================================================================
// LeverApi Implementation
// =============================================================================

NS_OBJECT_ENSURE_REGISTERED(LeverApi);

TypeId
LeverApi::GetTypeId()
{
    static TypeId tid = TypeId("ns3::LeverApi")
                            .SetParent<Application>()
                            .SetGroupName("LeverApi")
                            .AddConstructor<LeverApi>();
    return tid;
}

LeverApi::LeverApi()
    : m_config(nullptr)
{
    NS_LOG_FUNCTION(this);
}

LeverApi::~LeverApi()
{
    NS_LOG_FUNCTION(this);
}

void
LeverApi::SetConfig(Ptr<LeverConfig> config)
{
    NS_LOG_FUNCTION(this << config);
    m_config = config;
}

void
LeverApi::SwitchChannel(uint16_t channelNumber, uint8_t linkId)
{
    NS_LOG_FUNCTION(this << channelNumber << +linkId);

    Ptr<WifiNetDevice> device = GetWifiNetDevice();
    if (!device)
    {
        NS_LOG_ERROR("No WiFi device found on node " << GetNode()->GetId());
        return;
    }

    Ptr<WifiMac> mac = device->GetMac();
    if (!mac)
    {
        NS_LOG_ERROR("No WifiMac found on device");
        return;
    }

    // Delegate to WifiMac::SwitchChannel - it handles all PHY configuration
    NS_LOG_INFO("Delegating to WifiMac::SwitchChannel(" << channelNumber << ", " << +linkId << ")");
    mac->SwitchChannel(channelNumber, linkId);

    // NOTE: Do NOT call m_config->SwitchChannel() here!
    // WifiMac::SwitchChannel already handles the PHY channel switch.
    // Calling m_config->SwitchChannel() would fire a trace that triggers
    // OnChannelSettingsChanged(), which tries to switch the channel AGAIN,
    // causing a retry storm when PHY is busy (5ms retries pile up).
    // LeverApi getters read directly from PHY, so state is consistent.
}

void
LeverApi::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_config = nullptr;
    Application::DoDispose();
}

void
LeverApi::StartApplication()
{
    NS_LOG_FUNCTION(this);

    if (!m_config)
    {
        NS_LOG_ERROR("No LeverConfig object set for LeverApi application");
        return;
    }

    // Connect to all trace sources
    m_config->TraceConnectWithoutContext("TxPowerStart",
                                        MakeCallback(&LeverApi::OnTxPowerStartChanged, this));
    m_config->TraceConnectWithoutContext("TxPowerEnd",
                                        MakeCallback(&LeverApi::OnTxPowerEndChanged, this));
    m_config->TraceConnectWithoutContext("CcaEdThreshold",
                                        MakeCallback(&LeverApi::OnCcaEdThresholdChanged, this));
    m_config->TraceConnectWithoutContext("RxSensitivity",
                                        MakeCallback(&LeverApi::OnRxSensitivityChanged, this));
    m_config->TraceConnectWithoutContext("ChannelSettings",
                                        MakeCallback(&LeverApi::OnChannelSettingsChanged, this));

    // Apply initial PHY configuration from LeverConfig attributes
    ApplyInitialConfiguration();

    NS_LOG_INFO("LeverApi application started on node " << GetNode()->GetId());
}

void
LeverApi::StopApplication()
{
    NS_LOG_FUNCTION(this);

    if (m_config)
    {
        // Disconnect from all trace sources
        m_config->TraceDisconnectWithoutContext("TxPowerStart",
                                               MakeCallback(&LeverApi::OnTxPowerStartChanged, this));
        m_config->TraceDisconnectWithoutContext("TxPowerEnd",
                                               MakeCallback(&LeverApi::OnTxPowerEndChanged, this));
        m_config->TraceDisconnectWithoutContext("CcaEdThreshold",
                                               MakeCallback(&LeverApi::OnCcaEdThresholdChanged, this));
        m_config->TraceDisconnectWithoutContext("RxSensitivity",
                                               MakeCallback(&LeverApi::OnRxSensitivityChanged, this));
        m_config->TraceDisconnectWithoutContext("ChannelSettings",
                                               MakeCallback(&LeverApi::OnChannelSettingsChanged, this));
    }

    NS_LOG_INFO("LeverApi application stopped on node " << GetNode()->GetId());
}

void
LeverApi::ApplyInitialConfiguration()
{
    NS_LOG_FUNCTION(this);
    
    if (!m_config)
    {
        return;
    }

    Ptr<WifiNetDevice> device = GetWifiNetDevice();
    if (!device)
    {
        return;
    }

    Ptr<WifiPhy> phy = device->GetPhy();
    if (!phy)
    {
        NS_LOG_ERROR("No WifiPhy found on device");
        return;
    }

    // Apply all initial configuration values
    phy->SetTxPowerStart(m_config->GetTxPowerStart());
    phy->SetTxPowerEnd(m_config->GetTxPowerEnd());
    phy->SetCcaEdThreshold(m_config->GetCcaEdThreshold());
    phy->SetRxSensitivity(m_config->GetRxSensitivity());

    // Apply channel settings
    std::ostringstream oss;
    oss << "{" << +m_config->GetChannelNumber() << ", " 
        << m_config->GetChannelWidth() << ", ";

    switch (m_config->GetBand())
    {
        case BAND_2_4GHZ:
            oss << "BAND_2_4GHZ";
            break;
        case BAND_5GHZ:
            oss << "BAND_5GHZ";
            break;
        case BAND_6GHZ:
            oss << "BAND_6GHZ";
            break;
        default:
            oss << "BAND_UNSPECIFIED";
            break;
    }

    oss << ", " << +m_config->GetPrimary20Index() << "}";
    std::string channelSettings = oss.str();

    phy->SetAttribute("ChannelSettings", StringValue(channelSettings));

    NS_LOG_UNCOND("Node " << GetNode()->GetId() 
                << ": Applied initial configuration - Channel: " << channelSettings
                << ", TxPower: " << m_config->GetTxPowerStart() << " dBm");
}

Ptr<WifiNetDevice>
LeverApi::GetWifiNetDevice() const
{
    NS_LOG_FUNCTION(this);

    Ptr<Node> node = GetNode();
    if (!node)
    {
        NS_LOG_ERROR("No node associated with LeverApi application");
        return nullptr;
    }

    // Search for WiFi net device
    for (uint32_t i = 0; i < node->GetNDevices(); ++i)
    {
        Ptr<NetDevice> device = node->GetDevice(i);
        Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(device);
        if (wifiDevice)
        {
            NS_LOG_INFO("GetWifiNetDevice: Found WiFi device at index " << i << " on node " << node->GetId());
            return wifiDevice;
        }
    }

    NS_LOG_WARN("No WifiNetDevice found on node " << node->GetId());
    return nullptr;
}

// PHY Parameter Callbacks
void
LeverApi::OnTxPowerStartChanged(double oldValue, double newValue)
{
    NS_LOG_FUNCTION(this << oldValue << newValue);
    NS_LOG_UNCOND("LeverApi::OnTxPowerStartChanged called: " << oldValue << " -> " << newValue << " dBm on node " << GetNode()->GetId());

    // Only apply to PHY when both Start and End match (single power level case)
    double txPowerEnd = m_config->GetTxPowerEnd();
    if (newValue == txPowerEnd)
    {
        ApplyTxPowerToPhy(newValue);
    }
    else
    {
        NS_LOG_UNCOND("  Skipping PHY update - waiting for TxPowerEnd to match (Start=" << newValue << ", End=" << txPowerEnd << ")");
    }
}

void
LeverApi::OnTxPowerEndChanged(double oldValue, double newValue)
{
    NS_LOG_FUNCTION(this << oldValue << newValue);
    NS_LOG_UNCOND("LeverApi::OnTxPowerEndChanged called: " << oldValue << " -> " << newValue << " dBm on node " << GetNode()->GetId());

    // Only apply to PHY when both Start and End match (single power level case)
    double txPowerStart = m_config->GetTxPowerStart();
    if (newValue == txPowerStart)
    {
        ApplyTxPowerToPhy(newValue);
    }
    else
    {
        NS_LOG_UNCOND("  Skipping PHY update - waiting for TxPowerStart to match (Start=" << txPowerStart << ", End=" << newValue << ")");
    }
}

void
LeverApi::ApplyTxPowerToPhy(double powerDbm)
{
    Ptr<WifiNetDevice> device = GetWifiNetDevice();
    if (!device)
    {
        NS_LOG_UNCOND("ERROR: GetWifiNetDevice() returned nullptr when applying power on node " << GetNode()->GetId());
        return;
    }

    Ptr<WifiPhy> phy = device->GetPhy();
    if (!phy)
    {
        NS_LOG_UNCOND("ERROR: No WifiPhy found when applying power on node " << GetNode()->GetId());
        return;
    }

    // Read current power before change
    double oldPowerStart = phy->GetTxPowerStart();
    double oldPowerEnd = phy->GetTxPowerEnd();
    
    // Set both Start and End to the same value for single power level operation
    phy->SetTxPowerStart(powerDbm);
    phy->SetTxPowerEnd(powerDbm);
    
    // Verify the change was applied
    double newPowerStart = phy->GetTxPowerStart();
    double newPowerEnd = phy->GetTxPowerEnd();
    
    NS_LOG_UNCOND("✓ SUCCESS: Node " << GetNode()->GetId() 
                  << " Applied TX Power = " << powerDbm << " dBm to PHY"
                  << " | Before: Start=" << oldPowerStart << " End=" << oldPowerEnd
                  << " | After: Start=" << newPowerStart << " End=" << newPowerEnd);
}

void
LeverApi::OnCcaEdThresholdChanged(double oldValue, double newValue)
{
    NS_LOG_FUNCTION(this << oldValue << newValue);

    Ptr<WifiNetDevice> device = GetWifiNetDevice();
    if (!device)
    {
        return;
    }

    Ptr<WifiPhy> phy = device->GetPhy();
    if (!phy)
    {
        NS_LOG_ERROR("No WifiPhy found on device");
        return;
    }

    phy->SetCcaEdThreshold(newValue);
    NS_LOG_INFO("Node " << GetNode()->GetId() << ": Applied CcaEdThreshold = " << newValue << " dBm");
}

void
LeverApi::OnRxSensitivityChanged(double oldValue, double newValue)
{
    NS_LOG_FUNCTION(this << oldValue << newValue);

    Ptr<WifiNetDevice> device = GetWifiNetDevice();
    if (!device)
    {
        return;
    }

    Ptr<WifiPhy> phy = device->GetPhy();
    if (!phy)
    {
        NS_LOG_ERROR("No WifiPhy found on device");
        return;
    }

    phy->SetRxSensitivity(newValue);
    NS_LOG_INFO("Node " << GetNode()->GetId() << ": Applied RxSensitivity = " << newValue << " dBm");
}

// Channel Configuration Callback
void
LeverApi::OnChannelSettingsChanged(uint8_t channelNumber, uint16_t widthMhz, WifiPhyBandType band, uint8_t primary20Index)
{
    NS_LOG_FUNCTION(this << +channelNumber << widthMhz << band << +primary20Index);

    Ptr<WifiNetDevice> device = GetWifiNetDevice();
    if (!device)
    {
        return;
    }

    Ptr<WifiPhy> phy = device->GetPhy();
    if (!phy)
    {
        NS_LOG_ERROR("No WifiPhy found on device");
        return;
    }

    // Format the channel settings string for ns3 attribute
    std::ostringstream oss;
    oss << "{" << +channelNumber << ", " << widthMhz << ", ";

    switch (band)
    {
        case BAND_2_4GHZ:
            oss << "BAND_2_4GHZ";
            break;
        case BAND_5GHZ:
            oss << "BAND_5GHZ";
            break;
        case BAND_6GHZ:
            oss << "BAND_6GHZ";
            break;
        default:
            oss << "BAND_UNSPECIFIED";
            break;
    }

    oss << ", " << +primary20Index << "}";

    std::string channelSettings = oss.str();

    // Check PHY state before applying channel change (must be idle)
    bool phyIsBusy = phy->IsStateTx() || phy->IsStateRx() || phy->IsStateSwitching() || phy->IsStateSleep();

    if (phyIsBusy)
    {
        // Determine which state is blocking
        std::string stateStr = "UNKNOWN";
        if (phy->IsStateTx()) stateStr = "TX";
        else if (phy->IsStateRx()) stateStr = "RX";
        else if (phy->IsStateSwitching()) stateStr = "SWITCHING";
        else if (phy->IsStateSleep()) stateStr = "SLEEP";

        NS_LOG_INFO("Node " << GetNode()->GetId() << ": PHY is busy (" << stateStr << "), "
                    << "will retry channel change in 5ms: " << channelSettings);

        // Schedule retry - keep trying until PHY is free (IDLE or CCA_BUSY)
        Simulator::Schedule(MilliSeconds(5), [this, channelNumber, widthMhz, band, primary20Index]() {
            // Recursively call this function to retry
            OnChannelSettingsChanged(channelNumber, widthMhz, band, primary20Index);
        });
        return;
    }

    // Get current PHY operating channel before switching
    const WifiPhyOperatingChannel& currentOpChannel = phy->GetOperatingChannel();
    uint8_t oldChannelNum = 0;
    uint16_t oldWidth = 0;
    std::string oldBand = "UNKNOWN";

    if (currentOpChannel.IsSet())
    {
        oldChannelNum = currentOpChannel.GetNumber();
        oldWidth = currentOpChannel.GetWidth();
        if (currentOpChannel.GetPhyBand() == WIFI_PHY_BAND_2_4GHZ)
            oldBand = "2.4GHz";
        else if (currentOpChannel.GetPhyBand() == WIFI_PHY_BAND_5GHZ)
            oldBand = "5GHz";
        else if (currentOpChannel.GetPhyBand() == WIFI_PHY_BAND_6GHZ)
            oldBand = "6GHz";
    }

    NS_LOG_UNCOND("\n" << std::string(80, '='));
    NS_LOG_UNCOND("[PHY CHANNEL SWITCH] Node " << GetNode()->GetId());
    NS_LOG_UNCOND(std::string(80, '-'));
    NS_LOG_UNCOND("  BEFORE: CH" << +oldChannelNum << " @ " << oldWidth << " MHz (" << oldBand << ")");
    NS_LOG_UNCOND("  AFTER:  CH" << +channelNumber << " @ " << widthMhz << " MHz ("
                  << (band == BAND_2_4GHZ ? "2.4GHz" : band == BAND_5GHZ ? "5GHz" : "6GHz") << ")");
    NS_LOG_UNCOND(std::string(80, '-'));

    // Apply channel settings to this device only
    // Note: STA propagation is now handled by ApWifiMac::SwitchChannel() override
    // when using LeverApi::SwitchChannel() or calling ApWifiMac::SwitchChannel() directly
    phy->SetAttribute("ChannelSettings", StringValue(channelSettings));

    // Verify the channel change took effect
    const WifiPhyOperatingChannel& newOpChannel = phy->GetOperatingChannel();
    if (newOpChannel.IsSet())
    {
        uint8_t verifyChannelNum = newOpChannel.GetNumber();
        uint16_t verifyWidth = newOpChannel.GetWidth();
        NS_LOG_UNCOND("  ✓ VERIFIED: PHY now on CH" << +verifyChannelNum << " @ " << verifyWidth << " MHz");
    }
    else
    {
        NS_LOG_UNCOND("  ⚠ WARNING: Could not verify PHY channel after switch");
    }
    NS_LOG_UNCOND(std::string(80, '=') << "\n");
}

// ============================================================================
// Getters for Channel Parameters
// ============================================================================

uint8_t
LeverApi::GetChannelNumber() const
{
    NS_LOG_FUNCTION(this);
    Ptr<WifiNetDevice> device = GetWifiNetDevice();
    if (!device)
    {
        NS_LOG_WARN("No WiFi device found");
        return 0;
    }

    Ptr<WifiPhy> phy = device->GetPhy();
    if (!phy)
    {
        NS_LOG_WARN("No WifiPhy found");
        return 0;
    }

    const WifiPhyOperatingChannel& opChannel = phy->GetOperatingChannel();
    if (!opChannel.IsSet())
    {
        NS_LOG_WARN("Operating channel not set");
        return 0;
    }

    return opChannel.GetNumber();
}

uint16_t
LeverApi::GetChannelWidth() const
{
    NS_LOG_FUNCTION(this);
    Ptr<WifiNetDevice> device = GetWifiNetDevice();
    if (!device)
    {
        NS_LOG_WARN("No WiFi device found");
        return 20;  // Default to 20 MHz
    }

    Ptr<WifiPhy> phy = device->GetPhy();
    if (!phy)
    {
        NS_LOG_WARN("No WifiPhy found");
        return 20;  // Default to 20 MHz
    }

    return phy->GetChannelWidth();
}

WifiPhyBand
LeverApi::GetBand() const
{
    NS_LOG_FUNCTION(this);
    Ptr<WifiNetDevice> device = GetWifiNetDevice();
    if (!device)
    {
        NS_LOG_WARN("No WiFi device found");
        return WIFI_PHY_BAND_UNSPECIFIED;
    }

    Ptr<WifiPhy> phy = device->GetPhy();
    if (!phy)
    {
        NS_LOG_WARN("No WifiPhy found");
        return WIFI_PHY_BAND_UNSPECIFIED;
    }

    const WifiPhyOperatingChannel& opChannel = phy->GetOperatingChannel();
    if (!opChannel.IsSet())
    {
        NS_LOG_WARN("Operating channel not set");
        return WIFI_PHY_BAND_UNSPECIFIED;
    }

    return opChannel.GetPhyBand();
}

uint8_t
LeverApi::GetPrimary20Index() const
{
    NS_LOG_FUNCTION(this);
    Ptr<WifiNetDevice> device = GetWifiNetDevice();
    if (!device)
    {
        NS_LOG_WARN("No WiFi device found");
        return 0;
    }

    Ptr<WifiPhy> phy = device->GetPhy();
    if (!phy)
    {
        NS_LOG_WARN("No WifiPhy found");
        return 0;
    }

    const WifiPhyOperatingChannel& opChannel = phy->GetOperatingChannel();
    if (!opChannel.IsSet())
    {
        NS_LOG_WARN("Operating channel not set");
        return 0;
    }

    return opChannel.GetPrimaryChannelIndex(20);
}

// ============================================================================
// Getters for TX Power Parameters
// ============================================================================

double
LeverApi::GetTxPowerStart() const
{
    NS_LOG_FUNCTION(this);
    if (!m_config)
    {
        NS_LOG_WARN("No LeverConfig set");
        return 0.0;
    }
    return m_config->GetTxPowerStart();
}

double
LeverApi::GetTxPowerEnd() const
{
    NS_LOG_FUNCTION(this);
    if (!m_config)
    {
        NS_LOG_WARN("No LeverConfig set");
        return 0.0;
    }
    return m_config->GetTxPowerEnd();
}

double
LeverApi::GetCcaEdThreshold() const
{
    NS_LOG_FUNCTION(this);
    if (!m_config)
    {
        NS_LOG_WARN("No LeverConfig set");
        return -82.0;  // Default CCA threshold
    }
    return m_config->GetCcaEdThreshold();
}

double
LeverApi::GetRxSensitivity() const
{
    NS_LOG_FUNCTION(this);
    if (!m_config)
    {
        NS_LOG_WARN("No LeverConfig set");
        return -92.0;  // Default RX sensitivity
    }
    return m_config->GetRxSensitivity();
}

} // namespace ns3