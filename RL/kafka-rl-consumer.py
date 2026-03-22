#!/usr/bin/env python3
"""
Bayesian Optimizer - Kafka Metrics Consumer
=============================================

This script consumes WiFi simulation metrics from Kafka.
It will be moved to the Bayesian Optimizer repository later.

For now: Just receives and displays metrics (no optimization logic)
Also writes logs to a file for Optuna to consume.

Usage:
    python3 bayesian-optimizer-consumer.py
    python3 bayesian-optimizer-consumer.py --log-file ./output_logs/my_logs.json

Requirements:
    pip install kafka-python
"""

import json
import sys
import time
import argparse
from datetime import datetime
from pathlib import Path
from kafka import KafkaConsumer
from kafka.errors import KafkaError


class MetricsConsumer:
    """Consumes metrics from Kafka simulator-metrics topic"""

    def __init__(self, broker='localhost:9092', topic='ns3-metrics', group_id='bayesian-optimizer', log_file='./output_logs/gat_consumer_logs.json'):
        self.broker = broker
        self.topic = topic
        self.group_id = group_id
        self.consumer = None
        self.message_count = 0

        # Setup log file for Optuna integration
        if log_file:
            self.log_file = Path(log_file)
        else:
            # Auto-generate timestamped log file
            output_dir = Path("./output_logs")
            output_dir.mkdir(exist_ok=True)
            self.log_file = output_dir / f"simulation_logs_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"

        self.entries_written = 0

    def connect(self):
        """Connect to Kafka"""
        print(f"Connecting to Kafka broker: {self.broker}")
        print(f"Topic: {self.topic}")
        print(f"Consumer Group: {self.group_id}")
        print(f"Log File: {self.log_file}")
        print("-" * 80)

        try:
            # Don't deserialize automatically - we'll do it manually for better error handling
            self.consumer = KafkaConsumer(
                self.topic,
                bootstrap_servers=[self.broker],
                group_id=self.group_id,
                auto_offset_reset='earliest',  # Read from beginning if no committed offset
                enable_auto_commit=True,
                value_deserializer=None  # Manual deserialization for error handling
            )
            print("✓ Connected to Kafka successfully")
            print(f"✓ Writing logs to: {self.log_file}")
            print("Waiting for metrics...\n")
            return True

        except Exception as e:
            print(f"✗ Failed to connect to Kafka: {e}")
            return False

    def process_message(self, message):
        """Process a single metrics message"""
        self.message_count += 1

        sim_id = message.key.decode('utf-8') if message.key else 'unknown'

        # Manually decode and parse JSON with detailed error handling
        raw_value = message.value.decode('utf-8')

        try:
            data = json.loads(raw_value)
        except json.JSONDecodeError as e:
            print("\n" + "=" * 80)
            print(f"✗ JSON PARSE ERROR in MESSAGE #{self.message_count}")
            print("=" * 80)
            print(f"Simulation ID: {sim_id}")
            print(f"Error: {e}")
            print(f"Error at position: {e.pos}")
            print(f"Line: {e.lineno}, Column: {e.colno}")
            print("-" * 80)
            print("RAW MESSAGE (first 2000 chars):")
            print(raw_value[:2000])
            print("-" * 80)

            # Show context around the error
            if e.pos and e.pos < len(raw_value):
                start = max(0, e.pos - 100)
                end = min(len(raw_value), e.pos + 100)
                print(f"\nCONTEXT AROUND ERROR (position {e.pos}):")
                print(raw_value[start:end])
                print(" " * (e.pos - start) + "^^^^ ERROR HERE")
            print("=" * 80)
            raise

        # Extract basic info from NS-3 simulation
        sim_timestamp = data.get('timestamp_unix', 0)
        sim_time = data.get('sim_time_seconds', 0.0)

        # CRITICAL: Use wall-clock time for log timestamp, NOT simulation time
        # This ensures Optuna can filter logs from the last N seconds correctly
        wall_clock_timestamp = time.time()

        # Display clean readable summary
        print("\n" + "=" * 80)
        print(f"📊 MSG #{self.message_count} | Sim: {sim_id} | Time: {sim_time:.0f}s")
        print("=" * 80)
        
        ap_metrics = data.get('ap_metrics', {})
        
        # AP Summary Table
        print(f"\n🔷 ACCESS POINTS ({len(ap_metrics)})")
        print("-" * 80)
        print(f"{'BSSID':<20} {'CH':>4} {'PWR':>5} {'UTIL':>6} {'STA':>4} {'TPUT':>8}")
        print("-" * 80)
        
        total_clients = 0
        total_tput = 0
        for bssid, ap in ap_metrics.items():
            ch = ap.get('channel', 0)
            pwr = ap.get('tx_power_dbm', 0)
            util = ap.get('channel_utilization', 0)
            clients = ap.get('associated_clients', 0)
            tput = ap.get('throughput_mbps', 0)
            total_clients += clients
            total_tput += tput
            print(f"{bssid:<20} {ch:>4} {pwr:>5.0f} {util:>5.0%} {clients:>4} {tput:>7.1f}M")
        
        print("-" * 80)
        print(f"{'TOTAL':<20} {'':<4} {'':<5} {'':<6} {total_clients:>4} {total_tput:>7.1f}M")
        
        # Connection Metrics Summary (compact)
        conn_count = sum(len(ap.get('connection_metrics', {})) for ap in ap_metrics.values())
        if conn_count > 0:
            print(f"\n🔗 CONNECTIONS ({conn_count} clients)")
            print("-" * 80)
            print(f"{'STA→AP':<25} {'RTT':>6} {'JITTER':>7} {'LOSS':>6} {'UP↑':>7} {'DN↓':>7}")
            print("-" * 80)
            
            for bssid, ap in ap_metrics.items():
                for conn_key, m in ap.get('connection_metrics', {}).items():
                    sta = m.get('sta_address', '?')[-5:]
                    ap_short = bssid[-5:]
                    rtt = m.get('mean_rtt_latency', 0)
                    jitter = m.get('jitter_ms', 0)
                    loss = m.get('packet_loss_rate', 0)
                    up = m.get('uplink_throughput_mbps', 0)
                    down = m.get('downlink_throughput_mbps', 0)
                    print(f"{sta}→{ap_short:<18} {rtt:>5.0f}ms {jitter:>6.1f}ms {loss:>5.1f}% {up:>6.1f}M {down:>6.1f}M")
        
        # Neighbor Summary (only show if neighbors exist)
        has_neighbors = any(
            any(ch_data.get('neighbors') for ch_data in ap.get('scanning_channel_data', {}).values())
            for ap in ap_metrics.values()
        )
        if has_neighbors:
            print(f"\n📡 NEIGHBORS")
            print("-" * 80)
            for bssid, ap in ap_metrics.items():
                neighbors = []
                for ch_key, ch_data in ap.get('scanning_channel_data', {}).items():
                    for n in ch_data.get('neighbors', []):
                        neighbors.append(f"{n['bssid'][-5:]}@{n['channel']}:{n['rssi']:.0f}dB")
                if neighbors:
                    print(f"{bssid[-8:]}: {' | '.join(neighbors)}")
        
        print("=" * 80)

        # Write to log file for Optuna integration
        # Format matches what optuna_clean.py expects
        log_entry = {
            'message_number': self.message_count,
            'simulation_id': sim_id,
            'timestamp': wall_clock_timestamp,  # Wall-clock time for filtering
            'sim_timestamp': sim_timestamp,      # NS-3 simulation timestamp (for reference)
            'sim_time': sim_time,
            'data': data  # Full data (not compacted - for debugging)
        }

        # Append to log file
        with open(self.log_file, 'a') as f:
            json.dump(log_entry, f)
            f.write('\n')

        self.entries_written += 1
        print(f"✓ Written to log file (entry #{self.entries_written})")

        # Return the data for potential processing
        return data

    def run(self):
        """Main consumer loop"""
        if not self.connect():
            return

        try:
            print("Listening for metrics... (Press Ctrl+C to stop)\n")

            for message in self.consumer:
                # Process each message
                data = self.process_message(message)

                # TODO: Later, this is where Bayesian Optimizer logic will go
                # For now, we just receive and display

        except KeyboardInterrupt:
            print("\n\n" + "=" * 80)
            print("CONSUMER STOPPED")
            print("=" * 80)
            print(f"Total messages received: {self.message_count}")
            print(f"Total entries written to log: {self.entries_written}")
            print(f"Log file: {self.log_file}")
            print("\n✓ Consumer shutdown gracefully")

        except KafkaError as e:
            print(f"\n✗ Kafka error: {e}")
            self._print_troubleshooting()

        except Exception as e:
            print(f"\n✗ Unexpected error: {e}")

        finally:
            if self.consumer:
                self.consumer.close()

    def _print_troubleshooting(self):
        """Print troubleshooting tips"""
        print("\nTroubleshooting:")
        print("1. Check Kafka is running:")
        print("   docker ps | grep kafka")
        print("2. Check topic exists:")
        print("   docker exec ns3-kafka kafka-topics.sh --list --bootstrap-server localhost:9092")
        print("3. Restart Kafka:")
        print("   docker-compose -f docker-compose-kafka.yml restart")


