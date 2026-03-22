from db import PostgresMetricsLogger
import models
from dotenv import load_dotenv
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
import asyncio
import json
import os
import sys
import time
import argparse
import aiokafka
from aiokafka import AIOKafkaConsumer, AIOKafkaProducer
import uvicorn
from ws_manager import ws_manager
from pydantic import BaseModel
from typing import Dict, Any

load_dotenv()

# --- Argument Parsing ---
parser = argparse.ArgumentParser(description="API for ns-3 simulation metrics.")
parser.add_argument(
    "--kafka-topic",
    type=str,
    default="ns3-metrics",
    help="Kafka topic to consume messages from.",
)
parser.add_argument(
    "--dbname",
    type=str,
    default="extension_box",
    help="Name of the PostgreSQL database.",
)
parser.add_argument(
    "--migrate",
    action="store_true",
    help="Run migrations before running starting the API",
)
parser.add_argument(
    "--kafka-bootstrap-servers",
    type=str,
    default="kafka:9092",
    help="Kafka bootstrap servers.",
)
parser.add_argument(
    "--postgres-host",
    type=str,
    default="postgres",
    help="PostgreSQL host.",
)
args = parser.parse_args()


class SimulationCommand(BaseModel):
    command: str
    parameters: Dict[str, Any]
    simulationId: str
    timestamp: int


app = FastAPI()

# --- Kafka Configuration ---
KAFKA_TOPIC = args.kafka_topic  # Change this to your Kafka topic
# Change to your Kafka broker address
KAFKA_BOOTSTRAP_SERVERS = args.kafka_bootstrap_servers
DBHOST = args.postgres_host
DBNAME = args.dbname

db = None
_kafka_producer: AIOKafkaProducer = None

# --- Kafka Consumer Logic ---


async def consume_kafka_messages(consumer: AIOKafkaConsumer):
    """
    Asynchronous task to consume messages from Kafka
    and broadcast them to WebSocket clients.
    """
    try:
        # Continuously listen for messages
        async for msg in consumer:
            message_value = msg.value.decode("utf-8")
            if msg.topic == KAFKA_TOPIC:
                metrics = json.loads(message_value)
                if (
                    metrics.get("ap_metrics") is None
                    and metrics.get("sta_metrics") is None
                ):
                    continue
                db.insert_snapshot_data(metrics)
            elif msg.topic == "cnn-predictions":
                metrics = json.loads(message_value)
            elif msg.topic == "simulator-events":
                event_data = json.loads(message_value)
                # Process simulator events and insert into database
                db.insert_fast_loop_event(event_data)
            elif msg.topic == "rl-events":
                event_data = json.loads(message_value)
                # Process RL events (slow loop optimization updates)
                db.insert_rl_event(event_data)

    except Exception as e:
        print(f"Kafka consumer error: {e}")
        raise
    finally:
        # Clean up
        print("Stopping Kafka consumer...")
        await consumer.stop()


# --- FastAPI Events ---


@app.on_event("startup")
async def startup_event():
    """On application startup, create a background task for the Kafka consumer."""
    print("Application startup...")
    consumer = AIOKafkaConsumer(
        KAFKA_TOPIC,
        "cnn-predictions",
        "simulator-events",
        "rl-events",
        bootstrap_servers=KAFKA_BOOTSTRAP_SERVERS,
    )

    print("Starting Kafka consumer...")
    try:
        await consumer.start()
    except aiokafka.errors.KafkaConnectionError:
        print("error: failed to connect to kafka brokers", file=sys.stderr)
        return

    # Initialize Kafka producer for stress test commands
    global _kafka_producer
    _kafka_producer = AIOKafkaProducer(bootstrap_servers=KAFKA_BOOTSTRAP_SERVERS)
    try:
        await _kafka_producer.start()
        print("Kafka producer started for stress test commands.")
    except Exception as e:
        print(f"Warning: Could not start Kafka producer: {e}")

    if args.migrate:
        print("Ensuring database exists...")
        PostgresMetricsLogger.create_database_if_not_exists(
            host=args.postgres_host, dbname=DBNAME, password=os.environ["DB_PASS"]
        )
        print("Running migrations...")
        models.run_migrations(DBHOST, DBNAME)

    global db
    db = PostgresMetricsLogger(
        host=args.postgres_host, password=os.environ["DB_PASS"], dbname=DBNAME
    )

    # Create the background task
    asyncio.create_task(consume_kafka_messages(consumer))


@app.websocket("/events")
async def websocket_endpoint(websocket: WebSocket):
    try:
        await ws_manager.connect(websocket)
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        ws_manager.disconnect(websocket)
    except Exception as e:
        print(f"WebSocket error: {e}")
        if websocket in ws_manager.active_connections:
            ws_manager.disconnect(websocket)


@app.get("/ping")
def ping():
    return {"status": "available"}


@app.post("/rrm")
def rrm_changes(data: dict):
    """
    Submits RRM changes to be processed and stored.
    """
    db.insert_rrm_update_data(data)
    return {"status": "success", "message": "RRM changes submitted."}


@app.post("/planner/propose")
def planner_propose(data: dict):
    """
        Submits radio optimization proposals from the RRM planner.
    These are recommendations (not yet enforced) based on sensing, client exper
    ience, and AP telemetry, to be stored.
    """
    db.api_planner_propose(data)
    return {"status": "success", "message": "RRM proposals submitted."}


@app.post("/simulation/stress-test")
async def send_stress_test_command(command_data: dict):
    """
    Sends a stress test command to the running simulation via Kafka.

    Expected payload:
    {
        "command": "HIGH_INTERFERENCE" | "FORCE_DFS" | "HIGH_THROUGHPUT",
        "parameters": {}  # Optional command-specific parameters
    }

    Returns:
        Status message indicating command was sent
    """
    if not _kafka_producer:
        return JSONResponse(
            status_code=500,
            content={"status": "error", "message": "Kafka producer not initialized"}
        )

    command = command_data.get("command")
    if not command:
        return JSONResponse(
            status_code=400,
            content={"status": "error", "message": "Command field is required"}
        )

    # Validate command type
    valid_commands = ["HIGH_INTERFERENCE", "FORCE_DFS", "HIGH_THROUGHPUT"]
    if command not in valid_commands:
        return JSONResponse(
            status_code=400,
            content={
                "status": "error",
                "message": f"Invalid command. Must be one of: {', '.join(valid_commands)}"
            }
        )

    try:
        # Create command payload
        simulation_id = DBNAME  # Use database name as simulation ID
        parameters = command_data.get("parameters", {})
        timestamp = int(time.time() * 1000)

        cmd = SimulationCommand(
            command=command,
            parameters=parameters,
            simulationId=simulation_id,
            timestamp=timestamp
        )

        # Send to Kafka
        message_json = cmd.model_dump_json().encode("utf-8")
        await _kafka_producer.send_and_wait("simulator-commands", message_json)

        return {
            "status": "success",
            "message": f"Stress test command '{command}' sent to simulation",
            "simulationId": simulation_id,
            "timestamp": timestamp
        }
    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"status": "error", "message": f"Failed to send command: {str(e)}"}
        )


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)
