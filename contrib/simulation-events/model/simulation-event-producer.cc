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

#include "simulation-event-producer.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <kafka/Properties.h>
#include <kafka/ProducerRecord.h>
#include <iomanip>
#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("SimulationEventProducer");

NS_OBJECT_ENSURE_REGISTERED(SimulationEventProducer);

TypeId
SimulationEventProducer::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::SimulationEventProducer")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<SimulationEventProducer>()
            .AddAttribute("Brokers",
                          "Kafka broker addresses",
                          StringValue("localhost:9092"),
                          MakeStringAccessor(&SimulationEventProducer::m_brokers),
                          MakeStringChecker())
            .AddAttribute("Topic",
                          "Kafka topic name",
                          StringValue("simulator-events"),
                          MakeStringAccessor(&SimulationEventProducer::m_topic),
                          MakeStringChecker())
            .AddAttribute("SimulationId",
                          "Simulation identifier",
                          StringValue("sim-001"),
                          MakeStringAccessor(&SimulationEventProducer::m_simulationId),
                          MakeStringChecker())
            .AddAttribute("FlushInterval",
                          "Interval between batch flushes",
                          TimeValue(MilliSeconds(100)),
                          MakeTimeAccessor(&SimulationEventProducer::m_flushInterval),
                          MakeTimeChecker());
    return tid;
}

SimulationEventProducer::SimulationEventProducer()
    : m_producer(nullptr),
      m_totalEventsRecorded(0),
      m_batchesSent(0)
{
    NS_LOG_FUNCTION(this);
}

SimulationEventProducer::~SimulationEventProducer()
{
    NS_LOG_FUNCTION(this);
}

void
SimulationEventProducer::SetBrokers(const std::string& brokers)
{
    NS_LOG_FUNCTION(this << brokers);
    m_brokers = brokers;
}

void
SimulationEventProducer::SetTopic(const std::string& topic)
{
    NS_LOG_FUNCTION(this << topic);
    m_topic = topic;
}

void
SimulationEventProducer::SetSimulationId(const std::string& simId)
{
    NS_LOG_FUNCTION(this << simId);
    m_simulationId = simId;
}

void
SimulationEventProducer::SetFlushInterval(Time interval)
{
    NS_LOG_FUNCTION(this << interval);
    m_flushInterval = interval;
}

void
SimulationEventProducer::SetNodeIdMappings(const std::map<uint32_t, uint32_t>& staMapping,
                                            const std::map<uint32_t, uint32_t>& apMapping)
{
    NS_LOG_FUNCTION(this);
    m_staNodeIdMapping = staMapping;
    m_apNodeIdMapping = apMapping;
}

uint32_t
SimulationEventProducer::GetStaConfigNodeId(uint32_t simNodeId) const
{
    auto it = m_staNodeIdMapping.find(simNodeId);
    if (it != m_staNodeIdMapping.end())
    {
        return it->second;
    }
    return simNodeId; // Fallback to simNodeId if not found
}

uint32_t
SimulationEventProducer::GetApConfigNodeId(uint32_t simNodeId) const
{
    auto it = m_apNodeIdMapping.find(simNodeId);
    if (it != m_apNodeIdMapping.end())
    {
        return it->second;
    }
    return simNodeId; // Fallback to simNodeId if not found
}

void
SimulationEventProducer::RecordEvent(const SimulationEvent& event)
{
    NS_LOG_FUNCTION(this << event.eventId);
    std::string typeName = SimulationEvent::EventTypeToName(event.eventType);

    m_eventBuffer.push_back(event);
    m_totalEventsRecorded++;

    // Update event counts
    m_eventCounts[typeName]++;

    NS_LOG_DEBUG("Recorded event: " << event.eventId << " (buffer size: " << m_eventBuffer.size()
                                    << ")");
}

