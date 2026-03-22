"""
WiFi AP RL - Online Training Loop

This file contains the main training loop for online PPO with Kafka integration.
Adapted from GOODRL/step3.py but uses Kafka instead of a gym environment.

USAGE:
======
1. Define your action spaces (CHANNEL_OPTIONS, POWER_OPTIONS)
2. Configure Kafka topics
3. Modify reward computation as needed
4. Run: python main.py
"""

import os
import time
import json
import random
import logging
import numpy as np
import torch
from copy import deepcopy
from typing import Optional, Dict, Any
from datetime import datetime
from pathlib import Path

# Local imports
from actor_wifi import PPO, APGraphBatch, Memory, RolloutBuffer, device
from graph_builder import APGraphBuilder, APGraphState, state_to_tensors
from gat_streamer import GATStateProvider
from DynamicGraphColoring import DcSimpleAlgo
import networkx as nx


# ============================================================================
# Configuration
# ============================================================================

class Config:
    """
    Training configuration.
    Adjust these values as needed.
    """
    # Random seed
    seed = 69

    # =========================================================================
    # ACTION SPACE - DEFINE YOUR OPTIONS HERE
    # These are NOT hardcoded in the model
    # =========================================================================
    # Channel width options (PPO outputs width, mapped to channel via color)
    CHANNEL_WIDTH_OPTIONS = [20, 40, 80]
    POWER_OPTIONS = [13.0, 15.0, 17.0, 19.0, 21.0, 23.0, 25.0]

    # Channel mapping based on graph color (0-5) and channel width
    # Color -> {width -> channel options}
    COLOR_CHANNEL_MAP = {
        0: {20: [36, 40, 44, 48], 40: [38, 46], 80: [42]},
        1: {20: [52, 56, 60, 64], 40: [54, 62], 80: [58]},
        2: {20: [100, 104, 108, 112], 40: [102, 110], 80: [106]},
        3: {20: [116, 120, 124, 128], 40: [118, 126], 80: [122]},
        4: {20: [132, 136, 140, 144], 40: [134, 142], 80: [138]},
        5: {20: [149, 153, 157, 161], 40: [151, 159], 80: [155]},
    }

    # Model architecture
    input_dim_ap = 4        # Must match graph_builder.get_feature_dim()
    hidden_dim = 128
    gnn_layers = 2
    mlp_layers = 3
    heads = 1
    dropout = 0.0

    # PPO hyperparameters
    lr_actor = 1e-3
    lr_critic = 1e-3
    eps_clip = 0.2
    entropy_coef = 0.01
    value_coef = 0.5
    gamma = 0.99
    gae_lambda = 0.95
    normalize_rewards = 1000.0

    # Training loop
    max_updates = 1000
    stabilization_time = 60 # Seconds to wait before starting logs

    # basically for warm start training of critic
    warmup_steps = 5      # Initial exploration before critic updates
    warmup_critic = 3      # Critic warmstart updates,(this*window_steps+total_critic_warmup_step))

    # ppo update frequency
    window_steps = 5    
    
    # Buffer for critic training
    buffer_size = 50  # Max transitions stored; oldest dropped when full
    batch_size = 8  # Mini-batch size for gradient descent during PPO update
    n_epochs = 1 # How many times to iterate over the buffer per update

    # Kafka
    kafka_broker = 'localhost:9092'
    metrics_topic = 'ns3-metrics'
    commands_topic = 'optimization-commands'
    simulation_id = 'basic-sim'  # Must match NS3 simulation ID!

    # Timing
    action_cooldown = 5.0   # Seconds between actions
    config_settling_time = 10.0  # Seconds to wait after sending config for NS3 to apply it

    # GAT State Provider settings or HOw fast we call ppo to take actions
    gat_batch_size = 6 # Number of logs to EWMA before taking action
    gat_ewma_alpha = 0.5    # EWMA smoothing factor

    # Logging verbosity
    verbose = True          # Enable detailed logging

    best_objective=0.0


# ============================================================================
# Kafka Interface (simplified - expand as needed)
# ============================================================================

