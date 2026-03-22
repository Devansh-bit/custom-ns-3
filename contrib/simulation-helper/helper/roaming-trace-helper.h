#ifndef ROAMING_TRACE_HELPER_H
#define ROAMING_TRACE_HELPER_H

#include "ns3/simulation-helper.h"
#include "ns3/mac48-address.h"

namespace ns3
{

/**
 * @ingroup simulation-helper
 * @brief Helper class for connecting trace sources to callbacks
 *
 * This class provides utility functions to:
 * - Connect multiple protocol instances to the same callback
 * - Batch-connect roaming-related traces
 * - Connect association/disassociation traces
 * - Connect traffic/application traces
 *
 * Eliminates repetitive trace connection boilerplate code.
 */
class TraceHelper
{
  public:
    /**
     * @brief Connect link measurement traces for multiple STAs
     *
     * Connects the LinkMeasurementReportReceived trace for all STA protocols
     * in the roaming container to the specified callback.
     *
     * @param roamingContainer Container with STA protocol instances
     * @param callback Callback for link measurement reports
     */
    static void ConnectLinkMeasurementTraces(
        const SimulationHelper::AutoRoamingKvHelperContainer& roamingContainer,
        Callback<void, Mac48Address, LinkMeasurementReport> callback);
};

} // namespace ns3

#endif // ROAMING_TRACE_HELPER_H