void
SimulationEventProducer::RecordBssTmRequestSent(uint32_t apConfigNodeId,
                                                 uint32_t staConfigNodeId,
                                                 const std::string& reason,
                                                 double rssiDbm,
                                                 const std::string& targetBssid)
{
    SimulationEvent event;
    event.eventType = SimEventType::BSS_TM_REQUEST_SENT;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = apConfigNodeId;
    event.secondaryNodeId = staConfigNodeId;
    event.tertiaryNodeId = 0;
    event.eventId = SimulationEvent::GenerateEventId(event.eventType,
                                                      event.simTimestamp,
                                                      apConfigNodeId,
                                                      staConfigNodeId);

    event.eventData["reason"] = reason;

    std::ostringstream rssiStr;
    rssiStr << std::fixed << std::setprecision(1) << rssiDbm;
    event.eventData["rssi_dbm"] = rssiStr.str();

    event.eventData["target_bssid"] = targetBssid;

    RecordEvent(event);

    NS_LOG_INFO("[Event] BSS_TM_REQUEST_SENT: AP" << apConfigNodeId << " -> STA" << staConfigNodeId
                                                   << " reason=" << reason);
}

void
SimulationEventProducer::RecordClientRoamed(uint32_t staConfigNodeId,
                                            uint32_t sourceApConfigNodeId,
                                            uint32_t targetApConfigNodeId)
{
    SimulationEvent event;
    event.eventType = SimEventType::CLIENT_ROAMED;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = staConfigNodeId;
    event.secondaryNodeId = targetApConfigNodeId;
    event.tertiaryNodeId = sourceApConfigNodeId;
    event.eventId = SimulationEvent::GenerateEventId(event.eventType,
                                                      event.simTimestamp,
                                                      staConfigNodeId,
                                                      targetApConfigNodeId);

    event.eventData["source_ap_id"] = std::to_string(sourceApConfigNodeId);
    event.eventData["target_ap_id"] = std::to_string(targetApConfigNodeId);

    RecordEvent(event);

    NS_LOG_INFO("[Event] CLIENT_ROAMED: STA" << staConfigNodeId << " from AP"
                                              << sourceApConfigNodeId << " to AP"
                                              << targetApConfigNodeId);
}

void
SimulationEventProducer::RecordAssociation(uint32_t staConfigNodeId,
                                           uint32_t apConfigNodeId,
                                           const std::string& bssid)
{
    SimulationEvent event;
    event.eventType = SimEventType::ASSOCIATION;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = staConfigNodeId;
    event.secondaryNodeId = apConfigNodeId;
    event.tertiaryNodeId = 0;
    event.eventId = SimulationEvent::GenerateEventId(event.eventType,
                                                      event.simTimestamp,
                                                      staConfigNodeId,
                                                      apConfigNodeId);

    event.eventData["bssid"] = bssid;

    RecordEvent(event);

    NS_LOG_INFO("[Event] ASSOCIATION: STA" << staConfigNodeId << " with AP" << apConfigNodeId
                                            << " (BSSID: " << bssid << ")");
}

void
SimulationEventProducer::RecordDeassociation(uint32_t staConfigNodeId,
                                             uint32_t apConfigNodeId,
                                             const std::string& bssid)
{
    SimulationEvent event;
    event.eventType = SimEventType::DEASSOCIATION;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = staConfigNodeId;
    event.secondaryNodeId = apConfigNodeId;
    event.tertiaryNodeId = 0;
    event.eventId = SimulationEvent::GenerateEventId(event.eventType,
                                                      event.simTimestamp,
                                                      staConfigNodeId,
                                                      apConfigNodeId);

    event.eventData["bssid"] = bssid;

    RecordEvent(event);

    NS_LOG_INFO("[Event] DEASSOCIATION: STA" << staConfigNodeId << " from AP" << apConfigNodeId
                                              << " (BSSID: " << bssid << ")");
}

void
SimulationEventProducer::RecordConfigReceived(uint32_t nodeConfigId, const std::string& configType)
{
    SimulationEvent event;
    event.eventType = SimEventType::CONFIG_RECEIVED;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = nodeConfigId;
    event.secondaryNodeId = 0;
    event.tertiaryNodeId = 0;
    event.eventId =
        SimulationEvent::GenerateEventId(event.eventType, event.simTimestamp, nodeConfigId, 0);

    event.eventData["config_type"] = configType;

    RecordEvent(event);

    NS_LOG_INFO("[Event] CONFIG_RECEIVED: Node" << nodeConfigId << " type=" << configType);
}

