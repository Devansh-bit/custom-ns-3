from datetime import datetime, timezone
from ws_manager import ws_manager
import psycopg
import psycopg.adapt
from psycopg.rows import dict_row


METRIC_RANGES_CONFIG = {
    "latency": {
        "column": "latency_ms",
        "original_column": "latency_ms",
        "ranges": [(0, 10), (10, 20), (20, 30), (30, 40), (40, 50)],
        "over": 50,
        "label": "Latency (ms)",
    },
    "packet_loss_rate": {
        "column": "packet_loss_rate",
        "original_column": "packet_loss_rate",
        "ranges": [(0, 10), (10, 20), (20, 30), (30, 40), (40, 50)],
        "over": 50,
        "label": "Packet Loss Rate (%)",
    },
    "rssi": {
        "column": "-1 * ap_view_rssi",  # Include -1 directly here
        "original_column": "ap_view_rssi",  # Add original column for NULL check
        "ranges": [(30, 40), (40, 50), (50, 60), (60, 70), (70, 80)],
        "over": 80,
        "under": 30,
        "label": "RSSI (-dBm)",
    },
    "snr": {
        "column": "ap_view_snr",
        "original_column": "ap_view_snr",
        "ranges": [(0, 10), (10, 20), (20, 30), (30, 40), (40, 50)],
        "over": 50,
        "label": "SNR (dB)",
    },
    "jitter": {
        "column": "jitter_ms",
        "original_column": "jitter_ms",
        "ranges": [(0, 10), (10, 20), (20, 30), (30, 40), (40, 50)],
        "over": 50,
        "label": "Jitter (ms)",
    },
}


def _iso(ts_unix: int | None) -> str:
    return datetime.fromtimestamp(ts_unix or 0, tz=timezone.utc).isoformat()


class _PostgresManager:
    def __init__(self, host, dbname, password, user="postgres", port=5432):
        self.host = host
        self.dbname = dbname
        self.conn = None
        try:
            # Connect to your PostgreSQL database
            self.conn = psycopg.connect(
                dbname=dbname,
                user=user,
                password=password,
                host=host,
                port=port,
                row_factory=dict_row,
            )

        except (Exception, psycopg.Error) as error:
            print(f"Error connecting to or working with PostgreSQL: {error}")

    def __del__(self):
        # Close the connection
        if hasattr(self, "conn") and self.conn:
            self.conn.close()
            print("PostgreSQL connection closed.")

    def latest_snapshot(self, cur, at_time: float = None):
        """
        Returns {"snapshot_id": int, "snapshot_id_unix": int, "sim_time_seconds": float} for the snapshot.
        If at_time is provided, returns the snapshot at or before that time.
        If at_time is None, returns the most recent snapshot.
        Returns None if the table is empty.
        """
        if at_time is not None:
            cur.execute(
                """
                SELECT snapshot_id, snapshot_id_unix, sim_time_seconds
                FROM snapshots
                WHERE sim_time_seconds <= %s
                ORDER BY sim_time_seconds DESC
                LIMIT 1
                """,
                (at_time,)
            )
        else:
            cur.execute(
                """
                SELECT snapshot_id, snapshot_id_unix, sim_time_seconds FROM snapshots ORDER BY snapshot_id_unix DESC LIMIT 1
                """
            )
        return cur.fetchone()

    def get_time_range(self):
        """Get the min and max simulation time in the database."""
        with self.conn.cursor() as cur:
            cur.execute(
                """
                SELECT MIN(sim_time_seconds) as min_time, MAX(sim_time_seconds) as max_time FROM snapshots
                """
            )
            result = cur.fetchone()
            if result:
                return {"min_time": result["min_time"] or 10.0, "max_time": result["max_time"] or 1800.0}
            return {"min_time": 10.0, "max_time": 1800.0}

    def _latest_sensing_log(self, cur):
        """
        Returns {"sensing_log_id": int, "timestamp_unix": int} for the most recent sensing log,
        or None if the table is empty.
        """
        cur.execute(
            """
            SELECT sensing_log_id, timestamp_unix
            FROM sensing_logs
            ORDER BY timestamp_unix DESC
            LIMIT 1
        """
        )
        return cur.fetchone()


