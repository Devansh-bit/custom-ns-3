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
 */

#ifndef SIMULATION_EVENT_PRODUCER_H
#define SIMULATION_EVENT_PRODUCER_H

#include "simulation-event.h"

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"

#include <kafka/KafkaProducer.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ns3
{

/**
 * \ingroup applications
 * \brief Kafka producer for simulation events with batched publishing
 *
 * This application collects simulation events and publishes them to a Kafka topic
 * in batches at regular intervals. Events are buffered in memory and flushed
 * periodically to reduce Kafka overhead.
 *
 * Usage:
 * 1. Configure with SetBrokers(), SetTopic(), SetSimulationId()
 * 2. Set flush interval with SetFlushInterval()
 * 3. Set node ID mappings with SetNodeIdMappings()
 * 4. Record events using RecordEvent() or the convenience methods
 * 5. Events are automatically flushed at the configured interval
 */
class SimulationEventProducer : public Application
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    SimulationEventProducer();
    ~SimulationEventProducer() override;

    /**
     * \brief Set the Kafka broker address
     * \param brokers Comma-separated list of brokers (e.g., "localhost:9092")
     */
    void SetBrokers(const std::string& brokers);

    /**
     * \brief Set the Kafka topic name
     * \param topic Topic name (e.g., "simulator-events")
     */
    void SetTopic(const std::string& topic);

    /**
     * \brief Set the simulation ID used in batch metadata
     * \param simId Simulation identifier (e.g., "sim-001")
     */
    void SetSimulationId(const std::string& simId);

    /**
     * \brief Set the flush interval for batched event publishing
     * \param interval Time between batch flushes (default: 100ms)
     */
    void SetFlushInterval(Time interval);

    /**
     * \brief Set node ID mappings (simulation node ID -> config node ID)
     * \param staMapping STA node ID mapping
     * \param apMapping AP node ID mapping
     */
    void SetNodeIdMappings(const std::map<uint32_t, uint32_t>& staMapping,
                           const std::map<uint32_t, uint32_t>& apMapping);

    /**
     * \brief Record a simulation event to be published
     *
     * Events are buffered and published in batches.
     *
     * \param event The simulation event to record
     */
    void RecordEvent(const SimulationEvent& event);

    /**
     * \brief Record a BSS TM Request Sent event
     * \param apConfigNodeId Config node ID of the AP sending the request
     * \param staConfigNodeId Config node ID of the target STA
     * \param reason Reason for the BSS TM request
     * \param rssiDbm Current RSSI value
     * \param targetBssid Target BSSID suggested
     */
    void RecordBssTmRequestSent(uint32_t apConfigNodeId,
                                 uint32_t staConfigNodeId,
                                 const std::string& reason,
                                 double rssiDbm,
                                 const std::string& targetBssid);

    /**
     * \brief Record a Client Roamed event
     * \param staConfigNodeId Config node ID of the roaming STA
     * \param sourceApConfigNodeId Config node ID of the source AP
     * \param targetApConfigNodeId Config node ID of the target AP
     */
    void RecordClientRoamed(uint32_t staConfigNodeId,
                            uint32_t sourceApConfigNodeId,
                            uint32_t targetApConfigNodeId);

    /**
     * \brief Record an Association event
     * \param staConfigNodeId Config node ID of the associating STA
     * \param apConfigNodeId Config node ID of the AP
     * \param bssid BSSID of the AP
     */
    void RecordAssociation(uint32_t staConfigNodeId,
                           uint32_t apConfigNodeId,
                           const std::string& bssid);

    /**
     * \brief Record a Deassociation event
     * \param staConfigNodeId Config node ID of the deassociating STA
     * \param apConfigNodeId Config node ID of the AP
     * \param bssid BSSID of the AP
     */
    void RecordDeassociation(uint32_t staConfigNodeId,
                             uint32_t apConfigNodeId,
                             const std::string& bssid);

    /**
     * \brief Record a Config Received event
     * \param nodeConfigId Config node ID of the node receiving config
     * \param configType Type of configuration received
     */
    void RecordConfigReceived(uint32_t nodeConfigId, const std::string& configType);

    /**
     * \brief Record a Channel Switch event
     * \param nodeConfigId Config node ID of the switching node
     * \param oldChannel Previous channel number
     * \param newChannel New channel number
     */
    void RecordChannelSwitch(uint32_t nodeConfigId, uint8_t oldChannel, uint8_t newChannel);

    /**
     * \brief Record a Power Switch event
     * \param nodeConfigId Config node ID of the node changing power
     * \param oldPowerDbm Previous TX power in dBm
     * \param newPowerDbm New TX power in dBm
     * \param reason Reason for the power change (e.g., "kafka", "rl", "dfs")
     */
    void RecordPowerSwitch(uint32_t nodeConfigId,
                           double oldPowerDbm,
                           double newPowerDbm,
                           const std::string& reason);

    /**
     * \brief Record a Power Switch event with RACEBOT context
     * \param nodeConfigId Config node ID of the node changing power
     * \param oldPowerDbm Previous TX power in dBm
     * \param newPowerDbm New TX power in dBm
     * \param reason Reason for the power change (e.g., "racebot")
     * \param currentObsspdDbm Current OBSS-PD threshold in dBm
     * \param goalObsspdDbm Goal OBSS-PD threshold in dBm
     * \param inNonWifiMode Whether in non-WiFi interference mode
     * \param goalRecalculated Whether t1 (goal recalc) triggered this cycle
     */
    void RecordPowerSwitch(uint32_t nodeConfigId,
                           double oldPowerDbm,
                           double newPowerDbm,
                           const std::string& reason,
                           double currentObsspdDbm,
                           double goalObsspdDbm,
                           bool inNonWifiMode,
                           bool goalRecalculated);

    /**
     * \brief Record an OBSS-PD Goal Change event
     * \param nodeConfigId Config node ID of the node changing OBSS-PD goal
     * \param oldGoalDbm Previous OBSS-PD goal in dBm
     * \param newGoalDbm New OBSS-PD goal in dBm
     * \param reason Reason for the goal change (e.g., "racebot_t1", "kafka")
     */
    void RecordObssPdGoalChange(uint32_t nodeConfigId,
                                double oldGoalDbm,
                                double newGoalDbm,
                                const std::string& reason);

    /**
     * \brief Record a Load Balance Check event
     * \param apConfigNodeId Config node ID of the overloaded AP
     * \param currentUtilization Current channel utilization (0-1 scale)
     * \param threshold Utilization threshold (0-1 scale)
     * \param associatedStas Number of associated STAs
     * \param offloadedStas Number of STAs offloaded in this check
     */
    void RecordLoadBalanceCheck(uint32_t apConfigNodeId,
                                 double currentUtilization,
                                 double threshold,
                                 uint32_t associatedStas,
                                 uint32_t offloadedStas);

    /**
     * \brief Record a DFS Radar Detected event
     * \param detectorNodeId Config node ID of the node that detected radar
     * \param dfsChannel DFS channel where radar was detected
     * \param receiverChannel Channel the detector was operating on
     * \param affectedChannels String representation of affected channels (e.g., "[52,56,60]")
     */
    void RecordDfsRadarDetected(uint32_t detectorNodeId,
                                uint8_t dfsChannel,
                                uint8_t receiverChannel,
                                const std::string& affectedChannels);

    /**
     * \brief Record a DFS Channel Switch event
     * \param apConfigNodeId Config node ID of the AP switching channels
     * \param oldChannel Previous channel number
     * \param newChannel New channel number
     * \param reason Reason for the switch (e.g., "DFS_RADAR")
     */
    void RecordDfsChannelSwitch(uint32_t apConfigNodeId,
                                uint8_t oldChannel,
                                uint8_t newChannel,
                                const std::string& reason);

    /**
     * \brief Record a Force DFS stress test event
     * \param radarDurationSec Duration the radar will be active (seconds)
     */
    void RecordStressTestForceDfs(double radarDurationSec);

    /**
     * \brief Record a High Interference stress test event
     * \param apsSwitched Number of APs switched to 2.4GHz
     * \param channelList List of 2.4GHz channels used (e.g., "1,6,11")
     */
    void RecordStressTestHighInterference(uint32_t apsSwitched, const std::string& channelList);

    /**
     * \brief Record a High Throughput stress test event
     * \param stasBoosted Number of STAs with boosted throughput
     * \param boostFactor Multiplier applied to rates (e.g., 3.0)
     * \param durationSec Duration of the boost in seconds
     */
    void RecordStressTestHighThroughput(uint32_t stasBoosted, double boostFactor, double durationSec);

    /**
     * \brief Get the config node ID for a STA simulation node ID
     * \param simNodeId Simulation node ID
     * \return Config node ID, or simNodeId if not found
     */
    uint32_t GetStaConfigNodeId(uint32_t simNodeId) const;

    /**
     * \brief Get the config node ID for an AP simulation node ID
     * \param simNodeId Simulation node ID
     * \return Config node ID, or simNodeId if not found
     */
    uint32_t GetApConfigNodeId(uint32_t simNodeId) const;

    /**
     * \brief Get statistics about recorded events
     * \return Map of event type name to count
     */
    std::map<std::string, uint64_t> GetEventCounts() const;

    /**
     * \brief Get total number of events recorded
     * \return Total event count
     */
    uint64_t GetTotalEventsRecorded() const;

    /**
     * \brief Get total number of batches sent
     * \return Batch count
     */
    uint64_t GetBatchesSent() const;

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    /**
     * \brief Flush buffered events to Kafka
     */
    void FlushEvents();

    /**
     * \brief Schedule the next flush
     */
    void ScheduleNextFlush();

    std::string m_brokers;      ///< Kafka broker addresses
    std::string m_topic;        ///< Kafka topic name
    std::string m_simulationId; ///< Simulation ID for batch metadata
    Time m_flushInterval;       ///< Interval between batch flushes

    EventId m_flushEvent; ///< Event for periodic flushing

    std::unique_ptr<kafka::clients::producer::KafkaProducer> m_producer;  ///< Kafka producer (RAII)

    std::vector<SimulationEvent> m_eventBuffer; ///< Buffer of pending events

    std::map<uint32_t, uint32_t> m_staNodeIdMapping; ///< STA sim -> config node ID
    std::map<uint32_t, uint32_t> m_apNodeIdMapping;  ///< AP sim -> config node ID

    std::map<std::string, uint64_t> m_eventCounts; ///< Count of events by type
    uint64_t m_totalEventsRecorded;                ///< Total events recorded
    uint64_t m_batchesSent;                        ///< Total batches sent
};

} // namespace ns3

#endif /* SIMULATION_EVENT_PRODUCER_H */
