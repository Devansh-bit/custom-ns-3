#include "lever-api-helper.h"

#include "ns3/lever-api.h"
#include "ns3/log.h"
#include "ns3/names.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("LeverApiHelper");

LeverApiHelper::LeverApiHelper(Ptr<LeverConfig> config)
    : ApplicationHelper("ns3::LeverApi"),
      m_config(config)
{
    NS_LOG_FUNCTION(this << config);
}

Ptr<Application>
LeverApiHelper::DoInstall(Ptr<Node> node)
{
    NS_LOG_FUNCTION(this << node);

    // Create the application using the parent class
    Ptr<Application> app = ApplicationHelper::DoInstall(node);

    // Cast to LeverApi and set the config
    Ptr<LeverApi> leverApp = DynamicCast<LeverApi>(app);
    if (leverApp && m_config)
    {
        leverApp->SetConfig(m_config);
        NS_LOG_INFO("Installed LeverApi on node " << node->GetId() << " with config");
    }
    else
    {
        NS_LOG_WARN("Failed to install LeverApi or set config on node " << node->GetId());
    }

    return app;
}

} // namespace ns3