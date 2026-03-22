import os
import subprocess
import signal
from typing import Union
import time
import argparse
import asyncio
import json
import websockets
from config import (
    write_simulation_config,
    get_simulation_config,
    get_user_config_list,
    get_config_by_name,
    rename_config,
    delete_config,
)
from db import PostgresMetricsLogger
from dotenv import load_dotenv
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
import uvicorn
import sim_control
from ws_manager import ws_manager, visualizer_ws_manager
from aiokafka import AIOKafkaConsumer, AIOKafkaProducer
from pydantic import BaseModel
from typing import Dict, Any

load_dotenv()


class SimulationCommand(BaseModel):
    command: str
    parameters: Dict[str, Any]
    simulationId: str
    timestamp: int


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
args = parser.parse_args()


realtime_db = None
simulation_db = None
dashboard_db = None
simulation_build_time = None
simulation_start_time = None
simulation_name = None
_metrics_broadcast_task: asyncio.Task = None
_status_broadcast_task: asyncio.Task = None
_simulation_client_ws: asyncio.Task = None
_kafka_consumer_task: asyncio.Task = None
_kafka_events_consumer_task: asyncio.Task = None
_kafka_producer: AIOKafkaProducer = None
_rl_process: subprocess.Popen = None
_rl_process_simulation_id: str = None

# Replay state management
replay_state = {
    "current_time": 10.0,      # Current simulation time in seconds
    "is_playing": False,       # Whether replay is auto-advancing
    "speed": 1.0,              # Playback speed multiplier
    "min_time": 10.0,          # Minimum time in database
    "max_time": 1800.0,        # Maximum time in database (will be updated on startup)
    "last_update": None,       # Last time the replay time was updated
}
_replay_advance_task: asyncio.Task = None


async def broadcast_simulation_status():
    """Periodically fetches simulation status (uptime, metrics) and broadcasts them."""
    try:
        while True:
            if simulation_db:
                status_data = {
                    "mtype": "sim_status",
                    "uptime": 0,
                    "metrics": {
                        "throughput": None,
                        "latency": None,
                        "jitter": None,
                        "loss_rate": None,
                    },
                }

                # Uptime
                try:
                    with simulation_db.conn.cursor() as cur:
                        latest = simulation_db.latest_snapshot(cur)
                        if latest:
                            status_data["uptime"] = latest["sim_time_seconds"]
                except Exception as e:
                    print(f"Error fetching uptime: {e}")

                # Metrics
                try:
                    status_data["metrics"] = simulation_db.get_network_metrics()
                except Exception as e:
                    print(f"Error fetching network metrics: {e}")

                await ws_manager.broadcast([status_data])

            await asyncio.sleep(2)
    except asyncio.CancelledError:
        print("Simulation status broadcasting task cancelled.")
    except Exception as e:
        print(
            f"Simulation status broadcasting task encountered an unexpected error: {e}"
        )


async def broadcast_aggregated_metrics():
    """Periodically fetches aggregated metrics and broadcasts them."""
    try:
        while True:
            if simulation_db and simulation_db.conn:  # Check if simulation_db and connection are active
                try:
                    log_entries = []
                    aggregated_metrics = (
                        simulation_db.get_aggregated_metrics_by_interval(0.5)
                    )
                    for idx, metrics in enumerate(aggregated_metrics):
                        fields = [
                            "avg_latency_ms",
                            "avg_jitter_ms",
                            "avg_packet_loss_rate",
                            "avg_rssi",
                            "avg_snr",
                        ]
                        skip_record = False
                        for field in fields:
                            if metrics[field] is None:
                                skip_record = True

                        if skip_record:
                            continue

                        log_entries.append(
                            {
                                "mtype": "log",
                                "id": f"metrics-{metrics['bssid']}-{idx}",
                                "timestamp": metrics["timestamp"] * 1000,
                                "message": f"AP {metrics['bssid']}: Avg Latency: {metrics['avg_latency_ms']:.2f}ms, Jitter: {metrics['avg_jitter_ms']:.2f}ms, Packet Loss: {metrics['avg_packet_loss_rate']:.2f}%, RSSI: {metrics['avg_rssi']:.2f}dBm, SNR: {metrics['avg_snr']:.2f}dB",
                                "type": "info",
                                "source": "Metrics",
                            }
                        )

                    # Sort by timestamp
                    log_entries.sort(key=lambda x: x["timestamp"], reverse=True)

                    if log_entries:
                        await ws_manager.broadcast(log_entries)
                except Exception as e:
                    print(f"Error in broadcast_aggregated_metrics: {e}")
            await asyncio.sleep(30)  # Always sleep to prevent rapid looping
    except asyncio.CancelledError:
        print("Metrics broadcasting task cancelled.")
    except Exception as e:
        print(f"Metrics broadcasting task encountered an unexpected error: {e}")


async def consume_simulation_kafka(topic: str):
    """
    Consumes messages from the simulation Kafka topic, stores to database, and broadcasts position data.
    """
    consumer = AIOKafkaConsumer(
        topic,
        bootstrap_servers=KAFKA_BOOTSTRAP_SERVERS,
        value_deserializer=lambda m: json.loads(m.decode("utf-8")),
    )

    try:
        await consumer.start()
        print(f"Started Kafka consumer for topic: {topic}")

        async for msg in consumer:
            try:
                data = msg.value

                # Store metrics to database for dashboard charts
                if "ap_metrics" in data and simulation_db is not None:
                    try:
                        # Ensure timestamp_unix exists for database insert
                        if "timestamp_unix" not in data:
                            data["timestamp_unix"] = int(time.time())
                        simulation_db.insert_snapshot_data(data)
                    except Exception as db_err:
                        print(f"Error inserting snapshot data to DB: {db_err}")
                        # Rollback failed transaction to allow future queries
                        try:
                            simulation_db.conn.rollback()
                        except:
                            pass

                # Extract position data from AP metrics
                if "ap_metrics" in data:
                    position_data = []

                    for bssid, ap_data in data["ap_metrics"].items():
                        ap_node_id = ap_data.get("node_id")
                        connection_metrics = ap_data.get("connection_metrics", {})

                        for conn_id, conn_data in connection_metrics.items():
                            if isinstance(conn_data, dict):
                                position_x = conn_data.get("position_x")
                                position_y = conn_data.get("position_y")

                                if position_x is not None and position_y is not None:
                                    position_data.append(
                                        {
                                            "mtype": "pos_change",
                                            "node_id": conn_data.get("node_id"),
                                            "position_x": position_x,
                                            "position_y": position_y,
                                            "ap_id": ap_node_id,
                                        }
                                    )

                    # Broadcast position data through WebSocket
                    if position_data:
                        await ws_manager.broadcast(position_data)

                    # Get interferers from simulation config for visualization
                    interferers_config = []
                    try:
                        sim_config = get_simulation_config()
                        interferers_config = sim_config.get("virtualInterferers", {}).get("interferers", [])
                    except Exception:
                        pass  # Use empty list if config unavailable

                    viz_data = transform_metrics(data, interferers_config)
                    await visualizer_ws_manager.broadcast(json.dumps(viz_data))

            except Exception as e:
                print(f"Error processing Kafka message: {e}")
                import traceback

                traceback.print_exc()

    except asyncio.CancelledError:
        print(f"Kafka consumer for topic {topic} cancelled.")
    except Exception as e:
        print(f"Kafka consumer error: {e}")
        import traceback

        traceback.print_exc()
    finally:
        await consumer.stop()
        print(f"Stopped Kafka consumer for topic: {topic}")