void
SimulationEventProducer::RecordChannelSwitch(uint32_t nodeConfigId,
                                             uint8_t oldChannel,
                                             uint8_t newChannel)
{
    SimulationEvent event;
    event.eventType = SimEventType::CHANNEL_SWITCH;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = nodeConfigId;
    event.secondaryNodeId = 0;
    event.tertiaryNodeId = 0;
    event.eventId =
        SimulationEvent::GenerateEventId(event.eventType, event.simTimestamp, nodeConfigId, 0);

    event.eventData["old_channel"] = std::to_string(oldChannel);
    event.eventData["new_channel"] = std::to_string(newChannel);

    RecordEvent(event);

    NS_LOG_INFO("[Event] CHANNEL_SWITCH: Node" << nodeConfigId << " from ch" << (int)oldChannel
                                                << " to ch" << (int)newChannel);
}

void
SimulationEventProducer::RecordPowerSwitch(uint32_t nodeConfigId,
                                           double oldPowerDbm,
                                           double newPowerDbm,
                                           const std::string& reason)
{
    SimulationEvent event;
    event.eventType = SimEventType::POWER_SWITCH;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = nodeConfigId;
    event.secondaryNodeId = 0;
    event.tertiaryNodeId = 0;
    event.eventId =
        SimulationEvent::GenerateEventId(event.eventType, event.simTimestamp, nodeConfigId, 0);

    std::ostringstream oldPowerStr, newPowerStr;
    oldPowerStr << std::fixed << std::setprecision(1) << oldPowerDbm;
    newPowerStr << std::fixed << std::setprecision(1) << newPowerDbm;

    event.eventData["old_power_dbm"] = oldPowerStr.str();
    event.eventData["new_power_dbm"] = newPowerStr.str();
    event.eventData["reason"] = reason;

    RecordEvent(event);

    NS_LOG_INFO("[Event] POWER_SWITCH: Node" << nodeConfigId << " from " << oldPowerDbm << "dBm to "
                                              << newPowerDbm << "dBm reason=" << reason);
}

void
SimulationEventProducer::RecordPowerSwitch(uint32_t nodeConfigId,
                                           double oldPowerDbm,
                                           double newPowerDbm,
                                           const std::string& reason,
                                           double currentObsspdDbm,
                                           double goalObsspdDbm,
                                           bool inNonWifiMode,
                                           bool goalRecalculated)
{
    SimulationEvent event;
    event.eventType = SimEventType::POWER_SWITCH;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = nodeConfigId;
    event.secondaryNodeId = 0;
    event.tertiaryNodeId = 0;
    event.eventId =
        SimulationEvent::GenerateEventId(event.eventType, event.simTimestamp, nodeConfigId, 0);

    std::ostringstream oldPowerStr, newPowerStr, currentObsspdStr, goalObsspdStr;
    oldPowerStr << std::fixed << std::setprecision(1) << oldPowerDbm;
    newPowerStr << std::fixed << std::setprecision(1) << newPowerDbm;
    currentObsspdStr << std::fixed << std::setprecision(1) << currentObsspdDbm;
    goalObsspdStr << std::fixed << std::setprecision(1) << goalObsspdDbm;

    event.eventData["old_power_dbm"] = oldPowerStr.str();
    event.eventData["new_power_dbm"] = newPowerStr.str();
    event.eventData["reason"] = reason;
    event.eventData["current_obsspd_dbm"] = currentObsspdStr.str();
    event.eventData["goal_obsspd_dbm"] = goalObsspdStr.str();
    event.eventData["in_non_wifi_mode"] = inNonWifiMode ? "true" : "false";
    event.eventData["goal_recalculated"] = goalRecalculated ? "true" : "false";

    RecordEvent(event);

    NS_LOG_INFO("[Event] POWER_SWITCH: Node" << nodeConfigId << " from " << oldPowerDbm << "dBm to "
                                              << newPowerDbm << "dBm reason=" << reason
                                              << " obsspd=" << currentObsspdDbm
                                              << " goal=" << goalObsspdDbm);
}

