"""
Command models for optimization-commands Kafka topic.

These models match the JSON format expected by kafka-consumer.cc.
"""

import time
from dataclasses import dataclass, field
from typing import Any, Dict, Optional


@dataclass
class ApParameters:
    """WiFi AP parameters for optimization commands."""
    tx_power_start_dbm: float = 16.0
    tx_power_end_dbm: float = 16.0
    cca_ed_threshold_dbm: float = -82.0
    obss_pd: float = -82.0
    rx_sensitivity_dbm: float = -93.0
    channel_number: int = 36
    channel_width_mhz: int = 80
    band: str = "BAND_5GHZ"
    primary_20_index: int = 0

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {
            "tx_power_start_dbm": self.tx_power_start_dbm,
            "tx_power_end_dbm": self.tx_power_end_dbm,
            "cca_ed_threshold_dbm": self.cca_ed_threshold_dbm,
            "obss_pd": self.obss_pd,
            "rx_sensitivity_dbm": self.rx_sensitivity_dbm,
            "channel_number": self.channel_number,
            "channel_width_mhz": self.channel_width_mhz,
            "band": self.band,
            "primary_20_index": self.primary_20_index,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "ApParameters":
        """Create from dictionary."""
        return cls(
            tx_power_start_dbm=float(data.get("tx_power_start_dbm", 16.0)),
            tx_power_end_dbm=float(data.get("tx_power_end_dbm", 16.0)),
            cca_ed_threshold_dbm=float(data.get("cca_ed_threshold_dbm", -82.0)),
            obss_pd=float(data.get("obss_pd", -82.0)),
            rx_sensitivity_dbm=float(data.get("rx_sensitivity_dbm", -93.0)),
            channel_number=int(data.get("channel_number", 36)),
            channel_width_mhz=int(data.get("channel_width_mhz", 80)),
            band=str(data.get("band", "BAND_5GHZ")),
            primary_20_index=int(data.get("primary_20_index", 0)),
        )

    @classmethod
    def with_channel(cls, channel: int, **kwargs) -> "ApParameters":
        """Create parameters with a specific channel.

        Args:
            channel: Channel number to set
            **kwargs: Additional parameters to override defaults
        """
        return cls(channel_number=channel, **kwargs)

    @classmethod
    def with_power(cls, power_dbm: float, **kwargs) -> "ApParameters":
        """Create parameters with specific TX power.

        Args:
            power_dbm: TX power in dBm
            **kwargs: Additional parameters to override defaults
        """
        return cls(
            tx_power_start_dbm=power_dbm,
            tx_power_end_dbm=power_dbm,
            **kwargs
        )


@dataclass
class OptimizationCommand:
    """Complete optimization command to send to simulator."""
    simulation_id: str = ""
    command_type: str = "UPDATE_AP_PARAMETERS"
    ap_parameters: Dict[str, ApParameters] = field(default_factory=dict)
    timestamp_unix: int = field(default_factory=lambda: int(time.time()))

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {
            "timestamp_unix": self.timestamp_unix,
            "simulation_id": self.simulation_id,
            "command_type": self.command_type,
            "ap_parameters": {
                bssid: params.to_dict()
                for bssid, params in self.ap_parameters.items()
            },
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "OptimizationCommand":
        """Create from dictionary."""
        ap_parameters = {
            bssid: ApParameters.from_dict(params)
            for bssid, params in data.get("ap_parameters", {}).items()
        }
        return cls(
            timestamp_unix=int(data.get("timestamp_unix", int(time.time()))),
            simulation_id=str(data.get("simulation_id", "")),
            command_type=str(data.get("command_type", "UPDATE_AP_PARAMETERS")),
            ap_parameters=ap_parameters,
        )

    @classmethod
    def create(
        cls,
        simulation_id: str,
        ap_parameters: Dict[str, ApParameters],
        command_type: str = "UPDATE_AP_PARAMETERS",
    ) -> "OptimizationCommand":
        """Create a new optimization command.

        Args:
            simulation_id: Simulation identifier
            ap_parameters: Dictionary mapping BSSID to ApParameters
            command_type: Type of command (default: UPDATE_AP_PARAMETERS)
        """
        return cls(
            simulation_id=simulation_id,
            command_type=command_type,
            ap_parameters=ap_parameters,
            timestamp_unix=int(time.time()),
        )
