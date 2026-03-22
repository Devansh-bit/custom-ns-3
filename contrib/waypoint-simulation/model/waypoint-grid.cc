#include "waypoint-grid.h"

#include "ns3/log.h"

#include <cmath>
#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("WaypointGrid");

NS_OBJECT_ENSURE_REGISTERED(WaypointGrid);

TypeId
WaypointGrid::GetTypeId()
{
    static TypeId tid = TypeId("ns3::WaypointGrid")
                            .SetParent<Object>()
                            .SetGroupName("WaypointSimulation")
                            .AddConstructor<WaypointGrid>();
    return tid;
}

WaypointGrid::WaypointGrid()
{
    NS_LOG_FUNCTION(this);
    m_random = CreateObject<UniformRandomVariable>();
}

WaypointGrid::~WaypointGrid()
{
    NS_LOG_FUNCTION(this);
}

void
WaypointGrid::AddWaypoint(uint32_t id, Vector position)
{
    NS_LOG_FUNCTION(this << id << position);
    m_waypoints[id] = position;
    NS_LOG_DEBUG("Added waypoint " << id << " at position " << position);
}

Vector
WaypointGrid::GetWaypointPosition(uint32_t id) const
{
    NS_LOG_FUNCTION(this << id);

    auto it = m_waypoints.find(id);
    if (it != m_waypoints.end())
    {
        return it->second;
    }

    NS_FATAL_ERROR("Waypoint ID " << id << " not found in grid");
    return Vector(0, 0, 0);
}

bool
WaypointGrid::HasWaypoint(uint32_t id) const
{
    return m_waypoints.find(id) != m_waypoints.end();
}

uint32_t
WaypointGrid::GetWaypointCount() const
{
    return m_waypoints.size();
}

uint32_t
WaypointGrid::SelectRandomWaypoint(uint32_t currentId) const
{
    NS_LOG_FUNCTION(this << currentId);

    if (m_waypoints.size() <= 1)
    {
        return currentId;
    }

    uint32_t newId;
    do
    {
        uint32_t randomIndex = m_random->GetInteger(0, m_waypoints.size() - 1);
        auto it = m_waypoints.begin();
        std::advance(it, randomIndex);
        newId = it->first;
    } while (newId == currentId);

    NS_LOG_DEBUG("Selected random waypoint " << newId << " (current: " << currentId << ")");
    return newId;
}

double
WaypointGrid::CalculateDistance(uint32_t id1, uint32_t id2) const
{
    NS_LOG_FUNCTION(this << id1 << id2);

    Vector pos1 = GetWaypointPosition(id1);
    Vector pos2 = GetWaypointPosition(id2);

    double dx = pos2.x - pos1.x;
    double dy = pos2.y - pos1.y;
    double dz = pos2.z - pos1.z;

    double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    NS_LOG_DEBUG("Distance from waypoint " << id1 << " to " << id2 << ": " << distance << "m");

    return distance;
}

std::vector<uint32_t>
WaypointGrid::GetAllWaypointIds() const
{
    std::vector<uint32_t> ids;
    ids.reserve(m_waypoints.size());

    for (const auto& [id, position] : m_waypoints)
    {
        ids.push_back(id);
    }

    return ids;
}

void
WaypointGrid::Clear()
{
    NS_LOG_FUNCTION(this);
    m_waypoints.clear();
}

} // namespace ns3
