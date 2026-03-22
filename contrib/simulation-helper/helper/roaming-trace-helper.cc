#include "roaming-trace-helper.h"

#include "ns3/config.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoamingTraceHelper");

void
TraceHelper::ConnectLinkMeasurementTraces(
    const SimulationHelper::AutoRoamingKvHelperContainer& roamingContainer,
    Callback<void, Mac48Address, LinkMeasurementReport> callback)
{
    NS_LOG_FUNCTION_NOARGS();

    for (size_t i = 0; i < roamingContainer.staProtocols.size(); i++)
    {
        // Each STA has one link measurement protocol at index 0
        if (!roamingContainer.staProtocols[i].empty())
        {
            roamingContainer.staProtocols[i][0]->TraceConnectWithoutContext(
                "LinkMeasurementReportReceived",
                callback);

            NS_LOG_INFO("[TraceHelper] Connected LinkMeasurementReportReceived for STA " << i);
        }
    }

    NS_LOG_INFO("[TraceHelper] Connected link measurement traces for "
                << roamingContainer.staProtocols.size() << " STAs");
}

} // namespace ns3
