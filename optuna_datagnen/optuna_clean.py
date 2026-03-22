#!/usr/bin/env python3
"""
Clean Optuna Optimization - NS-3 Integrated Version
Uses Kafka for bidirectional communication with NS-3 simulation.
Receives aligned metrics from NS-3 via Kafka (like SISA data generator).
"""

import os
import sys
import json
import numpy as np
import optuna
import time
import argparse
from datetime import datetime
from pathlib import Path
from typing import List, Dict, Optional, Any, Tuple

# Add paths for imports
SCRIPT_DIR = Path(__file__).parent
sys.path.insert(0, str(SCRIPT_DIR))
sys.path.insert(0, str(SCRIPT_DIR.parent))

from kafka import KafkaProducer, KafkaConsumer
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


class KafkaMetricsReceiver:
    """Receives metrics from NS-3 via Kafka (same as SISA data generator)"""

    def __init__(self, broker: str = 'localhost:9092', topic: str = 'ns3-metrics',
                 group_id: str = 'optuna-optimizer'):
        self.broker = broker
        self.topic = topic
        self.group_id = group_id
        self.consumer = None

    def connect(self) -> bool:
        """Connect to Kafka consumer"""
        try:
            # Use unique group_id to avoid competing with other consumers
            unique_group = f"{self.group_id}-{int(time.time())}"
            self.consumer = KafkaConsumer(
                self.topic,
                bootstrap_servers=[self.broker],
                group_id=unique_group,
                auto_offset_reset='latest',  # Only get new messages
                enable_auto_commit=True,
                consumer_timeout_ms=60000,  # 60 second timeout
                value_deserializer=lambda v: json.loads(v.decode('utf-8'))
            )
            print(f"✓ Connected to Kafka consumer: {self.topic}")
            return True
        except Exception as e:
            print(f"✗ Failed to connect Kafka consumer: {e}")
            return False

    def get_metrics(self, timeout: float = 60.0) -> Optional[Dict]:
        """
        Get ONE metrics response from NS-3.

        Args:
            timeout: Max wait time in seconds

        Returns:
            Metrics dict or None if timeout
        """
        if self.consumer is None:
            if not self.connect():
                return None

        try:
            for message in self.consumer:
                data = message.value
                return data

        except Exception as e:
            print(f"  Error receiving metrics: {e}")
            return None

        return None

    def get_aligned_metrics(self, expected_channels: List[int], ap_bssids: List[str],
                            timeout: float = 120.0, max_attempts: int = 50) -> Optional[Dict]:
        """
        Get metrics from NS-3 that MATCH our expected channels.
        Discards old/mismatched data until we get aligned data.

        Args:
            expected_channels: Our chosen channels for each AP
            ap_bssids: List of AP BSSIDs
            timeout: Max wait time in seconds
            max_attempts: Max number of messages to check

        Returns:
            Metrics dict with matching channels, or None if timeout
        """
        if self.consumer is None:
            if not self.connect():
                return None

        start_time = time.time()
        attempts = 0

        print(f"  Waiting for aligned data (expected channels: {expected_channels})...")

        try:
            for message in self.consumer:
                attempts += 1
                data = message.value

                # Check if channels match
                ap_metrics = data.get('data', {}).get('ap_metrics', {})
                if not ap_metrics:
                    ap_metrics = data.get('ap_metrics', {})

                channels_match = True
                ns3_channels = []

                for i, bssid in enumerate(ap_bssids):
                    ap_data = ap_metrics.get(bssid, {})
                    ns3_ch = ap_data.get('channel', -1)
                    ns3_channels.append(ns3_ch)

                    if ns3_ch != expected_channels[i]:
                        channels_match = False

                if channels_match:
                    print(f"  ✓ Got aligned data after {attempts} attempts")
                    print(f"    NS-3 channels: {ns3_channels}")
                    # Print connection count for debugging
                    total_conns = sum(len(ap_metrics.get(bssid, {}).get('connection_metrics', {})) 
                                     for bssid in ap_bssids)
                    print(f"    Total connections: {total_conns}")
                    return data
                else:
                    print(f"  ✗ Discarding mismatched data (attempt {attempts}): NS-3={ns3_channels}, expected={expected_channels}")

                # Check timeout
                if time.time() - start_time > timeout:
                    print(f"  Timeout waiting for aligned metrics")
                    return None

                if attempts >= max_attempts:
                    print(f"  Max attempts reached, using latest data")
                    return data

        except Exception as e:
            print(f"  Error receiving metrics: {e}")
            return None

        return None

    def seek_to_end(self):
        """
        Seek to end of all partitions to skip stale messages.
        """
        if self.consumer is None:
            return

        try:
            partitions = self.consumer.assignment()

            if not partitions:
                self.consumer.poll(timeout_ms=1000)
                partitions = self.consumer.assignment()

            if partitions:
                self.consumer.seek_to_end()
                print(f"  [Kafka RX] Seeked to end - skipping stale messages")
            else:
                print(f"  [Kafka RX] Warning: No partitions assigned yet")

        except Exception as e:
            print(f"  [Kafka RX] Error seeking to end: {e}")

    def close(self):
        """Close consumer"""
        if self.consumer:
            self.consumer.close()


class KafkaConfigSender:
    """Sends configuration updates to NS-3 via Kafka (same as SISA data generator)"""

    def __init__(self, broker: str = 'localhost:9092',
                 topic: str = 'optimization-commands'):
        self.broker = broker
        self.topic = topic
        self.producer = None

    def connect(self) -> bool:
        """Connect to Kafka producer"""
        try:
            self.producer = KafkaProducer(
                bootstrap_servers=[self.broker],
                value_serializer=lambda v: json.dumps(v).encode('utf-8'),
                max_request_size=10485760,
                acks='all',  # Wait for all replicas to acknowledge
                retries=3,   # Retry on failure
                linger_ms=0  # Send immediately, don't batch
            )
            # Verify connection by checking metadata
            self.producer.bootstrap_connected()
            print(f"  [Kafka TX] ✓ Connected to broker: {self.broker}")
            print(f"  [Kafka TX]   Topic: {self.topic}")
            return True
        except Exception as e:
            print(f"✗ Failed to connect Kafka producer: {e}")
            return False

    def send_config(self, simulation_id: str, channels: List[int],
                   tx_power: List[float], obss_pd: List[float],
                   ap_bssids: List[str]) -> bool:
        """
        Send configuration update to NS-3.

        Args:
            simulation_id: Simulation identifier
            channels: List of channels for each AP
            tx_power: List of TX power values
            obss_pd: List of OBSS-PD threshold values
            ap_bssids: List of AP BSSIDs

        Returns:
            True if sent successfully
        """
        if self.producer is None:
            if not self.connect():
                return False

        # Send one message per AP (NS-3 processes one BSSID at a time)
        print(f"  [Kafka TX] Sending configs for {len(ap_bssids)} APs...")

        for i, bssid in enumerate(ap_bssids):
            # Get proper channel width based on channel number
            channel_width = ChannelUtils.get_channel_width(channels[i])
            
            config = {
                'timestamp_unix': int(time.time()),
                'simulation_id': simulation_id,
                'command_type': 'UPDATE_AP_PARAMETERS',
                'ap_parameters': {
                    bssid: {
                        'tx_power_start_dbm': float(tx_power[i]),
                        'tx_power_end_dbm': float(tx_power[i]),
                        'cca_ed_threshold_dbm': -82.0,
                        'obss_pd': float(obss_pd[i]),
                        'rx_sensitivity_dbm': -93.0,
                        'channel_number': int(channels[i]),
                        'channel_width_mhz': channel_width,
                        'band': 'BAND_5GHZ',
                        'primary_20_index': 0
                    }
                }
            }

            print(f"    AP{i+1} ({bssid}): CH={channels[i]} ({channel_width}MHz), TX={tx_power[i]:.1f}dBm, OBSS-PD={obss_pd[i]:.1f}dBm")

            try:
                future = self.producer.send(
                    self.topic,
                    key=simulation_id.encode('utf-8'),
                    value=config
                )
                # Force flush to ensure message is sent immediately
                self.producer.flush(timeout=5)
                result = future.get(timeout=10)
                print(f"      ✓ Sent to partition {result.partition}, offset {result.offset}")
            except Exception as e:
                print(f"  ✗ Failed to send config for {bssid}: {e}")
                import traceback
                traceback.print_exc()
                return False

        # Final flush to ensure all messages are delivered
        self.producer.flush(timeout=10)
        print(f"  [Kafka TX] ✓ All {len(ap_bssids)} configs sent and flushed successfully")
        return True

    def close(self):
        """Close producer"""
        if self.producer:
            self.producer.close()


