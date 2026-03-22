"""
WiFi AP RL - Graph Builder for Kafka Data Processing

This file contains flexible classes for converting Kafka AP metrics
into graph format for the GAT+PPO model.

EXTENSIBILITY NOTES:
====================

This file is intentionally flexible. You'll need to modify:

1. APGraphBuilder.extract_node_features() - Define what features to use for AP nodes
2. APGraphBuilder.build_edges() - Define how APs connect (interference, distance, etc.)
3. APGraphBuilder.normalize_values - Set normalization constants

The actual features depend on what your Kafka message contains.
See comments in each method for guidance.
"""

import numpy as np
import torch
from dataclasses import dataclass
from typing import Dict, List, Optional, Any


# ============================================================================
# APGraphState - Data container for graph state
# ============================================================================

@dataclass
class APGraphState:
    """
    Container for AP interaction graph state.

    Attributes:
        ap_features: numpy array [num_aps, feature_dim]
        edge_index: numpy array [2, num_edges]
        ap_bssids: List of AP MAC addresses (for mapping index to AP)
        num_aps: Number of APs in the graph
    """
    ap_features: np.ndarray
    edge_index: np.ndarray
    ap_bssids: List[str]
    num_aps: int


# ============================================================================
# APGraphBuilder - Flexible Kafka to Graph Converter
# ============================================================================

