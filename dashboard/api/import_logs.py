#!/usr/bin/env python3
"""
Import Kafka log files into the replay database (ns3_e2e_test).
This script parses ns3_metrics, simulator_events, and rl_events log files
and populates the database for the replay functionality.

Usage:
    python import_logs.py  # Auto-detect files from kafka-logs/
    python import_logs.py --metrics <ns3_metrics.json> --events <simulator_events.json> --rl <rl_events.json>
"""

import argparse
import json
import os
import glob
import psycopg
from dotenv import load_dotenv

load_dotenv()

# Default kafka-logs directory (relative to this script)
KAFKA_LOGS_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "kafka-logs")

# Node ID to BSSID mapping
NODE_TO_BSSID = {
    0: "00:00:00:00:00:01",
    1: "00:00:00:00:00:02",
    2: "00:00:00:00:00:03",
    3: "00:00:00:00:00:04",
}

MAX_SIM_TIME = 1800.0  # 30 minutes


def get_db_connection(dbname="ns3_e2e_test"):
    """Get database connection."""
    password = os.environ.get("DB_PASS", "")
    return psycopg.connect(
        host="localhost",
        port=5432,
        user="postgres",
        password=password,
        dbname=dbname
    )


def clear_database(conn):
    """Clear all existing data from replay tables."""
    print("Clearing existing data...")
    with conn.cursor() as cur:
        cur.execute("DELETE FROM fast_loop_changes")
        cur.execute("DELETE FROM fast_loop_updates")
        cur.execute("DELETE FROM slow_loop_changes")
        cur.execute("DELETE FROM slow_loop_updates")
        cur.execute("DELETE FROM sta_metrics")
        cur.execute("DELETE FROM ap_metrics")
        cur.execute("DELETE FROM ap_channel_metrics")
        cur.execute("DELETE FROM roaming_history")
        cur.execute("DELETE FROM snapshots")
    conn.commit()
    print("Database cleared.")


def import_ns3_metrics(conn, filepath):
    """Import ns3_metrics log file into snapshots, ap_metrics, sta_metrics tables."""
    print(f"Importing ns3_metrics from {filepath}...")

    snapshot_count = 0
    ap_count = 0
    sta_count = 0

    with open(filepath, 'r') as f:
        for line in f:
            try:
                record = json.loads(line.strip())
                data = record.get("data", {})

                sim_time = data.get("sim_time_seconds", 0)
                if sim_time > MAX_SIM_TIME:
                    continue  # Skip data beyond 30 minutes

                timestamp_unix = data.get("timestamp_unix", 0)

                with conn.cursor() as cur:
                    # Insert snapshot
                    cur.execute("""
                        INSERT INTO snapshots (snapshot_id_unix, sim_time_seconds, last_update_seconds)
                        VALUES (%s, %s, %s)
                        RETURNING snapshot_id
                    """, (timestamp_unix, sim_time, sim_time))
                    snapshot_id = cur.fetchone()[0]
                    snapshot_count += 1

                    # Insert AP metrics
                    ap_metrics = data.get("ap_metrics", {})
                    for bssid, ap_data in ap_metrics.items():
                        node_id = ap_data.get("node_id", 0)
                        channel = ap_data.get("channel", 1)
                        band = ap_data.get("band", "BAND_2_4GHZ")
                        channel_util = ap_data.get("channel_utilization", 0)
                        client_count = ap_data.get("associated_clients", 0)
                        tx_power = int(ap_data.get("tx_power_dbm", 20))
                        throughput = ap_data.get("throughput_mbps", 0)
                        bytes_sent = ap_data.get("bytes_sent", 0)
                        bytes_received = ap_data.get("bytes_received", 0)
                        non_wifi_util = ap_data.get("non_wifi_channel_utilization", 0)

                        cur.execute("""
                            INSERT INTO ap_metrics (
                                snapshot_id, node_id, bssid, channel, channel_width, band,
                                channel_utilization, client_count, phy_tx_power_level,
                                throughput, bytes_sent, bytes_received, interference
                            ) VALUES (%s, %s, %s::macaddr, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
                        """, (
                            snapshot_id, node_id, bssid, channel, 20, band,
                            channel_util, client_count, tx_power,
                            throughput, bytes_sent, bytes_received, non_wifi_util
                        ))
                        ap_count += 1

                        # Insert STA metrics from connection_metrics
                        connection_metrics = ap_data.get("connection_metrics", {})
                        for conn_key, sta_data in connection_metrics.items():
                            sta_mac = sta_data.get("sta_address", "")
                            sta_node_id = sta_data.get("node_id")
                            latency = sta_data.get("mean_rtt_latency", 0)
                            jitter = sta_data.get("jitter_ms", 0)
                            packet_loss = sta_data.get("packet_loss_rate", 0)
                            mac_retry = sta_data.get("mac_retry_rate", 0)
                            ap_rssi = sta_data.get("ap_view_rssi", -70)
                            ap_snr = sta_data.get("ap_view_snr", 20)
                            sta_rssi = sta_data.get("sta_view_rssi", -70)
                            sta_snr = sta_data.get("sta_view_snr", 20)
                            ul_throughput = sta_data.get("uplink_throughput_mbps", 0)
                            dl_throughput = sta_data.get("downlink_throughput_mbps", 0)
                            packet_count = sta_data.get("packet_count", 0)
                            last_update = sta_data.get("last_update_seconds", sim_time)

                            cur.execute("""
                                INSERT INTO sta_metrics (
                                    snapshot_id, node_id, mac_address, ap_bssid,
                                    latency_ms, jitter_ms, packet_loss_rate, mac_retry_rate,
                                    ap_view_rssi, ap_view_snr, sta_view_rssi, sta_view_snr,
                                    uplink_throughput_mbps, downlink_throughput_mbps,
                                    packet_count, last_update_seconds
                                ) VALUES (%s, %s, %s::macaddr, %s::macaddr, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
                            """, (
                                snapshot_id, sta_node_id, sta_mac, bssid,
                                latency, jitter, packet_loss, mac_retry,
                                ap_rssi, ap_snr, sta_rssi, sta_snr,
                                ul_throughput, dl_throughput,
                                packet_count, last_update
                            ))
                            sta_count += 1

                conn.commit()

            except json.JSONDecodeError:
                continue
            except Exception as e:
                print(f"Error processing line: {e}")
                conn.rollback()
                continue

    print(f"Imported {snapshot_count} snapshots, {ap_count} AP metrics, {sta_count} STA metrics")


