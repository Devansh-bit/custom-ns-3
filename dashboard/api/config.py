import json
import os
import time
import copy
import random


# Default values for AP configuration
DEFAULT_TX_POWER = 20.0
DEFAULT_CCA_THRESHOLD = -82.0
DEFAULT_OBSSPD_THRESHOLD = -72.0

# 5GHz 20MHz channels for random assignment to APs
CHANNELS_5GHZ_20MHZ = [36, 40, 44, 48, 52, 56, 60, 64]

# Default values for STA waypoint configuration
DEFAULT_WAYPOINT_SWITCH_TIME_MIN = 5
DEFAULT_WAYPOINT_SWITCH_TIME_MAX = 15
DEFAULT_TRANSFER_VELOCITY_MIN = 0.5
DEFAULT_TRANSFER_VELOCITY_MAX = 2.0

# Default values for system configuration (matching config-simulation.json)
DEFAULT_SCANNING_CHANNELS = [1, 6, 11, 36, 40, 44, 48, 38, 46, 42, 52, 56, 60, 64, 54, 62, 58, 100, 104, 108, 112, 102, 110, 106, 116, 120, 124, 128, 118, 126, 122, 132, 136, 140, 144, 134, 142, 138, 149, 153, 157, 161, 151, 159, 155]
DEFAULT_BSS_ORCHESTRATION_RSSI_THRESHOLD = -62
DEFAULT_CHANNEL_HOP_DURATION_MS = 300

# ============================================================================
# DEFAULT INTERFERER CONFIGURATIONS (matching config-simulation.json format)
# ============================================================================

DEFAULT_MICROWAVE = {
    "_comment": "Microwave - Wide band interference at 2.45 GHz - affects 2.4 GHz channels",
    "type": "microwave",
    "position": {"x": 0.0, "y": 0.0, "z": 3.5},
    "txPowerDbm": 10.0,
    "bandwidthMHz": 100.0,
    "dutyCycle": 0.5,
    "startTime": 5.0,
    "startTimeJitter": 5.0,
    "active": True,
    "schedule": {"onDuration": 2.5, "offDuration": 0.5}
}

DEFAULT_BLUETOOTH = {
    "_comment": "Bluetooth - Frequency hopping across 2.4 GHz band - affects CH 1,6,11",
    "type": "bluetooth",
    "position": {"x": -3.0, "y": 3.0, "z": 3.7},
    "txPowerDbm": 8.0,
    "dutyCycle": 0.45,
    "hoppingSeed": 55,
    "useRandomSeed": True,
    "startTime": 2.0,
    "startTimeJitter": 3.0,
    "active": True,
    "schedule": {"onDuration": 2.0, "offDuration": 1.0}
}

DEFAULT_CORDLESS = {
    "_comment": "Cordless Phone - Narrowband hopping in 2.4 GHz - affects CH 1,6,11",
    "type": "cordless",
    "position": {"x": 3.0, "y": 3.0, "z": 3.7},
    "txPowerDbm": 12.0,
    "numHops": 100,
    "hopInterval": 0.01,
    "bandwidthMhz": 1.728,
    "hoppingSeed": 42,
    "useRandomSeed": True,
    "startTime": 3.0,
    "startTimeJitter": 4.0,
    "active": True,
    "schedule": {"onDuration": 3.0, "offDuration": 1.0}
}

DEFAULT_ZIGBEE = {
    "_comment": "ZigBee sensor network - overlaps with WiFi CH 1",
    "type": "zigbee",
    "position": {"x": -5.0, "y": -5.0, "z": 1.0},
    "txPowerDbm": 5.0,
    "zigbeeChannel": 11,
    "dutyCycle": 0.1,
    "startTime": 1.0,
    "startTimeJitter": 2.0,
    "active": True,
    "schedule": {"onDuration": 1.5, "offDuration": 0.5}
}

