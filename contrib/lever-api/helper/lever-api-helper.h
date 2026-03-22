#ifndef LEVER_API_HELPER_H
#define LEVER_API_HELPER_H

#include "ns3/lever-api.h"
#include "ns3/application-helper.h"
#include "ns3/application-container.h"
#include "ns3/node-container.h"

namespace ns3
{

/**
 * @ingroup lever-api
 * @brief Helper to install LeverApi application on WiFi nodes
 *
 * This helper makes it easy to install the LeverApi application on nodes
 * and connect them to a shared LeverConfig object for dynamic PHY reconfiguration.
 */
class LeverApiHelper : public ApplicationHelper
{
  public:
    /**
     * @brief Create a LeverApiHelper to install LeverApi applications
     * @param config Pointer to the shared LeverConfig object that all applications will listen to
     */
    LeverApiHelper(Ptr<LeverConfig> config);

  protected:
    /**
     * @brief Install the application on a node and set the config
     * @param node The node to install on
     * @return Pointer to the installed application
     */
    Ptr<Application> DoInstall(Ptr<Node> node) override;

  private:
    Ptr<LeverConfig> m_config; //!< Configuration object to assign to applications
};

} // namespace ns3

#endif // LEVER_API_HELPER_H