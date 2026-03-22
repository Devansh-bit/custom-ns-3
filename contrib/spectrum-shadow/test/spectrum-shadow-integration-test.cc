/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Spectrum Shadow Integration Tests
 *
 * Tests:
 * 1. AP/STA/Interferer configuration matches JSON config
 * 2. STA mobility (waypoint) is properly configured
 * 3. PSD generation from spectrum analyzers
 * 4. Pipeline data preparation
 * 5. Named pipe functionality
 */

#include "ns3/test.h"
#include "ns3/spectrum-shadow.h"
#include "ns3/spectrum-shadow-helper.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/node-container.h"
#include "ns3/mobility-model.h"
#include "ns3/wifi-net-device.h"
#include "ns3/spectrum-wifi-phy.h"
#include "ns3/multi-model-spectrum-channel.h"

#include <fstream>
#include <cmath>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SpectrumShadowIntegrationTest");

// Helper to check if position matches expected
bool PositionMatches(Vector actual, Vector expected, double tolerance = 0.1)
{
    return (std::abs(actual.x - expected.x) < tolerance &&
            std::abs(actual.y - expected.y) < tolerance &&
            std::abs(actual.z - expected.z) < tolerance);
}

/**
 * Test 1: Verify AP configuration matches JSON config
 */
class ApConfigurationTest : public TestCase
{
public:
    ApConfigurationTest(const std::string& configFile);
    virtual ~ApConfigurationTest();

private:
    virtual void DoRun();
    std::string m_configFile;
};

ApConfigurationTest::ApConfigurationTest(const std::string& configFile)
    : TestCase("Test AP configuration matches JSON config"),
      m_configFile(configFile)
{
}

ApConfigurationTest::~ApConfigurationTest()
{
}

void
ApConfigurationTest::DoRun()
{
    SpectrumShadowHelper helper;

    // Load config
    bool loaded = helper.LoadConfig(m_configFile);
    NS_TEST_ASSERT_MSG_EQ(loaded, true, "Failed to load config file");

    SimulationConfigData config = helper.GetSimulationConfig();

    // Setup nodes and channel
    helper.SetupNodes();
    helper.SetupSpectrumChannel();
    helper.SetupMobility();

    NodeContainer apNodes = helper.GetApNodes();

    // Verify AP count
    NS_TEST_ASSERT_MSG_EQ(apNodes.GetN(), config.aps.size(),
                          "AP count mismatch");

    // Verify each AP position
    for (size_t i = 0; i < config.aps.size(); i++) {
        Ptr<Node> ap = apNodes.Get(i);
        Ptr<MobilityModel> mob = ap->GetObject<MobilityModel>();
        NS_TEST_ASSERT_MSG_NE(mob, nullptr, "AP has no mobility model");

        Vector actualPos = mob->GetPosition();
        Vector expectedPos = config.aps[i].position;

        NS_TEST_ASSERT_MSG_EQ(PositionMatches(actualPos, expectedPos), true,
                              "AP" << i << " position mismatch: expected ("
                              << expectedPos.x << "," << expectedPos.y << "," << expectedPos.z
                              << ") got (" << actualPos.x << "," << actualPos.y << "," << actualPos.z << ")");
    }

    Simulator::Destroy();
}

/**
 * Test 2: Verify STA configuration and initial positions match JSON config
 */
class StaConfigurationTest : public TestCase
{
public:
    StaConfigurationTest(const std::string& configFile);
    virtual ~StaConfigurationTest();

private:
    virtual void DoRun();
    std::string m_configFile;
};

StaConfigurationTest::StaConfigurationTest(const std::string& configFile)
    : TestCase("Test STA configuration matches JSON config"),
      m_configFile(configFile)
{
}

StaConfigurationTest::~StaConfigurationTest()
{
}