async def consume_simulation_events_kafka():
    """
    Consumes messages from simulator-events and rl-events Kafka topics,
    stores RRM updates to simulation database.
    """
    consumer = AIOKafkaConsumer(
        "simulator-events",
        "rl-events",
        bootstrap_servers=KAFKA_BOOTSTRAP_SERVERS,
        value_deserializer=lambda m: json.loads(m.decode("utf-8")),
    )

    try:
        await consumer.start()
        print("Started Kafka consumer for simulator-events and rl-events topics")

        async for msg in consumer:
            try:
                data = msg.value

                if simulation_db is None:
                    continue

                if msg.topic == "simulator-events":
                    # Process fast loop (RRM) events
                    try:
                        simulation_db.insert_fast_loop_event(data)
                    except Exception as db_err:
                        print(f"Error inserting fast loop event to DB: {db_err}")
                        try:
                            simulation_db.conn.rollback()
                        except:
                            pass

                elif msg.topic == "rl-events":
                    # Process RL/slow loop events
                    try:
                        simulation_db.insert_rl_event(data)
                    except Exception as db_err:
                        print(f"Error inserting RL event to DB: {db_err}")
                        try:
                            simulation_db.conn.rollback()
                        except:
                            pass

            except Exception as e:
                print(f"Error processing events Kafka message: {e}")
                import traceback
                traceback.print_exc()

    except asyncio.CancelledError:
        print("Kafka events consumer cancelled.")
    except Exception as e:
        print(f"Kafka events consumer error: {e}")
        import traceback
        traceback.print_exc()
    finally:
        await consumer.stop()
        print("Stopped Kafka events consumer")


async def simulation_ws_client(port: int):
    uri = f"ws://localhost:{port}/events"
    while True:
        try:
            # Try connecting without any origin header first
            async with websockets.connect(uri) as websocket:
                while True:
                    message = await websocket.recv()
                    log_data = json.loads(message)
                    await ws_manager.broadcast(log_data)
        except (
            websockets.exceptions.ConnectionClosedError,
            ConnectionRefusedError,
        ) as e:
            print(
                f"Simulation log server connection error: {
                    e
                }. Reconnecting in 5 seconds..."
            )
            await asyncio.sleep(5)
        except websockets.exceptions.InvalidStatusCode as e:
            print(f"WebSocket handshake failed with status {e.status_code}: {e}")
            print(
                f"This usually indicates origin/CORS issues. Retrying in 5 seconds..."
            )
            await asyncio.sleep(5)
        except Exception as e:
            print(f"An unexpected error occurred in simulation_ws_client: {e}")
            print(f"Error type: {type(e).__name__}")
            import traceback

            traceback.print_exc()
            await asyncio.sleep(5)


def setup_replay_database():
    """Auto-setup replay database: create DB, schema, and import data if needed."""
    import psycopg
    import subprocess

    replay_dbname = os.environ.get("DB_NAME", "ns3_e2e_test")
    db_pass = os.environ.get("DB_PASS", "")

    # Step 1: Create database if it doesn't exist
    try:
        conn = psycopg.connect(
            host="localhost", port=5432, user="postgres",
            password=db_pass, dbname="postgres"
        )
        conn.autocommit = True
        with conn.cursor() as cur:
            cur.execute(f"SELECT 1 FROM pg_database WHERE datname = %s", (replay_dbname,))
            if not cur.fetchone():
                print(f"Creating database: {replay_dbname}")
                cur.execute(f'CREATE DATABASE "{replay_dbname}"')
        conn.close()
    except Exception as e:
        print(f"Error checking/creating database: {e}")
        return False

    # Step 2: Create schema if tables don't exist
    try:
        conn = psycopg.connect(
            host="localhost", port=5432, user="postgres",
            password=db_pass, dbname=replay_dbname
        )
        with conn.cursor() as cur:
            cur.execute("SELECT EXISTS (SELECT FROM information_schema.tables WHERE table_name = 'snapshots')")
            tables_exist = cur.fetchone()[0]

        if not tables_exist:
            print("Creating database schema...")
            init_sql_path = os.path.join(os.path.dirname(__file__), "init-db.sql")
            if os.path.exists(init_sql_path):
                with open(init_sql_path, 'r') as f:
                    # Skip the CREATE DATABASE and \c lines
                    sql = f.read()
                    # Remove CREATE DATABASE line
                    lines = sql.split('\n')
                    filtered_lines = [l for l in lines if not l.strip().startswith('CREATE DATABASE') and not l.strip().startswith('\\c')]
                    sql = '\n'.join(filtered_lines)
                with conn.cursor() as cur:
                    cur.execute(sql)
                conn.commit()
                print("Schema created.")
        conn.close()
    except Exception as e:
        print(f"Error creating schema: {e}")
        return False

    # Step 3: Import data if snapshots table is empty
    try:
        conn = psycopg.connect(
            host="localhost", port=5432, user="postgres",
            password=db_pass, dbname=replay_dbname
        )
        with conn.cursor() as cur:
            cur.execute("SELECT COUNT(*) FROM snapshots")
            count = cur.fetchone()[0]
        conn.close()

        if count == 0:
            print("No replay data found. Running import_logs.py...")
            import_script = os.path.join(os.path.dirname(__file__), "import_logs.py")
            if os.path.exists(import_script):
                result = subprocess.run(
                    ["python", import_script],
                    cwd=os.path.dirname(__file__),
                    capture_output=True,
                    text=True
                )
                print(result.stdout)
                if result.returncode != 0:
                    print(f"Import warning: {result.stderr}")
            else:
                print("import_logs.py not found. Replay will have no data.")
    except Exception as e:
        print(f"Error checking/importing data: {e}")
        return False

    return True


