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
import numpy as np
from pathlib import Path
from typing import Dict, Any, Optional, List
from collections import defaultdict


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
    """

    def __init__(
        self,
        log_file: Optional[str] = None,
        batch_size: int = 6,
        ewma_alpha: float = 0.3,
        poll_interval: float = 0.5,
        settling_time: float = 10.0,
        verbose: bool = True
    ):
        """
        Initialize the GAT State Provider.

        Args:
            log_file: Path to log file (auto-detects if None)
            batch_size: Number of logs to collect before returning state
            ewma_alpha: EWMA smoothing factor (0-1)
            poll_interval: How often to check for new logs (seconds)
            verbose: Enable verbose logging
        """
        self.batch_size = batch_size
        self.ewma_alpha = ewma_alpha
        self.poll_interval = poll_interval
        self.verbose = verbose

        # Log file handling
        if log_file:
            self.log_file = Path(log_file)
        else:
            self.log_file = None

        # Initialize/reset state
        self.reset_state()

        # Track total batches processed
        self.total_batches = 0
        self.total_logs = 0

        # Track file read position (to avoid clearing file prematurely)
        self._last_read_position = 0

        # Connection status
        self._initialized = False

        # RSSI floor value for APs that don't see each other
        self.RSSI_FLOOR = -100.0

        self.settling_time = settling_time

    def reset_state(self):
        """Reset all EWMA state for a fresh batch."""
        self.ewma_state: Dict[str, Dict[str, float]] = {}
        self.edge_ewma_state: Dict[tuple, float] = {}
        self.ap_channels: Dict[str, int] = {}  # Last channel value (categorical)
        self.current_batch_count = 0
        # Per-log metrics for each AP (for percentile calculations across logs)
        # Structure: {ap_bssid: [value_log1, value_log2, ..., value_logN]}
        # Throughput = sum of uplink across clients, then p50 across logs
        self.ap_throughput_per_log: Dict[str, List[float]] = {}
        # Loss/RTT/Jitter = p95 across clients per log, then p75 across logs
        self.ap_loss_p95_per_log: Dict[str, List[float]] = {}
        self.ap_rtt_p95_per_log: Dict[str, List[float]] = {}
        self.ap_jitter_p95_per_log: Dict[str, List[float]] = {}
        # SNR = mean across clients per log, then p50 across logs (for RCPO cost)
        self.ap_snr_mean_per_log: Dict[str, List[float]] = {}
        # Channel width = last value from most recent log (categorical, not EWMA)
        self.ap_channel_widths: Dict[str, int] = {}

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

        # Process each AP
        for ap_bssid, ap_data in ap_metrics.items():
            channel = ap_data.get('channel', 0)
            tx_power = ap_data.get('tx_power_dbm', 17.0)  # Read actual TX power from NS3
            num_clients = ap_data.get('associated_clients', 0)
            scanning_data = ap_data.get('scanning_channel_data', {})

            # Get channel utilization directly from AP data (not from scanning_channel_data)
            channel_util = ap_data.get('channel_utilization', 0.0)

            # Get number of APs on the same channel
            num_aps_on_channel = self._get_num_aps_on_channel(scanning_data, channel)

            # Initialize EWMA state for this AP if needed
            if ap_bssid not in self.ewma_state:
                self.ewma_state[ap_bssid] = {}

            # Update EWMA for each field
            state = self.ewma_state[ap_bssid]
            state['channel_utilization'] = self._ewma_update(
                state.get('channel_utilization'), channel_util / 100.0  # Normalize to 0-1
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
            client_snrs = []  # For RCPO cost function

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

                    # SNR: from AP's view of the client (for RCPO cost)
                    snr = float(metrics.get('ap_view_snr', 0))
                    client_snrs.append(snr)

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

            # SNR: mean across clients for this log (for RCPO cost)
            if ap_bssid not in self.ap_snr_mean_per_log:
                self.ap_snr_mean_per_log[ap_bssid] = []
            mean_snr = np.mean(client_snrs) if client_snrs else 0.0
            self.ap_snr_mean_per_log[ap_bssid].append(mean_snr)

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
            'connection_metrics': {}  # NEW: Per-AP connection metrics for optuna-style reward
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

            # SNR: p50 across logs of (mean across clients per log) - for RCPO cost
            snr_mean_logs = self.ap_snr_mean_per_log.get(ap_bssid, [])
            p50_snr = float(np.percentile(snr_mean_logs, 50)) if snr_mean_logs else 0.0

            # Store all pre-computed metrics for this AP
            result['connection_metrics'][ap_bssid] = {
                'p50_throughput': round(p50_throughput, 4),
                'p75_loss_rate': round(p75_loss, 4),
                'p75_rtt': round(p75_rtt, 4),
                'p75_jitter': round(p75_jitter, 4),
                'p50_snr': round(p50_snr, 4)  # For RCPO cost function
            }

        return result

    def get_next_state(self, timeout: float = 600.0) -> Optional[Dict[str, Any]]:
        """
        Wait for batch_size logs, compute EWMA, return state, then reset.

        This is the main method to call from main.py.

        Args:
            timeout: Maximum time to wait for a complete batch (seconds)

        Returns:
            Dict with 'nodes', 'ap_order', 'edges' or None if timeout
        """
        # Try to initialize if not done
        if not self._initialized:
            self.initialize()

        # CLEAR file at START of batch collection to get FRESH logs only
        # This ensures we don't read stale logs from before the last action
        self._clear_log_file()

        start_time = time.time()

        if self.verbose:
            print(f"\n[GATProvider] Collecting batch {self.total_batches + 1} "
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
                    # DON'T clear log file - keep reading from where we left off
                    return state
                return None

            # Read logs from file
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

        # Reset EWMA state for next batch but DON'T clear log file
        # We keep reading from where we left off (tracked by _last_read_position)
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
