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
    pip install kafka-python
"""

import json
import sys
from datetime import datetime
from kafka import KafkaConsumer
from kafka.errors import KafkaError


class MetricsConsumer:
    """Consumes metrics from Kafka simulator-metrics topic"""

    def __init__(self, broker='10.145.54.131:9092', topic='simulator-metrics', group_id='bayesian-optimizer'):
        self.broker = broker
        self.topic = topic
        self.group_id = group_id
        self.consumer = None
        self.message_count = 0

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

        # Extract basic info
        timestamp = data.get('timestamp_unix', 0)
        sim_time = data.get('sim_time_seconds', 0.0)

        # Display header
        print("\n" + "=" * 80)
        print(f"MESSAGE #{self.message_count} | Simulation: {sim_id}")
        print(f"Time: {datetime.fromtimestamp(timestamp)} | Sim Time: {sim_time:.1f}s")
        print("=" * 80)

        # Print the entire JSON
        print(json.dumps(data, indent=2))

        print("-" * 80)

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
    print("\n" + "=" * 80)
    print("BAYESIAN OPTIMIZER - METRICS CONSUMER")
    print("=" * 80)
    print("Status: Receiving metrics only (optimization logic will be added later)")
    print("=" * 80 + "\n")

    # Create and run consumer
    consumer = MetricsConsumer(
        broker='localhost:9092',  # Match simulation broker
        topic='ns3-metrics',           # Match simulation topic
        group_id='bayesian-optimizer'
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
