/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Virtual Interferer Test Suite
 */

#include "ns3/test.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/node-container.h"
#include "ns3/mobility-helper.h"
#include "ns3/constant-position-mobility-model.h"

#include "ns3/virtual-interferer.h"
#include "ns3/virtual-interferer-environment.h"
#include "ns3/microwave-interferer.h"
#include "ns3/bluetooth-interferer.h"
#include "ns3/zigbee-interferer.h"
#include "ns3/radar-interferer.h"
#include "ns3/virtual-interferer-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("VirtualInterfererTestSuite");

/**
 * \brief Test basic interferer creation and installation
 */
class VirtualInterfererBasicTestCase : public TestCase
{
public:
    VirtualInterfererBasicTestCase();
    virtual ~VirtualInterfererBasicTestCase();

private:
    void DoRun() override;
};

VirtualInterfererBasicTestCase::VirtualInterfererBasicTestCase()
    : TestCase("Test basic interferer creation")
{
}

VirtualInterfererBasicTestCase::~VirtualInterfererBasicTestCase()
{
}

void
VirtualInterfererBasicTestCase::DoRun()
{
    // Test microwave creation
    auto microwave = CreateObject<MicrowaveInterferer>();
    NS_TEST_ASSERT_MSG_NE(microwave, nullptr, "Failed to create MicrowaveInterferer");
    NS_TEST_ASSERT_MSG_EQ(microwave->GetInterfererType(), "Microwave", "Wrong interferer type");

    microwave->SetPosition(Vector(5, 5, 1));
    NS_TEST_ASSERT_MSG_EQ(microwave->GetPosition(), Vector(5, 5, 1), "Position not set correctly");

    // Test power levels - HIGH should be greater than MEDIUM (realistic values are negative dBm)
    microwave->SetPowerLevel(MicrowaveInterferer::MEDIUM);
    double mediumPower = microwave->GetTxPowerDbm();
    microwave->SetPowerLevel(MicrowaveInterferer::HIGH);
    double highPower = microwave->GetTxPowerDbm();
    NS_TEST_ASSERT_MSG_GT(highPower, mediumPower, "HIGH power should be greater than MEDIUM");

    // Test Bluetooth creation
    auto bt = CreateObject<BluetoothInterferer>();
    NS_TEST_ASSERT_MSG_NE(bt, nullptr, "Failed to create BluetoothInterferer");
    NS_TEST_ASSERT_MSG_EQ(bt->GetInterfererType(), "Bluetooth", "Wrong interferer type");

    bt->SetDeviceClass(BluetoothInterferer::CLASS_1);
    NS_TEST_ASSERT_MSG_EQ(bt->GetTxPowerDbm(), 20.0, "CLASS_1 should have 20 dBm TX power");

    // Test ZigBee creation
    auto zb = CreateObject<ZigbeeInterferer>();
    NS_TEST_ASSERT_MSG_NE(zb, nullptr, "Failed to create ZigbeeInterferer");
    NS_TEST_ASSERT_MSG_EQ(zb->GetInterfererType(), "ZigBee", "Wrong interferer type");

    zb->SetZigbeeChannel(15);
    NS_TEST_ASSERT_MSG_EQ(zb->GetZigbeeChannel(), 15, "ZigBee channel not set correctly");

    // Test Radar creation
    auto radar = CreateObject<RadarInterferer>();
    NS_TEST_ASSERT_MSG_NE(radar, nullptr, "Failed to create RadarInterferer");
    NS_TEST_ASSERT_MSG_EQ(radar->GetInterfererType(), "Radar", "Wrong interferer type");

    radar->SetDfsChannel(100);
    NS_TEST_ASSERT_MSG_EQ(radar->GetDfsChannel(), 100, "DFS channel not set correctly");

    // Clean up
    VirtualInterfererEnvironment::Destroy();
    Simulator::Destroy();
}

/**
 * \brief Test interferer installation and environment
 */
class VirtualInterfererEnvironmentTestCase : public TestCase
{
public:
    VirtualInterfererEnvironmentTestCase();
    virtual ~VirtualInterfererEnvironmentTestCase();

private:
    void DoRun() override;
};

VirtualInterfererEnvironmentTestCase::VirtualInterfererEnvironmentTestCase()
    : TestCase("Test interferer environment and installation")
{
}

VirtualInterfererEnvironmentTestCase::~VirtualInterfererEnvironmentTestCase()
{
}