class PostgresWriter(_PostgresManager):
    def insert_fast_loop_event(self, data: dict):
        """
        Insert a fast loop update event with its changes into the database.
        Merges channel_switch and power_switch events by node_id into single records.

        Expected data structure:
        {
            "simulation_id": str,
            "batch_timestamp_unix": int,
            "batch_sim_time_sec": float,
            "network_score": float (optional),
            "status": str (optional, defaults to 'accepted'),
            "events": [
                {
                    "event_id": str,
                    "event_type": str,  # 'channel_switch' or 'power_switch'
                    "sim_timestamp_sec": float,
                    "primary_node_id": int,
                    "data": {
                        "old_channel": int (for channel_switch),
                        "new_channel": int (for channel_switch),
                        "old_power_dbm": float (for power_switch),
                        "new_power_dbm": float (for power_switch),
                        "reason": str (optional)
                    },
                    "bssid": str (optional)
                }
            ]
        }
        """
        with self.conn.cursor() as cur:
            # Insert the fast_loop_update parent record
            cur.execute(
                """
                INSERT INTO fast_loop_updates (
                    simulation_id, batch_timestamp_unix, batch_sim_time_sec,
                    network_score, status
                )
                VALUES (%s, %s, %s, %s, %s)
                RETURNING fast_loop_update_id
                """,
                (
                    data.get("simulation_id", "unknown"),
                    data["batch_timestamp_unix"],
                    data["batch_sim_time_sec"],
                    data.get("network_score"),
                    data.get("status", "accepted"),
                ),
            )
            update_id_row = cur.fetchone()
            update_id = update_id_row["fast_loop_update_id"]

            # Group events by node_id to merge channel, power, channel_width, and obss_pd changes
            # Process all RRM event types
            events_by_node = {}
            valid_event_types = ("channel_switch", "power_switch", "channel_width_switch", "obss_pd_switch")
            for event in data.get("events", []):
                event_type = event["event_type"]

                # Skip events that are not RRM changes
                if event_type not in valid_event_types:
                    continue

                node_id = event["primary_node_id"]
                if node_id not in events_by_node:
                    events_by_node[node_id] = {
                        "node_id": node_id,
                        "bssid": event.get("bssid"),
                        "sim_timestamp_sec": event["sim_timestamp_sec"],
                        "event_ids": [],
                        "event_types": [],
                        "old_channel": None,
                        "new_channel": None,
                        "old_channel_width": None,
                        "new_channel_width": None,
                        "old_power": None,
                        "new_power": None,
                        "old_obss_pd": None,
                        "new_obss_pd": None,
                        "reason": None,
                    }

                event_data = event.get("data", {})

                events_by_node[node_id]["event_ids"].append(event.get("event_id", ""))
                events_by_node[node_id]["event_types"].append(event_type)

                if event_type == "channel_switch":
                    events_by_node[node_id]["old_channel"] = event_data.get(
                        "old_channel"
                    )
                    events_by_node[node_id]["new_channel"] = event_data.get(
                        "new_channel"
                    )
                elif event_type == "power_switch":
                    events_by_node[node_id]["old_power"] = event_data.get(
                        "old_power_dbm"
                    )
                    events_by_node[node_id]["new_power"] = event_data.get(
                        "new_power_dbm"
                    )
                elif event_type == "channel_width_switch":
                    events_by_node[node_id]["old_channel_width"] = event_data.get(
                        "old_channel_width"
                    )
                    events_by_node[node_id]["new_channel_width"] = event_data.get(
                        "new_channel_width"
                    )
                elif event_type == "obss_pd_switch":
                    events_by_node[node_id]["old_obss_pd"] = event_data.get(
                        "old_obss_pd"
                    )
                    events_by_node[node_id]["new_obss_pd"] = event_data.get(
                        "new_obss_pd"
                    )

                # Use the most recent reason if available
                if event_data.get("reason"):
                    events_by_node[node_id]["reason"] = event_data.get("reason")

            # Insert merged records
            for node_data in events_by_node.values():
                # Determine the composite event type
                event_types = sorted(set(node_data["event_types"]))
                if len(event_types) > 1:
                    # Both channel and power switch
                    composite_event_type = (
                        "channel_switch"  # Use channel_switch as primary
                    )
                else:
                    composite_event_type = event_types[0]

                cur.execute(
                    """
                    INSERT INTO fast_loop_changes (
                        fast_loop_update_id, event_id, event_type, sim_timestamp_sec,
                        ap_node_id, bssid,
                        old_channel, new_channel,
                        old_channel_width, new_channel_width,
                        old_tx_power, new_tx_power,
                        old_obss_pd, new_obss_pd,
                        reason
                    )
                    VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
                    """,
                    (
                        update_id,
                        ",".join(node_data["event_ids"]),
                        composite_event_type,
                        node_data["sim_timestamp_sec"],
                        node_data["node_id"],
                        node_data["bssid"],
                        node_data["old_channel"],
                        node_data["new_channel"],
                        node_data["old_channel_width"],
                        node_data["new_channel_width"],
                        node_data["old_power"],
                        node_data["new_power"],
                        node_data["old_obss_pd"],
                        node_data["new_obss_pd"],
                        node_data["reason"],
                    ),
                )

        self.conn.commit()

    def insert_rl_event(self, data: dict):
        """
        Insert an RL event (slow loop update) into the database.

        Expected data structure for OBJECTIVE_CALCULATION:
        {
            "event_type": "OBJECTIVE_CALCULATION",
            "simulation_id": str,
            "step": int,
            "sim_time_start": float,
            "sim_time_end": float,
            "global_objective": float,
            "baseline_objective": float (optional),
            "best_objective": float (optional),
            "delta_reward": float,
            "pct_improvement_vs_baseline": float (optional),
            "num_aps": int,
            "per_ap_objectives": dict (optional),
            "avg_metrics": dict (optional)
        }

        Expected data structure for CONFIG_SENT:
        {
            "event_type": "CONFIG_SENT",
            "simulation_id": str,
            "step": int,
            "sim_time_start": float,
            "sim_time_end": float,
            "target_ap": {
                "bssid": str,
                "selection_reason": str,
                "graph_color": int
            },
            "action": {
                "channel_number": int,
                "channel_width_mhz": int,
                "power_dbm": float
            }
        }
        """
        event_type = data.get("event_type")

        with self.conn.cursor() as cur:
            if event_type == "OBJECTIVE_CALCULATION":
                # Insert a slow_loop_update record for the objective calculation
                cur.execute(
                    """
                    INSERT INTO slow_loop_updates (
                        simulation_id, step, sim_time_start, sim_time_end,
                        global_objective, status
                    )
                    VALUES (%s, %s, %s, %s, %s, %s)
                    RETURNING slow_loop_update_id
                    """,
                    (
                        data.get("simulation_id", "unknown"),
                        data.get("step", 0),
                        data.get("sim_time_start", 0.0),
                        data.get("sim_time_end", 0.0),
                        data.get("global_objective"),
                        "accepted",  # Objective calculations are always accepted
                    ),
                )

            elif event_type == "CONFIG_SENT":
                # First check if there's an existing slow_loop_update for this step
                step = data.get("step", 0)
                simulation_id = data.get("simulation_id", "unknown")

                cur.execute(
                    """
                    SELECT slow_loop_update_id FROM slow_loop_updates
                    WHERE simulation_id = %s AND step = %s
                    ORDER BY created_at DESC
                    LIMIT 1
                    """,
                    (simulation_id, step),
                )
                row = cur.fetchone()

                if row:
                    update_id = row["slow_loop_update_id"]
                else:
                    # Create a new slow_loop_update if none exists for this step
                    cur.execute(
                        """
                        INSERT INTO slow_loop_updates (
                            simulation_id, step, sim_time_start, sim_time_end,
                            global_objective, status
                        )
                        VALUES (%s, %s, %s, %s, %s, %s)
                        RETURNING slow_loop_update_id
                        """,
                        (
                            simulation_id,
                            step,
                            data.get("sim_time_start", 0.0),
                            data.get("sim_time_end", 0.0),
                            None,  # No objective score for config-only update
                            "proposed",
                        ),
                    )
                    update_id = cur.fetchone()["slow_loop_update_id"]

                # Insert the config change
                target_ap = data.get("target_ap", {})
                action = data.get("action", {})

                cur.execute(
                    """
                    INSERT INTO slow_loop_changes (
                        slow_loop_update_id, bssid,
                        proposed_channel, proposed_channel_width, proposed_power_dbm
                    )
                    VALUES (%s, %s::macaddr, %s, %s, %s)
                    """,
                    (
                        update_id,
                        target_ap.get("bssid"),
                        action.get("channel_number"),
                        action.get("channel_width_mhz"),
                        action.get("power_dbm"),
                    ),
                )

        self.conn.commit()

    def _get_last_update_seconds(self, data: dict) -> float:
        max_update = 0.0
        if not data:
            return 0.0

        for ap in data.get("ap_metrics", {}).values():
            conn_metrics = ap.get("connection_metrics") or {}
            for cm in conn_metrics.values():
                if not isinstance(cm, dict):
                    continue
                v = cm.get("last_update_seconds", 0)
                max_update = max(max_update, float(v))

        return float(max_update)

    def insert_snapshot_data(self, data: dict):
        with self.conn.cursor() as cur:
            last_updated_seconds = self._get_last_update_seconds(data)

            cur.execute(
                """
                INSERT INTO snapshots (snapshot_id_unix, sim_time_seconds, last_update_seconds)
                VALUES (%s, %s, %s)
                RETURNING snapshot_id
            """,
                (
                    data["timestamp_unix"],
                    data["sim_time_seconds"],
                    last_updated_seconds,
                ),
            )
            snapshot_id = cur.fetchone()

            for bssid, ap in data.get("ap_metrics", {}).items():
                cur.execute(
                    """
                    INSERT INTO ap_metrics (
                        snapshot_id, node_id, bssid, channel, channel_width, band,
                        phy_tx_power_level, channel_utilization,
                        client_count,
                        throughput, bytes_sent, bytes_received, interference
                    )
                    VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
                """,
                    (
                        snapshot_id["snapshot_id"],
                        ap["node_id"],
                        bssid,
                        ap["channel"],
                        ap.get("channel_width", 20),  # Channel width in MHz (default 20)
                        ap["band"],
                        20,  # default phy_tx_power
                        ap.get("channel_utilization"),
                        ap.get("associated_clients"),
                        ap.get("throughput_mbps"),
                        ap.get("bytes_sent"),
                        ap.get("bytes_received"),
                        ap.get("non_wifi_channel_utilization"),
                    ),
                )

                # Insert per-channel metrics for this AP
                for channel_num, channel_data in ap.get(
                    "scanning_channel_data", {}
                ).items():
                    if isinstance(channel_data, dict):
                        cur.execute(
                            """
                            INSERT INTO ap_channel_metrics (
                                snapshot_id, node_id, channel, non_wifi_channel_utilization
                            )
                            VALUES (%s, %s, %s, %s)
                            """,
                            (
                                snapshot_id["snapshot_id"],
                                ap["node_id"],
                                int(channel_num),
                                channel_data.get("non_wifi_channel_utilization"),
                            ),
                        )

                for i, con_metrics in enumerate(
                    ap.get("connection_metrics", {}).values()
                ):
                    sta_mac = con_metrics["sta_address"]
                    current_bssid = con_metrics["ap_address"]

                    cur.execute(
                        """
                           SELECT ap_bssid FROM sta_metrics
                           LEFT JOIN snapshots ON sta_metrics.snapshot_id = snapshots.snapshot_id
                           WHERE sta_metrics.mac_address = %s
                           ORDER BY snapshots.snapshot_id_unix DESC
                           LIMIT 1
                       """,
                        (sta_mac,),
                    )
                    previous_sta = cur.fetchone()

                    if previous_sta and previous_sta["ap_bssid"] != current_bssid:
                        cur.execute(
                            """
                            INSERT INTO roaming_history (
                                snapshot_id, sta_id, from_bssid, to_bssid
                            )
                            VALUES (%s, %s, %s, %s)
                        """,
                            (
                                snapshot_id["snapshot_id"],
                                sta_mac,
                                previous_sta["ap_bssid"],
                                current_bssid,
                            ),
                        )
                        ws_manager.broadcast(
                            {
                                "id": f"roam-{i}",
                                "timestamp": data["timestamp_unix"] * 1000,
                                "message": f"Client {sta_mac} roamed from {previous_sta['ap_bssid']} to {current_bssid}",
                                "type": "info",
                                "source": "Roaming",
                            }
                        )

                    cur.execute(
                        """
                        INSERT INTO sta_metrics (
                            snapshot_id, node_id, packet_loss_rate,
                            mac_address, ap_bssid, latency_ms, jitter_ms,
                            uplink_throughput_mbps, downlink_throughput_mbps,
                            ap_view_rssi, ap_view_snr,
                            sta_view_rssi, sta_view_snr,
                            mac_retry_rate, last_update_seconds, packet_count
                        )
                        VALUES (
                            %s, %s, %s,
                            %s, %s, %s, %s,
                            %s, %s,
                            %s, %s,
                            %s, %s,
                            %s, %s, %s
                        )
                    """,
                        (
                            snapshot_id["snapshot_id"],
                            con_metrics.get("node_id"),
                            con_metrics.get("packet_loss_rate", 0),
                            sta_mac,
                            current_bssid,
                            con_metrics.get("mean_rtt_latency", 0),
                            con_metrics.get("jitter_ms", 0),
                            con_metrics.get("uplink_throughput_mbps", 0),
                            con_metrics.get("downlink_throughput_mbps", 0),
                            con_metrics.get("ap_view_rssi", 0),
                            con_metrics.get("ap_view_snr", 0),
                            con_metrics.get("sta_view_rssi", 0),
                            con_metrics.get("sta_view_snr", 0),
                            con_metrics.get("mac_retry_rate", 0),
                            con_metrics.get("last_update_seconds", 0),
                            con_metrics.get("packet_count", 0),
                        ),
                    )

        self.conn.commit()

    def _insert_log(self, table_name: str, data: dict):
        with self.conn.cursor() as cur:
            cur.execute(
                f"""
                INSERT INTO {table_name} (timestamp_unix, sim_time_seconds, log_message)
                VALUES (%s, %s, %s)
                """,
                (
                    data["timestamp_unix"],
                    data["sim_time_seconds"],
                    data["log_message"],
                ),
            )
        self.conn.commit()

    def insert_simulator_log(self, data: dict):
        self._insert_log("simulator_logs", data)

    def insert_optimizer_log(self, data: dict):
        self._insert_log("optimizer_logs", data)


