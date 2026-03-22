#!/usr/bin/env python3
"""
Real-time WiFi Network Visualization Server

FastAPI server that consumes ns-3 simulation metrics from Kafka and
broadcasts them to connected WebSocket clients for visualization.
"""

import asyncio
import json
import logging
import sys
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Set

from fastapi import FastAPI, WebSocket, WebSocketDisconnect

# Add project root to path to import kafka_helper
PROJECT_ROOT = Path(__file__).parent.parent.parent
sys.path.insert(0, str(PROJECT_ROOT))

from kafka_helper import MetricsMessage
from kafka_helper.consumers import KafkaMetricsConsumer

# Configuration
KAFKA_BROKER = "localhost:9092"
METRICS_TOPIC = "ns3-metrics"
SERVER_PORT = 3002
CONSUMER_GROUP = "visualization-server"  # Unique group to not interfere with RL consumer

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


# WebSocket connection manager
class ConnectionManager:
    def __init__(self):
        self.active_connections: Set[WebSocket] = set()

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.add(websocket)
        logger.info(f"Client connected. Total connections: {len(self.active_connections)}")

    def disconnect(self, websocket: WebSocket):
        self.active_connections.discard(websocket)
        logger.info(f"Client disconnected. Total connections: {len(self.active_connections)}")

    async def broadcast(self, message: str):
        disconnected = set()
        for connection in self.active_connections:
            try:
                await connection.send_text(message)
            except Exception:
                disconnected.add(connection)

        for conn in disconnected:
            self.active_connections.discard(conn)


manager = ConnectionManager()

# Kafka task reference
kafka_task: asyncio.Task = None


def transform_metrics(metrics: MetricsMessage) -> dict:
    """Transform Kafka metrics to visualization-friendly format."""
    aps = []
    stas = []

    for bssid, ap in metrics.ap_metrics.items():
        ap_data = {
            "id": ap.node_id,
            "bssid": bssid,
            "x": ap.position.x,
            "y": ap.position.y,
            "z": ap.position.z,
            "channel": ap.channel,
            "band": ap.band.value if hasattr(ap.band, 'value') else str(ap.band),
            "utilization": ap.channel_utilization,
            "tx_power": ap.tx_power_dbm,
            "client_count": ap.associated_clients,
            "throughput": ap.throughput_mbps,
        }
        aps.append(ap_data)

        # Extract STA information from connection metrics
        for sta_addr, conn in ap.connection_metrics.items():
            sta_data = {
                "id": conn.node_id,
                "address": sta_addr,
                "x": conn.position.x,
                "y": conn.position.y,
                "z": conn.position.z,
                "connected_ap": bssid,
                "rssi": conn.sta_view_rssi,
                "snr": conn.sta_view_snr,
                "throughput_up": conn.uplink_throughput_mbps,
                "throughput_down": conn.downlink_throughput_mbps,
                "latency": conn.mean_rtt_latency,
                "jitter": conn.jitter_ms,
                "packet_loss": conn.packet_loss_rate,
            }
            stas.append(sta_data)

    return {
        "sim_time": metrics.sim_time_seconds,
        "aps": aps,
        "stas": stas,
    }


async def start_kafka_consumer():
    """Start consuming metrics from Kafka and broadcast to WebSocket clients."""
    logger.info(f"Starting Kafka consumer for topic '{METRICS_TOPIC}' on {KAFKA_BROKER}")
    logger.info(f"Using consumer group '{CONSUMER_GROUP}' (independent from RL consumer)")

    consumer = None

    while True:
        try:
            consumer = KafkaMetricsConsumer(
                broker=KAFKA_BROKER,
                topic=METRICS_TOPIC,
                group_id=CONSUMER_GROUP,  # Unique group - both consumers get all messages
            )
            consumer.connect()
            logger.info("Kafka consumer connected successfully")

            # Poll for messages
            while True:
                # Run blocking poll in executor to not block event loop
                loop = asyncio.get_event_loop()
                metrics = await loop.run_in_executor(
                    None,
                    lambda: consumer.poll(timeout_ms=500),
                )

                if metrics and manager.active_connections:
                    viz_data = transform_metrics(metrics)
                    await manager.broadcast(json.dumps(viz_data))
                    logger.debug(f"Broadcast metrics: {len(viz_data['aps'])} APs, {len(viz_data['stas'])} STAs")

                # Small delay to prevent busy loop
                await asyncio.sleep(0.1)

        except asyncio.CancelledError:
            logger.info("Kafka consumer stopped")
            break
        except Exception as e:
            logger.warning(f"Kafka consumer error: {e}. Retrying in 5 seconds...")
            await asyncio.sleep(5)
        finally:
            if consumer:
                try:
                    consumer.close()
                except Exception:
                    pass
                consumer = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Lifespan context manager for startup/shutdown events."""
    global kafka_task
    # Startup
    kafka_task = asyncio.create_task(start_kafka_consumer())
    logger.info("Server started")

    yield

    # Shutdown
    if kafka_task:
        kafka_task.cancel()
        try:
            await kafka_task
        except asyncio.CancelledError:
            pass
    logger.info("Server stopped")


# FastAPI app with lifespan
app = FastAPI(title="WiFi Network Visualization", lifespan=lifespan)


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """WebSocket endpoint for real-time visualization updates."""
    await manager.connect(websocket)
    try:
        while True:
            # Keep connection alive, handle incoming messages if needed
            data = await websocket.receive_text()
            # Could handle client messages here (e.g., filter settings)
    except WebSocketDisconnect:
        manager.disconnect(websocket)


@app.get("/health")
async def health():
    """Health check endpoint."""
    return {"status": "ok"}


if __name__ == "__main__":
    import uvicorn

    # Try to find an available port
    import socket

    def is_port_available(port):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            try:
                s.bind(('0.0.0.0', port))
                return True
            except OSError:
                return False

    port = SERVER_PORT
    if not is_port_available(port):
        logger.warning(f"Port {port} is in use, trying alternative ports...")
        # Note: 3001 is reserved for Next.js frontend
        for alt_port in [3002, 8000, 8080]:
            if is_port_available(alt_port):
                port = alt_port
                break
        else:
            logger.error("No available ports found. Please free up a port.")
            sys.exit(1)

    logger.info(f"Starting server on http://localhost:{port}")
    uvicorn.run(app, host="0.0.0.0", port=port)