def setup_simulation_database(sim_dbname: str):
    """Create simulation database and schema for a new simulation."""
    import psycopg

    db_pass = os.environ.get("DB_PASS", "")

    # Step 1: Create database if it doesn't exist
    try:
        conn = psycopg.connect(
            host="localhost", port=5432, user="postgres",
            password=db_pass, dbname="postgres"
        )
        conn.autocommit = True
        with conn.cursor() as cur:
            cur.execute(f"SELECT 1 FROM pg_database WHERE datname = %s", (sim_dbname,))
            if not cur.fetchone():
                print(f"Creating simulation database: {sim_dbname}")
                cur.execute(f'CREATE DATABASE "{sim_dbname}"')
        conn.close()
    except Exception as e:
        print(f"Error creating simulation database: {e}")
        return False

    # Step 2: Create schema (tables)
    try:
        conn = psycopg.connect(
            host="localhost", port=5432, user="postgres",
            password=db_pass, dbname=sim_dbname
        )
        with conn.cursor() as cur:
            cur.execute("SELECT EXISTS (SELECT FROM information_schema.tables WHERE table_name = 'snapshots')")
            tables_exist = cur.fetchone()[0]

        if not tables_exist:
            print(f"Creating schema for {sim_dbname}...")
            migrations_path = os.path.join(os.path.dirname(__file__), "migrations.sql")
            if os.path.exists(migrations_path):
                with open(migrations_path, 'r') as f:
                    sql = f.read()
                with conn.cursor() as cur:
                    cur.execute(sql)
                conn.commit()
                print("Schema created for simulation database.")
        conn.close()
    except Exception as e:
        print(f"Error creating simulation schema: {e}")
        return False

    return True


async def lifespan(app: FastAPI):
    """On application startup, create a background task for the Kafka consumer."""
    print("Application startup...")

    # Auto-setup replay database (create DB, schema, import data if needed)
    setup_replay_database()

    global realtime_db
    global dashboard_db
    global replay_state
    global _replay_advance_task

    # Initialize realtime_db for replay mode using the ns3_e2e_test database
    # This database contains historical simulation data for replay
    replay_dbname = os.environ.get("DB_NAME", "ns3_e2e_test")
    print(f"Initializing realtime_db with database: {replay_dbname}")
    realtime_db = PostgresMetricsLogger(
        host="localhost",
        password=os.environ["DB_PASS"],
        dbname=replay_dbname,
    )
    dashboard_db = realtime_db  # Default to replay mode on startup

    # Initialize replay time range from database
    time_range = realtime_db.get_time_range()
    replay_state["min_time"] = time_range["min_time"]
    replay_state["max_time"] = time_range["max_time"]
    replay_state["current_time"] = time_range["min_time"]
    print(f"Replay time range: {time_range['min_time']}s to {time_range['max_time']}s")

    # Start the replay advance task
    _replay_advance_task = asyncio.create_task(auto_advance_replay_time())

    global _kafka_producer
    _kafka_producer = AIOKafkaProducer(bootstrap_servers=KAFKA_BOOTSTRAP_SERVERS)
    await _kafka_producer.start()
    print("Kafka producer started.")

    yield

    print("Stopping all simulations...")
    sim_control.stop_all_simulations()

    if _kafka_producer:
        await _kafka_producer.stop()
        print("Kafka producer stopped.")


app = FastAPI(lifespan=lifespan)

origins = ["http://localhost:3000"]
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,  # Allow cookies to be sent with cross-origin requests
    # Allow all HTTP methods (GET, POST, PUT, DELETE, etc.)
    allow_methods=["*"],
    allow_headers=["*"],  # Allow all headers in cross-origin requests
)

# --- Kafka Configuration ---
KAFKA_TOPIC = args.kafka_topic  # Change this to your Kafka topic
# Change to your Kafka broker address
KAFKA_BOOTSTRAP_SERVERS = "localhost:9092"
DBNAME = args.dbname


def get_current_replay_time() -> float | None:
    """
    Returns the current replay time if in replay mode (dashboard_db == realtime_db),
    otherwise returns None (which means use latest snapshot).
    """
    if dashboard_db == realtime_db and realtime_db is not None:
        return replay_state["current_time"]
    return None


@app.get("/home/channel-interference")
def get_channel_interference(bssid: str):
    replay_time = get_current_replay_time()
    return dashboard_db.get_noise_per_channel(bssid, at_time=replay_time)


@app.get("/ap/list")
def get_aps(limit: Union[int, None] = None):
    replay_time = get_current_replay_time()
    return dashboard_db.get_last_aps(at_time=replay_time)


@app.get("/ap/metrics/{bssid}")
def ap_metrics(bssid: str):
    replay_time = get_current_replay_time()
    return dashboard_db.ap_metrics(bssid, at_time=replay_time)


@app.get("/simulation/ap/metrics/{node_id}")
def simulation_ap_metrics(node_id: int):
    if simulation_db is None:
        return JSONResponse(
            status_code=403, content={"message": "no running simulation"}
        )
    return simulation_db.ap_metrics_by_node_id(node_id)


@app.get("/simulation/client/metrics/{node_id}")
def simulation_client_metrics(node_id: int):
    if simulation_db is None:
        return JSONResponse(
            status_code=403, content={"message": "no running simulation"}
        )
    return simulation_db.client_metrics_by_node_id(node_id)