class ChannelUtils:
    """Channel mapping utilities (same as SISA data generator)"""

    # Color -> {width -> channel options}
    COLOR_CHANNEL_MAP = {
        0: {20: [36, 40, 44, 48], 40: [38, 46], 80: [42]},
        1: {20: [52, 56, 60, 64], 40: [54, 62], 80: [58]},
        2: {20: [100, 104, 108, 112], 40: [102, 110], 80: [106]},
        3: {20: [116, 120, 124, 128], 40: [118, 126], 80: [122]},
        4: {20: [132, 136, 140, 144], 40: [134, 142], 80: [138]},
        5: {20: [149, 153, 157, 161], 40: [151, 159], 80: [155]},
    }

    # All available channels
    AVAILABLE_CHANNELS = [
        # 20 MHz channels
        36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112,
        116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165,
        # 40 MHz center channels
        38, 46, 54, 62, 102, 110, 118, 126, 134, 142, 151, 159,
        # 80 MHz center channels
        42, 58, 106, 122, 138, 155,
        # 160 MHz center channels
        50, 114
    ]

    # Channel Width Mapping
    CHANNELS_20MHZ = [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165]
    CHANNELS_40MHZ = [38, 46, 54, 62, 102, 110, 118, 126, 134, 142, 151, 159]
    CHANNELS_80MHZ = [42, 58, 106, 122, 138, 155]
    CHANNELS_160MHZ = [50, 114]

    # Combined list of all usable channels (20 + 40 + 80 MHz)
    ALL_CHANNELS_20_40_80 = CHANNELS_20MHZ + CHANNELS_40MHZ + CHANNELS_80MHZ

    @classmethod
    def get_channel_width(cls, channel: int) -> int:
        """Get the channel width in MHz for a given channel number."""
        if channel in cls.CHANNELS_160MHZ:
            return 160
        elif channel in cls.CHANNELS_80MHZ:
            return 80
        elif channel in cls.CHANNELS_40MHZ:
            return 40
        else:
            return 20

    @classmethod
    def get_frequency_range(cls, channel: int) -> Tuple[float, float]:
        """Get the frequency range (min, max) in MHz for a channel."""
        center_freq = 5000.0 + (channel * 5.0)
        width = cls.get_channel_width(channel)
        half_width = width / 2.0
        return (center_freq - half_width, center_freq + half_width)

    @classmethod
    def compute_frequency_overlap(cls, ch1: int, ch2: int) -> float:
        """Compute the frequency overlap between two channels."""
        if ch1 == ch2:
            return 1.0

        min1, max1 = cls.get_frequency_range(ch1)
        min2, max2 = cls.get_frequency_range(ch2)

        overlap_start = max(min1, min2)
        overlap_end = min(max1, max2)

        if overlap_start >= overlap_end:
            return 0.0

        overlap_width = overlap_end - overlap_start
        smaller_width = min(max1 - min1, max2 - min2)
        overlap_ratio = overlap_width / smaller_width

        return min(1.0, overlap_ratio)


