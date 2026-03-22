#!/usr/bin/env python3
"""
Spectrum Data Receiver
Handles incoming spectrum analyzer data from ns-3 simulation
"""

import json
import logging
from collections import defaultdict
from queue import Queue, Full
import numpy as np
from datetime import datetime
import os


class SpectrumReceiver:
    """
    Receives and processes spectrum analyzer data from UDP stream.
    Demultiplexes data by node_id and maintains per-AP queues.
    """

    def __init__(self, output_dir='spectrum_data', queue_max_size=1000, save_to_file=True):
        """
        Initialize the spectrum receiver.

        Args:
            output_dir: Directory to save received data
            queue_max_size: Maximum size of per-AP queues
            save_to_file: Whether to save data to files
        """
        self.logger = logging.getLogger(__name__)
        self.output_dir = output_dir
        self.queue_max_size = queue_max_size
        self.save_to_file = save_to_file

        # Per-node data structures
        self.node_metadata = {}  # node_id -> metadata dict
        self.node_queues = defaultdict(lambda: Queue(maxsize=queue_max_size))  # node_id -> Queue
        self.node_data_count = defaultdict(int)  # node_id -> packet count

        # File handles for saving data
        self.node_files = {}  # node_id -> file handle

        # Statistics
        self.stats = {
            'total_packets': 0,
            'handshake_packets': 0,
            'data_packets': 0,
            'parse_errors': 0,
            'queue_full_errors': 0
        }

        # Create output directory
        if self.save_to_file:
            os.makedirs(self.output_dir, exist_ok=True)
            self.logger.info(f"Output directory: {self.output_dir}")

    def process_packet(self, data, addr):
        """
        Process a received UDP packet.

        Args:
            data: Raw packet data (bytes)
            addr: Source address tuple (ip, port)
        """
        self.stats['total_packets'] += 1

        try:
            # Decode and parse JSON
            json_str = data.decode('utf-8')
            packet = json.loads(json_str)

            packet_type = packet.get('type', 'unknown')
            node_id = packet.get('node_id')

            if node_id is None:
                self.logger.warning(f"Packet missing node_id from {addr}")
                return

            if packet_type == 'init':
                self._handle_handshake(packet, addr)
            elif packet_type == 'data':
                self._handle_data(packet, addr)
            else:
                self.logger.warning(f"Unknown packet type: {packet_type} from node {node_id}")

        except json.JSONDecodeError as e:
            self.stats['parse_errors'] += 1
            self.logger.error(f"JSON parse error from {addr}: {e}")
        except Exception as e:
            self.stats['parse_errors'] += 1
            self.logger.error(f"Error processing packet from {addr}: {e}")

    def _handle_handshake(self, packet, addr):
        """Handle initial handshake packet with metadata."""
        node_id = packet['node_id']
        self.stats['handshake_packets'] += 1

        # Store metadata
        self.node_metadata[node_id] = {
            'node_id': node_id,
            'mac': packet.get('mac', 'unknown'),
            'freq_start': packet.get('freq_start'),
            'freq_resolution': packet.get('freq_resolution'),
            'num_bins': packet.get('num_bins'),
            'first_seen': datetime.now().isoformat()
        }

        self.logger.info(f"Handshake from AP {node_id} (MAC: {packet.get('mac')})")
        self.logger.info(f"  Frequency: {packet.get('freq_start')/1e9:.3f} GHz, "
                        f"Resolution: {packet.get('freq_resolution')/1e3:.1f} kHz, "
                        f"Bins: {packet.get('num_bins')}")

        # Open file for this node
        if self.save_to_file and node_id not in self.node_files:
            filename = os.path.join(self.output_dir, f'node_{node_id}_data.csv')
            self.node_files[node_id] = open(filename, 'w')
            # Write header
            self.node_files[node_id].write("timestamp,psd_values\n")
            self.logger.info(f"  Output file: {filename}")

    def _handle_data(self, packet, addr):
        """Handle spectrum data packet."""
        node_id = packet['node_id']
        self.stats['data_packets'] += 1
        self.node_data_count[node_id] += 1

        timestamp = packet.get('timestamp')
        psd_data = packet.get('psd', [])

        # Check if we have metadata
        if node_id not in self.node_metadata:
            self.logger.warning(f"Data from AP {node_id} before handshake - skipping")
            return

        # Create data record
        data_record = {
            'node_id': node_id,
            'timestamp': timestamp,
            'psd': np.array(psd_data, dtype=np.float64)
        }

        # Add to queue (non-blocking)
        try:
            self.node_queues[node_id].put_nowait(data_record)
        except Full:
            self.stats['queue_full_errors'] += 1
            self.logger.warning(f"Queue full for AP {node_id}, dropping packet")

        # Save to file
        if self.save_to_file and node_id in self.node_files:
            # Save as comma-separated PSD values
            psd_str = ','.join([f'{val:.6e}' for val in psd_data])
            self.node_files[node_id].write(f"{timestamp},{psd_str}\n")
            self.node_files[node_id].flush()

        # Log periodically
        if self.node_data_count[node_id] % 10 == 0:
            self.logger.debug(f"AP {node_id}: received {self.node_data_count[node_id]} packets")

    def get_data(self, node_id, timeout=None):
        """
        Get next data packet from a specific AP's queue.

        Args:
            node_id: Node ID to get data from
            timeout: Timeout in seconds (None = blocking, 0 = non-blocking)

        Returns:
            Data record dict or None if queue empty (non-blocking mode)
        """
        if node_id not in self.node_queues:
            return None

        try:
            if timeout == 0:
                return self.node_queues[node_id].get_nowait()
            else:
                return self.node_queues[node_id].get(timeout=timeout)
        except:
            return None

    def get_metadata(self, node_id=None):
        """
        Get metadata for a specific node or all nodes.

        Args:
            node_id: Specific node ID, or None for all nodes

        Returns:
            Metadata dict or dict of all metadata
        """
        if node_id is not None:
            return self.node_metadata.get(node_id)
        return self.node_metadata

    def get_statistics(self):
        """Get receiver statistics."""
        stats = self.stats.copy()
        stats['nodes_seen'] = len(self.node_metadata)
        stats['data_per_node'] = dict(self.node_data_count)
        return stats

    def close(self):
        """Close all file handles."""
        for node_id, f in self.node_files.items():
            f.close()
            self.logger.info(f"Closed file for node {node_id}")
        self.node_files.clear()