def compute_network_score(conn, sim_time):
    """Compute network score from metrics at given sim_time using RL formula."""
    with conn.cursor() as cur:
        # Find closest snapshot
        cur.execute("""
            SELECT snapshot_id FROM snapshots
            WHERE sim_time_seconds <= %s
            ORDER BY sim_time_seconds DESC LIMIT 1
        """, (sim_time,))
        result = cur.fetchone()
        if not result:
            return None
        snapshot_id = result[0]

        # Get average metrics from sta_metrics
        cur.execute("""
            SELECT
                AVG(uplink_throughput_mbps + downlink_throughput_mbps) as avg_throughput,
                AVG(packet_loss_rate) as avg_loss,
                AVG(latency_ms) as avg_latency,
                AVG(jitter_ms) as avg_jitter
            FROM sta_metrics
            WHERE snapshot_id = %s
        """, (snapshot_id,))
        metrics = cur.fetchone()

        if not metrics or metrics[0] is None:
            return None

        avg_throughput, avg_loss, avg_latency, avg_jitter = metrics

        # Normalize metrics (same formula as RL agent)
        norm_throughput = min(avg_throughput / 100.0, 1.0) if avg_throughput else 0
        norm_loss = 1.0 - min(avg_loss, 1.0) if avg_loss is not None else 1.0
        norm_latency = max(0, 1.0 - (avg_latency / 750.0)) if avg_latency else 1.0
        norm_jitter = max(0, 1.0 - (avg_jitter / 30.0)) if avg_jitter else 1.0

        # Weighted score
        network_score = (
            0.4 * norm_throughput +
            0.3 * norm_loss +
            0.2 * norm_latency +
            0.1 * norm_jitter
        ) * 100  # Scale to 0-100

        return round(network_score, 2)


