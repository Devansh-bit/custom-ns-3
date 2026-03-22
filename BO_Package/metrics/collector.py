"""Metrics collection from log files

Handles reading and parsing simulation log files.
"""

import json
import logging
import time
from pathlib import Path
from typing import List, Dict, Optional

from .models import LogEntry

logger = logging.getLogger('bayesian_optimizer.metrics.collector')


class MetricsCollector:
    """Collects metrics from simulation log files"""

    def __init__(self, log_dir: str = "output_logs"):
        """Initialize metrics collector

        Args:
            log_dir: Directory containing log files
        """
        self.log_dir = Path(log_dir)
        self.log_dir.mkdir(exist_ok=True)

    def get_current_sim_time(self, max_wait_time: float = 6000.0) -> Optional[float]:
        """Get the current simulation time from the latest log entry

        Args:
            max_wait_time: Maximum real time to wait for a log (seconds)

        Returns:
            Current simulation time in seconds, or None if failed
        """
        try:
            log_files = sorted(self.log_dir.glob("simulation_logs_*.json"))
            if not log_files:
                logger.error("No log files found")
                return None

            latest_file = log_files[-1]
            start_wait = time.time()

            logger.info("Getting current simulation time from latest log...")

            # Poll until we get at least one log
            while True:
                if time.time() - start_wait > max_wait_time:
                    logger.error(f"Timeout: No logs found after {max_wait_time}s")
                    return None

                try:
                    with open(latest_file, 'r') as f:
                        lines = f.readlines()
                        if not lines:
                            time.sleep(0.5)
                            continue

                        # Get the last line (most recent log)
                        last_line = lines[-1].strip()
                        log = json.loads(last_line)

                        # Extract sim_time
                        sim_time = log.get('sim_time', 0)
                        if sim_time == 0:
                            # Try data field
                            data = log.get('data', {})
                            sim_time = data.get('sim_time_seconds', 0)

                        if sim_time > 0:
                            logger.info(f"Current simulation time: {sim_time:.2f}s")
                            return sim_time

                except (FileNotFoundError, json.JSONDecodeError):
                    time.sleep(0.5)
                    continue

                time.sleep(0.5)

        except Exception as e:
            logger.error(f"Error getting simulation time: {e}")
            return None

    def collect_logs_until_sim_time(
        self,
        start_sim_time: float,
        target_sim_time: float,
        max_wait_time: float = 300.0
    ) -> List[Dict]:
        """Collect logs until reaching a target simulation time

        Args:
            start_sim_time: Starting simulation time (e.g., 20.0s)
            target_sim_time: Target simulation time to reach (e.g., 38.0s)
            max_wait_time: Maximum real time to wait (seconds)

        Returns:
            List of log entries in the time window [start_sim_time, target_sim_time]
        """
        logs = []
        start_wait = time.time()

        try:
            log_files = sorted(self.log_dir.glob("simulation_logs_*.json"))
            if not log_files:
                logger.error("No log files found")
                return []

            latest_file = log_files[-1]
            last_progress = 0

            logger.info(
                f"Collecting logs from sim_time {start_sim_time:.2f}s "
                f"to {target_sim_time:.2f}s..."
            )

            while True:
                # Check if we've exceeded max wait time
                if time.time() - start_wait > max_wait_time:
                    logger.warning(
                        f"Max wait time reached, returning {len(logs)} logs collected so far"
                    )
                    break

                # Read all current logs
                collected_logs = []
                try:
                    with open(latest_file, 'r') as f:
                        for line in f:
                            try:
                                log = json.loads(line.strip())
                                collected_logs.append(log)
                            except json.JSONDecodeError:
                                continue
                except FileNotFoundError:
                    time.sleep(0.5)
                    continue

                if not collected_logs:
                    time.sleep(0.5)
                    continue

                # Check last log's simulation time
                last_log = collected_logs[-1]
                last_sim_time = last_log.get('sim_time', 0)
                if last_sim_time == 0:
                    data = last_log.get('data', {})
                    last_sim_time = data.get('sim_time_seconds', 0)

                if last_sim_time >= target_sim_time:
                    # We've reached the target! Filter logs to only include the time window
                    logs = []
                    for log in collected_logs:
                        log_sim_time = log.get('sim_time', 0)
                        if log_sim_time == 0:
                            data = log.get('data', {})
                            log_sim_time = data.get('sim_time_seconds', 0)

                        # Only include logs within [start_sim_time, target_sim_time]
                        if start_sim_time <= log_sim_time <= target_sim_time:
                            logs.append(log)

                    logger.info(f"Collected {len(logs)} log entries")
                    break

                # Update progress (only every 5% to avoid spam)
                if last_sim_time > 0:
                    # Get first log's sim_time for progress calculation
                    first_sim_time = collected_logs[0].get('sim_time', 0)
                    if first_sim_time == 0:
                        data = collected_logs[0].get('data', {})
                        first_sim_time = data.get('sim_time_seconds', 0)

                    if first_sim_time > 0 and first_sim_time < target_sim_time:
                        progress = ((last_sim_time - first_sim_time) /
                                   (target_sim_time - first_sim_time)) * 100
                        progress = min(progress, 100)  # Cap at 100%

                        # Only print if progress increased by at least 5%
                        if progress >= last_progress + 5:
                            logger.debug(
                                f"Progress: {progress:.0f}% "
                                f"(sim_time: {last_sim_time:.2f}s / {target_sim_time:.2f}s)"
                            )
                            last_progress = progress

                time.sleep(0.5)

        except Exception as e:
            logger.error(f"Error collecting logs: {e}", exc_info=True)

        return logs

    def clear_log_file(self) -> None:
        """Clear the log file after collecting logs for the trial"""
        try:
            log_files = sorted(self.log_dir.glob("simulation_logs_*.json"))
            if not log_files:
                logger.warning("No log file found to clear")
                return

            latest_file = log_files[-1]
            # Clear the file by opening in write mode
            with open(latest_file, 'w') as f:
                pass
            logger.debug(f"Cleared log file: {latest_file.name}")

        except Exception as e:
            logger.error(f"Error clearing log file: {e}")

    def save_sample_log(self, logs: List[Dict], filename: str = "sample_json.json") -> None:
        """Save first log as sample for debugging

        Args:
            logs: List of log entries
            filename: Output filename
        """
        if not logs:
            logger.warning("No logs to save as sample")
            return

        try:
            # Find first log with ap_metrics
            for log in logs:
                if log.get('data', {}).get('ap_metrics'):
                    with open(filename, 'w') as f:
                        json.dump(log, f, indent=2)
                    logger.debug(f"Saved sample JSON to {filename}")
                    return

            logger.warning("No logs with ap_metrics found")

        except Exception as e:
            logger.error(f"Failed to save sample log: {e}")