void
SimulationEventProducer::RecordObssPdGoalChange(uint32_t nodeConfigId,
                                                 double oldGoalDbm,
                                                 double newGoalDbm,
                                                 const std::string& reason)
{
    SimulationEvent event;
    event.eventType = SimEventType::OBSS_PD_GOAL_CHANGE;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = nodeConfigId;
    event.secondaryNodeId = 0;
    event.tertiaryNodeId = 0;
    event.eventId =
        SimulationEvent::GenerateEventId(event.eventType, event.simTimestamp, nodeConfigId, 0);

    std::ostringstream oldGoalStr, newGoalStr, deltaStr;
    oldGoalStr << std::fixed << std::setprecision(1) << oldGoalDbm;
    newGoalStr << std::fixed << std::setprecision(1) << newGoalDbm;
    deltaStr << std::fixed << std::setprecision(1) << (newGoalDbm - oldGoalDbm);

    event.eventData["old_goal_dbm"] = oldGoalStr.str();
    event.eventData["new_goal_dbm"] = newGoalStr.str();
    event.eventData["delta_db"] = deltaStr.str();
    event.eventData["reason"] = reason;

    RecordEvent(event);

    NS_LOG_INFO("[Event] OBSS_PD_GOAL_CHANGE: Node" << nodeConfigId << " from " << oldGoalDbm
                                                     << "dBm to " << newGoalDbm
                                                     << "dBm reason=" << reason);
}

void
SimulationEventProducer::RecordLoadBalanceCheck(uint32_t apConfigNodeId,
                                                 double currentUtilization,
                                                 double threshold,
                                                 uint32_t associatedStas,
                                                 uint32_t offloadedStas)
{
    SimulationEvent event;
    event.eventType = SimEventType::LOAD_BALANCE_CHECK;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = apConfigNodeId;
    event.secondaryNodeId = 0;
    event.tertiaryNodeId = 0;
    event.eventId =
        SimulationEvent::GenerateEventId(event.eventType, event.simTimestamp, apConfigNodeId, 0);

    std::ostringstream utilStr, threshStr;
    utilStr << std::fixed << std::setprecision(1) << (currentUtilization * 100.0);
    threshStr << std::fixed << std::setprecision(1) << (threshold * 100.0);

    event.eventData["current_utilization_percent"] = utilStr.str();
    event.eventData["threshold_percent"] = threshStr.str();
    event.eventData["associated_stas"] = std::to_string(associatedStas);
    event.eventData["offloaded_stas"] = std::to_string(offloadedStas);

    RecordEvent(event);

    NS_LOG_INFO("[Event] LOAD_BALANCE_CHECK: AP" << apConfigNodeId
                << " util=" << (currentUtilization * 100.0) << "%"
                << " threshold=" << (threshold * 100.0) << "%"
                << " stas=" << associatedStas << " offloaded=" << offloadedStas);
}

void
SimulationEventProducer::RecordDfsRadarDetected(uint32_t detectorNodeId,
                                                 uint8_t dfsChannel,
                                                 uint8_t receiverChannel,
                                                 const std::string& affectedChannels)
{
    SimulationEvent event;
    event.eventType = SimEventType::DFS_RADAR_DETECTED;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = detectorNodeId;
    event.secondaryNodeId = 0;
    event.tertiaryNodeId = 0;
    event.eventId =
        SimulationEvent::GenerateEventId(event.eventType, event.simTimestamp, detectorNodeId, 0);

    event.eventData["dfs_channel"] = std::to_string(dfsChannel);
    event.eventData["receiver_channel"] = std::to_string(receiverChannel);
    event.eventData["affected_channels"] = affectedChannels;

    RecordEvent(event);

    NS_LOG_INFO("[Event] DFS_RADAR_DETECTED: Node" << detectorNodeId
                << " detected radar on DFS ch" << (int)dfsChannel
                << " (receiver ch" << (int)receiverChannel << ")"
                << " affected: " << affectedChannels);
}