@app.get("/simulation/uptime")
def simulation_uptime():
    if simulation_db is not None:
        with simulation_db.conn.cursor() as cur:
            snapshot = simulation_db.latest_snapshot(cur)
            if snapshot is None:
                return {"time": {"snapshot_id": 0, "snapshot_id_unix": 0, "sim_time_seconds": 0}}
            # Return the full snapshot object as expected by frontend
            return {"time": snapshot}

    # In replay mode, return the current replay time
    if dashboard_db == realtime_db:
        return {"time": {"snapshot_id": 0, "snapshot_id_unix": int(replay_state["current_time"]), "sim_time_seconds": replay_state["current_time"]}}

    return {"message": "no simulation running"}


@app.get("/ap/{bssid}/clients")
def get_ap_clients(bssid: str):
    """
    Get list of all clients connected to a specific AP.

    Parameters:
        bssid: The BSSID of the AP (MAC address format)

    Returns:
        List of connected clients with their metrics (ap_view_rssi, ap_view_snr, latency, jitter, packet_loss_rate)
    """
    replay_time = get_current_replay_time()
    return dashboard_db.get_connected_clients(bssid, at_time=replay_time)


@app.get("/ap/{bssid}/channel-utilization")
def get_ap_channel_utilization(bssid: str, timeResolution: int = 10):
    """
    Get channel utilization history for a specific AP.

    Parameters:
        bssid: The BSSID of the AP (MAC address format)
        timeResolution: Duration in minutes to retrieve history (default: 10)

    Returns:
        List of channel utilization data points with timestamps
    """
    replay_time = get_current_replay_time()
    return dashboard_db.get_channel_utilization_history(bssid, timeResolution, at_time=replay_time)


@app.get("/sta/metrics")
def get_sta_metrics():
    """
    Get STA metrics with latency, jitter, packet loss, ap_view_rssi and ap_view_snr across all clients on the network.

    Returns:
        Dictionary containing:
        - aggregate_metrics: Average latency, jitter, and packet loss across all clients
        - client_metrics: List of individual client metrics
    """
    replay_time = get_current_replay_time()
    return dashboard_db.sta_metrics(at_time=replay_time)


@app.get("/sta/time-series")
def get_sta_time_series(
    metric_type: str,
    client_mac_address: Union[str, None] = None,
    timeResolution: int = 10,
):
    """
    Get time series data for STA metrics (latency, jitter, or packet loss).

    Parameters:
        metric_type: Type of metric for time series data. Options: 'latency', 'jitter', 'packet_loss'
        client_mac_address: Optional MAC address to filter by specific client. If None, gets aggregate metrics for all clients
        timeResolution: Duration in minutes to retrieve time series data (default: 10).

    Returns:
        List of dictionaries containing time series data with timestamps and metric values
    """
    return dashboard_db.get_sta_metrics_time_series(
        metric_type, client_mac_address, timeResolution
    )


@app.get("/sta/time-series/{client_mac_address}/{metric_type}/individual")
def get_sta_time_series_individual_client(
    client_mac_address: str,
    metric_type: str,
    timeResolution: int = 10,
):
    """
    Get time series data for a specific STA's metrics (latency, jitter, packet loss, RSSI, SNR).

    Parameters:
        client_mac_address: The MAC address of the client to filter by.
        metric_type: Type of metric for time series data. Options: 'latency', 'jitter', 'packet_loss', 'rssi', 'snr'.
        timeResolution: Duration in minutes to retrieve time series data (default: 10).

    Returns:
        List of dictionaries containing time series data with timestamps and metric values.
    """
    if dashboard_db is None:
        return JSONResponse(
            status_code=403, content={"message": "dashboard database not connected"}
        )

    try:
        return dashboard_db.get_sta_metrics_time_series_by_client(
            client_mac_address, metric_type, timeResolution
        )
    except ValueError as e:
        return JSONResponse(status_code=400, content={"message": str(e)})


@app.get("/sta/count-by-band")
def get_sta_count_by_band():
    """
    Get count of STAs grouped by the band they are using based on their connected AP.

    Returns:
        List of dictionaries conplayground/logstaining:
        - band: The frequency band (e.g., "2.4GHz", "5GHz")
        - sta_count: Number of STAs connected to APs on this band
    """
    return dashboard_db.get_sta_count_by_band()


@app.get("/sta/count-by-metric/{metric_type}")
def get_sta_count_by_metric(metric_type: str):
    """
    Get count of STAs grouped by a specific metric.

    Supported metric types:
    - throughput: Group by throughput usage (low, medium, high, very_high)
      - low: 0-3 mbps
      - medium: 3-6 mbps
      - high: 6-10 mbps
      - very_high: 10+ mbps

    - rssi: Group by signal strength (good, fair, poor)
      - good: > -50 dBm
      - fair: -50 to -65 dBm
      - poor: < -65 dBm

    Returns:
        List of dictionaries containing:
        - category: The metric category
        - sta_count: Number of STAs in this category
    """
    if metric_type == "throughput":
        return dashboard_db.get_sta_count_by_throughput()
    elif metric_type == "rssi":
        return dashboard_db.get_sta_count_by_rssi()
    else:
        return JSONResponse(
            status_code=400,
            content={
                "message": f"Invalid metric type: {
                    metric_type
                }. Supported types: throughput, rssi"
            },
        )


@app.get("/network/client-distribution")
def get_client_distribution():
    """
    Get the distribution of clients across different metric ranges for latency, packet loss, RSSI, SNR, and jitter
    """
    return dashboard_db.get_client_metrics_distribution()


@app.get("/sta/client-quality")
def get_client_quality():
    """
    Get counts of clients categorized as good, okayish, or bad based on latency,
    jitter, and packet loss thresholds at the most recent snapshot.

    Thresholds:
        - Latency (ms): good <= 50, okayish <= 100, bad > 100
        - Jitter (ms): good <= 10, okayish <= 30, bad > 30
        - Packet Loss (%): good <= 1, okayish <= 5, bad > 5
    """
    return dashboard_db.get_client_quality_summary()


@app.get("/client_view")
def client_view():
    """
        Shows STA-level performance, combining signal quality, transport-layer healt
    h, and throughput.
    Data originates from active and passive probing in the network
    """
    return dashboard_db.api_client_view()


@app.get("/metrics")
def metrics():
    """
        Provides system-level KPIs from AP-side telemetry and roaming statistics.
    Includes AP radio performance and AP-wise roaming outcomes.
    """
    return dashboard_db.api_metrics()


