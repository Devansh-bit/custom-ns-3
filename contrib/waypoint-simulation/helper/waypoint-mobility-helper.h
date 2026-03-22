#ifndef WAYPOINT_MOBILITY_HELPER_H
#define WAYPOINT_MOBILITY_HELPER_H

#include "ns3/node-container.h"
#include "ns3/waypoint-mobility-model.h"
#include "ns3/random-variable-stream.h"
#include "../model/waypoint-grid.h"

#include <map>

namespace ns3
{

/**
 * @ingroup waypoint-simulation
 * @brief Configuration for STA waypoint-based mobility
 */
struct StaMobilityConfig
{
    uint32_t nodeId;                    //!< Node ID
    uint32_t initialWaypointId;         //!< Starting waypoint ID
    double waypointSwitchTimeMin;       //!< Minimum dwell time at waypoint (seconds)
    double waypointSwitchTimeMax;       //!< Maximum dwell time at waypoint (seconds)
    double transferVelocityMin;         //!< Minimum velocity (m/s)
    double transferVelocityMax;         //!< Maximum velocity (m/s)
};

/**
 * @ingroup waypoint-simulation
 * @brief Helper for managing waypoint-based STA mobility
 *
 * This helper:
 * - Installs WaypointMobilityModel on STA nodes
 * - Manages random waypoint selection and movement
 * - Schedules waypoint switches with random timing and velocity
 * - Tracks mobility state for each STA
 */
class WaypointMobilityHelper
{
  public:
    /**
     * @brief Constructor
     */
    WaypointMobilityHelper();

    /**
     * @brief Destructor
     */
    ~WaypointMobilityHelper();

    /**
     * @brief Set the waypoint grid to use
     * @param grid Pointer to WaypointGrid object
     */
    void SetWaypointGrid(Ptr<WaypointGrid> grid);

    /**
     * @brief Install waypoint mobility on a STA node
     * @param node STA node
     * @param config Mobility configuration for this STA
     */
    void Install(Ptr<Node> node, const StaMobilityConfig& config);

    /**
     * @brief Install waypoint mobility on multiple STA nodes
     * @param nodes STA nodes
     * @param configs Mobility configurations (must match node count)
     */
    void Install(const NodeContainer& nodes, const std::vector<StaMobilityConfig>& configs);

    /**
     * @brief Start waypoint-based mobility for all STAs
     *
     * Schedules the first waypoint move for each STA after a random delay.
     * Call this after installing mobility on all nodes.
     */
    void StartMobility();

    /**
     * @brief Get the current waypoint ID of a STA
     * @param nodeId Node ID
     * @return Current waypoint ID
     */
    uint32_t GetCurrentWaypoint(uint32_t nodeId) const;

    /**
     * @brief Get the number of STAs being managed
     * @return Number of STAs
     */
    uint32_t GetStaCount() const;

  private:
    /**
     * @brief Internal state for each STA
     */
    struct StaMobilityState
    {
        Ptr<Node> node;                          //!< Node pointer
        Ptr<WaypointMobilityModel> mobility;     //!< Mobility model
        uint32_t currentWaypointId;              //!< Current waypoint ID
        StaMobilityConfig config;                //!< Mobility configuration
    };

    /**
     * @brief Schedule the next waypoint move for a STA
     * @param nodeId Node ID
     */
    void ScheduleNextMove(uint32_t nodeId);

    /**
     * @brief Callback when a waypoint is reached
     * @param nodeId Node ID
     * @param reachedWaypointId Reached waypoint ID
     */
    void OnWaypointReached(uint32_t nodeId, uint32_t reachedWaypointId);

    Ptr<WaypointGrid> m_grid;                           //!< Waypoint grid
    std::map<uint32_t, StaMobilityState> m_staStates;   //!< STA mobility states
    Ptr<UniformRandomVariable> m_random;                //!< Random variable generator
};

} // namespace ns3

#endif // WAYPOINT_MOBILITY_HELPER_H