class KafkaInterface:
    """
    Kafka consumer/producer for AP metrics and commands.

    This is a simplified interface. For full implementation,
    see BO_Package/integrations/kafka_client.py and
    BO/bayesian_optimiser_consumer.py
    """

    def __init__(self, config: Config):
        self.config = config
        self.consumer = None
        self.producer = None
        self._connected = False

    def connect(self) -> bool:
        """
        Connect to Kafka broker.

        Returns True if successful.
        """
        try:
            from kafka import KafkaConsumer, KafkaProducer

            self.consumer = KafkaConsumer(
                self.config.metrics_topic,
                bootstrap_servers=[self.config.kafka_broker],
                auto_offset_reset='latest',
                enable_auto_commit=True,
                value_deserializer=lambda m: json.loads(m.decode('utf-8')),
                consumer_timeout_ms=1000
            )

            self.producer = KafkaProducer(
                bootstrap_servers=[self.config.kafka_broker],
                value_serializer=lambda v: json.dumps(v).encode('utf-8'),
                max_request_size=10485760
            )

            self._connected = True
            print(f"Connected to Kafka at {self.config.kafka_broker}")
            return True

        except ImportError:
            print("WARNING: kafka-python not installed. Using mock mode.")
            self._connected = False
            return False
        except Exception as e:
            print(f"Failed to connect to Kafka: {e}")
            self._connected = False
            return False

    def get_metrics(self, timeout_ms: int = 1000) -> Optional[Dict]:
        """
        Poll for metrics from Kafka.

        Returns the latest metrics message or None if timeout.
        """
        if not self._connected:
            return self._mock_metrics()

        try:
            messages = self.consumer.poll(timeout_ms=timeout_ms, max_records=1)
            for tp, records in messages.items():
                for record in records:
                    return record.value
        except Exception as e:
            print(f"Error polling Kafka: {e}")

        return None

    def send_action(self, ap_bssid: str, channel_number: int, channel_width: int,
                    power_idx: int, timestamp: Optional[float] = None) -> bool:
        """
        Send action command via Kafka.

        Args:
            ap_bssid: Target AP's MAC address
            channel_number: Actual channel number (e.g., 36, 42, 58)
            channel_width: Channel width in MHz (20, 40, or 80)
            power_idx: Index into POWER_OPTIONS
            timestamp: Optional timestamp for correlation
        """
        power = self.config.POWER_OPTIONS[power_idx]

        if not self._connected:
            print(f"[MOCK] Action: AP={ap_bssid}, "
                  f"channel={channel_number}, width={channel_width}MHz, "
                  f"power={power}dBm")
            return True

        try:
            message = {
                'timestamp_unix': timestamp or int(time.time()),
                'simulation_id': self.config.simulation_id,
                'command_type': 'UPDATE_AP_PARAMETERS',
                'ap_parameters': {
                    ap_bssid: {
                        'channel_number': channel_number,
                        'tx_power_start_dbm': power,
                        'tx_power_end_dbm': power,
                        'channel_width_mhz': channel_width,
                        'band': 'BAND_5GHZ'
                    }
                }
            }

            future = self.producer.send(
                self.config.commands_topic,
                key=self.config.simulation_id.encode('utf-8'),
                value=message
            )
            future.get(timeout=10)

            # Log successful send
            print(f"[KAFKA TX] Sent to '{self.config.commands_topic}': "
                  f"AP={ap_bssid}, channel={channel_number}, width={channel_width}MHz, power={power}dBm")
            return True

        except Exception as e:
            print(f"[KAFKA TX ERROR] Failed to send action: {e}")
            return False

    def close(self):
        """Close Kafka connections."""
        if self.consumer:
            self.consumer.close()
        if self.producer:
            self.producer.close()


# ============================================================================
# Reward Computation
# ============================================================================

# ============================================================================
# NEW Optuna-Style Reward Computation (Per-AP)
# ============================================================================

def compute_optuna_style_reward(
    gat_state: Dict[str, Any],
    target_ap_bssid: str,
    verbose: bool = False
) -> Optional[float]:
    """
    Compute reward using Optuna-style objective function for a SINGLE target AP.

    New format from gat_streamer (all pre-computed):
    - p50_throughput: p50 across logs of (sum of uplink across clients)
    - p75_loss_rate: p75 across logs of (p95 across clients per log)
    - p75_rtt: p75 across logs of (p95 across clients per log)
    - p75_jitter: p75 across logs of (p95 across clients per log)

    Formula:
        objective = 0.35 * norm_throughput
                  + 0.10 * (1 - norm_jitter)
                  + 0.35 * (1 - norm_loss_rate)
                  + 0.20 * (1 - norm_rtt)

    Args:
        gat_state: GAT state dict with 'connection_metrics' per AP
        target_ap_bssid: BSSID of the AP we changed config for
        verbose: Print detailed breakdown

    Returns:
        Objective score [0, 1] where higher is better, or None if no data
    """
    # Normalization constants
    MAX_P50_THROUGHPUT = 100.0  # Mbps
    MAX_P75_LOSS_RATE = 1.0   # percentage (0-100)
    MAX_P75_RTT = 750.0         # ms
    MAX_P75_JITTER = 30.0       # ms

    # Get connection metrics for the target AP
    connection_metrics = gat_state.get('connection_metrics', {})

    if target_ap_bssid not in connection_metrics:
        if verbose:
            print(f"[OPTUNA REWARD] No connection_metrics for AP {target_ap_bssid}")
        return None

    ap_data = connection_metrics[target_ap_bssid]

    if not ap_data:
        if verbose:
            print(f"[OPTUNA REWARD] Empty connection_metrics for AP {target_ap_bssid}")
        return None

    # Get pre-computed metrics (all computed by gat_streamer)
    p50_throughput = float(ap_data.get('p50_throughput', 0))
    p75_loss_rate = float(ap_data.get('p75_loss_rate', 0))
    p75_rtt = float(ap_data.get('p75_rtt', 0))
    p75_jitter = float(ap_data.get('p75_jitter', 0))

    # Normalize (clamp to [0, 1])
    norm_throughput = min(p50_throughput / MAX_P50_THROUGHPUT, 1.0)
    norm_loss_rate = min(p75_loss_rate / MAX_P75_LOSS_RATE, 1.0)
    norm_rtt = min(p75_rtt / MAX_P75_RTT, 1.0)
    norm_jitter = min(p75_jitter / MAX_P75_JITTER, 1.0)

    # Calculate objective
    objective = (
        0.35 * norm_throughput +
        0.10 * (1 - norm_jitter) +
        0.35 * (1 - norm_loss_rate) +
        0.20 * (1 - norm_rtt)
    )

    if verbose:
        print(f"\n[OPTUNA REWARD] AP: {target_ap_bssid}")
        print(f"  p50 Throughput: {p50_throughput:.2f} Mbps (norm: {norm_throughput:.4f})")
        print(f"  p75 Jitter:     {p75_jitter:.2f} ms (norm: {norm_jitter:.4f})")
        print(f"  p75 Loss Rate:  {p75_loss_rate:.2f}% (norm: {norm_loss_rate:.4f})")
        print(f"  p75 RTT:        {p75_rtt:.2f} ms (norm: {norm_rtt:.4f})")
        print(f"  Components: tput={0.35*norm_throughput:.4f}, "
              f"jitter={0.10*(1-norm_jitter):.4f}, "
              f"loss={0.35*(1-norm_loss_rate):.4f}, "
              f"rtt={0.20*(1-norm_rtt):.4f}")
        print(f"  OBJECTIVE: {objective:.4f}")

    return objective


