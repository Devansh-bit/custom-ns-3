#include "simulation-config-parser.h"

#include "ns3/log.h"
#include "ns3/fatal-error.h"

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/error/en.h>

#include <cstdio>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("SimulationConfigParser");

using namespace rapidjson;

Vector
SimulationConfigParser::ParsePosition(const void* posObjPtr)
{
    const Value& posObj = *static_cast<const Value*>(posObjPtr);

    NS_ASSERT(posObj.HasMember("x") && posObj.HasMember("y") && posObj.HasMember("z"));

    double x = posObj["x"].GetDouble();
    double y = posObj["y"].GetDouble();
    double z = posObj["z"].GetDouble();

    return Vector(x, y, z);
}

SimulationConfigData
SimulationConfigParser::ParseFile(const std::string& filename)
{
    NS_LOG_FUNCTION(filename);

    SimulationConfigData config;
    config.bssOrchestrationRssiThreshold = -70.0;  // Default RSSI threshold

    // Open file
    FILE* fp = fopen(filename.c_str(), "r");
    if (!fp)
    {
        NS_FATAL_ERROR("Failed to open config file: " << filename);
    }

    // Read file into buffer
    char readBuffer[65536];
    FileReadStream is(fp, readBuffer, sizeof(readBuffer));

    // Parse JSON
    Document doc;
    doc.ParseStream(is);
    fclose(fp);

    if (doc.HasParseError())
    {
        NS_FATAL_ERROR("JSON parse error: " << GetParseError_En(doc.GetParseError())
                       << " at offset " << doc.GetErrorOffset());
    }

    // Parse simulation time
    NS_ASSERT_MSG(doc.HasMember("simulationTime"), "Missing 'simulationTime' in config");
    config.simulationTime = doc["simulationTime"].GetDouble();

    // Parse APs
    NS_ASSERT_MSG(doc.HasMember("aps"), "Missing 'aps' array in config");
    const Value& apsArray = doc["aps"];
    NS_ASSERT(apsArray.IsArray());

    for (SizeType i = 0; i < apsArray.Size(); i++)
    {
        const Value& ap = apsArray[i];

        ApConfigData apConfig;
        apConfig.nodeId = ap["nodeId"].GetUint();
        apConfig.position = ParsePosition(&ap["position"]);

        const Value& leverConfig = ap["leverConfig"];
        apConfig.txPower = leverConfig["txPower"].GetDouble();
        apConfig.channel = static_cast<uint8_t>(leverConfig["channel"].GetUint());
        apConfig.ccaThreshold = leverConfig["ccaThreshold"].GetDouble();
        apConfig.rxSensitivity = leverConfig["obsspdThreshold"].GetDouble();

        config.aps.push_back(apConfig);

        NS_LOG_INFO("[SimulationConfigParser] Parsed AP " << apConfig.nodeId
                    << ": pos=" << apConfig.position
                    << ", channel=" << (int)apConfig.channel
                    << ", txPower=" << apConfig.txPower);
    }

    // Parse Waypoints
    NS_ASSERT_MSG(doc.HasMember("waypoints"), "Missing 'waypoints' array in config");
    const Value& waypointsArray = doc["waypoints"];
    NS_ASSERT(waypointsArray.IsArray());

    for (SizeType i = 0; i < waypointsArray.Size(); i++)
    {
        const Value& wp = waypointsArray[i];

        WaypointData waypoint;
        waypoint.id = wp["id"].GetUint();
        waypoint.position.x = wp["x"].GetDouble();
        waypoint.position.y = wp["y"].GetDouble();
        waypoint.position.z = wp["z"].GetDouble();

        config.waypoints.push_back(waypoint);
    }

    NS_LOG_INFO("[SimulationConfigParser] Parsed " << config.waypoints.size() << " waypoints");

    // Parse STAs
    NS_ASSERT_MSG(doc.HasMember("stas"), "Missing 'stas' array in config");
    const Value& stasArray = doc["stas"];
    NS_ASSERT(stasArray.IsArray());

    for (SizeType i = 0; i < stasArray.Size(); i++)
    {
        const Value& sta = stasArray[i];

        StaMobilityConfig staConfig;
        staConfig.nodeId = sta["nodeId"].GetUint();
        staConfig.initialWaypointId = sta["initialWaypointId"].GetUint();
        staConfig.waypointSwitchTimeMin = sta["waypointSwitchTimeMin"].GetDouble();
        staConfig.waypointSwitchTimeMax = sta["waypointSwitchTimeMax"].GetDouble();
        staConfig.transferVelocityMin = sta["transferVelocityMin"].GetDouble();
        staConfig.transferVelocityMax = sta["transferVelocityMax"].GetDouble();

        config.stas.push_back(staConfig);

        NS_LOG_INFO("[SimulationConfigParser] Parsed STA " << staConfig.nodeId
                    << ": initialWaypoint=" << staConfig.initialWaypointId
                    << ", switchTime=[" << staConfig.waypointSwitchTimeMin
                    << "," << staConfig.waypointSwitchTimeMax << "]s");
    }

    // Parse system_config (optional)
    if (doc.HasMember("system_config"))
    {
        const Value& systemConfig = doc["system_config"];

        // Parse scanning_channels (optional)
        if (systemConfig.HasMember("scanning_channels"))
        {
            const Value& channelsArray = systemConfig["scanning_channels"];
            NS_ASSERT(channelsArray.IsArray());

            for (SizeType i = 0; i < channelsArray.Size(); i++)
            {
                uint8_t channel = static_cast<uint8_t>(channelsArray[i].GetUint());
                config.scanningChannels.push_back(channel);
            }

            NS_LOG_INFO("[SimulationConfigParser] Parsed " << config.scanningChannels.size()
                        << " scanning channels");
        }

        // Parse channel_hop_duration_ms (optional, default 300)
        if (systemConfig.HasMember("channel_hop_duration_ms"))
        {
            config.channelHopDurationMs = systemConfig["channel_hop_duration_ms"].GetDouble();
            NS_LOG_INFO("[SimulationConfigParser] Parsed channel hop duration: "
                        << config.channelHopDurationMs << " ms");
        }
        else
        {
            config.channelHopDurationMs = 300.0; // Default value
        }

        if (systemConfig.HasMember("bss_orchestration_rssi_threshold"))


        {
            config.bssOrchestrationRssiThreshold = systemConfig["bss_orchestration_rssi_threshold"].GetDouble();
            NS_LOG_INFO("[SimulationConfigParser] Parsed BSS orchestration RSSI threshold: "
                        << config.bssOrchestrationRssiThreshold << " dBm");
        }
    }

    // Parse interference config (optional)
    if (doc.HasMember("interference"))
    {
        const Value& interferenceConfig = doc["interference"];

        config.interference.enabled = interferenceConfig["enabled"].GetBool();

        if (interferenceConfig.HasMember("position"))
        {
            config.interference.position = ParsePosition(&interferenceConfig["position"]);
        }
        else
        {
            config.interference.position = Vector(0, 0, 0);  // Default to origin
        }

        config.interference.numSources = interferenceConfig.HasMember("numSources")
            ? interferenceConfig["numSources"].GetUint() : 3;
        config.interference.startTime = interferenceConfig.HasMember("startTime")
            ? interferenceConfig["startTime"].GetDouble() : 10.0;
        config.interference.centerFrequencyGHz = interferenceConfig.HasMember("centerFrequencyGHz")
            ? interferenceConfig["centerFrequencyGHz"].GetDouble() : 5.2;
        config.interference.bandwidthMHz = interferenceConfig.HasMember("bandwidthMHz")
            ? interferenceConfig["bandwidthMHz"].GetDouble() : 100.0;
        config.interference.powerPsdDbmHz = interferenceConfig.HasMember("powerPsdDbmHz")
            ? interferenceConfig["powerPsdDbmHz"].GetDouble() : -50.0;

        NS_LOG_INFO("[SimulationConfigParser] Parsed interference config: "
                    << "enabled=" << config.interference.enabled
                    << ", position=" << config.interference.position
                    << ", numSources=" << config.interference.numSources
                    << ", centerFreq=" << config.interference.centerFrequencyGHz << " GHz");
    }
    else
    {
        // Set defaults if interference section is missing
        config.interference.enabled = false;
        config.interference.position = Vector(0, 0, 0);
        config.interference.numSources = 3;
        config.interference.startTime = 10.0;
        config.interference.centerFrequencyGHz = 5.2;
        config.interference.bandwidthMHz = 100.0;
        config.interference.powerPsdDbmHz = -50.0;
    }

    // Parse ACI (Adjacent Channel Interference) config (optional)
    if (doc.HasMember("aci"))
    {
        const Value& aciConfig = doc["aci"];

        config.aci.enabled = aciConfig.HasMember("enabled")
            ? aciConfig["enabled"].GetBool() : false;
        config.aci.pathLossExponent = aciConfig.HasMember("pathLossExponent")
            ? aciConfig["pathLossExponent"].GetDouble() : 3.0;
        config.aci.maxInterferenceDistanceM = aciConfig.HasMember("maxInterferenceDistanceM")
            ? aciConfig["maxInterferenceDistanceM"].GetDouble() : 50.0;
        config.aci.clientWeightFactor = aciConfig.HasMember("clientWeightFactor")
            ? aciConfig["clientWeightFactor"].GetDouble() : 0.1;

        // Parse degradation factors (optional, with defaults)
        if (aciConfig.HasMember("degradation"))
        {
            const Value& degradation = aciConfig["degradation"];
            config.aci.degradation.throughputFactor = degradation.HasMember("throughputFactor")
                ? degradation["throughputFactor"].GetDouble() : 0.3;
            config.aci.degradation.packetLossFactor = degradation.HasMember("packetLossFactor")
                ? degradation["packetLossFactor"].GetDouble() : 5.0;
            config.aci.degradation.latencyFactor = degradation.HasMember("latencyFactor")
                ? degradation["latencyFactor"].GetDouble() : 0.5;
            config.aci.degradation.jitterFactor = degradation.HasMember("jitterFactor")
                ? degradation["jitterFactor"].GetDouble() : 0.4;
            config.aci.degradation.channelUtilFactor = degradation.HasMember("channelUtilFactor")
                ? degradation["channelUtilFactor"].GetDouble() : 0.15;
        }

        NS_LOG_INFO("[SimulationConfigParser] Parsed ACI config: "
                    << "enabled=" << config.aci.enabled
                    << ", pathLossExp=" << config.aci.pathLossExponent
                    << ", maxDist=" << config.aci.maxInterferenceDistanceM << "m"
                    << ", clientWeight=" << config.aci.clientWeightFactor);
    }
    else
    {
        // Set defaults if aci section is missing
        config.aci.enabled = false;
        config.aci.pathLossExponent = 3.0;
        config.aci.maxInterferenceDistanceM = 50.0;
        config.aci.clientWeightFactor = 0.1;
        // degradation uses default values from struct
    }

    // Parse OFDMA (OFDMA effects simulation) config (optional)
    if (doc.HasMember("ofdma"))
    {
        const Value& ofdmaConfig = doc["ofdma"];

        config.ofdma.enabled = ofdmaConfig.HasMember("enabled")
            ? ofdmaConfig["enabled"].GetBool() : false;
        config.ofdma.minStasForBenefit = ofdmaConfig.HasMember("minStasForBenefit")
            ? static_cast<uint8_t>(ofdmaConfig["minStasForBenefit"].GetUint()) : 2;
        config.ofdma.saturationStaCount = ofdmaConfig.HasMember("saturationStaCount")
            ? static_cast<uint8_t>(ofdmaConfig["saturationStaCount"].GetUint()) : 9;

        // Parse improvement factors (optional, with defaults)
        if (ofdmaConfig.HasMember("improvement"))
        {
            const Value& improvement = ofdmaConfig["improvement"];
            config.ofdma.improvement.throughputFactor = improvement.HasMember("throughputFactor")
                ? improvement["throughputFactor"].GetDouble() : 0.35;
            config.ofdma.improvement.latencyFactor = improvement.HasMember("latencyFactor")
                ? improvement["latencyFactor"].GetDouble() : 0.45;
            config.ofdma.improvement.jitterFactor = improvement.HasMember("jitterFactor")
                ? improvement["jitterFactor"].GetDouble() : 0.50;
            config.ofdma.improvement.packetLossFactor = improvement.HasMember("packetLossFactor")
                ? improvement["packetLossFactor"].GetDouble() : 0.20;
            config.ofdma.improvement.channelUtilFactor = improvement.HasMember("channelUtilFactor")
                ? improvement["channelUtilFactor"].GetDouble() : 0.25;
        }

        NS_LOG_INFO("[SimulationConfigParser] Parsed OFDMA config: "
                    << "enabled=" << config.ofdma.enabled
                    << ", minSTAs=" << +config.ofdma.minStasForBenefit
                    << ", saturationSTAs=" << +config.ofdma.saturationStaCount);
    }
    else
    {
        // Set defaults if ofdma section is missing
        config.ofdma.enabled = false;
        config.ofdma.minStasForBenefit = 2;
        config.ofdma.saturationStaCount = 9;
        // improvement uses default values from struct
    }

    // Parse channelScoring config (optional)
    if (doc.HasMember("channelScoring"))
    {
        const Value& scoringConfig = doc["channelScoring"];

        config.channelScoring.enabled = scoringConfig.HasMember("enabled")
            ? scoringConfig["enabled"].GetBool() : true;

        if (scoringConfig.HasMember("weights"))
        {
            const Value& weights = scoringConfig["weights"];
            config.channelScoring.weightBssid = weights.HasMember("bssid")
                ? weights["bssid"].GetDouble() : 0.5;
            config.channelScoring.weightRssi = weights.HasMember("rssi")
                ? weights["rssi"].GetDouble() : 0.5;
            config.channelScoring.weightNonWifi = weights.HasMember("nonWifi")
                ? weights["nonWifi"].GetDouble() : 0.5;
            config.channelScoring.weightOverlap = weights.HasMember("overlap")
                ? weights["overlap"].GetDouble() : 0.5;
        }

        config.channelScoring.nonWifiDiscardThreshold = scoringConfig.HasMember("nonWifiDiscardThreshold")
            ? scoringConfig["nonWifiDiscardThreshold"].GetDouble() : 40.0;

        NS_LOG_INFO("[SimulationConfigParser] Parsed channel scoring config: "
                    << "enabled=" << config.channelScoring.enabled
                    << ", weights=[bssid=" << config.channelScoring.weightBssid
                    << ", rssi=" << config.channelScoring.weightRssi
                    << ", nonWifi=" << config.channelScoring.weightNonWifi
                    << ", overlap=" << config.channelScoring.weightOverlap << "]"
                    << ", nonWifiThreshold=" << config.channelScoring.nonWifiDiscardThreshold << "%");
    }
    else
    {
        // Set defaults if channelScoring section is missing
        config.channelScoring.enabled = true;
        config.channelScoring.weightBssid = 0.5;
        config.channelScoring.weightRssi = 0.5;
        config.channelScoring.weightNonWifi = 0.5;
        config.channelScoring.weightOverlap = 0.5;
        config.channelScoring.nonWifiDiscardThreshold = 40.0;
    }

    // Parse virtualInterferers config (optional)
    if (doc.HasMember("virtualInterferers"))
    {
        const Value& viConfig = doc["virtualInterferers"];

        config.virtualInterferers.enabled = viConfig.HasMember("enabled")
            ? viConfig["enabled"].GetBool() : false;
        config.virtualInterferers.updateInterval = viConfig.HasMember("updateInterval")
            ? viConfig["updateInterval"].GetDouble() : 0.1;

        if (viConfig.HasMember("interferers") && viConfig["interferers"].IsArray())
        {
            const Value& interferers = viConfig["interferers"];

            for (SizeType i = 0; i < interferers.Size(); i++)
            {
                const Value& intf = interferers[i];
                std::string type = intf["type"].GetString();

                if (type == "microwave")
                {
                    MicrowaveInterfererConfigData mw;
                    mw.position = ParsePosition(&intf["position"]);
                    mw.txPowerDbm = intf["txPowerDbm"].GetDouble();
                    mw.centerFrequencyGHz = intf["centerFrequencyGHz"].GetDouble();
                    mw.bandwidthMHz = intf["bandwidthMHz"].GetDouble();
                    mw.dutyCycle = intf["dutyCycle"].GetDouble();
                    mw.startTime = intf["startTime"].GetDouble();
                    mw.active = intf["active"].GetBool();

                    // Parse schedule if present
                    if (intf.HasMember("schedule"))
                    {
                        const Value& schedule = intf["schedule"];
                        mw.schedule.onDuration = schedule["onDuration"].GetDouble();
                        mw.schedule.offDuration = schedule["offDuration"].GetDouble();
                        mw.schedule.hasSchedule = true;
                    }

                    config.virtualInterferers.microwaves.push_back(mw);

                    NS_LOG_INFO("[SimulationConfigParser] Parsed microwave interferer: "
                                << "pos=" << mw.position
                                << ", txPower=" << mw.txPowerDbm << " dBm"
                                << ", centerFreq=" << mw.centerFrequencyGHz << " GHz"
                                << (mw.schedule.hasSchedule ? ", schedule=" + std::to_string(mw.schedule.onDuration) + "s ON / " + std::to_string(mw.schedule.offDuration) + "s OFF" : ""));
                }
                else if (type == "bluetooth")
                {
                    BluetoothInterfererConfigData bt;
                    bt.position = ParsePosition(&intf["position"]);
                    bt.txPowerDbm = intf["txPowerDbm"].GetDouble();
                    bt.hoppingSeed = static_cast<uint8_t>(intf["hoppingSeed"].GetUint());
                    bt.hopInterval = intf["hopInterval"].GetDouble();
                    bt.profile = intf["profile"].GetString();
                    bt.startTime = intf["startTime"].GetDouble();
                    bt.active = intf["active"].GetBool();

                    // Parse schedule if present
                    if (intf.HasMember("schedule"))
                    {
                        const Value& schedule = intf["schedule"];
                        bt.schedule.onDuration = schedule["onDuration"].GetDouble();
                        bt.schedule.offDuration = schedule["offDuration"].GetDouble();
                        bt.schedule.hasSchedule = true;
                    }

                    config.virtualInterferers.bluetooths.push_back(bt);

                    NS_LOG_INFO("[SimulationConfigParser] Parsed Bluetooth interferer: "
                                << "pos=" << bt.position
                                << ", txPower=" << bt.txPowerDbm << " dBm"
                                << ", profile=" << bt.profile
                                << (bt.schedule.hasSchedule ? ", schedule=" + std::to_string(bt.schedule.onDuration) + "s ON / " + std::to_string(bt.schedule.offDuration) + "s OFF" : ""));
                }
                else if (type == "cordless")
                {
                    CordlessInterfererConfigData cordless;
                    cordless.position = ParsePosition(&intf["position"]);
                    cordless.txPowerDbm = intf["txPowerDbm"].GetDouble();
                    cordless.numHops = intf["numHops"].GetUint();
                    cordless.hopInterval = intf["hopInterval"].GetDouble();
                    cordless.bandwidthMhz = intf["bandwidthMhz"].GetDouble();
                    cordless.hoppingSeed = static_cast<uint8_t>(intf["hoppingSeed"].GetUint());
                    cordless.startTime = intf["startTime"].GetDouble();
                    cordless.active = intf["active"].GetBool();

                    // Parse schedule if present
                    if (intf.HasMember("schedule"))
                    {
                        const Value& schedule = intf["schedule"];
                        cordless.schedule.onDuration = schedule["onDuration"].GetDouble();
                        cordless.schedule.offDuration = schedule["offDuration"].GetDouble();
                        cordless.schedule.hasSchedule = true;
                    }

                    config.virtualInterferers.cordless.push_back(cordless);

                    NS_LOG_INFO("[SimulationConfigParser] Parsed Cordless phone interferer: "
                                << "pos=" << cordless.position
                                << ", txPower=" << cordless.txPowerDbm << " dBm"
                                << ", numHops=" << cordless.numHops
                                << ", hopInterval=" << cordless.hopInterval << "s"
                                << ", bandwidth=" << cordless.bandwidthMhz << " MHz"
                                << (cordless.schedule.hasSchedule ? ", schedule=" + std::to_string(cordless.schedule.onDuration) + "s ON / " + std::to_string(cordless.schedule.offDuration) + "s OFF" : ""));
                }
                else if (type == "zigbee")
                {
                    ZigbeeInterfererConfigData zb;
                    zb.position = ParsePosition(&intf["position"]);
                    zb.txPowerDbm = intf["txPowerDbm"].GetDouble();
                    zb.zigbeeChannel = static_cast<uint8_t>(intf["zigbeeChannel"].GetUint());
                    zb.bandwidthMHz = intf["bandwidthMHz"].GetDouble();
                    zb.networkType = intf["networkType"].GetString();
                    zb.dutyCycle = intf["dutyCycle"].GetDouble();
                    zb.startTime = intf["startTime"].GetDouble();
                    zb.active = intf["active"].GetBool();

                    // Parse schedule if present
                    if (intf.HasMember("schedule"))
                    {
                        const Value& schedule = intf["schedule"];
                        zb.schedule.onDuration = schedule["onDuration"].GetDouble();
                        zb.schedule.offDuration = schedule["offDuration"].GetDouble();
                        zb.schedule.hasSchedule = true;
                    }

                    config.virtualInterferers.zigbees.push_back(zb);

                    NS_LOG_INFO("[SimulationConfigParser] Parsed ZigBee interferer: "
                                << "pos=" << zb.position
                                << ", channel=" << (int)zb.zigbeeChannel
                                << ", networkType=" << zb.networkType
                                << (zb.schedule.hasSchedule ? ", schedule=" + std::to_string(zb.schedule.onDuration) + "s ON / " + std::to_string(zb.schedule.offDuration) + "s OFF" : ""));
                }
                else if (type == "radar")
                {
                    RadarInterfererConfigData radar;
                    radar.position = ParsePosition(&intf["position"]);
                    radar.txPowerDbm = intf["txPowerDbm"].GetDouble();
                    radar.centerFrequencyGHz = intf["centerFrequencyGHz"].GetDouble();
                    radar.dfsChannel = static_cast<uint8_t>(intf["dfsChannel"].GetUint());
                    radar.pulseDuration = intf["pulseDuration"].GetDouble();
                    radar.pulseInterval = intf["pulseInterval"].GetDouble();
                    radar.radarType = intf["radarType"].GetString();
                    radar.startTime = intf["startTime"].GetDouble();
                    radar.active = intf["active"].GetBool();

                    // Parse schedule if present
                    if (intf.HasMember("schedule"))
                    {
                        const Value& schedule = intf["schedule"];
                        radar.schedule.onDuration = schedule["onDuration"].GetDouble();
                        radar.schedule.offDuration = schedule["offDuration"].GetDouble();
                        radar.schedule.hasSchedule = true;
                    }

                    // Parse channel hopping configuration
                    radar.hopIntervalSec = 10.0;  // Default 10s
                    radar.randomHopping = true;   // Default random
                    if (intf.HasMember("channelHopping"))
                    {
                        const Value& hopping = intf["channelHopping"];
                        if (hopping.HasMember("dfsChannels") && hopping["dfsChannels"].IsArray())
                        {
                            const Value& channels = hopping["dfsChannels"];
                            for (SizeType i = 0; i < channels.Size(); i++)
                            {
                                radar.dfsChannels.push_back(static_cast<uint8_t>(channels[i].GetUint()));
                            }
                        }
                        if (hopping.HasMember("hopIntervalSec"))
                        {
                            radar.hopIntervalSec = hopping["hopIntervalSec"].GetDouble();
                        }
                        if (hopping.HasMember("randomHopping"))
                        {
                            radar.randomHopping = hopping["randomHopping"].GetBool();
                        }
                    }

                    // Parse wideband span configuration
                    radar.spanLength = 2;      // Default ±2 channels (5 channels = 100MHz)
                    radar.maxSpanLength = 4;   // Default max ±4 channels
                    radar.randomSpan = true;   // Default randomize span
                    if (intf.HasMember("widebandSpan"))
                    {
                        const Value& span = intf["widebandSpan"];
                        if (span.HasMember("spanLength"))
                        {
                            radar.spanLength = static_cast<uint8_t>(span["spanLength"].GetUint());
                        }
                        if (span.HasMember("maxSpanLength"))
                        {
                            radar.maxSpanLength = static_cast<uint8_t>(span["maxSpanLength"].GetUint());
                        }
                        if (span.HasMember("randomSpan"))
                        {
                            radar.randomSpan = span["randomSpan"].GetBool();
                        }
                    }

                    config.virtualInterferers.radars.push_back(radar);

                    NS_LOG_INFO("[SimulationConfigParser] Parsed Radar interferer: "
                                << "pos=" << radar.position
                                << ", txPower=" << radar.txPowerDbm << " dBm"
                                << ", dfsChannel=" << (int)radar.dfsChannel
                                << ", type=" << radar.radarType
                                << ", hopping=" << (radar.dfsChannels.empty() ? "disabled" : "enabled (" + std::to_string(radar.dfsChannels.size()) + " channels)")
                                << (radar.schedule.hasSchedule ? ", schedule=" + std::to_string(radar.schedule.onDuration) + "s ON / " + std::to_string(radar.schedule.offDuration) + "s OFF" : ""));
                }
            }
        }

        NS_LOG_INFO("[SimulationConfigParser] Parsed virtual interferers: "
                    << "enabled=" << config.virtualInterferers.enabled
                    << ", microwave=" << config.virtualInterferers.microwaves.size()
                    << ", bluetooth=" << config.virtualInterferers.bluetooths.size()
                    << ", cordless=" << config.virtualInterferers.cordless.size()
                    << ", zigbee=" << config.virtualInterferers.zigbees.size()
                    << ", radar=" << config.virtualInterferers.radars.size());
    }
    else
    {
        // Set defaults if virtualInterferers section is missing
        config.virtualInterferers.enabled = false;
        config.virtualInterferers.updateInterval = 0.1;
    }

    NS_LOG_INFO("[SimulationConfigParser] Configuration loaded: "
                << config.aps.size() << " APs, "
                << config.waypoints.size() << " waypoints, "
                << config.stas.size() << " STAs, "
                << config.scanningChannels.size() << " scanning channels, "
                << "simTime=" << config.simulationTime << "s");

    return config;
}

} // namespace ns3