def main():
    """Entry point"""
    parser = argparse.ArgumentParser(description='Bayesian Optimizer - Metrics Consumer (with raw JSON output)')
    parser.add_argument('--broker', type=str, default='localhost:9092',
                      help='Kafka broker address (default: localhost:9092)')
    parser.add_argument('--topic', type=str, default='ns3-metrics',
                      help='Kafka topic to consume from (default: ns3-metrics)')
    parser.add_argument('--group-id', type=str, default='bayesian-optimizer',
                      help='Kafka consumer group ID (default: bayesian-optimizer)')
    parser.add_argument('--simulation-id', type=str, default='sim-001',
                      help='Simulation ID (currently unused, for future filtering)')
    parser.add_argument('--log-file', type=str, default='./output_logs/gat_consumer_logs.json',
                      help='Path to log file (default: ./output_logs/gat_consumer_logs.json)')

    args = parser.parse_args()

    print("\n" + "=" * 80)
    print("BAYESIAN OPTIMIZER - METRICS CONSUMER (RAW JSON OUTPUT)")
    print("=" * 80)
    print("Status: Receiving metrics and writing to log file for Optuna")
    print(f"Simulation ID: {args.simulation_id}")
    if args.log_file:
        print(f"Log File: {args.log_file}")
    else:
        print("Log File: Auto-generated in ./output_logs/")
    print("=" * 80 + "\n")

    # Create and run consumer
    consumer = MetricsConsumer(
        broker=args.broker,
        topic=args.topic,
        group_id=args.group_id,
        log_file=args.log_file
    )

    consumer.run()


if __name__ == '__main__':
    # Check dependencies
    try:
        import kafka
    except ImportError:
        print("✗ Error: kafka-python is not installed\n")
        print("Install it with:")
        print("  pip install kafka-python\n")
        sys.exit(1)

    main()