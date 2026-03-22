/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Spectrum Shadow Module - Test Suite
 */

#include "ns3/test.h"
#include "ns3/spectrum-shadow.h"
#include "ns3/spectrum-shadow-helper.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/node-container.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/constant-position-mobility-model.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SpectrumShadowTestSuite");

/**
 * \ingroup spectrum-shadow-tests
 *
 * Test that SpectrumShadow object can be created and configured
 */
class SpectrumShadowConfigTest : public TestCase
{
public:
    SpectrumShadowConfigTest();
    virtual ~SpectrumShadowConfigTest();

private:
    virtual void DoRun();
};

SpectrumShadowConfigTest::SpectrumShadowConfigTest()
    : TestCase("Test SpectrumShadow configuration")
{
}

SpectrumShadowConfigTest::~SpectrumShadowConfigTest()
{
}

void
SpectrumShadowConfigTest::DoRun()
{
    // Create SpectrumShadow object
    Ptr<SpectrumShadow> shadow = CreateObject<SpectrumShadow>();
    NS_TEST_ASSERT_MSG_NE(shadow, nullptr, "Failed to create SpectrumShadow");

    // Test default configuration
    SpectrumShadowConfig config = shadow->GetConfig();
    NS_TEST_ASSERT_MSG_EQ(config.startFrequency, 2.4e9, "Default start frequency should be 2.4 GHz");
    NS_TEST_ASSERT_MSG_EQ(config.frequencyResolution, 100e3, "Default resolution should be 100 kHz");
    NS_TEST_ASSERT_MSG_EQ(config.numFrequencyBins, 1000, "Default bins should be 1000");

    // Test configuration update
    SpectrumShadowConfig newConfig;
    newConfig.startFrequency = 5.0e9;
    newConfig.frequencyResolution = 200e3;
    newConfig.numFrequencyBins = 500;
    newConfig.pipePath = "/custom/path";
    newConfig.enablePipeStreaming = false;

    shadow->SetConfig(newConfig);
    config = shadow->GetConfig();

    NS_TEST_ASSERT_MSG_EQ(config.startFrequency, 5.0e9, "Start frequency should be updated");
    NS_TEST_ASSERT_MSG_EQ(config.frequencyResolution, 200e3, "Resolution should be updated");
    NS_TEST_ASSERT_MSG_EQ(config.numFrequencyBins, 500, "Bins should be updated");
    NS_TEST_ASSERT_MSG_EQ(config.pipePath, "/custom/path", "Pipe path should be updated");
    NS_TEST_ASSERT_MSG_EQ(config.enablePipeStreaming, false, "Streaming flag should be updated");

    Simulator::Destroy();
}

/**
 * \ingroup spectrum-shadow-tests
 *
 * Test that SpectrumShadow can be initialized with a spectrum channel
 */
class SpectrumShadowChannelTest : public TestCase
{
public:
    SpectrumShadowChannelTest();
    virtual ~SpectrumShadowChannelTest();

private:
    virtual void DoRun();
};

SpectrumShadowChannelTest::SpectrumShadowChannelTest()
    : TestCase("Test SpectrumShadow channel setup")
{
}

SpectrumShadowChannelTest::~SpectrumShadowChannelTest()
{
}

void
SpectrumShadowChannelTest::DoRun()
{
    // Create spectrum channel
    Ptr<MultiModelSpectrumChannel> channel = CreateObject<MultiModelSpectrumChannel>();
    NS_TEST_ASSERT_MSG_NE(channel, nullptr, "Failed to create spectrum channel");

    // Create SpectrumShadow and set channel
    Ptr<SpectrumShadow> shadow = CreateObject<SpectrumShadow>();
    shadow->SetChannel(channel);

    NS_TEST_ASSERT_MSG_EQ(shadow->GetChannel(), channel, "Channel should be set correctly");
    NS_TEST_ASSERT_MSG_EQ(shadow->IsInitialized(), false, "Should not be initialized yet");

    // Initialize
    shadow->Initialize();
    NS_TEST_ASSERT_MSG_EQ(shadow->IsInitialized(), true, "Should be initialized after Initialize()");

    Simulator::Destroy();
}

/**
 * \ingroup spectrum-shadow-tests
 *
 * Test that SpectrumShadow annotation manager works
 */
class SpectrumShadowAnnotationTest : public TestCase
{
public:
    SpectrumShadowAnnotationTest();
    virtual ~SpectrumShadowAnnotationTest();

private:
    virtual void DoRun();
};

SpectrumShadowAnnotationTest::SpectrumShadowAnnotationTest()
    : TestCase("Test SpectrumShadow annotation manager")
{
}

SpectrumShadowAnnotationTest::~SpectrumShadowAnnotationTest()
{
}