@app.get("/sensing")
def sensing():
    """
        Returns interference sensing data produced by the sensing pipeline.
    Each snapshot groups results by access point (BSSID), showing interference
    composition across channels and bands.
    This helps diagnose hidden terminals, non-Wi-Fi interference, radar pulses, a
    nd spatial contention
    """
    return dashboard_db.api_sensing()


@app.post("/playground/config")
def playground_config(data: dict):
    """
    Receives a JSON configuration and writes it to a file.
    """
    global simulation_build_time
    simulation_build_time = time.time()
    write_simulation_config(data, f"~/config/{data['session_name']}.json")
    return {"status": "success", "message": "Configuration updated"}


@app.get("/playground/current")
def playground_current_config():
    """
    Returns the current simulation configuration.
    """
    if simulation_db is None:
        return {"running": False, "message": "no running simulation"}
    config = get_simulation_config()
    config["session_name"] = simulation_name
    config["running"] = True
    return config


@app.get("/playground/config/{name}")
def get_playground_config_by_name(name: str):
    """
    Returns the simulation configuration for a given name.
    """
    config_data = get_config_by_name(name)
    if not config_data:
        return JSONResponse(
            status_code=404, content={"message": f"Config '{name}' not found"}
        )
    return config_data


@app.patch("/playground/config/{name}")
def rename_playground_config(name: str, data: dict):
    """
    Renames a playground session configuration.
    """
    new_name = data.get("new_name")
    if not new_name:
        return JSONResponse(
            status_code=400, content={"message": "new_name not provided"}
        )
    try:
        rename_config(name, new_name)
        return {
            "status": "success",
            "message": f"Config '{name}' renamed to '{new_name}'",
        }
    except FileNotFoundError as e:
        return JSONResponse(status_code=404, content={"message": str(e)})
    except FileExistsError as e:
        return JSONResponse(status_code=409, content={"message": str(e)})


@app.delete("/playground/config/{name}")
def delete_playground_config(name: str):
    """
    Deletes a playground session configuration.
    """
    try:
        delete_config(name)
        return {"status": "success", "message": f"Config '{name}' deleted"}
    except FileNotFoundError as e:
        return JSONResponse(status_code=404, content={"message": str(e)})


@app.post("/simulation/start/{name}")
async def start_simulation(name: str):
    """
    Starts the simulation run by restarting docker-compose.
    Transforms the frontend config to complete ns-3 format before starting.

    Returns:
        Status message indicating the simulation has started
    """
    global simulation_start_time
    global simulation_db
    global simulation_name
    global _metrics_broadcast_task
    global _status_broadcast_task
    global _simulation_client_ws
    global _kafka_consumer_task
    global _kafka_events_consumer_task

    if simulation_db is not None:
        return JSONResponse(status_code=403, content="simulation is already running")

    # Load frontend config and transform to complete ns-3 format
    try:
        frontend_config = get_config_by_name(name)
        # Transform to ns-3 format with all required sections (aci, ofdma, virtualInterferers, etc.)
        # Write to config-simulation.json which is the standard file Docker container loads
        ns3_config_path = "~/config/config-simulation.json"
        write_simulation_config(frontend_config, ns3_config_path)
        config_file_to_use = "./config/config-simulation.json"
    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"message": f"Failed to transform config: {str(e)}"}
        )

    global dashboard_db

    simulation_start_time = time.time()
    sim_control.start_dockerc_compose(
        config_file=config_file_to_use
    )

    # Create simulation database and schema
    sim_dbname = f"sim_{sim_control.simulation_port - 1}"
    setup_simulation_database(sim_dbname)

    simulation_db = PostgresMetricsLogger(
        "localhost", sim_dbname, os.environ["DB_PASS"]
    )
    dashboard_db = simulation_db  # Set dashboard_db to use simulation database
    # Start simulation log client
    port = sim_control.simulation_port - 1
    _simulation_client_ws = asyncio.create_task(simulation_ws_client(port))
    _metrics_broadcast_task = asyncio.create_task(broadcast_aggregated_metrics())
    _status_broadcast_task = asyncio.create_task(broadcast_simulation_status())

    # Start Kafka consumer for position data
    kafka_topic = f"sim_{sim_control.simulation_port - 1}"
    _kafka_consumer_task = asyncio.create_task(consume_simulation_kafka(kafka_topic))

    # Start Kafka consumer for RRM events (simulator-events and rl-events topics)
    _kafka_events_consumer_task = asyncio.create_task(consume_simulation_events_kafka())

    simulation_name = name
    return {"status": "success", "message": "Simulation started"}


@app.post("/simulation/stop")
def stop_simulation():
    """
    Stops the simulation run by force stopping docker-compose.

    Returns:
        Status message indicating the simulation has stopped
    """
    global simulation_db
    global _metrics_broadcast_task
    global _status_broadcast_task
    global _kafka_consumer_task
    global _kafka_events_consumer_task
    global dashboard_db

    sim_control.stop_simulation(sim_control.simulation_port - 1)
    if _metrics_broadcast_task and not _metrics_broadcast_task.done():
        _metrics_broadcast_task.cancel()
    if _status_broadcast_task and not _status_broadcast_task.done():
        _status_broadcast_task.cancel()
    if _simulation_client_ws and not _simulation_client_ws.done():
        _simulation_client_ws.cancel()
    if _kafka_consumer_task and not _kafka_consumer_task.done():
        _kafka_consumer_task.cancel()
    if _kafka_events_consumer_task and not _kafka_events_consumer_task.done():
        _kafka_events_consumer_task.cancel()

    simulation_db = None
    dashboard_db = realtime_db
    return {"status": "success", "message": "Simulation stopped"}


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
    if simulation_db is None:
        return JSONResponse(
            status_code=400,
            content={"status": "error", "message": "No simulation is currently running"}
        )

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
        simulation_id = f"sim_{sim_control.simulation_port - 1}"
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


# =============================================================================
# RL PROCESS CONTROL ENDPOINTS
# =============================================================================

