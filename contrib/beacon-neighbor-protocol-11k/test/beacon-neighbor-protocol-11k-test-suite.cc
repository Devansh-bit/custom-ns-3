/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: [Your Name]
 */

#include "../model/beacon-neighbor-model.h"
#include "../model/neighbor-report-element.h"
#include "../model/beacon-report-element.h"

#include "ns3/buffer.h"
#include "ns3/log.h"
#include "ns3/test.h"

using namespace ns3;

/**
 * \defgroup beacon-neighbor-protocol-11k-tests Tests for beacon-neighbor-protocol-11k
 * \ingroup beacon-neighbor-protocol-11k
 * \ingroup tests
 */

/**
 * \ingroup beacon-neighbor-protocol-11k-tests
 * \brief Test NeighborReportElement serialization and deserialization
 */
class NeighborReportElementSerializationTestCase : public TestCase
{
  public:
    NeighborReportElementSerializationTestCase();

  private:
    void DoRun() override;
};

NeighborReportElementSerializationTestCase::NeighborReportElementSerializationTestCase()
    : TestCase("Test NeighborReportElement serialization/deserialization")
{
}

void
NeighborReportElementSerializationTestCase::DoRun()
{
    // Create and populate element
    NeighborReportElement original;
    original.SetBssid(Mac48Address("00:11:22:33:44:55"));
    original.SetBssidInfo(0x12345678);
    original.SetOperatingClass(115);  // 5 GHz, 20 MHz
    original.SetChannelNumber(36);
    original.SetPhyType(7);  // HT

    // Serialize
    uint16_t serializedSize = original.GetSerializedSize();
    NS_TEST_ASSERT_MSG_EQ(serializedSize, 15, "Serialized size should be 15 (2 header + 13 body)");

    Buffer buffer;
    buffer.AddAtStart(serializedSize);
    Buffer::Iterator iter = buffer.Begin();
    original.Serialize(iter);

    // Deserialize into new element
    NeighborReportElement deserialized;
    iter = buffer.Begin();
    deserialized.Deserialize(iter);

    // Verify fields match
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetBssid(),
                          Mac48Address("00:11:22:33:44:55"),
                          "BSSID mismatch after deserialization");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetBssidInfo(),
                          0x12345678,
                          "BSSIDInfo mismatch after deserialization");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetOperatingClass(),
                          115,
                          "OperatingClass mismatch after deserialization");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetChannelNumber(),
                          36,
                          "ChannelNumber mismatch after deserialization");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetPhyType(),
                          7,
                          "PhyType mismatch after deserialization");
}

/**
 * \ingroup beacon-neighbor-protocol-11k-tests
 * \brief Test BeaconReportElement serialization and deserialization
 */
class BeaconReportElementSerializationTestCase : public TestCase
{
  public:
    BeaconReportElementSerializationTestCase();

  private:
    void DoRun() override;
};

BeaconReportElementSerializationTestCase::BeaconReportElementSerializationTestCase()
    : TestCase("Test BeaconReportElement serialization/deserialization")
{
}

void
BeaconReportElementSerializationTestCase::DoRun()
{
    // Create and populate element
    BeaconReportElement original;
    original.SetMeasurementToken(42);
    original.SetMeasurementReportMode(0);
    original.SetOperatingClass(115);
    original.SetChannelNumber(36);
    original.SetActualMeasurementStartTime(123456789012345ULL);
    original.SetMeasurementDuration(100);  // 100 TUs
    original.SetReportedFrameInfo(0x07);   // PHY type 7 (HT), frame type 0 (beacon)
    original.SetRcpi(180);  // ~-20 dBm
    original.SetRsni(60);   // 30 dB SNR
    original.SetBssid(Mac48Address("aa:bb:cc:dd:ee:ff"));
    original.SetAntennaId(1);
    original.SetParentTsf(0xDEADBEEF);

    // Serialize
    uint16_t serializedSize = original.GetSerializedSize();
    NS_TEST_ASSERT_MSG_EQ(serializedSize, 28, "Serialized size should be 28 (2 header + 26 body)");

    Buffer buffer;
    buffer.AddAtStart(serializedSize);
    Buffer::Iterator iter = buffer.Begin();
    original.Serialize(iter);

    // Deserialize
    BeaconReportElement deserialized;
    iter = buffer.Begin();
    deserialized.Deserialize(iter);

    // Verify fields
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetMeasurementToken(),
                          42,
                          "MeasurementToken mismatch");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetOperatingClass(),
                          115,
                          "OperatingClass mismatch");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetChannelNumber(),
                          36,
                          "ChannelNumber mismatch");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetActualMeasurementStartTime(),
                          123456789012345ULL,
                          "ActualMeasurementStartTime mismatch");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetMeasurementDuration(),
                          100,
                          "MeasurementDuration mismatch");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetRcpi(),
                          180,
                          "RCPI mismatch");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetRsni(),
                          60,
                          "RSNI mismatch");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetBssid(),
                          Mac48Address("aa:bb:cc:dd:ee:ff"),
                          "BSSID mismatch");
    NS_TEST_ASSERT_MSG_EQ(deserialized.GetParentTsf(),
                          0xDEADBEEF,
                          "ParentTsf mismatch");
}

