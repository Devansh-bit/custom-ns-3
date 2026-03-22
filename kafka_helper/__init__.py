"""
Kafka Helper - Easy integration with ns-3 simulation Kafka infrastructure.

This module provides an asyncio-based event-driven interface for consuming
simulation metrics/events and sending optimization commands.

Example usage:
    import asyncio
    from kafka_helper import KafkaHelper, SimEventType, ApParameters

    helper = KafkaHelper(simulation_id="basic-sim")

    @helper.on_metrics
    async def handle_metrics(metrics):
        for bssid, ap in metrics.ap_metrics.items():
            print(f"AP {bssid}: Ch={ap.channel}, Util={ap.channel_utilization:.1%}")

    @helper.on_event(SimEventType.CLIENT_ROAMED)
    async def handle_roam(event):
        print(f"Roam: STA {event.primary_node_id} -> AP {event.secondary_node_id}")

    async def main():
        async with helper:
            await helper.send_ap_parameters({
                "00:00:00:00:00:01": ApParameters(channel_number=40),
            })
            await helper.run_forever()

    asyncio.run(main())
"""

import asyncio
import json
import logging
from typing import Any, Awaitable, Callable, Dict, List, Optional, Set, TypeVar

# Re-export models
from .models import (
    WifiBand,
    Position,
    ChannelNeighborInfo,
    ChannelScanData,
    ConnectionMetrics,
    ApMetrics,
    MetricsMessage,
    SimEventType,
    SimulationEvent,
    EventBatch,
    ApParameters,
    OptimizationCommand,
)

# Re-export exceptions
from .exceptions import (
    KafkaHelperError,
    ConnectionError,
    SerializationError,
    ConfigurationError,
    ProducerError,
    ConsumerError,
)

# Re-export consumers and producers for advanced usage
from .consumers import (
    BaseKafkaConsumer,
    KafkaMetricsConsumer,
    KafkaEventsConsumer,
)
from .producers import KafkaCommandProducer

logger = logging.getLogger(__name__)

__version__ = "1.0.0"

__all__ = [
    # Main facade
    "KafkaHelper",
    # Models
    "WifiBand",
    "Position",
    "ChannelNeighborInfo",
    "ChannelScanData",
    "ConnectionMetrics",
    "ApMetrics",
    "MetricsMessage",
    "SimEventType",
    "SimulationEvent",
    "EventBatch",
    "ApParameters",
    "OptimizationCommand",
    # Exceptions
    "KafkaHelperError",
    "ConnectionError",
    "SerializationError",
    "ConfigurationError",
    "ProducerError",
    "ConsumerError",
    # Low-level components (for advanced usage)
    "BaseKafkaConsumer",
    "KafkaMetricsConsumer",
    "KafkaEventsConsumer",
    "KafkaCommandProducer",
]


# Type aliases for callbacks
MetricsCallback = Callable[[MetricsMessage], Awaitable[None]]
EventCallback = Callable[[SimulationEvent], Awaitable[None]]


