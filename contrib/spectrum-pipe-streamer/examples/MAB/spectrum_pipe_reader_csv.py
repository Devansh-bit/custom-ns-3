#!/usr/bin/env python3
"""
Named Pipe Reader for NS-3 Spectrum Analyzer Data (Binary Format with CSV Output)

This script reads binary spectrum analyzer data from named pipes (FIFOs) created
by the SpectrumPipeStreamer module and writes to CSV files.

Binary Format:
    [node_id: uint32][timestamp: double][num_values: uint32][psd_values: double array]

Usage:
    python3 spectrum_pipe_reader_csv.py --base-path /tmp/ns3-spectrum --num-nodes 3

Start this script BEFORE running the NS-3 simulation!
"""

import argparse
import logging
from queue import Queue
import signal
import sys
import threading
import time
import struct
import csv
from pathlib import Path
import os


# Configuration defaults
DEFAULT_BASE_PATH = "/tmp/ns3-spectrum"
DEFAULT_NUM_NODES = 3
DEFAULT_OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "outputs", "spectrum_data")
LOG_LEVEL = "INFO"
LOG_FORMAT = '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
LOG_FILE = os.path.join(os.path.dirname(__file__), "logs", "spectrum_pipe_reader_csv.log")

# Create logs directory
os.makedirs(os.path.join(os.path.dirname(__file__), "logs"), exist_ok=True)


