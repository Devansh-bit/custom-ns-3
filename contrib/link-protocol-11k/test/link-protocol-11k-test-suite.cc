/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "../model/link-measurement-request.h"
#include "../model/link-measurement-report.h"

#include "ns3/test.h"
#include "ns3/packet.h"

using namespace ns3;

/**
 * \defgroup link-protocol-11k-tests Tests for link-protocol-11k
 * \ingroup link-protocol-11k
 * \ingroup tests
 */

/**
 * \ingroup link-protocol-11k-tests
 * \brief Test LinkMeasurementRequest creation and accessors
 */
class LinkMeasurementRequestTestCase : public TestCase
{
  public:
    LinkMeasurementRequestTestCase();

  private:
    void DoRun() override;
};

LinkMeasurementRequestTestCase::LinkMeasurementRequestTestCase()
    : TestCase("Test LinkMeasurementRequest creation and accessors")
{
}

void
LinkMeasurementRequestTestCase::DoRun()
{
    Mac48Address from("00:11:22:33:44:55");
    Mac48Address to("aa:bb:cc:dd:ee:ff");
    uint8_t dialogToken = 42;
    uint8_t txPowerUsed = 20;  // 20 dBm
    uint8_t maxTxPower = 23;   // 23 dBm
    Ptr<Packet> packet = Create<Packet>(100);

    LinkMeasurementRequest request(from, to, dialogToken, txPowerUsed, maxTxPower, packet);

    NS_TEST_ASSERT_MSG_EQ(request.GetFrom(), from, "Source address mismatch");
    NS_TEST_ASSERT_MSG_EQ(request.GetTo(), to, "Destination address mismatch");
    NS_TEST_ASSERT_MSG_EQ(request.GetDialogToken(), dialogToken, "Dialog token mismatch");
    NS_TEST_ASSERT_MSG_EQ(request.GetTransmitPowerUsed(), txPowerUsed, "Tx power used mismatch");
    NS_TEST_ASSERT_MSG_EQ(request.GetMaxTransmitPower(), maxTxPower, "Max tx power mismatch");
    NS_TEST_ASSERT_MSG_EQ(request.GetPacket()->GetSize(), 100, "Packet size mismatch");
}

/**
 * \ingroup link-protocol-11k-tests
 * \brief Test LinkMeasurementReport creation and accessors
 */
class LinkMeasurementReportTestCase : public TestCase
{
  public:
    LinkMeasurementReportTestCase();

  private:
    void DoRun() override;
};

LinkMeasurementReportTestCase::LinkMeasurementReportTestCase()
    : TestCase("Test LinkMeasurementReport creation and accessors")
{
}

void
LinkMeasurementReportTestCase::DoRun()
{
    Mac48Address from("00:11:22:33:44:55");
    Mac48Address to("aa:bb:cc:dd:ee:ff");
    uint8_t dialogToken = 42;
    uint8_t txPower = 20;
    uint8_t linkMargin = 15;
    uint8_t rxAntennaId = 1;
    uint8_t txAntennaId = 2;
    uint16_t rcpi = 180;  // ~-20 dBm
    uint16_t rsni = 60;   // 30 dB SNR

    LinkMeasurementReport report(from, to, dialogToken, txPower, linkMargin,
                                  rxAntennaId, txAntennaId, rcpi, rsni);

    NS_TEST_ASSERT_MSG_EQ(report.GetDialogToken(), dialogToken, "Dialog token mismatch");
    NS_TEST_ASSERT_MSG_EQ(report.GetReceiveAntennaId(), rxAntennaId, "Rx antenna ID mismatch");
    NS_TEST_ASSERT_MSG_EQ(report.GetTransmitAntennaId(), txAntennaId, "Tx antenna ID mismatch");
    NS_TEST_ASSERT_MSG_EQ(report.GetRcpi(), rcpi, "RCPI mismatch");
    NS_TEST_ASSERT_MSG_EQ(report.GetRsni(), rsni, "RSNI mismatch");
    NS_TEST_ASSERT_MSG_EQ(report.GetLinkMarginDb(), linkMargin, "Link margin mismatch");
}

/**
 * \ingroup link-protocol-11k-tests
 * \brief Test RCPI/RSNI conversion utilities
 */
class RcpiRsniConversionTestCase : public TestCase
{
  public:
    RcpiRsniConversionTestCase();

  private:
    void DoRun() override;
};

RcpiRsniConversionTestCase::RcpiRsniConversionTestCase()
    : TestCase("Test RCPI/RSNI conversion utilities in LinkMeasurementReport")
{
}

void
RcpiRsniConversionTestCase::DoRun()
{
    // Test RCPI to dBm conversion
    // RCPI = 2 * (dBm + 110), so dBm = (RCPI / 2) - 110
    NS_TEST_ASSERT_MSG_EQ_TOL(LinkMeasurementReport::ConvertRcpiToDbm(0), -110.0, 0.01,
                              "RCPI=0 should give -110 dBm");
    NS_TEST_ASSERT_MSG_EQ_TOL(LinkMeasurementReport::ConvertRcpiToDbm(100), -60.0, 0.01,
                              "RCPI=100 should give -60 dBm");
    NS_TEST_ASSERT_MSG_EQ_TOL(LinkMeasurementReport::ConvertRcpiToDbm(220), 0.0, 0.01,
                              "RCPI=220 should give 0 dBm");

    // Test RSNI to dB conversion
    // RSNI = 2 * SNR, so SNR = RSNI / 2
    NS_TEST_ASSERT_MSG_EQ_TOL(LinkMeasurementReport::ConvertRsniToDb(0), 0.0, 0.01,
                              "RSNI=0 should give 0 dB");
    NS_TEST_ASSERT_MSG_EQ_TOL(LinkMeasurementReport::ConvertRsniToDb(60), 30.0, 0.01,
                              "RSNI=60 should give 30 dB");
    NS_TEST_ASSERT_MSG_EQ_TOL(LinkMeasurementReport::ConvertRsniToDb(254), 127.0, 0.01,
                              "RSNI=254 should give 127 dB");
}

/**
 * \ingroup link-protocol-11k-tests
 * \brief TestSuite for module link-protocol-11k
 */
class LinkProtocol11kTestSuite : public TestSuite
{
  public:
    LinkProtocol11kTestSuite();
};

LinkProtocol11kTestSuite::LinkProtocol11kTestSuite()
    : TestSuite("link-protocol-11k", Type::UNIT)
{
    AddTestCase(new LinkMeasurementRequestTestCase, TestCase::Duration::QUICK);
    AddTestCase(new LinkMeasurementReportTestCase, TestCase::Duration::QUICK);
    AddTestCase(new RcpiRsniConversionTestCase, TestCase::Duration::QUICK);
}

static LinkProtocol11kTestSuite sLinkProtocol11kTestSuite;
