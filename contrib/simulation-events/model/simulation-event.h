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

#ifndef SIMULATION_EVENT_H
#define SIMULATION_EVENT_H

#include "ns3/nstime.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ns3
{

/**
 * \brief Event types for simulation events
 *
 * Extensible enum for different simulation event types.
 * Add new event types here as needed.
 */
enum class SimEventType : uint8_t
{
    BSS_TM_REQUEST_SENT, ///< AP sent BSS TM request to STA
    CLIENT_ROAMED,       ///< STA completed roaming to new AP
    ASSOCIATION,         ///< STA associated with AP
    DEASSOCIATION,       ///< STA deassociated from AP
    CONFIG_RECEIVED,     ///< Config received from Kafka
    CHANNEL_SWITCH,      ///< AP/STA channel switch
    POWER_SWITCH,        ///< TX power change
    LOAD_BALANCE_CHECK,  ///< Load balancing check performed on AP
    DFS_RADAR_DETECTED,  ///< Radar detected on DFS channel
    DFS_CHANNEL_SWITCH,   ///< AP channel switch due to DFS radar
    OBSS_PD_GOAL_CHANGE,  ///< OBSS-PD threshold goal changed
    STRESS_TEST_FORCE_DFS,        ///< Force DFS stress test triggered
    STRESS_TEST_HIGH_INTERFERENCE, ///< High interference stress test triggered
    STRESS_TEST_HIGH_THROUGHPUT   ///< High throughput stress test triggered
};

/**
 * \brief Simulation event structure
 *
 * Represents a single simulation event that will be published to Kafka.
 * Uses a flexible eventData map for event-specific fields.
 */
struct SimulationEvent
{
    std::string eventId;      ///< Unique event ID
    SimEventType eventType;   ///< Type of event
    Time simTimestamp;        ///< Simulation timestamp when event occurred

    uint32_t primaryNodeId;   ///< Config node ID of primary actor (e.g., STA for roaming)
    uint32_t secondaryNodeId; ///< Config node ID of secondary actor (e.g., target AP)
    uint32_t tertiaryNodeId;  ///< Config node ID of tertiary actor (e.g., source AP)

    /// Event-specific data as key-value pairs (flexible schema)
    std::map<std::string, std::string> eventData;

    /**
     * \brief Generate a unique event ID
     *
     * Format: {TYPE}-{TIMESTAMP_MS}-N{PRIMARY}-N{SECONDARY}-{SEQ}
     * Example: BTMRQ-00015000-N001-N005-00001
     *
     * \param type Event type
     * \param timestamp Simulation timestamp
     * \param primaryNode Config node ID of primary node
     * \param secondaryNode Config node ID of secondary node (0 if none)
     * \return Unique event ID string
     */
    static std::string GenerateEventId(SimEventType type,
                                        Time timestamp,
                                        uint32_t primaryNode,
                                        uint32_t secondaryNode = 0);

    /**
     * \brief Convert event type to short code string
     * \param type Event type enum
     * \return Short code string (e.g., "BTMRQ", "ROAMD", "ASSOC")
     */
    static std::string EventTypeToCode(SimEventType type);

    /**
     * \brief Convert event type to full name string
     * \param type Event type enum
     * \return Full name string (e.g., "bss_tm_request_sent", "client_roamed")
     */
    static std::string EventTypeToName(SimEventType type);

    /**
     * \brief Serialize event to JSON string
     * \return JSON representation of the event
     */
    std::string ToJson() const;
};

/**
 * \brief Serialize a batch of events to JSON
 *
 * Creates a JSON object with batch metadata and array of events.
 *
 * \param events Vector of events to serialize
 * \param simulationId Simulation identifier
 * \return JSON string containing batch
 */
std::string SerializeEventBatch(const std::vector<SimulationEvent>& events,
                                 const std::string& simulationId);

/// Global event counter for unique ID generation (thread-safe)
extern std::atomic<uint64_t> g_simEventCounter;

/// Global display time scale factor for event timestamps (1 sim second = N display seconds)
extern double g_eventDisplayTimeScaleFactor;
extern double g_eventDisplayTimeOffsetSeconds;

/**
 * \brief Set the display time scale factor for event timestamps
 * \param factor Scale factor (e.g., 10.0 means 1 sim second = 10 display seconds)
 */
void SetEventDisplayTimeScaleFactor(double factor);
void SetEventDisplayTimeOffsetSeconds(double offset);

} // namespace ns3

#endif /* SIMULATION_EVENT_H */