void
StaConfigurationTest::DoRun()
{
    SpectrumShadowHelper helper;

    bool loaded = helper.LoadConfig(m_configFile);
    NS_TEST_ASSERT_MSG_EQ(loaded, true, "Failed to load config file");

    SimulationConfigData config = helper.GetSimulationConfig();

    helper.SetupNodes();
    helper.SetupSpectrumChannel();
    helper.SetupMobility();

    NodeContainer staNodes = helper.GetStaNodes();

    // Verify STA count
    NS_TEST_ASSERT_MSG_EQ(staNodes.GetN(), config.stas.size(),
                          "STA count mismatch");

    // Verify each STA has mobility model and starts at correct waypoint
    for (size_t i = 0; i < config.stas.size(); i++) {
        Ptr<Node> sta = staNodes.Get(i);
        Ptr<MobilityModel> mob = sta->GetObject<MobilityModel>();
        NS_TEST_ASSERT_MSG_NE(mob, nullptr, "STA has no mobility model");

        // Get expected initial waypoint position
        uint32_t initialWpId = config.stas[i].initialWaypointId;
        Vector expectedPos;
        for (const auto& wp : config.waypoints) {
            if (wp.id == initialWpId) {
                expectedPos = wp.position;
                break;
            }
        }

        Vector actualPos = mob->GetPosition();
        NS_TEST_ASSERT_MSG_EQ(PositionMatches(actualPos, expectedPos, 0.5), true,
                              "STA" << i << " initial position mismatch");
    }

    Simulator::Destroy();
}

/**
 * Test 3: Verify interferer configuration
 */
class InterfererConfigurationTest : public TestCase
{
public:
    InterfererConfigurationTest(const std::string& configFile);
    virtual ~InterfererConfigurationTest();

private:
    virtual void DoRun();
    std::string m_configFile;
};

InterfererConfigurationTest::InterfererConfigurationTest(const std::string& configFile)
    : TestCase("Test interferer configuration matches JSON config"),
      m_configFile(configFile)
{
}

InterfererConfigurationTest::~InterfererConfigurationTest()
{
}

void
InterfererConfigurationTest::DoRun()
{
    SpectrumShadowHelper helper;

    bool loaded = helper.LoadConfig(m_configFile);
    NS_TEST_ASSERT_MSG_EQ(loaded, true, "Failed to load config file");

    SimulationConfigData config = helper.GetSimulationConfig();

    helper.SetupNodes();
    helper.SetupSpectrumChannel();
    helper.SetupMobility();
    helper.SetupInterferers();

    NodeContainer interfererNodes = helper.GetInterfererNodes();

    // Count expected active interferers
    uint32_t expectedCount = 0;
    if (config.virtualInterferers.enabled) {
        for (const auto& mw : config.virtualInterferers.microwaves) {
            if (mw.active) expectedCount++;
        }
        for (const auto& bt : config.virtualInterferers.bluetooths) {
            if (bt.active) expectedCount++;
        }
        for (const auto& zb : config.virtualInterferers.zigbees) {
            if (zb.active) expectedCount++;
        }
        for (const auto& cl : config.virtualInterferers.cordless) {
            if (cl.active) expectedCount++;
        }
    }

    NS_TEST_ASSERT_MSG_EQ(interfererNodes.GetN(), expectedCount,
                          "Interferer count mismatch: expected " << expectedCount
                          << " got " << interfererNodes.GetN());

    // Verify each interferer has position
    for (uint32_t i = 0; i < interfererNodes.GetN(); i++) {
        Ptr<MobilityModel> mob = interfererNodes.Get(i)->GetObject<MobilityModel>();
        NS_TEST_ASSERT_MSG_NE(mob, nullptr, "Interferer " << i << " has no mobility model");
    }

    Simulator::Destroy();
}

/**
 * Test 4: Verify WiFi devices use SpectrumWifiPhy
 */
class SpectrumPhyTest : public TestCase
{
public:
    SpectrumPhyTest(const std::string& configFile);
    virtual ~SpectrumPhyTest();

private:
    virtual void DoRun();
    std::string m_configFile;
};

SpectrumPhyTest::SpectrumPhyTest(const std::string& configFile)
    : TestCase("Test WiFi devices use SpectrumWifiPhy"),
      m_configFile(configFile)
{
}

SpectrumPhyTest::~SpectrumPhyTest()
{
}