DEFAULT_RADAR = {
    "_comment": "DFS Radar - 5 GHz band pulsed interference - REQUIRED for FORCE_DFS - triggers ~once per minute",
    "type": "radar",
    "position": {"x": -20.0, "y": 0.0, "z": 10.0},
    "txPowerDbm": 25.0,
    "dfsChannel": 52,
    "startTime": 30.0,
    "startTimeJitter": 30.0,
    "active": True,
    "schedule": {"onDuration": 5.0, "offDuration": 55.0},
    "channelHopping": {
        "dfsChannels": [52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144],
        "hopIntervalSec": 3.0,
        "randomHopping": True
    },
    "widebandSpan": {"spanLength": 2, "maxSpanLength": 4, "randomSpan": True}
}

# Type mapping from frontend to ns-3
INTERFERER_TYPE_MAP = {
    "ble": "bluetooth",
    "bluetooth": "bluetooth",
    "zigbee": "zigbee",
    "microwave": "microwave",
    "cordless": "cordless",
    "radar": "radar"
}

# Default template for each interferer type
INTERFERER_DEFAULTS = {
    "bluetooth": DEFAULT_BLUETOOTH,
    "microwave": DEFAULT_MICROWAVE,
    "cordless": DEFAULT_CORDLESS,
    "zigbee": DEFAULT_ZIGBEE,
    "radar": DEFAULT_RADAR
}

# ============================================================================
# DEFAULT NS-3 CONFIGURATION SECTIONS
# ============================================================================

DEFAULT_INTERFERENCE = {
    "enabled": False,
    "position": {"x": 0.0, "y": 0.0, "z": 0.0},
    "numSources": 3,
    "startTime": 10.0,
    "centerFrequencyGHz": 5.2,
    "bandwidthMHz": 100.0,
    "powerPsdDbmHz": -50.0
}

DEFAULT_ACI = {
    "_comment": "Adjacent Channel Interference (ACI) analytical simulation",
    "enabled": True,
    "pathLossExponent": 3.0,
    "maxInterferenceDistanceM": 50.0,
    "clientWeightFactor": 0.1,
    "degradation": {
        "throughputFactor": 0.3,
        "packetLossFactor": 5.0,
        "latencyFactor": 0.5,
        "jitterFactor": 0.4,
        "channelUtilFactor": 0.15
    }
}

DEFAULT_OFDMA = {
    "_comment": "OFDMA effects simulation - applies QoE improvements based on STA count per AP",
    "enabled": True,
    "minStasForBenefit": 2,
    "saturationStaCount": 9,
    "improvement": {
        "throughputFactor": 0.35,
        "latencyFactor": 0.45,
        "jitterFactor": 0.50,
        "packetLossFactor": 0.20,
        "channelUtilFactor": 0.25
    }
}

DEFAULT_CHANNEL_SCORING = {
    "enabled": True,
    "weights": {
        "bssid": 0.1192,
        "rssi": 0.4875,
        "nonWifi": 0.3898,
        "overlap": 0.0036
    },
    "nonWifiDiscardThreshold": 65.0,
    "nonWifiTriggerThreshold": 65.0,
    "dfsBlacklistDurationSec": 20.0,
    "channelSwitchCooldownSec": 15.0
}

DEFAULT_POWER_SCORING = {
    "enabled": True,
    "updateInterval": 1.0,
    "t1IntervalSec": 10.0,
    "t2IntervalSec": 2.0,
    "margin": 3.0,
    "gamma": 0.7,
    "alpha": 0.3,
    "ofcThreshold": 500.0,
    "obsspdMinDbm": -82.0,
    "obsspdMaxDbm": -62.0,
    "txPowerRefDbm": 20.0,
    "txPowerMinDbm": 10.0,
    "nonWifiThresholdPercent": 50.0,
    "nonWifiHysteresis": 10.0,
    "rlPowerMarginDbm": 2.0
}

DEFAULT_STA_CHANNEL_HOPPING = {
    "_comment": "Client health monitoring and emergency reconnect for orphaned STAs",
    "enabled": True,
    "scanningDelaySec": 5.0,
    "minimumSnrDb": 0.0
}

DEFAULT_LOAD_BALANCING = {
    "enabled": False,
    "channelUtilThreshold": 70.0,
    "intervalSec": 10.0,
    "cooldownSec": 30.0
}

