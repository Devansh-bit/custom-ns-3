#include "ap-link-measurement-helper.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/wifi-mac.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/node.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("ApLinkMeasurementHelper");

ApLinkMeasurementHelper::ApLinkMeasurementHelper()
    : m_measurementInterval(Seconds(1.0)),
      m_running(false)
{
    NS_LOG_FUNCTION(this);
}

ApLinkMeasurementHelper::~ApLinkMeasurementHelper()
{
    NS_LOG_FUNCTION(this);
}

void
ApLinkMeasurementHelper::SetMeasurementInterval(Time interval)
{
    NS_LOG_FUNCTION(this << interval);
    m_measurementInterval = interval;
}

void
ApLinkMeasurementHelper::RegisterApDevices(NetDeviceContainer apDevices,
                                          std::vector<Ptr<LinkMeasurementProtocol>> apProtocols)
{
    NS_LOG_FUNCTION(this);

    NS_ASSERT_MSG(apDevices.GetN() == apProtocols.size(),
                  "Number of AP devices must match number of protocol instances");

    for (uint32_t i = 0; i < apDevices.GetN(); i++)
    {
        Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(apDevices.Get(i));
        NS_ASSERT_MSG(wifiDevice, "Device must be a WifiNetDevice");

        uint32_t nodeId = wifiDevice->GetNode()->GetId();
        Mac48Address apMac = Mac48Address::ConvertFrom(wifiDevice->GetAddress());

        m_apDevices[nodeId] = wifiDevice;
        m_apProtocols[nodeId] = apProtocols[i];

        NS_LOG_INFO("Registered AP Node " << nodeId << " (MAC: " << apMac
                    << ") for AP→STA link measurements");
    }
}

void
ApLinkMeasurementHelper::Start()
{
    NS_LOG_FUNCTION(this);

    if (m_running)
    {
        NS_LOG_WARN("ApLinkMeasurementHelper is already running");
        return;
    }

    m_running = true;

    NS_LOG_UNCOND("[ApLinkMeasurementHelper] Starting AP→STA link measurement requests");
    NS_LOG_UNCOND("[ApLinkMeasurementHelper]   - Interval: " << m_measurementInterval.GetSeconds() << " s");
    NS_LOG_UNCOND("[ApLinkMeasurementHelper]   - Registered APs: " << m_apDevices.size());

    // Schedule first round of requests
    Simulator::Schedule(Seconds(2.0), &ApLinkMeasurementHelper::SendPeriodicRequests, this);
}

void
ApLinkMeasurementHelper::Stop()
{
    NS_LOG_FUNCTION(this);
    m_running = false;
    NS_LOG_INFO("ApLinkMeasurementHelper stopped");
}

void
ApLinkMeasurementHelper::SendPeriodicRequests()
{
    NS_LOG_FUNCTION(this);

    if (!m_running)
    {
        return;  // Stopped, don't reschedule
    }

    uint32_t totalRequestsSent = 0;

    // Iterate through all registered APs
    for (const auto& [nodeId, apDevice] : m_apDevices)
    {
        // Get AP MAC
        Ptr<WifiMac> wifiMac = apDevice->GetMac();
        Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(wifiMac);

        if (!apMac)
        {
            NS_LOG_WARN("Node " << nodeId << " is not an AP, skipping");
            continue;
        }

        // Get LinkMeasurementProtocol instance for this AP
        auto protocolIt = m_apProtocols.find(nodeId);
        if (protocolIt == m_apProtocols.end())
        {
            NS_LOG_WARN("No LinkMeasurementProtocol found for AP Node " << nodeId);
            continue;
        }

        Ptr<LinkMeasurementProtocol> protocol = protocolIt->second;

        // Get list of associated STAs (linkId = 0 for single-link operation)
        std::map<uint16_t, Mac48Address> staList = apMac->GetStaList(0);

        if (staList.empty())
        {
            NS_LOG_DEBUG("AP Node " << nodeId << " has no associated STAs");
            continue;
        }

        // Send link measurement request to each associated STA
        for (const auto& [aid, staMac] : staList)
        {
            NS_LOG_DEBUG("AP Node " << nodeId << " sending link measurement request to STA "
                         << staMac << " (AID=" << aid << ")");

            protocol->SendLinkMeasurementRequest(staMac, 20, 30);
            totalRequestsSent++;
        }
    }

    NS_LOG_DEBUG("[AP→STA] Sent " << totalRequestsSent << " link measurement requests at t="
                 << Simulator::Now().GetSeconds() << "s");

    // Schedule next round
    Simulator::Schedule(m_measurementInterval,
                        &ApLinkMeasurementHelper::SendPeriodicRequests,
                        this);
}

} // namespace ns3