class BinaryPipeReader:
    """Reads binary data from a single named pipe for one node"""
    
    def __init__(self, node_id, pipe_path, output_dir, logger, live_callback=None):
        self.node_id = node_id
        self.pipe_path = pipe_path
        self.output_dir = Path(output_dir)
        self.logger = logger
        self.running = False
        self.thread = None
        self.live_callback = live_callback
        
        # Statistics
        self.packets_received = 0
        self.bytes_received = 0
        
        # CSV file
        self.csv_file = None
        self.csv_writer = None
        self.csv_path = self.output_dir / f"node-{node_id}-spectrum.csv"
        
    def start(self):
        """Start reading from the pipe in a separate thread"""
        # Create output directory
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
        # Open CSV file
        self.csv_file = open(self.csv_path, 'w', newline='')
        self.logger.info(f"[Node {self.node_id}] Created CSV file: {self.csv_path}")
        
        self.running = True
        self.thread = threading.Thread(target=self._read_loop, daemon=True)
        self.thread.start()
        
    def stop(self):
        """Stop reading from the pipe"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=1.0)
        if self.csv_file:
            self.csv_file.close()
            self.logger.info(f"[Node {self.node_id}] Closed CSV file")
            
    def _read_loop(self):
        """Main reading loop - runs in separate thread"""
        try:
            self.logger.info(f"[Node {self.node_id}] Opening pipe: {self.pipe_path}")
            
            # Open pipe for reading (blocks until writer opens)
            with open(self.pipe_path, 'rb') as pipe:
                self.logger.info(f"[Node {self.node_id}] Pipe opened, reading binary data...")
                
                # Header format: uint32 + double + uint32 = 4 + 8 + 4 = 16 bytes
                header_size = 16
                
                while self.running:
                    # Read header
                    header_data = pipe.read(header_size)
                    if not header_data or len(header_data) < header_size:
                        # EOF or incomplete data
                        if header_data:
                            self.logger.warning(f"[Node {self.node_id}] Incomplete header ({len(header_data)} bytes)")
                        self.logger.info(f"[Node {self.node_id}] EOF reached")
                        break
                    
                    # Parse header
                    node_id, timestamp, num_values = struct.unpack('<Idi', header_data)

                    # self.logger.info(f"[Node {self.node_id}] Node ID: {node_id}, Timestamp: {timestamp}, Number of values: {num_values}")
                    
                    # Read PSD values
                    psd_size = num_values * 8  # 8 bytes per double
                    psd_data = pipe.read(psd_size)
                    
                    if len(psd_data) < psd_size:
                        self.logger.error(f"[Node {self.node_id}] Incomplete PSD data")
                        break
                    
                    self.bytes_received += header_size + psd_size
                    
                    # Parse PSD values
                    psd_values = struct.unpack(f'<{num_values}d', psd_data)

                    if self.live_callback:
                        self.live_callback(self.node_id, timestamp, psd_values)

                    # Write to CSV
                    self._write_csv_row(timestamp, psd_values)
                    
                    self.packets_received += 1
                    
                    # Print periodic updates
                    if self.packets_received % 10 == 1:
                        psd_max = max(psd_values)
                        psd_avg = sum(psd_values) / len(psd_values)
                        self.logger.debug(f"[Node {node_id}] t={timestamp:.3f}s, "
                                        f"packets={self.packets_received}, "
                                        f"values={num_values}, "
                                        f"PSD: max={psd_max:.2e}, avg={psd_avg:.2e}")
                            
        except Exception as e:
            self.logger.error(f"[Node {self.node_id}] Error reading pipe: {e}")
        finally:
            self.logger.info(f"[Node {self.node_id}] Reader thread stopped")
            
    def _write_csv_row(self, timestamp, psd_values):
        """Write timestamp and PSD values to CSV"""
        if self.packets_received == 0:
            # Write header on first packet
            header = ['timestamp'] + [f'psd_bin_{i}' for i in range(len(psd_values))]
            self.csv_writer = csv.writer(self.csv_file)
            self.csv_writer.writerow(header)
            self.csv_file.flush()
        
        # Write data row
        row = [f'{timestamp:.6f}'] + [f'{val:.6e}' for val in psd_values]
        self.csv_writer.writerow(row)
        
        # Flush periodically
        if self.packets_received % 10 == 0:
            self.csv_file.flush()
            
    def get_stats(self):
        """Return statistics dictionary"""
        return {
            'node_id': self.node_id,
            'packets_received': self.packets_received,
            'bytes_received': self.bytes_received,
            'csv_path': str(self.csv_path)
        }


class SpectrumPipeServer:
    """
    Named pipe server for receiving binary spectrum analyzer data from ns-3.
    """

    def __init__(self, base_path=DEFAULT_BASE_PATH, num_nodes=DEFAULT_NUM_NODES, 
                 output_dir=DEFAULT_OUTPUT_DIR):
        """
        Initialize the pipe server.

        Args:
            base_path: Base directory for named pipes
            num_nodes: Number of nodes to read from
            output_dir: Directory to save CSV files
        """
        self.base_path = Path(base_path)
        self.num_nodes = num_nodes
        self.output_dir = output_dir
        self.running = False
        self.readers = []
        self.live_queue = Queue()

        # Setup logging
        logging.basicConfig(
            level=getattr(logging, LOG_LEVEL),
            format=LOG_FORMAT,
            handlers=[
                logging.FileHandler(LOG_FILE),
                logging.StreamHandler(sys.stdout)
            ]
        )
        self.logger = logging.getLogger(__name__)



    def handle_live_data(self, node_id, timestamp, psd_values):
        
        # Package full data
        packet = {
            "node": node_id,
            "time": timestamp,
            "psd": psd_values
        }
        self.live_queue.put(packet)  # <-- sends full PSD to queue


    def start(self):
        """Start the pipe server and ensure all named pipes exist."""
        import os
        import stat

        self.running = True

        self.logger.info("=" * 60)
        self.logger.info("Spectrum Pipe Server Started (Binary/CSV Mode)")
        self.logger.info(f"Base path: {self.base_path}")
        self.logger.info(f"Number of nodes: {self.num_nodes}")
        self.logger.info(f"Output directory: {self.output_dir}")
        self.logger.info("Preparing named pipes and waiting for data from ns-3...")
        self.logger.info("=" * 60)

        # Create base directory if needed
        self.base_path.mkdir(parents=True, exist_ok=True)

        # Create readers for each node
        for node_id in range(self.num_nodes):
            pipe_path = self.base_path / f"node-{node_id}.pipe"

            # Create the FIFO if not already present
            if pipe_path.exists():
                # If exists but not a FIFO, remove and recreate
                if not stat.S_ISFIFO(os.stat(pipe_path).st_mode):
                    self.logger.warning(f"Existing file {pipe_path} is not a FIFO. Replacing it.")
                    pipe_path.unlink()
                    os.mkfifo(pipe_path)
                    self.logger.info(f"Created FIFO: {pipe_path}")
                else:
                    self.logger.info(f"FIFO already exists: {pipe_path}")
            else:
                os.mkfifo(pipe_path)
                self.logger.info(f"Created FIFO: {pipe_path}")

            reader = BinaryPipeReader(node_id, str(pipe_path), self.output_dir, self.logger, live_callback=self.handle_live_data)
            self.readers.append(reader)
            reader.start()

        self.logger.info("All readers started. Waiting for data...")
        self.logger.info("(Press Ctrl+C to stop)")

        try:
            # Keep running until interrupted
            while self.running:
                time.sleep(1)
        except KeyboardInterrupt:
            self.logger.info("\nReceived interrupt signal, shutting down...")
        finally:
            self.stop()

    def stop(self):
        """Stop the server and cleanup."""
        self.running = False

        self.logger.info("Stopping all readers...")
        for reader in self.readers:
            reader.stop()

        # Print statistics
        stats = self.get_statistics()
        self.logger.info("=" * 60)
        self.logger.info("Server Statistics:")
        self.logger.info(f"  Total packets: {stats['total_packets']}")
        self.logger.info(f"  Total bytes: {stats['total_bytes']}")
        self.logger.info(f"  Nodes seen: {stats['nodes_seen']}")

        if stats['data_per_node']:
            self.logger.info("  Data packets per node:")
            for node_id, count in stats['data_per_node'].items():
                self.logger.info(f"    Node {node_id}: {count} packets")
        
        self.logger.info("")
        self.logger.info("CSV Files Created:")
        for node_id in range(self.num_nodes):
            csv_path = Path(self.output_dir) / f"node-{node_id}-spectrum.csv"
            if csv_path.exists():
                size_mb = csv_path.stat().st_size / (1024 * 1024)
                self.logger.info(f"    {csv_path} ({size_mb:.2f} MB)")

        self.logger.info("=" * 60)
        self.logger.info("Server stopped")

        # Cleanup pipes
        for node_id in range(self.num_nodes):
            pipe_path = self.base_path / f"node-{node_id}.pipe"
            if pipe_path.exists():
                try:
                    pipe_path.unlink()
                    self.logger.info(f"Removed FIFO: {pipe_path}")
                except Exception as e:
                    self.logger.warning(f"Failed to remove {pipe_path}: {e}")

    def get_statistics(self):
        """Get statistics from all readers."""
        total_packets = 0
        total_bytes = 0
        data_per_node = {}

        for reader in self.readers:
            stats = reader.get_stats()
            total_packets += stats['packets_received']
            total_bytes += stats['bytes_received']
            if stats['packets_received'] > 0:
                data_per_node[stats['node_id']] = stats['packets_received']

        return {
            'total_packets': total_packets,
            'total_bytes': total_bytes,
            'nodes_seen': len(self.readers),
            'data_per_node': data_per_node
        }


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description='Spectrum Pipe Server - Receive binary spectrum data and save to CSV',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Start server with default settings (3 nodes)
  python3 spectrum_pipe_reader_csv.py

  # Start server for 5 nodes
  python3 spectrum_pipe_reader_csv.py --num-nodes 5

  # Custom base path and output directory
  python3 spectrum_pipe_reader_csv.py --base-path /tmp/my-spectrum --output-dir my_csv_data

  # With debug logging
  python3 spectrum_pipe_reader_csv.py --log-level DEBUG

Note: Start this script BEFORE running the NS-3 simulation!
      The simulation will block when opening pipes until readers connect.
        """
    )

    parser.add_argument('--base-path', type=str, default=DEFAULT_BASE_PATH,
                        help=f'Base directory for named pipes (default: {DEFAULT_BASE_PATH})')
    parser.add_argument('--num-nodes', type=int, default=DEFAULT_NUM_NODES,
                        help=f'Number of nodes to read from (default: {DEFAULT_NUM_NODES})')
    parser.add_argument('--output-dir', type=str, default=DEFAULT_OUTPUT_DIR,
                        help=f'Output directory for CSV files (default: {DEFAULT_OUTPUT_DIR})')
    parser.add_argument('--log-level', type=str, default=LOG_LEVEL,
                        choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
                        help=f'Logging level (default: {LOG_LEVEL})')

    args = parser.parse_args()

    # Update logging level for this instance
    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format=LOG_FORMAT,
        handlers=[
            logging.FileHandler(LOG_FILE),
            logging.StreamHandler(sys.stdout)
        ],
        force=True  # Override any previous configuration
    )

    # Create and start server
    server = SpectrumPipeServer(
        base_path=args.base_path,
        num_nodes=args.num_nodes,
        output_dir=args.output_dir
    )

    # Setup signal handlers
    def signal_handler(sig, frame):
        server.logger.info('\nReceived signal, shutting down...')
        server.running = False

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # Start server
    server.start()


if __name__ == '__main__':
    main()