void
SimulationEventProducer::RecordDfsChannelSwitch(uint32_t apConfigNodeId,
                                                 uint8_t oldChannel,
                                                 uint8_t newChannel,
                                                 const std::string& reason)
{
    SimulationEvent event;
    event.eventType = SimEventType::DFS_CHANNEL_SWITCH;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = apConfigNodeId;
    event.secondaryNodeId = 0;
    event.tertiaryNodeId = 0;
    event.eventId =
        SimulationEvent::GenerateEventId(event.eventType, event.simTimestamp, apConfigNodeId, 0);

    event.eventData["old_channel"] = std::to_string(oldChannel);
    event.eventData["new_channel"] = std::to_string(newChannel);
    event.eventData["reason"] = reason;

    RecordEvent(event);

    NS_LOG_INFO("[Event] DFS_CHANNEL_SWITCH: AP" << apConfigNodeId
                << " from ch" << (int)oldChannel
                << " to ch" << (int)newChannel
                << " reason=" << reason);
}

void
SimulationEventProducer::RecordStressTestForceDfs(double radarDurationSec)
{
    SimulationEvent event;
    event.eventType = SimEventType::STRESS_TEST_FORCE_DFS;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = 0;  // System-level event
    event.secondaryNodeId = 0;
    event.tertiaryNodeId = 0;
    event.eventId =
        SimulationEvent::GenerateEventId(event.eventType, event.simTimestamp, 0, 0);

    std::ostringstream durationStr;
    durationStr << std::fixed << std::setprecision(1) << radarDurationSec;
    event.eventData["radar_duration_sec"] = durationStr.str();
    event.eventData["stress_test_type"] = "FORCE_DFS";

    RecordEvent(event);

    NS_LOG_INFO("[Event] STRESS_TEST_FORCE_DFS: Radar activated for " << radarDurationSec << "s");
}

void
SimulationEventProducer::RecordStressTestHighInterference(uint32_t apsSwitched,
                                                           const std::string& channelList)
{
    SimulationEvent event;
    event.eventType = SimEventType::STRESS_TEST_HIGH_INTERFERENCE;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = 0;  // System-level event
    event.secondaryNodeId = 0;
    event.tertiaryNodeId = 0;
    event.eventId =
        SimulationEvent::GenerateEventId(event.eventType, event.simTimestamp, 0, 0);

    event.eventData["aps_switched"] = std::to_string(apsSwitched);
    event.eventData["target_channels"] = channelList;
    event.eventData["target_band"] = "2.4GHz";
    event.eventData["stress_test_type"] = "HIGH_INTERFERENCE";

    RecordEvent(event);

    NS_LOG_INFO("[Event] STRESS_TEST_HIGH_INTERFERENCE: " << apsSwitched
                << " APs switched to 2.4GHz channels: " << channelList);
}

void
SimulationEventProducer::RecordStressTestHighThroughput(uint32_t stasBoosted,
                                                         double boostFactor,
                                                         double durationSec)
{
    SimulationEvent event;
    event.eventType = SimEventType::STRESS_TEST_HIGH_THROUGHPUT;
    event.simTimestamp = Simulator::Now();
    event.primaryNodeId = 0;  // System-level event
    event.secondaryNodeId = 0;
    event.tertiaryNodeId = 0;
    event.eventId =
        SimulationEvent::GenerateEventId(event.eventType, event.simTimestamp, 0, 0);

    event.eventData["stas_boosted"] = std::to_string(stasBoosted);

    std::ostringstream factorStr, durationStr;
    factorStr << std::fixed << std::setprecision(1) << boostFactor;
    durationStr << std::fixed << std::setprecision(1) << durationSec;

    event.eventData["boost_factor"] = factorStr.str();
    event.eventData["duration_sec"] = durationStr.str();
    event.eventData["stress_test_type"] = "HIGH_THROUGHPUT";

    RecordEvent(event);

    NS_LOG_INFO("[Event] STRESS_TEST_HIGH_THROUGHPUT: " << stasBoosted
                << " STAs boosted by " << boostFactor << "x for " << durationSec << "s");
}

std::map<std::string, uint64_t>
SimulationEventProducer::GetEventCounts() const
{
    return m_eventCounts;
}

uint64_t
SimulationEventProducer::GetTotalEventsRecorded() const
{
    return m_totalEventsRecorded;
}

uint64_t
SimulationEventProducer::GetBatchesSent() const
{
    return m_batchesSent;
}