@app.post("/rl/start")
async def start_rl_process():
    """
    Starts the RL training process (RL/main.py) for the currently running simulation.

    The RL process will:
    - Connect to the simulation's Kafka topic for metrics
    - Send optimization commands via optimization-commands topic
    - Publish RL events to rl-events topic

    Returns:
        Status message indicating the RL process has started
    """
    global _rl_process
    global _rl_process_simulation_id

    if simulation_db is None:
        return JSONResponse(
            status_code=400,
            content={"status": "error", "message": "No simulation is currently running"}
        )

    if _rl_process is not None and _rl_process.poll() is None:
        return JSONResponse(
            status_code=400,
            content={"status": "error", "message": "RL process is already running"}
        )

    try:
        # Get the simulation ID from the current running simulation
        simulation_id = f"sim_{sim_control.simulation_port - 1}"
        _rl_process_simulation_id = simulation_id

        # Path to the RL directory (relative to api directory, RL is in parent/RL)
        rl_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "RL"))
        rl_main_path = os.path.join(rl_dir, "main.py")

        if not os.path.exists(rl_main_path):
            return JSONResponse(
                status_code=500,
                content={"status": "error", "message": f"RL main.py not found at {rl_main_path}"}
            )

        # Check if venv exists and use it
        venv_python = os.path.join(rl_dir, "venv", "bin", "python")
        if os.path.exists(venv_python):
            python_cmd = venv_python
        else:
            python_cmd = "python3"

        # Start the RL process with proper environment
        env = os.environ.copy()
        env["SIMULATION_ID"] = simulation_id
        env["KAFKA_BROKER"] = "localhost:9092"
        env["METRICS_TOPIC"] = simulation_id  # ns3 metrics topic is same as simulation_id

        _rl_process = subprocess.Popen(
            [python_cmd, rl_main_path],
            cwd=rl_dir,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            preexec_fn=os.setsid  # Create new process group for clean termination
        )

        return {
            "status": "success",
            "message": "RL process started",
            "simulationId": simulation_id,
            "pid": _rl_process.pid
        }

    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"status": "error", "message": f"Failed to start RL process: {str(e)}"}
        )


@app.post("/rl/stop")
async def stop_rl_process():
    """
    Stops the running RL training process.

    Returns:
        Status message indicating the RL process has stopped
    """
    global _rl_process
    global _rl_process_simulation_id

    if _rl_process is None:
        return JSONResponse(
            status_code=400,
            content={"status": "error", "message": "No RL process is running"}
        )

    if _rl_process.poll() is not None:
        # Process already terminated
        _rl_process = None
        _rl_process_simulation_id = None
        return {"status": "success", "message": "RL process was already stopped"}

    try:
        # Send SIGTERM to the process group for clean shutdown
        os.killpg(os.getpgid(_rl_process.pid), signal.SIGTERM)

        # Wait for process to terminate (with timeout)
        try:
            _rl_process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            # Force kill if it doesn't stop gracefully
            os.killpg(os.getpgid(_rl_process.pid), signal.SIGKILL)
            _rl_process.wait()

        _rl_process = None
        _rl_process_simulation_id = None

        return {"status": "success", "message": "RL process stopped"}

    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"status": "error", "message": f"Failed to stop RL process: {str(e)}"}
        )


@app.get("/rl/status")
async def get_rl_status():
    """
    Gets the current status of the RL training process.

    Returns:
        Status object with running state and process info
    """
    global _rl_process
    global _rl_process_simulation_id

    if _rl_process is None:
        return {
            "status": "stopped",
            "running": False,
            "simulationId": None,
            "pid": None
        }

    # Check if process is still running
    poll_result = _rl_process.poll()
    if poll_result is not None:
        # Process has terminated
        _rl_process = None
        sim_id = _rl_process_simulation_id
        _rl_process_simulation_id = None
        return {
            "status": "stopped",
            "running": False,
            "exitCode": poll_result,
            "simulationId": sim_id,
            "pid": None
        }

    return {
        "status": "running",
        "running": True,
        "simulationId": _rl_process_simulation_id,
        "pid": _rl_process.pid
    }


@app.get("/playground/sessions/list")
def list_sessions():
    """
    Returns a list of all playground sessions.
    Sessions are managed client-side via localStorage, so this returns an empty array.
    The frontend will load sessions from localStorage using loadSessionsSync().
    """
    return get_user_config_list()


@app.websocket("/playground/logs")
async def playground_logs_websocket(websocket: WebSocket):
    """
    Creates a websocket connection to stream playground logs to the client.
    It polls for recent roaming logs and sends them to the client.
    Also handles incoming commands from the client and forwards them to Kafka.
    """
    await ws_manager.connect(websocket)
    try:
        await websocket.send_json(initial_playground_logs())
        while True:
            # Receive message from client
            message = await websocket.receive_text()
            try:
                data = json.loads(message)
                # Check if it's a command
                if "command" in data:
                    data["simulationId"] = f"sim_{sim_control.simulation_port - 1}"
                    try:
                        # Validate against model
                        cmd = SimulationCommand(**data)
                        if _kafka_producer:
                            message_json = cmd.model_dump_json().encode("utf-8")
                            await _kafka_producer.send_and_wait(
                                "simulation-commands", message_json
                            )
                        else:
                            print(
                                "Kafka producer not initialized, cannot forward command"
                            )
                    except Exception as e:
                        print(f"Invalid command format: {e}")
            except json.JSONDecodeError:
                pass  # Ignore non-JSON messages (like simple keepalives)
    except WebSocketDisconnect:
        ws_manager.disconnect(websocket)
        print("Client disconnected from playground log stream.")


@app.websocket("/visualizer")
async def visualizer_websocket(websocket: WebSocket):
    """
    Creates a websocket connection to stream position data to the visualizer client.
    Position data is broadcasted from the Kafka consumer when received.
    """
    await visualizer_ws_manager.connect(websocket)
    try:
        while True:
            # Keep connection alive and wait for messages
            await websocket.receive_text()
    except WebSocketDisconnect:
        visualizer_ws_manager.disconnect(websocket)
        print("Client disconnected from visualizer stream.")