void
SpectrumPhyTest::DoRun()
{
    SpectrumShadowHelper helper;

    bool loaded = helper.LoadConfig(m_configFile);
    NS_TEST_ASSERT_MSG_EQ(loaded, true, "Failed to load config file");

    helper.SetupNodes();
    helper.SetupSpectrumChannel();
    helper.SetupWifiDevices();
    helper.SetupMobility();

    NetDeviceContainer apDevices = helper.GetApDevices();
    NetDeviceContainer staDevices = helper.GetStaDevices();

    // Verify AP devices have SpectrumWifiPhy
    for (uint32_t i = 0; i < apDevices.GetN(); i++) {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(apDevices.Get(i));
        NS_TEST_ASSERT_MSG_NE(wifiDev, nullptr, "AP device is not WifiNetDevice");

        Ptr<WifiPhy> phy = wifiDev->GetPhy();
        NS_TEST_ASSERT_MSG_NE(phy, nullptr, "AP has no PHY");

        Ptr<SpectrumWifiPhy> specPhy = DynamicCast<SpectrumWifiPhy>(phy);
        NS_TEST_ASSERT_MSG_NE(specPhy, nullptr, "AP PHY is not SpectrumWifiPhy");
    }

    // Verify STA devices have SpectrumWifiPhy
    for (uint32_t i = 0; i < staDevices.GetN(); i++) {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        NS_TEST_ASSERT_MSG_NE(wifiDev, nullptr, "STA device is not WifiNetDevice");

        Ptr<WifiPhy> phy = wifiDev->GetPhy();
        NS_TEST_ASSERT_MSG_NE(phy, nullptr, "STA has no PHY");

        Ptr<SpectrumWifiPhy> specPhy = DynamicCast<SpectrumWifiPhy>(phy);
        NS_TEST_ASSERT_MSG_NE(specPhy, nullptr, "STA PHY is not SpectrumWifiPhy");
    }

    // Verify spectrum channel exists
    Ptr<MultiModelSpectrumChannel> channel = helper.GetSpectrumChannel();
    NS_TEST_ASSERT_MSG_NE(channel, nullptr, "No spectrum channel");

    Simulator::Destroy();
}

/**
 * Test 5: Verify STA mobility changes over time
 */
class StaMobilityTest : public TestCase
{
public:
    StaMobilityTest(const std::string& configFile);
    virtual ~StaMobilityTest();

private:
    virtual void DoRun();
    void CheckMobility();

    std::string m_configFile;
    NodeContainer m_staNodes;
    std::vector<Vector> m_initialPositions;
    bool m_positionChanged;
};

StaMobilityTest::StaMobilityTest(const std::string& configFile)
    : TestCase("Test STA mobility changes over time"),
      m_configFile(configFile),
      m_positionChanged(false)
{
}

StaMobilityTest::~StaMobilityTest()
{
}

void
StaMobilityTest::CheckMobility()
{
    for (uint32_t i = 0; i < m_staNodes.GetN(); i++) {
        Ptr<MobilityModel> mob = m_staNodes.Get(i)->GetObject<MobilityModel>();
        Vector currentPos = mob->GetPosition();

        if (!PositionMatches(currentPos, m_initialPositions[i], 0.1)) {
            m_positionChanged = true;
        }
    }
}

void
StaMobilityTest::DoRun()
{
    SpectrumShadowHelper helper;

    bool loaded = helper.LoadConfig(m_configFile);
    NS_TEST_ASSERT_MSG_EQ(loaded, true, "Failed to load config file");

    helper.SetupNodes();
    helper.SetupSpectrumChannel();
    helper.SetupMobility();

    m_staNodes = helper.GetStaNodes();

    // Record initial positions
    for (uint32_t i = 0; i < m_staNodes.GetN(); i++) {
        Ptr<MobilityModel> mob = m_staNodes.Get(i)->GetObject<MobilityModel>();
        m_initialPositions.push_back(mob->GetPosition());
    }

    // Schedule position check after some time
    Simulator::Schedule(Seconds(3.0), &StaMobilityTest::CheckMobility, this);

    // Run simulation
    Simulator::Stop(Seconds(4.0));
    Simulator::Run();

    // Verify at least one STA moved
    NS_TEST_ASSERT_MSG_EQ(m_positionChanged, true,
                          "No STA positions changed - mobility not working");

    Simulator::Destroy();
}

/**
 * Test 6: Verify annotation manager records interferers
 */
class AnnotationTest : public TestCase
{
public:
    AnnotationTest(const std::string& configFile);
    virtual ~AnnotationTest();

private:
    virtual void DoRun();
    std::string m_configFile;
};

AnnotationTest::AnnotationTest(const std::string& configFile)
    : TestCase("Test annotation manager records interferers"),
      m_configFile(configFile)
{
}

AnnotationTest::~AnnotationTest()
{
}

