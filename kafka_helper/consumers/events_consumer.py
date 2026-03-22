"""
Kafka consumer for simulator-events topic.
"""

from typing import Any, Dict, List, Optional, Set

from ..models.events import EventBatch, SimEventType, SimulationEvent
from .base import BaseKafkaConsumer


class KafkaEventsConsumer(BaseKafkaConsumer[EventBatch]):
    """Consumer for simulator-events topic.

    Consumes simulation event batches from the ns-3 simulation.
    Supports optional filtering by event type.
    """

    def __init__(
        self,
        broker: str = "localhost:9092",
        topic: str = "simulator-events",
        group_id: str = "kafka-helper-events",
        simulation_id: Optional[str] = None,
        auto_offset_reset: str = "latest",
        event_types: Optional[List[SimEventType]] = None,
    ):
        """Initialize events consumer.

        Args:
            broker: Kafka broker address
            topic: Topic to consume (default: simulator-events)
            group_id: Consumer group ID
            simulation_id: Optional simulation ID to filter messages
            auto_offset_reset: Where to start consuming
            event_types: Optional list of event types to filter
        """
        super().__init__(
            broker=broker,
            topic=topic,
            group_id=group_id,
            simulation_id=simulation_id,
            auto_offset_reset=auto_offset_reset,
        )
        self._event_types: Optional[Set[SimEventType]] = (
            set(event_types) if event_types else None
        )

    def set_event_filter(self, *event_types: SimEventType) -> None:
        """Set event type filter.

        Args:
            event_types: Event types to filter by (empty to clear filter)
        """
        if event_types:
            self._event_types = set(event_types)
        else:
            self._event_types = None

    def _deserialize(
        self, data: Dict[str, Any], key: Optional[str]
    ) -> EventBatch:
        """Deserialize event batch.

        Args:
            data: Parsed JSON data
            key: Message key (simulation_id)

        Returns:
            EventBatch object, optionally filtered by event type
        """
        batch = EventBatch.from_dict(data)

        # Apply event type filtering if set
        if self._event_types:
            batch.events = [
                e for e in batch.events if e.event_type in self._event_types
            ]
            batch.event_count = len(batch.events)

        return batch

    def poll_events(
        self, timeout_ms: int = 1000
    ) -> List[SimulationEvent]:
        """Poll for individual events (flattened from batches).

        Args:
            timeout_ms: Polling timeout in milliseconds

        Returns:
            List of individual SimulationEvent objects
        """
        batch = self.poll(timeout_ms=timeout_ms)
        if batch:
            return batch.events
        return []