class APGraphBuilder:
    """
    Converts Kafka metrics to graph format.

    EXTENSIBILITY NOTES:
    --------------------

    Node Features:
    - Modify `extract_node_features()` to change what features are used
    - Features come from Kafka message's `ap_metrics[bssid]` dict
    - Current placeholders: channel, tx_power, num_clients, utilization
    - Add/remove features by editing the feature list

    Edge Construction:
    - Modify `build_edges()` to change AP-AP connectivity
    - Current: Connect APs on same/adjacent channels (interference)
    - Could change to: distance-based, all-to-all, etc.

    Normalization:
    - Set normalize_values dict to define per-feature max values
    - Features are divided by these values for [0,1] range
    """

    def __init__(self, normalize: bool = True, normalize_values: Optional[Dict] = None):
        """
        Initialize the graph builder.

        Args:
            normalize: Whether to normalize features
            normalize_values: Dict mapping feature index to max value for normalization
                             Example: {0: 200.0, 1: 30.0, 2: 50.0, 3: 1.0}
        """
        self.normalize = normalize

        # =====================================================================
        # ADJUST THIS: Define max values for normalization
        # These should match the order of features in extract_node_features()
        # =====================================================================
        self.normalize_values = normalize_values or np.array([
            200.0,   # channel (5GHz channels go up to ~165)
            30.0,    # tx_power (dBm, typically 10-25)
            50.0,    # num_clients (max expected clients per AP)
            1.0,     # channel_utilization (already 0-1)
            # Add more as needed...
        ])

    def process(self, kafka_data: dict) -> Optional[APGraphState]:
        """
        Convert Kafka message to graph tensors.

        Args:
            kafka_data: Dict with structure like:
                {
                    'ap_metrics': {
                        'bssid1': {'channel': 36, 'tx_power': 20, ...},
                        'bssid2': {'channel': 40, 'tx_power': 18, ...},
                        ...
                    },
                    ...
                }

        Returns:
            APGraphState with ap_features, edge_index, ap_bssids
            or None if no APs found
        """
        # =====================================================================
        # ADJUST THIS: Change 'ap_metrics' to match your Kafka message structure
        # =====================================================================
        ap_metrics = kafka_data.get('ap_metrics', {})

        # Also check 'data' wrapper if present (like in SHITTY BO format)
        if not ap_metrics and 'data' in kafka_data:
            ap_metrics = kafka_data['data'].get('ap_metrics', {})

        # Sort BSSIDs for consistent ordering
        ap_bssids = sorted(ap_metrics.keys())
        num_aps = len(ap_bssids)

        if num_aps == 0:
            return None

        # Extract features for each AP
        features = []
        for bssid in ap_bssids:
            feat = self.extract_node_features(ap_metrics[bssid])
            features.append(feat)
        ap_features = np.array(features, dtype=np.float32)

        # Normalize if enabled
        if self.normalize:
            ap_features = self._normalize(ap_features)

        # Build edges
        edge_index = self.build_edges(ap_bssids, ap_metrics)

        return APGraphState(
            ap_features=ap_features,
            edge_index=edge_index,
            ap_bssids=ap_bssids,
            num_aps=num_aps
        )

    def extract_node_features(self, ap_data: dict) -> np.ndarray:
        """
        Extract feature vector for one AP.

        =====================================================================
        MODIFY THIS METHOD to change what features are used.
        =====================================================================

        The features you extract depend on what's available in your Kafka
        message. Common options based on SHITTY BO format:

        From ap_metrics[bssid]:
        - 'channel': Current WiFi channel
        - 'channel_utilization': How busy the channel is (0-1)
        - 'associated_clients': Number of connected clients
        - 'bytes_sent': Total bytes transmitted
        - 'bytes_received': Total bytes received

        From connection_metrics (per-client, needs aggregation):
        - 'uplink_throughput_mbps'
        - 'downlink_throughput_mbps'
        - 'packet_loss_rate'
        - 'mac_retry_rate'
        - 'mean_rtt_latency'
        - 'ap_view_rssi'
        - 'ap_view_snr'

        Example of adding aggregated client metrics:
            connection_metrics = ap_data.get('connection_metrics', {})
            avg_throughput = np.mean([
                c.get('downlink_throughput_mbps', 0)
                for c in connection_metrics.values()
            ]) if connection_metrics else 0

        Args:
            ap_data: Dict containing metrics for a single AP

        Returns:
            numpy array of features
        """
        # =====================================================================
        # CURRENT PLACEHOLDER FEATURES - MODIFY AS NEEDED
        # =====================================================================
        return np.array([
            float(ap_data.get('channel', 36)),
            float(ap_data.get('tx_power', 20.0)),
            float(ap_data.get('associated_clients', 0)),
            float(ap_data.get('channel_utilization', 0.0)),
            # Add more features here...
            # float(ap_data.get('bytes_sent', 0)) / 1e9,
            # float(ap_data.get('bytes_received', 0)) / 1e9,
        ], dtype=np.float32)

    def build_edges(self, ap_bssids: List[str], ap_metrics: dict) -> np.ndarray:
        """
        Build edge index for AP-AP graph.

        =====================================================================
        MODIFY THIS METHOD to change connectivity logic.
        =====================================================================

        Current strategy: Connect APs that are on same or adjacent channels
        (potential interference).

        Alternative strategies you might implement:
        1. All-to-all: Every AP connected to every other AP
        2. Distance-based: Connect APs within certain distance (needs location data)
        3. Interference-based: Use measured interference levels
        4. K-nearest: Connect each AP to K closest APs

        For all-to-all connections:
            for i in range(len(ap_bssids)):
                for j in range(len(ap_bssids)):
                    if i != j:
                        src.append(i)
                        dst.append(j)

        Args:
            ap_bssids: List of AP MAC addresses
            ap_metrics: Dict of AP metrics

        Returns:
            numpy array [2, num_edges] with (source, destination) pairs
        """
        src, dst = [], []

        for i, bssid_i in enumerate(ap_bssids):
            for j, bssid_j in enumerate(ap_bssids):
                if i != j:
                    ch_i = ap_metrics[bssid_i].get('channel', 0)
                    ch_j = ap_metrics[bssid_j].get('channel', 0)

                    # =========================================================
                    # CURRENT: Connect if same or adjacent channel (interference)
                    # Adjacent = within 4 channels (20MHz spacing in 5GHz)
                    # =========================================================
                    if ch_i == ch_j or abs(ch_i - ch_j) <= 4:
                        src.append(i)
                        dst.append(j)

        # If no edges, add self-loops for GAT stability
        if not src:
            src = list(range(len(ap_bssids)))
            dst = list(range(len(ap_bssids)))

        return np.array([src, dst], dtype=np.int64)

    def _normalize(self, features: np.ndarray) -> np.ndarray:
        """Normalize features by dividing by max values."""
        if len(self.normalize_values) != features.shape[1]:
            # If dimensions don't match, do simple scaling
            return features / (np.max(np.abs(features), axis=0) + 1e-8)
        return features / (self.normalize_values + 1e-8)

    def get_feature_dim(self) -> int:
        """Return the dimension of node features."""
        # This should match the number of features in extract_node_features()
        return len(self.normalize_values)


# ============================================================================
# Helper functions
# ============================================================================

def get_ap_index_by_bssid(state: APGraphState, bssid: str) -> int:
    """
    Get the index of an AP by its BSSID.

    Args:
        state: APGraphState containing the graph
        bssid: MAC address of the AP

    Returns:
        Index of the AP in the graph, or -1 if not found
    """
    try:
        return state.ap_bssids.index(bssid)
    except ValueError:
        return -1


def state_to_tensors(state: APGraphState, device: torch.device = None):
    """
    Convert APGraphState to PyTorch tensors.

    Args:
        state: APGraphState to convert
        device: PyTorch device to place tensors on

    Returns:
        Tuple of (ap_features, edge_index) as tensors
    """
    if device is None:
        device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')

    ap_features = torch.from_numpy(state.ap_features).float().to(device)
    edge_index = torch.from_numpy(state.edge_index).long().to(device)

    return ap_features, edge_index