def compute_global_objective(
    gat_state: Dict[str, Any],
    verbose: bool = False,
    logger: Optional[logging.Logger] = None
) -> Optional[float]:
    """
    Compute global objective across ALL APs.

    New format from gat_streamer (all pre-computed per AP):
    - p50_throughput: p50 across logs of (sum of uplink across clients)
    - p75_loss_rate: p75 across logs of (p95 across clients per log)
    - p75_rtt: p75 across logs of (p95 across clients per log)
    - p75_jitter: p75 across logs of (p95 across clients per log)

    Formula:
        objective = 0.35 * norm_throughput
                  + 0.10 * (1 - norm_jitter)
                  + 0.35 * (1 - norm_loss_rate)
                  + 0.20 * (1 - norm_rtt)

    Args:
        gat_state: GAT state dict with 'connection_metrics' = {
            ap_bssid: {
                'p50_throughput': float,
                'p75_loss_rate': float,
                'p75_rtt': float,
                'p75_jitter': float
            }
        }
        verbose: Print detailed breakdown
        logger: Optional logger for output (uses print if None)

    Returns:
        Global objective score [0, 1] where higher is better, or None if no data
    """
    # Normalization constants
    MAX_P50_THROUGHPUT = 100.0  # Mbps (per AP)
    MAX_P75_LOSS_RATE = 1.0   # percentage (0-100)
    MAX_P75_RTT = 750.0         # ms
    MAX_P75_JITTER = 30.0       # ms

    def log_output(msg):
        if logger:
            logger.info(msg)
        else:
            print(msg)

    connection_metrics = gat_state.get('connection_metrics', {})

    if not connection_metrics:
        if verbose:
            log_output(f"[GLOBAL OBJECTIVE] No connection_metrics available")
        return None

    # Store per-AP metrics for averaging
    ap_p50_throughputs = []
    ap_p75_loss_rates = []
    ap_p75_rtts = []
    ap_p75_jitters = []
    aps_with_data = []

    # Iterate over each AP's connection metrics
    for ap_bssid, ap_data in connection_metrics.items():
        if not ap_data:
            continue

        # Get pre-computed metrics (all computed by gat_streamer)
        p50_throughput = float(ap_data.get('p50_throughput', 0))
        p75_loss_rate = float(ap_data.get('p75_loss_rate', 0))
        p75_rtt = float(ap_data.get('p75_rtt', 0))
        p75_jitter = float(ap_data.get('p75_jitter', 0))

        # Store metrics for this AP
        ap_p50_throughputs.append(p50_throughput)
        ap_p75_loss_rates.append(p75_loss_rate)
        ap_p75_rtts.append(p75_rtt)
        ap_p75_jitters.append(p75_jitter)
        aps_with_data.append((ap_bssid, p50_throughput, p75_loss_rate, p75_rtt, p75_jitter))

    if not ap_p50_throughputs:
        if verbose:
            log_output(f"[GLOBAL OBJECTIVE] No APs with data found")
        return None

    # Calculate global metrics by averaging across all APs
    avg_p50_throughput = np.mean(ap_p50_throughputs)
    avg_p75_loss_rate = np.mean(ap_p75_loss_rates)
    avg_p75_rtt = np.mean(ap_p75_rtts)
    avg_p75_jitter = np.mean(ap_p75_jitters)

    # Normalize by dividing by max values (clamp to [0, 1])
    norm_throughput = min(avg_p50_throughput / MAX_P50_THROUGHPUT, 1.0)
    norm_loss_rate = min(avg_p75_loss_rate / MAX_P75_LOSS_RATE, 1.0)
    norm_rtt = min(avg_p75_rtt / MAX_P75_RTT, 1.0)
    norm_jitter = min(avg_p75_jitter / MAX_P75_JITTER, 1.0)

    # Calculate global objective
    objective = (
        0.35 * norm_throughput +
        0.10 * (1 - norm_jitter) +
        0.35 * (1 - norm_loss_rate) +
        0.20 * (1 - norm_rtt)
    )

    # ALWAYS log the global objective value to file (even if verbose=False)
    if logger:
        logger.info(f"[GLOBAL OBJECTIVE] {objective:.4f} | "
                   f"Throughput={avg_p50_throughput:.2f}Mbps | "
                   f"Loss={avg_p75_loss_rate:.2f}% | "
                   f"RTT={avg_p75_rtt:.2f}ms | "
                   f"Jitter={avg_p75_jitter:.2f}ms | "
                   f"APs={len(aps_with_data)}")

    if verbose:
        log_output(f"[GLOBAL OBJECTIVE] Across {len(aps_with_data)} APs:")
        for ap_bssid, tput, loss, rtt, jitter in aps_with_data:
            log_output(f"    {ap_bssid}: tput={tput:.2f}, loss={loss:.2f}%, rtt={rtt:.2f}ms, jitter={jitter:.2f}ms")
        log_output(f"  Avg p50 Throughput: {avg_p50_throughput:.2f} Mbps (norm: {norm_throughput:.4f})")
        log_output(f"  Avg p75 Loss Rate:  {avg_p75_loss_rate:.2f}% (norm: {norm_loss_rate:.4f})")
        log_output(f"  Avg p75 RTT:        {avg_p75_rtt:.2f} ms (norm: {norm_rtt:.4f})")
        log_output(f"  Avg p75 Jitter:     {avg_p75_jitter:.2f} ms (norm: {norm_jitter:.4f})")
        log_output(f"  Components: tput={0.35*norm_throughput:.4f}, "
              f"jitter={0.10*(1-norm_jitter):.4f}, "
              f"loss={0.35*(1-norm_loss_rate):.4f}, "
              f"rtt={0.20*(1-norm_rtt):.4f}")
        log_output(f"  OBJECTIVE: {objective:.4f}")

    return objective


