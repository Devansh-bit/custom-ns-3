#!/usr/bin/env python3
"""
Clean Optuna Optimization - No Baseline
Uses initial config as Trial 0, then optimizes
"""

import json
import numpy as np
import optuna
import time
import argparse
import sys
from datetime import datetime
from pathlib import Path
from typing import List, Dict
import pandas as pd
from kafka import KafkaProducer
from kafka.errors import KafkaError
import requests


class TeeLogger:
    """
    Tee-like logger that writes to both console and file
    All print() statements will be captured to the log file
    """
    def __init__(self, log_file):
        self.terminal = sys.stdout
        self.log = open(log_file, 'a', buffering=1)  # Line buffering

    def write(self, message):
        self.terminal.write(message)
        self.log.write(message)

    def flush(self):
        self.terminal.flush()
        self.log.flush()

    def close(self):
        self.log.close()


class NetworkOptimizer:
    # Simulation clock time constraints for optimization (24-hour format, based on simulation time with 10X timescale)
    # START_TIME_HOUR acts as BOTH the offset and the start constraint
    # Simulation clock time = START_TIME_HOUR + (sim_time * 10)
    # Optimization runs from START_TIME_HOUR to END_TIME_HOUR (in simulation clock time)
    START_TIME_HOUR = 0#ation starts at this hour (1pm) - also acts as time offset
    END_TIME_HOUR = 24#mization must stop before this hour in simulation clock (4pm)

    def __init__(self, study_time: int = 120, evaluation_window: int = 5, settling_time: int = 5,
                 config_file: str = "config-simulation.json", api_base_url: str = "http://localhost:8000",
                 simulation_id: str = "sim-001", log_file: str = None):
        self.study_time = study_time
        self.evaluation_window = evaluation_window  # Simulation time window (in seconds)
        self.settling_time = settling_time  # Real time to wait for network to settle (Trial 0 only)
        self.kafka_producer = None
        self.trial_count = 0
        self.baseline_objective = None  # Store Trial 0 result as baseline
        self.best_objective_so_far = float('-inf')  # Track best objective across all trials
        self.planner_version = 0  # Counter for number of times we send improved config
        self.api_base_url = api_base_url  # Base URL for API calls
        self.planner_ids = {}  # Store planner IDs by trial_count: {trial_count: planner_id}
        self.simulation_id = simulation_id  # Configurable simulation ID

        # Load configuration from config-simulation.json
        # This will set: self.num_aps, self.initial_channels, self.initial_tx_power,
        # self.initial_obss_pd, self.ap_bssids, self.available_channels
        self._load_config(config_file)

        # EWMA parameters
        self.ewma_alpha = 0.3

        # Doubly-Robust safety gate parameters
        self.trial_history = []  # Store (state, action, outcome) for DR estimation
        self.dr_confidence_threshold = 0.95  # 95% confidence (balanced)
        self.dr_bootstrap_samples = 100  # Number of bootstrap samples
        self.dr_min_improvement = 0.02  # Minimum 1% improvement required in lower bound

        # Config churn tracking
        self.config_churn_without_dr = 0  # Count of times we beat previous best (naive)
        self.config_churn_with_dr = 0     # Count of times DR gate approved deployment

        # Output directory for logs
        self.output_dir = Path("output_logs")
        self.output_dir.mkdir(exist_ok=True)

        # Use provided log file or auto-detect latest
        if log_file:
            self.log_file = Path(log_file)
        else:
            # Fallback to old behavior: find latest file
            log_files = sorted(self.output_dir.glob("simulation_logs_*.json"))
            if log_files:
                self.log_file = log_files[-1]
            else:
                # Generate a default name if no files exist
                self.log_file = self.output_dir / f"simulation_logs_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"

        print("\n" + "="*80)
        print("NETWORK OPTIMIZER INITIALIZED")
        print("="*80)
        print(f"Timescale: 10X (1s sim time = 10s clock time)")
        print(f"Simulation Start Time: {self.START_TIME_HOUR:02d}:00 (acts as offset)")
        print(f"Simulation End Time: {self.END_TIME_HOUR:02d}:00")
        print(f"Number of APs: {self.num_aps}")
        print(f"Evaluation Window: {evaluation_window}s")
        print(f"API Base URL: {self.api_base_url}")
        print(f"Log File: {self.log_file}")
        print(f"Initial Config - Channels: {self.initial_channels}")
        print(f"Initial Config - TX Power: {self.initial_tx_power} dBm")
        print(f"Initial Config - OBSS-PD: {self.initial_obss_pd} dBm")
        print(f"AP BSSIDs: {self.ap_bssids}")

        # Get current simulation time and display clock time
        current_sim_time = self.get_current_sim_time(max_wait_time=10.0)
        if current_sim_time is not None:
            current_clock = self.sim_time_to_clock(current_sim_time)
            print(f"Current Clock Time: {current_clock}")
        else:
            start_clock = self.sim_time_to_clock(0)
            print(f"Start Clock Time: {start_clock}")

        print("="*80 + "\n")

    def sim_time_to_clock(self, sim_time_seconds: float) -> str:
        """
        Convert simulation time (in seconds) to clock time string with AM/PM format.
        Clock time = START_TIME_HOUR + (sim_time_seconds * 10)
        With 10X timescale: 1 second simulation time = 10 seconds clock time

        Args:
            sim_time_seconds: Simulation time in seconds

        Returns:
            Clock time string in format "HH:MM:SS AM/PM"
        """
        # Total seconds from start of day (START_TIME_HOUR acts as offset)
        start_seconds = self.START_TIME_HOUR * 3600

        # Add simulation time (multiply by 10 for 10X timescale)
        total_seconds = int(start_seconds + (sim_time_seconds * 10))

        # Calculate hours, minutes, seconds
        total_hours = (total_seconds // 3600) % 24  # Wrap around at 24 hours
        minutes = (total_seconds % 3600) // 60
        seconds = total_seconds % 60

        # Convert to 12-hour format with AM/PM
        if total_hours == 0:
            display_hours = 12
            period = "AM"
        elif total_hours < 12:
            display_hours = total_hours
            period = "AM"
        elif total_hours == 12:
            display_hours = 12
            period = "PM"
        else:
            display_hours = total_hours - 12
            period = "PM"

        return f"{display_hours:02d}:{minutes:02d}:{seconds:02d} {period}"

    def _load_config(self, config_file: str):
        """Load configuration from config-simulation.json"""
        try:
            with open(config_file, 'r') as f:
                config = json.load(f)

            # Extract AP configurations
            aps = config.get('aps', [])
            self.num_aps = len(aps)

            if self.num_aps == 0:
                raise ValueError("No APs defined in config file")

            # Extract initial configurations from each AP
            self.initial_channels = []
            self.initial_tx_power = []
            self.initial_obss_pd = []

            for ap in aps:
                lever_config = ap.get('leverConfig', {})
                self.initial_channels.append(lever_config.get('channel', 36))
                self.initial_tx_power.append(lever_config.get('txPower', 20.0))
                self.initial_obss_pd.append(lever_config.get('obsspdThreshold', -50.0))

            # Generate AP BSSIDs dynamically based on number of APs
            # Format: 00:00:00:00:00:01, 00:00:00:00:00:02, etc.
            self.ap_bssids = [f"00:00:00:00:00:{i+1:02x}" for i in range(self.num_aps)]

            # Extract available channels from system_config.scanning_channels
            system_config = config.get('system_config', {})
            scanning_channels = system_config.get('scanning_channels', [])

            # Filter to only 5GHz channels (>= 36) and sort them
            # scanning_channels may include 2.4GHz channels (1, 6, 11) which we don't want
            self.available_channels = sorted([ch for ch in scanning_channels if ch >= 36])

            # Fallback if no 5GHz channels found
            if not self.available_channels:
                print("⚠ Warning: No 5GHz channels in scanning_channels, using defaults")
                self.available_channels = [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112]

            # Extract clock-time (simulation start hour, 1-24)
            self.clock_time_start = system_config.get('clock_time', 12)

        except Exception as e:
            print(f"✗ Error loading config file: {e}")
            print("  Using default configuration.")
            # Fallback to hardcoded defaults (6 APs)
            self.num_aps = 6
            self.initial_channels = [36, 40, 44, 36, 40, 44]
            self.initial_tx_power = [20.0, 20.0, 20.0, 20.0, 20.0, 20.0]
            self.initial_obss_pd = [-50.0, -50.0, -50.0, -50.0, -50.0, -50.0]
            self.ap_bssids = [f"00:00:00:00:00:{i+1:02x}" for i in range(self.num_aps)]
            self.available_channels = [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112]
            self.clock_time_start = 12  # Default to 12:00

    def connect_kafka_producer(self) -> bool:
        """Connect to Kafka producer"""
        if self.kafka_producer is not None:
            return True

        try:
            self.kafka_producer = KafkaProducer(
                bootstrap_servers=['localhost:9092'],
                value_serializer=lambda v: json.dumps(v).encode('utf-8'),
                max_request_size=10485760
            )
            #print("✓ Kafka producer connected")
            return True
        except Exception as e:
            print(f"✗ Failed to connect Kafka producer: {e}")
            return False

    def clear_log_file(self):
        """Clear the log file after collecting logs for the trial"""
        try:
            if not self.log_file.exists():
                print("⚠ No log file found to clear")
                return

            # Clear the file by opening in write mode
            with open(self.log_file, 'w') as f:
                pass
            #print(f"✓ Cleared log file: {self.log_file.name}")
        except Exception as e:
            print(f"✗ Error clearing log file: {e}")


    def get_current_sim_time(self, max_wait_time: float = 6000.0) -> float:
        """
        Get the current simulation time from the latest log entry.

        Args:
            max_wait_time: Maximum real time to wait for a log (default: 30s)

        Returns:
            Current simulation time in seconds, or None if failed
        """
        try:
            start_wait = time.time()

            print(f"Getting current simulation time from log: {self.log_file.name}")

            # Poll until we get at least one log
            while True:
                # if time.time() - start_wait > max_wait_time:
                #     print(f"✗ Timeout: No logs found after {max_wait_time}s")
                #     return None

                try:
                    with open(self.log_file, 'r') as f:
                        lines = f.readlines()
                        if not lines:
                            time.sleep(0.5)
                            continue

                        # Get the last line (most recent log)
                        last_line = lines[-1].strip()
                        log = json.loads(last_line)

                        # Extract sim_time
                        sim_time = log.get('sim_time', 0)
                        if sim_time == 0:
                            # Try data field
                            data = log.get('data', {})
                            sim_time = data.get('sim_time_seconds', 0)

                        if sim_time > 0:
                            return sim_time

                except (FileNotFoundError, json.JSONDecodeError):
                    time.sleep(0.5)
                    continue

                time.sleep(0.5)

        except Exception as e:
            print(f"✗ Error getting simulation time: {e}")
            return None

    def collect_logs_until_sim_time(self, start_sim_time: float, target_sim_time: float, max_wait_time: float = 300.0) -> List[Dict]:
        """
        Collect logs until reaching a target simulation time.

        Args:
            start_sim_time: Starting simulation time (e.g., 20.0s)
            target_sim_time: Target simulation time to reach (e.g., 38.0s)
            max_wait_time: Maximum real time to wait (default: 300s = 5 minutes)

        Returns:
            List of log entries in the time window [start_sim_time, target_sim_time]
        """
        logs = []
        start_wait = time.time()

        try:
            last_progress = 0

            #print(f"Collecting logs until sim_time reaches {target_sim_time:.2f}s...")

            while True:
                # Check if we've exceeded max wait time
                # if time.time() - start_wait > max_wait_time:
                #     print(f"\n⚠ Warning: Max wait time reached, returning {len(logs)} logs collected so far")
                #     break

                # Read all current logs
                collected_logs = []
                try:
                    with open(self.log_file, 'r') as f:
                        for line in f:
                            try:
                                log = json.loads(line.strip())
                                collected_logs.append(log)
                            except json.JSONDecodeError:
                                continue
                except FileNotFoundError:
                    time.sleep(0.5)
                    continue

                if not collected_logs:
                    time.sleep(0.5)
                    continue

                # Check last log's simulation time
                last_log = collected_logs[-1]
                last_sim_time = last_log.get('sim_time', 0)
                if last_sim_time == 0:
                    data = last_log.get('data', {})
                    last_sim_time = data.get('sim_time_seconds', 0)

                if last_sim_time >= target_sim_time:
                    # We've reached the target! Filter logs to only include the time window
                    logs = []
                    for log in collected_logs:
                        log_sim_time = log.get('sim_time', 0)
                        if log_sim_time == 0:
                            data = log.get('data', {})
                            log_sim_time = data.get('sim_time_seconds', 0)

                        # Only include logs within [start_sim_time, target_sim_time]
                        if start_sim_time <= log_sim_time <= target_sim_time:
                            logs.append(log)

                    print(f"\n✓ Collected {len(logs)} log entries")
                    break

                # Update progress (only every 5% to avoid spam)
                if last_sim_time > 0:
                    # Get first log's sim_time for progress calculation
                    first_sim_time = collected_logs[0].get('sim_time', 0)
                    if first_sim_time == 0:
                        data = collected_logs[0].get('data', {})
                        first_sim_time = data.get('sim_time_seconds', 0)

                    if first_sim_time > 0 and first_sim_time < target_sim_time:
                        progress = ((last_sim_time - first_sim_time) / (target_sim_time - first_sim_time)) * 100
                        progress = min(progress, 100)  # Cap at 100%

                        # Only print if progress increased by at least 5%
                        if progress >= last_progress + 5:
                            #print(f"  Progress: {progress:.0f}% (sim_time: {last_sim_time:.2f}s / {target_sim_time:.2f}s)")
                            last_progress = progress

                time.sleep(0.5)

        except Exception as e:
            print(f"✗ Error collecting logs: {e}")
            import traceback
            traceback.print_exc()

        return logs

    def print_average_state_metrics(self, logs: List[Dict]) -> None:
        """
        Calculate and print average EWMA state metrics across all clients.
        Uses the same EWMA calculation as objective function, then averages across clients.
        """
        if not logs:
            print("⚠ No logs to calculate average metrics")
            return

        # Per-client EWMA state tracking (same as calculate_objective)
        client_ewma_states = {}

        # Process logs with per-client EWMA
        for idx, log in enumerate(logs):
            try:
                data = log.get('data', {})

                for ap_bssid, ap_data in data.get('ap_metrics', {}).items():
                    connection_metrics = ap_data.get('connection_metrics', {})

                    for connection_key, metrics in connection_metrics.items():
                        sta_mac = metrics.get('sta_address', '')
                        if not sta_mac:
                            continue

                        # Initialize client state if not exists
                        if sta_mac not in client_ewma_states:
                            client_ewma_states[sta_mac] = {
                                'throughput_uplink': None,
                                'throughput_downlink': None,
                                'throughput_total': None,
                                'packet_loss_rate': None,
                                'retry_rate': None,
                                'tcp_rtt': None,
                            }

                        # Get current values - cap and normalize
                        # Throughput: cap between 0-100 Mbps
                        throughput_uplink = min(max(float(metrics.get('uplink_throughput_mbps', 0)), 0.0), 100.0)
                        throughput_downlink = min(max(float(metrics.get('downlink_throughput_mbps', 0)), 0.0), 100.0)
                        throughput_total = throughput_uplink + throughput_downlink

                        # Packet loss rate: comes as percentage 0-100, cap then divide by 100
                        packet_loss_rate = min(max(float(metrics.get('packet_loss_rate', 0)), 0.0), 100.0) / 100.0

                        # Retry rate: comes as percentage 0-500, cap then divide by 100
                        retry_rate = min(max(float(metrics.get('mac_retry_rate', 0)), 0.0), 500.0) / 100.0

                        # RTT: cap between 0-750 ms
                        tcp_rtt = min(max(float(metrics.get('mean_rtt_latency', 0)), 0.0), 750.0)

                        # Apply EWMA for each metric
                        state = client_ewma_states[sta_mac]

                        state['throughput_uplink'] = throughput_uplink if state['throughput_uplink'] is None else \
                            self.ewma_alpha * throughput_uplink + (1 - self.ewma_alpha) * state['throughput_uplink']

                        state['throughput_downlink'] = throughput_downlink if state['throughput_downlink'] is None else \
                            self.ewma_alpha * throughput_downlink + (1 - self.ewma_alpha) * state['throughput_downlink']

                        state['throughput_total'] = throughput_total if state['throughput_total'] is None else \
                            self.ewma_alpha * throughput_total + (1 - self.ewma_alpha) * state['throughput_total']

                        state['packet_loss_rate'] = packet_loss_rate if state['packet_loss_rate'] is None else \
                            self.ewma_alpha * packet_loss_rate + (1 - self.ewma_alpha) * state['packet_loss_rate']

                        state['retry_rate'] = retry_rate if state['retry_rate'] is None else \
                            self.ewma_alpha * retry_rate + (1 - self.ewma_alpha) * state['retry_rate']

                        state['tcp_rtt'] = tcp_rtt if state['tcp_rtt'] is None else \
                            self.ewma_alpha * tcp_rtt + (1 - self.ewma_alpha) * state['tcp_rtt']

            except Exception as e:
                continue

        if not client_ewma_states:
            print("⚠ No valid clients found in logs")
            return


    def calculate_per_ap_metrics(self, logs: List[Dict]) -> Dict:
        """
        Calculate per-AP metrics using the same EWMA approach as objective function.
        Returns a dictionary with per-AP percentile metrics.

        Returns:
            {
                'bssid1': {
                    'p15_throughput': float,
                    'p90_throughput': float,
                    'p90_latency': float
                },
                ...
            }
        """
        print(f"\n{'='*60}")
        print("CALCULATING PER-AP METRICS FOR API CALL")
        print(f"{'='*60}")

        # Per-client EWMA state tracking (same as calculate_objective)
        client_ewma_states = {}
        client_ap_mapping = {}

        # Process logs with per-client EWMA
        for idx, log in enumerate(logs):
            try:
                data = log.get('data', {})

                for ap_bssid, ap_data in data.get('ap_metrics', {}).items():
                    connection_metrics = ap_data.get('connection_metrics', {})

                    for connection_key, metrics in connection_metrics.items():
                        sta_mac = metrics.get('sta_address', '')
                        if not sta_mac:
                            continue

                        if sta_mac not in client_ewma_states:
                            client_ewma_states[sta_mac] = {
                                'throughput_total': None,
                                'tcp_rtt': None,
                            }

                        current_bssid = metrics.get('ap_address', ap_bssid)
                        if current_bssid:
                            client_ap_mapping[sta_mac] = current_bssid

                        # Get current values
                        throughput_uplink = float(metrics.get('uplink_throughput_mbps', 0))
                        throughput_downlink = float(metrics.get('downlink_throughput_mbps', 0))
                        throughput_total = throughput_uplink + throughput_downlink
                        tcp_rtt = float(metrics.get('mean_rtt_latency', 0))

                        # Apply EWMA
                        state = client_ewma_states[sta_mac]

                        state['throughput_total'] = throughput_total if state['throughput_total'] is None else \
                            self.ewma_alpha * throughput_total + (1 - self.ewma_alpha) * state['throughput_total']

                        state['tcp_rtt'] = tcp_rtt if state['tcp_rtt'] is None else \
                            self.ewma_alpha * tcp_rtt + (1 - self.ewma_alpha) * state['tcp_rtt']

            except Exception as e:
                print(f"Warning: Error processing log entry {idx}: {e}")
                continue

        # Group clients by AP
        ap_client_states = {}
        for sta_mac, state in client_ewma_states.items():
            current_bssid = client_ap_mapping.get(sta_mac)
            if not current_bssid or current_bssid == "00:00:00:00:00:00":
                continue

            if current_bssid not in ap_client_states:
                ap_client_states[current_bssid] = []

            ap_client_states[current_bssid].append(state)

        # Calculate per-AP percentiles
        per_ap_metrics = {}

        for bssid, client_states in ap_client_states.items():
            if not client_states:
                continue

            # Extract final EWMA states for all clients of this AP
            throughputs = [s['throughput_total'] for s in client_states if s['throughput_total'] is not None]
            rtts = [s['tcp_rtt'] for s in client_states if s['tcp_rtt'] is not None]

            # Calculate percentiles
            p15_throughput = np.percentile(throughputs, 15) if throughputs else 0.0
            p90_throughput = np.percentile(throughputs, 90) if throughputs else 0.0
            p90_latency = np.percentile(rtts, 90) if rtts else 0.0

            per_ap_metrics[bssid] = {
                'p15_throughput': p15_throughput,
                'p90_throughput': p90_throughput,
                'p90_latency': p90_latency
            }

            print(f"\nAP: {bssid}")
            print(f"  Clients: {len(client_states)}")
            print(f"  p15 Throughput: {p15_throughput:.4f} Mbps")
            print(f"  p90 Throughput: {p90_throughput:.4f} Mbps")
            print(f"  p90 Latency: {p90_latency:.4f} ms")

        print(f"\n{'='*60}\n")
        return per_ap_metrics

    def calculate_objective(self, logs: List[Dict], channels: List[int] = None,
                           tx_power: List[float] = None, obss_pd: List[float] = None) -> float:
        """Calculate objective function from logs using per-client EWMA and per-AP percentiles"""
        print(f"\n{'='*60}")
        print("CALCULATING OBJECTIVE")
        print(f"{'='*60}")

        # Per-client EWMA state tracking
        # Structure: {sta_mac: {metric_name: ewma_value}}
        client_ewma_states = {}

        # Track which AP each client is connected to in each log
        # Structure: {sta_mac: current_bssid}
        client_ap_mapping = {}

        sample_count = 0

        # Process logs with per-client EWMA
        for idx, log in enumerate(logs):
            try:
                # Save first good log as sample for report
                if idx == 0 and log.get('data', {}).get('ap_metrics'):
                    import json
                    with open('sample_json.json', 'w') as f:
                        json.dump(log, f, indent=2)
                 #   print("✓ Saved sample JSON to sample_json.json")

                data = log.get('data', {})

                # NEW FORMAT: Process ap_metrics -> connection_metrics
                for ap_bssid, ap_data in data.get('ap_metrics', {}).items():
                    connection_metrics = ap_data.get('connection_metrics', {})

                    # Process each connection (STA->AP pair)
                    for connection_key, metrics in connection_metrics.items():
                        # Extract STA MAC from connection key format "STA->AP"
                        sta_mac = metrics.get('sta_address', '')
                        if not sta_mac:
                            continue

                        # Initialize client state if not exists
                        if sta_mac not in client_ewma_states:
                            client_ewma_states[sta_mac] = {
                                'throughput_uplink': None,
                                'throughput_downlink': None,
                                'throughput_total': None,
                                'packet_loss_rate': None,
                                'retry_rate': None,
                                'tcp_rtt': None,
                                'retries': None,
                            }

                        # Update AP mapping - use ap_address from metrics
                        current_bssid = metrics.get('ap_address', ap_bssid)
                        if current_bssid:
                            client_ap_mapping[sta_mac] = current_bssid

                        # Get current values - cap and normalize
                        # Throughput: cap between 0-100 Mbps
                        throughput_uplink = min(max(float(metrics.get('uplink_throughput_mbps', 0)), 0.0), 100.0)
                        throughput_downlink = min(max(float(metrics.get('downlink_throughput_mbps', 0)), 0.0), 100.0)
                        throughput_total = throughput_uplink + throughput_downlink

                        # Packet loss rate: comes as percentage 0-100, cap then divide by 100
                        packet_loss_rate = min(max(float(metrics.get('packet_loss_rate', 0)), 0.0), 100.0) / 100.0

                        # Retry rate: comes as percentage 0-500, cap then divide by 100
                        retry_rate = min(max(float(metrics.get('mac_retry_rate', 0)), 0.0), 500.0) / 100.0

                        # RTT: cap between 0-750 ms
                        tcp_rtt = min(max(float(metrics.get('mean_rtt_latency', 0)), 0.0), 750.0)

                        # For compatibility, store retry_rate as retries (same value)
                        retries = retry_rate

                        # Apply EWMA for each metric
                        state = client_ewma_states[sta_mac]

                        state['throughput_uplink'] = throughput_uplink if state['throughput_uplink'] is None else \
                            self.ewma_alpha * throughput_uplink + (1 - self.ewma_alpha) * state['throughput_uplink']

                        state['throughput_downlink'] = throughput_downlink if state['throughput_downlink'] is None else \
                            self.ewma_alpha * throughput_downlink + (1 - self.ewma_alpha) * state['throughput_downlink']

                        state['throughput_total'] = throughput_total if state['throughput_total'] is None else \
                            self.ewma_alpha * throughput_total + (1 - self.ewma_alpha) * state['throughput_total']

                        state['packet_loss_rate'] = packet_loss_rate if state['packet_loss_rate'] is None else \
                            self.ewma_alpha * packet_loss_rate + (1 - self.ewma_alpha) * state['packet_loss_rate']

                        state['retry_rate'] = retry_rate if state['retry_rate'] is None else \
                            self.ewma_alpha * retry_rate + (1 - self.ewma_alpha) * state['retry_rate']

                        state['tcp_rtt'] = tcp_rtt if state['tcp_rtt'] is None else \
                            self.ewma_alpha * tcp_rtt + (1 - self.ewma_alpha) * state['tcp_rtt']

                        state['retries'] = retries if state['retries'] is None else \
                            self.ewma_alpha * retries + (1 - self.ewma_alpha) * state['retries']

                        sample_count += 1

            except Exception as e:
                print(f"Warning: Error processing log entry {idx}: {e}")
                continue

        # Now calculate per-AP metrics using percentiles
        # Group clients by AP
        ap_client_states = {}  # {bssid: [list of client states]}

        for sta_mac, state in client_ewma_states.items():
            current_bssid = client_ap_mapping.get(sta_mac)
            if not current_bssid or current_bssid == "00:00:00:00:00:00":
                continue

            if current_bssid not in ap_client_states:
                ap_client_states[current_bssid] = []

            ap_client_states[current_bssid].append(state)

        # Calculate objective function parameters per AP
        # Normalization constants (must match capping ranges)
        MAX_P50_THROUGHPUT = 100.0  # Mbps (throughput capped at 0-100)
        MAX_P95_RETRY_RATE = 5.0    # retry_rate capped at 0-500%, then /100 = 0-5
        MAX_P95_LOSS_RATE = 1.0     # loss_rate capped at 0-100%, then /100 = 0-1
        MAX_P95_RTT = 750.0         # ms (RTT capped at 0-750)

        # Store per-AP metrics for later averaging
        ap_p50_throughputs = []
        ap_p95_retry_rates = []
        ap_p95_loss_rates = []
        ap_p95_rtts = []

        # Get last log for AP-level metrics
        last_log = logs[-1] if logs else {}
        ap_metrics_data = last_log.get('data', {}).get('ap_metrics', {})

        # Print Per-AP Client Distribution (including APs with 0 clients)
        print(f"\nPer-AP Client Distribution:")
        for bssid in self.ap_bssids:
            client_count = len(ap_client_states.get(bssid, []))
            print(f"  {bssid}: {client_count} clients")

        print(f"\n{'='*60}")
        print("PER-AP METRICS")
        print(f"{'='*60}")

        for idx, (bssid, client_states) in enumerate(ap_client_states.items()):
            if not client_states:
                continue

            # Extract final EWMA states for all clients of this AP
            throughputs = [s['throughput_total'] for s in client_states if s['throughput_total'] is not None]
            retry_rates = [s['retry_rate'] for s in client_states if s['retry_rate'] is not None]
            loss_rates = [s['packet_loss_rate'] for s in client_states if s['packet_loss_rate'] is not None]
            rtts = [s['tcp_rtt'] for s in client_states if s['tcp_rtt'] is not None]

            # Calculate percentiles (use defaults if no data)
            p50_throughput = np.percentile(throughputs, 50) if throughputs else 0.0
            p95_retry_rate = np.percentile(retry_rates, 95) if retry_rates else 0.0
            p95_loss_rate = np.percentile(loss_rates, 95) if loss_rates else 0.0
            p95_rtt = np.percentile(rtts, 95) if rtts else 0.0

            # Store for global averaging
            ap_p50_throughputs.append(p50_throughput)
            ap_p95_retry_rates.append(p95_retry_rate)
            ap_p95_loss_rates.append(p95_loss_rate)
            ap_p95_rtts.append(p95_rtt)

            # Get AP-level metrics from last log
            ap_data = ap_metrics_data.get(bssid, {})

            # Get channel from trial parameters if available, otherwise from logs
            ap_idx = self.ap_bssids.index(bssid) if bssid in self.ap_bssids else idx
            channel = channels[ap_idx] if channels and ap_idx < len(channels) else ap_data.get('channel', 'N/A')

            channel_util = ap_data.get('channel_utilization', 0.0)
            assoc_clients = ap_data.get('associated_clients', 0)
            bytes_sent = ap_data.get('bytes_sent', 0)
            bytes_received = ap_data.get('bytes_received', 0)

            # Get RSSI, SNR, latency, jitter from connection metrics
            connection_metrics = ap_data.get('connection_metrics', {})
            rssi_values = []
            snr_values = []
            latency_values = []
            jitter_values = []

            for conn_key, metrics in connection_metrics.items():
                ap_rssi = metrics.get('ap_view_rssi', 0.0)
                ap_snr = metrics.get('ap_view_snr', 0.0)
                latency = metrics.get('mean_rtt_latency', 0.0)
                jitter = metrics.get('jitter_ms', 0.0)

                if ap_rssi != 0.0:
                    rssi_values.append(ap_rssi)
                if ap_snr != 0.0:
                    snr_values.append(ap_snr)
                if latency > 0:
                    latency_values.append(latency)
                if jitter > 0:
                    jitter_values.append(jitter)

            avg_rssi = np.mean(rssi_values) if rssi_values else None
            avg_snr = np.mean(snr_values) if snr_values else None
            avg_latency = np.mean(latency_values) if latency_values else None
            avg_jitter = np.mean(jitter_values) if jitter_values else None

            print(f"\nAP: {bssid}")
            print(f"  Clients: {len(client_states)}")
            print(f"  Channel: {channel}")
            print(f"  Channel Utilization: {channel_util:.2f}")
            print(f"  Bytes Sent: {bytes_sent}")
            print(f"  Bytes Received: {bytes_received}")

            # Print RSSI, SNR, Latency, Jitter - show N/A if no data
            rssi_str = f"{avg_rssi:.1f} dBm" if avg_rssi is not None else "N/A"
            snr_str = f"{avg_snr:.1f} dB" if avg_snr is not None else "N/A"
            latency_str = f"{avg_latency:.1f} ms" if avg_latency is not None else "N/A"
            jitter_str = f"{avg_jitter:.1f} ms" if avg_jitter is not None else "N/A"

            print(f"  RSSI: {rssi_str}")
            print(f"  SNR: {snr_str}")
            print(f"  Latency: {latency_str}")
            print(f"  Jitter: {jitter_str}")
            print(f"  p50 Throughput: {p50_throughput:.4f} Mbps")
            print(f"  p95 Retry Rate: {p95_retry_rate:.4f}")
            print(f"  p95 Loss Rate: {p95_loss_rate:.4f}")
            print(f"  p95 RTT: {p95_rtt:.4f} ms")

        # Calculate global metrics by averaging across all APs
        if ap_p50_throughputs:
            global_p50_throughput = np.mean(ap_p50_throughputs)
            global_p95_retry_rate = np.mean(ap_p95_retry_rates)
            global_p95_loss_rate = np.mean(ap_p95_loss_rates)
            global_p95_rtt = np.mean(ap_p95_rtts)
        else:
            print("\n⚠ Warning: No valid AP metrics found, using defaults")
            global_p50_throughput = 0.0
            global_p95_retry_rate = 0.0
            global_p95_loss_rate = 0.0
            global_p95_rtt = 0.0

        print(f"\n{'='*60}")
        print("GLOBAL METRICS (Averaged)")
        print(f"{'='*60}")
        print(f"Global p50 Throughput: {global_p50_throughput:.4f} Mbps")
        print(f"Global p95 Retry Rate: {global_p95_retry_rate:.4f}")
        print(f"Global p95 Loss Rate: {global_p95_loss_rate:.4f}")
        print(f"Global p95 RTT: {global_p95_rtt:.4f} ms")

        # Normalize by dividing by max values
        norm_throughput = global_p50_throughput / MAX_P50_THROUGHPUT
        norm_retry_rate = global_p95_retry_rate / MAX_P95_RETRY_RATE
        norm_loss_rate = global_p95_loss_rate / MAX_P95_LOSS_RATE
        norm_rtt = global_p95_rtt / MAX_P95_RTT

        print(f"\n{'='*60}")
        print("NORMALIZED METRICS")
        print(f"{'='*60}")
        print(f"Normalized p50 Throughput: {norm_throughput:.4f}")
        print(f"Normalized p95 Retry Rate: {norm_retry_rate:.4f}")
        print(f"Normalized p95 Loss Rate: {norm_loss_rate:.4f}")
        print(f"Normalized p95 RTT: {norm_rtt:.4f}")

        # Calculate final objective function
        # Formula: 0.35*p50_throughput + 0.1*(1-p95_retry) + 0.35*(1-p95_loss) + 0.2*(1-p95_rtt)
        overall_objective = (
            0.35 * norm_throughput +
            0.10 * (1 - norm_retry_rate) +
            0.35 * (1 - norm_loss_rate) +
            0.20 * (1 - norm_rtt)
        )

        print(f"\n{'='*60}")
        print("OBJECTIVE FUNCTION CALCULATION")
        print(f"{'='*60}")
        print(f"Throughput component:  {0.35 * norm_throughput:.4f}")
        print(f"Retry component:      {0.10 * (1 - norm_retry_rate):.4f}")
        print(f"Loss component:        {0.35 * (1 - norm_loss_rate):.4f}")
        print(f"RTT component:        {0.20 * (1 - norm_rtt):.4f}")

        print(f"\n{'='*60}")
        print(f"SUMMARY")
        print(f"{'='*60}")
        print(f"Logs processed: {len(logs)}")
        print(f"EWMA samples: {sample_count}")
        print(f"Total clients tracked: {len(client_ewma_states)}")
        print(f"APs with clients: {len(ap_p50_throughputs)}")

        print(f"\n{'*'*60}")
        print(f"OVERALL OBJECTIVE VALUE: {overall_objective:.4f}")
        print(f"{'*'*60}\n")

        return overall_objective

    def get_optuna_propensity(self, trial, action_params: Dict) -> float:
        """
        Estimate propensity score using trial history similarity.

        Instead of computing tiny probabilities from the parameter space,
        we estimate propensity based on how similar this action is to
        previously tried actions in the optimization history.

        Args:
            trial: Optuna trial object
            action_params: Dict with 'channels', 'tx_power', 'obss_pd'

        Returns:
            Propensity score π(a|s) - normalized similarity to trial history
        """
        # If we have no history yet, return uniform propensity
        if len(self.trial_history) <= 1:
            return 0.5  # Neutral propensity for first trials

        # Calculate similarity to each historical trial
        # Using normalized L2 distance in parameter space
        similarities = []

        for hist_trial in self.trial_history[:-1]:  # Exclude current trial
            hist_action = hist_trial['action']

            # Channel similarity (discrete): fraction of APs with same channel
            channel_matches = sum(
                1 for i in range(self.num_aps)
                if action_params['channels'][i] == hist_action['channels'][i]
            )
            channel_sim = channel_matches / self.num_aps

            # TX Power similarity (continuous): normalized distance
            tx_diffs = [
                abs(action_params['tx_power'][i] - hist_action['tx_power'][i]) / 12.0
                for i in range(self.num_aps)
            ]
            tx_sim = 1.0 - (sum(tx_diffs) / self.num_aps)

            # OBSS-PD similarity (continuous): normalized distance
            obss_diffs = [
                abs(action_params['obss_pd'][i] - hist_action['obss_pd'][i]) / 20.0
                for i in range(self.num_aps)
            ]
            obss_sim = 1.0 - (sum(obss_diffs) / self.num_aps)

            # Combined similarity (average of all three)
            overall_sim = (channel_sim + tx_sim + obss_sim) / 3.0
            similarities.append(overall_sim)

        # Propensity = average similarity to historical trials
        # Higher similarity → higher propensity (more likely to be selected)
        # Range: [0, 1] instead of [0, 10^-11]
        avg_similarity = np.mean(similarities) if similarities else 0.5

        # Transform to propensity: add small constant to avoid zero
        # π(a|s) = 0.1 + 0.9 × similarity
        # This gives range [0.1, 1.0] which is numerically stable
        propensity = 0.1 + 0.9 * avg_similarity

        return propensity

    def compute_doubly_robust_uplift(self, trial, action_params: Dict,
                                     baseline_objective: float,
                                     candidate_objective: float) -> tuple:
        """
        Compute doubly-robust (DR) uplift estimate with confidence bound.

        Uses the DR estimator from the theory:
        V_DR(a*) = (1/N) * Σ[G(s_i, a*) + I{a_i=a*} * (1/π(a*|s_i)) * (Y_i - G(s_i, a*))]

        Where:
        - G(s, a*) is the outcome model (surrogate prediction) = candidate_objective
        - π(a*|s) is the propensity score from Optuna's TPE sampler
        - Y_i is the actual observed outcome = candidate_objective
        - I{a_i=a*} is indicator if action was taken (=1 for current candidate)

        Args:
            trial: Optuna trial object
            action_params: Dict with 'channels', 'tx_power', 'obss_pd'
            baseline_objective: Baseline objective value (a_0)
            candidate_objective: Candidate objective value (a*)

        Returns:
            (dr_uplift, lower_confidence_bound, propensity_score)
        """
        # Get propensity score π(a*|s_i) from Optuna's sampling model
        propensity = self.get_optuna_propensity(trial, action_params)

        # Outcome model G(s_i, a*): Use the candidate objective as surrogate
        # This is our prediction of what would happen if we deployed a*
        surrogate_outcome = candidate_objective

        # Actual observed outcome Y_i for this specific trial
        actual_outcome = candidate_objective

        # ACTUAL DR FORMULA IMPLEMENTATION
        # V_DR(a*) = G(s, a*) + (1/π(a*|s)) × (Y - G(s, a*))

        if len(self.trial_history) > 2:
            historical_objectives = [t['outcome'] for t in self.trial_history[:-1]]

            # G(s, a*) = Outcome model (use historical mean as prediction)
            G_prediction = np.mean(historical_objectives)

            # Y = Actual observed outcome for this candidate
            Y_actual = candidate_objective

            # DR Estimator formula:
            # V_DR = G(s, a*) + (1/π(a*|s)) × (Y - G(s, a*))
            importance_weight = 1.0 / propensity
            prediction_error = Y_actual - G_prediction

            dr_estimate = G_prediction + importance_weight * prediction_error

            # Debug: Store these for printing
            self._debug_dr = {
                'G_prediction': G_prediction,
                'importance_weight': importance_weight,
                'prediction_error': prediction_error
            }

            # Compute uncertainty for confidence bounds
            # Use historical std weighted by propensity
            hist_std = np.std(historical_objectives)

            # Lower propensity (novel) → higher uncertainty
            # Higher propensity (similar) → lower uncertainty
            adjusted_std = hist_std * importance_weight

            # Bootstrap for confidence intervals
            dr_values = []
            for _ in range(self.dr_bootstrap_samples):
                sample = dr_estimate + np.random.normal(0, adjusted_std)
                dr_values.append(sample)

            mean_dr = np.mean(dr_values)
            std_dr = np.std(dr_values)
        else:
            # Not enough history: use direct estimate
            mean_dr = candidate_objective
            std_dr = 0.05  # 5% uncertainty

        # More lenient confidence bound: 80% confidence (z=1.28)
        z_score = 1.28  # 80% confidence interval (more lenient)
        lower_bound = mean_dr - z_score * std_dr

        # Compute uplift relative to baseline
        dr_uplift = mean_dr - baseline_objective
        uplift_lower_bound = lower_bound - baseline_objective

        return dr_uplift, uplift_lower_bound, propensity

    def doubly_robust_safety_gate(self, trial, action_params: Dict,
                                   baseline_objective: float,
                                   candidate_objective: float) -> tuple:
        """
        Doubly-Robust safety gate to decide if candidate should be deployed to canary.

        This function implements the DR-based causal safety gate that filters
        configurations before deployment. Only configurations with statistically
        significant improvement (lower confidence bound > 0) are allowed through.

        Args:
            trial: Optuna trial object
            action_params: Dict with 'channels', 'tx_power', 'obss_pd'
            baseline_objective: Baseline objective value
            candidate_objective: Candidate objective value

        Returns:
            (should_deploy, dr_uplift, lower_bound, propensity)
        """
        print(f"\n{'='*80}")
        print("DOUBLY-ROBUST SAFETY GATE")
        print(f"{'='*80}")

        # Compute DR uplift with confidence bounds
        dr_uplift, lower_bound, propensity = self.compute_doubly_robust_uplift(
            trial, action_params, baseline_objective, candidate_objective
        )

        # Decision criterion: Lower bound must exceed minimum improvement threshold
        # (which implicitly ensures it's positive if threshold > 0)
        should_deploy = lower_bound >= self.dr_min_improvement

        print(f"Baseline:     {baseline_objective:.4f}")
        print(f"Candidate:    {candidate_objective:.4f}")
        print(f"Uplift:       {candidate_objective - baseline_objective:+.4f} ({((candidate_objective - baseline_objective)/baseline_objective)*100:+.1f}%)")
        print(f"Propensity:   {propensity:.3f}")
        print(f"DR Estimate:  {dr_uplift + baseline_objective:.4f}")
        print(f"80% CI Lower: {lower_bound:+.4f}")
        print(f"")

        if should_deploy:
            print(f"✓ PASSED - Lower bound {lower_bound:+.4f} ≥ threshold {self.dr_min_improvement:+.4f}")
            print(f"  → Deploying to canary")
        else:
            print(f"✗ BLOCKED - Lower bound {lower_bound:+.4f} < threshold {self.dr_min_improvement:+.4f}")
            print(f"  → Insufficient confidence, skipping deployment")

        print(f"{'='*80}\n")

        return should_deploy, dr_uplift, lower_bound, propensity

    def prepare_improvement_report(self, trial, channels: List[int], tx_power: List[float],
                                   obss_pd: List[float], per_ap_metrics: Dict,
                                   new_objective: float, previous_objective: float,
                                   dr_uplift: float = None, dr_lower_bound: float = None,
                                   propensity: float = None) -> Dict:
        """
        Prepare JSON report for API call when we beat the previous best objective.
        This contains the configuration that CAUSED the improvement (not the next config).

        Args:
            trial: Optuna trial object
            channels: Channel configuration that achieved the new best
            tx_power: TX power configuration that achieved the new best
            obss_pd: OBSS-PD configuration that achieved the new best
            per_ap_metrics: Per-AP metrics dictionary from calculate_per_ap_metrics
            new_objective: The new (better) objective value
            previous_objective: The previous best objective value
            dr_uplift: Doubly-robust uplift estimate
            dr_lower_bound: Lower confidence bound for DR uplift
            propensity: Propensity score from Optuna's sampler

        Returns:
            Dictionary ready to be sent via API call
        """
        self.planner_version += 1  # Increment version counter

        # Calculate improvement
        expected_change = new_objective - previous_objective

        # Add DR metrics to report metadata
        dr_metrics = {
            'dr_uplift': dr_uplift if dr_uplift is not None else expected_change,
            'dr_lower_bound': dr_lower_bound if dr_lower_bound is not None else expected_change,
            'propensity_score': propensity if propensity is not None else 0.0,
            'dr_gate_enabled': True
        }

        # Build the report with the same structure as Kafka config
        report = {
            'timestamp_unix': int(time.time()),
            'simulation_id': self.simulation_id,
            'command_type': 'UPDATE_AP_PARAMETERS',
            'dr_metrics': dr_metrics,  # Add DR safety gate metrics
            'ap_parameters': {}
        }

        # Add parameters for each AP with additional metrics
        for i, bssid in enumerate(self.ap_bssids):
            # Get per-AP metrics (default to 0 if AP has no clients)
            ap_metrics = per_ap_metrics.get(bssid, {
                'p15_throughput': 0.0,
                'p90_throughput': 0.0,
                'p90_latency': 0.0
            })

            report['ap_parameters'][bssid] = {
                # Original parameters (the action that caused improvement)
                'tx_power_start_dbm': tx_power[i],
                'tx_power_end_dbm': tx_power[i],
                'cca_ed_threshold_dbm': -82.0,
                'obss_pd': obss_pd[i],
                'rx_sensitivity_dbm': -93.0,
                'channel_number': channels[i],
                'channel_width_mhz': 20,
                'band': 'BAND_5GHZ',
                'primary_20_index': 0,

                # NEW FIELDS for API call
                'change_accepted': True,  # Always true when we iort improvement
                'canery_time': 5,  # Constant value
                'p90_latency': ap_metrics['p90_latency'],
                'p15_throughput': ap_metrics['p15_throughput'],
                'p90_throughput': ap_metrics['p90_throughput'],
                'planner_version': self.planner_version,
                'expected_change': expected_change  # Change in objective function
            }

        print(f"\n{'='*80}")
        print(f"IMPROVEMENT REPORT PREPARED (Planner Version: {self.planner_version})")
        print(f"{'='*80}")
        print(f"Previous Best Objective: {previous_objective:.4f}")
        print(f"New Best Objective: {new_objective:.4f}")
        print(f"Expected Change: {expected_change:+.4f}")
        print(f"")
        print(f"DR Safety Gate Metrics:")
        print(f"  DR Uplift:        {dr_metrics['dr_uplift']:+.4f}")
        print(f"  95% Lower Bound:  {dr_metrics['dr_lower_bound']:+.4f}")
        print(f"  Propensity Score: {dr_metrics['propensity_score']:.6f}")
        print(f"\nConfiguration that achieved this improvement:")
        for i, bssid in enumerate(self.ap_bssids):
            ap_metrics = per_ap_metrics.get(bssid, {})
            print(f"  AP {i+1} ({bssid}):")
            print(f"    Channel: {channels[i]}, TX Power: {tx_power[i]:.1f} dBm, OBSS-PD: {obss_pd[i]:.1f} dBm")
            print(f"    p15_throughput: {ap_metrics.get('p15_throughput', 0):.4f} Mbps")
            print(f"    p90_throughput: {ap_metrics.get('p90_throughput', 0):.4f} Mbps")
            print(f"    p90_latency: {ap_metrics.get('p90_latency', 0):.4f} ms")
        print(f"{'='*80}\n")

        # Make API call to send improvement report
        self.send_improvement_to_api(report)

        return report

    def send_improvement_to_api(self, report: Dict) -> bool:
        """
        Send improvement report to the API endpoint.

        Args:
            report: The improvement report dictionary

        Returns:
            True if successful, False otherwise
        """
        api_url = f"{self.api_base_url}/rrm"

        try:
            # print(f"\n{'='*80}")
            # print(f"SENDING IMPROVEMENT REPORT TO API")
            # print(f"{'='*80}")
            # print(f"API Endpoint: {api_url}")
            # print(f"Planner Version: {self.planner_version}")

            # Make POST request
            response = requests.post(
                api_url,
                json=report,
                headers={'Content-Type': 'application/json'},
                timeout=10  # 10 second timeout
            )

            # Check response
            if response.status_code == 200:
                # print(f"✓ API call successful (Status: {response.status_code})")
                try:
                    response_data = response.json()
                    print(f"  Response: {json.dumps(response_data, indent=2)}")
                except:
                    print(f"  Response: {response.text}")
                print(f"{'='*80}\n")
                return True
            else:
                # print(f"⚠ API call returned non-200 status: {response.status_code}")
                # print(f"  Response: {response.text}")
                # print(f"{'='*80}\n")
                return False

        except requests.exceptions.Timeout:
            # print(f"✗ API call timeout (>10s)")
            # print(f"  Check if API server is running at {self.api_base_url}")
            # print(f"{'='*80}\n")
            return False

        except requests.exceptions.ConnectionError:
            # print(f"✗ API connection error")
            # print(f"  Check if API server is running at {self.api_base_url}")
            # print(f"{'='*80}\n")
            return False

        except Exception as e:
            # print(f"✗ Unexpected error during API call: {e}")
            # import traceback
            # traceback.print_exc()
            # print(f"{'='*80}\n")
            return False

    def send_configuration(self, channels: List[int], tx_power: List[float],
                          obss_pd: List[float]) -> bool:
        """Send configuration to producer via Kafka using proper ApParameters format"""
        if not self.connect_kafka_producer():
            return False

        try:
            # Build proper message format matching kafka_helper.py
            config = {
                'timestamp_unix': int(time.time()),
                'simulation_id': self.simulation_id,
                'command_type': 'UPDATE_AP_PARAMETERS',
                'ap_parameters': {}
            }

            # Build ApParameters for each AP (3 parameters only: channel, txPower, obss-pd)
            for i, bssid in enumerate(self.ap_bssids):
                config['ap_parameters'][bssid] = {
                    'tx_power_start_dbm': tx_power[i],
                    'tx_power_end_dbm': tx_power[i],
                    'cca_ed_threshold_dbm': -82.0,  # Fixed default
                    'obss_pd': obss_pd[i],
                    'rx_sensitivity_dbm': -93.0,  # Fixed default
                    'channel_number': channels[i],
                    'channel_width_mhz': 20,  # Using 20 MHz channels
                    'band': 'BAND_5GHZ',
                    'primary_20_index': 0
                }

            print(f"\nSending Configuration:")
            for i, bssid in enumerate(self.ap_bssids):
                print(f"  AP {i+1} ({bssid}): CH={channels[i]}, TX={tx_power[i]:.1f}dBm, "
                      f"OBSS-PD={obss_pd[i]:.1f}dBm")

            future = self.kafka_producer.send('optimization-commands', key=self.simulation_id.encode('utf-8'), value=config)
            future.get(timeout=10)

            print("✓ Configuration sent\n")
            # CRITICAL DELAY: Allow network to recover after channel switches
            # - Channel switches are staggered over 1.6s (500ms between APs)
            # - STAs have 5s ScanningDelay to build beacon cache
            # - Association process takes 2-3s
            # - Connection stabilization needs 5s
            # - Buffer for safety: 5s
            # Total: 18s ensures STAs complete recovery before metric collection
            time.sleep(18)  # Was 5s - increased to prevent STA disconnection loop
            return True

        except Exception as e:
            print(f"✗ Failed to send configuration: {e}")
            return False

    def generate_planner_id(self, trial_number: int) -> str:
        """
        Generate a 12-digit hexadecimal planner ID from trial number.
        The hash is non-deterministic - uses timestamp to ensure unique ID each time.
        Once generated for a trial_number, the same ID is reused.

        Args:
            trial_number: The trial number to hash (trial_count value)

        Returns:
            12-digit hexadecimal string
        """
        import hashlib

        # Check if we already generated an ID for this trial
        if trial_number in self.planner_ids:
            return self.planner_ids[trial_number]

        # Create non-deterministic hash using trial number and timestamp
        # This ensures each trial gets a unique ID
        hash_input = f"planner_trial_{trial_number}_time_{time.time()}".encode('utf-8')
        hash_obj = hashlib.sha256(hash_input)
        hex_digest = hash_obj.hexdigest()

        # Take first 12 characters and store it
        planner_id = hex_digest[:12]
        self.planner_ids[trial_number] = planner_id

        return planner_id

    def check_time_constraint(self, current_sim_time: float) -> bool:
        """
        Check if current simulation clock time is within allowed optimization window.
        Uses the simulation clock time (START_TIME_HOUR + sim_time*10), not real wall-clock time.

        Args:
            current_sim_time: Current simulation time in seconds

        Returns:
            True if within allowed time, False if outside window
        """
        # Calculate simulation clock hour from simulation time
        # Clock time = START_TIME_HOUR + (sim_time * 10)
        start_seconds = self.START_TIME_HOUR * 3600
        total_seconds = int(start_seconds + (current_sim_time * 10))
        current_clock_hour = (total_seconds // 3600) % 24

        # Check if current simulation clock time is within START_TIME_HOUR and END_TIME_HOUR
        if current_clock_hour < self.START_TIME_HOUR or current_clock_hour >= self.END_TIME_HOUR:
            return False
        return True

    def objective(self, trial: optuna.Trial) -> float:
        """Optuna objective function"""
        self.trial_count += 1

        # Generate planner ID for this trial
        planner_id = self.generate_planner_id(self.trial_count)

        print("\n\n")
        print("#" * 80)
        print(f"PLANNER ID #{planner_id}")
        print("#" * 80)

        try:
            # Get current simulation time first
            start_sim_time = self.get_current_sim_time()
            if start_sim_time is None:
                print("✗ Could not get current simulation time")
                return float('-inf')

            # Check time constraint before starting trial (using simulation clock time)
            if not self.check_time_constraint(start_sim_time):
                current_clock_time = self.sim_time_to_clock(start_sim_time)
                print(f"\n{'='*80}")
                print(f"TIME CONSTRAINT EXCEEDED")
                print(f"{'='*80}")
                print(f"Current simulation clock time: {current_clock_time}")
                print(f"Allowed time window: {self.START_TIME_HOUR:02d}:00 - {self.END_TIME_HOUR:02d}:00 (24-hour format)")
                print(f"Stopping optimization study...")
                print(f"{'='*80}\n")
                trial.study.stop()
                return float('-inf')

        except Exception as e:
            print(f"✗ Error checking time constraint: {e}")
            return float('-inf')

        try:
            # Suggest parameters for all APs (channel, txPower, obss-pd)
            channels = [
                self.available_channels[trial.suggest_int(f'channel_idx_{i}', 0, len(self.available_channels)-1)]
                for i in range(self.num_aps)
            ]

            tx_power = [
                trial.suggest_float(f'tx_power_{i}', 13.0, 25.0)  # Fixed range: 13-25 dBm
                for i in range(self.num_aps)
            ]

            obss_pd = [
                trial.suggest_float(f'obss_pd_{i}', -80.0, -60.0)  # Fixed range: -60 to -20 dBm
                for i in range(self.num_aps)
            ]

            # Wait for network to settle (only for first trial) - REAL TIME
            if self.trial_count == 1:
               # print(f"\n{'='*80}")
                #print(f"WAITING FOR NETWORK TO SETTLE (Trial 0 only)")
                #print(f"{'='*80}\n")
                time.sleep(self.settling_time)

            # Step 1: Calculate target times (start_sim_time already obtained above)
            #print(f"\n{'='*80}")
            #print(f"PREPARING TRIAL #{self.trial_count}")
            #print(f"{'='*80}")

            start_clock_time = self.sim_time_to_clock(start_sim_time)
            target_sim_time = start_sim_time + self.evaluation_window
            target_clock_time = self.sim_time_to_clock(target_sim_time)

            print(f"✓ Current simulation clock time: {start_clock_time}")
            #print(f"  Target simulation time: {target_clock_time}")

            # Step 2: Send configuration
            if not self.send_configuration(channels, tx_power, obss_pd):
                return float('-inf')

            # Step 3: Collect logs until we reach target_sim_time
            print(f"\n{'='*80}")
            print(f"COLLECTING LOGS FOR {self.evaluation_window * 10}s sim time)")
            print(f"{'='*80}\n")
            logs = self.collect_logs_until_sim_time(start_sim_time, target_sim_time)

            if not logs:
                print("✗ No logs collected - stopping study")
                trial.study.stop()
                return float('-inf')

            # Step 4: Print average state metrics from collected logs
            self.print_average_state_metrics(logs)

            # Step 5: Clear log file AFTER collecting logs
            #print(f"\n{'='*80}")
            #print(f"POST-TRIAL CLEANUP")
            #print(f"{'='*80}")
            self.clear_log_file()

            # Step 6: Calculate objective
            objective_value = self.calculate_objective(logs, channels, tx_power, obss_pd)

            # Store trial history for DR estimation
            action_params = {
                'channels': channels,
                'tx_power': tx_power,
                'obss_pd': obss_pd
            }
            self.trial_history.append({
                'trial_num': self.trial_count,
                'action': action_params,
                'outcome': objective_value
            })

            # Store baseline (Trial 0) result
            if self.trial_count == 1:
                self.baseline_objective = objective_value
                self.best_objective_so_far = objective_value

            # Step 6: Check if we beat the previous best objective
            if objective_value > self.best_objective_so_far:
                print(f"NEW BEST OBJECTIVE ACHIEVED!")
                print(f"Previous Best: {self.best_objective_so_far:.4f}")
                print(f"New Best: {objective_value:.4f}")
                print(f"Improvement: {objective_value - self.best_objective_so_far:+.4f}")

                # Track config churn WITHOUT DR gate (naive deployment)
                self.config_churn_without_dr += 1

                # DOUBLY-ROBUST SAFETY GATE
                # Check if this improvement passes the DR confidence test
                should_deploy, dr_uplift, lower_bound, propensity = self.doubly_robust_safety_gate(
                    trial=trial,
                    action_params=action_params,
                    baseline_objective=self.baseline_objective,
                    candidate_objective=objective_value
                )

                # Only deploy to canary if DR gate approves
                if should_deploy:
                    # Track config churn WITH DR gate (approved deployments)
                    self.config_churn_with_dr += 1
                    # Calculate per-AP metrics for the improvement report
                    per_ap_metrics = self.calculate_per_ap_metrics(logs)

                    # Prepare improvement report (contains the config that achieved this)
                    # This will also send it to the API automatically
                    improvement_report = self.prepare_improvement_report(
                        trial=trial,
                        channels=channels,
                        tx_power=tx_power,
                        obss_pd=obss_pd,
                        per_ap_metrics=per_ap_metrics,
                        new_objective=objective_value,
                        previous_objective=self.best_objective_so_far,
                        dr_uplift=dr_uplift,
                        dr_lower_bound=lower_bound,
                        propensity=propensity
                    )

                    # Update baseline since we deployed to canary
                    self.baseline_objective = objective_value
                    print(f"\n✓ Config deployed to canary - baseline updated to {objective_value:.4f}\n")
                else:
                    print(f"\n⚠ Configuration NOT deployed due to DR safety gate rejection")
                    print(f"  Canary baseline remains {self.baseline_objective:.4f}")
                    print(f"  Trial will continue with next candidate\n")

                # Update best_objective_so_far regardless of DR gate result
                # (we track best simulation result separately from what's on canary)
                self.best_objective_so_far = objective_value

            # Cleanup
            del logs
            import gc
            gc.collect()

            return objective_value

        except Exception as e:
            print(f"✗ Trial failed: {e}")
            import traceback
            traceback.print_exc()
            return float('-inf')


def main():
    parser = argparse.ArgumentParser(description='Clean Optuna Optimization')
    parser.add_argument('--study-time', type=int, default=30,
                      help='Study duration in minutes (default: 30)')
    parser.add_argument('--evaluation-window', type=int, default=5,
                      help='Evaluation window in simulation seconds (default: 5)')
    parser.add_argument('--api-url', type=str, default='http://localhost:8000',
                      help='Base URL for API calls (default: http://localhost:8000)')
    parser.add_argument('--simulation-id', type=str, default='sim-001',
                      help='Simulation ID (default: sim-001)')
    parser.add_argument('--log-file', type=str, default=None,
                      help='Path to log file to read metrics from (default: auto-detect latest)')

    args = parser.parse_args()

    # Setup logging to file
    output_dir = Path("output_logs")
    output_dir.mkdir(exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    start_time = NetworkOptimizer.START_TIME_HOUR
    end_time = NetworkOptimizer.END_TIME_HOUR
    log_file = output_dir / f"optuna_optimization_{start_time:02d}h_to_{end_time:02d}h_{timestamp}.txt"

    # Redirect stdout to both console and file
    tee_logger = TeeLogger(log_file)
    sys.stdout = tee_logger

    print("\n" + "=" * 80)
    print("OPTUNA NETWORK OPTIMIZATION (SIMULATION TIME-BASED)")
    print("=" * 80)
    #print(f"Study Time: {args.study_time} minutes")
    print(f"Evaluation Window: {args.evaluation_window}s")
   # print(f"Log File: {log_file}")
    print("=" * 80 + "\n")

    # Note: Time constraint checking is based on simulation clock time, not real wall-clock time
    # The constraint will be checked at the start of each trial using the simulation time

    # Create optimizer
    optimizer = NetworkOptimizer(
        study_time=args.study_time,
        evaluation_window=args.evaluation_window,
        api_base_url=args.api_url,
        simulation_id=args.simulation_id,
        log_file=args.log_file
    )

    # Create Optuna study
    study = optuna.create_study(
        direction='maximize',
        sampler=optuna.samplers.TPESampler(seed=42)
    )

    # Seed with initial configuration as Trial 0
    print("=" * 80)
    print("SEEDING WITH INITIAL CONFIGURATION")
    print("=" * 80)

    baseline_channel_indices = []
    for ch in optimizer.initial_channels:
        baseline_channel_indices.append(optimizer.available_channels.index(ch))
        print(f"✓ Channel {ch} -> index {optimizer.available_channels.index(ch)}")

    # Build trial parameters dynamically based on number of APs
    baseline_params = {}
    for i in range(optimizer.num_aps):
        baseline_params[f'channel_idx_{i}'] = baseline_channel_indices[i]
        baseline_params[f'tx_power_{i}'] = optimizer.initial_tx_power[i]
        baseline_params[f'obss_pd_{i}'] = optimizer.initial_obss_pd[i]

    study.enqueue_trial(baseline_params)

    # Generate planner ID for trial 0
    trial_0_planner_id = optimizer.generate_planner_id(1)  # trial_count starts at 1

    print(f"✓ Initial config enqueued as PLANNER ID #{trial_0_planner_id}")
    print(f"  Channels: {optimizer.initial_channels}")
    print(f"  TX Power: {optimizer.initial_tx_power}")
    print(f"  OBSS-PD: {optimizer.initial_obss_pd}")
    print("=" * 80 + "\n")

    # Start optimization
    print("=" * 80)
    #print(f"STARTING OPTIMIZATION - {args.study_time} minutes")
    #print("=" * 80 + "\n")

    try:
        study.optimize(
            optimizer.objective,
            n_trials=200,
            show_progress_bar=False
        )
    except KeyboardInterrupt:
        print("\n\n✗ Optimization interrupted by user")

    # Print results
    print("\n\n")
    print("=" * 80)
    print("OPTIMIZATION COMPLETE")
    print("=" * 80)

    completed_trials = [t for t in study.trials if t.state == optuna.trial.TrialState.COMPLETE]

    if not completed_trials:
        print("⚠ No trials completed successfully")
        return

    print(f"\nCompleted Trials: {len(completed_trials)}")

    # Trial 0 (initial config) results
    trial_0 = completed_trials[0]
    trial_0_value = trial_0.value
    trial_0_id = optimizer.generate_planner_id(1)  # trial_count starts at 1

    print(f"\n{'='*80}")
    print(f"PLANNER ID #{trial_0_id} (Baseline)")
    print(f"{'='*80}")
    print(f"Channels: {optimizer.initial_channels}")
    print(f"TX Power: {optimizer.initial_tx_power} dBm")
    print(f"OBSS-PD: {optimizer.initial_obss_pd} dBm")
    print(f"Objective Value: {trial_0_value:.4f}")

    # Best trial results
    best_trial = study.best_trial
    best_trial_id = optimizer.generate_planner_id(best_trial.number + 1)  # +1 because trial_count starts at 1

    print(f"\n{'='*80}")
    print("BEST CONFIGURATION FOUND")
    print(f"{'='*80}")
    print(f"Trial Number: {best_trial.number}")
    print(f"PLANNER ID: #{best_trial_id}")

    # Check if best trial has valid parameters
    try:
        best_channels = [
            optimizer.available_channels[best_trial.params[f'channel_idx_{i}']]
            for i in range(optimizer.num_aps)
        ]
        best_tx_power = [best_trial.params[f'tx_power_{i}'] for i in range(optimizer.num_aps)]
        best_obss_pd = [best_trial.params[f'obss_pd_{i}'] for i in range(optimizer.num_aps)]

        print(f"Channels: {best_channels}")
        print(f"TX Power: {[f'{p:.2f}' for p in best_tx_power]} dBm")
        print(f"OBSS-PD: {[f'{o:.2f}' for o in best_obss_pd]} dBm")
        print(f"Objective Value: {best_trial.value:.4f}")

        # Calculate improvement
        improvement = best_trial.value - trial_0_value
        improvement_pct = (improvement / abs(trial_0_value)) * 100 if trial_0_value != 0 else 0

        print(f"\n{'='*80}")
        print("IMPROVEMENT OVER BASELINE")
        print(f"{'='*80}")
        print(f"Baseline (PLANNER ID #{trial_0_id}):  {trial_0_value:.4f}")
        print(f"Best (PLANNER ID #{best_trial_id}):      {best_trial.value:.4f}")
        print(f"Absolute Improvement: {improvement:+.4f}")
        print(f"Percentage Improvement: {improvement_pct:+.2f}%")

        if improvement > 0:
            print(f"✓ OPTIMIZATION SUCCEEDED!")
            print(f"  Found configuration {improvement_pct:.2f}% better than baseline")
        elif improvement == 0:
            print(f"\n⚠ No improvement - Baseline config was optimal")
        else:
            print(f"\n⚠ No improvement over baseline")

    except KeyError as e:
        print(f"⚠ Best trial parameters incomplete or missing: {e}")
        print(f"Best trial was Trial #{best_trial.number}")
        print(f"PLANNER ID: #{best_trial_id}")
        print(f"Objective Value: {best_trial.value:.4f}")

        # If best trial is Trial 0, show baseline config
        if best_trial.number == 0:
            print(f"\nBest configuration is the baseline (PLANNER ID #{trial_0_id}):")
            print(f"Channels: {optimizer.initial_channels}")
            print(f"TX Power: {optimizer.initial_tx_power} dBm")
            print(f"OBSS-PD: {optimizer.initial_obss_pd} dBm")

        improvement = 0
        improvement_pct = 0

    # Config Churn Analysis
    print("\n" + "=" * 80)
    print("CONFIG CHURN ANALYSIS")
    print("=" * 80)
    print(f"WITHOUT DR Gate (Naive deployment on every improvement):")
    print(f"  Deployments: {optimizer.config_churn_without_dr}")
    print(f"")
    print(f"WITH DR Gate (Statistically confident improvements only):")
    print(f"  Deployments: {optimizer.config_churn_with_dr}")
    print(f"")
    if optimizer.config_churn_without_dr > 0:
        churn_reduction = optimizer.config_churn_without_dr - optimizer.config_churn_with_dr
        churn_reduction_pct = (churn_reduction / optimizer.config_churn_without_dr) * 100
        print(f"Reduction in Config Churn:")
        print(f"  Prevented {churn_reduction} unnecessary deployments ({churn_reduction_pct:.1f}% reduction)")
        print(f"  Deployment rate: {optimizer.config_churn_with_dr}/{optimizer.config_churn_without_dr} = {(optimizer.config_churn_with_dr/optimizer.config_churn_without_dr)*100:.1f}%")
    print("=" * 80)

    print("\n" + "=" * 80)
    print("FINAL SUMMARY")
    print("=" * 80)
    print(f"Total trials completed: {len(completed_trials)}")
    print(f"Best objective score: {best_trial.value:.4f}")
    print(f"Improvement over baseline: {improvement_pct:+.2f}%")
    print(f"Log saved to: {log_file}")
    print("=" * 80 + "\n")

    # Close the log file and restore stdout
    tee_logger.close()
    sys.stdout = tee_logger.terminal


if __name__ == "__main__":
    main()
