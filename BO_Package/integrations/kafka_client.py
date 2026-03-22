"""Kafka client for configuration deployment

Wraps Kafka producer functionality for sending optimization configurations.
"""

import json
import logging
from typing import List, Optional
from kafka import KafkaProducer
from kafka.errors import KafkaError

from ..config.models import APConfig

logger = logging.getLogger('bayesian_optimizer.kafka')


class KafkaClient:
    """Kafka producer client for sending configurations"""

    def __init__(
        self,
        broker: str = "localhost:9092",
        topic: str = "optimization-commands",
        key: str = "sim-001",
        max_request_size: int = 10485760
    ):
        """Initialize Kafka client

        Args:
            broker: Kafka broker address
            topic: Topic to publish to
            key: Message key
            max_request_size: Maximum request size in bytes
        """
        self.broker = broker
        self.topic = topic
        self.key = key
        self.max_request_size = max_request_size
        self.producer: Optional[KafkaProducer] = None

    def connect(self) -> bool:
        """Connect to Kafka broker

        Returns:
            True if connected successfully, False otherwise
        """
        try:
            logger.info(f"Connecting to Kafka broker: {self.broker}")

            self.producer = KafkaProducer(
                bootstrap_servers=[self.broker],
                value_serializer=lambda v: json.dumps(v).encode('utf-8'),
                max_request_size=self.max_request_size
            )

            logger.info("Kafka producer connected successfully")
            logger.info(f"  Topic: {self.topic}")
            logger.info(f"  Key: {self.key}")
            return True

        except KafkaError as e:
            logger.error(f"Kafka connection failed: {e}")
            return False

        except Exception as e:
            logger.error(f"Unexpected error connecting to Kafka: {e}")
            return False

    def send_configuration(self, ap_configs: List[APConfig], trial_number: int = 0) -> bool:
        """Send AP configuration to Kafka

        Args:
            ap_configs: List of AP configurations
            trial_number: Current trial number (for logging)

        Returns:
            True if sent successfully, False otherwise
        """
        if self.producer is None:
            logger.error("Kafka producer not connected. Call connect() first.")
            return False

        try:
            # Build ApParameters message
            message = {
                "type": "ApParameters",
                "aps": [ap.to_dict() for ap in ap_configs]
            }

            # Send to Kafka
            future = self.producer.send(
                self.topic,
                key=self.key.encode('utf-8'),
                value=message
            )

            # Wait for confirmation
            record_metadata = future.get(timeout=10)

            logger.info(f"Configuration sent for Trial {trial_number}")
            logger.debug(f"  Topic: {record_metadata.topic}")
            logger.debug(f"  Partition: {record_metadata.partition}")
            logger.debug(f"  Offset: {record_metadata.offset}")

            # Log AP configurations
            for i, ap in enumerate(ap_configs):
                logger.debug(
                    f"  AP {i} ({ap.bssid}): Channel={ap.channel}, "
                    f"TX={ap.tx_power} dBm, OBSS-PD={ap.obss_pd} dBm"
                )

            return True

        except KafkaError as e:
            logger.error(f"Failed to send configuration: {e}")
            return False

        except Exception as e:
            logger.error(f"Unexpected error sending configuration: {e}")
            return False

    def flush(self) -> None:
        """Flush pending messages"""
        if self.producer:
            try:
                self.producer.flush()
                logger.debug("Kafka producer flushed")
            except Exception as e:
                logger.error(f"Failed to flush Kafka producer: {e}")

    def close(self) -> None:
        """Close Kafka connection"""
        if self.producer:
            try:
                self.producer.close()
                logger.info("Kafka producer closed")
            except Exception as e:
                logger.error(f"Failed to close Kafka producer: {e}")

    def __enter__(self):
        """Context manager entry"""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        self.close()
