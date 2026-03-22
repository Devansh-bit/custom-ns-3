"""Metrics processing - EWMA and percentile calculations

Handles metric aggregation, EWMA smoothing, and per-AP statistics.
"""

import logging
import numpy as np
from typing import List, Dict, Tuple

from .models import APMetrics, AggregatedMetrics, ClientEWMAState

logger = logging.getLogger('bayesian_optimizer.metrics.processor')


class MetricsProcessor:
    """Processes metrics with EWMA and percentile aggregation"""

    def __init__(self, ewma_alpha: float = 0.3):
        """Initialize metrics processor

        Args:
            ewma_alpha: EWMA smoothing factor (0-1)
        """
        self.ewma_alpha = ewma_alpha
        self.client_ewma_states: Dict[str, ClientEWMAState] = {}
        self.client_ap_mapping: Dict[str, str] = {}

    def process_logs(self, logs: List[Dict]) -> Tuple[Dict[str, ClientEWMAState], Dict[str, str]]:
        """Process logs with per-client EWMA

        Args:
            logs: List of log entries

        Returns:
            Tuple of (client_ewma_states, client_ap_mapping)
        """
        if not logs:
            logger.warning("No logs to process")
            return {}, {}

        sample_count = 0

        # Process logs with per-client EWMA
        for idx, log in enumerate(logs):
            try:
                data = log.get('data', {})

                # Process ap_metrics -> connection_metrics
                for ap_bssid, ap_data in data.get('ap_metrics', {}).items():
                    connection_metrics = ap_data.get('connection_metrics', {})

                    # Process each connection (STA->AP pair)
                    for connection_key, metrics in connection_metrics.items():
                        # Extract STA MAC from connection key format "STA->AP"
                        sta_mac = metrics.get('sta_address', '')
                        if not sta_mac:
                            continue

                        # Initialize client state if not exists
                        if sta_mac not in self.client_ewma_states:
                            self.client_ewma_states[sta_mac] = ClientEWMAState()

                        # Update AP mapping - use ap_address from metrics
                        current_bssid = metrics.get('ap_address', ap_bssid)
                        if current_bssid:
                            self.client_ap_mapping[sta_mac] = current_bssid

                        # Get current values - cap and normalize
                        # Throughput: cap between 0-100 Mbps
                        throughput_uplink = self._clamp(
                            float(metrics.get('uplink_throughput_mbps', 0)), 0.0, 100.0
                        )
                        throughput_downlink = self._clamp(
                            float(metrics.get('downlink_throughput_mbps', 0)), 0.0, 100.0
                        )
                        throughput_total = throughput_uplink + throughput_downlink

                        # Packet loss rate: comes as percentage 0-100, cap then divide by 100
                        packet_loss_rate = self._clamp(
                            float(metrics.get('packet_loss_rate', 0)), 0.0, 100.0
                        ) / 100.0

                        # Retry rate: comes as percentage 0-500, cap then divide by 100
                        retry_rate = self._clamp(
                            float(metrics.get('mac_retry_rate', 0)), 0.0, 500.0
                        ) / 100.0

                        # RTT: cap between 0-750 ms
                        tcp_rtt = self._clamp(
                            float(metrics.get('mean_rtt_latency', 0)), 0.0, 750.0
                        )

                        # Update EWMA states
                        self.client_ewma_states[sta_mac].update_all(
                            throughput=throughput_total,
                            retry=retry_rate,
                            loss=packet_loss_rate,
                            rtt=tcp_rtt,
                            alpha=self.ewma_alpha
                        )

                        sample_count += 1

            except Exception as e:
                logger.warning(f"Error processing log entry {idx}: {e}")
                continue

        logger.info(f"Processed {sample_count} metric samples from {len(logs)} log entries")
        logger.info(f"Tracked {len(self.client_ewma_states)} unique clients")

        return self.client_ewma_states, self.client_ap_mapping

    def calculate_per_ap_metrics(self) -> Dict[str, APMetrics]:
        """Calculate per-AP percentile metrics

        Returns:
            Dictionary mapping BSSID to APMetrics
        """
        # Group clients by AP
        ap_client_states: Dict[str, List[ClientEWMAState]] = {}

        for sta_mac, state in self.client_ewma_states.items():
            current_bssid = self.client_ap_mapping.get(sta_mac)
            if not current_bssid or current_bssid == "00:00:00:00:00:00":
                continue

            if current_bssid not in ap_client_states:
                ap_client_states[current_bssid] = []

            ap_client_states[current_bssid].append(state)

        # Calculate percentiles for each AP
        per_ap_metrics: Dict[str, APMetrics] = {}

        for bssid, client_states in ap_client_states.items():
            if not client_states:
                continue

            # Extract EWMA values for each metric
            throughputs = [s.throughput.value for s in client_states]
            retries = [s.retry.value for s in client_states]
            losses = [s.loss.value for s in client_states]
            rtts = [s.rtt.value for s in client_states]

            # Calculate percentiles
            p50_throughput = float(np.percentile(throughputs, 50)) if throughputs else 0.0
            p95_retry = float(np.percentile(retries, 95)) if retries else 0.0
            p95_loss = float(np.percentile(losses, 95)) if losses else 0.0
            p95_rtt = float(np.percentile(rtts, 95)) if rtts else 0.0

            per_ap_metrics[bssid] = APMetrics(
                bssid=bssid,
                associated_clients=[],  # Can be populated if needed
                p50_throughput=p50_throughput,
                p95_retry_rate=p95_retry,
                p95_loss_rate=p95_loss,
                p95_rtt=p95_rtt
            )

            logger.debug(
                f"AP {bssid}: "
                f"p50_tput={p50_throughput:.2f} Mbps, "
                f"p95_retry={p95_retry:.4f}, "
                f"p95_loss={p95_loss:.4f}, "
                f"p95_rtt={p95_rtt:.2f} ms "
                f"({len(client_states)} clients)"
            )

        logger.info(f"Calculated metrics for {len(per_ap_metrics)} APs")
        return per_ap_metrics

    def calculate_aggregated_metrics(
        self,
        per_ap_metrics: Dict[str, APMetrics]
    ) -> Tuple[float, float, float, float]:
        """Calculate global aggregated metrics across all APs

        Args:
            per_ap_metrics: Per-AP metrics dictionary

        Returns:
            Tuple of (avg_p50_throughput, avg_p95_retry, avg_p95_loss, avg_p95_rtt)
        """
        if not per_ap_metrics:
            logger.warning("No AP metrics to aggregate")
            return 0.0, 0.0, 0.0, 0.0

        # Extract values
        throughputs = [ap.p50_throughput for ap in per_ap_metrics.values()]
        retries = [ap.p95_retry_rate for ap in per_ap_metrics.values()]
        losses = [ap.p95_loss_rate for ap in per_ap_metrics.values()]
        rtts = [ap.p95_rtt for ap in per_ap_metrics.values()]

        # Average across APs
        avg_p50_throughput = float(np.mean(throughputs)) if throughputs else 0.0
        avg_p95_retry = float(np.mean(retries)) if retries else 0.0
        avg_p95_loss = float(np.mean(losses)) if losses else 0.0
        avg_p95_rtt = float(np.mean(rtts)) if rtts else 0.0

        logger.info("Aggregated metrics across APs:")
        logger.info(f"  Avg p50 throughput: {avg_p50_throughput:.2f} Mbps")
        logger.info(f"  Avg p95 retry rate: {avg_p95_retry:.4f}")
        logger.info(f"  Avg p95 loss rate: {avg_p95_loss:.4f}")
        logger.info(f"  Avg p95 RTT: {avg_p95_rtt:.2f} ms")

        return avg_p50_throughput, avg_p95_retry, avg_p95_loss, avg_p95_rtt

    def reset_states(self) -> None:
        """Reset EWMA states (call between trials)"""
        self.client_ewma_states.clear()
        self.client_ap_mapping.clear()
        logger.debug("Reset EWMA states")

    @staticmethod
    def _clamp(value: float, min_val: float, max_val: float) -> float:
        """Clamp value between min and max

        Args:
            value: Value to clamp
            min_val: Minimum value
            max_val: Maximum value

        Returns:
            Clamped value
        """
        return max(min(value, max_val), min_val)

    def build_per_ap_report(
        self,
        per_ap_metrics: Dict[str, APMetrics]
    ) -> Dict[str, Dict[str, float]]:
        """Build per-AP metrics for API reporting

        Args:
            per_ap_metrics: Per-AP metrics dictionary

        Returns:
            Dictionary suitable for API report
        """
        report = {}

        for bssid, metrics in per_ap_metrics.items():
            report[bssid] = {
                'p50_throughput': metrics.p50_throughput,
                'p95_retry_rate': metrics.p95_retry_rate,
                'p95_loss_rate': metrics.p95_loss_rate,
                'p95_rtt': metrics.p95_rtt
            }

        return report
