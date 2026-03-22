"""
Event models for simulator-events Kafka topic.

These models match the JSON format produced by simulation-event-producer.cc.
"""

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Dict, List, Optional


class SimEventType(Enum):
    """Simulation event types matching ns-3 SimEventType enum."""
    BSS_TM_REQUEST_SENT = "bss_tm_request_sent"
    CLIENT_ROAMED = "client_roamed"
    ASSOCIATION = "association"
    DEASSOCIATION = "deassociation"
    CONFIG_RECEIVED = "config_received"
    CHANNEL_SWITCH = "channel_switch"
    POWER_SWITCH = "power_switch"
    LOAD_BALANCE_CHECK = "load_balance_check"
    UNKNOWN = "unknown"

    @classmethod
    def from_string(cls, value: str) -> "SimEventType":
        """Convert string to SimEventType enum."""
        for member in cls:
            if member.value == value:
                return member
        return cls.UNKNOWN


@dataclass
class SimulationEvent:
    """Single simulation event."""
    event_id: str = ""
    event_type: SimEventType = SimEventType.UNKNOWN
    sim_timestamp_sec: float = 0.0
    primary_node_id: int = 0
    secondary_node_id: int = 0
    tertiary_node_id: int = 0
    data: Dict[str, str] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        result = {
            "event_id": self.event_id,
            "event_type": self.event_type.value,
            "sim_timestamp_sec": self.sim_timestamp_sec,
            "primary_node_id": self.primary_node_id,
            "secondary_node_id": self.secondary_node_id,
        }
        if self.tertiary_node_id > 0:
            result["tertiary_node_id"] = self.tertiary_node_id
        if self.data:
            result["data"] = self.data
        return result

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "SimulationEvent":
        """Create from dictionary."""
        return cls(
            event_id=str(data.get("event_id", "")),
            event_type=SimEventType.from_string(data.get("event_type", "unknown")),
            sim_timestamp_sec=float(data.get("sim_timestamp_sec", 0.0)),
            primary_node_id=int(data.get("primary_node_id", 0)),
            secondary_node_id=int(data.get("secondary_node_id", 0)),
            tertiary_node_id=int(data.get("tertiary_node_id", 0)),
            data=dict(data.get("data", {})),
        )


@dataclass
class EventBatch:
    """Batch of simulation events from Kafka."""
    simulation_id: str = ""
    batch_timestamp_unix: int = 0
    batch_sim_time_sec: float = 0.0
    event_count: int = 0
    events: List[SimulationEvent] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            "simulation_id": self.simulation_id,
            "batch_timestamp_unix": self.batch_timestamp_unix,
            "batch_sim_time_sec": self.batch_sim_time_sec,
            "event_count": self.event_count,
            "events": [e.to_dict() for e in self.events],
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "EventBatch":
        """Create from dictionary."""
        events = [
            SimulationEvent.from_dict(e)
            for e in data.get("events", [])
        ]
        return cls(
            simulation_id=str(data.get("simulation_id", "")),
            batch_timestamp_unix=int(data.get("batch_timestamp_unix", 0)),
            batch_sim_time_sec=float(data.get("batch_sim_time_sec", 0.0)),
            event_count=int(data.get("event_count", 0)),
            events=events,
        )

    def filter_by_type(self, *event_types: SimEventType) -> List[SimulationEvent]:
        """Filter events by type(s).

        Args:
            event_types: One or more event types to filter by

        Returns:
            List of events matching any of the specified types
        """
        if not event_types:
            return self.events
        type_set = set(event_types)
        return [e for e in self.events if e.event_type in type_set]
