/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "../model/parsed-frame-context.h"

#include "ns3/test.h"
#include "ns3/mac48-address.h"

using namespace ns3;

/**
 * @defgroup unified-phy-sniffer-tests Tests for unified-phy-sniffer
 * @ingroup unified-phy-sniffer
 * @ingroup tests
 */

/**
 * @ingroup unified-phy-sniffer-tests
 * @brief Test FrameInterest flags
 */
class FrameInterestTestCase : public TestCase
{
  public:
    FrameInterestTestCase();

  private:
    void DoRun() override;
};

FrameInterestTestCase::FrameInterestTestCase()
    : TestCase("Test FrameInterest flag combinations")
{
}

void
FrameInterestTestCase::DoRun()
{
    // Test individual flags
    NS_TEST_ASSERT_MSG_EQ(static_cast<uint8_t>(FrameInterest::BEACON), 0x01, "BEACON should be 0x01");
    NS_TEST_ASSERT_MSG_EQ(static_cast<uint8_t>(FrameInterest::ACTION_CAT5), 0x02, "ACTION_CAT5 should be 0x02");
    NS_TEST_ASSERT_MSG_EQ(static_cast<uint8_t>(FrameInterest::DATA), 0x10, "DATA should be 0x10");
    
    // Test combination
    FrameInterest combined = FrameInterest::BEACON | FrameInterest::ACTION_CAT5;
    NS_TEST_ASSERT_MSG_EQ(static_cast<uint8_t>(combined), 0x03, "BEACON | ACTION_CAT5 should be 0x03");
    
    // Test ALL
    NS_TEST_ASSERT_MSG_EQ(static_cast<uint8_t>(FrameInterest::ALL), 0xFF, "ALL should be 0xFF");
}

/**
 * @ingroup unified-phy-sniffer-tests
 * @brief Test ParsedFrameContext signal measurements
 */
class ParsedFrameContextSignalTestCase : public TestCase
{
  public:
    ParsedFrameContextSignalTestCase();

  private:
    void DoRun() override;
};

ParsedFrameContextSignalTestCase::ParsedFrameContextSignalTestCase()
    : TestCase("Test ParsedFrameContext signal measurement storage")
{
}

void
ParsedFrameContextSignalTestCase::DoRun()
{
    ParsedFrameContext ctx;
    
    // Set signal measurements
    ctx.rssi = -55.0;
    ctx.snr = 30.0;
    ctx.rcpi = 110;  // = 2 * (-55 + 110) = 110
    ctx.rsni = 60;   // = 2 * 30 = 60
    ctx.channelFreqMhz = 5180;  // Channel 36
    ctx.channel = 36;
    ctx.band = WIFI_PHY_BAND_5GHZ;
    
    NS_TEST_ASSERT_MSG_EQ_TOL(ctx.rssi, -55.0, 0.01, "RSSI mismatch");
    NS_TEST_ASSERT_MSG_EQ_TOL(ctx.snr, 30.0, 0.01, "SNR mismatch");
    NS_TEST_ASSERT_MSG_EQ(ctx.rcpi, 110, "RCPI mismatch");
    NS_TEST_ASSERT_MSG_EQ(ctx.rsni, 60, "RSNI mismatch");
    NS_TEST_ASSERT_MSG_EQ(ctx.channel, 36, "Channel mismatch");
    NS_TEST_ASSERT_MSG_EQ(ctx.band, WIFI_PHY_BAND_5GHZ, "Band mismatch");
}

/**
 * @ingroup unified-phy-sniffer-tests
 * @brief Test FrameType classification enum values
 */
class FrameTypeEnumTestCase : public TestCase
{
  public:
    FrameTypeEnumTestCase();

  private:
    void DoRun() override;
};

FrameTypeEnumTestCase::FrameTypeEnumTestCase()
    : TestCase("Test FrameType enum values")
{
}

void
FrameTypeEnumTestCase::DoRun()
{
    // Verify frame types have distinct values
    NS_TEST_ASSERT_MSG_NE(static_cast<uint8_t>(FrameType::BEACON),
                          static_cast<uint8_t>(FrameType::ACTION),
                          "BEACON and ACTION should be different");
    NS_TEST_ASSERT_MSG_NE(static_cast<uint8_t>(FrameType::DATA),
                          static_cast<uint8_t>(FrameType::CONTROL),
                          "DATA and CONTROL should be different");
    NS_TEST_ASSERT_MSG_EQ(static_cast<uint8_t>(FrameType::UNKNOWN), 0, "UNKNOWN should be 0");
}

/**
 * @ingroup unified-phy-sniffer-tests
 * @brief TestSuite for module unified-phy-sniffer
 */
class UnifiedPhySnifferTestSuite : public TestSuite
{
  public:
    UnifiedPhySnifferTestSuite();
};

UnifiedPhySnifferTestSuite::UnifiedPhySnifferTestSuite()
    : TestSuite("unified-phy-sniffer", Type::UNIT)
{
    AddTestCase(new FrameInterestTestCase, TestCase::Duration::QUICK);
    AddTestCase(new ParsedFrameContextSignalTestCase, TestCase::Duration::QUICK);
    AddTestCase(new FrameTypeEnumTestCase, TestCase::Duration::QUICK);
}

static UnifiedPhySnifferTestSuite sUnifiedPhySnifferTestSuite;
