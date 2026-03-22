/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "../model/bss_tm_11v.h"

#include "ns3/test.h"

using namespace ns3;

/**
 * \defgroup bss_tm_11v-tests Tests for bss_tm_11v
 * \ingroup bss_tm_11v
 * \ingroup tests
 */

/**
 * \ingroup bss_tm_11v-tests
 * \brief Test BssTmParameters default construction
 */
class BssTmParametersDefaultTestCase : public TestCase
{
  public:
    BssTmParametersDefaultTestCase();

  private:
    void DoRun() override;
};

BssTmParametersDefaultTestCase::BssTmParametersDefaultTestCase()
    : TestCase("Test BssTmParameters default constructor")
{
}

void
BssTmParametersDefaultTestCase::DoRun()
{
    BssTmParameters params;
    
    // Default values match those defined in bss_tm_11v.cc constructor
    NS_TEST_ASSERT_MSG_EQ(params.dialogToken, 0x03, "Default dialogToken should be 3");
    NS_TEST_ASSERT_MSG_EQ(params.disassociationTimer, 100, "Default disassociationTimer should be 100 TUs");
    NS_TEST_ASSERT_MSG_EQ(params.validityInterval, 0x05, "Default validityInterval should be 5");
    NS_TEST_ASSERT_MSG_EQ(params.candidates.size(), 0, "Default candidates should be empty");
}

/**
 * \ingroup bss_tm_11v-tests
 * \brief Test BssTmParameters with candidate APs
 */
class BssTmParametersCandidatesTestCase : public TestCase
{
  public:
    BssTmParametersCandidatesTestCase();

  private:
    void DoRun() override;
};

BssTmParametersCandidatesTestCase::BssTmParametersCandidatesTestCase()
    : TestCase("Test BssTmParameters candidate list management")
{
}

void
BssTmParametersCandidatesTestCase::DoRun()
{
    BssTmParameters params;
    params.dialogToken = 42;
    params.disassociationTimer = 100;  // TUs
    params.validityInterval = 50;
    params.reasonCode = BssTmParameters::ReasonCode::LOW_RSSI;

    // Add candidate AP
    BssTmParameters::CandidateAP candidate1;
    candidate1.operatingClass = 115;
    candidate1.channel = 36;
    candidate1.phyType = 7;  // HT
    candidate1.preference = 255;  // Highest preference
    // Set BSSID
    candidate1.BSSID[0] = 0x00;
    candidate1.BSSID[1] = 0x11;
    candidate1.BSSID[2] = 0x22;
    candidate1.BSSID[3] = 0x33;
    candidate1.BSSID[4] = 0x44;
    candidate1.BSSID[5] = 0x55;

    params.candidates.push_back(candidate1);

    NS_TEST_ASSERT_MSG_EQ(params.dialogToken, 42, "Dialog token mismatch");
    NS_TEST_ASSERT_MSG_EQ(params.candidates.size(), 1, "Should have 1 candidate");
    NS_TEST_ASSERT_MSG_EQ(params.candidates[0].channel, 36, "Candidate channel mismatch");
    NS_TEST_ASSERT_MSG_EQ(params.candidates[0].preference, 255, "Candidate preference mismatch");
}

/**
 * \ingroup bss_tm_11v-tests
 * \brief Test BssTmResponseParameters construction
 */
class BssTmResponseParametersTestCase : public TestCase
{
  public:
    BssTmResponseParametersTestCase();

  private:
    void DoRun() override;
};

BssTmResponseParametersTestCase::BssTmResponseParametersTestCase()
    : TestCase("Test BssTmResponseParameters construction")
{
}

void
BssTmResponseParametersTestCase::DoRun()
{
    BssTmResponseParameters response;
    response.dialogToken = 42;
    response.statusCode = 0;  // Accept
    response.terminationDelay = 0;
    response.channel = 36;
    response.targetBSSID[0] = 0xaa;
    response.targetBSSID[1] = 0xbb;
    response.targetBSSID[2] = 0xcc;
    response.targetBSSID[3] = 0xdd;
    response.targetBSSID[4] = 0xee;
    response.targetBSSID[5] = 0xff;

    NS_TEST_ASSERT_MSG_EQ(response.dialogToken, 42, "Dialog token mismatch");
    NS_TEST_ASSERT_MSG_EQ(response.statusCode, 0, "Status code mismatch");
    NS_TEST_ASSERT_MSG_EQ(response.channel, 36, "Channel mismatch");
    NS_TEST_ASSERT_MSG_EQ(response.targetBSSID[0], 0xaa, "BSSID[0] mismatch");
}

/**
 * \ingroup bss_tm_11v-tests
 * \brief Test ReasonCode to string conversion
 */
class ReasonCodeToStringTestCase : public TestCase
{
  public:
    ReasonCodeToStringTestCase();

  private:
    void DoRun() override;
};

ReasonCodeToStringTestCase::ReasonCodeToStringTestCase()
    : TestCase("Test ReasonCodeToString utility")
{
}

void
ReasonCodeToStringTestCase::DoRun()
{
    std::string lowRssi = ReasonCodeToString(BssTmParameters::ReasonCode::LOW_RSSI);
    std::string highLoad = ReasonCodeToString(BssTmParameters::ReasonCode::HIGH_LOAD);
    
    // Just verify they return non-empty strings
    NS_TEST_ASSERT_MSG_EQ(lowRssi.empty(), false, "LOW_RSSI should have string representation");
    NS_TEST_ASSERT_MSG_EQ(highLoad.empty(), false, "HIGH_LOAD should have string representation");
}

/**
 * \ingroup bss_tm_11v-tests
 * \brief TestSuite for module bss_tm_11v
 */
class BssTm11vTestSuite : public TestSuite
{
  public:
    BssTm11vTestSuite();
};

BssTm11vTestSuite::BssTm11vTestSuite()
    : TestSuite("bss_tm_11v", Type::UNIT)
{
    AddTestCase(new BssTmParametersDefaultTestCase, TestCase::Duration::QUICK);
    AddTestCase(new BssTmParametersCandidatesTestCase, TestCase::Duration::QUICK);
    AddTestCase(new BssTmResponseParametersTestCase, TestCase::Duration::QUICK);
    AddTestCase(new ReasonCodeToStringTestCase, TestCase::Duration::QUICK);
}

static BssTm11vTestSuite sBssTm11vTestSuite;
