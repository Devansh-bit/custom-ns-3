#!/usr/bin/env python3
"""
GAT Node Data Processor - Batched EWMA State Provider

This module processes simulation logs and computes EWMA (Exponentially Weighted
Moving Average) of AP metrics in batches. Designed to be imported by main.py.

Usage:
    # As a module (imported by main.py)
    from gat_streamer import GATStateProvider
    provider = GATStateProvider(batch_size=20)
    state = provider.get_next_state()  # Returns EWMA of 20 logs, then resets

    # Standalone (for testing)
    python gat_streamer.py --batch-size 20
"""

import json
import time
import argparse
import sys
import numpy as np
from pathlib import Path
from typing import Dict, Any, Optional, List, TYPE_CHECKING
from collections import defaultdict

# Add parent directory to path for kafka_helper import
sys.path.insert(0, str(Path(__file__).parent.parent))

# Try to import kafka_helper, fall back gracefully if not available
try:
    from kafka_helper import KafkaMetricsConsumer
    KAFKA_AVAILABLE = True
except ImportError:
    KAFKA_AVAILABLE = False

if TYPE_CHECKING:
    from kafka_helper import KafkaMetricsConsumer


def get_default_log_file() -> Path:
    """
    Get the default log file path that matches kafka-rl-consumer.py.

    Returns:
        Path to ./output_logs/gat_consumer_logs.json (relative to project root)
    """
    script_dir = Path(__file__).parent.resolve()
    return script_dir.parent / 'output_logs' / 'gat_consumer_logs.json'