class KafkaHelper:
    """Asyncio event-driven Kafka helper for ns-3 simulation integration.

    Provides decorator-based callback registration for consuming metrics and
    events, plus methods for sending optimization commands.
    """

    def __init__(
        self,
        broker: str = "localhost:9092",
        simulation_id: str = "",
        metrics_topic: str = "ns3-metrics",
        events_topic: str = "simulator-events",
        commands_topic: str = "optimization-commands",
        poll_interval_ms: int = 100,
    ):
        """Initialize KafkaHelper.

        Args:
            broker: Kafka broker address
            simulation_id: Simulation ID for filtering and commands
            metrics_topic: Topic for metrics (default: ns3-metrics)
            events_topic: Topic for events (default: simulator-events)
            commands_topic: Topic for commands (default: optimization-commands)
            poll_interval_ms: Polling interval in milliseconds
        """
        self._broker = broker
        self._simulation_id = simulation_id
        self._metrics_topic = metrics_topic
        self._events_topic = events_topic
        self._commands_topic = commands_topic
        self._poll_interval = poll_interval_ms / 1000.0

        # Callbacks
        self._metrics_callbacks: List[MetricsCallback] = []
        self._event_callbacks: List[tuple[Set[SimEventType], EventCallback]] = []
        self._any_event_callbacks: List[EventCallback] = []

        # Consumers and producer (created on start)
        self._metrics_consumer: Optional[KafkaMetricsConsumer] = None
        self._events_consumer: Optional[KafkaEventsConsumer] = None
        self._command_producer: Optional[KafkaCommandProducer] = None

        # Asyncio tasks
        self._metrics_task: Optional[asyncio.Task] = None
        self._events_task: Optional[asyncio.Task] = None
        self._running = False

    @property
    def simulation_id(self) -> str:
        """Get the simulation ID."""
        return self._simulation_id

    @simulation_id.setter
    def simulation_id(self, value: str) -> None:
        """Set the simulation ID."""
        self._simulation_id = value
        if self._command_producer:
            self._command_producer.simulation_id = value

    def on_metrics(self, func: MetricsCallback) -> MetricsCallback:
        """Decorator to register a metrics callback.

        Example:
            @helper.on_metrics
            async def handle_metrics(metrics: MetricsMessage):
                for bssid, ap in metrics.ap_metrics.items():
                    print(f"AP {bssid}: Util={ap.channel_utilization:.1%}")
        """
        self._metrics_callbacks.append(func)
        return func

    def on_event(
        self, *event_types: SimEventType
    ) -> Callable[[EventCallback], EventCallback]:
        """Decorator to register an event callback with optional type filter.

        Example:
            @helper.on_event(SimEventType.CLIENT_ROAMED)
            async def handle_roam(event: SimulationEvent):
                print(f"STA {event.primary_node_id} roamed")

            @helper.on_event(SimEventType.ASSOCIATION, SimEventType.DEASSOCIATION)
            async def handle_assoc(event: SimulationEvent):
                print(f"{event.event_type.value}: STA {event.primary_node_id}")
        """
        def decorator(func: EventCallback) -> EventCallback:
            type_set = set(event_types) if event_types else set()
            self._event_callbacks.append((type_set, func))
            return func
        return decorator

    def on_any_event(self, func: EventCallback) -> EventCallback:
        """Decorator to register a callback for all events.

        Example:
            @helper.on_any_event
            async def handle_all_events(event: SimulationEvent):
                print(f"Event: {event.event_type.value}")
        """
        self._any_event_callbacks.append(func)
        return func

    async def start(self) -> None:
        """Start consuming messages in background tasks."""
        if self._running:
            return

        self._running = True

        # Create consumers
        if self._metrics_callbacks:
            self._metrics_consumer = KafkaMetricsConsumer(
                broker=self._broker,
                topic=self._metrics_topic,
                simulation_id=self._simulation_id if self._simulation_id else None,
            )
            self._metrics_consumer.connect()
            # Skip existing messages - start from latest
            self._metrics_consumer.seek_to_end()
            self._metrics_task = asyncio.create_task(self._consume_metrics())
            logger.info("Started metrics consumer")

        if self._event_callbacks or self._any_event_callbacks:
            self._events_consumer = KafkaEventsConsumer(
                broker=self._broker,
                topic=self._events_topic,
                simulation_id=self._simulation_id if self._simulation_id else None,
            )
            self._events_consumer.connect()
            # Skip existing messages - start from latest
            self._events_consumer.seek_to_end()
            self._events_task = asyncio.create_task(self._consume_events())
            logger.info("Started events consumer")

        # Create producer
        self._command_producer = KafkaCommandProducer(
            broker=self._broker,
            topic=self._commands_topic,
            simulation_id=self._simulation_id,
        )
        self._command_producer.connect()
        logger.info("KafkaHelper started")

    async def stop(self) -> None:
        """Stop consumers gracefully."""
        self._running = False

        # Cancel tasks
        if self._metrics_task:
            self._metrics_task.cancel()
            try:
                await self._metrics_task
            except asyncio.CancelledError:
                pass
            self._metrics_task = None

        if self._events_task:
            self._events_task.cancel()
            try:
                await self._events_task
            except asyncio.CancelledError:
                pass
            self._events_task = None

        # Close connections
        if self._metrics_consumer:
            self._metrics_consumer.close()
            self._metrics_consumer = None

        if self._events_consumer:
            self._events_consumer.close()
            self._events_consumer = None

        if self._command_producer:
            self._command_producer.close()
            self._command_producer = None

        logger.info("KafkaHelper stopped")

    async def run_forever(self) -> None:
        """Run until cancelled (Ctrl+C)."""
        try:
            while self._running:
                await asyncio.sleep(1)
        except asyncio.CancelledError:
            pass

    async def send_command(self, command: OptimizationCommand) -> bool:
        """Send an optimization command.

        Args:
            command: OptimizationCommand to send

        Returns:
            True if send was successful
        """
        if not self._command_producer:
            raise ProducerError("Producer not connected. Call start() first.")

        # Run in executor to avoid blocking
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(
            None, self._command_producer.send_command, command
        )

    async def send_ap_parameters(
        self,
        ap_parameters: Dict[str, ApParameters],
        command_type: str = "UPDATE_AP_PARAMETERS",
    ) -> bool:
        """Send AP parameters command.

        Args:
            ap_parameters: Dictionary mapping BSSID to ApParameters
            command_type: Type of command (default: UPDATE_AP_PARAMETERS)

        Returns:
            True if send was successful
        """
        command = OptimizationCommand.create(
            simulation_id=self._simulation_id,
            ap_parameters=ap_parameters,
            command_type=command_type,
        )
        return await self.send_command(command)

    async def _consume_metrics(self) -> None:
        """Background task for consuming metrics."""
        while self._running:
            try:
                # Run blocking poll in executor
                loop = asyncio.get_event_loop()
                metrics = await loop.run_in_executor(
                    None,
                    lambda: self._metrics_consumer.poll(timeout_ms=100),
                )

                if metrics:
                    # Invoke all callbacks
                    for callback in self._metrics_callbacks:
                        try:
                            await callback(metrics)
                        except Exception as e:
                            logger.error(f"Error in metrics callback: {e}")

                await asyncio.sleep(self._poll_interval)

            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"Error consuming metrics: {e}")
                await asyncio.sleep(1)

    async def _consume_events(self) -> None:
        """Background task for consuming events."""
        while self._running:
            try:
                # Run blocking poll in executor
                loop = asyncio.get_event_loop()
                batch = await loop.run_in_executor(
                    None,
                    lambda: self._events_consumer.poll(timeout_ms=100),
                )

                if batch and batch.events:
                    for event in batch.events:
                        # Invoke any_event callbacks
                        for callback in self._any_event_callbacks:
                            try:
                                await callback(event)
                            except Exception as e:
                                logger.error(f"Error in any_event callback: {e}")

                        # Invoke filtered callbacks
                        for type_filter, callback in self._event_callbacks:
                            if not type_filter or event.event_type in type_filter:
                                try:
                                    await callback(event)
                                except Exception as e:
                                    logger.error(f"Error in event callback: {e}")

                await asyncio.sleep(self._poll_interval)

            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"Error consuming events: {e}")
                await asyncio.sleep(1)

    async def __aenter__(self) -> "KafkaHelper":
        """Async context manager entry."""
        await self.start()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        """Async context manager exit."""
        await self.stop()