def compute_all_per_ap_objectives(
    gat_state: Dict[str, Any],
    verbose: bool = False
) -> Dict[str, float]:
    """
    Compute objective for each AP and return as a dict.

    Args:
        gat_state: GAT state dict with 'connection_metrics'
        verbose: Print detailed breakdown

    Returns:
        Dict mapping ap_bssid -> objective value
    """
    connection_metrics = gat_state.get('connection_metrics', {})
    results = {}

    for ap_bssid in connection_metrics.keys():
        obj = compute_optuna_style_reward(gat_state, ap_bssid, verbose=verbose)
        if obj is not None:
            results[ap_bssid] = obj

    return results


def compute_per_ap_objective_with_components(
    gat_state: Dict[str, Any],
    ap_bssid: str
) -> Optional[Dict[str, float]]:
    """
    Compute objective for a single AP and return all components.

    New format from gat_streamer (all pre-computed):
    - p50_throughput: p50 across logs of (sum of uplink across clients)
    - p75_loss_rate: p75 across logs of (p95 across clients per log)
    - p75_rtt: p75 across logs of (p95 across clients per log)
    - p75_jitter: p75 across logs of (p95 across clients per log)

    Args:
        gat_state: GAT state dict with 'connection_metrics'
        ap_bssid: BSSID of the target AP

    Returns:
        Dict with 'objective', 'p50_throughput', 'norm_throughput',
        'p75_loss_rate', 'norm_loss_rate', 'p75_rtt', 'norm_rtt',
        'p75_jitter', 'norm_jitter' or None if no data
    """
    # Normalization constants
    MAX_P50_THROUGHPUT = 100.0  # Mbps (per AP)
    MAX_P75_LOSS_RATE = 1.0   # percentage (0-100)
    MAX_P75_RTT = 750.0         # ms
    MAX_P75_JITTER = 30.0       # ms

    connection_metrics = gat_state.get('connection_metrics', {})

    if ap_bssid not in connection_metrics:
        return None

    ap_data = connection_metrics[ap_bssid]
    if not ap_data:
        return None

    # Get pre-computed metrics (all computed by gat_streamer)
    p50_throughput = float(ap_data.get('p50_throughput', 0))
    p75_loss_rate = float(ap_data.get('p75_loss_rate', 0))
    p75_rtt = float(ap_data.get('p75_rtt', 0))
    p75_jitter = float(ap_data.get('p75_jitter', 0))

    # Normalize (clamp to [0, 1])
    norm_throughput = min(p50_throughput / MAX_P50_THROUGHPUT, 1.0)
    norm_loss_rate = min(p75_loss_rate / MAX_P75_LOSS_RATE, 1.0)
    norm_rtt = min(p75_rtt / MAX_P75_RTT, 1.0)
    norm_jitter = min(p75_jitter / MAX_P75_JITTER, 1.0)

    # Calculate objective
    objective = (
        0.35 * norm_throughput +
        0.10 * (1 - norm_jitter) +
        0.35 * (1 - norm_loss_rate) +
        0.20 * (1 - norm_rtt)
    )

    return {
        'objective': objective,
        'p50_throughput': p50_throughput,
        'norm_throughput': norm_throughput,
        'p75_loss_rate': p75_loss_rate,
        'norm_loss_rate': norm_loss_rate,
        'p75_rtt': p75_rtt,
        'norm_rtt': norm_rtt,
        'p75_jitter': p75_jitter,
        'norm_jitter': norm_jitter
    }


# ============================================================================
# Utility Functions
# ============================================================================

def map_width_color_to_channel(width_idx: int, color_id: int, config: Config) -> int:
    """
    Map channel width and graph color to actual channel number.

    Args:
        width_idx: Index into CHANNEL_WIDTH_OPTIONS (0=20MHz, 1=40MHz, 2=80MHz)
        color_id: Graph coloring color ID (0-5)
        config: Config object with COLOR_CHANNEL_MAP

    Returns:
        Channel number (e.g., 36, 38, 42, etc.)
    """
    width = config.CHANNEL_WIDTH_OPTIONS[width_idx]
    channel_options = config.COLOR_CHANNEL_MAP[color_id][width]

    # For 80MHz there's only one option, for others pick randomly
    if len(channel_options) == 1:
        return channel_options[0]
    else:
        return random.choice(channel_options)


