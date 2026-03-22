"""
Common types used across Kafka Helper models.
"""

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Dict


class WifiBand(Enum):
    """WiFi frequency bands."""
    BAND_2_4GHZ = "BAND_2_4GHZ"
    BAND_5GHZ = "BAND_5GHZ"
    BAND_6GHZ = "BAND_6GHZ"
    BAND_UNSPECIFIED = "BAND_UNSPECIFIED"

    @classmethod
    def from_string(cls, value: str) -> "WifiBand":
        """Convert string to WifiBand enum."""
        try:
            return cls(value)
        except ValueError:
            return cls.BAND_UNSPECIFIED


@dataclass
class Position:
    """3D position coordinates."""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

    def to_dict(self) -> Dict[str, float]:
        """Convert to dictionary."""
        return {"x": self.x, "y": self.y, "z": self.z}

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Position":
        """Create from dictionary."""
        return cls(
            x=float(data.get("x", 0.0)),
            y=float(data.get("y", 0.0)),
            z=float(data.get("z", 0.0)),
        )

    @classmethod
    def from_flat(cls, x: float, y: float, z: float) -> "Position":
        """Create from individual coordinates."""
        return cls(x=x, y=y, z=z)
