/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "spectrum-shadow.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/spectrogram-generation.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SpectrumShadow");
NS_OBJECT_ENSURE_REGISTERED(SpectrumShadow);

TypeId
SpectrumShadow::GetTypeId()
{
    static TypeId tid = TypeId("ns3::SpectrumShadow")
        .SetParent<Object>()
        .SetGroupName("SpectrumShadow")
        .AddConstructor<SpectrumShadow>();
    return tid;
}

SpectrumShadow::SpectrumShadow()
    : m_initialized(false)
{
    NS_LOG_FUNCTION(this);
    m_annotationMgr = CreateObject<AnnotationManager>();
}

SpectrumShadow::~SpectrumShadow()
{
    NS_LOG_FUNCTION(this);
}

void
SpectrumShadow::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_channel = nullptr;
    m_annotationMgr = nullptr;
    Object::DoDispose();
}

void
SpectrumShadow::DoInitialize()
{
    NS_LOG_FUNCTION(this);
    Object::DoInitialize();
}

void
SpectrumShadow::SetConfig(const SpectrumShadowConfig& config)
{
    NS_LOG_FUNCTION(this);
    m_config = config;
}

SpectrumShadowConfig
SpectrumShadow::GetConfig() const
{
    return m_config;
}

void
SpectrumShadow::SetChannel(Ptr<SpectrumChannel> channel)
{
    NS_LOG_FUNCTION(this << channel);
    NS_ASSERT_MSG(!m_initialized, "Cannot set channel after initialization");
    m_channel = channel;
}

Ptr<SpectrumChannel>
SpectrumShadow::GetChannel() const
{
    return m_channel;
}

void
SpectrumShadow::Initialize()
{
    NS_LOG_FUNCTION(this);

    NS_ASSERT_MSG(m_channel, "Spectrum channel must be set before initialization");

    // Create output directories if needed
    if (m_config.enablePipeStreaming) {
        NS_LOG_INFO("Pipe streaming enabled, base path: " << m_config.pipePath);
    }

    if (m_config.enableFileLogging) {
        NS_LOG_INFO("File logging enabled, log path: " << m_config.logPath);
    }

    m_initialized = true;
    NS_LOG_INFO("SpectrumShadow initialized successfully");
}

bool
SpectrumShadow::IsInitialized() const
{
    return m_initialized;
}

SpectrumShadowStats
SpectrumShadow::GetStats() const
{
    SpectrumShadowStats stats = m_stats;
    stats.simulationDuration = Simulator::Now();
    return stats;
}

Ptr<AnnotationManager>
SpectrumShadow::GetAnnotationManager() const
{
    return m_annotationMgr;
}

void
SpectrumShadow::ExportAnnotations(const std::string& filename)
{
    NS_LOG_FUNCTION(this << filename);

    std::string outputFile = filename.empty() ? m_config.annotationFile : filename;

    if (m_annotationMgr) {
        m_annotationMgr->WriteAnnotationsToJson(outputFile);
        NS_LOG_INFO("Annotations exported to: " << outputFile);
    }
}

} // namespace ns3
