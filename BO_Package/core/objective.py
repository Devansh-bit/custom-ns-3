"""Objective function calculation

Computes weighted objective function from network performance metrics.
"""

import logging
import numpy as np
from typing import Dict, Tuple

from ..metrics.models import APMetrics

logger = logging.getLogger('bayesian_optimizer.objective')


class ObjectiveCalculator:
    """Calculates objective function from metrics"""

    def __init__(
        self,
        weight_throughput: float = 0.35,
        weight_retry: float = 0.10,
        weight_loss: float = 0.35,
        weight_rtt: float = 0.20,
        max_p50_throughput: float = 100.0,
        max_p95_retry_rate: float = 5.0,
        max_p95_loss_rate: float = 1.0,
        max_p95_rtt: float = 750.0
    ):
        """Initialize objective calculator

        Args:
            weight_throughput: Weight for throughput component (default: 0.35)
            weight_retry: Weight for retry rate component (default: 0.10)
            weight_loss: Weight for packet loss component (default: 0.35)
            weight_rtt: Weight for RTT component (default: 0.20)
            max_p50_throughput: Maximum throughput for normalization (Mbps)
            max_p95_retry_rate: Maximum retry rate for normalization
            max_p95_loss_rate: Maximum packet loss rate for normalization
            max_p95_rtt: Maximum RTT for normalization (ms)
        """
        self.weight_throughput = weight_throughput
        self.weight_retry = weight_retry
        self.weight_loss = weight_loss
        self.weight_rtt = weight_rtt

        self.max_p50_throughput = max_p50_throughput
        self.max_p95_retry_rate = max_p95_retry_rate
        self.max_p95_loss_rate = max_p95_loss_rate
        self.max_p95_rtt = max_p95_rtt

        # Validate weights sum to 1.0
        total_weight = weight_throughput + weight_retry + weight_loss + weight_rtt
        if abs(total_weight - 1.0) > 0.001:
            raise ValueError(f"Weights must sum to 1.0, got {total_weight}")

    def calculate(
        self,
        per_ap_metrics: Dict[str, APMetrics]
    ) -> Tuple[float, float, float, float, float]:
        """Calculate objective function from per-AP metrics

        Formula: 0.35*throughput + 0.1*(1-retry) + 0.35*(1-loss) + 0.2*(1-rtt)

        Args:
            per_ap_metrics: Dictionary mapping BSSID to APMetrics

        Returns:
            Tuple of (objective_value, avg_p50_throughput, avg_p95_retry,
                     avg_p95_loss, avg_p95_rtt)
        """
        if not per_ap_metrics:
            logger.warning("No AP metrics to calculate objective")
            return 0.0, 0.0, 0.0, 0.0, 0.0

        # Extract values from all APs
        throughputs = [ap.p50_throughput for ap in per_ap_metrics.values()]
        retries = [ap.p95_retry_rate for ap in per_ap_metrics.values()]
        losses = [ap.p95_loss_rate for ap in per_ap_metrics.values()]
        rtts = [ap.p95_rtt for ap in per_ap_metrics.values()]

        # Average across APs
        global_p50_throughput = float(np.mean(throughputs)) if throughputs else 0.0
        global_p95_retry = float(np.mean(retries)) if retries else 0.0
        global_p95_loss = float(np.mean(losses)) if losses else 0.0
        global_p95_rtt = float(np.mean(rtts)) if rtts else 0.0

        logger.info("=" * 60)
        logger.info("GLOBAL METRICS (Averaged)")
        logger.info("=" * 60)
        logger.info(f"Global p50 Throughput: {global_p50_throughput:.4f} Mbps")
        logger.info(f"Global p95 Retry Rate: {global_p95_retry:.4f}")
        logger.info(f"Global p95 Loss Rate: {global_p95_loss:.4f}")
        logger.info(f"Global p95 RTT: {global_p95_rtt:.4f} ms")

        # Normalize by dividing by max values
        norm_throughput = global_p50_throughput / self.max_p50_throughput
        norm_retry = global_p95_retry / self.max_p95_retry_rate
        norm_loss = global_p95_loss / self.max_p95_loss_rate
        norm_rtt = global_p95_rtt / self.max_p95_rtt

        logger.info("")
        logger.info("NORMALIZED METRICS")
        logger.info("=" * 60)
        logger.info(f"Normalized p50 Throughput: {norm_throughput:.4f}")
        logger.info(f"Normalized p95 Retry Rate: {norm_retry:.4f}")
        logger.info(f"Normalized p95 Loss Rate: {norm_loss:.4f}")
        logger.info(f"Normalized p95 RTT: {norm_rtt:.4f}")

        # Calculate final objective function
        # Higher throughput is better, lower retry/loss/rtt is better
        throughput_component = self.weight_throughput * norm_throughput
        retry_component = self.weight_retry * (1 - norm_retry)
        loss_component = self.weight_loss * (1 - norm_loss)
        rtt_component = self.weight_rtt * (1 - norm_rtt)

        objective_value = (
            throughput_component +
            retry_component +
            loss_component +
            rtt_component
        )

        logger.info("")
        logger.info("OBJECTIVE FUNCTION CALCULATION")
        logger.info("=" * 60)
        logger.info(f"Throughput component: {throughput_component:.4f}")
        logger.info(f"Retry component:      {retry_component:.4f}")
        logger.info(f"Loss component:       {loss_component:.4f}")
        logger.info(f"RTT component:        {rtt_component:.4f}")
        logger.info("")
        logger.info("*" * 60)
        logger.info(f"OVERALL OBJECTIVE VALUE: {objective_value:.4f}")
        logger.info("*" * 60)

        return (
            objective_value,
            global_p50_throughput,
            global_p95_retry,
            global_p95_loss,
            global_p95_rtt
        )

    def get_component_breakdown(
        self,
        norm_throughput: float,
        norm_retry: float,
        norm_loss: float,
        norm_rtt: float
    ) -> Dict[str, float]:
        """Get breakdown of objective function components

        Args:
            norm_throughput: Normalized throughput [0, 1]
            norm_retry: Normalized retry rate [0, 1]
            norm_loss: Normalized loss rate [0, 1]
            norm_rtt: Normalized RTT [0, 1]

        Returns:
            Dictionary with component values
        """
        return {
            'throughput': self.weight_throughput * norm_throughput,
            'retry': self.weight_retry * (1 - norm_retry),
            'loss': self.weight_loss * (1 - norm_loss),
            'rtt': self.weight_rtt * (1 - norm_rtt)
        }
