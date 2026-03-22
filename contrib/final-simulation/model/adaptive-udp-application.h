//
// Copyright (c) 2024
//
// SPDX-License-Identifier: GPL-2.0-only
//

#ifndef ADAPTIVE_UDP_APPLICATION_H
#define ADAPTIVE_UDP_APPLICATION_H

#include "ns3/source-application.h"
#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"
#include "ns3/seq-ts-size-header.h"

namespace ns3
{

class Socket;

/**
 * @ingroup applications
 * @brief UDP application with TCP-like AIMD congestion control.
 *
 * This application sends UDP traffic with adaptive rate control:
 * - Exponential backoff when Socket::Send() fails (buffer full)
 * - Additive increase after successful transmission windows
 * - Configurable rate limits (min/max)
 *
 * Unlike OnOffApplication, this maintains continuous transmission
 * with dynamic rate adaptation based on congestion signals.
 */
class AdaptiveUdpApplication : public SourceApplication
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    AdaptiveUdpApplication();
    ~AdaptiveUdpApplication() override;

    /**
     * @brief Set the total number of bytes to send.
     * @param maxBytes the total number of bytes to send (0 = unlimited)
     */
    void SetMaxBytes(uint64_t maxBytes);

    /**
     * @brief Return a pointer to associated socket.
     * @return pointer to associated socket
     */
    Ptr<Socket> GetSocket() const;

    /**
     * @brief Get current sending data rate.
     * @return current data rate
     */
    DataRate GetCurrentRate() const;

    /**
     * @brief Set the maximum data rate ceiling.
     * @param rate New maximum rate
     */
    void SetMaxRate(DataRate rate);

    /**
     * @brief Set the current sending rate directly.
     * @param rate New current rate (will be clamped to min/max bounds)
     */
    void SetCurrentRate(DataRate rate);

    /**
     * @brief Boost throughput by a multiplier.
     * Multiplies both current rate and max rate by factor.
     * @param factor Multiplier (e.g., 3.0 = triple rates)
     */
    void BoostThroughput(double factor);

    /**
     * @brief Get the maximum data rate.
     * @return Maximum rate ceiling
     */
    DataRate GetMaxRate() const;

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    /**
     * @brief Cancel all pending events.
     */
    void CancelEvents();

    /**
     * @brief Schedule the next packet transmission.
     */
    void ScheduleNextTx();

    /**
     * @brief Send a packet.
     */
    void SendPacket();

    /**
     * @brief Handle successful packet send - additive increase logic.
     */
    void OnSendSuccess();

    /**
     * @brief Handle failed packet send - exponential backoff logic.
     */
    void OnSendFailure();

    /**
     * @brief Recalculate send interval based on current rate.
     */
    void RecalculateSendInterval();

    /**
     * @brief Handle a Connection Succeed event.
     * @param socket the connected socket
     */
    void ConnectionSucceeded(Ptr<Socket> socket);

    /**
     * @brief Handle a Connection Failed event.
     * @param socket the not connected socket
     */
    void ConnectionFailed(Ptr<Socket> socket);

    /**
     * @brief Callback when buffer space becomes available.
     * @param socket the socket
     * @param availableBufferSize available buffer size in bytes
     */
    void DataSend(Ptr<Socket> socket, uint32_t availableBufferSize);

    // Socket and connection state
    Ptr<Socket> m_socket;                //!< Associated socket
    bool m_connected;                    //!< True if connected
    TypeId m_tid;                        //!< Type of the socket used

    // Packet configuration
    uint32_t m_pktSize;                  //!< Size of packets
    uint64_t m_maxBytes;                 //!< Limit total number of bytes sent
    uint64_t m_totBytes;                 //!< Total bytes sent so far
    uint32_t m_seq{0};                   //!< Sequence number
    Ptr<Packet> m_unsentPacket;          //!< Unsent packet cached for future attempt
    bool m_enableSeqTsSizeHeader{true};  //!< Enable SeqTsSizeHeader (default true for loss tracking)

    // Rate control - AIMD state
    DataRate m_currentRate;              //!< Current sending rate
    DataRate m_initialRate;              //!< Initial/starting rate
    DataRate m_minRate;                  //!< Minimum rate floor
    DataRate m_maxRate;                  //!< Maximum rate ceiling
    Time m_sendInterval;                 //!< Current interval between packets

    // Backoff parameters
    double m_backoffMultiplier;          //!< Rate multiplier on failure (e.g., 0.5)
    uint32_t m_backoffCount;             //!< Consecutive backoff counter
    uint32_t m_maxBackoffCount;          //!< Maximum backoff count
    Time m_lastBackoffTime;              //!< Time of last backoff (for rate limiting)
    Time m_backoffCooldown;              //!< Minimum time between backoffs

    // Additive increase parameters
    uint64_t m_additiveIncrease;         //!< Rate increase per success window (bps)
    uint32_t m_successCount;             //!< Consecutive successful sends
    uint32_t m_successThreshold;         //!< Successes needed before rate increase

    // Events
    EventId m_sendEvent;                 //!< Event id of pending "send packet" event

    // Trace sources
    TracedCallback<Ptr<const Packet>> m_txTrace;
    TracedCallback<Ptr<const Packet>, const Address&, const Address&> m_txTraceWithAddresses;
    TracedCallback<Ptr<const Packet>, const Address&, const Address&, const SeqTsSizeHeader&>
        m_txTraceWithSeqTsSize;
    TracedCallback<DataRate, DataRate> m_rateChangeTrace;  //!< (oldRate, newRate)
};

} // namespace ns3

#endif /* ADAPTIVE_UDP_APPLICATION_H */