class APQuerier(_PostgresManager):
    def get_last_aps(self, at_time: float = None):
        with self.conn.cursor() as cur:
            row = self.latest_snapshot(cur, at_time)

            if not row:
                return []

            last_snapshot_id = row["snapshot_id"]

            cur.execute(
                """
            SELECT DISTINCT bssid
            FROM ap_metrics
            WHERE snapshot_id = %s
            ORDER BY bssid
            """,
                (last_snapshot_id,),
            )
            return cur.fetchall()

    def get_channel_utilization_history(self, bssid: str, timeResolution: int = 10, at_time: float = None):
        with self.conn.cursor() as cur:
            snapshot = self.latest_snapshot(cur, at_time)

            if not snapshot:
                return []

            # Use at_time as the end point if provided, otherwise use latest
            end_time = at_time if at_time is not None else snapshot["sim_time_seconds"]
            time_threshold = end_time - (timeResolution * 60)

            cur.execute(
                """
                SELECT
                    snapshots.sim_time_seconds,
                    ap_metrics.channel_utilization AS channel_utilization
                FROM ap_metrics
                LEFT JOIN snapshots ON
                ap_metrics.snapshot_id = snapshots.snapshot_id
                WHERE ap_metrics.bssid = %s::macaddr
                AND snapshots.sim_time_seconds >= %s
                AND snapshots.sim_time_seconds <= %s
                ORDER BY snapshots.sim_time_seconds ASC
                """,
                (bssid, time_threshold, end_time),
            )
            return cur.fetchall()

    def ap_metrics(self, bssid: str, limit: int | None = None, at_time: float = None):
        with self.conn.cursor() as cur:
            snapshot = self.latest_snapshot(cur, at_time)

            if not snapshot:
                return {
                    "count_clients": 0,
                    "uplink_throughput_mbps": None,
                    "downlink_throughput_mbps": None,
                    "channel": None,
                    "bytes_sent": None,
                    "phy_tx_power_level": None,
                    "interference": None,
                    "last_update_seconds": 0,
                    "channel_width": 20,
                }

            cur.execute(
                """
            SELECT count(*) FROM sta_metrics
            LEFT JOIN snapshots ON
            sta_metrics.snapshot_id = snapshots.snapshot_id
            WHERE sta_metrics.ap_bssid = %s::macaddr AND snapshots.snapshot_id = %s
            """,
                (bssid, snapshot["snapshot_id"]),
            )
            count_clients = cur.fetchone()

            cur.execute(
                """
            SELECT throughput, channel, channel_width, phy_tx_power_level, interference
            FROM ap_metrics
            LEFT JOIN snapshots ON
            ap_metrics.snapshot_id = snapshots.snapshot_id
            WHERE ap_metrics.bssid = %s::macaddr AND snapshots.snapshot_id = %s
            """,
                (bssid, snapshot["snapshot_id"]),
            )
            ap_data = cur.fetchone()

            cur.execute(
                """
            SELECT SUM(bytes_sent) AS total_bytes_sent
            FROM ap_metrics
            WHERE ap_metrics.bssid = %s::macaddr
            GROUP BY bssid
            """,
                (bssid,),
            )
            bytes_sent_row = cur.fetchone()
            bytes_sent = bytes_sent_row["total_bytes_sent"] if bytes_sent_row else None

            # Calculate last_update_seconds:
            # Always show the simulation time of the snapshot being displayed
            # This represents the actual time in the simulation/replay that the data corresponds to
            last_update_seconds = snapshot["sim_time_seconds"]

            return {
                "count_clients": count_clients["count"] if count_clients else 0,
                "uplink_throughput_mbps": ap_data["throughput"] if ap_data else None,
                "downlink_throughput_mbps": ap_data["throughput"] if ap_data else None,
                "channel": ap_data["channel"] if ap_data else None,
                "bytes_sent": bytes_sent,
                "phy_tx_power_level": ap_data["phy_tx_power_level"]
                if ap_data
                else None,
                "interference": ap_data["interference"]
                if ap_data and ap_data.get("interference") is not None
                else None,
                "last_update_seconds": last_update_seconds,
                "channel_width": ap_data["channel_width"] if ap_data else 20,
            }

    def get_ap_ids(self):
        query = """
        SELECT DISTINCT node_id
        FROM ap_metrics
        ORDER BY node_id;
        """
        try:
            with self.conn.cursor() as cur:
                cur.execute(query)
                results = cur.fetchall()
                ap_ids = [row["node_id"] for row in results]
                return ap_ids
        except Exception as e:
            print(f"Error fetching AP IDs: {e}")
            return []


class STAQuerier(_PostgresManager):
    def get_connected_clients(self, bssid: str, at_time: float = None):
        with self.conn.cursor() as cur:
            snapshot = self.latest_snapshot(cur, at_time)

            if not snapshot:
                return []

            cur.execute(
                """
                SELECT DISTINCT
                    sta_metrics.mac_address,
                    sta_metrics.node_id,
                    sta_metrics.ap_view_rssi,
                    sta_metrics.ap_view_snr,
                    sta_metrics.latency_ms,
                    sta_metrics.jitter_ms,
                    sta_metrics.packet_loss_rate
                FROM sta_metrics
                LEFT JOIN snapshots ON
                sta_metrics.snapshot_id = snapshots.snapshot_id
                WHERE sta_metrics.ap_bssid = %s::macaddr
                AND snapshots.snapshot_id = %s
                ORDER BY sta_metrics.mac_address
                """,
                (bssid, snapshot["snapshot_id"]),
            )
            return cur.fetchall()

    def get_sta_metrics_time_series(
        self,
        metric_type: str,
        client_mac_address: str = None,
        timeResolution: int = 10,
    ):
        with self.conn.cursor() as cur:
            latest_snapshot = self.latest_snapshot(cur)

            if not latest_snapshot:
                return []

            metric_column_map = {
                "latency": "latency_ms",
                "jitter": "jitter_ms",
                "packet_loss": "packet_loss_rate",
                "rssi": "ap_view_rssi",
                "snr": "ap_view_snr",
            }

            if metric_type not in metric_column_map:
                raise ValueError(
                    f"Invalid metric_type. Must be one of: {
                        list(metric_column_map.keys())
                    }"
                )

            metric_column = metric_column_map[metric_type]

            time_threshold = latest_snapshot["sim_time_seconds"] - (timeResolution * 60)

            if client_mac_address:
                time_series_query = f"""
                    SELECT DISTINCT
                        snapshots.sim_time_seconds,
                        sta_metrics.{metric_column} as value
                    FROM sta_metrics
                    LEFT JOIN snapshots ON
                    sta_metrics.snapshot_id = snapshots.snapshot_id
                    WHERE sta_metrics.mac_address = %s
                    AND snapshots.snapshot_id_unix >= %s
                    AND sta_metrics.{metric_column} IS NOT NULL
                    ORDER BY snapshots.sim_time_seconds ASC
                """
                cur.execute(time_series_query, (client_mac_address, time_threshold))
            else:
                time_series_query = f"""
                    SELECT
                        snapshots.sim_time_seconds,
                        AVG(sta_metrics.{metric_column}) as avg_value,
                        MIN(sta_metrics.{metric_column}) as min_value,
                        MAX(sta_metrics.{metric_column}) as max_value,
                        COUNT(*) as client_count
                    FROM sta_metrics
                    LEFT JOIN snapshots ON
                    sta_metrics.snapshot_id = snapshots.snapshot_id
                    WHERE snapshots.sim_time_seconds >= %s
                    AND snapshots.sim_time_seconds > 10
                    AND sta_metrics.{metric_column} IS NOT NULL
                    GROUP BY snapshots.sim_time_seconds
                    ORDER BY snapshots.sim_time_seconds ASC
                """
                cur.execute(time_series_query, (time_threshold,))

            return cur.fetchall()

    def get_sta_metrics_time_series_by_client(
        self,
        client_mac_address: str,
        metric_type: str,
        timeResolution: int = 10,
    ):
        with self.conn.cursor() as cur:
            latest_snapshot = self.latest_snapshot(cur)

            if not latest_snapshot:
                return []

            metric_column_map = {
                "latency": "latency_ms",
                "jitter": "jitter_ms",
                "packet_loss": "packet_loss_rate",
                "rssi": "ap_view_rssi",
                "snr": "ap_view_snr",
            }

            if metric_type not in metric_column_map:
                raise ValueError(
                    f"Invalid metric_type. Must be one of: {
                        list(metric_column_map.keys())
                    }"
                )

            metric_column = metric_column_map[metric_type]

            time_threshold = latest_snapshot["sim_time_seconds"] - (timeResolution * 60)

            time_series_query = f"""
                SELECT DISTINCT
                    snapshots.sim_time_seconds,
                    sta_metrics.{metric_column} as value
                FROM sta_metrics
                LEFT JOIN snapshots ON
                sta_metrics.snapshot_id = snapshots.snapshot_id
                WHERE sta_metrics.mac_address = %s
                AND snapshots.sim_time_seconds >= %s
                AND sta_metrics.{metric_column} IS NOT NULL
                ORDER BY snapshots.sim_time_seconds ASC
            """
            cur.execute(time_series_query, (client_mac_address, time_threshold))

            return cur.fetchall()

    def sta_metrics(self, at_time: float = None):
        with self.conn.cursor() as cur:
            snapshot = self.latest_snapshot(cur, at_time)

            if not snapshot:
                return {
                    "aggregate_metrics": {
                        "avg_latency_ms": None,
                        "avg_jitter_ms": None,
                        "avg_packet_loss_rate": None,
                        "total_clients": 0,
                    },
                    "client_metrics": [],
                }

            query = """
                SELECT DISTINCT
                    sta_metrics.mac_address,
                    sta_metrics.ap_bssid,
                    sta_metrics.latency_ms,
                    sta_metrics.jitter_ms,
                    sta_metrics.packet_loss_rate,
                    sta_metrics.ap_view_rssi,
                    sta_metrics.ap_view_snr
                FROM sta_metrics
                WHERE sta_metrics.snapshot_id = %s
            """
            cur.execute(query, (snapshot["snapshot_id"],))
            client_metrics = cur.fetchall()

            if client_metrics:
                total_clients = len(client_metrics)
                avg_latency = (
                    sum(
                        c["latency_ms"]
                        for c in client_metrics
                        if c["latency_ms"] is not None
                    )
                    / total_clients
                    if total_clients > 0
                    else None
                )
                avg_jitter = (
                    sum(
                        c["jitter_ms"]
                        for c in client_metrics
                        if c["jitter_ms"] is not None
                    )
                    / total_clients
                    if total_clients > 0
                    else None
                )
                avg_packet_loss = (
                    sum(
                        c["packet_loss_rate"]
                        for c in client_metrics
                        if c["packet_loss_rate"] is not None
                    )
                    / total_clients
                    if total_clients > 0
                    else None
                )
            else:
                total_clients = 0
                avg_latency = None
                avg_jitter = None
                avg_packet_loss = None

            return {
                "aggregate_metrics": {
                    "avg_latency_ms": avg_latency,
                    "avg_jitter_ms": avg_jitter,
                    "avg_packet_loss_rate": avg_packet_loss,
                    "total_clients": total_clients,
                },
                "client_metrics": client_metrics,
            }

    def get_sta_ids(self):
        cur = self.conn.execute(
            """
            SELECT DISTINCT mac_address
            FROM sta_metrics
            LEFT JOIN snapshots ON
            sta_metrics.snapshot_id = snapshots.snapshot_id
            ORDER BY snapshots.snapshot_id_unix DESC
            """
        )
        return cur.fetchall()

    def get_sta_count_by_band(self):
        """
        Get count of STAs grouped by the band they are using based on their connected AP.

        Returns:
            List of dictionaries containing band and sta_count for the latest snapshot
        """
        with self.conn.cursor() as cur:
            latest_snapshot = self.latest_snapshot(cur)

            if not latest_snapshot:
                return []

            cur.execute(
                """
                SELECT
                    ap_metrics.band,
                    COUNT(sta_metrics.mac_address) as sta_count
                FROM sta_metrics
                LEFT JOIN ap_metrics ON
                    sta_metrics.ap_bssid = ap_metrics.bssid
                    AND sta_metrics.snapshot_id = ap_metrics.snapshot_id
                WHERE sta_metrics.snapshot_id = %s
                GROUP BY ap_metrics.band
                """,
                (latest_snapshot["snapshot_id"],),
            )
            return cur.fetchall()

    def get_sta_count_by_throughput(self):
        """
        Get count of STAs grouped by their throughput usage.

        Categories:
        - low: 0-3 mbps (total throughput)
        - medium: 3-6 mbps
        - high: 6-10 mbps
        - very_high: 10+ mbps

        Returns:
            List of dictionaries containing category and sta_count for the latest snapshot
        """
        with self.conn.cursor() as cur:
            latest_snapshot = self.latest_snapshot(cur)

            if not latest_snapshot:
                return []

            cur.execute(
                """
                WITH categorized AS (
                    SELECT
                        CASE
                            WHEN (COALESCE(uplink_throughput_mbps, 0) + COALESCE(downlink_throughput_mbps, 0)) < 3 THEN 'low'
                            WHEN (COALESCE(uplink_throughput_mbps, 0) + COALESCE(downlink_throughput_mbps, 0)) >= 3
                             AND (COALESCE(uplink_throughput_mbps, 0) + COALESCE(downlink_throughput_mbps, 0)) < 6 THEN 'medium'
                            WHEN (COALESCE(uplink_throughput_mbps, 0) + COALESCE(downlink_throughput_mbps, 0)) >= 6
                             AND (COALESCE(uplink_throughput_mbps, 0) + COALESCE(downlink_throughput_mbps, 0)) < 10 THEN 'high'
                            ELSE 'very_high'
                        END as category
                    FROM sta_metrics
                    WHERE snapshot_id = %s
                )
                SELECT
                    category,
                    COUNT(*) as sta_count
                FROM categorized
                GROUP BY category
                ORDER BY
                    CASE category
                        WHEN 'low' THEN 1
                        WHEN 'medium' THEN 2
                        WHEN 'high' THEN 3
                        WHEN 'very_high' THEN 4
                    END
                """,
                (latest_snapshot["snapshot_id"],),
            )
            return cur.fetchall()

    def get_sta_count_by_rssi(self):
        """
        Get count of STAs grouped by their RSSI (signal strength).

        Categories:
        - good: > -50 dBm
        - fair: -50 to -65 dBm
        - poor: < -65 dBm

        Returns:
            List of dictionaries containing category and sta_count for the latest snapshot
        """
        with self.conn.cursor() as cur:
            latest_snapshot = self.latest_snapshot(cur)

            if not latest_snapshot:
                return []

            cur.execute(
                """
                WITH categorized AS (
                    SELECT
                        CASE
                            WHEN sta_view_rssi > -50 THEN 'good'
                            WHEN sta_view_rssi >= -65 AND sta_view_rssi <= -50 THEN 'fair'
                            ELSE 'poor'
                        END as category
                    FROM sta_metrics
                    WHERE snapshot_id = %s AND sta_view_rssi IS NOT NULL
                )
                SELECT
                    category,
                    COUNT(*) as sta_count
                FROM categorized
                GROUP BY category
                ORDER BY
                    CASE category
                        WHEN 'good' THEN 1
                        WHEN 'fair' THEN 2
                        WHEN 'poor' THEN 3
                    END
                """,
                (latest_snapshot["snapshot_id"],),
            )
            return cur.fetchall()