void
SimulationEventProducer::StartApplication()
{
    NS_LOG_FUNCTION(this);

    try
    {
        // Create Kafka configuration using modern-cpp-kafka Properties
        kafka::Properties props;
        props.put("bootstrap.servers", m_brokers);
        // Background thread handles delivery callbacks automatically
        props.put("enable.manual.events.poll", "false");
        // Set timeout configurations
        props.put("socket.timeout.ms", "30000");
        props.put("message.timeout.ms", "30000");
        props.put("request.timeout.ms", "30000");

        // Create producer instance with background thread for delivery callbacks
        m_producer = std::make_unique<kafka::clients::producer::KafkaProducer>(props);

        NS_LOG_INFO("Created Kafka producer for brokers: " << m_brokers);
        NS_LOG_INFO("Created Kafka topic: " << m_topic);

        // Schedule first flush
        ScheduleNextFlush();

        NS_LOG_INFO("SimulationEventProducer started with flush interval: "
                    << m_flushInterval.GetMilliSeconds() << "ms");
    }
    catch (const kafka::KafkaException& e)
    {
        NS_LOG_ERROR("Failed to create Kafka producer: " << e.what());
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception in StartApplication: " << e.what());
    }
    catch (...)
    {
        NS_LOG_ERROR("Unknown exception in StartApplication");
    }
}

void
SimulationEventProducer::StopApplication()
{
    NS_LOG_FUNCTION(this);

    // Cancel pending flush
    Simulator::Cancel(m_flushEvent);

    // Flush remaining events
    if (!m_eventBuffer.empty())
    {
        FlushEvents();
    }

    // Cleanup - modern-cpp-kafka handles flushing via RAII
    if (m_producer)
    {
        m_producer.reset();
    }

    NS_LOG_INFO("SimulationEventProducer stopped. Total events: "
                << m_totalEventsRecorded << ", Batches sent: " << m_batchesSent);
}

void
SimulationEventProducer::FlushEvents()
{
    NS_LOG_FUNCTION(this);

    if (m_eventBuffer.empty())
    {
        NS_LOG_DEBUG("No events to flush");
        return;
    }

    if (!m_producer)
    {
        NS_LOG_WARN("Cannot flush events - Kafka not initialized");
        return;
    }

    // Serialize batch
    std::string jsonPayload = SerializeEventBatch(m_eventBuffer, m_simulationId);

    // Generate batch key (simulation ID + batch number)
    std::ostringstream keyStream;
    keyStream << m_simulationId << "-batch-" << m_batchesSent;
    std::string key = keyStream.str();

    try
    {
        // Create producer record with topic, key, and value
        kafka::clients::producer::ProducerRecord record(
            m_topic,
            kafka::Key(key.c_str(), key.size()),
            kafka::Value(jsonPayload.c_str(), jsonPayload.size())
        );

        // Async send with delivery callback - non-blocking
        m_producer->send(record,
            [this](const kafka::clients::producer::RecordMetadata& metadata,
                   const kafka::Error& error) {
                if (error)
                {
                    NS_LOG_ERROR("Failed to deliver event batch: " << error.message());
                }
            },
            kafka::clients::producer::KafkaProducer::SendOption::ToCopyRecordValue
        );

        NS_LOG_DEBUG("Flushed " << m_eventBuffer.size() << " events to Kafka (batch "
                                << m_batchesSent << ")");
        m_batchesSent++;
    }
    catch (const kafka::KafkaException& e)
    {
        NS_LOG_ERROR("Failed to produce event batch: " << e.what());
    }

    // Clear buffer
    m_eventBuffer.clear();
}

void
SimulationEventProducer::ScheduleNextFlush()
{
    NS_LOG_FUNCTION(this);
    m_flushEvent = Simulator::Schedule(m_flushInterval, &SimulationEventProducer::FlushEvents, this);
    Simulator::Schedule(m_flushInterval, &SimulationEventProducer::ScheduleNextFlush, this);
}

void
SimulationEventProducer::DoDispose()
{
    NS_LOG_FUNCTION(this);
    StopApplication();
    Application::DoDispose();
}

} // namespace ns3