DEFAULT_CLIENT_SCHEDULE = {
    "_comment": "Time-based client scheduling - clients arrive/depart based on hour of day",
    "enabled": False,
    "simulationTimePerDay": 300,
    "startHour": 0,
    "parkingZone": {"x": 500.0, "y": 500.0, "z": 1.5},
    "transitionVelocity": 100.0,
    "arrivalJitterMaxSec": 2.0,
    "departureJitterMaxSec": 2.0,
    "schedule": [
        {"hourStart": 0, "hourEnd": 2, "activeClients": 2, "_comment": "Night: 12AM-6AM"},
        {"hourStart": 2, "hourEnd": 4, "activeClients": 4, "_comment": "Early morning: 6AM-8AM"},
        {"hourStart": 4, "hourEnd": 6, "activeClients": 15, "_comment": "Morning: 8AM-12PM"},
        {"hourStart": 6, "hourEnd": 14, "activeClients": 20, "_comment": "Midday: 12PM-2PM"},
        {"hourStart": 14, "hourEnd": 16, "activeClients": 30, "_comment": "Peak afternoon: 2PM-4PM"},
        {"hourStart": 16, "hourEnd": 18, "activeClients": 20, "_comment": "Late afternoon: 4PM-6PM"},
        {"hourStart": 18, "hourEnd": 22, "activeClients": 15, "_comment": "Evening: 6PM-10PM"},
        {"hourStart": 22, "hourEnd": 24, "activeClients": 8, "_comment": "Night: 10PM-12AM"}
    ]
}


def transform_interferences_to_virtual_interferers(interferences: list) -> dict:
    """
    Transform frontend 'interferences' format to ns-3 'virtualInterferers' format.

    Frontend format:
        [{"nodeId": 1, "type": "ble", "position": {x, y, z}, "power": 0}, ...]

    ns-3 format:
        {"enabled": true, "updateInterval": 0.1, "interferers": [...]}
    """
    if not interferences:
        # Return default virtualInterferers with radar always enabled for FORCE_DFS
        return {
            "enabled": True,
            "updateInterval": 0.1,
            "interferers": [copy.deepcopy(DEFAULT_RADAR)]
        }

    ns3_interferers = []
    has_radar = False

    for interference in interferences:
        frontend_type = interference.get("type", "")
        ns3_type = INTERFERER_TYPE_MAP.get(frontend_type, frontend_type)

        if ns3_type == "radar":
            has_radar = True

        # Get default template for this type
        template = INTERFERER_DEFAULTS.get(ns3_type)
        if not template:
            print(f"Warning: Unknown interferer type '{frontend_type}', skipping")
            continue

        # Create a copy of the template
        ns3_interferer = copy.deepcopy(template)

        # Override position if provided
        if "position" in interference:
            ns3_interferer["position"] = interference["position"]

        # Override power if provided and non-zero
        if "power" in interference and interference["power"] != 0:
            ns3_interferer["txPowerDbm"] = interference["power"]

        # Mark as active since user placed it
        ns3_interferer["active"] = True

        ns3_interferers.append(ns3_interferer)

    # ALWAYS add radar for FORCE_DFS support (if not already present)
    if not has_radar:
        ns3_interferers.append(copy.deepcopy(DEFAULT_RADAR))

    return {
        "enabled": True,
        "updateInterval": 0.1,
        "interferers": ns3_interferers
    }


