/*
 * Copyright (c) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "link-measurement-protocol-helper.h"

#include "ns3/log.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-mac.h"
#include "ns3/unified-phy-sniffer-helper.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("LinkMeasurementProtocolHelper");
NS_OBJECT_ENSURE_REGISTERED(LinkMeasurementProtocolHelper);

TypeId
LinkMeasurementProtocolHelper::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::LinkMeasurementProtocolHelper")
            .SetParent<Object>()
            .SetGroupName("LinkProtocol11k")
            .AddTraceSource(
                "LinkMeasurementRequestReceived",
                "Fired when Link Measurement Request is received",
                MakeTraceSourceAccessor(&LinkMeasurementProtocolHelper::m_requestReceivedTrace),
                "ns3::LinkMeasurementProtocolHelper::LinkMeasurementRequestCallback")
            .AddTraceSource(
                "LinkMeasurementReportReceived",
                "Fired when Link Measurement Report is received",
                MakeTraceSourceAccessor(&LinkMeasurementProtocolHelper::m_reportReceivedTrace),
                "ns3::LinkMeasurementProtocolHelper::LinkMeasurementReportCallback");
    return tid;
}

LinkMeasurementProtocolHelper::LinkMeasurementProtocolHelper()
{
    NS_LOG_FUNCTION(this);
}

LinkMeasurementProtocolHelper::~LinkMeasurementProtocolHelper()
{
    NS_LOG_FUNCTION(this);
    m_protocols.clear();
}

void
LinkMeasurementProtocolHelper::InstallOnAp(Ptr<WifiNetDevice> apDevice)
{
    NS_LOG_FUNCTION(this << apDevice);

    Mac48Address apMac = apDevice->GetMac()->GetAddress();
    uint32_t nodeId = apDevice->GetNode()->GetId();

    // Check if already installed
    if (m_protocols.find(apMac) != m_protocols.end())
    {
        NS_LOG_WARN("Link Measurement Protocol already installed on AP " << apMac);
        return;
    }

    // Get or create the singleton UnifiedPhySniffer for this device
    Ptr<UnifiedPhySniffer> sniffer = UnifiedPhySnifferHelper::GetOrInstall(apDevice);

    // Create protocol instance with external sniffer
    Ptr<LinkMeasurementProtocol> protocol = CreateObject<LinkMeasurementProtocol>();
    protocol->InstallWithSniffer(apDevice, sniffer);

    // Connect trace callbacks
    protocol->TraceConnectWithoutContext(
        "LinkMeasurementRequestReceived",
        MakeCallback(&LinkMeasurementProtocolHelper::OnRequestReceived, this));
    protocol->TraceConnectWithoutContext(
        "LinkMeasurementReportReceived",
        MakeCallback(&LinkMeasurementProtocolHelper::OnReportReceived, this));

    m_protocols[apMac] = protocol;

    NS_LOG_INFO("Link Measurement Protocol installed on AP Node " << nodeId
                << " (" << apMac << ") using singleton sniffer");
}

void
LinkMeasurementProtocolHelper::InstallOnSta(Ptr<WifiNetDevice> staDevice)
{
    NS_LOG_FUNCTION(this << staDevice);

    Mac48Address staMac = staDevice->GetMac()->GetAddress();
    uint32_t nodeId = staDevice->GetNode()->GetId();

    // Check if already installed
    if (m_protocols.find(staMac) != m_protocols.end())
    {
        NS_LOG_WARN("Link Measurement Protocol already installed on STA " << staMac);
        return;
    }

    // Get or create the singleton UnifiedPhySniffer for this device
    Ptr<UnifiedPhySniffer> sniffer = UnifiedPhySnifferHelper::GetOrInstall(staDevice);

    // Create protocol instance with external sniffer
    Ptr<LinkMeasurementProtocol> protocol = CreateObject<LinkMeasurementProtocol>();
    protocol->InstallWithSniffer(staDevice, sniffer);

    // Connect trace callbacks
    protocol->TraceConnectWithoutContext(
        "LinkMeasurementRequestReceived",
        MakeCallback(&LinkMeasurementProtocolHelper::OnRequestReceived, this));
    protocol->TraceConnectWithoutContext(
        "LinkMeasurementReportReceived",
        MakeCallback(&LinkMeasurementProtocolHelper::OnReportReceived, this));

    m_protocols[staMac] = protocol;

    NS_LOG_INFO("Link Measurement Protocol installed on STA Node " << nodeId
                << " (" << staMac << ") using singleton sniffer");
}

void
LinkMeasurementProtocolHelper::SendLinkMeasurementRequest(Ptr<WifiNetDevice> device,
                                                          Mac48Address to,
                                                          int8_t transmitPowerUsed,
                                                          int8_t maxTransmitPower)
{
    NS_LOG_FUNCTION(this << device << to << (int)transmitPowerUsed << (int)maxTransmitPower);

    Mac48Address deviceMac = device->GetMac()->GetAddress();

    auto it = m_protocols.find(deviceMac);
    if (it == m_protocols.end())
    {
        NS_LOG_WARN("Link Measurement Protocol not installed on device " << deviceMac);
        return;
    }

    it->second->SendLinkMeasurementRequest(to, transmitPowerUsed, maxTransmitPower);
}

Ptr<LinkMeasurementProtocol>
LinkMeasurementProtocolHelper::GetProtocol(Ptr<WifiNetDevice> device) const
{
    Mac48Address deviceMac = device->GetMac()->GetAddress();
    auto it = m_protocols.find(deviceMac);
    if (it != m_protocols.end())
    {
        return it->second;
    }
    return nullptr;
}

void
LinkMeasurementProtocolHelper::OnRequestReceived(Mac48Address from, LinkMeasurementRequest request)
{
    NS_LOG_FUNCTION(this << from);
    m_requestReceivedTrace(from, request);
}

void
LinkMeasurementProtocolHelper::OnReportReceived(Mac48Address from, LinkMeasurementReport report)
{
    NS_LOG_FUNCTION(this << from);
    m_reportReceivedTrace(from, report);
}

} // namespace ns3
