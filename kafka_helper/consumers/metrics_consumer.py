"""
Kafka consumer for ns3-metrics topic.
"""

from typing import Any, Dict, Optional

from ..models.metrics import MetricsMessage
from .base import BaseKafkaConsumer


class KafkaMetricsConsumer(BaseKafkaConsumer[MetricsMessage]):
    """Consumer for ns3-metrics topic.

    Consumes AP metrics messages from the ns-3 simulation.
    """

    def __init__(
        self,
        broker: str = "localhost:9092",
        topic: str = "ns3-metrics",
        group_id: str = "kafka-helper-metrics",
        simulation_id: Optional[str] = None,
        auto_offset_reset: str = "latest",
    ):
        """Initialize metrics consumer.

        Args:
            broker: Kafka broker address
            topic: Topic to consume (default: ns3-metrics)
            group_id: Consumer group ID
            simulation_id: Optional simulation ID to filter messages
            auto_offset_reset: Where to start consuming
        """
        super().__init__(
            broker=broker,
            topic=topic,
            group_id=group_id,
            simulation_id=simulation_id,
            auto_offset_reset=auto_offset_reset,
        )

    def _deserialize(
        self, data: Dict[str, Any], key: Optional[str]
    ) -> MetricsMessage:
        """Deserialize metrics message.

        Args:
            data: Parsed JSON data
            key: Message key (simulation_id)

        Returns:
            MetricsMessage object
        """
        return MetricsMessage.from_dict(data, simulation_id=key or "")
