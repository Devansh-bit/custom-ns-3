#include "sta-channel-hopping-helper.h"

#include "ns3/log.h"
#include "ns3/sta-wifi-mac.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("StaChannelHoppingHelper");

StaChannelHoppingHelper::StaChannelHoppingHelper()
    : m_dualPhySniffer(nullptr),
      m_apDevices(nullptr)
{
    NS_LOG_FUNCTION(this);
    m_managerFactory.SetTypeId("ns3::StaChannelHoppingManager");
}

void
StaChannelHoppingHelper::SetAttribute(std::string name, const AttributeValue& value)
{
    NS_LOG_FUNCTION(this << name);
    m_managerFactory.Set(name, value);
}

void
StaChannelHoppingHelper::SetDualPhySniffer(DualPhySnifferHelper* sniffer)
{
    NS_LOG_FUNCTION(this << sniffer);
    m_dualPhySniffer = sniffer;
}

void
StaChannelHoppingHelper::SetApDevices(NetDeviceContainer* apDevices)
{
    NS_LOG_FUNCTION(this << apDevices);
    m_apDevices = apDevices;
}

Ptr<StaChannelHoppingManager>
StaChannelHoppingHelper::Install(Ptr<WifiNetDevice> staDevice)
{
    NS_LOG_FUNCTION(this << staDevice);

    if (!m_dualPhySniffer)
    {
        NS_LOG_ERROR("DualPhySniffer not configured. Call SetDualPhySniffer() first.");
        return nullptr;
    }

    // Verify this is a STA device
    Ptr<WifiMac> mac = staDevice->GetMac();
    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(mac);
    if (!staMac)
    {
        NS_LOG_ERROR("Device " << staDevice << " is not a STA");
        return nullptr;
    }

    // Create manager instance
    Ptr<StaChannelHoppingManager> manager =
        m_managerFactory.Create<StaChannelHoppingManager>();

    // Get node ID for DualPhySniffer MAC lookup
    uint32_t nodeId = staDevice->GetNode()->GetId();
    Mac48Address operatingMac = m_dualPhySniffer->GetOperatingMac(nodeId);

    NS_LOG_INFO("Installing StaChannelHoppingManager on STA " << mac->GetAddress()
                                                              << " (node " << nodeId << ")");
    NS_LOG_INFO("Using DualPhySniffer operating MAC: " << operatingMac);

    // Configure manager
    manager->SetStaDevice(staDevice);
    manager->SetDualPhySniffer(m_dualPhySniffer, operatingMac);
    if (m_apDevices)
    {
        manager->SetApDevices(m_apDevices);
    }

    return manager;
}

std::vector<Ptr<StaChannelHoppingManager>>
StaChannelHoppingHelper::Install(NetDeviceContainer staDevices)
{
    NS_LOG_FUNCTION(this);

    std::vector<Ptr<StaChannelHoppingManager>> managers;

    for (uint32_t i = 0; i < staDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> device = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        if (device)
        {
            Ptr<StaChannelHoppingManager> manager = Install(device);
            if (manager)
            {
                managers.push_back(manager);
            }
        }
        else
        {
            NS_LOG_WARN("Device " << i << " is not a WifiNetDevice, skipping");
        }
    }

    NS_LOG_INFO("Installed StaChannelHoppingManager on " << managers.size() << " STA devices");

    return managers;
}

} // namespace ns3
