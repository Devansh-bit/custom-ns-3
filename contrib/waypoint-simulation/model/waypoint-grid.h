#ifndef WAYPOINT_GRID_H
#define WAYPOINT_GRID_H

#include "ns3/object.h"
#include "ns3/vector.h"
#include "ns3/random-variable-stream.h"

#include <map>
#include <vector>

namespace ns3
{

/**
 * @ingroup waypoint-simulation
 * @brief Manages a grid of waypoints for mobility simulation
 *
 * This class stores waypoint positions and provides utilities for:
 * - Adding waypoints to the grid
 * - Retrieving waypoint positions by ID
 * - Selecting random waypoints
 * - Calculating distances between waypoints
 */
class WaypointGrid : public Object
{
  public:
    /**
     * @brief Get the type ID
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    WaypointGrid();
    ~WaypointGrid() override;

    /**
     * @brief Add a waypoint to the grid
     * @param id Waypoint ID
     * @param position Position of the waypoint
     */
    void AddWaypoint(uint32_t id, Vector position);

    /**
     * @brief Get the position of a waypoint by ID
     * @param id Waypoint ID
     * @return Position vector
     */
    Vector GetWaypointPosition(uint32_t id) const;

    /**
     * @brief Check if a waypoint exists
     * @param id Waypoint ID
     * @return true if waypoint exists
     */
    bool HasWaypoint(uint32_t id) const;

    /**
     * @brief Get the number of waypoints in the grid
     * @return Number of waypoints
     */
    uint32_t GetWaypointCount() const;

    /**
     * @brief Select a random waypoint different from the current one
     * @param currentId Current waypoint ID
     * @return Random waypoint ID
     */
    uint32_t SelectRandomWaypoint(uint32_t currentId) const;

    /**
     * @brief Calculate distance between two waypoints
     * @param id1 First waypoint ID
     * @param id2 Second waypoint ID
     * @return Distance in meters
     */
    double CalculateDistance(uint32_t id1, uint32_t id2) const;

    /**
     * @brief Get all waypoint IDs
     * @return Vector of waypoint IDs
     */
    std::vector<uint32_t> GetAllWaypointIds() const;

    /**
     * @brief Clear all waypoints
     */
    void Clear();

  private:
    std::map<uint32_t, Vector> m_waypoints; //!< Map of waypoint ID to position
    Ptr<UniformRandomVariable> m_random;     //!< Random variable for waypoint selection
};

} // namespace ns3

#endif // WAYPOINT_GRID_H
