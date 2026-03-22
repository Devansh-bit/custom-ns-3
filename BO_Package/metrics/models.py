"""Metric data models

Defines structured data types for metrics collection and processing.
"""

from dataclasses import dataclass, field
from typing import Dict, List, Optional


@dataclass
class STAMetrics:
    """Station (client) metrics"""
    mac: str
    associated_ap: str
    throughput: float = 0.0
    retry_rate: float = 0.0
    packet_loss: float = 0.0
    rtt: float = 0.0

    # EWMA state
    ewma_throughput: float = 0.0
    ewma_retry: float = 0.0
    ewma_loss: float = 0.0
    ewma_rtt: float = 0.0


@dataclass
class APMetrics:
    """Access Point metrics"""
    bssid: str
    associated_clients: List[str] = field(default_factory=list)

    # Per-client metrics
    client_metrics: Dict[str, STAMetrics] = field(default_factory=dict)

    # Aggregated percentiles
    p50_throughput: float = 0.0
    p95_retry_rate: float = 0.0
    p95_loss_rate: float = 0.0
    p95_rtt: float = 0.0


@dataclass
class AggregatedMetrics:
    """Aggregated metrics across all APs"""
    # Per-AP metrics
    per_ap_metrics: Dict[str, APMetrics] = field(default_factory=dict)

    # Global aggregated metrics
    avg_p50_throughput: float = 0.0
    avg_p95_retry: float = 0.0
    avg_p95_loss: float = 0.0
    avg_p95_rtt: float = 0.0

    # Objective value
    objective_value: float = 0.0

    def get_ap_count(self) -> int:
        """Get number of APs with metrics"""
        return len(self.per_ap_metrics)

    def get_total_clients(self) -> int:
        """Get total number of clients across all APs"""
        return sum(
            len(ap.associated_clients)
            for ap in self.per_ap_metrics.values()
        )


@dataclass
class LogEntry:
    """Single log entry from simulation"""
    message_number: int
    simulation_id: str
    timestamp: float  # Wall-clock time
    sim_timestamp: int  # NS-3 simulation timestamp
    sim_time: float  # Simulation time in seconds
    data: Dict  # Raw metrics data (ap_metrics, sta_metrics)


@dataclass
class EWMAState:
    """EWMA state for a single metric"""
    value: float = 0.0
    initialized: bool = False

    def update(self, new_value: float, alpha: float = 0.3) -> float:
        """Update EWMA with new value

        Args:
            new_value: New metric value
            alpha: EWMA smoothing factor (0-1)

        Returns:
            Updated EWMA value
        """
        if not self.initialized:
            self.value = new_value
            self.initialized = True
        else:
            self.value = alpha * new_value + (1 - alpha) * self.value

        return self.value


@dataclass
class ClientEWMAState:
    """EWMA states for all metrics of a single client"""
    throughput: EWMAState = field(default_factory=EWMAState)
    retry: EWMAState = field(default_factory=EWMAState)
    loss: EWMAState = field(default_factory=EWMAState)
    rtt: EWMAState = field(default_factory=EWMAState)

    def update_all(
        self,
        throughput: float,
        retry: float,
        loss: float,
        rtt: float,
        alpha: float = 0.3
    ) -> Dict[str, float]:
        """Update all EWMA states

        Args:
            throughput: New throughput value
            retry: New retry rate
            loss: New packet loss
            rtt: New RTT
            alpha: EWMA smoothing factor

        Returns:
            Dictionary of updated EWMA values
        """
        return {
            'throughput': self.throughput.update(throughput, alpha),
            'retry': self.retry.update(retry, alpha),
            'loss': self.loss.update(loss, alpha),
            'rtt': self.rtt.update(rtt, alpha)
        }

    def get_values(self) -> Dict[str, float]:
        """Get current EWMA values

        Returns:
            Dictionary of current EWMA values
        """
        return {
            'throughput': self.throughput.value,
            'retry': self.retry.value,
            'loss': self.loss.value,
            'rtt': self.rtt.value
        }