def import_simulator_events(conn, filepath):
    """Import simulator_events log file into fast_loop_updates and fast_loop_changes tables."""
    print(f"Importing simulator_events (fast loop) from {filepath}...")

    update_count = 0
    change_count = 0

    with open(filepath, 'r') as f:
        for line in f:
            try:
                record = json.loads(line.strip())
                data = record.get("data", {})

                batch_sim_time = data.get("batch_sim_time_sec", 0)
                if batch_sim_time > MAX_SIM_TIME:
                    continue

                simulation_id = data.get("simulation_id", "replay-sim")
                batch_timestamp = data.get("batch_timestamp_unix", 0)
                events = data.get("events", [])

                # Filter for channel_switch and power_switch events
                config_events = [e for e in events if e.get("event_type") in ("channel_switch", "power_switch")]

                if not config_events:
                    continue

                with conn.cursor() as cur:
                    # Compute network score for this batch
                    network_score = compute_network_score(conn, batch_sim_time)

                    # Insert fast_loop_update
                    cur.execute("""
                        INSERT INTO fast_loop_updates (
                            simulation_id, batch_timestamp_unix, batch_sim_time_sec,
                            network_score, status
                        ) VALUES (%s, %s, %s, %s, %s)
                        RETURNING fast_loop_update_id
                    """, (
                        simulation_id, batch_timestamp, batch_sim_time,
                        network_score, 'accepted'
                    ))
                    update_id = cur.fetchone()[0]
                    update_count += 1

                    # Insert individual changes
                    for event in config_events:
                        event_id = event.get("event_id", "")
                        event_type = event.get("event_type", "")
                        sim_timestamp = event.get("sim_timestamp_sec", batch_sim_time)
                        node_id = event.get("primary_node_id", 0)
                        event_data = event.get("data", {})

                        bssid = NODE_TO_BSSID.get(node_id, f"00:00:00:00:00:0{node_id+1}")

                        old_channel = None
                        new_channel = None
                        old_power = None
                        new_power = None
                        reason = event_data.get("reason", "")

                        if event_type == "channel_switch":
                            old_channel = int(event_data.get("old_channel", 0)) if event_data.get("old_channel") else None
                            new_channel = int(event_data.get("new_channel", 0)) if event_data.get("new_channel") else None
                        elif event_type == "power_switch":
                            old_power = float(event_data.get("old_power_dbm", 0)) if event_data.get("old_power_dbm") else None
                            new_power = float(event_data.get("new_power_dbm", 0)) if event_data.get("new_power_dbm") else None

                        cur.execute("""
                            INSERT INTO fast_loop_changes (
                                fast_loop_update_id, event_id, event_type, sim_timestamp_sec,
                                ap_node_id, bssid, old_channel, new_channel,
                                old_tx_power, new_tx_power, reason
                            ) VALUES (%s, %s, %s, %s, %s, %s::macaddr, %s, %s, %s, %s, %s)
                        """, (
                            update_id, event_id, event_type, sim_timestamp,
                            node_id, bssid, old_channel, new_channel,
                            old_power, new_power, reason
                        ))
                        change_count += 1

                conn.commit()

            except json.JSONDecodeError:
                continue
            except Exception as e:
                print(f"Error processing simulator event: {e}")
                conn.rollback()
                continue

    print(f"Imported {update_count} fast loop updates, {change_count} fast loop changes")


def import_rl_events(conn, filepath):
    """Import rl_events log file into slow_loop_updates and slow_loop_changes tables."""
    print(f"Importing rl_events (slow loop) from {filepath}...")

    update_count = 0
    change_count = 0

    # Track CONFIG_SENT events to pair with OBJECTIVE_CALCULATION
    config_sent_by_step = {}

    with open(filepath, 'r') as f:
        lines = f.readlines()

    # First pass: collect all events
    all_events = []
    for line in lines:
        try:
            record = json.loads(line.strip())
            data = record.get("data", {})
            all_events.append(data)
        except:
            continue

    # Process CONFIG_SENT events first
    for data in all_events:
        if data.get("event_type") == "CONFIG_SENT":
            step = data.get("step", 0)
            config_sent_by_step[step] = data

    # Process OBJECTIVE_CALCULATION events
    for data in all_events:
        if data.get("event_type") != "OBJECTIVE_CALCULATION":
            continue

        sim_time_start = data.get("sim_time_start", 0)
        sim_time_end = data.get("sim_time_end", 0)

        if sim_time_end > MAX_SIM_TIME:
            continue

        simulation_id = data.get("simulation_id", "replay-sim")
        step = data.get("step", 0)
        global_objective = data.get("global_objective", 0)

        with conn.cursor() as cur:
            # Insert slow_loop_update
            cur.execute("""
                INSERT INTO slow_loop_updates (
                    simulation_id, step, sim_time_start, sim_time_end,
                    global_objective, status
                ) VALUES (%s, %s, %s, %s, %s, %s)
                RETURNING slow_loop_update_id
            """, (
                simulation_id, step, sim_time_start, sim_time_end,
                global_objective, 'accepted'
            ))
            update_id = cur.fetchone()[0]
            update_count += 1

            # Get corresponding CONFIG_SENT for this step
            config_data = config_sent_by_step.get(step, {})
            if config_data:
                target_ap = config_data.get("target_ap", {})
                action = config_data.get("action", {})

                bssid = target_ap.get("bssid", "00:00:00:00:00:01")
                proposed_channel = action.get("channel_number")
                proposed_width = action.get("channel_width_mhz")
                proposed_power = action.get("power_dbm")

                cur.execute("""
                    INSERT INTO slow_loop_changes (
                        slow_loop_update_id, bssid,
                        proposed_channel, proposed_channel_width, proposed_power_dbm
                    ) VALUES (%s, %s::macaddr, %s, %s, %s)
                """, (
                    update_id, bssid,
                    proposed_channel, proposed_width, proposed_power
                ))
                change_count += 1

        conn.commit()

    print(f"Imported {update_count} slow loop updates, {change_count} slow loop changes")