class GATStateProvider:
    """
    Provides batched EWMA state from simulation logs.

    Collects `batch_size` logs, computes EWMA, returns the state,
    then resets for the next batch.

    Supports two modes:
    - Kafka mode (default): Direct consumption from Kafka using kafka_helper
    - File mode (fallback): Read from JSON log file for local testing
    """

    def __init__(
        self,
        # Kafka mode parameters
        broker: str = "localhost:9092",
        topic: str = "ns3-metrics",
        simulation_id: str = "basic-sim",
        use_kafka: bool = True,
        # File mode parameters (kept for backward compatibility)
        log_file: Optional[str] = None,
        # Common parameters
        batch_size: int = 6,
        ewma_alpha: float = 0.3,
        poll_interval: float = 0.5,
        settling_time: float = 10.0,
        verbose: bool = True
    ):
        """
        Initialize the GAT State Provider.

        Args:
            broker: Kafka broker address (Kafka mode)
            topic: Kafka topic to consume from (Kafka mode)
            simulation_id: Simulation ID for Kafka message filtering (Kafka mode)
            use_kafka: If True, use Kafka; if False, use file-based mode
            log_file: Path to log file for file mode (auto-detects if None)
            batch_size: Number of logs to collect before returning state
            ewma_alpha: EWMA smoothing factor (0-1)
            poll_interval: How often to check for new logs (seconds)
            settling_time: Time to wait for network to stabilize (seconds)
            verbose: Enable verbose logging
        """
        self.batch_size = batch_size
        self.ewma_alpha = ewma_alpha
        self.poll_interval = poll_interval
        self.verbose = verbose
        self.settling_time = settling_time

        # Mode selection
        self._use_kafka = use_kafka and KAFKA_AVAILABLE
        if use_kafka and not KAFKA_AVAILABLE:
            if verbose:
                print("[GATProvider] WARNING: kafka_helper not available, falling back to file mode")
            self._use_kafka = False

        # Kafka mode state
        self._broker = broker
        self._topic = topic
        self._simulation_id = simulation_id
        self._kafka_consumer: Optional['KafkaMetricsConsumer'] = None

        # File mode state (kept for backward compatibility)
        if log_file:
            self.log_file = Path(log_file)
        else:
            self.log_file = None
        self._last_read_position = 0

        # Initialize/reset state
        self.reset_state()

        # Track total batches processed
        self.total_batches = 0
        self.total_logs = 0

        # Connection status
        self._initialized = False

        # RSSI floor value for APs that don't see each other
        self.RSSI_FLOOR = -100.0

    def reset_state(self):
        """Reset all EWMA state for a fresh batch."""
        self.ewma_state: Dict[str, Dict[str, float]] = {}
        self.edge_ewma_state: Dict[tuple, float] = {}
        self.ap_channels: Dict[str, int] = {}  # Last channel value (categorical)
        self.current_batch_count = 0
        # Track sim_times for this batch (for correlation with NS3 logs)
        self.batch_sim_times: List[float] = []
        # Per-log metrics for each AP (for percentile calculations across logs)
        # Structure: {ap_bssid: [value_log1, value_log2, ..., value_logN]}
        # Throughput = sum of uplink across clients, then p50 across logs
        self.ap_throughput_per_log: Dict[str, List[float]] = {}
        # Loss/RTT/Jitter = p95 across clients per log, then p75 across logs
        self.ap_loss_p95_per_log: Dict[str, List[float]] = {}
        self.ap_rtt_p95_per_log: Dict[str, List[float]] = {}
        self.ap_jitter_p95_per_log: Dict[str, List[float]] = {}

    def initialize(self) -> bool:
        """
        Initialize the provider by finding the log file.

        Returns:
            True if successful, False otherwise
        """
        if self._initialized:
            return True

        if self.log_file is None:
            # Use the fixed log file path (same as kafka-rl-consumer.py)
            self.log_file = get_default_log_file()
            if self.verbose:
                try:
                    display_path = self.log_file.relative_to(Path.cwd())
                except ValueError:
                    display_path = self.log_file.name
                print(f"[GATProvider] Using log file: {display_path}")

        self._initialized = True
        if self.verbose:
            print(f"[GATProvider] Initialized with batch_size={self.batch_size}, "
                  f"ewma_alpha={self.ewma_alpha}")
        return True

    def connect(self) -> bool:
        """
        Connect to data source (Kafka or file).

        For Kafka mode: Connects to Kafka broker and seeks to end of topic.
        For File mode: Falls back to initialize() for file-based mode.

        Returns:
            True if successful, False otherwise
        """
        if not self._use_kafka:
            # File mode - use existing initialize()
            return self.initialize()

        if self._kafka_consumer is not None:
            return True

        try:
            self._kafka_consumer = KafkaMetricsConsumer(
                broker=self._broker,
                topic=self._topic,
                simulation_id=self._simulation_id,
                auto_offset_reset="latest"
            )
            self._kafka_consumer.connect()

            # Do an initial poll to trigger partition assignment
            # This is required because seek_to_end() needs partitions to be assigned first
            if self.verbose:
                print(f"[GATProvider] Waiting for Kafka partition assignment...")
            initial_msgs = self._kafka_consumer.poll_batch(timeout_ms=5000, max_records=1)
            if self.verbose:
                print(f"[GATProvider] Partition assignment complete (got {len(initial_msgs)} initial messages)")

            # Now seek to end to start from latest messages
            try:
                self._kafka_consumer.seek_to_end()
                if self.verbose:
                    print(f"[GATProvider] Seeked to end of topic")
            except Exception as seek_err:
                if self.verbose:
                    print(f"[GATProvider] seek_to_end warning (non-fatal): {seek_err}")
                # Not fatal - auto_offset_reset="latest" handles this

            self._initialized = True
            if self.verbose:
                print(f"[GATProvider] Connected to Kafka: {self._broker}/{self._topic} "
                      f"(simulation_id={self._simulation_id})")
            return True
        except Exception as e:
            print(f"[GATProvider] Failed to connect to Kafka: {e}")
            print(f"[GATProvider] Falling back to file mode")
            self._use_kafka = False
            return self.initialize()

    def close(self):
        """Close Kafka connection (if connected)."""
        if self._kafka_consumer:
            try:
                self._kafka_consumer.close()
            except Exception as e:
                if self.verbose:
                    print(f"[GATProvider] Error closing Kafka consumer: {e}")
            self._kafka_consumer = None

    def _ewma_update(self, old_value: Optional[float], new_value: float) -> float:
        """Update EWMA value."""
        if old_value is None:
            return new_value
        return self.ewma_alpha * new_value + (1 - self.ewma_alpha) * old_value

    def _get_max_channel_util_for_channel(self, ap_metrics: Dict, target_channel: int) -> float:
        """Get maximum channel utilization for a given channel across all APs."""
        channel_key = str(target_channel)
        max_util = 0.0

        for ap_bssid, ap_data in ap_metrics.items():
            scanning_data = ap_data.get('scanning_channel_data', {})
            if channel_key in scanning_data:
                channel_info = scanning_data[channel_key]
                util = channel_info.get('channel_utilization', 0.0)
                max_util = max(max_util, util)

        return max_util

    def _get_num_aps_on_channel(self, scanning_data: Dict, target_channel: int) -> int:
        """Get number of APs on a given channel."""
        channel_key = str(target_channel)

        if channel_key not in scanning_data:
            return 1  # At least self

        channel_info = scanning_data[channel_key]
        bssid_count = channel_info.get('bssid_count', 0)

        return bssid_count + 1


    def _extract_rssi_between_aps(self, ap_metrics: Dict) -> Dict[tuple, float]:
        """
        Extract RSSI values between AP pairs from scanning data.
        
        IMPORTANT: This function ALWAYS returns ALL possible AP pairs (N*(N-1) pairs).
        For pairs where no edge exists (APs don't see each other), RSSI_FLOOR is used.
        
        Returns:
            Dict mapping (ap_bssid_i, ap_bssid_j) -> RSSI value (or RSSI_FLOOR if no edge)
        """
        rssi_pairs = {}
        all_ap_bssids = list(ap_metrics.keys())

        if not all_ap_bssids:
            return rssi_pairs

        # FIRST: Initialize ALL pairs to RSSI_FLOOR (no edge exists by default)
        for ap_i in all_ap_bssids:
            for ap_j in all_ap_bssids:
                if ap_i != ap_j:
                    rssi_pairs[(ap_i, ap_j)] = self.RSSI_FLOOR

        # SECOND: Overwrite with actual RSSI values where edges DO exist
        for ap_bssid, ap_data in ap_metrics.items():
            scanning_data = ap_data.get('scanning_channel_data', {})
            
            if not scanning_data or not isinstance(scanning_data, dict):
                # No scanning data - keep RSSI_FLOOR for all pairs from this AP
                continue

            # Collect all neighbors this AP can see across ALL channels
            for channel_key, channel_info in scanning_data.items():
                if not isinstance(channel_info, dict):
                    continue
                    
                neighbors = channel_info.get('neighbors', [])
                if not isinstance(neighbors, list):
                    continue
                    
                for neighbor in neighbors:
                    if not isinstance(neighbor, dict):
                        continue
                        
                    neighbor_bssid = neighbor.get('bssid')
                    rssi = neighbor.get('rssi')

                    # Validate and store RSSI if valid
                    if neighbor_bssid and rssi is not None:
                        try:
                            rssi_float = float(rssi)
                            # Only store if it's a valid RSSI value (not NaN, not infinite)
                            if not (np.isnan(rssi_float) or np.isinf(rssi_float)):
                                # Overwrite the RSSI_FLOOR with actual RSSI value
                                rssi_pairs[(ap_bssid, neighbor_bssid)] = rssi_float
                        except (ValueError, TypeError):
                            # Skip invalid RSSI values - keep RSSI_FLOOR
                            pass

        return rssi_pairs

    def _process_log_entry(self, log_entry: Dict[str, Any]) -> bool:
        """
        Process a single log entry and update EWMA state.

        Returns:
            True if entry was processed, False if invalid
        """
        data = log_entry.get('data', {})
        ap_metrics = data.get('ap_metrics', {})

        if not ap_metrics:
            return False

        # Extract and track sim_time for this log entry
        sim_time = data.get('sim_time_seconds', 0.0)
        if sim_time > 0:
            self.batch_sim_times.append(sim_time)

        # Process each AP
        for ap_bssid, ap_data in ap_metrics.items():
            channel = ap_data.get('channel', 0)
            tx_power = ap_data.get('tx_power_dbm', 17.0)  # Read actual TX power from NS3
            num_clients = ap_data.get('associated_clients', 0)
            scanning_data = ap_data.get('scanning_channel_data', {})

            # Get channel utilization directly from AP data (not from scanning_channel_data)
            # NS-3 sends as percentage (0-100), convert to fraction (0-1)
            channel_util = ap_data.get('channel_utilization', 0.0) / 100.0

            # Get number of APs on the same channel
            num_aps_on_channel = self._get_num_aps_on_channel(scanning_data, channel)

            # Initialize EWMA state for this AP if needed
            if ap_bssid not in self.ewma_state:
                self.ewma_state[ap_bssid] = {}

            # Update EWMA for each field
            state = self.ewma_state[ap_bssid]
            state['channel_utilization'] = self._ewma_update(
                state.get('channel_utilization'), channel_util
            )
            state['num_aps_on_channel'] = self._ewma_update(
                state.get('num_aps_on_channel'), num_aps_on_channel
            )
            state['num_clients'] = self._ewma_update(
                state.get('num_clients'), num_clients
            )
            # Apply EWMA to power as well
            state['power'] = self._ewma_update(
                state.get('power'), tx_power
            )

            # Track channel as LAST value (not EWMA - channel is categorical)
            self.ap_channels[ap_bssid] = channel

            # ================================================================
            # Process connection_metrics:
            # - Throughput: sum uplink across clients for this log
            # - Loss/RTT/Jitter: p95 across clients for this log
            # ================================================================
            connection_metrics = ap_data.get('connection_metrics', {})

            # Collect raw values from all clients for this AP in this log
            ap_uplink_sum_this_log = 0.0
            client_losses = []
            client_rtts = []
            client_jitters = []

            if connection_metrics:
                for conn_key, metrics in connection_metrics.items():
                    sta_mac = metrics.get('sta_address', conn_key.split('->')[0])
                    if not sta_mac:
                        continue

                    # Throughput: sum uplink across clients
                    uplink = float(metrics.get('uplink_throughput_mbps', 0))
                    ap_uplink_sum_this_log += uplink

                    # Packet loss rate: percentage 0-100
                    loss = float(metrics.get('packet_loss_rate', 0))
                    client_losses.append(loss)

                    # RTT: in ms
                    rtt = float(metrics.get('mean_rtt_latency', 0))
                    client_rtts.append(rtt)

                    # Jitter: in ms
                    jitter = float(metrics.get('jitter_ms', 0))
                    client_jitters.append(jitter)

            # Store this log's metrics for this AP
            # Throughput: sum across clients
            if ap_bssid not in self.ap_throughput_per_log:
                self.ap_throughput_per_log[ap_bssid] = []
            self.ap_throughput_per_log[ap_bssid].append(ap_uplink_sum_this_log)

            # Loss/RTT/Jitter: p95 across clients (or 0 if no clients)
            if ap_bssid not in self.ap_loss_p95_per_log:
                self.ap_loss_p95_per_log[ap_bssid] = []
            if ap_bssid not in self.ap_rtt_p95_per_log:
                self.ap_rtt_p95_per_log[ap_bssid] = []
            if ap_bssid not in self.ap_jitter_p95_per_log:
                self.ap_jitter_p95_per_log[ap_bssid] = []

            p95_loss = np.percentile(client_losses, 95) if client_losses else 0.0
            p95_rtt = np.percentile(client_rtts, 95) if client_rtts else 0.0
            p95_jitter = np.percentile(client_jitters, 95) if client_jitters else 0.0

            self.ap_loss_p95_per_log[ap_bssid].append(p95_loss)
            self.ap_rtt_p95_per_log[ap_bssid].append(p95_rtt)
            self.ap_jitter_p95_per_log[ap_bssid].append(p95_jitter)

        # Process RSSI between AP pairs
        rssi_pairs = self._extract_rssi_between_aps(ap_metrics)
        for pair, rssi in rssi_pairs.items():
            self.edge_ewma_state[pair] = self._ewma_update(
                self.edge_ewma_state.get(pair), rssi
            )

        self.current_batch_count += 1
        self.total_logs += 1
        return True

    def _read_logs_from_file(self) -> List[Dict]:
        """Read NEW logs from file without clearing (track position instead)."""
        if self.log_file is None or not self.log_file.exists():
            return []

        logs = []
        try:
            with open(self.log_file, 'r') as f:
                lines = f.readlines()

            if not lines:
                return []

            # Only return NEW lines (after last read position)
            new_lines = lines[self._last_read_position:]
            for line in new_lines:
                line = line.strip()
                if line:
                    try:
                        log_entry = json.loads(line)
                        logs.append(log_entry)
                    except json.JSONDecodeError:
                        pass

            # Update position to include all current lines
            self._last_read_position = len(lines)

            return logs

        except Exception as e:
            if self.verbose:
                print(f"[GATProvider] Error reading log file: {e}")
            return []

    def _clear_log_file(self):
        """Clear log file and reset position tracking."""
        if self.log_file is None:
            return
        try:
            with open(self.log_file, 'w') as f:
                pass
            self._last_read_position = 0
            if self.verbose:
                print(f"[GATProvider] Cleared log file")
        except Exception as e:
            if self.verbose:
                print(f"[GATProvider] Error clearing log file: {e}")

    def _poll_kafka(self, timeout_ms: int = 1000) -> List[Dict]:
        """
        Poll metrics from Kafka consumer.

        Returns:
            List of log entries in the format expected by _process_log_entry()
        """
        if self._kafka_consumer is None:
            return []

        logs = []
        try:
            messages = self._kafka_consumer.poll_batch(timeout_ms=timeout_ms, max_records=10)
            for msg in messages:
                # Use to_dict() - produces exact format expected by _process_log_entry
                # Wrap in {'data': ...} format to match file-based log entry structure
                log_entry = {'data': msg.to_dict()}
                logs.append(log_entry)
        except Exception as e:
            if self.verbose:
                print(f"[GATProvider] Kafka poll error: {e}")
        return logs

    def get_current_state(self) -> Optional[Dict[str, Any]]:
        """
        Get the current EWMA state without waiting for batch completion.

        Returns:
            State dict or None if no data
        """
        if not self.ewma_state:
            return None

        result = {
            'nodes': {},
            'ap_order': [],
            'edges': [],
            'batch_count': self.current_batch_count,
            'batch_number': self.total_batches,
            'connection_metrics': {},  # Per-AP connection metrics for optuna-style reward
            'sim_time_range': None  # Simulation time info for log correlation
        }

        # Add sim_time_range if we have sim_times from this batch
        # effect_at_sim_time is the join key for merging with NS3 logs
        if self.batch_sim_times:
            result['sim_time_range'] = {
                'start': round(min(self.batch_sim_times), 3),
                'end': round(max(self.batch_sim_times), 3),
                'effect_at': round(max(self.batch_sim_times) + 1.0, 3)  # Action visible at next sim second
            }

        # Get sorted list of AP BSSIDs for consistent ordering
        ap_list = sorted(self.ewma_state.keys())
        result['ap_order'] = ap_list

        # Build nodes dict
        for ap_bssid in ap_list:
            state = self.ewma_state[ap_bssid]
            result['nodes'][ap_bssid] = {
                'channel': self.ap_channels.get(ap_bssid, 0),  # Last value (categorical)
                'power': round(state.get('power', 17.0), 2),   # EWMA'd power
                'channel_utilization': round(state.get('channel_utilization', 0.0), 4),
                'num_aps_on_channel': round(state.get('num_aps_on_channel', 0), 2),
                'num_clients': round(state.get('num_clients', 0), 2)
            }

        # Build edge matrix (RSSI between APs)
        # Diagonal = 0.0 (self), off-diagonal = RSSI (or RSSI_FLOOR if no edge exists)
        for i, ap_i in enumerate(ap_list):
            row = []
            for j, ap_j in enumerate(ap_list):
                if i == j:
                    row.append(0.0)  # Diagonal: self-connection = 0
                else:
                    rssi = self.edge_ewma_state.get((ap_i, ap_j))
                    # If pair exists in edge_ewma_state, use it; otherwise use RSSI_FLOOR
                    # (This should not happen if _extract_rssi_between_aps works correctly,
                    #  but we ensure RSSI_FLOOR is always used for missing edges)
                    if rssi is not None:
                        row.append(round(rssi, 2))
                    else:
                        # Missing edge - use RSSI_FLOOR
                        row.append(round(self.RSSI_FLOOR, 2))
            result['edges'].append(row)

        # Build connection_metrics with pre-computed percentile values:
        # - p50_throughput: p50 across logs of (sum of uplink across clients)
        # - p75_loss_rate: p75 across logs of (p95 across clients per log)
        # - p75_rtt: p75 across logs of (p95 across clients per log)
        # - p75_jitter: p75 across logs of (p95 across clients per log)
        for ap_bssid in ap_list:
            # Throughput: p50 across logs of (sum of uplink across clients)
            throughput_logs = self.ap_throughput_per_log.get(ap_bssid, [])
            p50_throughput = float(np.percentile(throughput_logs, 50)) if throughput_logs else 0.0

            # Loss/RTT/Jitter: p75 across logs of (p95 across clients per log)
            loss_p95_logs = self.ap_loss_p95_per_log.get(ap_bssid, [])
            rtt_p95_logs = self.ap_rtt_p95_per_log.get(ap_bssid, [])
            jitter_p95_logs = self.ap_jitter_p95_per_log.get(ap_bssid, [])

            p75_loss = float(np.percentile(loss_p95_logs, 75)) if loss_p95_logs else 0.0
            p75_rtt = float(np.percentile(rtt_p95_logs, 75)) if rtt_p95_logs else 0.0
            p75_jitter = float(np.percentile(jitter_p95_logs, 75)) if jitter_p95_logs else 0.0

            # Store all pre-computed metrics for this AP
            result['connection_metrics'][ap_bssid] = {
                'p50_throughput': round(p50_throughput, 4),
                'p75_loss_rate': round(p75_loss, 4),
                'p75_rtt': round(p75_rtt, 4),
                'p75_jitter': round(p75_jitter, 4)
            }

        return result

    def get_next_state(self, timeout: float = 600.0) -> Optional[Dict[str, Any]]:
        """
        Wait for batch_size logs, compute EWMA, return state, then reset.

        This is the main method to call from main.py.
        Works with both Kafka mode and file mode.

        Args:
            timeout: Maximum time to wait for a complete batch (seconds)

        Returns:
            Dict with 'nodes', 'ap_order', 'edges' or None if timeout
        """
        # Try to connect/initialize if not done
        if not self._initialized:
            self.connect()  # Works for both Kafka and file modes

        # File mode only: CLEAR file at START of batch collection to get FRESH logs only
        # Kafka mode: Not needed - Kafka consumer tracks offset automatically
        if not self._use_kafka:
            self._clear_log_file()

        start_time = time.time()
        mode_str = "Kafka" if self._use_kafka else "File"

        if self.verbose:
            print(f"\n[GATProvider] [{mode_str}] Collecting batch {self.total_batches + 1} "
                  f"(need {self.batch_size} FRESH logs)...")

        while self.current_batch_count < self.batch_size:
            # Check timeout
            if time.time() - start_time > timeout:
                if self.verbose:
                    print(f"[GATProvider] Timeout waiting for batch "
                          f"(got {self.current_batch_count}/{self.batch_size})")
                # Return partial state if we have any data
                if self.current_batch_count > 0:
                    state = self.get_current_state()
                    self.total_batches += 1
                    self.reset_state()
                    return state
                return None

            # Poll from correct source based on mode
            if self._use_kafka:
                logs = self._poll_kafka(timeout_ms=int(self.poll_interval * 1000))
            else:
                logs = self._read_logs_from_file()

            if logs:
                for log_entry in logs:
                    if self._process_log_entry(log_entry):
                        if self.verbose and self.current_batch_count % 5 == 0:
                            print(f"[GATProvider] Progress: {self.current_batch_count}/{self.batch_size}")

                        # Check if batch complete
                        if self.current_batch_count >= self.batch_size:
                            break

            # Small sleep to avoid busy waiting
            if self.current_batch_count < self.batch_size:
                time.sleep(self.poll_interval)

        # Batch complete - get state and reset
        state = self.get_current_state()
        self.total_batches += 1

        if self.verbose:
            print(f"[GATProvider] Batch {self.total_batches} complete: "
                  f"{len(state['nodes'])} APs, {self.current_batch_count} logs processed")

        # Reset EWMA state for next batch
        self.reset_state()

        return state

    def process_kafka_message(self, kafka_msg: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        """
        Process a single Kafka message directly (alternative to file-based).

        Args:
            kafka_msg: Raw Kafka message with 'ap_metrics'

        Returns:
            State dict if batch is complete, None otherwise
        """
        # Wrap in the expected format
        log_entry = {'data': kafka_msg}

        if self._process_log_entry(log_entry):
            if self.current_batch_count >= self.batch_size:
                # Batch complete
                state = self.get_current_state()
                self.total_batches += 1

                if self.verbose:
                    print(f"[GATProvider] Batch {self.total_batches} complete: "
                          f"{len(state['nodes'])} APs")

                self.reset_state()
                return state

        return None


# Keep the old class name for backwards compatibility
GATNodeDataProcessor = GATStateProvider


def main():
    """Standalone mode for testing."""
    parser = argparse.ArgumentParser(description='GAT State Provider (Batched EWMA)')
    parser.add_argument('--log-file', type=str, default=None,
                        help='Path to log file (default: auto-detect)')
    parser.add_argument('--batch-size', type=int, default=6,
                        help='Number of logs per batch (default: 20)')
    parser.add_argument('--ewma-alpha', type=float, default=0.3,
                        help='EWMA smoothing factor (0-1). Default: 0.3')
    parser.add_argument('--poll-interval', type=float, default=0.5,
                        help='Poll interval in seconds. Default: 0.5')
    parser.add_argument('--num-batches', type=int, default=5,
                        help='Number of batches to collect (default: 5)')

    args = parser.parse_args()

    provider = GATStateProvider(
        log_file=args.log_file,
        batch_size=args.batch_size,
        ewma_alpha=args.ewma_alpha,
        poll_interval=args.poll_interval,
        verbose=True
    )

    print("=" * 60)
    print("GAT State Provider - Batched EWMA Mode")
    print("=" * 60)
    print(f"Batch size: {args.batch_size}")
    print(f"EWMA alpha: {args.ewma_alpha}")
    print(f"Collecting {args.num_batches} batches...")
    print("=" * 60)

    for i in range(args.num_batches):
        state = provider.get_next_state(timeout=120.0)

        if state:
            print(f"\n{'='*60}")
            print(f"BATCH {i+1} STATE (EWMA of {state['batch_count']} logs)")
            print("=" * 60)
            print(json.dumps(state, indent=2))
        else:
            print(f"\n[WARN] Batch {i+1} failed or timed out")

    print("\n" + "=" * 60)
    print(f"Total logs processed: {provider.total_logs}")
    print(f"Total batches: {provider.total_batches}")
    print("=" * 60)


if __name__ == '__main__':
    main()