class NetworkQuerier(_PostgresManager):
    def get_noise_per_channel(self, bssid: str, at_time: float = None):
        """
        Get non-WiFi channel utilization for all scanned channels for a given AP.

        Parameters:
            bssid: The BSSID of the AP to get channel data for
            at_time: Optional simulation time to query at (for replay mode)

        Returns:
            List of dictionaries containing channel number and non-WiFi utilization
        """
        with self.conn.cursor() as cur:
            snapshot = self.latest_snapshot(cur, at_time)

            if not snapshot:
                return []

            cur.execute(
                """
                SELECT
                    acm.channel,
                    acm.non_wifi_channel_utilization
                FROM ap_channel_metrics acm
                JOIN ap_metrics am ON
                    acm.snapshot_id = am.snapshot_id
                    AND acm.node_id = am.node_id
                WHERE am.bssid = %s::macaddr
                    AND acm.snapshot_id = %s
                ORDER BY acm.channel
                """,
                (bssid, snapshot["snapshot_id"]),
            )

            rows = cur.fetchall()

            return [
                {
                    "channel": row["channel"],
                    "non_wifi_channel_utilization": row["non_wifi_channel_utilization"],
                }
                for row in rows
            ]

    def get_client_quality_summary(self):
        thresholds = {
            "audio": {
                "latency_ms": {"good": 25, "okayish": 65},
                "jitter_ms": {"good": 7, "okayish": 30},
                "packet_loss_rate": {"good": 0.3, "okayish": 1},
            },
            "video": {
                "latency_ms": {"good": 25, "okayish": 65},
                "jitter_ms": {"good": 5, "okayish": 10},
                "packet_loss_rate": {"good": 0.05, "okayish": 0.1},
            },
            "transaction": {
                "latency_ms": {"good": 50, "okayish": 80},
                "jitter_ms": {"good": 200, "okayish": 200},
                "packet_loss_rate": {"good": 1, "okayish": 3},
            },
        }

        def classify(value: float | None, application: str, metric: str) -> str:
            if value is None:
                return "unknown"
            metric_thresholds = thresholds[application][metric]
            if value < metric_thresholds["good"]:
                return "good"
            if value < metric_thresholds["okayish"]:
                return "okayish"
            return "bad"

        with self.conn.cursor() as cur:
            latest_snapshot = self.latest_snapshot(cur)

            if not latest_snapshot:
                return {
                    "total_clients": 0,
                    "thresholds": thresholds,
                    "latency": {"good": 0, "okayish": 0, "bad": 0, "unknown": 0},
                    "jitter": {"good": 0, "okayish": 0, "bad": 0, "unknown": 0},
                    "packet_loss": {"good": 0, "okayish": 0, "bad": 0, "unknown": 0},
                }

            cur.execute(
                """
                SELECT
                    latency_ms,
                    jitter_ms,
                    packet_loss_rate
                FROM sta_metrics
                WHERE snapshot_id = %s
                """,
                (latest_snapshot["snapshot_id"],),
            )

            rows = cur.fetchall()

        counts_template = {"good": 0, "okayish": 0, "bad": 0, "unknown": 0}
        data = {
            "audio": {
                "latency": counts_template.copy(),
                "jitter": counts_template.copy(),
                "packet_loss": counts_template.copy(),
                "total": len(rows),
            },
            "video": {
                "latency": counts_template.copy(),
                "jitter": counts_template.copy(),
                "packet_loss": counts_template.copy(),
                "total": len(rows),
            },
            "transaction": {
                "latency": counts_template.copy(),
                "jitter": counts_template.copy(),
                "packet_loss": counts_template.copy(),
                "total": len(rows),
            },
        }

        for row in rows:
            for application in data:
                data[application]["latency"][
                    classify(row["latency_ms"], application, "latency_ms")
                ] += 1
                data[application]["jitter"][
                    classify(row["jitter_ms"], application, "jitter_ms")
                ] += 1
                data[application]["packet_loss"][
                    classify(row["packet_loss_rate"], application, "packet_loss_rate")
                ] += 1

        return data

    def get_client_metrics_distribution(self):
        with self.conn.cursor() as cur:
            latest_snapshot = self.latest_snapshot(cur)

            if not latest_snapshot:
                return {}

            snapshot_id = latest_snapshot["snapshot_id"]

            results = {}
            for metric, config in METRIC_RANGES_CONFIG.items():
                column = config["column"]  # Now includes -1 * for RSSI
                null_check_column = config.get("original_column", column)

                case_parts = []
                range_labels = []

                if "under" in config:
                    label = f"<{config['under']}"
                    range_labels.append(label)
                    case_parts.append(
                        f"WHEN {column} < {config['under']} THEN '{label}'"
                    )

                for start, end in config["ranges"]:
                    label = f"{start}-{end}"
                    range_labels.append(label)
                    case_parts.append(
                        f"WHEN {column} >= {start} AND {column} < {end} THEN '{label}'"
                    )

                if "over" in config:
                    label = f"{config['over']}+"
                    range_labels.append(label)
                    case_parts.append(
                        f"WHEN {column} >= {config['over']} THEN '{label}'"
                    )

                case_statement = " ".join(case_parts)

                query = f"""
                    SELECT
                        CASE {case_statement} ELSE 'Other' END as metric_range,
                        COUNT(*) as client_count
                    FROM sta_metrics
                    WHERE snapshot_id = %s AND {null_check_column} IS NOT NULL
                    GROUP BY metric_range
                """

                cur.execute(query, (snapshot_id,))
                rows = cur.fetchall()

                counts_by_range = {label: 0 for label in range_labels}
                if "Other" not in range_labels:
                    counts_by_range["Other"] = 0

                for row in rows:
                    counts_by_range[row["metric_range"]] = row["client_count"]

                chart_data = [
                    {"range": r, "clients": counts_by_range[r]} for r in range_labels
                ]
                if counts_by_range.get("Other"):
                    chart_data.append(
                        {"range": "Other", "clients": counts_by_range["Other"]}
                    )

                results[metric] = {"label": config["label"], "data": chart_data}

            return results

    # =========================================================================
    # UNIFIED HEALTH SCORE (RL-Aligned) with Reset Mechanism
    # =========================================================================

    # RL-style normalization constants (from RL/main.py)
    MAX_P50_THROUGHPUT = 100.0  # Mbps
    MAX_P75_LOSS_RATE = 1.0  # fraction (0-1), not percentage
    MAX_P75_RTT = 750.0  # ms (latency)
    MAX_P75_JITTER = 30.0  # ms

    # Reset interval (30 minutes in simulation seconds)
    RESET_INTERVAL_SECONDS = 30 * 60  # 1800 seconds

    # Class-level state for reset tracking (shared across instances)
    _last_reset_sim_time = 0.0

    @classmethod
    def reset_health_baseline(cls, sim_time: float = None):
        """
        Reset the health calculation baseline.
        Called automatically every 30 min OR when RL is triggered.

        Args:
            sim_time: Current simulation time. If None, will be fetched.
        """
        if sim_time is not None:
            cls._last_reset_sim_time = sim_time
        else:
            cls._last_reset_sim_time = 0.0  # Will be set on next query

    @staticmethod
    def _compute_ap_objective(
        p50_throughput: float, p75_latency: float, p75_jitter: float, p75_loss: float
    ) -> float:
        """
        Compute AP health score using RL formula.

        Formula (from RL/main.py):
            objective = 0.35 * norm_throughput
                      + 0.10 * (1 - norm_jitter)
                      + 0.35 * (1 - norm_loss_rate)
                      + 0.20 * (1 - norm_rtt)

        Returns:
            Health score 0-100 (higher is better)
        """
        MAX_THROUGHPUT = 100.0
        MAX_LATENCY = 750.0
        MAX_JITTER = 30.0
        MAX_LOSS = 1.0

        # Normalize (clamp to [0, 1])
        norm_throughput = min((p50_throughput or 0) / MAX_THROUGHPUT, 1.0)
        norm_latency = min((p75_latency or 0) / MAX_LATENCY, 1.0)
        norm_jitter = min((p75_jitter or 0) / MAX_JITTER, 1.0)
        norm_loss = min((p75_loss or 0) / MAX_LOSS, 1.0)

        # RL formula
        objective = (
            0.35 * norm_throughput
            + 0.10 * (1 - norm_jitter)
            + 0.35 * (1 - norm_loss)
            + 0.20 * (1 - norm_latency)
        )

        return round(objective * 100, 2)

    def get_health_scores_unified(self, time_resolution_minutes: int = 30, at_time: float = None):
        """
        Calculate AP Health and Network Score together using RL-aligned formula.

        Features:
        - Auto-resets every 30 minutes
        - Uses p50/p75 percentiles like RL
        - Network Score = formula(avg_metrics), NOT avg(ap_scores)

        Args:
            time_resolution_minutes: Max time window in minutes (default 30)
            at_time: If provided, calculate scores up to this time (for replay mode)

        Returns:
            {
                "ap_health": {bssid: score, ...},
                "network_score": float,
                "last_reset_sim_time": float,
                "current_sim_time": float,
                "time_until_reset": float,
                "ap_count": int
            }
        """
        with self.conn.cursor() as cur:
            # For replay mode, use at_time; otherwise use latest snapshot
            if at_time is not None:
                latest_snapshot = self.latest_snapshot(cur, at_time)
            else:
                latest_snapshot = self.latest_snapshot(cur)

            if not latest_snapshot:
                return {
                    "ap_health": {},
                    "network_score": None,
                    "last_reset_sim_time": self._last_reset_sim_time,
                    "current_sim_time": 0,
                    "time_until_reset": self.RESET_INTERVAL_SECONDS,
                    "ap_count": 0,
                }

            current_sim_time = latest_snapshot["sim_time_seconds"]

            # Check if we need auto-reset (30 min passed)
            if self._last_reset_sim_time == 0:
                # First time - initialize baseline
                self._last_reset_sim_time = max(
                    0, current_sim_time - time_resolution_minutes * 60
                )

            time_since_reset = current_sim_time - self._last_reset_sim_time
            if time_since_reset >= self.RESET_INTERVAL_SECONDS:
                # Auto-reset: start fresh window
                self._last_reset_sim_time = current_sim_time
                time_since_reset = 0

            # Query window: from last_reset to now
            time_threshold = self._last_reset_sim_time

            # Get percentile stats per AP using RL-style calculation
            # p50 for throughput, p75 for latency/jitter/loss
            cur.execute(
                """
                SELECT
                    sta_metrics.ap_bssid::text AS bssid,
                    PERCENTILE_CONT(0.5) WITHIN GROUP (
                        ORDER BY COALESCE(sta_metrics.uplink_throughput_mbps, 0) +
                                 COALESCE(sta_metrics.downlink_throughput_mbps, 0)
                    ) AS p50_throughput,
                    PERCENTILE_CONT(0.75) WITHIN GROUP (
                        ORDER BY sta_metrics.latency_ms
                    ) AS p75_latency,
                    PERCENTILE_CONT(0.75) WITHIN GROUP (
                        ORDER BY sta_metrics.jitter_ms
                    ) AS p75_jitter,
                    PERCENTILE_CONT(0.75) WITHIN GROUP (
                        ORDER BY sta_metrics.packet_loss_rate
                    ) AS p75_loss,
                    COUNT(*) AS sample_count
                FROM sta_metrics
                JOIN snapshots ON sta_metrics.snapshot_id = snapshots.snapshot_id
                WHERE snapshots.sim_time_seconds >= %s
                  AND snapshots.sim_time_seconds <= %s
                  AND snapshots.sim_time_seconds > 10
                  AND sta_metrics.ap_bssid IS NOT NULL
                GROUP BY sta_metrics.ap_bssid
                HAVING COUNT(*) >= 1
                """,
                (time_threshold, current_sim_time),
            )

            ap_stats = cur.fetchall()

            if not ap_stats:
                return {
                    "ap_health": {},
                    "network_score": None,
                    "last_reset_sim_time": self._last_reset_sim_time,
                    "current_sim_time": current_sim_time,
                    "time_until_reset": self.RESET_INTERVAL_SECONDS - time_since_reset,
                    "ap_count": 0,
                }

            # Calculate per-AP health scores AND collect metrics for network average
            ap_health = {}
            all_throughputs = []
            all_latencies = []
            all_jitters = []
            all_losses = []

            for ap in ap_stats:
                bssid = ap["bssid"]
                p50_throughput = float(ap["p50_throughput"] or 0)
                p75_latency = float(ap["p75_latency"] or 0)
                p75_jitter = float(ap["p75_jitter"] or 0)
                p75_loss = float(ap["p75_loss"] or 0)

                # Per-AP health score
                ap_health[bssid] = self._compute_ap_objective(
                    p50_throughput, p75_latency, p75_jitter, p75_loss
                )

                # Collect for network average
                all_throughputs.append(p50_throughput)
                all_latencies.append(p75_latency)
                all_jitters.append(p75_jitter)
                all_losses.append(p75_loss)

            # Network Score = formula(avg_metrics), NOT avg(ap_scores)!
            if all_throughputs:
                avg_throughput = sum(all_throughputs) / len(all_throughputs)
                avg_latency = sum(all_latencies) / len(all_latencies)
                avg_jitter = sum(all_jitters) / len(all_jitters)
                avg_loss = sum(all_losses) / len(all_losses)

                network_score = self._compute_ap_objective(
                    avg_throughput, avg_latency, avg_jitter, avg_loss
                )
            else:
                network_score = None

            return {
                "ap_health": ap_health,
                "network_score": network_score,
                "last_reset_sim_time": self._last_reset_sim_time,
                "current_sim_time": current_sim_time,
                "time_until_reset": max(
                    0, self.RESET_INTERVAL_SECONDS - time_since_reset
                ),
                "ap_count": len(ap_health),
            }

    def get_health_scores_time_series(self, time_resolution_minutes: int = 30, at_time: float = None):
        """
        Get health scores over time for charting.

        Returns time series of network scores at each snapshot.

        Args:
            time_resolution_minutes: Max time window in minutes (default 30)
            at_time: If provided, return data only up to this time (for replay mode)
        """
        with self.conn.cursor() as cur:
            # For replay mode, use at_time; otherwise use latest snapshot
            if at_time is not None:
                latest_snapshot = self.latest_snapshot(cur, at_time)
            else:
                latest_snapshot = self.latest_snapshot(cur)

            if not latest_snapshot:
                return []

            current_time = latest_snapshot["sim_time_seconds"]

            # Use same time window logic
            if self._last_reset_sim_time == 0:
                time_threshold = max(
                    0,
                    current_time - time_resolution_minutes * 60,
                )
            else:
                time_threshold = self._last_reset_sim_time

            # Get scores per snapshot period, filtered by time range
            cur.execute(
                """
                WITH snapshot_stats AS (
                    SELECT
                        snapshots.sim_time_seconds,
                        AVG(COALESCE(sta_metrics.uplink_throughput_mbps, 0) +
                            COALESCE(sta_metrics.downlink_throughput_mbps, 0)) AS avg_throughput,
                        AVG(sta_metrics.latency_ms) AS avg_latency,
                        AVG(sta_metrics.jitter_ms) AS avg_jitter,
                        AVG(sta_metrics.packet_loss_rate) AS avg_loss
                    FROM snapshots
                    LEFT JOIN sta_metrics ON sta_metrics.snapshot_id = snapshots.snapshot_id
                    WHERE snapshots.sim_time_seconds >= %s
                      AND snapshots.sim_time_seconds <= %s
                      AND snapshots.sim_time_seconds > 10
                    GROUP BY snapshots.sim_time_seconds
                    ORDER BY snapshots.sim_time_seconds ASC
                )
                SELECT * FROM snapshot_stats
                WHERE avg_throughput IS NOT NULL
                """,
                (time_threshold, current_time),
            )

            rows = cur.fetchall()

            results = []
            for row in rows:
                score = self._compute_ap_objective(
                    float(row["avg_throughput"] or 0),
                    float(row["avg_latency"] or 0),
                    float(row["avg_jitter"] or 0),
                    float(row["avg_loss"] or 0),
                )
                results.append(
                    {
                        "sim_time_seconds": row["sim_time_seconds"],
                        "network_score": score,
                    }
                )

            return results


