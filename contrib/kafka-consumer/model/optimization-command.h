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

#ifndef OPTIMIZATION_COMMAND_H
#define OPTIMIZATION_COMMAND_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ns3
{

/**
 * \brief WiFi AP parameters for optimization
 *
 * This structure holds the WiFi PHY/MAC parameters that can be adjusted
 * by the Bayesian Optimizer through Kafka messages.
 */
struct ApParameters
{
    double txPowerStartDbm;       ///< Minimum transmission power (dBm)
    double txPowerEndDbm;         ///< Maximum transmission power (dBm)
    double ccaEdThresholdDbm;     ///< CCA Energy Detection threshold (dBm)
    double obssPd;                ///< OBSS_PD threshold (dBm)
    double rxSensitivityDbm;      ///< Receiver sensitivity (dBm)
    uint8_t channelNumber;        ///< WiFi channel number (e.g., 36, 40, 52)
    uint16_t channelWidthMhz;     ///< Channel width in MHz (20, 40, 80, 160)
    std::string band;             ///< Band type: "BAND_2_4GHZ", "BAND_5GHZ", "BAND_6GHZ"
    uint8_t primary20Index;       ///< Primary 20MHz channel index

    /**
     * \brief Default constructor with typical 5GHz AP parameters
     */
    ApParameters()
        : txPowerStartDbm(16.0),
          txPowerEndDbm(16.0),
          ccaEdThresholdDbm(-82.0),
          obssPd(-82.0),
          rxSensitivityDbm(-93.0),
          channelNumber(36),
          channelWidthMhz(80),
          band("BAND_5GHZ"),
          primary20Index(0)
    {
    }
};

/**
 * \brief Optimization command from Bayesian Optimizer
 *
 * This structure represents a complete optimization command received from
 * Kafka, containing parameters for one or more APs.
 */
struct OptimizationCommand
{
    uint64_t timestampUnix;                            ///< Unix timestamp
    std::string simulationId;                          ///< Simulation identifier
    std::string commandType;                           ///< Command type (e.g., "UPDATE_AP_PARAMETERS")
    std::map<std::string, ApParameters> apParameters;  ///< BSSID -> parameters map

    /**
     * \brief Default constructor
     */
    OptimizationCommand()
        : timestampUnix(0),
          simulationId(""),
          commandType("")
    {
    }
};

/**
 * \brief Parse JSON string into OptimizationCommand
 *
 * Expected JSON format:
 * {
 *   "timestamp_unix": 1730739615,
 *   "simulation_id": "sim-001",
 *   "command_type": "UPDATE_AP_PARAMETERS",
 *   "ap_parameters": {
 *     "00:00:00:00:00:01": {
 *       "tx_power_start_dbm": 18.0,
 *       "tx_power_end_dbm": 18.0,
 *       "cca_ed_threshold_dbm": -80.0,
 *       "rx_sensitivity_dbm": -91.0,
 *       "channel_number": 36,
 *       "channel_width_mhz": 80,
 *       "band": "BAND_5GHZ",
 *       "primary_20_index": 0
 *     }
 *   }
 * }
 *
 * \param json JSON string from Kafka message
 * \return Parsed OptimizationCommand structure
 */
OptimizationCommand ParseJsonCommand(const std::string& json);

} // namespace ns3

#endif /* OPTIMIZATION_COMMAND_H */