void
VirtualInterfererEnvironmentTestCase::DoRun()
{
    // Get environment singleton
    auto env = VirtualInterfererEnvironment::Get();
    NS_TEST_ASSERT_MSG_NE(env, nullptr, "Failed to get environment singleton");

    // Create and install interferers
    auto microwave = CreateObject<MicrowaveInterferer>();
    microwave->SetPosition(Vector(0, 0, 1));

    NS_TEST_ASSERT_MSG_EQ(microwave->IsInstalled(), false, "Should not be installed yet");
    microwave->Install();
    NS_TEST_ASSERT_MSG_EQ(microwave->IsInstalled(), true, "Should be installed now");

    auto bt = CreateObject<BluetoothInterferer>();
    bt->SetPosition(Vector(5, 5, 1));
    bt->Install();

    // Test getting interferers
    auto allInterferers = env->GetInterferers();
    NS_TEST_ASSERT_MSG_EQ(allInterferers.size(), 2, "Should have 2 interferers");

    // Test uninstall
    microwave->Uninstall();
    NS_TEST_ASSERT_MSG_EQ(microwave->IsInstalled(), false, "Should be uninstalled");

    allInterferers = env->GetInterferers();
    NS_TEST_ASSERT_MSG_EQ(allInterferers.size(), 1, "Should have 1 interferer after uninstall");

    // Clean up
    VirtualInterfererEnvironment::Destroy();
    Simulator::Destroy();
}

/**
 * \brief Test interference effect calculation
 */
class VirtualInterfererEffectTestCase : public TestCase
{
public:
    VirtualInterfererEffectTestCase();
    virtual ~VirtualInterfererEffectTestCase();

private:
    void DoRun() override;
};

VirtualInterfererEffectTestCase::VirtualInterfererEffectTestCase()
    : TestCase("Test interference effect calculation")
{
}

VirtualInterfererEffectTestCase::~VirtualInterfererEffectTestCase()
{
}

void
VirtualInterfererEffectTestCase::DoRun()
{
    // Create microwave interferer
    auto microwave = CreateObject<MicrowaveInterferer>();
    microwave->SetPosition(Vector(0, 0, 1));
    microwave->SetPowerLevel(MicrowaveInterferer::HIGH);
    microwave->Install();

    // Test effect calculation at different distances
    Vector nearPos(1, 0, 1);  // 1 meter away
    Vector farPos(50, 0, 1);  // 50 meters away

    // Calculate actual RX power at each distance
    double nearRxPower = microwave->CalculateRxPower(1.0);   // Close: should be strong
    double farRxPower = microwave->CalculateRxPower(50.0);   // Far: should be weak

    // Microwave affects 2.4 GHz channels (1-14)
    auto nearEffect = microwave->CalculateEffect(nearPos, 6, 1.0, nearRxPower);
    auto farEffect = microwave->CalculateEffect(farPos, 6, 50.0, farRxPower);

    // Near should have more effect
    NS_TEST_ASSERT_MSG_GT(nearEffect.nonWifiCcaPercent, 0, "Should have CCA effect near microwave");
    NS_TEST_ASSERT_MSG_GT(nearEffect.packetLossProbability, 0, "Should have packet loss near microwave");

    // Near effect should be stronger than far
    NS_TEST_ASSERT_MSG_GT(nearEffect.signalPowerDbm, farEffect.signalPowerDbm,
                          "Near signal should be stronger");

    // Test on 5 GHz channel - should have no effect
    auto effect5g = microwave->CalculateEffect(nearPos, 36, 1.0, nearRxPower);
    NS_TEST_ASSERT_MSG_EQ(effect5g.nonWifiCcaPercent, 0, "Microwave should not affect 5 GHz");

    // Clean up
    VirtualInterfererEnvironment::Destroy();
    Simulator::Destroy();
}

/**
 * \brief Test channel overlap calculation
 */
class VirtualInterfererChannelOverlapTestCase : public TestCase
{
public:
    VirtualInterfererChannelOverlapTestCase();
    virtual ~VirtualInterfererChannelOverlapTestCase();

private:
    void DoRun() override;
};

VirtualInterfererChannelOverlapTestCase::VirtualInterfererChannelOverlapTestCase()
    : TestCase("Test channel overlap calculation")
{
}

VirtualInterfererChannelOverlapTestCase::~VirtualInterfererChannelOverlapTestCase()
{
}

void
VirtualInterfererChannelOverlapTestCase::DoRun()
{
    // Create ZigBee on channel 15 (2425 MHz center)
    auto zb = CreateObject<ZigbeeInterferer>();
    zb->SetZigbeeChannel(15);

    // WiFi channel 6 center is 2437 MHz
    // ZigBee 15 center is 2425 MHz, 2 MHz bandwidth
    // Should have some overlap with WiFi channel 5

    // Channel mapping: WiFi ch 1 = 2412, ch 6 = 2437, ch 11 = 2462
    // ZigBee ch 15 = 2425 MHz ± 1 MHz = 2424-2426 MHz
    // WiFi ch 5 = 2432 MHz ± 10 MHz = 2422-2442 MHz
    // WiFi ch 4 = 2427 MHz ± 10 MHz = 2417-2437 MHz

    double overlapCh5 = zb->GetChannelOverlapFactor(5);
    double overlapCh11 = zb->GetChannelOverlapFactor(11);

    NS_TEST_ASSERT_MSG_GT(overlapCh5, 0, "Should overlap with WiFi channel 5");
    NS_TEST_ASSERT_MSG_EQ(overlapCh11, 0, "Should not overlap with WiFi channel 11");

    // Clean up
    VirtualInterfererEnvironment::Destroy();
    Simulator::Destroy();
}

