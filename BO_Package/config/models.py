"""Configuration data models

Defines typed configuration structures for the optimizer.
"""

from dataclasses import dataclass, field
from typing import List, Optional, Union


@dataclass
class APConfig:
    """Configuration for a single Access Point"""
    bssid: str
    channel: int
    tx_power: float
    obss_pd: float

    def to_dict(self) -> dict:
        """Convert to dictionary for Kafka messages"""
        return {
            'bssid': self.bssid,
            'channel': self.channel,
            'tx_power': self.tx_power,
            'obss_pd': self.obss_pd
        }


@dataclass
class OptimizerConfig:
    """Main optimizer configuration"""
    # Number of APs (dynamic, set via CLI)
    num_aps: int

    # Optimization parameters
    study_time: int = 10000  # minutes
    evaluation_window: int = 5  # seconds (simulation time)
    settling_time: int = 5  # seconds (real time, Trial 0 only)

    # Time constraints (simulation clock)
    start_time_hour: int = 6
    end_time_hour: int = 9

    # File paths
    config_file: str = "config-simulation.json"
    log_dir: str = "output_logs"

    # External systems
    kafka_broker: str = "localhost:9092"
    kafka_topic: str = "optimization-commands"
    kafka_key: str = "sim-001"
    api_base_url: str = "http://localhost:8000"

    # Optimization parameters
    available_channels: List[int] = field(default_factory=lambda: [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112])
    tx_power_range: tuple = (13.0, 25.0)  # dBm
    obss_pd_range: tuple = (-80.0, -60.0)  # dBm

    # EWMA parameters
    ewma_alpha: float = 0.3

    # Normalization constants
    max_p50_throughput: float = 100.0  # Mbps
    max_p95_retry_rate: float = 5.0
    max_p95_loss_rate: float = 1.0
    max_p95_rtt: float = 750.0  # ms

    # Objective function weights
    weight_throughput: float = 0.35
    weight_retry: float = 0.10
    weight_loss: float = 0.35
    weight_rtt: float = 0.20

    # Doubly-robust parameters
    dr_confidence_threshold: float = 0.95
    dr_bootstrap_samples: int = 100
    dr_min_improvement: float = 0.02
    dr_min_propensity: float = 1e-10

    # Optuna parameters
    optuna_seed: int = 42

    def __post_init__(self):
        """Validate configuration after initialization"""
        # Validate weights sum to 1.0
        total_weight = (self.weight_throughput + self.weight_retry +
                       self.weight_loss + self.weight_rtt)
        if abs(total_weight - 1.0) > 0.001:
            raise ValueError(f"Objective weights must sum to 1.0, got {total_weight}")

        # Validate num_aps is positive
        if self.num_aps <= 0:
            raise ValueError(f"num_aps must be positive, got {self.num_aps}")

        # Validate time window
        if self.start_time_hour >= self.end_time_hour:
            raise ValueError(
                f"start_time_hour ({self.start_time_hour}) must be < "
                f"end_time_hour ({self.end_time_hour})"
            )


@dataclass
class SimulationConfig:
    """Configuration loaded from config-simulation.json"""
    num_aps: int
    ap_bssids: List[str]
    initial_channels: List[int]
    initial_tx_power: List[float]
    initial_obss_pd: List[float]
    available_channels: List[int]
    clock_start_time: Union[str, int]

    @classmethod
    def from_json(cls, config_data: dict) -> 'SimulationConfig':
        """Create from loaded JSON config"""
        # Extract APs configuration
        aps = config_data.get('aps', [])
        num_aps = len(aps)

        ap_bssids = []
        initial_channels = []
        initial_tx_power = []
        initial_obss_pd = []

        for ap in aps:
            lever_config = ap.get('leverConfig', {})
            ap_bssids.append(ap.get('bssid', ''))
            initial_channels.append(lever_config.get('channel', 36))
            initial_tx_power.append(lever_config.get('tx_power', 20.0))
            initial_obss_pd.append(lever_config.get('obss_pd', -72.0))

        # Extract available channels (5GHz only)
        system_config = config_data.get('system_config', {})
        scanning_channels = system_config.get('scanning_channels', [])
        available_channels = [ch for ch in scanning_channels if ch >= 36]

        # Extract clock start time
        clock_start_time = (
            config_data.get('clock-time') or
            system_config.get('clock_time') or
            "06:00:00"
        )

        return cls(
            num_aps=num_aps,
            ap_bssids=ap_bssids,
            initial_channels=initial_channels,
            initial_tx_power=initial_tx_power,
            initial_obss_pd=initial_obss_pd,
            available_channels=available_channels if available_channels else [36, 40, 44, 48, 52, 56, 60, 64],
            clock_start_time=clock_start_time
        )

    def get_baseline_aps(self) -> List[APConfig]:
        """Get baseline AP configurations"""
        return [
            APConfig(
                bssid=self.ap_bssids[i],
                channel=self.initial_channels[i],
                tx_power=self.initial_tx_power[i],
                obss_pd=self.initial_obss_pd[i]
            )
            for i in range(self.num_aps)
        ]