void
SpectrumShadowAnnotationTest::DoRun()
{
    Ptr<SpectrumShadow> shadow = CreateObject<SpectrumShadow>();

    // Get annotation manager
    Ptr<AnnotationManager> annotMgr = shadow->GetAnnotationManager();
    NS_TEST_ASSERT_MSG_NE(annotMgr, nullptr, "Annotation manager should exist");

    // Add some annotations
    annotMgr->AddAnnotation("wifi", 0.0, 1.0, 2.4e9, 20e6);
    annotMgr->AddAnnotation("bluetooth", 0.5, 0.1, 2.402e9, 1e6);

    // Check annotations were added
    const auto& annotations = annotMgr->GetAnnotations();
    NS_TEST_ASSERT_MSG_EQ(annotations.size(), 2, "Should have 2 annotations");
    NS_TEST_ASSERT_MSG_EQ(annotations[0].interfererType, "wifi", "First should be wifi");
    NS_TEST_ASSERT_MSG_EQ(annotations[1].interfererType, "bluetooth", "Second should be bluetooth");

    Simulator::Destroy();
}

/**
 * \ingroup spectrum-shadow-tests
 *
 * Test SpectrumShadowHelper setup sequence
 */
class SpectrumShadowHelperSetupTest : public TestCase
{
public:
    SpectrumShadowHelperSetupTest();
    virtual ~SpectrumShadowHelperSetupTest();

private:
    virtual void DoRun();
};

SpectrumShadowHelperSetupTest::SpectrumShadowHelperSetupTest()
    : TestCase("Test SpectrumShadowHelper setup sequence")
{
}

SpectrumShadowHelperSetupTest::~SpectrumShadowHelperSetupTest()
{
}

void
SpectrumShadowHelperSetupTest::DoRun()
{
    SpectrumShadowHelper helper;

    // Verify initial state - nothing should be complete
    NS_TEST_ASSERT_MSG_EQ(helper.IsSetupComplete("nodes"), false, "Nodes should not be complete");
    NS_TEST_ASSERT_MSG_EQ(helper.IsSetupComplete("channel"), false, "Channel should not be complete");
    NS_TEST_ASSERT_MSG_EQ(helper.IsSetupComplete("wifi"), false, "Wifi should not be complete");
    NS_TEST_ASSERT_MSG_EQ(helper.IsSetupComplete("mobility"), false, "Mobility should not be complete");

    // Setup channel (no prerequisites)
    helper.SetupSpectrumChannel();
    NS_TEST_ASSERT_MSG_EQ(helper.IsSetupComplete("channel"), true, "Channel should be complete");

    // Get channel
    Ptr<MultiModelSpectrumChannel> channel = helper.GetSpectrumChannel();
    NS_TEST_ASSERT_MSG_NE(channel, nullptr, "Channel should exist");

    Simulator::Destroy();
}

/**
 * \ingroup spectrum-shadow-tests
 *
 * Test SpectrumShadow statistics
 */
class SpectrumShadowStatsTest : public TestCase
{
public:
    SpectrumShadowStatsTest();
    virtual ~SpectrumShadowStatsTest();

private:
    virtual void DoRun();
};

SpectrumShadowStatsTest::SpectrumShadowStatsTest()
    : TestCase("Test SpectrumShadow statistics")
{
}

SpectrumShadowStatsTest::~SpectrumShadowStatsTest()
{
}

void
SpectrumShadowStatsTest::DoRun()
{
    Ptr<SpectrumShadow> shadow = CreateObject<SpectrumShadow>();

    // Get initial stats
    SpectrumShadowStats stats = shadow->GetStats();
    NS_TEST_ASSERT_MSG_EQ(stats.totalPsdSamples, 0, "Initial samples should be 0");
    NS_TEST_ASSERT_MSG_EQ(stats.psdPacketsSent, 0, "Initial packets should be 0");
    NS_TEST_ASSERT_MSG_EQ(stats.psdBytesWritten, 0, "Initial bytes should be 0");
    NS_TEST_ASSERT_MSG_EQ(stats.interferersActive, 0, "Initial interferers should be 0");

    Simulator::Destroy();
}

/**
 * \ingroup spectrum-shadow-tests
 *
 * Spectrum Shadow Test Suite
 */
class SpectrumShadowTestSuite : public TestSuite
{
public:
    SpectrumShadowTestSuite();
};

SpectrumShadowTestSuite::SpectrumShadowTestSuite()
    : TestSuite("spectrum-shadow", Type::UNIT)
{
    AddTestCase(new SpectrumShadowConfigTest, TestCase::Duration::QUICK);
    AddTestCase(new SpectrumShadowChannelTest, TestCase::Duration::QUICK);
    AddTestCase(new SpectrumShadowAnnotationTest, TestCase::Duration::QUICK);
    AddTestCase(new SpectrumShadowHelperSetupTest, TestCase::Duration::QUICK);
    AddTestCase(new SpectrumShadowStatsTest, TestCase::Duration::QUICK);
}

static SpectrumShadowTestSuite sSpectrumShadowTestSuite;