def initial_playground_logs(limit: int = 100):
    """
    Fetches roaming logs and aggregated metrics grouped by 5-minute simulation intervals.

    Parameters:
        limit: Maximum number of logs to return per category (default: 100)

    Returns:
        List of LogEntry compatible objects containing roaming logs and aggregated metrics
    """

    if simulation_db is None:
        return JSONResponse(
            status_code=403, content={"message": "no running imulation"}
        )

    log_entries = []
    if simulation_build_time:
        log_entries.append(
            {
                "id": "build-log",
                "timestamp": simulation_build_time * 1000,
                "message": "Simulation built",
                "type": "info",
                "source": "Build",
            }
        )
    if simulation_start_time:
        log_entries.append(
            {
                "id": "start-log",
                "timestamp": simulation_start_time * 1000,
                "message": "Simulation started",
                "type": "info",
                "source": "System",
            }
        )

    # Add roaming logs
    roaming_logs = simulation_db.get_roaming_logs(limit)
    for log in roaming_logs:
        log_entries.append(
            {
                "id": f"roam-{log['roam_id']}",
                "timestamp": log["snapshot_id_unix"] * 1000,
                "message": f"Client {log['sta_id']} roamed from {log['from_bssid']} to {log['to_bssid']}",
                "type": "info",
                "source": "Roaming",
            }
        )

    # Add sensing logs from database
    sensing_logs = simulation_db.get_sensing_logs(limit)

    for sensing in sensing_logs:
        confidence_pct = (
            f"{sensing['confidence'] * 100:.1f}%"
            if sensing["confidence"] is not None
            else "N/A"
        )
        duty_cycle = (
            f"{sensing['interference_duty_cycle']:.1f}%"
            if sensing["interference_duty_cycle"] is not None
            else "N/A"
        )
        log_entries.append(
            {
                "id": f"sensing-{sensing['sensing_result_id']}",
                "timestamp": sensing["timestamp_unix"] * 1000,
                "message": f"Sensing: AP {sensing['bssid']} ch{sensing['channel']} detected {sensing['interference_type'] or 'unknown'} interference (confidence: {confidence_pct}, duty cycle: {duty_cycle})",
                "type": "warning" if sensing["interference_type"] else "info",
                "source": "Sensing",
            }
        )

    # Add aggregated metrics (5-minute intervals, per AP)
    aggregated_metrics = simulation_db.get_aggregated_metrics_by_interval(5)
    for idx, metrics in enumerate(aggregated_metrics):
        fields = [
            "avg_latency_ms",
            "avg_jitter_ms",
            "avg_packet_loss_rate",
            "avg_rssi",
            "avg_snr",
        ]
        skip_record = False
        for field in fields:
            if metrics[field] is None:
                skip_record = True

        if skip_record:
            continue

        log_entries.append(
            {
                "id": f"metrics-{metrics['bssid']}-{idx}",
                "timestamp": metrics["timestamp"] * 1000,
                "message": f"AP {metrics['bssid']}: Avg Latency: {metrics['avg_latency_ms']:.2f}ms, Jitter: {metrics['avg_jitter_ms']:.2f}ms, Packet Loss: {metrics['avg_packet_loss_rate']:.2f}%, RSSI: {metrics['avg_rssi']:.2f}dBm, SNR: {metrics['avg_snr']:.2f}dB",
                "type": "info",
                "source": "Metrics",
            }
        )

    # Sort by timestamp
    log_entries.sort(key=lambda x: x["timestamp"], reverse=True)

    return log_entries


@app.get("/simulation/sta/list/{ap_node_id}")
def simulation_sta_metrics(ap_node_id: int):
    if simulation_db is None:
        return JSONResponse(
            status_code=403, content={"message": "no running simulation"}
        )

    # Directly query for the AP by node_id for better performance
    ap_metrics = simulation_db.ap_metrics_by_node_id(ap_node_id)
    if not ap_metrics:
        return []

    bssid = ap_metrics.get("bssid")
    if not bssid:
        return JSONResponse(
            status_code=500,
            content={"message": f"BSSID not found for AP with node_id {ap_node_id}"},
        )

    # Get list of connected clients
    assoc_clients = simulation_db.get_connected_clients(bssid)

    client_ids = []
    for client in assoc_clients:
        client_ids.append(client["node_id"])

    # Build the response with AP metrics and associated clients
    result = {
        "bssid": ap_metrics.get("bssid"),
        "clientcount": ap_metrics.get("count_clients", 0),
        "uplink_throughput_mbps": ap_metrics.get("uplink_throughput_mbps"),
        "downlink_throughput_mbps": ap_metrics.get("downlink_throughput_mbps"),
        "channel": ap_metrics.get("channel"),
        "phy_transmit_power": ap_metrics.get("phy_tx_power_level"),
        "last_updated_seconds": ap_metrics.get("last_update_seconds", 0),
        "channel_width": ap_metrics.get("channel_width", 20),
        "clients": client_ids,
    }

    return result


# =============================================================================
# UNIFIED HEALTH SCORES (RL-Aligned) - NEW ENDPOINTS
# =============================================================================
@app.get("/network/health-unified")
def get_health_unified(time_resolution: int = 30):
    """
    Get unified AP Health and Network Score using RL-aligned formula.

    This endpoint calculates health scores using the same formula as the RL agent:
    - Formula: 0.35*throughput + 0.10*(1-jitter) + 0.35*(1-loss) + 0.20*(1-latency)
    - Uses p50 for throughput, p75 for latency/jitter/loss (percentiles)
    - Auto-resets every 30 minutes
    - Network Score = formula(avg_metrics), NOT avg(ap_scores)

    Parameters:
        time_resolution: Max time window in minutes (default: 30)

    Returns:
        {
            "ap_health": {bssid: score, ...},
            "network_score": float (0-100),
            "last_reset_sim_time": float,
            "current_sim_time": float,
            "time_until_reset": float (seconds),
            "ap_count": int
        }
    """
    replay_time = get_current_replay_time()
    return dashboard_db.get_health_scores_unified(time_resolution, at_time=replay_time)


@app.get("/network/health-time-series")
def get_health_time_series(time_resolution: int = 30):
    """
    Get network health scores over time for charting.

    Uses the same RL-aligned formula as /network/health-unified.

    Parameters:
        time_resolution: Max time window in minutes (default: 30)

    Returns:
        List of {
            "sim_time_seconds": float,
            "network_score": float (0-100)
        }
    """
    replay_time = get_current_replay_time()
    return dashboard_db.get_health_scores_time_series(time_resolution, at_time=replay_time)


@app.post("/network/health-reset")
def reset_health_baseline():
    """
    Reset the health calculation baseline.

    Called when:
    - RL is triggered from playground (manual reset)
    - User wants to start fresh measurement

    After reset, health scores will be calculated from the current time forward.

    Returns:
        {"status": "ok", "message": "Health baseline reset"}
    """
    from db import NetworkQuerier

    NetworkQuerier.reset_health_baseline()
    return {
        "status": "ok",
        "message": "Health baseline reset. Scores will be calculated from now.",
    }