def find_latest_log(pattern):
    """Find the most recent log file matching the pattern in kafka-logs directory."""
    search_path = os.path.join(KAFKA_LOGS_DIR, pattern)
    files = glob.glob(search_path)
    if not files:
        return None
    # Sort by modification time, get the latest
    return max(files, key=os.path.getmtime)


def auto_detect_logs():
    """Auto-detect log files from kafka-logs directory."""
    metrics = find_latest_log("ns3_metrics_*.json")
    events = find_latest_log("simulator_events_*.json")
    rl = find_latest_log("rl_events_*.json")
    return metrics, events, rl


def main():
    parser = argparse.ArgumentParser(description="Import Kafka logs into replay database")
    parser.add_argument("--metrics", help="Path to ns3_metrics JSON log file (auto-detected if not provided)")
    parser.add_argument("--events", help="Path to simulator_events JSON log file (auto-detected if not provided)")
    parser.add_argument("--rl", help="Path to rl_events JSON log file (auto-detected if not provided)")
    parser.add_argument("--dbname", default="ns3_e2e_test", help="Database name")
    parser.add_argument("--no-clear", action="store_true", help="Don't clear existing data")

    args = parser.parse_args()

    # Auto-detect files if not provided
    if not args.metrics or not args.events or not args.rl:
        print("Auto-detecting log files from kafka-logs/...")
        auto_metrics, auto_events, auto_rl = auto_detect_logs()

        if not args.metrics:
            args.metrics = auto_metrics
        if not args.events:
            args.events = auto_events
        if not args.rl:
            args.rl = auto_rl

    # Validate all files exist
    missing = []
    if not args.metrics or not os.path.exists(args.metrics):
        missing.append("metrics (ns3_metrics_*.json)")
    if not args.events or not os.path.exists(args.events):
        missing.append("events (simulator_events_*.json)")
    if not args.rl or not os.path.exists(args.rl):
        missing.append("rl (rl_events_*.json)")

    if missing:
        print(f"ERROR: Could not find log files: {', '.join(missing)}")
        print(f"Looked in: {os.path.abspath(KAFKA_LOGS_DIR)}")
        print("Please provide paths manually with --metrics, --events, --rl")
        return

    print(f"Using log files:")
    print(f"  Metrics: {args.metrics}")
    print(f"  Events:  {args.events}")
    print(f"  RL:      {args.rl}")

    print(f"Connecting to database: {args.dbname}")
    conn = get_db_connection(args.dbname)

    try:
        if not args.no_clear:
            clear_database(conn)

        # Import in order: metrics first (for snapshots), then events
        import_ns3_metrics(conn, args.metrics)
        import_simulator_events(conn, args.events)
        import_rl_events(conn, args.rl)

        print("\nImport complete!")

        # Print summary
        with conn.cursor() as cur:
            cur.execute("SELECT COUNT(*) FROM snapshots")
            print(f"Total snapshots: {cur.fetchone()[0]}")
            cur.execute("SELECT MIN(sim_time_seconds), MAX(sim_time_seconds) FROM snapshots")
            min_t, max_t = cur.fetchone()
            print(f"Time range: {min_t}s - {max_t}s")
            cur.execute("SELECT COUNT(*) FROM fast_loop_updates")
            print(f"Fast loop updates: {cur.fetchone()[0]}")
            cur.execute("SELECT COUNT(*) FROM slow_loop_updates")
            print(f"Slow loop updates: {cur.fetchone()[0]}")

    finally:
        conn.close()


if __name__ == "__main__":
    main()