def write_simulation_config(
    config: dict, file_path: str = "~/config/config-simulation.json"
):
    """
    Writes the simulation config to a JSON file, adding all missing fields
    and transforming frontend format to ns-3 format.

    Args:
        config (dict): The dictionary containing the configuration.
        file_path (str): The path to the output JSON file.
    """

    # Replace spaces with underscores in the filename part of file_path
    base_name = os.path.basename(file_path)
    dir_name = os.path.dirname(file_path)
    file_name_without_ext, ext = os.path.splitext(base_name)

    file_name_without_ext = file_name_without_ext.replace(" ", "_")
    file_path = os.path.join(dir_name, file_name_without_ext + ext)

    # Ensure the output directory exists
    output_dir = os.path.dirname(os.path.expanduser(file_path))
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # Basic simulation settings
    config.setdefault("clock-time", 12)
    config.setdefault("simulationTime", 86400)

    # AP configuration: ALWAYS assign random 5GHz 20MHz channels to each AP
    # Override any existing channel assignments to ensure 5GHz compliance
    for i in range(len(config.get("aps", []))):
        # Randomly select a 5GHz channel for each AP (duplicates are OK)
        random_channel = random.choice(CHANNELS_5GHZ_20MHZ)

        if "leverConfig" not in config["aps"][i]:
            config["aps"][i]["leverConfig"] = {
                "txPower": DEFAULT_TX_POWER,
                "channel": random_channel,
                "ccaThreshold": DEFAULT_CCA_THRESHOLD,
                "obsspdThreshold": DEFAULT_OBSSPD_THRESHOLD,
            }
        else:
            # ALWAYS override channel to ensure random 5GHz assignment
            config["aps"][i]["leverConfig"]["txPower"] = config["aps"][i]["leverConfig"].get("txPower", DEFAULT_TX_POWER)
            config["aps"][i]["leverConfig"]["channel"] = random_channel  # Force random 5GHz channel
            config["aps"][i]["leverConfig"]["ccaThreshold"] = config["aps"][i]["leverConfig"].get("ccaThreshold", DEFAULT_CCA_THRESHOLD)
            config["aps"][i]["leverConfig"]["obsspdThreshold"] = config["aps"][i]["leverConfig"].get("obsspdThreshold", DEFAULT_OBSSPD_THRESHOLD)

    # STA configuration defaults
    for i in range(len(config.get("stas", []))):
        config["stas"][i].setdefault(
            "waypointSwitchTimeMin", DEFAULT_WAYPOINT_SWITCH_TIME_MIN
        )
        config["stas"][i].setdefault(
            "waypointSwitchTimeMax", DEFAULT_WAYPOINT_SWITCH_TIME_MAX
        )
        config["stas"][i].setdefault(
            "transferVelocityMin", DEFAULT_TRANSFER_VELOCITY_MIN
        )
        config["stas"][i].setdefault(
            "transferVelocityMax", DEFAULT_TRANSFER_VELOCITY_MAX
        )

    # Transform frontend 'interferences' to ns-3 'virtualInterferers'
    if "interferences" in config:
        config["virtualInterferers"] = transform_interferences_to_virtual_interferers(
            config.pop("interferences")
        )
    elif "virtualInterferers" not in config:
        # Add default virtualInterferers with radar for FORCE_DFS
        config["virtualInterferers"] = transform_interferences_to_virtual_interferers([])

    # System configuration
    config.setdefault(
        "system_config",
        {
            "scanning_channels": DEFAULT_SCANNING_CHANNELS,
            "bss_orchestration_rssi_threshold": DEFAULT_BSS_ORCHESTRATION_RSSI_THRESHOLD,
            "channelHopDurationMs": DEFAULT_CHANNEL_HOP_DURATION_MS,
        },
    )

    # Old interference format (disabled by default)
    config.setdefault("interference", DEFAULT_INTERFERENCE)

    # ACI - Adjacent Channel Interference
    config.setdefault("aci", copy.deepcopy(DEFAULT_ACI))

    # OFDMA
    config.setdefault("ofdma", copy.deepcopy(DEFAULT_OFDMA))

    # Channel Scoring (for DFS and channel selection)
    config.setdefault("channelScoring", copy.deepcopy(DEFAULT_CHANNEL_SCORING))

    # Power Scoring
    config.setdefault("powerScoring", copy.deepcopy(DEFAULT_POWER_SCORING))

    # STA Channel Hopping (client reconnection)
    config.setdefault("staChannelHopping", copy.deepcopy(DEFAULT_STA_CHANNEL_HOPPING))

    # Load Balancing
    config.setdefault("loadBalancing", copy.deepcopy(DEFAULT_LOAD_BALANCING))

    # Client Schedule
    config.setdefault("clientSchedule", copy.deepcopy(DEFAULT_CLIENT_SCHEDULE))

    # Timestamp
    if "modified_at" not in config:
        config["modified_at"] = time.time()

    # Remove old channelScanning if present (replaced by channelScoring)
    config.pop("channelScanning", None)

    with open(os.path.expanduser(file_path), "w") as f:
        json.dump(config, f, indent=2)


