"""Time conversion and constraint utilities

Handles conversion between simulation time and 12-hour clock format,
and validates optimization time windows.
"""

from datetime import datetime
import logging
from typing import Tuple, Optional, Union

logger = logging.getLogger('bayesian_optimizer.time_utils')


def sim_time_to_clock(
    sim_time_seconds: float,
    clock_start_time: Union[str, int],
    start_time_hour: int = 6
) -> Tuple[int, int, int]:
    """Convert simulation time to 12-hour clock format

    Args:
        sim_time_seconds: Simulation time in seconds
        clock_start_time: Initial clock time (e.g., "06:00:00" or 6)
        start_time_hour: Starting hour offset (default: 6)

    Returns:
        Tuple of (hour, minute, second) in 12-hour format

    Example:
        sim_time_to_clock(600.0, "06:00:00", 6) -> (6, 10, 0)
        # 600 seconds = 10 minutes, so 06:00:00 + 10 min = 06:10:00
    """
    try:
        # Parse clock start time - handle both string and integer inputs
        if isinstance(clock_start_time, int):
            # If integer, treat it as hour (e.g., 6 -> 06:00:00)
            start_hour = clock_start_time
            start_minute = 0
            start_second = 0
        else:
            # If string, parse as HH:MM:SS
            clock_parts = clock_start_time.split(':')
            start_hour = int(clock_parts[0])
            start_minute = int(clock_parts[1])
            start_second = int(clock_parts[2])

        # Convert sim time to total seconds from start
        total_seconds = int(sim_time_seconds)

        # Calculate elapsed time components
        elapsed_hours = total_seconds // 3600
        elapsed_minutes = (total_seconds % 3600) // 60
        elapsed_seconds = total_seconds % 60

        # Add to start time
        current_second = start_second + elapsed_seconds
        carry_minute = current_second // 60
        current_second = current_second % 60

        current_minute = start_minute + elapsed_minutes + carry_minute
        carry_hour = current_minute // 60
        current_minute = current_minute % 60

        current_hour = start_hour + elapsed_hours + carry_hour
        current_hour = current_hour % 24  # Wrap around 24 hours

        return current_hour, current_minute, current_second

    except Exception as e:
        logger.error(f"Failed to convert sim time {sim_time_seconds}s: {e}")
        # Return start time as fallback
        return start_time_hour, 0, 0


def check_time_constraint(
    sim_time_seconds: float,
    clock_start_time: Union[str, int],
    start_time_hour: int = 6,
    end_time_hour: int = 9
) -> bool:
    """Check if current simulation time is within optimization window

    Args:
        sim_time_seconds: Current simulation time in seconds
        clock_start_time: Initial clock time (e.g., "06:00:00" or 6)
        start_time_hour: Optimization window start hour (default: 6)
        end_time_hour: Optimization window end hour (default: 9)

    Returns:
        True if within time constraint, False otherwise

    Example:
        # Sim time 600s = 06:10:00, window is 06:00-09:00
        check_time_constraint(600.0, "06:00:00", 6, 9) -> True

        # Sim time 12000s = 09:20:00, window is 06:00-09:00
        check_time_constraint(12000.0, "06:00:00", 6, 9) -> False
    """
    try:
        current_hour, current_minute, current_second = sim_time_to_clock(
            sim_time_seconds, clock_start_time, start_time_hour
        )

        # Check if current hour is within window
        if current_hour < start_time_hour or current_hour >= end_time_hour:
            logger.warning(
                f"Time constraint violated: {current_hour:02d}:{current_minute:02d}:{current_second:02d} "
                f"outside window [{start_time_hour:02d}:00 - {end_time_hour:02d}:00)"
            )
            return False

        logger.debug(
            f"Time constraint OK: {current_hour:02d}:{current_minute:02d}:{current_second:02d} "
            f"within window [{start_time_hour:02d}:00 - {end_time_hour:02d}:00)"
        )
        return True

    except Exception as e:
        logger.error(f"Failed to check time constraint: {e}")
        return False


def format_clock_time(hour: int, minute: int, second: int) -> str:
    """Format clock time as HH:MM:SS string

    Args:
        hour: Hour (0-23)
        minute: Minute (0-59)
        second: Second (0-59)

    Returns:
        Formatted time string
    """
    return f"{hour:02d}:{minute:02d}:{second:02d}"


def get_elapsed_time_str(start_time: float, end_time: Optional[float] = None) -> str:
    """Get human-readable elapsed time string

    Args:
        start_time: Start timestamp
        end_time: End timestamp (uses current time if None)

    Returns:
        Formatted elapsed time string (e.g., "5m 23s")
    """
    if end_time is None:
        import time
        end_time = time.time()

    elapsed = end_time - start_time
    hours = int(elapsed // 3600)
    minutes = int((elapsed % 3600) // 60)
    seconds = int(elapsed % 60)

    parts = []
    if hours > 0:
        parts.append(f"{hours}h")
    if minutes > 0:
        parts.append(f"{minutes}m")
    if seconds > 0 or not parts:
        parts.append(f"{seconds}s")

    return " ".join(parts)
