#ifndef STA_CHANNEL_HOPPING_HELPER_H
#define STA_CHANNEL_HOPPING_HELPER_H

#include "ns3/object-factory.h"
#include "ns3/net-device-container.h"
#include "ns3/wifi-net-device.h"
#include "ns3/dual-phy-sniffer-helper.h"
#include "ns3/nstime.h"

#include "../model/sta-channel-hopping-manager.h"

#include <vector>

namespace ns3
{

/**
 * \ingroup sta-channel-hopping
 * \brief Helper class to install StaChannelHoppingManager on STA devices
 *
 * This helper simplifies the installation and configuration of StaChannelHoppingManager
 * on STA devices. It requires a DualPhySnifferHelper to be provided for multi-channel
 * AP tracking.
 */
class StaChannelHoppingHelper
{
  public:
    /**
     * \brief Constructor
     */
    StaChannelHoppingHelper();

    /**
     * \brief Set an attribute on the StaChannelHoppingManager
     * \param name Attribute name
     * \param value Attribute value
     */
    void SetAttribute(std::string name, const AttributeValue& value);

    /**
     * \brief Set the DualPhySniffer to use for beacon cache queries
     * \param sniffer Pointer to DualPhySnifferHelper (not owned)
     *
     * This sniffer should already be installed on the STA node and configured
     * with the desired scanning channels.
     */
    void SetDualPhySniffer(DualPhySnifferHelper* sniffer);

    /**
     * \brief Set the AP device container for connection recovery
     * \param apDevices Pointer to NetDeviceContainer containing all AP devices
     *
     * This allows connection recovery to find available APs directly
     * instead of iterating through NodeList. Must be called before Install().
     */
    void SetApDevices(NetDeviceContainer* apDevices);

    /**
     * \brief Install StaChannelHoppingManager on a single STA device
     * \param staDevice STA WiFi device
     * \return Pointer to installed manager
     */
    Ptr<StaChannelHoppingManager> Install(Ptr<WifiNetDevice> staDevice);

    /**
     * \brief Install StaChannelHoppingManager on multiple STA devices
     * \param staDevices Container of STA WiFi devices
     * \return Vector of pointers to installed managers
     */
    std::vector<Ptr<StaChannelHoppingManager>> Install(NetDeviceContainer staDevices);

  private:
    ObjectFactory m_managerFactory;            ///< Factory for creating managers
    DualPhySnifferHelper* m_dualPhySniffer;    ///< DualPhySniffer for beacon cache (not owned)
    NetDeviceContainer* m_apDevices;           ///< AP devices for connection recovery (not owned)
};

} // namespace ns3

#endif // STA_CHANNEL_HOPPING_HELPER_H
