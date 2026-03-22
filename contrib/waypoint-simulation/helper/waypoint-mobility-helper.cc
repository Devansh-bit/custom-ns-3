#include "waypoint-mobility-helper.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/double.h"

#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("WaypointMobilityHelper");

WaypointMobilityHelper::WaypointMobilityHelper()
{
    NS_LOG_FUNCTION(this);
    m_random = CreateObject<UniformRandomVariable>();
}

WaypointMobilityHelper::~WaypointMobilityHelper()
{
    NS_LOG_FUNCTION(this);
}

void
WaypointMobilityHelper::SetWaypointGrid(Ptr<WaypointGrid> grid)
{
    NS_LOG_FUNCTION(this << grid);
    m_grid = grid;
}

void
WaypointMobilityHelper::Install(Ptr<Node> node, const StaMobilityConfig& config)
{
    NS_LOG_FUNCTION(this << node->GetId());

    NS_ASSERT_MSG(m_grid, "WaypointGrid must be set before installing mobility");
    NS_ASSERT_MSG(m_grid->HasWaypoint(config.initialWaypointId),
                  "Initial waypoint ID " << config.initialWaypointId << " not found in grid");

    // Create and configure WaypointMobilityModel
    Ptr<WaypointMobilityModel> mobility = CreateObject<WaypointMobilityModel>();

    // Set initial position
    Vector initialPos = m_grid->GetWaypointPosition(config.initialWaypointId);
    mobility->SetPosition(initialPos);

    // Aggregate to node
    node->AggregateObject(mobility);

    // Store state
    StaMobilityState state;
    state.node = node;
    state.mobility = mobility;
    state.currentWaypointId = config.initialWaypointId;
    state.config = config;

    m_staStates[node->GetId()] = state;

    NS_LOG_INFO("[WaypointMobilityHelper] Installed waypoint mobility on STA Node "
                << node->GetId() << " at waypoint " << config.initialWaypointId
                << " (position: " << initialPos << ")");
}

void
WaypointMobilityHelper::Install(const NodeContainer& nodes,
                                 const std::vector<StaMobilityConfig>& configs)
{
    NS_LOG_FUNCTION(this << nodes.GetN());

    NS_ASSERT_MSG(nodes.GetN() == configs.size(),
                  "Number of nodes must match number of configs");

    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        Install(nodes.Get(i), configs[i]);
    }

    NS_LOG_INFO("[WaypointMobilityHelper] Installed waypoint mobility on "
                << nodes.GetN() << " STA nodes");
}

void
WaypointMobilityHelper::StartMobility()
{
    NS_LOG_FUNCTION(this);

    for (auto& [nodeId, state] : m_staStates)
    {
        // Schedule first move after random initial delay
        double initialDelay = m_random->GetValue(state.config.waypointSwitchTimeMin,
                                                  state.config.waypointSwitchTimeMax);

        NS_LOG_INFO("[WaypointMobilityHelper] STA Node " << nodeId
                    << " will start moving after " << initialDelay << " seconds");

        Simulator::Schedule(Seconds(initialDelay), &WaypointMobilityHelper::ScheduleNextMove, this, nodeId);
    }

    NS_LOG_INFO("[WaypointMobilityHelper] Started waypoint-based mobility for "
                << m_staStates.size() << " STAs");
}

uint32_t
WaypointMobilityHelper::GetCurrentWaypoint(uint32_t nodeId) const
{
    auto it = m_staStates.find(nodeId);
    if (it != m_staStates.end())
    {
        return it->second.currentWaypointId;
    }

    NS_FATAL_ERROR("STA Node " << nodeId << " not found in mobility helper");
    return 0;
}

uint32_t
WaypointMobilityHelper::GetStaCount() const
{
    return m_staStates.size();
}

void
WaypointMobilityHelper::ScheduleNextMove(uint32_t nodeId)
{
    NS_LOG_FUNCTION(this << nodeId);

    auto& state = m_staStates[nodeId];

    // Select random destination waypoint
    uint32_t destWaypointId = m_grid->SelectRandomWaypoint(state.currentWaypointId);
    Vector destPosition = m_grid->GetWaypointPosition(destWaypointId);

    // Select random velocity
    double velocity = m_random->GetValue(state.config.transferVelocityMin,
                                          state.config.transferVelocityMax);

    // Calculate distance and travel time
    Vector currentPos = state.mobility->GetPosition();
    double dx = destPosition.x - currentPos.x;
    double dy = destPosition.y - currentPos.y;
    double dz = destPosition.z - currentPos.z;
    double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    double travelTime = distance / velocity;

    // Add waypoint to mobility model
    Time arrivalTime = Simulator::Now() + Seconds(travelTime);
    state.mobility->AddWaypoint(ns3::Waypoint(arrivalTime, destPosition));

    NS_LOG_INFO("[WaypointMobilityHelper] STA Node " << nodeId
                << " moving from waypoint " << state.currentWaypointId
                << " to " << destWaypointId
                << " | Distance: " << distance << "m"
                << " | Velocity: " << velocity << " m/s"
                << " | Travel time: " << travelTime << "s"
                << " | Arrival: " << arrivalTime.GetSeconds() << "s");

    // Schedule callback when waypoint is reached
    Simulator::Schedule(Seconds(travelTime),
                        &WaypointMobilityHelper::OnWaypointReached,
                        this,
                        nodeId,
                        destWaypointId);
}

void
WaypointMobilityHelper::OnWaypointReached(uint32_t nodeId, uint32_t reachedWaypointId)
{
    NS_LOG_FUNCTION(this << nodeId << reachedWaypointId);

    auto& state = m_staStates[nodeId];
    state.currentWaypointId = reachedWaypointId;

    NS_LOG_INFO("[WaypointMobilityHelper] STA Node " << nodeId
                << " reached waypoint " << reachedWaypointId
                << " at position " << state.mobility->GetPosition()
                << " at time " << Simulator::Now().GetSeconds() << "s");

    // Schedule next move after random dwell time
    double dwellTime = m_random->GetValue(state.config.waypointSwitchTimeMin,
                                           state.config.waypointSwitchTimeMax);

    NS_LOG_INFO("[WaypointMobilityHelper] STA Node " << nodeId
                << " will stay at waypoint " << reachedWaypointId
                << " for " << dwellTime << " seconds");

    Simulator::Schedule(Seconds(dwellTime), &WaypointMobilityHelper::ScheduleNextMove, this, nodeId);
}

} // namespace ns3