def gat_state_to_graph_state(gat_state: Dict[str, Any], graph_coloring: Optional[DcSimpleAlgo] = None) -> tuple:
    """
    Convert GAT provider state dict to APGraphState for PPO.
    Also updates/creates graph coloring for the nodes.

    Args:
        gat_state: Dict with 'nodes', 'ap_order', 'edges' from GATStateProvider
        graph_coloring: Existing DcSimpleAlgo instance to update, or None to create new

    Returns:
        Tuple of (APGraphState, DcSimpleAlgo, node_colors_dict) or (None, None, None) if invalid
    """
    nodes = gat_state.get('nodes', {})
    ap_order = gat_state.get('ap_order', [])
    edges_matrix = gat_state.get('edges', [])

    if not nodes or not ap_order:
        return None, None, None

    num_aps = len(ap_order)

    # Build feature matrix [num_aps, 4]
    # Features: [channel, power, num_clients, channel_utilization]
    ap_features = np.zeros((num_aps, 4), dtype=np.float32)

    for i, bssid in enumerate(ap_order):
        node_data = nodes.get(bssid, {})
        ap_features[i, 0] = float(node_data.get('channel', 36))
        ap_features[i, 1] = float(node_data.get('power', 17))
        ap_features[i, 2] = float(node_data.get('num_clients', 0))
        ap_features[i, 3] = float(node_data.get('channel_utilization', 0))

    # Build edge index from adjacency (non-zero RSSI means connected)
    edge_sources = []
    edge_targets = []
    edges_list = []  # For graph coloring (undirected edges)

    for i in range(num_aps):
        for j in range(num_aps):
            if i != j:
                # Check if there's a connection (non-zero RSSI or same/adjacent channel)
                if edges_matrix and len(edges_matrix) > i and len(edges_matrix[i]) > j:
                    rssi = edges_matrix[i][j]
                    if rssi != 0.0:  # Has RSSI measurement
                        edge_sources.append(i)
                        edge_targets.append(j)
                        if i < j:  # Only add edge once for undirected graph
                            edges_list.append((ap_order[i], ap_order[j]))
                else:
                    # Fallback: connect based on channel adjacency
                    ch_i = ap_features[i, 0]
                    ch_j = ap_features[j, 0]
                    if abs(ch_i - ch_j) <= 8:  # Adjacent channels
                        edge_sources.append(i)
                        edge_targets.append(j)
                        if i < j:  # Only add edge once for undirected graph
                            edges_list.append((ap_order[i], ap_order[j]))

    # If no edges, add self-loops for GAT stability
    if not edge_sources:
        for i in range(num_aps):
            edge_sources.append(i)
            edge_targets.append(i)

    edge_index = np.array([edge_sources, edge_targets], dtype=np.int64)

    # =========================================================================
    # Dynamic Graph Coloring
    # =========================================================================
    # Build networkx graph for coloring
    G = nx.Graph()
    G.add_nodes_from(ap_order)
    G.add_edges_from(edges_list)

    # Create or update graph coloring
    if graph_coloring is None:
        # First call: constructor builds the graph and colors it automatically
        graph_coloring = DcSimpleAlgo(G=G, p=0.5, max_colors=6)
    else:
        # Subsequent calls: update graph structure and recolor
        current_nodes = set(graph_coloring.G.nodes())
        new_nodes = set(ap_order)

        # Add new nodes
        for node in new_nodes - current_nodes:
            graph_coloring.addVertexLazy(node)

        # Remove old nodes
        for node in current_nodes - new_nodes:
            graph_coloring.removeVertexLazy(node)

        # Update edges
        current_edges = set(graph_coloring.G.edges())
        new_edges = set(edges_list)

        for edge in new_edges - current_edges:
            graph_coloring.addEdgeLazy(edge[0], edge[1])

        for edge in current_edges - new_edges:
            graph_coloring.removeEdgeLazy(edge[0], edge[1])

        # Apply changes and recompute coloring
        graph_coloring.updateColoring()

    # Get the coloring dict {bssid: color_id} where color_id is 0 to 5
    node_colors = graph_coloring.getColoring()

    ap_graph_state = APGraphState(
        ap_features=ap_features,
        edge_index=edge_index,
        ap_bssids=ap_order,
        num_aps=num_aps
    )

    return ap_graph_state, graph_coloring, node_colors


def set_seed(seed: int):
    """Set random seeds for reproducibility."""
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


# ============================================================================
# Main Training Loop
# ============================================================================