def get_simulation_config(file_path: str = "~/config/config-simulation.json"):
    """
    Reads the simulation config from a JSON file.

    Args:
        file_path (str): The path to the input JSON file.

    Returns:
        dict: The configuration dictionary, or an empty dict if the file doesn't exist.
    """
    try:
        with open(os.path.expanduser(file_path), "r") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def get_user_config_list(config_dir: str = "~/config/") -> list[dict]:
    """
    Returns a list of user-created configurations with AP and STA counts.
    Sorted by most recently modified.

    Args:
        config_dir (str): The directory where config files are stored.

    Returns:
        list[dict]: A list of dictionaries, each containing 'name', 'node_count', and 'modified'.
    """
    expanded_config_dir = os.path.expanduser(config_dir)
    if not os.path.isdir(expanded_config_dir):
        return []

    configs_details = []
    for filename in os.listdir(expanded_config_dir):
        if filename.endswith(".json"):
            name = os.path.splitext(filename)[0]
            config_path = os.path.join(config_dir, filename)
            config = get_simulation_config(config_path)

            ap_count = len(config.get("aps", []))
            sta_count = len(config.get("stas", []))

            # Use modified timestamp
            modified_at = config.get("modified_at", 0)

            configs_details.append(
                {
                    # Display with spaces
                    "session_name": name.replace("_", " "),
                    "node_count": ap_count + sta_count,
                    "modified": modified_at,
                }
            )

    # Helper function to parse modified timestamp (handles both numeric and ISO format)
    def parse_modified_time(modified_value):
        if not modified_value:
            return 0
        try:
            # Try numeric first
            return float(modified_value)
        except (ValueError, TypeError):
            # Try ISO format
            try:
                from datetime import datetime
                dt = datetime.fromisoformat(modified_value.replace('Z', '+00:00'))
                return dt.timestamp()
            except:
                return 0

    # Sort by most recently modified (descending)
    configs_details.sort(key=lambda x: parse_modified_time(x.get("modified", 0)), reverse=True)

    return configs_details


def get_config_by_name(name: str):
    """
    Reads a specific simulation config by its name.

    Args:
        name (str): The name of the configuration (without .json extension).

    Returns:
        dict: The configuration dictionary, or an empty dict if the file doesn't exist.
    """
    name = name.replace(" ", "_")  # Ensure name matches the file naming convention
    file_path = f"~/config/{name}.json"
    return get_simulation_config(file_path)


def rename_config(old_name: str, new_name: str, config_dir: str = "~/config/"):
    """
    Renames a configuration file.

    Args:
        old_name (str): The current name of the configuration.
        new_name (str): The new name for the configuration.
        config_dir (str): The directory where config files are stored.
    """
    old_name_fs = old_name.replace(" ", "_")
    new_name_fs = new_name.replace(" ", "_")

    expanded_config_dir = os.path.expanduser(config_dir)

    old_path = os.path.join(expanded_config_dir, f"{old_name_fs}.json")
    new_path = os.path.join(expanded_config_dir, f"{new_name_fs}.json")

    if not os.path.exists(old_path):
        raise FileNotFoundError(f"Config '{old_name}' not found.")

    if os.path.exists(new_path):
        raise FileExistsError(f"Config '{new_name}' already exists.")

    os.rename(old_path, new_path)


def delete_config(name: str, config_dir: str = "~/config/"):
    """
    Deletes a configuration file.

    Args:
        name (str): The name of the configuration to delete.
        config_dir (str): The directory where config files are stored.
    """
    name_fs = name.replace(" ", "_")
    expanded_config_dir = os.path.expanduser(config_dir)
    file_path = os.path.join(expanded_config_dir, f"{name_fs}.json")

    if not os.path.exists(file_path):
        raise FileNotFoundError(f"Config '{name}' not found.")

    os.remove(file_path)