/**
 * \ingroup beacon-neighbor-protocol-11k-tests
 * \brief Test RCPI/RSNI conversion functions
 */
class RcpiRsniConversionTestCase : public TestCase
{
  public:
    RcpiRsniConversionTestCase();

  private:
    void DoRun() override;
};

RcpiRsniConversionTestCase::RcpiRsniConversionTestCase()
    : TestCase("Test RCPI/RSNI conversion functions")
{
}

void
RcpiRsniConversionTestCase::DoRun()
{
    // Test RssiToRcpi
    // RCPI = 2 * (RSSI + 110)
    NS_TEST_ASSERT_MSG_EQ(RssiToRcpi(-110.0), 0, "RSSI=-110 should give RCPI=0");
    NS_TEST_ASSERT_MSG_EQ(RssiToRcpi(-60.0), 100, "RSSI=-60 should give RCPI=100");
    NS_TEST_ASSERT_MSG_EQ(RssiToRcpi(0.0), 220, "RSSI=0 should give RCPI=220");
    NS_TEST_ASSERT_MSG_EQ(RssiToRcpi(-120.0), 0, "RSSI=-120 should clamp to RCPI=0");
    NS_TEST_ASSERT_MSG_EQ(RssiToRcpi(50.0), 255, "RSSI=50 should clamp to RCPI=255");

    // Test RcpiToRssi (inverse)
    NS_TEST_ASSERT_MSG_EQ_TOL(RcpiToRssi(0), -110.0, 0.01, "RCPI=0 should give RSSI=-110");
    NS_TEST_ASSERT_MSG_EQ_TOL(RcpiToRssi(100), -60.0, 0.01, "RCPI=100 should give RSSI=-60");
    NS_TEST_ASSERT_MSG_EQ_TOL(RcpiToRssi(220), 0.0, 0.01, "RCPI=220 should give RSSI=0");

    // Test SnrToRsni
    // RSNI = 2 * SNR
    NS_TEST_ASSERT_MSG_EQ(SnrToRsni(0.0), 0, "SNR=0 should give RSNI=0");
    NS_TEST_ASSERT_MSG_EQ(SnrToRsni(30.0), 60, "SNR=30 should give RSNI=60");
    NS_TEST_ASSERT_MSG_EQ(SnrToRsni(127.5), 255, "SNR=127.5 should give RSNI=255");
    NS_TEST_ASSERT_MSG_EQ(SnrToRsni(-10.0), 0, "SNR=-10 should clamp to RSNI=0");

    // Test RsniToSnr (inverse)
    NS_TEST_ASSERT_MSG_EQ_TOL(RsniToSnr(0), 0.0, 0.01, "RSNI=0 should give SNR=0");
    NS_TEST_ASSERT_MSG_EQ_TOL(RsniToSnr(60), 30.0, 0.01, "RSNI=60 should give SNR=30");
}

/**
 * \ingroup beacon-neighbor-protocol-11k-tests
 * \brief TestSuite for module beacon-neighbor-protocol-11k
 */
class BeaconNeighborProtocol11kTestSuite : public TestSuite
{
  public:
    BeaconNeighborProtocol11kTestSuite();
};

BeaconNeighborProtocol11kTestSuite::BeaconNeighborProtocol11kTestSuite()
    : TestSuite("beacon-neighbor-protocol-11k", Type::UNIT)
{
    AddTestCase(new NeighborReportElementSerializationTestCase, TestCase::Duration::QUICK);
    AddTestCase(new BeaconReportElementSerializationTestCase, TestCase::Duration::QUICK);
    AddTestCase(new RcpiRsniConversionTestCase, TestCase::Duration::QUICK);
}

static BeaconNeighborProtocol11kTestSuite sbeaconNeighborProtocol11kTestSuite;