def main():
    """
    Main online training loop.

    Uses GATStateProvider to get batched EWMA state from simulation logs.
    Each step waits for batch_size logs, computes EWMA, then takes action.
    """
    config = Config()
    set_seed(config.seed)

    print("=" * 60)
    print("WiFi AP RL - Online Training with GAT State Provider")
    print("=" * 60)
    print(f"Device: {device}")
    print(f"Channel widths: {config.CHANNEL_WIDTH_OPTIONS} MHz")
    print(f"Power levels: {config.POWER_OPTIONS}")
    print(f"GAT batch size: {config.gat_batch_size} logs per state")
    print(f"EWMA alpha: {config.gat_ewma_alpha}")
    print(f"Graph colors: 6 (mapped to channel bands)")
    print()

    # Initialize GAT State Provider (reads from bayesian_optimiser_consumer logs)
    gat_provider = GATStateProvider(
        batch_size=config.gat_batch_size,
        ewma_alpha=config.gat_ewma_alpha,
        poll_interval=0.5,
        settling_time=config.config_settling_time,
        verbose=config.verbose
    )

    # Initialize Kafka (only for sending actions)
    kafka = KafkaInterface(config)

    # Initialize PPO
    ppo = PPO(
        input_dim_ap=config.input_dim_ap,
        hidden_dim=config.hidden_dim,
        gnn_layers=config.gnn_layers,
        mlp_layers=config.mlp_layers,
        num_channels=len(config.CHANNEL_WIDTH_OPTIONS),  # Now outputs channel width (3 options)
        num_power_levels=len(config.POWER_OPTIONS),
        heads=config.heads,
        dropout=config.dropout,
        lr_actor=config.lr_actor,
        lr_critic=config.lr_critic,
        eps_clip=config.eps_clip,
        entropy_coef=config.entropy_coef,
        value_coef=config.value_coef,
    )

    # Initialize memory buffer
    buffer = Memory(config.buffer_size)

    # Load pretrained weights if available
    actor_weights_path = os.path.join(os.path.dirname(__file__), 'online_weights', 'wifi_actor_sisa.pth')
    critic_weights_path = os.path.join(os.path.dirname(__file__), 'online_weights', 'wifi_critic_sisa.pth')
    
    # Load actor weights
    if os.path.exists(actor_weights_path):
        ppo.actor.load_state_dict(torch.load(actor_weights_path, map_location=device))
        print(f"✓ Loaded pretrained actor weights from {actor_weights_path}")
    else:
        print(f"⚠ Warning: {actor_weights_path} not found, using random initialization for actor")
    
    # Load critic weights
    if os.path.exists(critic_weights_path):
        ppo.critic.load_state_dict(torch.load(critic_weights_path, map_location=device))
        print(f"✓ Loaded pretrained critic weights from {critic_weights_path}")
    else:
        print(f"⚠ Warning: {critic_weights_path} not found, using random initialization for critic")

    # Connect to Kafka for sending actions
    kafka.connect()

    # Training state
    step = 0
    update_count = 0
    prev_gat_state = None
    prev_state = None
    graph_coloring = None  # Will be created on first graph formation
    node_colors = {}       # {bssid: color} mapping

    # NEW: Optuna-style reward tracking
    # We track the objective for the target AP we sent action for
    prev_target_ap_for_reward = None  # BSSID of AP we sent action to
    prev_optuna_objective = None      # Objective value before action was applied

    print("Starting training loop...")
    print("-" * 60)

    # Statistics tracking
    batches_received = 0
    actions_sent = 0
    best_reward = -1  # Track best global objective for improvement comparison
    best_reward_step = 0  # Track at which step best reward was observed

    # Setup logging (file + console)
    output_dir = Path("./rl_output_logs")
    output_dir.mkdir(exist_ok=True)
    log_timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    rl_log_file = output_dir / f"rl_training_logs_{log_timestamp}.json"

    # Configure logger
    logger = logging.getLogger('RL_Training')
    logger.setLevel(logging.INFO)
    # File handler for structured logs
    fh = logging.FileHandler(output_dir / f"rl_training_{log_timestamp}.log")
    fh.setLevel(logging.INFO)
    fh.setFormatter(logging.Formatter('%(asctime)s - %(levelname)s - %(message)s'))
    # Console handler
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter('%(asctime)s - %(message)s'))
    logger.addHandler(fh)
    logger.addHandler(ch)

    logger.info(f"RL training started. JSON logs: rl_training_logs_{log_timestamp}.json")

    # Network settling time - wait for simulation to stabilize before collecting data
    print(f"\n[SETTLING] Waiting {config.stabilization_time} seconds for network to stabilize...")
    time.sleep(config.stabilization_time)
    print(f"[SETTLING] Done. Starting to collect batches.\n")

    try:
        while update_count < config.max_updates:
            # 1. Get batched EWMA state from GAT provider
            # This waits for batch_size logs, computes EWMA, then returns
            gat_state = gat_provider.get_next_state(timeout=6000.0)

            if gat_state is None:
                print("[WAITING] No GAT state received (timeout or no logs)...")
                continue

            batches_received += 1

            # 2. Convert GAT state to APGraphState for PPO (also updates graph coloring)
            state, graph_coloring, node_colors = gat_state_to_graph_state(gat_state, graph_coloring)

            if state is None or state.num_aps == 0:
                if config.verbose:
                    print(f"[WARN] Failed to convert GAT state to graph state")
                continue

            # 3. Compute per-AP objectives and select worst-performing AP
            per_ap_objectives = compute_all_per_ap_objectives(gat_state, verbose=False)

            if per_ap_objectives:
                # Find the AP with the lowest (worst) objective value
                worst_ap_bssid = min(per_ap_objectives, key=per_ap_objectives.get)
                target_ap_bssid = worst_ap_bssid
                target_ap_idx = state.ap_bssids.index(target_ap_bssid)
                selection_reason = "worst_performing"
            else:
                # Fallback to random if no AP has valid metrics
                target_ap_idx = int(np.random.rand() * state.num_aps)
                target_ap_bssid = state.ap_bssids[target_ap_idx]
                selection_reason = "random_fallback"

            # 4. Get action from policy (or random during warmup)
            ap_features, edge_index = state_to_tensors(state)

            # Log PPO input
            if config.verbose:
                logger.info(f"{'='*60}")
                logger.info(f"[STEP {step}] {datetime.now().strftime('%H:%M:%S')} | Batch #{batches_received}")
                logger.info(f"{'='*60}")
                logger.info(f"[GAT STATE] EWMA of {gat_state.get('batch_count', 0)} logs:")
                logger.info(f"  - Num APs: {state.num_aps}")
                logger.info(f"  - AP BSSIDs: {state.ap_bssids}")
                logger.info(f"  - Features tensor: {ap_features.shape}")
                logger.info(f"  - Edge index: {edge_index.shape} ({edge_index.shape[1]} edges)")
                logger.info(f"  - Target AP idx: {target_ap_idx} ({target_ap_bssid})")
                # Print graph coloring summary
                logger.info(f"[GRAPH COLORING] Node -> Color mapping:")
                for bssid, color_id in node_colors.items():
                    logger.info(f"    {bssid} -> color {color_id}")
                # Print actual feature values for each AP
                logger.info(f"[AP FEATURES]")
                for i, bssid in enumerate(state.ap_bssids):
                    feat = ap_features[i].cpu().numpy()
                    logger.info(f"    AP[{i}] {bssid}: ch={feat[0]:.0f}, pwr={feat[1]:.1f}, "
                          f"clients={feat[2]:.0f}, util={feat[3]:.4f}")
                # Print per-AP objective values with components and show which one is selected
                logger.info(f"[PER-AP OBJECTIVES]")
                for bssid in state.ap_bssids:
                    comp = compute_per_ap_objective_with_components(gat_state, bssid)
                    if comp is not None:
                        marker = " <-- WORST" if bssid == target_ap_bssid and selection_reason == "worst_performing" else ""
                        logger.info(f"    {bssid}: obj={comp['objective']:.4f} | "
                                    f"tput={comp['p50_throughput']:.2f} ({comp['norm_throughput']:.2f}) | "
                                    f"jitter={comp['p75_jitter']:.2f} ({comp['norm_jitter']:.2f}) | "
                                    f"loss={comp['p75_loss_rate']:.2f}% ({comp['norm_loss_rate']:.2f}) | "
                                    f"rtt={comp['p75_rtt']:.2f} ({comp['norm_rtt']:.2f}){marker}")
                    else:
                        logger.info(f"    {bssid}: N/A (no clients)")
                logger.info(f"[TARGET AP SELECTION] {target_ap_bssid} (reason: {selection_reason})")

            if step < config.warmup_steps:
                # Pure random exploration during warmup phase
                width_action = random.randint(0, len(config.CHANNEL_WIDTH_OPTIONS) - 1)
                power_action = random.randint(0, len(config.POWER_OPTIONS) - 1)
                log_prob = 0.0
                action_source = "RANDOM (warmup)"
            else:
                # Use learned policy after warmup
                with torch.no_grad():
                    result = ppo.actor(
                        ap_features=ap_features,
                        edge_index=edge_index,
                        target_ap_idx=torch.tensor([target_ap_idx], device=device),
                        deterministic=False
                    )

                width_action = result['channel_action'].item()  # PPO outputs width index
                power_action = result['power_action'].item()
                log_prob = result['log_prob'].item()
                action_source = "PPO Policy"

            # Get target AP's color and map width to channel number
            target_color = node_colors.get(target_ap_bssid, 0)
            channel_number = map_width_color_to_channel(width_action, target_color, config)
            width_val = config.CHANNEL_WIDTH_OPTIONS[width_action]

            # Check if we're in critic warmstart phase (actor training not yet started)
            # warmstart lasts for warmup_steps * warmup_critic total steps
            total_critic_warmstart_steps = config.warmup_steps + (config.warmup_critic * config.window_steps)
            in_critic_warmstart = step >= config.warmup_steps and step < total_critic_warmstart_steps

            # Log PPO output / action decision
            if config.verbose:
                power_val = config.POWER_OPTIONS[power_action]
                logger.info(f"[PPO OUTPUT] Action ({action_source}):")
                logger.info(f"  - Width: idx={width_action} -> {width_val} MHz")
                logger.info(f"  - Target AP color: {target_color}")
                logger.info(f"  - Mapped channel: {channel_number}")
                logger.info(f"  - Power: idx={power_action} -> {power_val} dBm")
                logger.info(f"  - Log prob: {log_prob:.4f}")
                if in_critic_warmstart:
                    remaining_warmstart = total_critic_warmstart_steps - step
                    logger.info(f"  [WARMSTART] Critic-only training phase - actor updates start in {remaining_warmstart} steps")

            # 5. Send action via Kafka
            current_time = time.time()
            send_success = kafka.send_action(
                ap_bssid=target_ap_bssid,
                channel_number=channel_number,
                channel_width=width_val,
                power_idx=power_action,
                timestamp=current_time
            )

            if send_success:
                actions_sent += 1
                if config.verbose:
                    print(f"[ACTION SENT] Total actions sent: {actions_sent}")
            else:
                print(f"[ACTION FAILED] Could not send action to Kafka!")

            # 6. Compute GLOBAL reward (optuna-style averaged across ALL APs)
            # Logic:
            # - Step 0: Compute objective but reward=0 (no previous action to evaluate)
            # - Step 1+: Reward = curr_objective - prev_objective (DELTA)
            # This measures improvement from our previous action

            # Always compute current objective (need it for delta at next step)
            curr_objective = compute_global_objective(
                gat_state,
                verbose=config.verbose,
                logger=logger
            )

            if(curr_objective>config.best_objective):
                config.best_objective = curr_objective

            if step == 0:
                # First step: compute objective for baseline, but no reward
                reward = 0.0
                if curr_objective is not None:
                    baseline_objective_t1 = curr_objective
                    config.best_objective = curr_objective
                    if config.verbose:
                        print(f"[GLOBAL REWARD] Step 0: Baseline objective = {curr_objective:.4f}")
            else:
                # Step 1+: Reward = DELTA (improvement from previous objective)
                if curr_objective is not None and prev_objective is not None:
                    reward = curr_objective - prev_objective  # DELTA reward

                    # Calculate % improvement vs baseline at t=1
                    pct_improvement = ((curr_objective - baseline_objective_t1) / baseline_objective_t1 * 100) if baseline_objective_t1 != 0 else 0

                    if config.verbose:
                        delta_sign = "+" if reward > 0 else ""
                        direction = "↑" if pct_improvement > 0 else ("↓" if pct_improvement < 0 else "→")
                        print(f"[GLOBAL REWARD] AP chosen at t-1: {prev_target_ap_bssid}")
                        print(f"[GLOBAL REWARD] Objective(t={step}): {curr_objective:.4f} | "
                              f"Prev: {prev_objective:.4f} | Delta: {delta_sign}{reward:.4f}")
                        print(f"[GLOBAL REWARD] vs Baseline(t=1): {baseline_objective_t1:.4f} | "
                              f"Total improvement: {pct_improvement:+.2f}% {direction}")
                        print(f"[GLOBAL REWARD] Best Score until now: {config.best_objective:.4f}")
                else:
                    reward = 0.0
                    if config.verbose:
                        print(f"[GLOBAL REWARD] N/A - Missing objective (curr={curr_objective}, prev={prev_objective})")

            # Update tracking for next iteration
            prev_target_ap_bssid = target_ap_bssid
            prev_objective = curr_objective  # Track for delta reward at next step

            # 7. Store transition in buffer
            batch_state = APGraphBatch().from_single(
                state.ap_features, state.edge_index, target_ap_idx
            )
            v_state = deepcopy(batch_state)

            if step > 0:  # Skip first step (no reward yet)
                buffer.record(
                    state=prev_batch_state,
                    channel_action=prev_width_action,  # width action for PPO
                    power_action=prev_power_action,
                    reward=reward,
                    done=False,
                    log_prob=prev_log_prob,
                    v_state=prev_v_state,
                    target_ap_idx=prev_target_ap_idx
                )
                if config.verbose:
                    logger.info(f"[BUFFER] Recorded transition at step {step-1}. Buffer size: {len(buffer.state_mb)}")

            # Store for next iteration
            prev_gat_state = gat_state
            prev_state = state
            prev_batch_state = deepcopy(batch_state)
            prev_v_state = deepcopy(v_state)
            prev_width_action = width_action
            prev_power_action = power_action
            prev_log_prob = log_prob
            prev_target_ap_idx = target_ap_idx

            step += 1

            # 8. Periodic PPO update
            if step >= config.warmup_steps and (step - config.warmup_steps) % config.window_steps == 0:
                update_count += 1
                
                # Debug logging
                buffer_size = len(buffer.state_mb)
                logger.info(f"[UPDATE TRIGGER] Step {step}, Update #{update_count}, Buffer size: {buffer_size}")
                print(f"\n[UPDATE TRIGGER] Step {step} | Update #{update_count}/{config.max_updates} | Buffer size: {buffer_size}")

                # Compute returns
                try:
                    returns = buffer.compute_returns(
                        gamma=config.gamma,
                        normalize_rewards=config.normalize_rewards
                    )
                    logger.info(f"[UPDATE DEBUG] Returns computed: len={len(returns)}, buffer had {buffer_size} transitions")
                except Exception as e:
                    logger.error(f"[UPDATE ERROR] Failed to compute returns: {e}", exc_info=True)
                    print(f"[UPDATE ERROR] Failed to compute returns: {e}")
                    returns = []

                if len(returns) > 0:
                    try:
                        # Create RolloutBuffer
                        samples = RolloutBuffer(
                            buffer, returns, returns, len(returns)
                        )

                        for epoch in range(config.n_epochs):
                            # Train critic
                            try:
                                new_values, closs = ppo.train_critic(
                                    samples, batch_size=config.batch_size
                                )
                            except Exception as e:
                                logger.error(f"[UPDATE ERROR] Critic training failed at epoch {epoch}: {e}", exc_info=True)
                                print(f"[UPDATE ERROR] Critic training failed: {e}")
                                raise

                            # Train actor (after warmup)
                            if update_count > config.warmup_critic:
                                try:
                                    samples.update_advantages(new_values)
                                    actor_samples = deepcopy(samples)
                                    actor_samples.get_reversed_slice(config.window_steps)
                                    aloss = ppo.train_actor(
                                        actor_samples, batch_size=config.batch_size
                                    )
                                except Exception as e:
                                    logger.error(f"[UPDATE ERROR] Actor training failed at epoch {epoch}: {e}", exc_info=True)
                                    print(f"[UPDATE ERROR] Actor training failed: {e}")
                                    raise
                            else:
                                aloss = ([0], [0], [0])
                                logger.info(f"[UPDATE] Actor warmup: {update_count} <= {config.warmup_critic}, skipping actor training")

                        # Print progress
                        # Note: aloss[0] is the negated PPO objective (for minimization)
                        # We negate it back to show the actual objective (positive = good)
                        actor_objective = -np.mean(aloss[0])
                        print(f"\n[UPDATE {update_count}/{config.max_updates}] "
                              f"Step {step} | "
                              f"Actor Objective: {actor_objective:.4f} | "
                              f"Entropy: {np.mean(aloss[1]):.4f} | "
                              f"Critic RMSE: {np.mean(closs[0]):.4f}")
                        logger.info(f"[UPDATE {update_count}/{config.max_updates}] "
                                    f"Step {step} | Actor Objective: {actor_objective:.4f} | "
                                    f"Entropy: {np.mean(aloss[1]):.4f} | Critic RMSE: {np.mean(closs[0]):.4f}")

                        # Save weights (overwrite same files each time)
                        try:
                            ppo.save(
                                './online_weights/wifi_actor.pth',
                                './online_weights/wifi_critic.pth'
                            )
                            logger.info(f"[UPDATE] Weights saved successfully")
                            print(f"[UPDATE] Weights saved to ./online_weights/")
                        except Exception as e:
                            logger.error(f"[UPDATE ERROR] Failed to save weights: {e}", exc_info=True)
                            print(f"[UPDATE ERROR] Failed to save weights: {e}")
                            
                    except Exception as e:
                        logger.error(f"[UPDATE ERROR] PPO update failed: {e}", exc_info=True)
                        print(f"[UPDATE ERROR] PPO update failed: {e}")
                        # Continue training even if update fails
                else:
                    warning_msg = f"[UPDATE WARNING] Update #{update_count} skipped: returns is empty! Buffer has {buffer_size} transitions but compute_returns() returned empty list."
                    logger.warning(warning_msg)
                    print(f"\n{warning_msg}")
                    print(f"[UPDATE WARNING] This may indicate an issue with buffer recording or returns computation.")

    except KeyboardInterrupt:
        print("\nTraining interrupted by user")

    finally:
        # Print summary
        logger.info("=" * 50)
        logger.info("TRAINING SUMMARY")
        logger.info(f"Total steps: {step}")
        logger.info(f"Total updates: {update_count}")
        logger.info(f"GAT batches received: {batches_received}")
        logger.info(f"Total logs processed: {gat_provider.total_logs}")
        logger.info(f"Actions sent to Kafka: {actions_sent}")
        logger.info(f"Buffer size: {len(buffer.state_mb)}")
        logger.info(f"JSON log: rl_training_logs_{log_timestamp}.json")
        logger.info("=" * 50)

        # Cleanup
        kafka.close()

        # Save final model (same files as periodic saves)
        ppo.save('./online_weights/wifi_actor.pth', './online_weights/wifi_critic.pth')
        logger.info("Training complete. Models saved to ./online_weights/")


if __name__ == '__main__':
    # Create directories
    import os
    os.makedirs('./logs', exist_ok=True)
    os.makedirs('./online_weights', exist_ok=True)

    main()