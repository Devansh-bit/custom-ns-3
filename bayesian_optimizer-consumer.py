#!/usr/bin/env python3
"""
Bayesian Optimizer - Kafka Metrics Consumer
=============================================

This script consumes WiFi simulation metrics from Kafka.
It will be moved to the Bayesian Optimizer repository later.

For now: Just receives and displays metrics (no optimization logic)

Usage:
    python3 bayesian-optimizer-consumer.py

Requirements:
    pip install kafka-pytho
"""

import json
import sys
from datetime import datetime
from kafka import KafkaConsumer
from kafka.errors import KafkaError


class MetricsConsumer:
    """Consumes metrics from Kafka simulator-metrics topic"""

    def __init__(self, broker='localhost:9092', topic='ns3-metrics', group_id='bayesian-optimizer', log_file=None):
        self.broker = broker
        self.topic = topic
        self.group_id = group_id
        self.consumer = None
        self.message_count = 0
        # Use provided log_file or generate timestamped name
        if log_file:
            self.log_file = log_file
        else:
            self.log_file = f"./output_logs/simulation_logs_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        self.max_log_entries = 200  # Keep only last 200 entries (~100 seconds at 2 logs/sec)
        self.entries_written = 0  # Track entries written to current file

    def _compact_metrics(self, data):
        """
        Strip out large cumulative fields to prevent memory issues

        WARNING: Modifies data IN-PLACE to avoid deepcopy memory overhead!
        Problem: roaming_history can grow to 180KB+ per STA (1400+ events)
        Solution: Keep only roaming count, not full history

        Reduces log size from 360KB to ~2KB per entry (180x reduction!)
        """
        # Compact STA metrics - strip large history fields IN-PLACE
        if 'sta_metrics' in data:
            for mac, sta_data in data['sta_metrics'].items():
                # Keep roaming count but discard history details
                if 'roaming_history' in sta_data:
                    roaming_count = len(sta_data['roaming_history'])
                    sta_data['roaming_history'] = []  # Empty the history IN-PLACE
                    sta_data['roaming_count'] = roaming_count  # Store just the count

                # Keep only essential scan results (not full scan history)
                if 'scan_results' in sta_data and isinstance(sta_data['scan_results'], list):
                    # Keep only last 3 scan results instead of all history
                    sta_data['scan_results'] = sta_data['scan_results'][-3:]

        return data  # Return same reference, now compacted

    def _trim_log_file(self):
        """
        Periodically trim log file to prevent unlimited growth.
        Keeps only the last max_log_entries lines.
        Called every 50 entries to avoid excessive I/O.
        """
        if self.entries_written % 50 != 0:
            return  # Only trim every 50 entries

        try:
            # Read last N lines
            with open(self.log_file, 'r') as f:
                lines = f.readlines()

            # Keep only last max_log_entries
            if len(lines) > self.max_log_entries:
                with open(self.log_file, 'w') as f:
                    f.writelines(lines[-self.max_log_entries:])
                print(f"✓ Trimmed log file: kept last {self.max_log_entries} entries (removed {len(lines) - self.max_log_entries})")
        except Exception as e:
            print(f"⚠ Failed to trim log file: {e}")

    def connect(self):
        """Connect to Kafka"""
        print(f"Connecting to Kafka broker: {self.broker}")
        print(f"Topic: {self.topic}")
        print(f"Consumer Group: {self.group_id}")
        print("-" * 80)

        try:
            self.consumer = KafkaConsumer(
                self.topic,
                bootstrap_servers=[self.broker],
                group_id=self.group_id,
                auto_offset_reset='earliest',  # Read from beginning if no committed offset
                enable_auto_commit=True,
                value_deserializer=lambda m: json.loads(m.decode('utf-8'))
            )
            print("✓ Connected to Kafka successfully")
            print("Waiting for metrics...\n")
            return True

        except Exception as e:
            print(f"✗ Failed to connect to Kafka: {e}")
            return False

    def process_message(self, message):
        """Process a single metrics message"""
        self.message_count += 1

        sim_id = message.key.decode('utf-8') if message.key else 'unknown'
        data = message.value

        # Extract basic info from NS-3 simulation
        sim_timestamp = data.get('timestamp_unix', 0)
        sim_time = data.get('sim_time_seconds', 0.0)

        # CRITICAL: Use wall-clock time for log timestamp, NOT simulation time
        # This ensures Optuna can filter logs from the last N seconds correctly
        import time
        wall_clock_timestamp = time.time()

        # Display header for console
        print("\n" + "=" * 80)
        print(f"MESSAGE #{self.message_count} | Simulation: {sim_id}")
        print(f"NS-3 Time: {datetime.fromtimestamp(sim_timestamp)} | Sim Time: {sim_time:.1f}s")
        print(f"Wall Clock: {datetime.fromtimestamp(wall_clock_timestamp)}")
        print("=" * 80)

        # MEMORY OPTIMIZATION: Strip out large cumulative fields that cause memory issues
        # roaming_history can grow to 180KB+ per STA, causing log files to hit 1GB+
        # We only need current state for optimization, not full history
        # NOTE: This modifies data IN-PLACE to avoid deepcopy overhead
        data_compact = self._compact_metrics(data)

        # Store the log in the file - SIMPLE APPEND (no reading/trimming)
        # Optuna will handle reading only what it needs using deque
        log_entry = {
            'message_number': self.message_count,
            'simulation_id': sim_id,
            'timestamp': wall_clock_timestamp,  # Wall-clock time for filtering
            'sim_timestamp': sim_timestamp,      # NS-3 simulation timestamp (for reference)
            'sim_time': sim_time,
            'data': data_compact  # Use compacted data (without huge history fields)
        }

        # MEMORY SAFE: Simple append, no file reading
        with open(self.log_file, 'a') as f:
            json.dump(log_entry, f)
            f.write('\n')

        # Increment counter
        self.entries_written += 1
        # NOTE: Trimming disabled - Optuna handles log clearing between trials
        # self._trim_log_file()

        # CRITICAL: Do NOT print full data (causes memory accumulation in logs)
        # Just print summary
        num_aps = len(data_compact.get('ap_metrics', {}))
        num_stas = len(data_compact.get('sta_metrics', {}))
        print(f"✓ Logged: {num_aps} APs, {num_stas} STAs (compacted) [Total: {self.entries_written}]")
        print("-" * 80)

        # CRITICAL: Explicitly delete data to free memory immediately
        del data, data_compact, log_entry

        # Return nothing to avoid accumulation
        return None

    def run(self):
        """Main consumer loop"""
        if not self.connect():
            return

        try:
            print("Listening for metrics... (Press Ctrl+C to stop)\n")

            for message in self.consumer:
                # Process each message (returns None to avoid accumulation)
                self.process_message(message)

                # TODO: Later, this is where Bayesian Optimizer logic will go
                # For now, we just receive and display

        except KeyboardInterrupt:
            print("\n\n" + "=" * 80)
            print("CONSUMER STOPPED")
            print("=" * 80)
            print(f"Total messages received: {self.message_count}")
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
    import argparse

    parser = argparse.ArgumentParser(description='Bayesian Optimizer - Metrics Consumer')
    parser.add_argument('--broker', type=str, default='localhost:9092',
                      help='Kafka broker address (default: localhost:9092)')
    parser.add_argument('--topic', type=str, default='ns3-metrics',
                      help='Kafka topic to consume from (default: ns3-metrics)')
    parser.add_argument('--group-id', type=str, default='bayesian-optimizer',
                      help='Kafka consumer group ID (default: bayesian-optimizer)')
    parser.add_argument('--simulation-id', type=str, default='sim-001',
                      help='Simulation ID (currently unused, for future filtering)')
    parser.add_argument('--log-file', type=str, default=None,
                      help='Path to log file (default: auto-generated timestamped name)')

    args = parser.parse_args()

    print("\n" + "=" * 80)
    print("BAYESIAN OPTIMIZER - METRICS CONSUMER")
    print("=" * 80)
    print("Status: Receiving metrics only (optimization logic will be added later)")
    print(f"Simulation ID: {args.simulation_id}")
    if args.log_file:
        print(f"Log File: {args.log_file}")
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