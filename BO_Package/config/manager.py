"""Configuration manager

Handles loading and validation of configuration files.
"""

import json
import logging
from pathlib import Path
from typing import Optional

from .models import SimulationConfig, OptimizerConfig

logger = logging.getLogger('bayesian_optimizer.config')


class ConfigManager:
    """Manages configuration loading and validation"""

    def __init__(self, optimizer_config: OptimizerConfig):
        """Initialize configuration manager

        Args:
            optimizer_config: Main optimizer configuration
        """
        self.optimizer_config = optimizer_config
        self.simulation_config: Optional[SimulationConfig] = None

    def load_simulation_config(self) -> SimulationConfig:
        """Load configuration from config-simulation.json

        Returns:
            SimulationConfig object

        Raises:
            FileNotFoundError: If config file doesn't exist
            ValueError: If config is invalid
        """
        config_path = Path(self.optimizer_config.config_file)

        if not config_path.exists():
            logger.warning(f"Config file not found: {config_path}, using defaults")
            return self._get_default_simulation_config()

        try:
            with open(config_path, 'r') as f:
                config_data = json.load(f)

            self.simulation_config = SimulationConfig.from_json(config_data)

            # Validate AP count matches if specified
            if self.simulation_config.num_aps != self.optimizer_config.num_aps:
                logger.warning(
                    f"Config file has {self.simulation_config.num_aps} APs, "
                    f"but CLI specified {self.optimizer_config.num_aps} APs. "
                    f"Using CLI value ({self.optimizer_config.num_aps})."
                )

            logger.info(f"Loaded config from {config_path}")
            logger.info(f"  APs: {self.simulation_config.num_aps}")
            logger.info(f"  Available channels: {self.simulation_config.available_channels}")
            logger.info(f"  Clock start: {self.simulation_config.clock_start_time}")

            # Update optimizer config with loaded channels if needed
            if self.simulation_config.available_channels:
                self.optimizer_config.available_channels = self.simulation_config.available_channels

            return self.simulation_config

        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse config file: {e}")
            logger.warning("Using default configuration")
            return self._get_default_simulation_config()

        except Exception as e:
            logger.error(f"Failed to load config file: {e}")
            logger.warning("Using default configuration")
            return self._get_default_simulation_config()

    def _get_default_simulation_config(self) -> SimulationConfig:
        """Get default simulation configuration

        Returns:
            Default SimulationConfig
        """
        num_aps = self.optimizer_config.num_aps

        logger.info(f"Creating default config for {num_aps} APs")

        # Generate default BSSIDs
        ap_bssids = [f"00:00:00:00:00:{i:02x}" for i in range(num_aps)]

        # Generate default channels (cycle through common 5GHz channels)
        default_channels = [36, 40, 44, 48, 52, 56]
        initial_channels = [default_channels[i % len(default_channels)] for i in range(num_aps)]

        # Default power and OBSS-PD
        initial_tx_power = [20.0] * num_aps
        initial_obss_pd = [-72.0] * num_aps

        return SimulationConfig(
            num_aps=num_aps,
            ap_bssids=ap_bssids,
            initial_channels=initial_channels,
            initial_tx_power=initial_tx_power,
            initial_obss_pd=initial_obss_pd,
            available_channels=self.optimizer_config.available_channels,
            clock_start_time="06:00:00"
        )

    def get_baseline_config(self) -> dict:
        """Get baseline configuration for first trial

        Returns:
            Baseline configuration dict suitable for Optuna
        """
        if self.simulation_config is None:
            self.load_simulation_config()

        baseline = {}

        for i in range(self.optimizer_config.num_aps):
            # Find channel index in available channels
            channel = self.simulation_config.initial_channels[i]
            try:
                channel_idx = self.optimizer_config.available_channels.index(channel)
            except ValueError:
                # If channel not in available list, use first channel
                logger.warning(
                    f"AP {i} initial channel {channel} not in available channels, "
                    f"using {self.optimizer_config.available_channels[0]}"
                )
                channel_idx = 0

            baseline[f'channel_idx_{i}'] = channel_idx
            baseline[f'tx_power_{i}'] = self.simulation_config.initial_tx_power[i]
            baseline[f'obss_pd_{i}'] = self.simulation_config.initial_obss_pd[i]

        logger.info("Baseline configuration:")
        for i in range(self.optimizer_config.num_aps):
            logger.info(
                f"  AP {i}: Channel={self.simulation_config.initial_channels[i]}, "
                f"TX={self.simulation_config.initial_tx_power[i]} dBm, "
                f"OBSS-PD={self.simulation_config.initial_obss_pd[i]} dBm"
            )

        return baseline

    def validate_config(self) -> bool:
        """Validate configuration consistency

        Returns:
            True if valid, False otherwise
        """
        try:
            # Validate optimizer config
            if self.optimizer_config.num_aps <= 0:
                logger.error("num_aps must be positive")
                return False

            # Validate simulation config if loaded
            if self.simulation_config:
                if len(self.simulation_config.ap_bssids) != self.optimizer_config.num_aps:
                    logger.error(
                        f"BSSID count mismatch: expected {self.optimizer_config.num_aps}, "
                        f"got {len(self.simulation_config.ap_bssids)}"
                    )
                    return False

                if not self.optimizer_config.available_channels:
                    logger.error("No available channels configured")
                    return False

            logger.info("Configuration validation passed")
            return True

        except Exception as e:
            logger.error(f"Configuration validation failed: {e}")
            return False