class APIViewQuerier(_PostgresManager):
    def api_client_view(self):
        with self.conn.cursor() as cur:
            snap = self.latest_snapshot(cur)
            if not snap:
                return {"timestamp": _iso(None), "results": []}
            snap_id = snap["snapshot_id"]
            ts_unix = snap["snapshot_id_unix"]

            cur.execute(
                """
                SELECT DISTINCT
                    mac_address::text           AS mac_address,
                    ap_bssid::text              AS ap_bssid,
                    sta_view_rssi               AS rssi_of_client,
                    sta_view_snr                AS snr_of_client,
                    ap_view_rssi                AS rssi_of_ap,
                    ap_view_snr                 AS snr_of_ap,
                    packet_loss_rate,
                    latency_ms,
                    jitter_ms,
                    uplink_throughput_mbps,
                    downlink_throughput_mbps,
                    packet_loss_rate
                FROM sta_metrics
                WHERE snapshot_id = %s
                ORDER BY mac_address
            """,
                (snap_id,),
            )
            rows = cur.fetchall()

        results = []
        for d in rows:
            results.append(
                {
                    "mac_address": d["mac_address"],
                    "ap_bssid": d["ap_bssid"],
                    "rssi_of_client": d["rssi_of_client"],
                    "snr_of_client": d["snr_of_client"],
                    "rssi_of_ap": d["rssi_of_ap"],
                    "snr_of_ap": d["snr_of_ap"],
                    "packet_loss_rate": d["packet_loss_rate"],
                    "latency_ms": d["latency_ms"],
                    "jitter_ms": d["jitter_ms"],
                    "uplink_throughput_mbps": d["uplink_throughput_mbps"],
                    "downlink_throughput_mbps": d["downlink_throughput_mbps"],
                    "tcp_success_rate": 1 - d["packet_loss_rate"],
                    "retry_rate": d["packet_loss_rate"],
                }
            )
        return {"timestamp": _iso(ts_unix), "results": results}

    def api_metrics(self):
        with self.conn.cursor() as cur:
            snap = self.latest_snapshot(cur)
            if not snap:
                return {"timestamp": _iso(None), "roaming": [], "ap_metrics": []}
            snap_id = snap["snapshot_id"]
            ts_unix = snap["snapshot_id_unix"]

            cur.execute(
                """
                WITH ri AS (
                  SELECT to_bssid::text AS bssid, COUNT(*)::int AS c_in
                  FROM roaming_history
                  WHERE snapshot_id = %s
                  GROUP BY to_bssid
                ),
                ro AS (
                  SELECT from_bssid::text AS bssid, COUNT(*)::int AS c_out
                  FROM roaming_history
                  WHERE snapshot_id = %s
                  GROUP BY from_bssid
                ),
                agg AS (
                  SELECT COALESCE(ri.bssid, ro.bssid) AS bssid,
                         COALESCE(ri.c_in, 0) AS c_in,
                         COALESCE(ro.c_out, 0) AS c_out
                  FROM ri FULL OUTER JOIN ro USING (bssid)
                )
                SELECT bssid, c_in, c_out,
                       CASE WHEN (c_in + c_out) > 0
                            THEN c_in::float / (c_in + c_out)
                            ELSE NULL END AS success_ratio
                FROM agg
                ORDER BY bssid
            """,
                (snap_id, snap_id),
            )
            roam_rows = cur.fetchall()

            cur.execute(
                """
                WITH ap AS (
                  SELECT bssid::text AS bssid,
                         channel_utilization,
                         throughput,
                         phy_tx_power_level
                  FROM ap_metrics
                  WHERE snapshot_id = %s
                ),
                lat AS (
                  SELECT ap_bssid::text AS bssid,
                         AVG(latency_ms)::float AS mean_client_latency_ms
                  FROM sta_metrics
                  WHERE snapshot_id = %s
                  GROUP BY ap_bssid
                )
                SELECT ap.bssid,
                       ap.channel_utilization,
                       ap.throughput,
                       lat.mean_client_latency_ms,
                       ap.phy_tx_power_level
                FROM ap
                LEFT JOIN lat USING (bssid)
                ORDER BY ap.bssid
            """,
                (snap_id, snap_id),
            )
            ap_rows = cur.fetchall()

        roaming = [
            {
                "ap_bssid": r["bssid"],
                "roam_in_count": r["c_in"],
                "roam_out_count": r["c_out"],
                "roaming_success_rate": r["success_ratio"],
            }
            for r in roam_rows
        ]

        ap_metrics = [
            {
                "bssid": r["bssid"],
                "channel_utilization": r["channel_utilization"],
                "uplink_throughput_mbps": r["throughput"],
                "downlink_throughput_mbps": r["throughput"],
                "mean_client_latency_ms": r["mean_client_latency_ms"],
                "phy_tx_power_level": r["phy_tx_power_level"],
            }
            for r in ap_rows
        ]

        return {
            "timestamp": _iso(ts_unix),
            "roaming": roaming,
            "ap_metrics": ap_metrics,
        }

    def api_sensing(self):
        with self.conn.cursor() as cur:
            log = self._latest_sensing_log(cur)
            if not log:
                return {"timestamp": _iso(None), "results": []}
            log_id = log["sensing_log_id"]
            ts_unix = log["timestamp_unix"]

            cur.execute(
                """
                SELECT
                  bssid::text                         AS bssid,
                  channel,
                  COALESCE(band, 'BAND_2_4GHZ')       AS band,
                  interference_type,
                  confidence                          AS confidence_score,
                  interference_bandwidth,
                  interference_center_frequency,
                  interference_duty_cycle
                FROM sensing_results
                WHERE sensing_log_id = %s
                ORDER BY bssid, channel
            """,
                (log_id,),
            )
            rows = cur.fetchall()

        grouped = {}
        for r in rows:
            b = r["bssid"]
            grouped.setdefault(b, {"bssid": b, "router_name": None, "channels": []})
            grouped[b]["channels"].append(
                {
                    "channel": r["channel"],
                    "band": r["band"],
                    "interference_type": r["interference_type"],
                    "confidence_score": r["confidence_score"],
                    "interference_bandwidth": r["interference_bandwidth"],
                    "interference_center_frequency": r["interference_center_frequency"],
                    "interference_duty_cycle": r["interference_duty_cycle"],
                }
            )

        return {"timestamp": _iso(ts_unix), "results": list(grouped.values())}


