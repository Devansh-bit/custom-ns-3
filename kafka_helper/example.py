import asyncio
import logging
import sys
from pathlib import Path

# Add parent directory to path if running as script
if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).parent.parent))

from kafka_helper import (
    KafkaHelper,
    SimEventType,
    ApParameters,
    MetricsMessage,
    SimulationEvent,
)

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger(__name__)


# Create helper instance
helper = KafkaHelper(
    broker="localhost:9092",
    simulation_id="basic-sim",
)


@helper.on_metrics
async def handle_metrics(metrics: MetricsMessage):
    """Handle incoming metrics messages."""
    print(metrics)


@helper.on_event(SimEventType.CLIENT_ROAMED)
async def handle_roam(event: SimulationEvent):
    """Handle roaming events."""
    logger.info(
        f"[ROAM] STA {event.primary_node_id} -> AP {event.secondary_node_id} "
        f"at t={event.sim_timestamp_sec:.2f}s"
    )


@helper.on_event(SimEventType.ASSOCIATION)
async def handle_association(event: SimulationEvent):
    """Handle association events."""
    bssid = event.data.get("bssid", "unknown")
    logger.info(
        f"[ASSOC] STA {event.primary_node_id} associated with AP {event.secondary_node_id} "
        f"(BSSID: {bssid}) at t={event.sim_timestamp_sec:.2f}s"
    )


@helper.on_event(SimEventType.DEASSOCIATION)
async def handle_deassociation(event: SimulationEvent):
    """Handle deassociation events."""
    logger.info(
        f"[DEASSOC] STA {event.primary_node_id} deassociated from AP {event.secondary_node_id} "
        f"at t={event.sim_timestamp_sec:.2f}s"
    )


@helper.on_event(SimEventType.BSS_TM_REQUEST_SENT)
async def handle_bss_tm(event: SimulationEvent):
    """Handle BSS TM request events."""
    reason = event.data.get("reason", "unknown")
    target = event.data.get("target_bssid", "unknown")
    logger.info(
        f"[BSS_TM] AP {event.secondary_node_id} -> STA {event.primary_node_id}: "
        f"reason={reason}, target={target}"
    )


@helper.on_event(SimEventType.CHANNEL_SWITCH)
async def handle_channel_switch(event: SimulationEvent):
    """Handle channel switch events."""
    old_ch = event.data.get("old_channel", "?")
    new_ch = event.data.get("new_channel", "?")
    logger.info(
        f"[CH_SWITCH] Node {event.primary_node_id}: Ch {old_ch} -> {new_ch}"
    )


@helper.on_event(SimEventType.LOAD_BALANCE_CHECK)
async def handle_load_balance(event: SimulationEvent):
    """Handle load balance check events."""
    util = event.data.get("utilization", "?")
    offloaded = event.data.get("offloaded_stas", "0")
    logger.info(
        f"[LOAD_BAL] AP {event.primary_node_id}: util={util}, offloaded={offloaded}"
    )


async def main():
    """Main entry point."""
    logger.info("Starting kafka_helper example...")
    logger.info(f"Simulation ID: {helper.simulation_id}")
    logger.info("Press Ctrl+C to stop\n")

    try:
        async with helper:
            # Run for a while to collect metrics and events
            await helper.run_forever()
    except KeyboardInterrupt:
        logger.info("\nStopping...")


if __name__ == "__main__":
    asyncio.run(main())