class NetworkOptimizer:
    """Network optimizer using Optuna with NS-3 Kafka integration"""

    # Time constraints
    START_TIME_HOUR = 0
    END_TIME_HOUR = 23

    def __init__(self, study_time: int = 120, evaluation_window: int = 5, settling_time: int = 5,
                 config_file: str = "config-simulation.json", api_base_url: str = "http://localhost:8000",
                 simulation_id: str = "sim-001", kafka_broker: str = "localhost:9092",
                 training_data_dir: str = "./training_data"):
        self.study_time = study_time
        self.evaluation_window = evaluation_window
        self.settling_time = settling_time
        self.trial_count = 0
        self.baseline_objective = None
        self.best_objective_so_far = float('-inf')
        self.planner_version = 0
        self.api_base_url = api_base_url
        self.planner_ids = {}
        self.simulation_id = simulation_id
        self.kafka_broker = kafka_broker

        # Kafka components (like SISA data generator)
        self.kafka_receiver = KafkaMetricsReceiver(kafka_broker, topic='ns3-metrics', group_id='optuna-optimizer')
        self.kafka_sender = KafkaConfigSender(kafka_broker, topic='optimization-commands')

        # AP BSSIDs - will be discovered from NS-3 metrics (not hardcoded!)
        self.ap_bssids = None
        self.num_aps = None

        # Load configuration from config file
        self._load_config(config_file)

        # EWMA parameters
        self.ewma_alpha = 0.3

        # Doubly-Robust safety gate parameters
        self.trial_history = []
        self.dr_confidence_threshold = 0.95
        self.dr_bootstrap_samples = 100
        self.dr_min_improvement = 0.02

        # Config churn tracking
        self.config_churn_without_dr = 0
        self.config_churn_with_dr = 0

        # RL-style dataset logging
        self.rl_transitions = []

        # Output directory for logs
        self.output_dir = Path("logs_folder")
        self.output_dir.mkdir(exist_ok=True)

        # Training data output directory (same format as SISA)
        self.training_data_dir = Path("training_data")
        self.training_data_dir.mkdir(parents=True, exist_ok=True)

        # Training data storage (same format as SISA data generator)
        self.training_pairs = []  # List of (X, Y) pairs
        self.ap_coordinates = None  # Will be randomly generated

        # Available channels - use all widths (20, 40, 80 MHz)
        self.available_channels = ChannelUtils.ALL_CHANNELS_20_40_80

        print("\n" + "="*80)
        print("NETWORK OPTIMIZER INITIALIZED (NS-3 KAFKA INTEGRATION)")
        print("="*80)
        print(f"Kafka Broker: {self.kafka_broker}")
        print(f"Kafka Topics: ns3-metrics (receive), optimization-commands (send)")
        print(f"Simulation ID: {self.simulation_id}")
        print(f"Evaluation Window: {evaluation_window}s")
        print(f"API Base URL: {self.api_base_url}")
        print(f"Training Data Dir: {self.training_data_dir}")
        print(f"Available Channels: {len(self.available_channels)} (20/40/80 MHz)")
        print("="*80 + "\n")

    def sim_time_to_clock(self, sim_time_seconds: float) -> str:
        """Convert simulation time to clock time string."""
        start_seconds = self.START_TIME_HOUR * 3600
        total_seconds = int(start_seconds + (sim_time_seconds * 10))
        total_hours = (total_seconds // 3600) % 24
        minutes = (total_seconds % 3600) // 60
        seconds = total_seconds % 60

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

            aps = config.get('aps', [])
            self.num_aps = len(aps)

            if self.num_aps == 0:
                raise ValueError("No APs defined in config file")

            self.initial_channels = []
            self.initial_tx_power = []
            self.initial_obss_pd = []

            for ap in aps:
                lever_config = ap.get('leverConfig', {})
                self.initial_channels.append(lever_config.get('channel', 36))
                self.initial_tx_power.append(lever_config.get('txPower', 20.0))
                self.initial_obss_pd.append(lever_config.get('obsspdThreshold', -50.0))

            # BSSIDs will be discovered from NS-3 - generate placeholder format
            self.ap_bssids = [f"00:00:00:00:00:{i+1:02x}" for i in range(self.num_aps)]

            system_config = config.get('system_config', {})
            scanning_channels = system_config.get('scanning_channels', [])
            # Use all channels (20, 40, 80 MHz)
            self.available_channels = sorted([ch for ch in scanning_channels 
                                             if ch >= 36 and ch in ChannelUtils.ALL_CHANNELS_20_40_80])

            if not self.available_channels:
                # Use all channel widths (20, 40, 80 MHz)
                self.available_channels = ChannelUtils.ALL_CHANNELS_20_40_80

            self.current_channels = list(self.initial_channels)
            self.current_tx_power = list(self.initial_tx_power)
            self.current_obss_pd = list(self.initial_obss_pd)

            print(f"✓ Loaded config: {self.num_aps} APs")
            print(f"  Initial Channels: {self.initial_channels}")
            print(f"  Initial TX Power: {self.initial_tx_power}")
            print(f"  Initial OBSS-PD: {self.initial_obss_pd}")
            print(f"  Available Channels ({len(self.available_channels)} - 20/40/80 MHz): {self.available_channels[:10]}...")

        except Exception as e:
            print(f"✗ Error loading config file: {e}")
            print("  Using default configuration.")
            self.num_aps = 6
            self.initial_channels = [36, 40, 44, 36, 40, 44]
            self.initial_tx_power = [20.0, 20.0, 20.0, 20.0, 20.0, 20.0]
            self.initial_obss_pd = [-50.0, -50.0, -50.0, -50.0, -50.0, -50.0]
            self.ap_bssids = [f"00:00:00:00:00:{i+1:02x}" for i in range(self.num_aps)]
            # Use all channel widths (20, 40, 80 MHz)
            self.available_channels = ChannelUtils.ALL_CHANNELS_20_40_80
            self.current_channels = list(self.initial_channels)
            self.current_tx_power = list(self.initial_tx_power)
            self.current_obss_pd = list(self.initial_obss_pd)

    def verify_kafka_connectivity(self) -> bool:
        """
        Verify Kafka connectivity for both producer and consumer.
        """
        print("\n" + "="*60)
        print("VERIFYING KAFKA CONNECTIVITY")
        print("="*60)

        # Test receiver (consumer)
        print("\n1. Testing Kafka Consumer (metrics receiver)...")
        if not self.kafka_receiver.connect():
            print("   ✗ Failed to connect consumer")
            return False
        print("   ✓ Consumer connected")

        # Test sender (producer)
        print("\n2. Testing Kafka Producer (config sender)...")
        if not self.kafka_sender.connect():
            print("   ✗ Failed to connect producer")
            return False
        print("   ✓ Producer connected")

        # Send a test/ping message to verify producer can write to topic
        print("\n3. Sending test message to optimization-commands topic...")
        try:
            test_config = {
                'timestamp_unix': int(time.time()),
                'simulation_id': self.simulation_id,
                'command_type': 'PING',  # Test message
                'ap_parameters': {}
            }
            future = self.kafka_sender.producer.send(
                self.kafka_sender.topic,
                key=self.simulation_id.encode('utf-8'),
                value=test_config
            )
            self.kafka_sender.producer.flush(timeout=5)
            result = future.get(timeout=10)
            print(f"   ✓ Test message sent to partition {result.partition}, offset {result.offset}")
        except Exception as e:
            print(f"   ✗ Failed to send test message: {e}")
            return False

        print("\n" + "="*60)
        print("KAFKA CONNECTIVITY VERIFIED")
        print("="*60 + "\n")
        return True

    def discover_bssids_from_ns3(self) -> bool:
        """
        Discover actual AP BSSIDs from NS-3 metrics (like SISA data generator).
        Must be called before optimization starts.
        """
        print("\n" + "="*60)
        print("DISCOVERING AP BSSIDs FROM NS-3")
        print("="*60)

        if not self.kafka_receiver.connect():
            print("✗ Failed to connect to Kafka receiver")
            return False

        print("  Waiting for initial NS-3 metrics...")
        metrics = self.kafka_receiver.get_metrics(timeout=60.0)

        if metrics is None:
            print("✗ Failed to get initial metrics from NS-3")
            return False

        # Extract BSSIDs from metrics
        raw_data = metrics.get('data', metrics)
        ap_metrics = raw_data.get('ap_metrics', {})

        if not ap_metrics:
            print("✗ No ap_metrics found in NS-3 data")
            return False

        # Extract BSSIDs with node_ids for sorting
        bssid_node_pairs = []
        for bssid, ap_info in ap_metrics.items():
            node_id = ap_info.get('node_id', 999)
            bssid_node_pairs.append((node_id, bssid))

        bssid_node_pairs.sort(key=lambda x: x[0])
        self.ap_bssids = [pair[1] for pair in bssid_node_pairs]

        print(f"✓ Discovered {len(self.ap_bssids)} BSSIDs from NS-3:")
        for i, bssid in enumerate(self.ap_bssids):
            print(f"    AP{i+1}: {bssid}")

        # Adjust num_aps if needed
        if len(self.ap_bssids) != self.num_aps:
            print(f"  Adjusting num_aps from {self.num_aps} to {len(self.ap_bssids)}")
            self.num_aps = len(self.ap_bssids)

            # Extend/truncate initial configs if needed
            while len(self.initial_channels) < self.num_aps:
                self.initial_channels.append(36)
                self.initial_tx_power.append(20.0)
                self.initial_obss_pd.append(-50.0)
            self.initial_channels = self.initial_channels[:self.num_aps]
            self.initial_tx_power = self.initial_tx_power[:self.num_aps]
            self.initial_obss_pd = self.initial_obss_pd[:self.num_aps]

            self.current_channels = list(self.initial_channels)
            self.current_tx_power = list(self.initial_tx_power)
            self.current_obss_pd = list(self.initial_obss_pd)

        print("="*60 + "\n")
        return True

    def generate_random_coordinates(self,
                                    x_range: Tuple[float, float] = (-10.0, 10.0),
                                    y_range: Tuple[float, float] = (-10.0, 10.0)) -> Dict[str, List[float]]:
        """
        Generate random AP coordinates within specified range (same as SISA).
        """
        coordinates = {}
        for i in range(self.num_aps):
            ap_id = f'AP{i+1}'
            x = round(float(np.random.uniform(x_range[0], x_range[1])), 1)
            y = round(float(np.random.uniform(y_range[0], y_range[1])), 1)
            coordinates[ap_id] = [x, y]
        return coordinates

    def extract_features_from_metrics(self, metrics: Dict,
                                      channels: List[int],
                                      tx_power: List[float]) -> Dict[str, Any]:
        """
        Extract features (X) from NS-3 metrics in SISA format.

        Returns features dict with:
        - per-AP: CHANNEL, CHANNEL_WIDTH_MHZ, AP_POWER, Channel_util, Client_count, num_aps_on_channel
        - rssi_matrix: NxN matrix of AP-to-AP RSSI values
        - channel_gain_matrix: NxN matrix (RSSI - TX_POWER)
        """
        if not metrics:
            return self._default_features(channels, tx_power)

        features = {
            'aps': {},
            'rssi_matrix': [],
            'channel_gain_matrix': []
        }

        raw_data = metrics.get('data', metrics)
        ap_metrics = raw_data.get('ap_metrics', {})

        # Get TX power for each AP from NS-3 data
        ap_tx_powers = []
        for i, bssid in enumerate(self.ap_bssids):
            ap_data = ap_metrics.get(bssid, {})
            ns3_tx_power = ap_data.get('tx_power_dbm', ap_data.get('tx_power', tx_power[i]))
            ap_tx_powers.append(float(ns3_tx_power))

        # Build RSSI matrix (NxN) from NS-3 scanning data
        rssi_matrix = [[-100.0 for _ in range(self.num_aps)] for _ in range(self.num_aps)]

        # Channel utils by channel
        channel_utils_by_channel = {}

        for i, bssid in enumerate(self.ap_bssids):
            ap_data = ap_metrics.get(bssid, {})
            rssi_matrix[i][i] = 0.0  # Diagonal = 0 (self)

            our_channel = channels[i]

            # Get channel_utilization directly from AP metrics
            direct_ch_util = ap_data.get('channel_utilization')
            if direct_ch_util is not None:
                if our_channel not in channel_utils_by_channel:
                    channel_utils_by_channel[our_channel] = []
                channel_utils_by_channel[our_channel].append(float(direct_ch_util))

            # Extract from scanning_channel_data
            scanning = ap_data.get('scanning_channel_data', {})
            for ch_key, ch_info in scanning.items():
                try:
                    ch_num = int(ch_key)
                except ValueError:
                    continue

                ch_util = ch_info.get('channel_utilization')
                if ch_util is not None:
                    if ch_num not in channel_utils_by_channel:
                        channel_utils_by_channel[ch_num] = []
                    channel_utils_by_channel[ch_num].append(float(ch_util))

                # Extract RSSI from neighbors array
                for neighbor in ch_info.get('neighbors', []):
                    n_bssid = neighbor.get('bssid', '')
                    rssi = neighbor.get('rssi', -100)

                    for j, other_bssid in enumerate(self.ap_bssids):
                        if other_bssid == n_bssid:
                            rssi_matrix[i][j] = round(float(rssi), 1)

        # Compute CHANNEL GAIN MATRIX from RSSI and TX power
        channel_gain_matrix = [[0.0 for _ in range(self.num_aps)] for _ in range(self.num_aps)]

        for i in range(self.num_aps):
            for j in range(self.num_aps):
                if i == j:
                    channel_gain_matrix[i][j] = 0.0
                else:
                    rssi_ij = rssi_matrix[i][j]
                    tx_j = ap_tx_powers[j]
                    channel_gain_matrix[i][j] = round(rssi_ij - tx_j, 1)

        # Build AP features
        for i, bssid in enumerate(self.ap_bssids):
            ap_id = f'AP{i+1}'
            ap_data = ap_metrics.get(bssid, {})

            client_count = ap_data.get('associated_clients', 0)
            ap_channel = channels[i]

            # Get MAX channel_util for this AP's channel
            utils_for_channel = channel_utils_by_channel.get(ap_channel, [0.5])
            max_channel_util = max(utils_for_channel) if utils_for_channel else 0.5

            # Count APs on same channel
            num_on_channel = sum(1 for j, ch in enumerate(channels)
                               if ch == channels[i] and j != i)

            features['aps'][ap_id] = {
                'CHANNEL': channels[i],
                'CHANNEL_WIDTH_MHZ': ChannelUtils.get_channel_width(channels[i]),
                'AP_POWER': tx_power[i],
                'Channel_util': round(max_channel_util, 3),
                'Client_count': client_count,
                'num_aps_on_channel': num_on_channel
            }

        features['rssi_matrix'] = rssi_matrix
        features['channel_gain_matrix'] = channel_gain_matrix

        return features

    def _default_features(self, channels: List[int], tx_power: List[float]) -> Dict:
        """Return default features when no metrics available"""
        rssi_matrix = [[-100.0 if i != j else 0.0 for j in range(self.num_aps)]
                       for i in range(self.num_aps)]

        channel_gain_matrix = [[0.0 for _ in range(self.num_aps)] for _ in range(self.num_aps)]
        for i in range(self.num_aps):
            for j in range(self.num_aps):
                if i != j:
                    channel_gain_matrix[i][j] = round(-100.0 - tx_power[j], 1)

        features = {
            'aps': {},
            'rssi_matrix': rssi_matrix,
            'channel_gain_matrix': channel_gain_matrix
        }

        for i in range(self.num_aps):
            ap_id = f'AP{i+1}'
            features['aps'][ap_id] = {
                'CHANNEL': channels[i],
                'CHANNEL_WIDTH_MHZ': ChannelUtils.get_channel_width(channels[i]),
                'AP_POWER': tx_power[i],
                'Channel_util': 0.5,
                'Client_count': 0,
                'num_aps_on_channel': 0
            }

        return features

    def save_training_data(self, sample_idx: int = 0):
        """
        Save training data in SISA-compatible format.

        Output format matches main_data_generator_ns3_integrated.py:
        - training_trajectory_XXXX.json: Formatted for GAT models
        - raw_trajectory_XXXX.json: Raw trajectory data
        """
        if not self.training_pairs:
            print("⚠ No training pairs to save")
            return

        # Format matching existing generated_data structure
        output_data = {
            'data_points': []
        }

        # Each training pair becomes a data point
        for pair in self.training_pairs:
            data_point = {
                'AP_coordinates': {
                    'coordinates': self.ap_coordinates.copy() if self.ap_coordinates else {}
                },
                'Initial_Configuration': {
                    'aps': pair['X']['aps'],
                    'rssi_matrix': pair['X']['rssi_matrix'],
                    'channel_gain_matrix': pair['X']['channel_gain_matrix']
                },
                'Final_Configuration': {
                    'optimized_aps': {}
                }
            }

            # Add ONLY the changed AP to Y (using ap_index from training pair)
            changed_ap_idx = pair.get('ap_index', 0)
            changed_ap_id = f'AP{changed_ap_idx + 1}'
            
            if changed_ap_id in pair['Y']['aps']:
                y_data = pair['Y']['aps'][changed_ap_id]
                data_point['Final_Configuration']['optimized_aps'][changed_ap_id] = {
                    'CHANNEL': y_data['CHANNEL'],
                    'CHANNEL_WIDTH_MHZ': y_data['CHANNEL_WIDTH_MHZ'],
                    'AP_POWER': y_data['AP_POWER']
                }

            output_data['data_points'].append(data_point)

        # Save to file
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f'training_trajectory_{timestamp}_{sample_idx:04d}.json'
        filepath = self.training_data_dir / filename

        with open(filepath, 'w') as f:
            json.dump(output_data, f, indent=2)

        print(f"✓ Saved training data: {filepath} ({len(output_data['data_points'])} data points)")

        # Also save raw trajectory for debugging
        raw_data = {
            'sample_id': f'{sample_idx:04d}',
            'coordinates': self.ap_coordinates.copy() if self.ap_coordinates else {},
            'initial_config': {
                'channels': self.initial_channels.copy(),
                'tx_power': self.initial_tx_power.copy(),
                'obss_pd': self.initial_obss_pd.copy()
            },
            'training_pairs': self.training_pairs,
            'final_config': {
                'channels': self.current_channels.copy(),
                'tx_power': self.current_tx_power.copy(),
                'obss_pd': self.current_obss_pd.copy()
            },
            'metadata': {
                'timestamp': datetime.now().isoformat(),
                'total_trials': self.trial_count,
                'best_objective': self.best_objective_so_far,
                'simulation_id': self.simulation_id
            }
        }

        raw_filepath = self.training_data_dir / f'raw_trajectory_{timestamp}_{sample_idx:04d}.json'
        with open(raw_filepath, 'w') as f:
            json.dump(raw_data, f, indent=2, default=str)

        print(f"✓ Saved raw trajectory: {raw_filepath}")

    def send_configuration(self, channels: List[int], tx_power: List[float],
                          obss_pd: List[float]) -> bool:
        """Send configuration to NS-3 via Kafka"""
        print(f"\n{'='*60}")
        print(f"SENDING CONFIGURATION VIA KAFKA")
        print(f"{'='*60}")
        print(f"Simulation ID: {self.simulation_id}")
        print(f"Target APs: {len(self.ap_bssids)}")
        for i, bssid in enumerate(self.ap_bssids):
            print(f"  AP {i+1} ({bssid}): CH={channels[i]}, TX={tx_power[i]:.1f}dBm, "
                  f"OBSS-PD={obss_pd[i]:.1f}dBm")

        success = self.kafka_sender.send_config(
            self.simulation_id, channels, tx_power, obss_pd, self.ap_bssids
        )

        if success:
            print("✓ Configuration sent successfully")
            # Wait longer for NS-3 to receive and apply config
            print("  Waiting 3s for NS-3 to apply configuration...")
            time.sleep(3)
            # Skip stale messages before collecting new metrics
            self.kafka_receiver.seek_to_end()
            print("  Seeked to end of metrics topic")
        else:
            print("✗ Failed to send configuration")

        print(f"{'='*60}\n")
        return success

    def collect_aligned_metrics(self, channels: List[int], timeout: float = 120.0) -> Optional[Dict]:
        """
        Collect aligned metrics from NS-3 that match expected channels.
        """
        return self.kafka_receiver.get_aligned_metrics(
            expected_channels=channels,
            ap_bssids=self.ap_bssids,
            timeout=timeout,
            max_attempts=50
        )

    def calculate_objective_from_metrics(self, metrics: Dict, channels: List[int] = None,
                                         tx_power: List[float] = None, obss_pd: List[float] = None) -> float:
        """Calculate objective function from NS-3 metrics"""
        print(f"\n{'='*60}")
        print("CALCULATING OBJECTIVE FROM NS-3 METRICS")
        print(f"{'='*60}")

        if not metrics:
            print("✗ No metrics provided")
            return float('-inf')

        raw_data = metrics.get('data', metrics)
        ap_metrics = raw_data.get('ap_metrics', {})

        # Per-AP metrics storage
        ap_p50_throughputs = []
        ap_p95_retry_rates = []
        ap_p95_loss_rates = []
        ap_p95_rtts = []

        # Normalization constants
        MAX_P50_THROUGHPUT = 100.0
        MAX_P95_RETRY_RATE = 5.0
        MAX_P95_LOSS_RATE = 1.0
        MAX_P95_RTT = 750.0

        print(f"\nPer-AP Metrics (from {len(ap_metrics)} APs in response):")

        for i, bssid in enumerate(self.ap_bssids):
            ap_data = ap_metrics.get(bssid, {})
            if not ap_data:
                print(f"\n  ⚠ AP{i+1} ({bssid}): No data found in metrics!")

            # Extract per-client metrics from connection_metrics
            connection_metrics = ap_data.get('connection_metrics', {})
            if not connection_metrics and ap_data:
                print(f"\n  ⚠ AP{i+1} ({bssid}): No connection_metrics (0 clients)")

            throughputs = []
            retry_rates = []
            loss_rates = []
            rtts = []

            for conn_key, conn_data in connection_metrics.items():
                # Throughput
                uplink = float(conn_data.get('uplink_throughput_mbps', 0))
                downlink = float(conn_data.get('downlink_throughput_mbps', 0))
                total = min(uplink + downlink, 100.0)
                throughputs.append(total)

                # Retry rate - NS-3 sends as ratio (0-1), not percentage
                # Cap at 5.0 (500% retry rate) and keep as ratio
                retry = min(float(conn_data.get('mac_retry_rate', 0)), 5.0)
                retry_rates.append(retry)

                # Packet loss rate - NS-3 sends as ratio (0-1), not percentage
                # Cap at 1.0 (100% loss) and keep as ratio
                loss = min(float(conn_data.get('packet_loss_rate', 0)), 1.0)
                loss_rates.append(loss)

                # RTT
                rtt = min(float(conn_data.get('mean_rtt_latency', 0)), 750.0)
                rtts.append(rtt)

            # Calculate percentiles
            p50_throughput = np.percentile(throughputs, 50) if throughputs else 0.0
            p95_retry_rate = np.percentile(retry_rates, 95) if retry_rates else 0.0
            p95_loss_rate = np.percentile(loss_rates, 95) if loss_rates else 0.0
            p95_rtt = np.percentile(rtts, 95) if rtts else 0.0

            ap_p50_throughputs.append(p50_throughput)
            ap_p95_retry_rates.append(p95_retry_rate)
            ap_p95_loss_rates.append(p95_loss_rate)
            ap_p95_rtts.append(p95_rtt)

            channel = channels[i] if channels else ap_data.get('channel', 'N/A')
            client_count = ap_data.get('associated_clients', len(connection_metrics))

            print(f"\n  AP{i+1} ({bssid}):")
            print(f"    Channel: {channel}, Clients: {client_count}")
            print(f"    p50 Throughput: {p50_throughput:.2f} Mbps")
            print(f"    p95 Retry Rate: {p95_retry_rate:.4f}")
            print(f"    p95 Loss Rate: {p95_loss_rate:.4f}")
            print(f"    p95 RTT: {p95_rtt:.2f} ms")

        # Calculate global metrics
        if ap_p50_throughputs:
            global_p50_throughput = np.mean(ap_p50_throughputs)
            global_p95_retry_rate = np.mean(ap_p95_retry_rates)
            global_p95_loss_rate = np.mean(ap_p95_loss_rates)
            global_p95_rtt = np.mean(ap_p95_rtts)
        else:
            global_p50_throughput = 0.0
            global_p95_retry_rate = 0.0
            global_p95_loss_rate = 0.0
            global_p95_rtt = 0.0

        # Normalize
        norm_throughput = global_p50_throughput / MAX_P50_THROUGHPUT
        norm_retry_rate = global_p95_retry_rate / MAX_P95_RETRY_RATE
        norm_loss_rate = global_p95_loss_rate / MAX_P95_LOSS_RATE
        norm_rtt = global_p95_rtt / MAX_P95_RTT

        # Calculate objective
        overall_objective = (
            0.35 * norm_throughput +
            0.10 * (1 - norm_retry_rate) +
            0.35 * (1 - norm_loss_rate) +
            0.20 * (1 - norm_rtt)
        )

        print(f"\n{'='*60}")
        print("GLOBAL METRICS")
        print(f"{'='*60}")
        print(f"Global p50 Throughput: {global_p50_throughput:.4f} Mbps")
        print(f"Global p95 Retry Rate: {global_p95_retry_rate:.4f}")
        print(f"Global p95 Loss Rate: {global_p95_loss_rate:.4f}")
        print(f"Global p95 RTT: {global_p95_rtt:.4f} ms")

        print(f"\n{'*'*60}")
        print(f"OVERALL OBJECTIVE VALUE: {overall_objective:.4f}")
        print(f"{'*'*60}\n")

        return overall_objective

    def calculate_per_ap_metrics(self, metrics: Dict) -> Dict:
        """Calculate per-AP metrics for API reporting"""
        raw_data = metrics.get('data', metrics)
        ap_metrics_data = raw_data.get('ap_metrics', {})
        per_ap_metrics = {}

        for bssid in self.ap_bssids:
            ap_data = ap_metrics_data.get(bssid, {})
            connection_metrics = ap_data.get('connection_metrics', {})

            throughputs = []
            rtts = []

            for conn_key, conn_data in connection_metrics.items():
                uplink = float(conn_data.get('uplink_throughput_mbps', 0))
                downlink = float(conn_data.get('downlink_throughput_mbps', 0))
                throughputs.append(uplink + downlink)
                rtts.append(float(conn_data.get('mean_rtt_latency', 0)))

            per_ap_metrics[bssid] = {
                'p15_throughput': np.percentile(throughputs, 15) if throughputs else 0.0,
                'p90_throughput': np.percentile(throughputs, 90) if throughputs else 0.0,
                'p90_latency': np.percentile(rtts, 90) if rtts else 0.0
            }

        return per_ap_metrics

    def get_optuna_propensity(self, trial, action_params: Dict) -> float:
        """Estimate propensity score using trial history similarity."""
        if len(self.trial_history) <= 1:
            return 0.5

        similarities = []

        for hist_trial in self.trial_history[:-1]:
            hist_action = hist_trial['action']

            channel_matches = sum(
                1 for i in range(self.num_aps)
                if action_params['channels'][i] == hist_action['channels'][i]
            )
            channel_sim = channel_matches / self.num_aps

            tx_diffs = [
                abs(action_params['tx_power'][i] - hist_action['tx_power'][i]) / 12.0
                for i in range(self.num_aps)
            ]
            tx_sim = 1.0 - (sum(tx_diffs) / self.num_aps)

            obss_diffs = [
                abs(action_params['obss_pd'][i] - hist_action['obss_pd'][i]) / 20.0
                for i in range(self.num_aps)
            ]
            obss_sim = 1.0 - (sum(obss_diffs) / self.num_aps)

            overall_sim = (channel_sim + tx_sim + obss_sim) / 3.0
            similarities.append(overall_sim)

        avg_similarity = np.mean(similarities) if similarities else 0.5
        propensity = 0.1 + 0.9 * avg_similarity

        return propensity

    def compute_doubly_robust_uplift(self, trial, action_params: Dict,
                                     baseline_objective: float,
                                     candidate_objective: float) -> tuple:
        """Compute doubly-robust (DR) uplift estimate with confidence bound."""
        propensity = self.get_optuna_propensity(trial, action_params)
        surrogate_outcome = candidate_objective
        actual_outcome = candidate_objective

        if len(self.trial_history) > 2:
            historical_objectives = [t['outcome'] for t in self.trial_history[:-1]]
            G_prediction = np.mean(historical_objectives)
            Y_actual = candidate_objective

            importance_weight = 1.0 / propensity
            prediction_error = Y_actual - G_prediction
            dr_estimate = G_prediction + importance_weight * prediction_error

            hist_std = np.std(historical_objectives)
            adjusted_std = hist_std * importance_weight

            dr_values = []
            for _ in range(self.dr_bootstrap_samples):
                sample = dr_estimate + np.random.normal(0, adjusted_std)
                dr_values.append(sample)

            mean_dr = np.mean(dr_values)
            std_dr = np.std(dr_values)
        else:
            mean_dr = candidate_objective
            std_dr = 0.05

        z_score = 1.28
        lower_bound = mean_dr - z_score * std_dr

        dr_uplift = mean_dr - baseline_objective
        uplift_lower_bound = lower_bound - baseline_objective

        return dr_uplift, uplift_lower_bound, propensity

    def doubly_robust_safety_gate(self, trial, action_params: Dict,
                                   baseline_objective: float,
                                   candidate_objective: float) -> tuple:
        """Doubly-Robust safety gate to decide if candidate should be deployed."""
        print(f"\n{'='*80}")
        print("DOUBLY-ROBUST SAFETY GATE")
        print(f"{'='*80}")

        dr_uplift, lower_bound, propensity = self.compute_doubly_robust_uplift(
            trial, action_params, baseline_objective, candidate_objective
        )

        should_deploy = lower_bound >= self.dr_min_improvement

        print(f"Baseline:     {baseline_objective:.4f}")
        print(f"Candidate:    {candidate_objective:.4f}")
        print(f"Uplift:       {candidate_objective - baseline_objective:+.4f}")
        print(f"Propensity:   {propensity:.3f}")
        print(f"DR Estimate:  {dr_uplift + baseline_objective:.4f}")
        print(f"80% CI Lower: {lower_bound:+.4f}")

        if should_deploy:
            print(f"✓ PASSED - Lower bound {lower_bound:+.4f} ≥ threshold {self.dr_min_improvement:+.4f}")
        else:
            print(f"✗ BLOCKED - Lower bound {lower_bound:+.4f} < threshold {self.dr_min_improvement:+.4f}")

        print(f"{'='*80}\n")

        return should_deploy, dr_uplift, lower_bound, propensity

    def prepare_improvement_report(self, trial, channels: List[int], tx_power: List[float],
                                   obss_pd: List[float], per_ap_metrics: Dict,
                                   new_objective: float, previous_objective: float,
                                   dr_uplift: float = None, dr_lower_bound: float = None,
                                   propensity: float = None) -> Dict:
        """Prepare JSON report for API call when we beat the previous best objective."""
        self.planner_version += 1

        expected_change = new_objective - previous_objective

        dr_metrics = {
            'dr_uplift': dr_uplift if dr_uplift is not None else expected_change,
            'dr_lower_bound': dr_lower_bound if dr_lower_bound is not None else expected_change,
            'propensity_score': propensity if propensity is not None else 0.0,
            'dr_gate_enabled': True
        }

        report = {
            'timestamp_unix': int(time.time()),
            'simulation_id': self.simulation_id,
            'command_type': 'UPDATE_AP_PARAMETERS',
            'dr_metrics': dr_metrics,
            'ap_parameters': {}
        }

        for i, bssid in enumerate(self.ap_bssids):
            ap_metrics = per_ap_metrics.get(bssid, {
                'p15_throughput': 0.0,
                'p90_throughput': 0.0,
                'p90_latency': 0.0
            })

            report['ap_parameters'][bssid] = {
                'tx_power_start_dbm': tx_power[i],
                'tx_power_end_dbm': tx_power[i],
                'cca_ed_threshold_dbm': -82.0,
                'obss_pd': obss_pd[i],
                'rx_sensitivity_dbm': -93.0,
                'channel_number': channels[i],
                'channel_width_mhz': 20,
                'band': 'BAND_5GHZ',
                'primary_20_index': 0,
                'change_accepted': True,
                'canery_time': 5,
                'p90_latency': ap_metrics['p90_latency'],
                'p15_throughput': ap_metrics['p15_throughput'],
                'p90_throughput': ap_metrics['p90_throughput'],
                'planner_version': self.planner_version,
                'expected_change': expected_change
            }

        print(f"\n{'='*80}")
        print(f"IMPROVEMENT REPORT PREPARED (Planner Version: {self.planner_version})")
        print(f"{'='*80}")
        print(f"Previous Best: {previous_objective:.4f}")
        print(f"New Best: {new_objective:.4f}")
        print(f"Expected Change: {expected_change:+.4f}")
        print(f"{'='*80}\n")

        self.send_improvement_to_api(report)

        return report

    def send_improvement_to_api(self, report: Dict) -> bool:
        """Send improvement report to the API endpoint."""
        api_url = f"{self.api_base_url}/rrm"

        try:
            response = requests.post(
                api_url,
                json=report,
                headers={'Content-Type': 'application/json'},
                timeout=10
            )

            if response.status_code == 200:
                try:
                    response_data = response.json()
                    print(f"  API Response: {json.dumps(response_data, indent=2)}")
                except:
                    print(f"  API Response: {response.text}")
                return True
            else:
                return False

        except requests.exceptions.Timeout:
            return False
        except requests.exceptions.ConnectionError:
            return False
        except Exception as e:
            return False

    def generate_planner_id(self, trial_number: int) -> str:
        """Generate a 12-digit hexadecimal planner ID from trial number."""
        import hashlib

        if trial_number in self.planner_ids:
            return self.planner_ids[trial_number]

        hash_input = f"planner_trial_{trial_number}_time_{time.time()}".encode('utf-8')
        hash_obj = hashlib.sha256(hash_input)
        hex_digest = hash_obj.hexdigest()

        planner_id = hex_digest[:12]
        self.planner_ids[trial_number] = planner_id

        return planner_id

    def objective(self, trial: optuna.Trial) -> float:
        """Optuna objective function with NS-3 Kafka integration"""
        self.trial_count += 1

        # Generate random coordinates on first trial (like SISA)
        if self.trial_count == 1:
            self.ap_coordinates = self.generate_random_coordinates()
            print(f"Generated AP coordinates: {self.ap_coordinates}")

        planner_id = self.generate_planner_id(self.trial_count)

        print("\n\n")
        print("#" * 80)
        print(f"TRIAL #{self.trial_count} - PLANNER ID #{planner_id}")
        print("#" * 80)

        try:
            import random

            # Store PREVIOUS configuration (X - initial state)
            prev_channels = list(self.current_channels)
            prev_tx_power = list(self.current_tx_power)
            prev_obss_pd = list(self.current_obss_pd)

            # Start with current configuration
            channels = list(self.current_channels)
            tx_power = list(self.current_tx_power)
            obss_pd = list(self.current_obss_pd)

            # Randomly select ONE AP to change
            selected_ap = random.randint(0, self.num_aps - 1)
            print(f"\n  SELECTED AP {selected_ap + 1} for optimization")

            # RL LOGGING: Capture initial state
            initial_state = {
                'channel': channels[selected_ap],
                'tx_power': tx_power[selected_ap],
                'obss_pd': obss_pd[selected_ap]
            }

            # Get channels used by OTHER APs
            used_channels = set(channels[i] for i in range(self.num_aps) if i != selected_ap)
            available_for_selected = [ch for ch in self.available_channels if ch not in used_channels]

            if not available_for_selected:
                available_for_selected = self.available_channels

            # Suggest new parameters for selected AP
            suggested_channel_pos = trial.suggest_int(f'channel_pos_{selected_ap}', 0, len(available_for_selected) - 1)
            new_channel = available_for_selected[suggested_channel_pos]
            new_tx_power = trial.suggest_float(f'tx_power_{selected_ap}', 13.0, 25.0)
            new_obss_pd = trial.suggest_float(f'obss_pd_{selected_ap}', -80.0, -60.0)

            # RL LOGGING: Capture action
            action = {
                'channel': new_channel,
                'tx_power': new_tx_power,
                'obss_pd': new_obss_pd
            }

            # Update only selected AP
            channels[selected_ap] = new_channel
            tx_power[selected_ap] = new_tx_power
            obss_pd[selected_ap] = new_obss_pd

            print(f"  AP {selected_ap + 1} NEW: CH={new_channel}, TX={new_tx_power:.1f}dBm, OBSS-PD={new_obss_pd:.1f}dBm")

            # Wait for network settling (first trial only)
            if self.trial_count == 1:
                time.sleep(self.settling_time)

            # Send configuration to NS-3 via Kafka
            if not self.send_configuration(channels, tx_power, obss_pd):
                return float('-inf')

            # Collect aligned metrics from NS-3
            print(f"\n{'='*80}")
            print(f"COLLECTING ALIGNED METRICS FROM NS-3")
            print(f"{'='*80}\n")

            metrics = self.collect_aligned_metrics(channels, timeout=120.0)

            if not metrics:
                print("✗ No aligned metrics received - stopping study")
                trial.study.stop()
                return float('-inf')

            # Extract features in SISA format (X - before state)
            features_before = self.extract_features_from_metrics(metrics, prev_channels, prev_tx_power)

            # Calculate objective from metrics
            objective_value = self.calculate_objective_from_metrics(metrics, channels, tx_power, obss_pd)

            # Check if config actually changed
            config_changed = (prev_channels[selected_ap] != channels[selected_ap] or 
                            abs(prev_tx_power[selected_ap] - tx_power[selected_ap]) > 0.5)

            # TRAINING DATA: Store training pair if config changed (same as SISA)
            if config_changed:
                # Build Y (final configuration after change)
                y_aps = {}
                for i in range(self.num_aps):
                    ap_id = f'AP{i+1}'
                    y_aps[ap_id] = {
                        'CHANNEL': channels[i],
                        'CHANNEL_WIDTH_MHZ': ChannelUtils.get_channel_width(channels[i]),
                        'AP_POWER': round(tx_power[i], 1)
                    }

                training_pair = {
                    'iteration': self.trial_count,
                    'ap_index': selected_ap,
                    'X': {
                        'aps': features_before['aps'].copy(),
                        'rssi_matrix': features_before['rssi_matrix'].copy(),
                        'channel_gain_matrix': features_before['channel_gain_matrix'].copy()
                    },
                    'Y': {
                        'aps': y_aps
                    }
                }
                self.training_pairs.append(training_pair)
                print(f"  ✓ Training pair saved (total: {len(self.training_pairs)})")

            # RL LOGGING: Store transition
            rl_transition = {
                'trial': self.trial_count,
                'ap': selected_ap + 1,
                'ap_bssid': self.ap_bssids[selected_ap],
                'state': initial_state,
                'action': action,
                'reward': objective_value
            }
            self.rl_transitions.append(rl_transition)

            # Save RL transitions
            rl_log_file = self.output_dir / f"rl_transitions_{datetime.now().strftime('%Y%m%d')}.json"
            with open(rl_log_file, 'w') as f:
                json.dump(self.rl_transitions, f, indent=2)

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

            # Store baseline (Trial 1)
            if self.trial_count == 1:
                self.baseline_objective = objective_value
                self.best_objective_so_far = objective_value

            # Check if we beat previous best
            if objective_value > self.best_objective_so_far:
                print(f"NEW BEST OBJECTIVE!")
                print(f"Previous Best: {self.best_objective_so_far:.4f}")
                print(f"New Best: {objective_value:.4f}")

                self.config_churn_without_dr += 1

                # DR Safety Gate
                should_deploy, dr_uplift, lower_bound, propensity = self.doubly_robust_safety_gate(
                    trial=trial,
                    action_params=action_params,
                    baseline_objective=self.baseline_objective,
                    candidate_objective=objective_value
                )

                if should_deploy:
                    self.config_churn_with_dr += 1
                    per_ap_metrics = self.calculate_per_ap_metrics(metrics)

                    self.prepare_improvement_report(
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

                    self.baseline_objective = objective_value
                    print(f"\n✓ Config deployed - baseline updated to {objective_value:.4f}\n")
                else:
                    print(f"\n⚠ Configuration NOT deployed due to DR safety gate rejection\n")

                self.best_objective_so_far = objective_value

            # Update current configuration
            self.current_channels = list(channels)
            self.current_tx_power = list(tx_power)
            self.current_obss_pd = list(obss_pd)

            return objective_value

        except Exception as e:
            print(f"✗ Trial failed: {e}")
            import traceback
            traceback.print_exc()
            return float('-inf')

    def cleanup(self):
        """Cleanup Kafka connections"""
        self.kafka_receiver.close()
        self.kafka_sender.close()


def main():
    parser = argparse.ArgumentParser(description='Optuna Optimization - NS-3 Kafka Integration')
    parser.add_argument('--study-time', type=int, default=30,
                      help='Study duration in minutes (default: 30)')
    parser.add_argument('--evaluation-window', type=int, default=5,
                      help='Evaluation window in seconds (default: 5)')
    parser.add_argument('--api-url', type=str, default='http://localhost:8000',
                      help='Base URL for API calls (default: http://localhost:8000)')
    parser.add_argument('--simulation-id', type=str, default='sim-001',
                      help='Simulation ID (default: sim-001)')
    parser.add_argument('--kafka-broker', type=str, default='localhost:9092',
                      help='Kafka broker address (default: localhost:9092)')
    parser.add_argument('--config-file', type=str, default='config-simulation.json',
                      help='Path to config file (default: config-simulation.json)')
    parser.add_argument('--n-trials', type=int, default=200,
                      help='Number of optimization trials (default: 200)')
    parser.add_argument('--training-data-dir', type=str, default='training_data',
                      help='Output directory for training data (default: training_data)')

    args = parser.parse_args()

    # Setup logging
    output_dir = Path("logs_folder")
    output_dir.mkdir(exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = output_dir / f"optuna_ns3_integrated_{timestamp}.txt"

    tee_logger = TeeLogger(log_file)
    sys.stdout = tee_logger

    print("\n" + "=" * 80)
    print("OPTUNA NETWORK OPTIMIZATION (NS-3 KAFKA INTEGRATION)")
    print("=" * 80)
    print(f"Kafka Broker: {args.kafka_broker}")
    print(f"Simulation ID: {args.simulation_id}")
    print(f"N Trials: {args.n_trials}")
    print(f"Log File: {log_file}")
    print(f"Training Data Dir: {args.training_data_dir}")
    print("=" * 80 + "\n")

    # Create optimizer
    optimizer = NetworkOptimizer(
        study_time=args.study_time,
        evaluation_window=args.evaluation_window,
        api_base_url=args.api_url,
        simulation_id=args.simulation_id,
        kafka_broker=args.kafka_broker,
        config_file=args.config_file,
        training_data_dir=args.training_data_dir
    )

    # CRITICAL: Verify Kafka connectivity first
    if not optimizer.verify_kafka_connectivity():
        print("✗ Kafka connectivity check failed. Is Kafka running?")
        tee_logger.close()
        sys.stdout = tee_logger.terminal
        return 1

    # CRITICAL: Discover BSSIDs from NS-3 before starting optimization
    if not optimizer.discover_bssids_from_ns3():
        print("✗ Failed to discover BSSIDs from NS-3. Is the simulation running?")
        tee_logger.close()
        sys.stdout = tee_logger.terminal
        return 1

    # Create Optuna study
    study = optuna.create_study(
        direction='maximize',
        sampler=optuna.samplers.TPESampler(seed=42)
    )

    print("=" * 80)
    print("SINGLE-AP OPTIMIZATION MODE")
    print("=" * 80)
    print("Each trial randomly selects ONE AP to optimize")
    print(f"\nInitial Configuration:")
    print(f"  Channels: {optimizer.initial_channels}")
    print(f"  TX Power: {optimizer.initial_tx_power}")
    print(f"  OBSS-PD: {optimizer.initial_obss_pd}")
    print(f"  BSSIDs: {optimizer.ap_bssids}")
    print("=" * 80 + "\n")

    try:
        study.optimize(
            optimizer.objective,
            n_trials=args.n_trials,
            show_progress_bar=False
        )
    except KeyboardInterrupt:
        print("\n\n✗ Optimization interrupted by user")

    # Save training data in SISA format
    print("\n" + "=" * 80)
    print("SAVING TRAINING DATA (SISA FORMAT)")
    print("=" * 80)
    optimizer.save_training_data(sample_idx=0)

    # Cleanup
    optimizer.cleanup()

    # Print results
    print("\n\n")
    print("=" * 80)
    print("OPTIMIZATION COMPLETE")
    print("=" * 80)

    completed_trials = [t for t in study.trials if t.state == optuna.trial.TrialState.COMPLETE]

    if not completed_trials:
        print("⚠ No trials completed successfully")
        tee_logger.close()
        sys.stdout = tee_logger.terminal
        return 1

    print(f"\nCompleted Trials: {len(completed_trials)}")

    trial_0 = completed_trials[0]
    trial_0_value = trial_0.value

    print(f"\n{'='*80}")
    print("INITIAL CONFIGURATION")
    print(f"{'='*80}")
    print(f"Channels: {optimizer.initial_channels}")
    print(f"TX Power: {optimizer.initial_tx_power}")
    print(f"OBSS-PD: {optimizer.initial_obss_pd}")
    print(f"First Trial Objective: {trial_0_value:.4f}")

    best_trial = study.best_trial
    best_trial_id = optimizer.generate_planner_id(best_trial.number + 1)

    print(f"\n{'='*80}")
    print("BEST TRIAL FOUND")
    print(f"{'='*80}")
    print(f"Trial Number: {best_trial.number}")
    print(f"PLANNER ID: #{best_trial_id}")
    print(f"Objective Value: {best_trial.value:.4f}")

    print(f"\n{'='*80}")
    print("FINAL CONFIGURATION")
    print(f"{'='*80}")
    print(f"Channels: {optimizer.current_channels}")
    print(f"TX Power: {[f'{p:.2f}' for p in optimizer.current_tx_power]}")
    print(f"OBSS-PD: {[f'{o:.2f}' for o in optimizer.current_obss_pd]}")

    improvement = best_trial.value - trial_0_value
    improvement_pct = (improvement / abs(trial_0_value)) * 100 if trial_0_value != 0 else 0

    print(f"\n{'='*80}")
    print("IMPROVEMENT SUMMARY")
    print(f"{'='*80}")
    print(f"First Trial:  {trial_0_value:.4f}")
    print(f"Best Trial:   {best_trial.value:.4f}")
    print(f"Improvement: {improvement:+.4f} ({improvement_pct:+.2f}%)")

    if improvement > 0:
        print(f"✓ OPTIMIZATION SUCCEEDED!")
    else:
        print(f"⚠ No improvement found")

    print("\n" + "=" * 80)
    print("CONFIG CHURN ANALYSIS")
    print("=" * 80)
    print(f"WITHOUT DR Gate: {optimizer.config_churn_without_dr} deployments")
    print(f"WITH DR Gate: {optimizer.config_churn_with_dr} deployments")
    if optimizer.config_churn_without_dr > 0:
        churn_reduction = optimizer.config_churn_without_dr - optimizer.config_churn_with_dr
        churn_reduction_pct = (churn_reduction / optimizer.config_churn_without_dr) * 100
        print(f"Reduction: {churn_reduction} ({churn_reduction_pct:.1f}%)")
    print("=" * 80)

    print("\n" + "=" * 80)
    print("TRAINING DATA SUMMARY (SISA FORMAT)")
    print("=" * 80)
    print(f"Training data directory: {optimizer.training_data_dir}")
    print(f"Total training pairs saved: {len(optimizer.training_pairs)}")
    print(f"AP coordinates: {optimizer.ap_coordinates}")
    print("=" * 80)

    print(f"\nLog saved to: {log_file}")
    print("=" * 80 + "\n")

    tee_logger.close()
    sys.stdout = tee_logger.terminal

    return 0


if __name__ == "__main__":
    sys.exit(main())