@app.get("/network/rrm-updates")
def get_rrm_updates():
    """
    Fetch combined RRM updates (Fast Loop and Slow Loop) history.
    """
    if dashboard_db is None:
        return []
    return dashboard_db.get_rrm_updates()


@app.post("/data/switch")
def switch_db(data: dict):
    global dashboard_db
    if data["to"] == "replay":
        dashboard_db = realtime_db
        return {"message": "switched to realtime db"}
    elif data["to"] == "simulation":
        if simulation_db:
            dashboard_db = simulation_db
            return {"message": "switched to simulation db"}
        else:
            return JSONResponse(status_code=400, content={"message": "no simulation running, cannot switch to simulation mode"})
    else:
        return JSONResponse(status_code=400, content={"message": "invalid choice of database"})


# ============== REPLAY CONTROL ENDPOINTS ==============

@app.get("/replay/state")
def get_replay_state():
    """Get current replay state including time, playing status, and speed."""
    return {
        "current_time": replay_state["current_time"],
        "is_playing": replay_state["is_playing"],
        "speed": replay_state["speed"],
        "min_time": replay_state["min_time"],
        "max_time": replay_state["max_time"],
    }


@app.post("/replay/play")
async def replay_play():
    """Start or resume replay playback."""
    global _replay_advance_task
    replay_state["is_playing"] = True
    replay_state["last_update"] = time.time()

    # Start the auto-advance task if not already running
    if _replay_advance_task is None or _replay_advance_task.done():
        _replay_advance_task = asyncio.create_task(auto_advance_replay_time())

    return {"message": "Replay started", "current_time": replay_state["current_time"]}


@app.post("/replay/pause")
async def replay_pause():
    """Pause replay playback."""
    replay_state["is_playing"] = False
    return {"message": "Replay paused", "current_time": replay_state["current_time"]}


@app.post("/replay/seek")
def replay_seek(data: dict):
    """Seek to a specific time in the replay."""
    target_time = data.get("time", replay_state["min_time"])
    # Clamp to valid range
    target_time = max(replay_state["min_time"], min(replay_state["max_time"], target_time))
    replay_state["current_time"] = target_time
    replay_state["last_update"] = time.time()
    return {"message": f"Seeked to {target_time}s", "current_time": target_time}


@app.post("/replay/speed")
def replay_set_speed(data: dict):
    """Set replay playback speed (1x, 2x, 5x, 10x, 20x)."""
    speed = data.get("speed", 1.0)
    if speed not in [1, 2, 5, 10, 20]:
        speed = 1.0
    replay_state["speed"] = float(speed)
    return {"message": f"Speed set to {speed}x", "speed": speed}


@app.post("/replay/reset")
def replay_reset():
    """Reset replay to the beginning."""
    replay_state["current_time"] = replay_state["min_time"]
    replay_state["is_playing"] = False
    return {"message": "Replay reset", "current_time": replay_state["current_time"]}


async def auto_advance_replay_time():
    """Background task to auto-advance replay time when playing."""
    while True:
        if replay_state["is_playing"]:
            current_loop_time = time.time()
            if replay_state["last_update"] is not None:
                elapsed = current_loop_time - replay_state["last_update"]
                # Advance simulation time by elapsed * speed
                replay_state["current_time"] += elapsed * replay_state["speed"]

                # Loop back to start if we reach the end
                if replay_state["current_time"] >= replay_state["max_time"]:
                    replay_state["current_time"] = replay_state["min_time"]

            replay_state["last_update"] = current_loop_time

            # Broadcast replay state to connected clients
            await ws_manager.broadcast({
                "mtype": "replay_state",
                "current_time": replay_state["current_time"],
                "is_playing": replay_state["is_playing"],
                "speed": replay_state["speed"],
            })

        await asyncio.sleep(0.5)  # Update every 500ms


def transform_metrics(metrics: dict, interferers_config: list = None) -> dict:
    """Transform Kafka metrics to visualization-friendly format."""
    aps = []
    stas = []
    interferers = []

    for bssid, ap in metrics["ap_metrics"].items():
        ap_data = {
            "id": ap["node_id"],
            "bssid": bssid,
            "x": ap["position_x"],
            "y": ap["position_y"],
            "z": ap["position_z"],
            "channel": ap["channel"],
            "band": ap["band"],
            "utilization": ap["channel_utilization"],
            "tx_power": ap["tx_power_dbm"],
            "client_count": ap["associated_clients"],
            "throughput": ap["throughput_mbps"],
        }
        aps.append(ap_data)

        # Extract STA information from connection metrics
        for sta_addr, conn in ap["connection_metrics"].items():
            sta_data = {
                "id": conn["node_id"],
                "address": sta_addr,
                "x": conn["position_x"],
                "y": conn["position_y"],
                "z": conn["position_z"],
                "connected_ap": bssid,
                "rssi": conn["sta_view_rssi"],
                "snr": conn["sta_view_snr"],
                "throughput_up": conn["uplink_throughput_mbps"],
                "throughput_down": conn["downlink_throughput_mbps"],
                "latency": conn["mean_rtt_latency"],
                "jitter": conn["jitter_ms"],
                "packet_loss": conn["packet_loss_rate"],
            }
            stas.append(sta_data)

    # Transform interferers from config (excluding radar/DFS)
    if interferers_config:
        for idx, intf in enumerate(interferers_config):
            intf_type = intf.get("type", "other").lower()
            # Skip radar (DFS) interferers as requested
            if intf_type == "radar":
                continue
            # Map ns-3 types to frontend types
            type_map = {"bluetooth": "bluetooth", "microwave": "microwave", "zigbee": "zigbee", "cordless": "other"}
            interferer_data = {
                "id": idx,
                "type": type_map.get(intf_type, "other"),
                "x": intf.get("position", {}).get("x", 0),
                "y": intf.get("position", {}).get("y", 0),
                "z": intf.get("position", {}).get("z", 0),
                "power_dbm": intf.get("txPowerDbm"),
                "active": intf.get("active", True),
            }
            interferers.append(interferer_data)

    return {
        "sim_time": metrics["sim_time_seconds"],
        "aps": aps,
        "stas": stas,
        "interferers": interferers,
    }


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)