class APIViewQuerier(_PostgresManager):
    def api_client_view(self):
        with self.conn.cursor() as cur:
            snap = self.latest_snapshot(cur)
            if not snap:
                return {"timestamp": _iso(None), "results": []}
            snap_id = snap["snapshot_id"]
            ts_unix = snap["snapshot_id_unix"]

            cur.execute(
                """
                SELECT DISTINCT
                    mac_address::text           AS mac_address,
                    ap_bssid::text              AS ap_bssid,
                    sta_view_rssi               AS rssi_of_client,
                    sta_view_snr                AS snr_of_client,
                    ap_view_rssi                AS rssi_of_ap,
                    ap_view_snr                 AS snr_of_ap,
                    packet_loss_rate,
                    latency_ms,
                    jitter_ms,
                    uplink_throughput_mbps,
                    downlink_throughput_mbps,
                    packet_loss_rate
                FROM sta_metrics
                WHERE snapshot_id = %s
                ORDER BY mac_address
            """,
                (snap_id,),
            )
            rows = cur.fetchall()

        results = []
        for d in rows:
            results.append(
                {
                    "mac_address": d["mac_address"],
                    "ap_bssid": d["ap_bssid"],
                    "rssi_of_client": d["rssi_of_client"],
                    "snr_of_client": d["snr_of_client"],
                    "rssi_of_ap": d["rssi_of_ap"],
                    "snr_of_ap": d["snr_of_ap"],
                    "packet_loss_rate": d["packet_loss_rate"],
                    "latency_ms": d["latency_ms"],
                    "jitter_ms": d["jitter_ms"],
                    "uplink_throughput_mbps": d["uplink_throughput_mbps"],
                    "downlink_throughput_mbps": d["downlink_throughput_mbps"],
                    "tcp_success_rate": 1 - d["packet_loss_rate"],
                    "retry_rate": d["packet_loss_rate"],
                }
            )
        return {"timestamp": _iso(ts_unix), "results": results}

    def api_metrics(self):
        with self.conn.cursor() as cur:
            snap = self.latest_snapshot(cur)
            if not snap:
                return {"timestamp": _iso(None), "roaming": [], "ap_metrics": []}
            snap_id = snap["snapshot_id"]
            ts_unix = snap["snapshot_id_unix"]

            cur.execute(
                """
                WITH ri AS (
                  SELECT to_bssid::text AS bssid, COUNT(*)::int AS c_in
                  FROM roaming_history
                  WHERE snapshot_id = %s
                  GROUP BY to_bssid
                ),
                ro AS (
                  SELECT from_bssid::text AS bssid, COUNT(*)::int AS c_out
                  FROM roaming_history
                  WHERE snapshot_id = %s
                  GROUP BY from_bssid
                ),
                agg AS (
                  SELECT COALESCE(ri.bssid, ro.bssid) AS bssid,
                         COALESCE(ri.c_in, 0) AS c_in,
                         COALESCE(ro.c_out, 0) AS c_out
                  FROM ri FULL OUTER JOIN ro USING (bssid)
                )
                SELECT bssid, c_in, c_out,
                       CASE WHEN (c_in + c_out) > 0
                            THEN c_in::float / (c_in + c_out)
                            ELSE NULL END AS success_ratio
                FROM agg
                ORDER BY bssid
            """,
                (snap_id, snap_id),
            )
            roam_rows = cur.fetchall()

            cur.execute(
                """
                WITH ap AS (
                  SELECT bssid::text AS bssid,
                         channel_utilization,
                         throughput,
                         phy_tx_power_level
                  FROM ap_metrics
                  WHERE snapshot_id = %s
                ),
                lat AS (
                  SELECT ap_bssid::text AS bssid,
                         AVG(latency_ms)::float AS mean_client_latency_ms
                  FROM sta_metrics
                  WHERE snapshot_id = %s
                  GROUP BY ap_bssid
                )
                SELECT ap.bssid,
                       ap.channel_utilization,
                       ap.throughput,
                       lat.mean_client_latency_ms,
                       ap.phy_tx_power_level
                FROM ap
                LEFT JOIN lat USING (bssid)
                ORDER BY ap.bssid
            """,
                (snap_id, snap_id),
            )
            ap_rows = cur.fetchall()

        roaming = [
            {
                "ap_bssid": r["bssid"],
                "roam_in_count": r["c_in"],
                "roam_out_count": r["c_out"],
                "roaming_success_rate": r["success_ratio"],
            }
            for r in roam_rows
        ]

        ap_metrics = [
            {
                "bssid": r["bssid"],
                "channel_utilization": r["channel_utilization"],
                "uplink_throughput_mbps": r["throughput"],
                "downlink_throughput_mbps": r["throughput"],
                "mean_client_latency_ms": r["mean_client_latency_ms"],
                "phy_tx_power_level": r["phy_tx_power_level"],
            }
            for r in ap_rows
        ]

        return {
            "timestamp": _iso(ts_unix),
            "roaming": roaming,
            "ap_metrics": ap_metrics,
        }

    def api_sensing(self):
        with self.conn.cursor() as cur:
            log = self._latest_sensing_log(cur)
            if not log:
                return {"timestamp": _iso(None), "results": []}
            log_id = log["sensing_log_id"]
            ts_unix = log["timestamp_unix"]

            cur.execute(
                """
                SELECT
                  bssid::text                         AS bssid,
                  channel,
                  COALESCE(band, 'BAND_2_4GHZ')       AS band,
                  interference_type,
                  confidence                          AS confidence_score,
                  interference_bandwidth,
                  interference_center_frequency,
                  interference_duty_cycle
                FROM sensing_results
                WHERE sensing_log_id = %s
                ORDER BY bssid, channel
            """,
                (log_id,),
            )
            rows = cur.fetchall()

        grouped = {}
        for r in rows:
            b = r["bssid"]
            grouped.setdefault(b, {"bssid": b, "router_name": None, "channels": []})
            grouped[b]["channels"].append(
                {
                    "channel": r["channel"],
                    "band": r["band"],
                    "interference_type": r["interference_type"],
                    "confidence_score": r["confidence_score"],
                    "interference_bandwidth": r["interference_bandwidth"],
                    "interference_center_frequency": r["interference_center_frequency"],
                    "interference_duty_cycle": r["interference_duty_cycle"],
                }
            )

        return {"timestamp": _iso(ts_unix), "results": list(grouped.values())}


