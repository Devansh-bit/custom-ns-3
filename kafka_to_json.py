#!/usr/bin/env python3
"""
Kafka Multi-Topic JSON Logger
==============================

Consumes messages from multiple Kafka topics and stores each topic's messages
in its own JSON file.

Usage:
    python3 kafka_to_json.py
    python3 kafka_to_json.py --topics ns3-metrics,optimization-commands
    python3 kafka_to_json.py --output-dir ./kafka_logs
    python3 kafka_to_json.py --broker localhost:9092

Requirements:
    pip install kafka-python
"""

import json
import sys
import time
import argparse
import signal
from datetime import datetime
from pathlib import Path
from typing import Dict, Set
from kafka import KafkaConsumer
from kafka.errors import KafkaError


class MultiTopicKafkaLogger:
    """Consumes messages from multiple Kafka topics and logs to separate JSON files."""

    # Default topics to subscribe to
    DEFAULT_TOPICS = [
        'ns3-metrics',
        'optimization-commands',
        'simulator-events',
        'spectrum-data',
        'cnn-predictions',
        'rl-events',
    ]

    def __init__(
        self,
        broker: str = 'localhost:9092',
        topics: list = None,
        output_dir: str = './kafka_json_logs',
        group_id: str = 'json-logger',
        auto_offset_reset: str = 'earliest',
    ):
        self.broker = broker
        self.topics = topics or self.DEFAULT_TOPICS
        self.output_dir = Path(output_dir)
        self.group_id = group_id
        self.auto_offset_reset = auto_offset_reset
        self.consumer = None
        self.running = False

        # Statistics per topic
        self.message_counts: Dict[str, int] = {topic: 0 for topic in self.topics}
        self.file_handles: Dict[str, Path] = {}

        # Create output directory
        self.output_dir.mkdir(parents=True, exist_ok=True)

        # Initialize file paths for each topic
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        for topic in self.topics:
            # Sanitize topic name for filename
            safe_name = topic.replace('-', '_').replace('.', '_')
            self.file_handles[topic] = self.output_dir / f"{safe_name}_{timestamp}.json"

    def connect(self) -> bool:
        """Connect to Kafka and subscribe to topics."""
        print(f"Connecting to Kafka broker: {self.broker}")
        print(f"Topics: {', '.join(self.topics)}")
        print(f"Output directory: {self.output_dir}")
        print("-" * 80)

        try:
            self.consumer = KafkaConsumer(
                *self.topics,
                bootstrap_servers=[self.broker],
                group_id=self.group_id,
                auto_offset_reset=self.auto_offset_reset,
                enable_auto_commit=True,
                value_deserializer=None,  # Manual deserialization for error handling
                key_deserializer=None,
            )

            # Check which topics actually exist
            available_topics = self.consumer.topics()
            subscribed = set(self.topics) & available_topics
            missing = set(self.topics) - available_topics

            if missing:
                print(f"Note: These topics don't exist yet (will consume when created): {missing}")
            if subscribed:
                print(f"Subscribed to existing topics: {subscribed}")

            print("Connected to Kafka successfully")
            return True

        except Exception as e:
            print(f"Failed to connect to Kafka: {e}")
            return False

    def process_message(self, message) -> None:
        """Process a single message and write to appropriate JSON file."""
        topic = message.topic
        self.message_counts[topic] = self.message_counts.get(topic, 0) + 1

        # Decode key if present
        key = message.key.decode('utf-8') if message.key else None

        # Decode and parse value
        try:
            raw_value = message.value.decode('utf-8')
            try:
                data = json.loads(raw_value)
            except json.JSONDecodeError:
                # Store as raw string if not valid JSON
                data = raw_value
        except UnicodeDecodeError:
            # Binary data - store as base64 or hex
            data = {"_binary": message.value.hex()}

        # Create log entry with metadata
        log_entry = {
            'message_number': self.message_counts[topic],
            'topic': topic,
            'partition': message.partition,
            'offset': message.offset,
            'key': key,
            'timestamp': message.timestamp,
            'received_at': time.time(),
            'received_at_iso': datetime.now().isoformat(),
            'data': data
        }

        # Get or create file path for this topic
        if topic not in self.file_handles:
            timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
            safe_name = topic.replace('-', '_').replace('.', '_')
            self.file_handles[topic] = self.output_dir / f"{safe_name}_{timestamp}.json"

        # Append to file (one JSON object per line - JSONL format)
        file_path = self.file_handles[topic]
        with open(file_path, 'a') as f:
            json.dump(log_entry, f, default=str)
            f.write('\n')

        # Print summary
        print(f"[{topic}] #{self.message_counts[topic]} | "
              f"key={key} | offset={message.offset} | "
              f"file={file_path.name}")

    def run(self) -> None:
        """Main consumer loop."""
        if not self.connect():
            return

        self.running = True

        # Setup signal handlers for graceful shutdown
        def signal_handler(sig, frame):
            print("\nShutdown signal received...")
            self.running = False

        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)

        print("\n" + "=" * 80)
        print("LISTENING FOR MESSAGES (Press Ctrl+C to stop)")
        print("=" * 80 + "\n")

        try:
            while self.running:
                # Poll with timeout to allow checking running flag
                records = self.consumer.poll(timeout_ms=1000)

                for topic_partition, messages in records.items():
                    for message in messages:
                        self.process_message(message)

                        if not self.running:
                            break
                    if not self.running:
                        break

        except KafkaError as e:
            print(f"Kafka error: {e}")
            self._print_troubleshooting()

        except Exception as e:
            print(f"Unexpected error: {e}")

        finally:
            self._shutdown()

    def _shutdown(self) -> None:
        """Clean shutdown."""
        print("\n" + "=" * 80)
        print("CONSUMER STOPPED")
        print("=" * 80)

        # Print statistics
        total = sum(self.message_counts.values())
        print(f"\nTotal messages received: {total}")
        print("\nMessages per topic:")
        for topic, count in sorted(self.message_counts.items()):
            if count > 0:
                file_path = self.file_handles.get(topic, "N/A")
                print(f"  {topic}: {count} messages -> {file_path}")

        print(f"\nOutput directory: {self.output_dir}")
        print("\nConsumer shutdown gracefully")

        if self.consumer:
            self.consumer.close()

    def _print_troubleshooting(self) -> None:
        """Print troubleshooting tips."""
        print("\nTroubleshooting:")
        print("1. Check Kafka is running:")
        print("   docker ps | grep kafka")
        print("2. Check topics exist:")
        print("   docker exec ns3-kafka kafka-topics.sh --list --bootstrap-server localhost:9092")
        print("3. Restart Kafka:")
        print("   docker-compose -f docker-compose-kafka.yml restart")


