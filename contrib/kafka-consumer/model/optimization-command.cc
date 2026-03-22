/*
 * Copyright (c) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "optimization-command.h"

#include "ns3/log.h"

#include "../external/json.hpp"

#include <stdexcept>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OptimizationCommand");

using json = nlohmann::json;

OptimizationCommand
ParseJsonCommand(const std::string& jsonStr)
{
    NS_LOG_FUNCTION(jsonStr);

    OptimizationCommand command;

    try
    {
        // Parse JSON string
        json j = json::parse(jsonStr);

        // Extract top-level fields
        command.timestampUnix = j.value("timestamp_unix", 0ULL);
        if (j.contains("timestamp")) {
            command.timestampUnix = j.value("timestamp", 0ULL);
        }
        
        command.simulationId = j.value("simulation_id", "");
        if (j.contains("simulationId")) {
            command.simulationId = j.value("simulationId", "");
        }
        
        // Support both "command_type" (old) and "command" (new) fields
        command.commandType = j.value("command_type", "");
        if (j.contains("command")) {
            command.commandType = j.value("command", "");
        }

        NS_LOG_INFO("Parsing command: type=" << command.commandType
                                             << " sim_id=" << command.simulationId
                                             << " timestamp=" << command.timestampUnix);

        // Extract ap_parameters map
        if (j.contains("ap_parameters") && j["ap_parameters"].is_object())
        {
            for (auto& [bssid, params] : j["ap_parameters"].items())
            {
                ApParameters apParams;

                apParams.txPowerStartDbm = params.value("tx_power_start_dbm", 16.0);
                apParams.txPowerEndDbm = params.value("tx_power_end_dbm", 16.0);
                apParams.ccaEdThresholdDbm = params.value("cca_ed_threshold_dbm", -82.0);
                apParams.obssPd = params.value("obss_pd", -82.0);
                apParams.rxSensitivityDbm = params.value("rx_sensitivity_dbm", -93.0);
                apParams.channelNumber = params.value("channel_number", 36);
                apParams.channelWidthMhz = params.value("channel_width_mhz", 80);
                apParams.band = params.value("band", "BAND_5GHZ");
                apParams.primary20Index = params.value("primary_20_index", 0);

                command.apParameters[bssid] = apParams;

                NS_LOG_INFO("  AP " << bssid << ": power=" << apParams.txPowerStartDbm
                                    << " channel=" << +apParams.channelNumber
                                    << " width=" << apParams.channelWidthMhz
                                    << " band=" << apParams.band);
            }
        }
        else
        {
            NS_LOG_WARN("No ap_parameters found in JSON");
        }

        NS_LOG_INFO("Parsed " << command.apParameters.size() << " AP parameter sets");
    }
    catch (const json::parse_error& e)
    {
        NS_LOG_ERROR("JSON parse error: " << e.what());
        throw std::runtime_error("Failed to parse JSON: " + std::string(e.what()));
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Error parsing command: " << e.what());
        throw;
    }

    return command;
}

} // namespace ns3