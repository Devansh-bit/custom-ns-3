"""
Kafka producer for optimization-commands topic.
"""

import json
import logging
from typing import Dict, Optional

from kafka import KafkaProducer
from kafka.errors import KafkaError

from ..exceptions import ConnectionError, ProducerError
from ..models.commands import ApParameters, OptimizationCommand

logger = logging.getLogger(__name__)


class KafkaCommandProducer:
    """Producer for optimization-commands topic.

    Sends optimization commands to the ns-3 simulation.
    """

    def __init__(
        self,
        broker: str = "localhost:9092",
        topic: str = "optimization-commands",
        simulation_id: str = "",
    ):
        """Initialize command producer.

        Args:
            broker: Kafka broker address
            topic: Topic to produce to (default: optimization-commands)
            simulation_id: Simulation ID for commands
        """
        self._broker = broker
        self._topic = topic
        self._simulation_id = simulation_id
        self._producer: Optional[KafkaProducer] = None
        self._connected = False

    @property
    def connected(self) -> bool:
        """Check if producer is connected."""
        return self._connected and self._producer is not None

    @property
    def simulation_id(self) -> str:
        """Get the simulation ID."""
        return self._simulation_id

    @simulation_id.setter
    def simulation_id(self, value: str) -> None:
        """Set the simulation ID."""
        self._simulation_id = value

    def connect(self) -> None:
        """Connect to Kafka broker."""
        if self._connected:
            return

        try:
            self._producer = KafkaProducer(
                bootstrap_servers=self._broker,
                value_serializer=lambda v: json.dumps(v).encode("utf-8"),
                key_serializer=lambda k: k.encode("utf-8") if k else None,
            )
            self._connected = True
            logger.info(f"Producer connected to {self._broker}")
        except KafkaError as e:
            raise ConnectionError(f"Failed to connect to Kafka: {e}") from e

    def close(self) -> None:
        """Close the producer connection."""
        if self._producer:
            self._producer.flush()
            self._producer.close()
            self._producer = None
            self._connected = False
            logger.info("Producer connection closed")

    def send_command(self, command: OptimizationCommand) -> bool:
        """Send an optimization command.

        Args:
            command: OptimizationCommand to send

        Returns:
            True if send was successful

        Raises:
            ProducerError: If sending fails
        """
        if not self._connected:
            self.connect()

        try:
            future = self._producer.send(
                self._topic,
                key=command.simulation_id,
                value=command.to_dict(),
            )
            # Wait for send to complete
            future.get(timeout=10)
            logger.debug(f"Sent command to {self._topic}")
            return True
        except KafkaError as e:
            raise ProducerError(f"Failed to send command: {e}") from e

    def send_ap_parameters(
        self,
        ap_parameters: Dict[str, ApParameters],
        command_type: str = "UPDATE_AP_PARAMETERS",
    ) -> bool:
        """Convenience method to send AP parameters.

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
        return self.send_command(command)

    def flush(self) -> None:
        """Flush any pending messages."""
        if self._producer:
            self._producer.flush()

    def __enter__(self) -> "KafkaCommandProducer":
        """Context manager entry."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Context manager exit."""
        self.close()