class LogQuerier(_PostgresManager):
    def get_roaming_logs(self, limit: int = 100):
        with self.conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    roaming_history.roam_id,
                    roaming_history.sta_id::text,
                    roaming_history.from_bssid::text,
                    roaming_history.to_bssid::text,
                    snapshots.snapshot_id_unix,
                    snapshots.sim_time_seconds
                FROM roaming_history
                LEFT JOIN snapshots ON roaming_history.snapshot_id = snapshots.snapshot_id
                ORDER BY snapshots.snapshot_id_unix DESC
                LIMIT %s
                """,
                (limit,),
            )
            return cur.fetchall()

    def get_recent_roaming_logs(self, since_roam_id: int):
        """
        Fetches roaming logs that occurred after the given roam_id.

        Parameters:
            since_roam_id: The roam_id to fetch logs after

        Returns:
            List of roaming log entries with roam_id greater than the given id
        """
        with self.conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    roaming_history.roam_id,
                    roaming_history.sta_id::text,
                    roaming_history.from_bssid::text,
                    roaming_history.to_bssid::text,
                    snapshots.snapshot_id_unix,
                    snapshots.sim_time_seconds
                FROM roaming_history
                LEFT JOIN snapshots ON roaming_history.snapshot_id = snapshots.snapshot_id
                WHERE roaming_history.roam_id > %s
                ORDER BY snapshots.snapshot_id_unix DESC
                """,
                (since_roam_id,),
            )
            return cur.fetchall()

    def get_aggregated_metrics_by_interval(self, interval_minutes: int = 5):
        """
        Get aggregated metrics from ap_metrics and sta_metrics tables,
        grouped by simulation time intervals and individual APs.

        Parameters:
            interval_minutes: Duration of each interval in minutes (default: 5)

        Returns:
            List of dictionaries containing aggregated metrics per interval per AP
        """
        interval_seconds = interval_minutes * 60

        with self.conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    sta_metrics.ap_bssid::text AS bssid,
                    MAX(snapshots.snapshot_id_unix) AS timestamp,
                    AVG(sta_metrics.latency_ms) AS avg_latency_ms,
                    AVG(sta_metrics.jitter_ms) AS avg_jitter_ms,
                    AVG(sta_metrics.packet_loss_rate) AS avg_packet_loss_rate,
                    AVG(sta_metrics.ap_view_rssi) AS avg_rssi,
                    AVG(sta_metrics.ap_view_snr) AS avg_snr
                FROM snapshots
                LEFT JOIN sta_metrics ON sta_metrics.snapshot_id = snapshots.snapshot_id
                WHERE snapshots.sim_time_seconds > 0
                AND sta_metrics.ap_bssid IS NOT NULL
                GROUP BY FLOOR(snapshots.sim_time_seconds / %s), sta_metrics.ap_bssid
                """,
                (interval_seconds,),
            )
            return cur.fetchall()

    def get_sensing_logs(self, limit: int = 100):
        """
        Fetches sensing results with their log timestamps.

        Parameters:
            limit: Maximum number of sensing results to return (default: 100)

        Returns:
            List of sensing result entries with timestamps
        """
        with self.conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    sensing_results.sensing_result_id,
                    sensing_results.bssid::text,
                    sensing_results.channel,
                    sensing_results.band,
                    sensing_results.interference_type,
                    sensing_results.confidence,
                    sensing_results.interference_bandwidth,
                    sensing_results.interference_center_frequency,
                    sensing_results.interference_duty_cycle,
                    sensing_logs.timestamp_unix,
                    sensing_logs.sim_time_seconds
                FROM sensing_results
                LEFT JOIN sensing_logs ON sensing_results.sensing_log_id = sensing_logs.sensing_log_id
                ORDER BY sensing_logs.timestamp_unix DESC
                LIMIT %s
                """,
                (limit,),
            )
            return cur.fetchall()

    def get_recent_sensing_logs(self, since_sensing_result_id: int):
        """
        Fetches sensing results that occurred after the given sensing_result_id.

        Parameters:
            since_sensing_result_id: The sensing_result_id to fetch results after

        Returns:
            List of sensing result entries with sensing_result_id greater than the given id
        """
        with self.conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    sensing_results.sensing_result_id,
                    sensing_results.bssid::text,
                    sensing_results.channel,
                    sensing_results.band,
                    sensing_results.interference_type,
                    sensing_results.confidence,
                    sensing_results.interference_bandwidth,
                    sensing_results.interference_center_frequency,
                    sensing_results.interference_duty_cycle,
                    sensing_logs.timestamp_unix,
                    sensing_logs.sim_time_seconds
                FROM sensing_results
                LEFT JOIN sensing_logs ON sensing_results.sensing_log_id = sensing_logs.sensing_log_id
                WHERE sensing_results.sensing_result_id > %s
                ORDER BY sensing_logs.timestamp_unix DESC
                """,
                (since_sensing_result_id,),
            )
            return cur.fetchall()


class RRMQuerier(_PostgresManager):
    @staticmethod
    def _compute_network_score(
        p50_throughput: float, p75_latency: float, p75_jitter: float, p75_loss: float
    ) -> float:
        """
        Compute network score using RL formula (same as NetworkQuerier._compute_ap_objective).
        Returns health score 0-100 (higher is better).
        """
        MAX_THROUGHPUT = 100.0
        MAX_LATENCY = 750.0
        MAX_JITTER = 30.0
        MAX_LOSS = 1.0

        # Normalize (clamp to [0, 1])
        norm_throughput = min((p50_throughput or 0) / MAX_THROUGHPUT, 1.0)
        norm_latency = min((p75_latency or 0) / MAX_LATENCY, 1.0)
        norm_jitter = min((p75_jitter or 0) / MAX_JITTER, 1.0)
        norm_loss = min((p75_loss or 0) / MAX_LOSS, 1.0)

        # RL formula
        objective = (
            0.35 * norm_throughput
            + 0.10 * (1 - norm_jitter)
            + 0.35 * (1 - norm_loss)
            + 0.20 * (1 - norm_latency)
        )

        return round(objective * 100, 2)

    def get_rrm_updates(self, limit: int = 50):
        updates = []
        with self.conn.cursor() as cur:
            # Fetch Fast Loop Updates - only changed APs
            # Calculate network score from sta_metrics at closest snapshot
            cur.execute(
                """
                WITH fast_updates_with_snapshot AS (
                    SELECT
                        u.fast_loop_update_id,
                        u.batch_timestamp_unix,
                        u.status,
                        (
                            SELECT s.snapshot_id
                            FROM snapshots s
                            WHERE s.snapshot_id_unix <= u.batch_timestamp_unix
                            ORDER BY s.snapshot_id_unix DESC
                            LIMIT 1
                        ) AS closest_snapshot_id
                    FROM fast_loop_updates u
                    ORDER BY u.batch_timestamp_unix DESC
                    LIMIT %s
                ),
                snapshot_metrics AS (
                    SELECT
                        fu.fast_loop_update_id,
                        AVG(COALESCE(sm.uplink_throughput_mbps, 0) + COALESCE(sm.downlink_throughput_mbps, 0)) AS avg_throughput,
                        AVG(sm.latency_ms) AS avg_latency,
                        AVG(sm.jitter_ms) AS avg_jitter,
                        AVG(sm.packet_loss_rate) AS avg_loss
                    FROM fast_updates_with_snapshot fu
                    LEFT JOIN sta_metrics sm ON sm.snapshot_id = fu.closest_snapshot_id
                    GROUP BY fu.fast_loop_update_id
                )
                SELECT
                    u.fast_loop_update_id AS id,
                    u.batch_timestamp_unix,
                    sm.avg_throughput,
                    sm.avg_latency,
                    sm.avg_jitter,
                    sm.avg_loss,
                    u.status,
                    fu_orig.network_score AS stored_network_score,
                    'fast' AS type,
                    COALESCE(c.bssid::text, am.bssid::text, 'AP-' || c.ap_node_id::text) AS ap_identifier,
                    c.ap_node_id,
                    -- Use changed value if exists, otherwise use current AP value
                    COALESCE(c.new_channel, am.channel) AS new_channel,
                    c.new_channel_width,
                    COALESCE(c.new_tx_power, am.phy_tx_power_level) AS new_tx_power,
                    c.new_obss_pd,
                    c.old_channel,
                    c.old_channel_width,
                    c.old_tx_power,
                    c.old_obss_pd,
                    -- Current AP values for reference
                    am.channel AS current_channel,
                    am.phy_tx_power_level AS current_tx_power
                FROM fast_updates_with_snapshot u
                LEFT JOIN snapshot_metrics sm ON sm.fast_loop_update_id = u.fast_loop_update_id
                LEFT JOIN fast_loop_updates fu_orig ON u.fast_loop_update_id = fu_orig.fast_loop_update_id
                INNER JOIN fast_loop_changes c ON u.fast_loop_update_id = c.fast_loop_update_id
                LEFT JOIN (
                    SELECT DISTINCT ON (node_id) node_id, bssid, channel, phy_tx_power_level
                    FROM ap_metrics
                    ORDER BY node_id, snapshot_id DESC
                ) am ON c.ap_node_id = am.node_id
                WHERE c.ap_node_id IS NOT NULL
                  AND (c.new_channel IS NOT NULL OR c.new_tx_power IS NOT NULL
                       OR c.new_channel_width IS NOT NULL OR c.new_obss_pd IS NOT NULL)
                ORDER BY u.batch_timestamp_unix DESC
                """,
                (limit,),
            )
            fast_rows = cur.fetchall()

            # Fetch Slow Loop Updates - ONLY entries with actual config changes
            # Use INNER JOIN to filter out entries without changes
            cur.execute(
                """
                SELECT
                    u.slow_loop_update_id AS id,
                    EXTRACT(EPOCH FROM u.created_at) * 1000 AS timestamp_unix,
                    u.global_objective AS network_score,
                    u.status,
                    'slow' AS type,
                    c.bssid::text,
                    c.proposed_channel AS new_channel,
                    c.proposed_channel_width AS new_channel_width,
                    c.proposed_power_dbm AS new_tx_power,
                    NULL AS new_obss_pd
                FROM slow_loop_updates u
                INNER JOIN slow_loop_changes c ON u.slow_loop_update_id = c.slow_loop_update_id
                WHERE c.bssid IS NOT NULL
                  AND (c.proposed_channel IS NOT NULL
                       OR c.proposed_channel_width IS NOT NULL
                       OR c.proposed_power_dbm IS NOT NULL)
                ORDER BY u.created_at DESC
                LIMIT %s
                """,
                (limit,),
            )
            slow_rows = cur.fetchall()

        # Process and Merge
        temp_updates = {}

        for row in fast_rows:
            uid = f"fast-{row['id']}"
            if uid not in temp_updates:
                # Use stored network_score from fast_loop_updates table
                # This allows for explicit positive/negative scores (accepted/rejected)
                network_score = row.get("stored_network_score")
                # Fall back to computing from metrics if not stored
                if network_score is None and row.get("avg_throughput") is not None:
                    network_score = self._compute_network_score(
                        float(row["avg_throughput"] or 0),
                        float(row["avg_latency"] or 0),
                        float(row["avg_jitter"] or 0),
                        float(row["avg_loss"] or 0),
                    )
                temp_updates[uid] = {
                    "planner_id": row["id"],
                    "type": "fast",
                    # Convert to ms
                    "time_of_update": row["batch_timestamp_unix"] * 1000,
                    "status": row["status"],
                    "delta_network_score": network_score,
                    "changes": [],
                }
            # Check if there's a change record (ap_node_id or ap_identifier exists)
            if row["ap_node_id"] is not None or row["ap_identifier"]:
                temp_updates[uid]["changes"].append(
                    {
                        "ap_id": row["ap_identifier"] or f"AP-{row['ap_node_id']}",
                        # new_channel and new_tx_power already include current values via COALESCE
                        "updated_tx_power": row["new_tx_power"],
                        "updated_channel": row["new_channel"],
                        "updated_channel_width": row["new_channel_width"],
                        "updated_obss_pd_threshold": row["new_obss_pd"],
                        # old values only exist if there was a change
                        "old_tx_power": row["old_tx_power"],
                        "old_channel": row["old_channel"],
                        "old_channel_width": row["old_channel_width"],
                        "old_obss_pd_threshold": row["old_obss_pd"],
                        # Current AP values (for reference)
                        "current_channel": row.get("current_channel"),
                        "current_tx_power": row.get("current_tx_power"),
                    }
                )

        for row in slow_rows:
            uid = f"slow-{row['id']}"
            if uid not in temp_updates:
                temp_updates[uid] = {
                    "planner_id": row["id"],
                    "type": "slow",
                    # Already in ms from SQL query
                    "time_of_update": row["timestamp_unix"],
                    "status": row["status"],
                    "delta_network_score": row["network_score"],
                    "changes": [],
                }
            if row["bssid"]:
                temp_updates[uid]["changes"].append(
                    {
                        "ap_id": row["bssid"],
                        "updated_tx_power": row["new_tx_power"],
                        "updated_channel": row["new_channel"],
                        "updated_channel_width": row["new_channel_width"],
                        "updated_obss_pd_threshold": None,  # Slow loop doesn't have OBSS PD
                    }
                )

        updates = list(temp_updates.values())
        updates.sort(key=lambda x: x["time_of_update"], reverse=True)
        return updates[:limit]


class PostgresSimulationDB(_PostgresManager):
    def ap_metrics_by_node_id(self, node_id: int):
        with self.conn.cursor() as cur:
            last_snapshot = self.latest_snapshot(cur)
            if not last_snapshot:
                return {}

            cur.execute(
                "SELECT bssid FROM ap_metrics WHERE node_id = %s AND snapshot_id = %s",
                (node_id, last_snapshot["snapshot_id"]),
            )
            bssid_row = cur.fetchone()
            if not bssid_row:
                return {}
            bssid = bssid_row["bssid"]

            cur.execute(
                """
            SELECT count(*) FROM sta_metrics
            LEFT JOIN snapshots ON
            sta_metrics.snapshot_id = snapshots.snapshot_id
            WHERE sta_metrics.ap_bssid = %s::macaddr AND snapshots.snapshot_id = %s
            """,
                (bssid, last_snapshot["snapshot_id"]),
            )
            count_clients = cur.fetchone()

            cur.execute(
                """
            SELECT throughput, channel, channel_width, phy_tx_power_level
            FROM ap_metrics
            LEFT JOIN snapshots ON
            ap_metrics.snapshot_id = snapshots.snapshot_id
            WHERE ap_metrics.bssid = %s::macaddr AND snapshots.snapshot_id = %s
            """,
                (bssid, last_snapshot["snapshot_id"]),
            )
            ap_data = cur.fetchone()

            cur.execute(
                """
            SELECT SUM(bytes_sent) AS total_bytes_sent
            FROM ap_metrics
            WHERE ap_metrics.bssid = %s::macaddr
            GROUP BY bssid
            """,
                (bssid,),
            )
            bytes_sent_row = cur.fetchone()
            bytes_sent = bytes_sent_row["total_bytes_sent"] if bytes_sent_row else 0

            return {
                "bssid": bssid,
                "count_clients": count_clients["count"] if count_clients else 0,
                "uplink_throughput_mbps": ap_data["throughput"] if ap_data else None,
                "downlink_throughput_mbps": ap_data["throughput"] if ap_data else None,
                "channel": ap_data["channel"] if ap_data else None,
                "bytes_sent": bytes_sent,
                "phy_tx_power_level": ap_data["phy_tx_power_level"]
                if ap_data
                else None,
                # In simulation mode, show the simulation time when this data was captured
                "last_update_seconds": last_snapshot["sim_time_seconds"],
                "channel_width": ap_data["channel_width"] if ap_data else 20,
            }

    def client_metrics_by_node_id(self, node_id: int):
        with self.conn.cursor() as cur:
            last_snapshot = self.latest_snapshot(cur)
            if not last_snapshot:
                return {}

            cur.execute(
                """
                SELECT
                    latency_ms,
                    jitter_ms,
                    packet_loss_rate,
                    sta_view_rssi,
                    sta_view_snr
                FROM sta_metrics
                WHERE node_id = %s AND snapshot_id = %s
                """,
                (node_id, last_snapshot["snapshot_id"]),
            )
            metrics = cur.fetchone()

            if not metrics:
                return {}

            return {
                "latency": metrics["latency_ms"],
                "jitter": metrics["jitter_ms"],
                "packet_loss_rate": metrics["packet_loss_rate"],
                "rssi": metrics["sta_view_rssi"],
                "snr": metrics["sta_view_snr"],
            }

    def get_network_metrics(self):
        """
        Get overall network metrics from the latest snapshot.

        Returns:
            Dictionary containing:
            - throughput: Sum of uplink and downlink throughput across all clients (Mbps)
            - latency: Average latency across all clients (ms)
            - jitter: Average jitter across all clients (ms)
            - loss_rate: Average packet loss rate across all clients (%)
        """
        with self.conn.cursor() as cur:
            last_snapshot = self.latest_snapshot(cur)

            if not last_snapshot:
                return {
                    "throughput": None,
                    "latency": None,
                    "jitter": None,
                    "loss_rate": None,
                }

            cur.execute(
                """
                SELECT
                    SUM(COALESCE(uplink_throughput_mbps, 0) + COALESCE(downlink_throughput_mbps, 0)) as total_throughput,
                    AVG(latency_ms) as avg_latency,
                    AVG(jitter_ms) as avg_jitter,
                    AVG(packet_loss_rate) as avg_loss_rate
                FROM sta_metrics
                WHERE snapshot_id = %s
                """,
                (last_snapshot["snapshot_id"],),
            )

            result = cur.fetchone()

            return {
                "throughput": round(result["total_throughput"], 2)
                if result["total_throughput"] is not None
                else None,
                "latency": round(result["avg_latency"], 2)
                if result["avg_latency"] is not None
                else None,
                "jitter": round(result["avg_jitter"], 2)
                if result["avg_jitter"] is not None
                else None,
                "loss_rate": round(result["avg_loss_rate"], 2)
                if result["avg_loss_rate"] is not None
                else None,
            }


class PostgresMetricsLogger(
    PostgresWriter,
    APQuerier,
    STAQuerier,
    NetworkQuerier,
    APIViewQuerier,
    LogQuerier,
    RRMQuerier,
    PostgresSimulationDB,
):
    def __init__(self, host, dbname, password, user="postgres", port=5432):
        super().__init__(host, dbname, password, user, port)

    @staticmethod
    def create_database_if_not_exists(
        host, dbname, password, user="postgres", port=5432
    ):
        """
        Connects to PostgreSQL and creates the specified database if it does not already exist.
        """
        conn = None
        try:
            # Connect to the default 'postgres' database to create a new one
            conn = psycopg.connect(
                dbname="postgres", user=user, password=password, host=host, port=port
            )
            conn.autocommit = True  # Allow DDL statements outside of a transaction

            with conn.cursor() as cur:
                # Check if database exists
                cur.execute(f"SELECT 1 FROM pg_database WHERE datname = '{dbname}'")
                if not cur.fetchone():
                    print(f"Database '{dbname}' does not exist. Creating it...")
                    cur.execute(f"CREATE DATABASE {dbname}")
                    print(f"Database '{dbname}' created successfully.")
                else:
                    print(f"Database '{dbname}' already exists.")

        except psycopg.errors.DuplicateDatabase:
            print(f"Database '{dbname}' already exists (handled by exception).")
        except Exception as e:
            print(f"Error creating database '{dbname}': {e}")
        finally:
            if conn:
                conn.close()
