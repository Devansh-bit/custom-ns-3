"""
Base Kafka consumer with common functionality.
"""

import json
import logging
from abc import ABC, abstractmethod
from typing import Any, Dict, Generic, Optional, TypeVar

from kafka import KafkaConsumer
from kafka.errors import KafkaError

from ..exceptions import ConnectionError, ConsumerError, SerializationError

logger = logging.getLogger(__name__)

T = TypeVar("T")


class BaseKafkaConsumer(ABC, Generic[T]):
    """Abstract base class for Kafka consumers.

    Provides common connection management and message polling functionality.
    Subclasses must implement the _deserialize method.
    """

    def __init__(
        self,
        broker: str = "localhost:9092",
        topic: str = "",
        group_id: str = "kafka-helper",
        simulation_id: Optional[str] = None,
        auto_offset_reset: str = "latest",
    ):
        """Initialize consumer.

        Args:
            broker: Kafka broker address
            topic: Topic to consume from
            group_id: Consumer group ID
            simulation_id: Optional simulation ID to filter messages
            auto_offset_reset: Where to start consuming ("latest" or "earliest")
        """
        self._broker = broker
        self._topic = topic
        self._group_id = group_id
        self._simulation_id = simulation_id
        self._auto_offset_reset = auto_offset_reset
        self._consumer: Optional[KafkaConsumer] = None
        self._connected = False

    @property
    def connected(self) -> bool:
        """Check if consumer is connected."""
        return self._connected and self._consumer is not None

    @property
    def topic(self) -> str:
        """Get the topic name."""
        return self._topic

    def connect(self) -> None:
        """Connect to Kafka broker."""
        if self._connected:
            return

        try:
            self._consumer = KafkaConsumer(
                self._topic,
                bootstrap_servers=self._broker,
                group_id=self._group_id,
                auto_offset_reset=self._auto_offset_reset,
                enable_auto_commit=True,
                value_deserializer=lambda m: m.decode("utf-8"),
                key_deserializer=lambda k: k.decode("utf-8") if k else None,
            )
            self._connected = True
            logger.info(f"Connected to {self._broker}, topic: {self._topic}")
        except KafkaError as e:
            raise ConnectionError(f"Failed to connect to Kafka: {e}") from e

    def close(self) -> None:
        """Close the consumer connection."""
        if self._consumer:
            self._consumer.close()
            self._consumer = None
            self._connected = False
            logger.info("Consumer connection closed")

    def seek_to_end(self) -> None:
        """Seek to the end of all assigned partitions (skip existing messages)."""
        if not self._consumer:
            raise ConsumerError("Consumer not connected")
        # Force partition assignment by polling
        self._consumer.poll(timeout_ms=0)
        self._consumer.seek_to_end()
        logger.info(f"Seeked to end of topic {self._topic}")

    def poll(self, timeout_ms: int = 1000) -> Optional[T]:
        """Poll for a single message.

        Args:
            timeout_ms: Polling timeout in milliseconds

        Returns:
            Deserialized message or None if no message available
        """
        if not self._connected:
            self.connect()

        try:
            records = self._consumer.poll(timeout_ms=timeout_ms, max_records=1)

            for topic_partition, messages in records.items():
                for message in messages:
                    # Filter by simulation ID if set
                    if self._simulation_id:
                        if message.key != self._simulation_id:
                            continue

                    try:
                        data = json.loads(message.value)
                        return self._deserialize(data, message.key)
                    except json.JSONDecodeError as e:
                        logger.warning(f"Failed to parse JSON: {e}")
                        continue
                    except SerializationError as e:
                        logger.warning(f"Failed to deserialize: {e}")
                        continue

            return None

        except KafkaError as e:
            raise ConsumerError(f"Error polling messages: {e}") from e

    def poll_batch(
        self, timeout_ms: int = 1000, max_records: int = 100
    ) -> list[T]:
        """Poll for multiple messages.

        Args:
            timeout_ms: Polling timeout in milliseconds
            max_records: Maximum number of records to return

        Returns:
            List of deserialized messages
        """
        if not self._connected:
            self.connect()

        results = []

        try:
            records = self._consumer.poll(
                timeout_ms=timeout_ms, max_records=max_records
            )

            for topic_partition, messages in records.items():
                for message in messages:
                    # Filter by simulation ID if set
                    if self._simulation_id:
                        if message.key != self._simulation_id:
                            continue

                    try:
                        data = json.loads(message.value)
                        result = self._deserialize(data, message.key)
                        if result:
                            results.append(result)
                    except (json.JSONDecodeError, SerializationError) as e:
                        logger.warning(f"Failed to process message: {e}")
                        continue

            return results

        except KafkaError as e:
            raise ConsumerError(f"Error polling messages: {e}") from e

    @abstractmethod
    def _deserialize(self, data: Dict[str, Any], key: Optional[str]) -> T:
        """Deserialize message data to typed object.

        Args:
            data: Parsed JSON data
            key: Message key (usually simulation_id)

        Returns:
            Deserialized typed object
        """
        pass

    def __enter__(self) -> "BaseKafkaConsumer[T]":
        """Context manager entry."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Context manager exit."""
        self.close()
