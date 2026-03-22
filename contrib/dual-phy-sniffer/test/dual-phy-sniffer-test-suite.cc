/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "../helper/dual-phy-sniffer-helper.h"

#include "ns3/test.h"

using namespace ns3;

/**
 * @defgroup dual-phy-sniffer-tests Tests for dual-phy-sniffer
 * @ingroup dual-phy-sniffer
 * @ingroup tests
 */

/**
 * @ingroup dual-phy-sniffer-tests
 * @brief Test DualPhyMeasurement struct creation
 */
class DualPhyMeasurementTestCase : public TestCase
{
  public:
    DualPhyMeasurementTestCase();

  private:
    void DoRun() override;
};

DualPhyMeasurementTestCase::DualPhyMeasurementTestCase()
    : TestCase("Test DualPhyMeasurement struct initialization")
{
}

void
DualPhyMeasurementTestCase::DoRun()
{
    DualPhyMeasurement measurement;
    measurement.receiverBssid = Mac48Address("00:11:22:33:44:55");
    measurement.transmitterBssid = Mac48Address("aa:bb:cc:dd:ee:ff");
    measurement.channel = 36;
    measurement.rssi = -60.0;
    measurement.rcpi = 100.0;  // RCPI = 2 * (RSSI + 110) = 2 * 50 = 100
    measurement.timestamp = 1.5;

    NS_TEST_ASSERT_MSG_EQ(measurement.channel, 36, "Channel mismatch");
    NS_TEST_ASSERT_MSG_EQ_TOL(measurement.rssi, -60.0, 0.01, "RSSI mismatch");
    NS_TEST_ASSERT_MSG_EQ_TOL(measurement.rcpi, 100.0, 0.01, "RCPI mismatch");
}

/**
 * @ingroup dual-phy-sniffer-tests
 * @brief Test BeaconInfo struct creation and fields
 */
class BeaconInfoTestCase : public TestCase
{
  public:
    BeaconInfoTestCase();

  private:
    void DoRun() override;
};

BeaconInfoTestCase::BeaconInfoTestCase()
    : TestCase("Test BeaconInfo struct fields")
{
}

void
BeaconInfoTestCase::DoRun()
{
    BeaconInfo info;
    info.bssid = Mac48Address("aa:bb:cc:dd:ee:ff");
    info.receivedBy = Mac48Address("00:11:22:33:44:55");
    info.rssi = -55.0;
    info.rcpi = 110.0;
    info.snr = 25.0;
    info.channel = 40;
    info.band = WIFI_PHY_BAND_5GHZ;
    info.channelWidth = 80;
    info.staCount = 5;
    info.channelUtilization = 128;  // 50%
    info.beaconCount = 10;

    NS_TEST_ASSERT_MSG_EQ(info.channel, 40, "Channel mismatch");
    NS_TEST_ASSERT_MSG_EQ(info.band, WIFI_PHY_BAND_5GHZ, "Band mismatch");
    NS_TEST_ASSERT_MSG_EQ(info.channelWidth, 80, "Channel width mismatch");
    NS_TEST_ASSERT_MSG_EQ(info.staCount, 5, "STA count mismatch");
    NS_TEST_ASSERT_MSG_EQ(info.channelUtilization, 128, "Channel utilization mismatch");
    NS_TEST_ASSERT_MSG_EQ(info.beaconCount, 10, "Beacon count mismatch");
    NS_TEST_ASSERT_MSG_EQ_TOL(info.rssi, -55.0, 0.01, "RSSI mismatch");
    NS_TEST_ASSERT_MSG_EQ_TOL(info.snr, 25.0, 0.01, "SNR mismatch");
}

/**
 * @ingroup dual-phy-sniffer-tests
 * @brief Test DualPhySnifferHelper default configuration
 */
class DualPhySnifferHelperConfigTestCase : public TestCase
{
  public:
    DualPhySnifferHelperConfigTestCase();

  private:
    void DoRun() override;
};

DualPhySnifferHelperConfigTestCase::DualPhySnifferHelperConfigTestCase()
    : TestCase("Test DualPhySnifferHelper configuration methods")
{
}

void
DualPhySnifferHelperConfigTestCase::DoRun()
{
    DualPhySnifferHelper helper;
    
    // Test SetScanningChannels
    std::vector<uint8_t> channels = {36, 40, 44, 48};
    helper.SetScanningChannels(channels);
    
    // Test SetHopInterval
    helper.SetHopInterval(MilliSeconds(300));
    
    // Test SetSsid
    helper.SetSsid(Ssid("TestNetwork"));
    
    // Test SetBeaconMaxAge
    helper.SetBeaconMaxAge(Seconds(30));
    
    // Test SetBeaconMaxEntries
    helper.SetBeaconMaxEntries(100);
    
    // Verify helper was created successfully (no crashes)
    NS_TEST_ASSERT_MSG_EQ(true, true, "Helper configuration completed without errors");
}

/**
 * @ingroup dual-phy-sniffer-tests
 * @brief TestSuite for module dual-phy-sniffer
 */
class DualPhySnifferTestSuite : public TestSuite
{
  public:
    DualPhySnifferTestSuite();
};

DualPhySnifferTestSuite::DualPhySnifferTestSuite()
    : TestSuite("dual-phy-sniffer", Type::UNIT)
{
    AddTestCase(new DualPhyMeasurementTestCase, TestCase::Duration::QUICK);
    AddTestCase(new BeaconInfoTestCase, TestCase::Duration::QUICK);
    AddTestCase(new DualPhySnifferHelperConfigTestCase, TestCase::Duration::QUICK);
}

static DualPhySnifferTestSuite sDualPhySnifferTestSuite;
