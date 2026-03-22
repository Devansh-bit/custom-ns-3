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

#include "simulation-event.h"

#include "ns3/simulator.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace ns3
{

// Global event counter for unique ID generation
std::atomic<uint64_t> g_simEventCounter{0};

// Global display time scale factor for event timestamps (default 1.0 = no scaling)
double g_eventDisplayTimeScaleFactor = 1.0;
double g_eventDisplayTimeOffsetSeconds = 0.0;

void SetEventDisplayTimeScaleFactor(double factor)
{
    g_eventDisplayTimeScaleFactor = factor;
}

void SetEventDisplayTimeOffsetSeconds(double offset)
{
    g_eventDisplayTimeOffsetSeconds = offset;
}

std::string
SimulationEvent::EventTypeToCode(SimEventType type)
{
    switch (type)
    {
    case SimEventType::BSS_TM_REQUEST_SENT:
        return "BTMRQ";
    case SimEventType::CLIENT_ROAMED:
        return "ROAMD";
    case SimEventType::ASSOCIATION:
        return "ASSOC";
    case SimEventType::DEASSOCIATION:
        return "DEASS";
    case SimEventType::CONFIG_RECEIVED:
        return "CFGRC";
    case SimEventType::CHANNEL_SWITCH:
        return "CHSWT";
    case SimEventType::POWER_SWITCH:
        return "PWSWT";
    case SimEventType::LOAD_BALANCE_CHECK:
        return "LDBCK";
    case SimEventType::DFS_RADAR_DETECTED:
        return "DFSRD";
    case SimEventType::DFS_CHANNEL_SWITCH:
        return "DFSSW";
    case SimEventType::OBSS_PD_GOAL_CHANGE:
        return "OBSSG";
    case SimEventType::STRESS_TEST_FORCE_DFS:
        return "STDFS";
    case SimEventType::STRESS_TEST_HIGH_INTERFERENCE:
        return "STINF";
    case SimEventType::STRESS_TEST_HIGH_THROUGHPUT:
        return "STTHR";
    default:
        return "UNKNW";
    }
}

std::string
SimulationEvent::EventTypeToName(SimEventType type)
{
    switch (type)
    {
    case SimEventType::BSS_TM_REQUEST_SENT:
        return "bss_tm_request_sent";
    case SimEventType::CLIENT_ROAMED:
        return "client_roamed";
    case SimEventType::ASSOCIATION:
        return "association";
    case SimEventType::DEASSOCIATION:
        return "deassociation";
    case SimEventType::CONFIG_RECEIVED:
        return "config_received";
    case SimEventType::CHANNEL_SWITCH:
        return "channel_switch";
    case SimEventType::POWER_SWITCH:
        return "power_switch";
    case SimEventType::LOAD_BALANCE_CHECK:
        return "load_balance_check";
    case SimEventType::DFS_RADAR_DETECTED:
        return "dfs_radar_detected";
    case SimEventType::DFS_CHANNEL_SWITCH:
        return "dfs_channel_switch";
    case SimEventType::OBSS_PD_GOAL_CHANGE:
        return "obss_pd_goal_change";
    case SimEventType::STRESS_TEST_FORCE_DFS:
        return "stress_test_force_dfs";
    case SimEventType::STRESS_TEST_HIGH_INTERFERENCE:
        return "stress_test_high_interference";
    case SimEventType::STRESS_TEST_HIGH_THROUGHPUT:
        return "stress_test_high_throughput";
    default:
        return "unknown";
    }
}

std::string
SimulationEvent::GenerateEventId(SimEventType type,
                                  Time timestamp,
                                  uint32_t primaryNode,
                                  uint32_t secondaryNode)
{
    std::ostringstream oss;

    // Event type code
    oss << EventTypeToCode(type);

    // Timestamp in milliseconds (zero-padded to 11 digits for up to ~115 days)
    // Apply display time scale factor and offset
    uint64_t scaledMs = static_cast<uint64_t>(timestamp.GetMilliSeconds() * g_eventDisplayTimeScaleFactor + g_eventDisplayTimeOffsetSeconds * 1000.0);
    oss << "-" << std::setfill('0') << std::setw(11) << scaledMs;

    // Primary node ID (zero-padded to 3 digits)
    oss << "-N" << std::setfill('0') << std::setw(3) << primaryNode;

    // Secondary node ID (zero-padded to 3 digits)
    oss << "-N" << std::setfill('0') << std::setw(3) << secondaryNode;

    // Sequence counter (zero-padded to 5 digits, supports 100k events per millisecond)
    uint64_t seq = g_simEventCounter++;
    oss << "-" << std::setfill('0') << std::setw(5) << (seq % 100000);

    return oss.str();
}

std::string
SimulationEvent::ToJson() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);

    oss << "{";
    oss << "\"event_id\":\"" << eventId << "\",";
    oss << "\"event_type\":\"" << EventTypeToName(eventType) << "\",";
    oss << "\"sim_timestamp_sec\":" << (simTimestamp.GetSeconds() * g_eventDisplayTimeScaleFactor + g_eventDisplayTimeOffsetSeconds) << ",";
    oss << "\"primary_node_id\":" << primaryNodeId << ",";
    oss << "\"secondary_node_id\":" << secondaryNodeId;

    // Only include tertiary node if it's set (non-zero)
    if (tertiaryNodeId > 0)
    {
        oss << ",\"tertiary_node_id\":" << tertiaryNodeId;
    }

    // Event-specific data
    if (!eventData.empty())
    {
        oss << ",\"data\":{";
        bool first = true;
        for (const auto& [key, value] : eventData)
        {
            if (!first)
            {
                oss << ",";
            }
            first = false;

            // Escape quotes in value and output
            oss << "\"" << key << "\":\"";
            for (char c : value)
            {
                if (c == '"')
                {
                    oss << "\\\"";
                }
                else if (c == '\\')
                {
                    oss << "\\\\";
                }
                else
                {
                    oss << c;
                }
            }
            oss << "\"";
        }
        oss << "}";
    }

    oss << "}";
    return oss.str();
}

std::string
SerializeEventBatch(const std::vector<SimulationEvent>& events, const std::string& simulationId)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);

    // Get current unix timestamp
    auto now = std::chrono::system_clock::now();
    auto epochSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    // Get current simulation time (apply display time scale factor and offset)
    double simTimeSec = Simulator::Now().GetSeconds() * g_eventDisplayTimeScaleFactor + g_eventDisplayTimeOffsetSeconds;

    oss << "{";
    oss << "\"simulation_id\":\"" << simulationId << "\",";
    oss << "\"batch_timestamp_unix\":" << epochSeconds << ",";
    oss << "\"batch_sim_time_sec\":" << simTimeSec << ",";
    oss << "\"event_count\":" << events.size() << ",";
    oss << "\"events\":[";

    bool first = true;
    for (const auto& event : events)
    {
        if (!first)
        {
            oss << ",";
        }
        first = false;
        oss << event.ToJson();
    }

    oss << "]}";
    return oss.str();
}

} // namespace ns3
