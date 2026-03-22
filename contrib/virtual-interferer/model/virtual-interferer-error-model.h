/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Virtual Interferer Error Model - Drops packets based on interference
 */

#ifndef VIRTUAL_INTERFERER_ERROR_MODEL_H
#define VIRTUAL_INTERFERER_ERROR_MODEL_H

#include "ns3/error-model.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{

/**
 * \brief Error model that drops packets based on virtual interferer effects
 *
 * This error model is installed on WiFi PHYs to simulate packet loss
 * caused by non-WiFi interference (microwave, Bluetooth, radar, etc.)
 *
 * The packet loss rate is dynamically updated by the VirtualInterfererEnvironment
 * based on the current interference conditions.
 */
class VirtualInterfererErrorModel : public ErrorModel
{
public:
    static TypeId GetTypeId();

    VirtualInterfererErrorModel();
    virtual ~VirtualInterfererErrorModel();

    /**
     * \brief Set the current packet loss rate
     * \param rate Packet loss probability (0.0 to 1.0)
     */
    void SetPacketLossRate(double rate);

    /**
     * \brief Get the current packet loss rate
     * \return Packet loss probability (0.0 to 1.0)
     */
    double GetPacketLossRate() const;

    /**
     * \brief Get the number of packets dropped by this model
     * \return Number of dropped packets
     */
    uint64_t GetDroppedPackets() const;

    /**
     * \brief Get the number of packets received by this model
     * \return Number of received packets (passed + dropped)
     */
    uint64_t GetTotalPackets() const;

    /**
     * \brief Reset statistics
     */
    void ResetStats();

private:
    /**
     * \brief Determine if packet should be corrupted (dropped)
     * \param pkt The packet to check
     * \return true if packet should be dropped
     */
    bool DoCorrupt(Ptr<Packet> pkt) override;

    /**
     * \brief Reset the error model state
     */
    void DoReset() override;

    double m_packetLossRate;                    //!< Current packet loss rate (0.0-1.0)
    Ptr<UniformRandomVariable> m_random;        //!< RNG for packet drop decisions
    uint64_t m_droppedPackets;                  //!< Number of dropped packets
    uint64_t m_totalPackets;                    //!< Total packets processed
};

} // namespace ns3

#endif /* VIRTUAL_INTERFERER_ERROR_MODEL_H */