def main():
    """Entry point."""
    parser = argparse.ArgumentParser(
        description='Kafka Multi-Topic JSON Logger - Stores Kafka messages in JSON files'
    )
    parser.add_argument(
        '--broker', type=str, default='localhost:9092',
        help='Kafka broker address (default: localhost:9092)'
    )
    parser.add_argument(
        '--topics', type=str, default=None,
        help='Comma-separated list of topics to consume (default: all known topics)'
    )
    parser.add_argument(
        '--output-dir', type=str, default='./kafka_json_logs',
        help='Output directory for JSON files (default: ./kafka_json_logs)'
    )
    parser.add_argument(
        '--group-id', type=str, default='json-logger',
        help='Kafka consumer group ID (default: json-logger)'
    )
    parser.add_argument(
        '--from-beginning', action='store_true',
        help='Start consuming from the beginning of topics'
    )
    parser.add_argument(
        '--list-default-topics', action='store_true',
        help='List default topics and exit'
    )

    args = parser.parse_args()

    # Handle list-default-topics
    if args.list_default_topics:
        print("Default topics:")
        for topic in MultiTopicKafkaLogger.DEFAULT_TOPICS:
            print(f"  - {topic}")
        return

    # Parse topics
    topics = None
    if args.topics:
        topics = [t.strip() for t in args.topics.split(',')]

    # Determine offset reset
    auto_offset_reset = 'earliest' if args.from_beginning else 'latest'

    print("\n" + "=" * 80)
    print("KAFKA MULTI-TOPIC JSON LOGGER")
    print("=" * 80)
    print(f"Broker: {args.broker}")
    print(f"Topics: {topics or 'all defaults'}")
    print(f"Output: {args.output_dir}")
    print(f"Offset: {auto_offset_reset}")
    print("=" * 80 + "\n")

    # Create and run logger
    logger = MultiTopicKafkaLogger(
        broker=args.broker,
        topics=topics,
        output_dir=args.output_dir,
        group_id=args.group_id,
        auto_offset_reset=auto_offset_reset,
    )

    logger.run()


if __name__ == '__main__':
    # Check dependencies
    try:
        import kafka
    except ImportError:
        print("Error: kafka-python is not installed\n")
        print("Install it with:")
        print("  pip install kafka-python\n")
        sys.exit(1)

    main()