/**
 * \brief Test helper class functionality
 */
class VirtualInterfererHelperTestCase : public TestCase
{
public:
    VirtualInterfererHelperTestCase();
    virtual ~VirtualInterfererHelperTestCase();

private:
    void DoRun() override;
};

VirtualInterfererHelperTestCase::VirtualInterfererHelperTestCase()
    : TestCase("Test helper class")
{
}

VirtualInterfererHelperTestCase::~VirtualInterfererHelperTestCase()
{
}

void
VirtualInterfererHelperTestCase::DoRun()
{
    VirtualInterfererHelper helper;

    // Create interferers using helper
    auto microwave = helper.CreateMicrowave(Vector(5, 5, 1), MicrowaveInterferer::MEDIUM);
    NS_TEST_ASSERT_MSG_NE(microwave, nullptr, "Failed to create microwave via helper");
    NS_TEST_ASSERT_MSG_EQ(microwave->IsInstalled(), true, "Helper should auto-install");

    auto bt = helper.CreateBluetooth(Vector(3, 3, 1));
    NS_TEST_ASSERT_MSG_NE(bt, nullptr, "Failed to create Bluetooth via helper");

    auto zb = helper.CreateZigbee(Vector(7, 7, 1), 20);
    NS_TEST_ASSERT_MSG_NE(zb, nullptr, "Failed to create ZigBee via helper");

    // Check all interferers are tracked
    auto all = helper.GetAllInterferers();
    NS_TEST_ASSERT_MSG_EQ(all.size(), 3, "Should have 3 interferers");

    // Test turn off/on
    helper.TurnOffAll();
    NS_TEST_ASSERT_MSG_EQ(microwave->IsActive(), false, "Should be turned off");

    helper.TurnOnAll();
    NS_TEST_ASSERT_MSG_EQ(microwave->IsActive(), true, "Should be turned on");

    // Clean up
    VirtualInterfererEnvironment::Destroy();
    Simulator::Destroy();
}

/**
 * \brief Test DFS radar triggering
 */
class VirtualInterfererDfsTestCase : public TestCase
{
public:
    VirtualInterfererDfsTestCase();
    virtual ~VirtualInterfererDfsTestCase();

private:
    void DoRun() override;
};

VirtualInterfererDfsTestCase::VirtualInterfererDfsTestCase()
    : TestCase("Test DFS radar triggering")
{
}

VirtualInterfererDfsTestCase::~VirtualInterfererDfsTestCase()
{
}

void
VirtualInterfererDfsTestCase::DoRun()
{
    // Create radar on DFS channel 100
    auto radar = CreateObject<RadarInterferer>();
    radar->SetPosition(Vector(0, 0, 0));
    radar->SetDfsChannel(100);
    radar->SetRadarType(RadarInterferer::WEATHER);
    radar->Initialize();  // Ensure DoInitialize is called
    radar->Install();

    // Test effect on DFS channel - close receiver should trigger DFS
    Vector closePos(10, 0, 0);  // 10 meters away
    auto effect = radar->CalculateEffect(closePos, 100, 10.0, -70.0);

    // High power radar at close range should trigger DFS
    NS_TEST_ASSERT_MSG_EQ(effect.triggersDfs, true, "Close radar should trigger DFS");

    // Far away should not trigger
    Vector farPos(10000, 0, 0);  // 10 km away
    auto farEffect = radar->CalculateEffect(farPos, 100, 10000.0, -70.0);
    NS_TEST_ASSERT_MSG_EQ(farEffect.triggersDfs, false, "Far radar should not trigger DFS");

    // Non-DFS channel should never trigger
    auto nonDfsEffect = radar->CalculateEffect(closePos, 36, 10.0, -70.0);
    NS_TEST_ASSERT_MSG_EQ(nonDfsEffect.triggersDfs, false, "Non-DFS channel should not trigger");

    // Clean up
    VirtualInterfererEnvironment::Destroy();
    Simulator::Destroy();
}

/**
 * \brief Virtual Interferer Test Suite
 */
class VirtualInterfererTestSuite : public TestSuite
{
public:
    VirtualInterfererTestSuite();
};

VirtualInterfererTestSuite::VirtualInterfererTestSuite()
    : TestSuite("virtual-interferer", Type::UNIT)
{
    AddTestCase(new VirtualInterfererBasicTestCase, TestCase::Duration::QUICK);
    AddTestCase(new VirtualInterfererEnvironmentTestCase, TestCase::Duration::QUICK);
    AddTestCase(new VirtualInterfererEffectTestCase, TestCase::Duration::QUICK);
    AddTestCase(new VirtualInterfererChannelOverlapTestCase, TestCase::Duration::QUICK);
    AddTestCase(new VirtualInterfererHelperTestCase, TestCase::Duration::QUICK);
    AddTestCase(new VirtualInterfererDfsTestCase, TestCase::Duration::QUICK);
}

static VirtualInterfererTestSuite sVirtualInterfererTestSuite;