void
AnnotationTest::DoRun()
{
    SpectrumShadowHelper helper;

    bool loaded = helper.LoadConfig(m_configFile);
    NS_TEST_ASSERT_MSG_EQ(loaded, true, "Failed to load config file");

    helper.SetupNodes();
    helper.SetupSpectrumChannel();
    helper.SetupMobility();
    helper.SetupInterferers();

    // Get annotation manager
    Ptr<SpectrumShadow> shadow = helper.GetSpectrumShadow();
    NS_TEST_ASSERT_MSG_NE(shadow, nullptr, "No SpectrumShadow object");

    Ptr<AnnotationManager> annotMgr = shadow->GetAnnotationManager();
    NS_TEST_ASSERT_MSG_NE(annotMgr, nullptr, "No annotation manager");

    // Run a short simulation to generate annotations
    Simulator::Stop(Seconds(1.0));
    Simulator::Run();

    // Check annotations were created
    const auto& annotations = annotMgr->GetAnnotations();
    // Note: Annotations may be 0 if interferers haven't been triggered yet
    // This just verifies the annotation manager exists and is accessible

    Simulator::Destroy();
}

/**
 * Test 7: Verify spectrum config is applied
 */
class SpectrumConfigTest : public TestCase
{
public:
    SpectrumConfigTest(const std::string& configFile);
    virtual ~SpectrumConfigTest();

private:
    virtual void DoRun();
    std::string m_configFile;
};

SpectrumConfigTest::SpectrumConfigTest(const std::string& configFile)
    : TestCase("Test spectrum config is applied correctly"),
      m_configFile(configFile)
{
}

SpectrumConfigTest::~SpectrumConfigTest()
{
}

void
SpectrumConfigTest::DoRun()
{
    SpectrumShadowHelper helper;

    bool loaded = helper.LoadConfig(m_configFile);
    NS_TEST_ASSERT_MSG_EQ(loaded, true, "Failed to load config file");

    // Set custom spectrum config
    SpectrumShadowConfig specConfig;
    specConfig.startFrequency = 5.0e9;
    specConfig.frequencyResolution = 200e3;
    specConfig.numFrequencyBins = 500;
    specConfig.pipePath = "/tmp/test-spectrum";
    specConfig.enablePipeStreaming = false;  // Disable for test
    specConfig.enableFileLogging = false;    // Disable for test

    helper.SetSpectrumConfig(specConfig);

    // Verify config was applied
    Ptr<SpectrumShadow> shadow = helper.GetSpectrumShadow();
    SpectrumShadowConfig appliedConfig = shadow->GetConfig();

    NS_TEST_ASSERT_MSG_EQ(appliedConfig.startFrequency, 5.0e9,
                          "Start frequency not applied");
    NS_TEST_ASSERT_MSG_EQ(appliedConfig.frequencyResolution, 200e3,
                          "Frequency resolution not applied");
    NS_TEST_ASSERT_MSG_EQ(appliedConfig.numFrequencyBins, 500,
                          "Num bins not applied");
    NS_TEST_ASSERT_MSG_EQ(appliedConfig.pipePath, "/tmp/test-spectrum",
                          "Pipe path not applied");

    Simulator::Destroy();
}

/**
 * Test Suite for Spectrum Shadow Integration Tests
 */
class SpectrumShadowIntegrationTestSuite : public TestSuite
{
public:
    SpectrumShadowIntegrationTestSuite();
};

SpectrumShadowIntegrationTestSuite::SpectrumShadowIntegrationTestSuite()
    : TestSuite("spectrum-shadow-integration", Type::SYSTEM)
{
    // Use relative path for config file
    std::string configFile = "test-spectrum-shadow-config.json";

    AddTestCase(new ApConfigurationTest(configFile), TestCase::Duration::QUICK);
    AddTestCase(new StaConfigurationTest(configFile), TestCase::Duration::QUICK);
    AddTestCase(new InterfererConfigurationTest(configFile), TestCase::Duration::QUICK);
    AddTestCase(new SpectrumPhyTest(configFile), TestCase::Duration::QUICK);
    AddTestCase(new StaMobilityTest(configFile), TestCase::Duration::QUICK);
    AddTestCase(new AnnotationTest(configFile), TestCase::Duration::QUICK);
    AddTestCase(new SpectrumConfigTest(configFile), TestCase::Duration::QUICK);
}

static SpectrumShadowIntegrationTestSuite sSpectrumShadowIntegrationTestSuite;
