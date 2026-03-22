//
// Copyright (c) 2024
//
// SPDX-License-Identifier: GPL-2.0-only
//

#include "adaptive-udp-application.h"

#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/double.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/packet-socket-address.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/socket.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("AdaptiveUdpApplication");

NS_OBJECT_ENSURE_REGISTERED(AdaptiveUdpApplication);

TypeId
AdaptiveUdpApplication::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::AdaptiveUdpApplication")
            .SetParent<SourceApplication>()
            .SetGroupName("Applications")
            .AddConstructor<AdaptiveUdpApplication>()
            .AddAttribute("InitialDataRate",
                          "Initial sending data rate.",
                          DataRateValue(DataRate("15Mbps")),
                          MakeDataRateAccessor(&AdaptiveUdpApplication::m_initialRate),
                          MakeDataRateChecker())
            .AddAttribute("MinDataRate",
                          "Minimum data rate floor.",
                          DataRateValue(DataRate("100Kbps")),
                          MakeDataRateAccessor(&AdaptiveUdpApplication::m_minRate),
                          MakeDataRateChecker())
            .AddAttribute("MaxDataRate",
                          "Maximum data rate ceiling.",
                          DataRateValue(DataRate("20Mbps")),
                          MakeDataRateAccessor(&AdaptiveUdpApplication::m_maxRate),
                          MakeDataRateChecker())
            .AddAttribute("PacketSize",
                          "Size of packets sent.",
                          UintegerValue(1400),
                          MakeUintegerAccessor(&AdaptiveUdpApplication::m_pktSize),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("MaxBytes",
                          "Total bytes to send (0 = unlimited).",
                          UintegerValue(0),
                          MakeUintegerAccessor(&AdaptiveUdpApplication::m_maxBytes),
                          MakeUintegerChecker<uint64_t>())
            .AddAttribute("Protocol",
                          "The type of protocol to use.",
                          TypeIdValue(UdpSocketFactory::GetTypeId()),
                          MakeTypeIdAccessor(&AdaptiveUdpApplication::m_tid),
                          MakeTypeIdChecker())
            .AddAttribute("BackoffMultiplier",
                          "Rate multiplier on backoff (e.g., 0.5 = halve rate).",
                          DoubleValue(0.5),
                          MakeDoubleAccessor(&AdaptiveUdpApplication::m_backoffMultiplier),
                          MakeDoubleChecker<double>(0.1, 0.9))
            .AddAttribute("MaxBackoffCount",
                          "Maximum consecutive backoffs before hitting floor.",
                          UintegerValue(10),
                          MakeUintegerAccessor(&AdaptiveUdpApplication::m_maxBackoffCount),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("BackoffCooldown",
                          "Minimum time between backoff decisions.",
                          TimeValue(MilliSeconds(10)),
                          MakeTimeAccessor(&AdaptiveUdpApplication::m_backoffCooldown),
                          MakeTimeChecker())
            .AddAttribute("AdditiveIncrease",
                          "Rate increase per success window (bps).",
                          UintegerValue(100000),
                          MakeUintegerAccessor(&AdaptiveUdpApplication::m_additiveIncrease),
                          MakeUintegerChecker<uint64_t>())
            .AddAttribute("SuccessThreshold",
                          "Successful sends needed before rate increase.",
                          UintegerValue(10),
                          MakeUintegerAccessor(&AdaptiveUdpApplication::m_successThreshold),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("EnableSeqTsSizeHeader",
                          "Enable SeqTsSizeHeader for sequence/timestamp.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&AdaptiveUdpApplication::m_enableSeqTsSizeHeader),
                          MakeBooleanChecker())
            .AddTraceSource("Tx",
                            "A new packet is sent",
                            MakeTraceSourceAccessor(&AdaptiveUdpApplication::m_txTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("TxWithAddresses",
                            "A new packet is sent with addresses",
                            MakeTraceSourceAccessor(&AdaptiveUdpApplication::m_txTraceWithAddresses),
                            "ns3::Packet::TwoAddressTracedCallback")
            .AddTraceSource("TxWithSeqTsSize",
                            "A new packet is sent with SeqTsSizeHeader",
                            MakeTraceSourceAccessor(&AdaptiveUdpApplication::m_txTraceWithSeqTsSize),
                            "ns3::PacketSink::SeqTsSizeCallback")
            .AddTraceSource("RateChange",
                            "Data rate changed (oldRate, newRate)",
                            MakeTraceSourceAccessor(&AdaptiveUdpApplication::m_rateChangeTrace),
                            "ns3::AdaptiveUdpApplication::RateChangeCallback");
    return tid;
}

AdaptiveUdpApplication::AdaptiveUdpApplication()
    : m_socket(nullptr),
      m_connected(false),
      m_totBytes(0),
      m_unsentPacket(nullptr),
      m_backoffCount(0),
      m_successCount(0)
{
    NS_LOG_FUNCTION(this);
}

AdaptiveUdpApplication::~AdaptiveUdpApplication()
{
    NS_LOG_FUNCTION(this);
}

void
AdaptiveUdpApplication::SetMaxBytes(uint64_t maxBytes)
{
    NS_LOG_FUNCTION(this << maxBytes);
    m_maxBytes = maxBytes;
}

Ptr<Socket>
AdaptiveUdpApplication::GetSocket() const
{
    NS_LOG_FUNCTION(this);
    return m_socket;
}

DataRate
AdaptiveUdpApplication::GetCurrentRate() const
{
    return m_currentRate;
}

void
AdaptiveUdpApplication::SetMaxRate(DataRate rate)
{
    NS_LOG_FUNCTION(this << rate);
    DataRate oldRate = m_maxRate;
    m_maxRate = rate;
    NS_LOG_INFO("MaxRate changed from " << oldRate << " to " << m_maxRate);
}

void
AdaptiveUdpApplication::SetCurrentRate(DataRate rate)
{
    NS_LOG_FUNCTION(this << rate);
    DataRate oldRate = m_currentRate;

    // Clamp to min/max bounds
    if (rate.GetBitRate() < m_minRate.GetBitRate())
        rate = m_minRate;
    if (rate.GetBitRate() > m_maxRate.GetBitRate())
        rate = m_maxRate;

    m_currentRate = rate;
    RecalculateSendInterval();
    m_rateChangeTrace(oldRate, m_currentRate);
    NS_LOG_INFO("CurrentRate changed from " << oldRate << " to " << m_currentRate);
}

void
AdaptiveUdpApplication::BoostThroughput(double factor)
{
    NS_LOG_FUNCTION(this << factor);

    // Boost max rate first
    uint64_t newMaxBitRate = static_cast<uint64_t>(m_maxRate.GetBitRate() * factor);
    m_maxRate = DataRate(newMaxBitRate);

    // Boost current rate
    DataRate oldRate = m_currentRate;
    uint64_t newCurrentBitRate = static_cast<uint64_t>(m_currentRate.GetBitRate() * factor);

    // Clamp to new max
    if (newCurrentBitRate > newMaxBitRate)
        newCurrentBitRate = newMaxBitRate;

    m_currentRate = DataRate(newCurrentBitRate);
    RecalculateSendInterval();
    m_rateChangeTrace(oldRate, m_currentRate);

    NS_LOG_INFO("BoostThroughput(" << factor << "): rate " << oldRate
                << " -> " << m_currentRate << ", max=" << m_maxRate);
}

DataRate
AdaptiveUdpApplication::GetMaxRate() const
{
    return m_maxRate;
}

void
AdaptiveUdpApplication::DoDispose()
{
    NS_LOG_FUNCTION(this);
    CancelEvents();
    m_socket = nullptr;
    m_unsentPacket = nullptr;
    Application::DoDispose();
}

void
AdaptiveUdpApplication::StartApplication()
{
    NS_LOG_FUNCTION(this);

    // Initialize rate control
    m_currentRate = m_initialRate;
    RecalculateSendInterval();
    m_backoffCount = 0;
    m_successCount = 0;
    m_lastBackoffTime = Time(0);

    NS_LOG_INFO("AdaptiveUdpApplication starting with rate " << m_currentRate);

    // Create the socket if not already
    if (!m_socket)
    {
        m_socket = Socket::CreateSocket(GetNode(), m_tid);
        int ret = -1;

        NS_ABORT_MSG_IF(m_peer.IsInvalid(), "'Remote' attribute not properly set");

        if (!m_local.IsInvalid())
        {
            NS_ABORT_MSG_IF((Inet6SocketAddress::IsMatchingType(m_peer) &&
                             InetSocketAddress::IsMatchingType(m_local)) ||
                                (InetSocketAddress::IsMatchingType(m_peer) &&
                                 Inet6SocketAddress::IsMatchingType(m_local)),
                            "Incompatible peer and local address IP version");
            ret = m_socket->Bind(m_local);
        }
        else
        {
            if (Inet6SocketAddress::IsMatchingType(m_peer))
            {
                ret = m_socket->Bind6();
            }
            else if (InetSocketAddress::IsMatchingType(m_peer) ||
                     PacketSocketAddress::IsMatchingType(m_peer))
            {
                ret = m_socket->Bind();
            }
        }

        if (ret == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket");
        }

        m_socket->SetConnectCallback(MakeCallback(&AdaptiveUdpApplication::ConnectionSucceeded, this),
                                     MakeCallback(&AdaptiveUdpApplication::ConnectionFailed, this));

        // Register send callback to retry when buffer space becomes available
        m_socket->SetSendCallback(MakeCallback(&AdaptiveUdpApplication::DataSend, this));

        if (InetSocketAddress::IsMatchingType(m_peer))
        {
            m_socket->SetIpTos(m_tos);
        }
        m_socket->Connect(m_peer);
        m_socket->SetAllowBroadcast(true);
        m_socket->ShutdownRecv();
    }

    CancelEvents();

    if (m_connected)
    {
        ScheduleNextTx();
    }
}

void
AdaptiveUdpApplication::StopApplication()
{
    NS_LOG_FUNCTION(this);

    CancelEvents();
    if (m_socket)
    {
        m_socket->Close();
    }
    else
    {
        NS_LOG_WARN("AdaptiveUdpApplication found null socket to close in StopApplication");
    }
}

void
AdaptiveUdpApplication::CancelEvents()
{
    NS_LOG_FUNCTION(this);
    Simulator::Cancel(m_sendEvent);
    if (m_unsentPacket)
    {
        NS_LOG_DEBUG("Discarding cached packet upon CancelEvents()");
    }
    m_unsentPacket = nullptr;
}

void
AdaptiveUdpApplication::RecalculateSendInterval()
{
    // Time between packets = packet_size_bits / rate_bps
    // Reduce effective data rate by 10x (matches HePhy::GetDataRate divisor)
    double intervalSec = (m_pktSize * 8.0) * 10 / m_currentRate.GetBitRate();
    m_sendInterval = Seconds(intervalSec);
    NS_LOG_LOGIC("Send interval recalculated to " << m_sendInterval.As(Time::MS)
                 << " for rate " << m_currentRate);
}

void
AdaptiveUdpApplication::OnSendSuccess()
{
    NS_LOG_FUNCTION(this);

    m_successCount++;
    m_backoffCount = 0;  // Reset backoff counter on success

    if (m_successCount >= m_successThreshold)
    {
        m_successCount = 0;

        DataRate oldRate = m_currentRate;
        uint64_t newBitRate = m_currentRate.GetBitRate() + m_additiveIncrease;

        if (newBitRate > m_maxRate.GetBitRate())
        {
            newBitRate = m_maxRate.GetBitRate();
        }

        if (newBitRate != m_currentRate.GetBitRate())
        {
            m_currentRate = DataRate(newBitRate);
            RecalculateSendInterval();
            m_rateChangeTrace(oldRate, m_currentRate);
            NS_LOG_INFO("AIMD increase: rate " << oldRate << " -> " << m_currentRate);
        }
    }
}

void
AdaptiveUdpApplication::OnSendFailure()
{
    NS_LOG_FUNCTION(this);

    Time now = Simulator::Now();

    // Rate-limit backoff decisions to avoid multiple backoffs for same congestion event
    if (now - m_lastBackoffTime < m_backoffCooldown)
    {
        NS_LOG_DEBUG("Backoff cooldown active, skipping");
        return;
    }

    m_lastBackoffTime = now;
    m_successCount = 0;  // Reset success counter
    m_backoffCount = std::min(m_backoffCount + 1, m_maxBackoffCount);

    DataRate oldRate = m_currentRate;
    uint64_t newBitRate = static_cast<uint64_t>(m_currentRate.GetBitRate() * m_backoffMultiplier);

    if (newBitRate < m_minRate.GetBitRate())
    {
        newBitRate = m_minRate.GetBitRate();
    }

    if (newBitRate != m_currentRate.GetBitRate())
    {
        m_currentRate = DataRate(newBitRate);
        RecalculateSendInterval();
        m_rateChangeTrace(oldRate, m_currentRate);
        NS_LOG_INFO("AIMD backoff: rate " << oldRate << " -> " << m_currentRate
                    << " (backoff #" << m_backoffCount << ")");
    }
}

void
AdaptiveUdpApplication::ScheduleNextTx()
{
    NS_LOG_FUNCTION(this);

    if (m_maxBytes == 0 || m_totBytes < m_maxBytes)
    {
        NS_LOG_LOGIC("Scheduling next Tx in " << m_sendInterval.As(Time::MS));
        m_sendEvent = Simulator::Schedule(m_sendInterval,
                                          &AdaptiveUdpApplication::SendPacket,
                                          this);
    }
    else
    {
        // All done
        StopApplication();
    }
}

void
AdaptiveUdpApplication::SendPacket()
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT(m_sendEvent.IsExpired());

    Ptr<Packet> packet;
    if (m_unsentPacket)
    {
        packet = m_unsentPacket;
    }
    else if (m_enableSeqTsSizeHeader)
    {
        Address from;
        Address to;
        m_socket->GetSockName(from);
        m_socket->GetPeerName(to);
        SeqTsSizeHeader header;
        header.SetSeq(m_seq++);
        header.SetSize(m_pktSize);
        NS_ABORT_IF(m_pktSize < header.GetSerializedSize());
        packet = Create<Packet>(m_pktSize - header.GetSerializedSize());
        m_txTraceWithSeqTsSize(packet, from, to, header);
        packet->AddHeader(header);
    }
    else
    {
        packet = Create<Packet>(m_pktSize);
    }

    int actual = m_socket->Send(packet);
    if (actual > 0 && (unsigned)actual == packet->GetSize())
    {
        // SUCCESS
        m_txTrace(packet);
        m_totBytes += m_pktSize;
        m_unsentPacket = nullptr;

        OnSendSuccess();  // AIMD: additive increase logic

        Address localAddress;
        m_socket->GetSockName(localAddress);
        if (InetSocketAddress::IsMatchingType(m_peer))
        {
            NS_LOG_DEBUG("At time " << Simulator::Now().As(Time::S)
                         << " adaptive-udp sent " << packet->GetSize()
                         << " bytes to " << InetSocketAddress::ConvertFrom(m_peer).GetIpv4()
                         << " port " << InetSocketAddress::ConvertFrom(m_peer).GetPort()
                         << " rate=" << m_currentRate);
            m_txTraceWithAddresses(packet, localAddress, InetSocketAddress::ConvertFrom(m_peer));
        }
        else if (Inet6SocketAddress::IsMatchingType(m_peer))
        {
            NS_LOG_DEBUG("At time " << Simulator::Now().As(Time::S)
                         << " adaptive-udp sent " << packet->GetSize()
                         << " bytes to " << Inet6SocketAddress::ConvertFrom(m_peer).GetIpv6()
                         << " port " << Inet6SocketAddress::ConvertFrom(m_peer).GetPort()
                         << " rate=" << m_currentRate);
            m_txTraceWithAddresses(packet, localAddress, Inet6SocketAddress::ConvertFrom(m_peer));
        }
    }
    else
    {
        // FAILURE - buffer full
        NS_LOG_DEBUG("Send failed (actual=" << actual << "), caching packet for retry");
        m_unsentPacket = packet;

        OnSendFailure();  // AIMD: exponential backoff
    }

    // Schedule next transmission
    ScheduleNextTx();
}

void
AdaptiveUdpApplication::ConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    m_connected = true;
    ScheduleNextTx();
}

void
AdaptiveUdpApplication::ConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_FATAL_ERROR("Can't connect");
}

void
AdaptiveUdpApplication::DataSend(Ptr<Socket> socket, uint32_t availableBufferSize)
{
    NS_LOG_FUNCTION(this << socket << availableBufferSize);

    // Retry cached packet when buffer space becomes available
    if (m_unsentPacket && m_connected)
    {
        NS_LOG_DEBUG("Buffer space available (" << availableBufferSize
                     << " bytes), retrying cached packet");
        SendPacket();
    }
}

} // namespace ns3
