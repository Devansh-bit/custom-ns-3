#!/usr/bin/env python3
"""
Spectrum Pipe Reader
Main reader script to receive spectrum analyzer data from ns-3 simulation via named pipes
"""

import argparse
import json
import logging
import signal
import sys
import threading
import time
from pathlib import Path
import os


# Configuration defaults
DEFAULT_BASE_PATH = "/tmp/ns3-spectrum"
DEFAULT_NUM_NODES = 3
LOG_LEVEL = "INFO"
LOG_FORMAT = '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
LOG_FILE = os.path.join(os.path.dirname(__file__), "logs", "spectrum_pipe_reader.log")

# Create logs directory
os.makedirs(os.path.join(os.path.dirname(__file__), "logs"), exist_ok=True)


class PipeReader:
    """Reads data from a single named pipe for one node"""
    
    def __init__(self, node_id, pipe_path, logger, stats_callback=None):
        self.node_id = node_id
        self.pipe_path = pipe_path
        self.logger = logger
        self.stats_callback = stats_callback
        self.running = False
        self.thread = None
        
        # Statistics
        self.packets_received = 0
        self.bytes_received = 0
        
    def start(self):
        """Start reading from the pipe in a separate thread"""
        self.running = True
        self.thread = threading.Thread(target=self._read_loop, daemon=True)
        self.thread.start()
        
    def stop(self):
        """Stop reading from the pipe"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=1.0)
            
    def _read_loop(self):
        """Main reading loop - runs in separate thread"""
        try:
            self.logger.info(f"[Node {self.node_id}] Opening pipe: {self.pipe_path}")
            
            # Open pipe for reading (blocks until writer opens)
            with open(self.pipe_path, 'r') as pipe:
                self.logger.info(f"[Node {self.node_id}] Pipe opened, reading data...")
                
                buffer = ""
                while self.running:
                    # Read data from pipe
                    chunk = pipe.read(4096)
                    if not chunk:
                        # EOF reached (writer closed pipe)
                        self.logger.info(f"[Node {self.node_id}] EOF reached")
                        break
                        
                    buffer += chunk
                    self.bytes_received += len(chunk)
                    
                    # Process complete lines (line-delimited JSON)
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        if line.strip():
                            self._process_line(line)
                            
        except Exception as e:
            self.logger.error(f"[Node {self.node_id}] Error reading pipe: {e}")
        finally:
            self.logger.info(f"[Node {self.node_id}] Reader thread stopped")
            
    def _process_line(self, line):
        """Process one line of JSON data"""
        try:
            data = json.loads(line)
            msg_type = data.get('type', 'data')
            
            if msg_type == 'data':
                # PSD data packet
                timestamp = data.get('timestamp', 0)
                psd = data.get('psd', [])
                node_id = data.get('node_id', self.node_id)
                
                self.packets_received += 1
                
                # Print periodic updates
                if self.packets_received % 10 == 1:
                    if psd:
                        psd_max = max(psd)
                        psd_avg = sum(psd) / len(psd)
                        self.logger.debug(f"[Node {node_id}] t={timestamp:.3f}s, "
                                        f"packets={self.packets_received}, "
                                        f"PSD: max={psd_max:.2e}, avg={psd_avg:.2e}")
                              
                # Optional: Call callback for custom processing
                if self.stats_callback:
                    self.stats_callback(node_id, timestamp, psd)
                    
            else:
                self.logger.warning(f"[Node {self.node_id}] Unknown message type: {msg_type}")
                
        except json.JSONDecodeError as e:
            self.logger.error(f"[Node {self.node_id}] JSON decode error: {e}")
        except Exception as e:
            self.logger.error(f"[Node {self.node_id}] Error processing data: {e}")
            
    def get_stats(self):
        """Return statistics dictionary"""
        return {
            'node_id': self.node_id,
            'packets_received': self.packets_received,
            'bytes_received': self.bytes_received
        }


class SpectrumPipeServer:
    """
    Named pipe server for receiving spectrum analyzer data from ns-3.
    """

    def __init__(self, base_path=DEFAULT_BASE_PATH, num_nodes=DEFAULT_NUM_NODES):
        """
        Initialize the pipe server.

        Args:
            base_path: Base directory for named pipes
            num_nodes: Number of nodes to read from
        """
        self.base_path = Path(base_path)
        self.num_nodes = num_nodes
        self.running = False
        self.readers = []

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

    def start(self):
        """Start the pipe server and ensure all named pipes exist."""
        import os
        import stat

        self.running = True

        self.logger.info("=" * 60)
        self.logger.info("Spectrum Pipe Server Started")
        self.logger.info(f"Base path: {self.base_path}")
        self.logger.info(f"Number of nodes: {self.num_nodes}")
        self.logger.info("Preparing named pipes and waiting for data from ns-3...")
        self.logger.info("=" * 60)

        # Create base directory if needed
        self.base_path.mkdir(parents=True, exist_ok=True)

        # Create readers for each node
        for node_id in range(self.num_nodes):
            pipe_path = self.base_path / f"node-{node_id}.pipe"

            # --- NEW: Create the FIFO if not already present ---
            if pipe_path.exists():
                # If exists but not a FIFO (e.g. regular file), remove and recreate
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
            # ---------------------------------------------------

            reader = PipeReader(node_id, str(pipe_path), self.logger)
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
        self.logger.info("="*60)
        self.logger.info("Server Statistics:")
        self.logger.info(f"  Total packets: {stats['total_packets']}")
        self.logger.info(f"  Total bytes: {stats['total_bytes']}")
        self.logger.info(f"  Nodes seen: {stats['nodes_seen']}")

        if stats['data_per_node']:
            self.logger.info("  Data packets per node:")
            for node_id, count in stats['data_per_node'].items():
                self.logger.info(f"    Node {node_id}: {count} packets")

        self.logger.info("="*60)
        self.logger.info("Server stopped")

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
        description='Spectrum Pipe Server - Receive spectrum analyzer data from ns-3 via named pipes',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Start server with default settings (3 nodes)
  python3 spectrum_pipe_reader.py

  # Start server for 5 nodes
  python3 spectrum_pipe_reader.py --num-nodes 5

  # Custom base path
  python3 spectrum_pipe_reader.py --base-path /tmp/my-spectrum

  # With debug logging
  python3 spectrum_pipe_reader.py --log-level DEBUG

Note: Start this script BEFORE running the NS-3 simulation!
      The simulation will block when opening pipes until readers connect.
        """
    )

    parser.add_argument('--base-path', type=str, default=DEFAULT_BASE_PATH,
                        help=f'Base directory for named pipes (default: {DEFAULT_BASE_PATH})')
    parser.add_argument('--num-nodes', type=int, default=DEFAULT_NUM_NODES,
                        help=f'Number of nodes to read from (default: {DEFAULT_NUM_NODES})')
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
        num_nodes=args.num_nodes
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
