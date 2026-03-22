#!/usr/bin/env python3
"""
Spectrum UDP Server
Main server script to receive spectrum analyzer data from ns-3 simulation
"""

import socket
import argparse
import logging
import signal
import sys
from spectrum_receiver import SpectrumReceiver
import config


class SpectrumUDPServer:
    """
    UDP server for receiving spectrum analyzer data from ns-3.
    """

    def __init__(self, host=config.DEFAULT_HOST, port=config.DEFAULT_PORT,
                 output_dir=config.OUTPUT_DIR, save_to_file=config.SAVE_TO_FILE):
        """
        Initialize the UDP server.

        Args:
            host: Host address to bind to
            port: UDP port to listen on
            output_dir: Directory to save data
            save_to_file: Whether to save data to files
        """
        self.host = host
        self.port = port
        self.running = False

        # Setup logging
        logging.basicConfig(
            level=getattr(logging, config.LOG_LEVEL),
            format=config.LOG_FORMAT,
            handlers=[
                logging.FileHandler(config.LOG_FILE),
                logging.StreamHandler(sys.stdout)
            ]
        )
        self.logger = logging.getLogger(__name__)

        # Create receiver
        self.receiver = SpectrumReceiver(
            output_dir=output_dir,
            queue_max_size=config.QUEUE_MAX_SIZE,
            save_to_file=save_to_file
        )

        # Create UDP socket
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    def start(self):
        """Start the UDP server."""
        self.socket.bind((self.host, self.port))
        self.running = True

        self.logger.info("="*60)
        self.logger.info("Spectrum UDP Server Started")
        self.logger.info(f"Listening on {self.host}:{self.port}")
        self.logger.info(f"Output directory: {self.receiver.output_dir}")
        self.logger.info(f"Save to file: {self.receiver.save_to_file}")
        self.logger.info("Waiting for data from ns-3...")
        self.logger.info("="*60)

        try:
            while self.running:
                try:
                    # Receive UDP packet
                    data, addr = self.socket.recvfrom(config.MAX_PACKET_SIZE)

                    # Process packet
                    self.receiver.process_packet(data, addr)

                except socket.timeout:
                    continue
                except Exception as e:
                    self.logger.error(f"Error receiving packet: {e}")

        except KeyboardInterrupt:
            self.logger.info("\nReceived interrupt signal, shutting down...")
        finally:
            self.stop()

    def stop(self):
        """Stop the server and cleanup."""
        self.running = False
        self.receiver.close()
        self.socket.close()

        # Print statistics
        stats = self.receiver.get_statistics()
        self.logger.info("="*60)
        self.logger.info("Server Statistics:")
        self.logger.info(f"  Total packets: {stats['total_packets']}")
        self.logger.info(f"  Handshake packets: {stats['handshake_packets']}")
        self.logger.info(f"  Data packets: {stats['data_packets']}")
        self.logger.info(f"  Parse errors: {stats['parse_errors']}")
        self.logger.info(f"  Queue full errors: {stats['queue_full_errors']}")
        self.logger.info(f"  Nodes seen: {stats['nodes_seen']}")

        if stats['data_per_node']:
            self.logger.info("  Data packets per node:")
            for node_id, count in stats['data_per_node'].items():
                self.logger.info(f"    Node {node_id}: {count} packets")

        self.logger.info("="*60)
        self.logger.info("Server stopped")


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description='Spectrum UDP Server - Receive spectrum analyzer data from ns-3',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Start server on default port 9000
  python3 server.py

  # Start server on custom port
  python3 server.py --port 9001

  # Start server without saving to files
  python3 server.py --no-save

  # Custom output directory
  python3 server.py --output-dir my_spectrum_data
        """
    )

    parser.add_argument('--host', type=str, default=config.DEFAULT_HOST,
                        help=f'Host address to bind to (default: {config.DEFAULT_HOST})')
    parser.add_argument('--port', type=int, default=config.DEFAULT_PORT,
                        help=f'UDP port to listen on (default: {config.DEFAULT_PORT})')
    parser.add_argument('--output-dir', type=str, default=config.OUTPUT_DIR,
                        help=f'Output directory for data files (default: {config.OUTPUT_DIR})')
    parser.add_argument('--no-save', action='store_true',
                        help='Do not save data to files (default: save enabled)')
    parser.add_argument('--log-level', type=str, default=config.LOG_LEVEL,
                        choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
                        help=f'Logging level (default: {config.LOG_LEVEL})')

    args = parser.parse_args()

    # Update config
    config.LOG_LEVEL = args.log_level

    # Create and start server
    server = SpectrumUDPServer(
        host=args.host,
        port=args.port,
        output_dir=args.output_dir,
        save_to_file=not args.no_save
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
