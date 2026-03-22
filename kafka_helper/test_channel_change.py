#!/usr/bin/env python3
"""
Test channel change command via kafka_helper.

Sends a channel change command to an AP and monitors metrics to verify.
"""

import asyncio
import logging
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from kafka_helper import KafkaHelper, ApParameters, MetricsMessage

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger(__name__)

# Target AP to change
TARGET_BSSID = "00:00:00:00:00:01"
NEW_CHANNEL = 149  # Change from current channel to 149

helper = KafkaHelper(simulation_id="basic-sim")

# Track channel changes
channel_history = {}


@helper.on_metrics
async def handle_metrics(metrics: MetricsMessage):
    """Monitor AP channels."""
    for bssid, ap in metrics.ap_metrics.items():
        old_ch = channel_history.get(bssid)
        if old_ch != ap.channel:
            if old_ch is not None:
                logger.info(f"*** CHANNEL CHANGED: {bssid} Ch {old_ch} -> {ap.channel} ***")
            channel_history[bssid] = ap.channel

        # Show current state
        logger.info(f"  {bssid}: Ch={ap.channel}, Clients={ap.associated_clients}")


async def main():
    logger.info("=" * 60)
    logger.info("Channel Change Test")
    logger.info("=" * 60)
    logger.info(f"Target AP: {TARGET_BSSID}")
    logger.info(f"New Channel: {NEW_CHANNEL}")
    logger.info("=" * 60)

    async with helper:
        # Wait a moment to get initial metrics
        logger.info("\nWaiting for initial metrics...")
        await asyncio.sleep(3)

        # Send channel change command
        logger.info(f"\n>>> SENDING CHANNEL CHANGE COMMAND: {TARGET_BSSID} -> Ch {NEW_CHANNEL}")

        success = await helper.send_ap_parameters({
            TARGET_BSSID: ApParameters(channel_number=NEW_CHANNEL),
        })

        if success:
            logger.info(">>> Command sent successfully!")
        else:
            logger.error(">>> Failed to send command!")
            return

        # Monitor for changes
        logger.info("\nMonitoring for channel change (30 seconds)...")
        await asyncio.sleep(30)

    logger.info("\nTest complete!")


if __name__ == "__main__":
    asyncio.run(main())
