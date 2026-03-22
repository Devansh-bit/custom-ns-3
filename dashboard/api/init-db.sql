-- Create the replay database for historical data
CREATE DATABASE ns3_e2e_test;

-- Connect to it and create schema
\c ns3_e2e_test

-- SNAPSHOT TABLE
CREATE TABLE snapshots (
    snapshot_id SERIAL PRIMARY KEY,
    snapshot_id_unix BIGINT NOT NULL,
    sim_time_seconds DOUBLE PRECISION NOT NULL,
    last_update_seconds DOUBLE PRECISION NOT NULL
);

-- AP METRICS TABLE
CREATE TABLE ap_metrics (
    ap_id SERIAL PRIMARY KEY,
    snapshot_id INTEGER NOT NULL REFERENCES snapshots(snapshot_id) ON DELETE CASCADE,
    node_id INTEGER NOT NULL,
    bssid MACADDR NOT NULL,
    channel INTEGER NOT NULL,
    channel_width INTEGER DEFAULT 20,
    band TEXT CHECK (band IN ('BAND_2_4GHZ', 'BAND_5GHZ', 'BAND_6GHZ')),
    channel_utilization DOUBLE PRECISION,
    client_count INTEGER,
    phy_tx_power_level INTEGER NOT NULL,
    throughput DOUBLE PRECISION,
    bytes_sent BIGINT,
    bytes_received BIGINT,
    interference DOUBLE PRECISION
);

-- AP CHANNEL METRICS TABLE
CREATE TABLE ap_channel_metrics (
    ap_channel_metric_id SERIAL PRIMARY KEY,
    snapshot_id INTEGER NOT NULL REFERENCES snapshots(snapshot_id) ON DELETE CASCADE,
    node_id INTEGER NOT NULL,
    channel INTEGER NOT NULL,
    non_wifi_channel_utilization DOUBLE PRECISION
);

-- STA METRICS TABLE
CREATE TABLE sta_metrics (
    sta_id SERIAL PRIMARY KEY,
    snapshot_id INTEGER NOT NULL REFERENCES snapshots(snapshot_id) ON DELETE CASCADE,
    node_id INTEGER,
    mac_address MACADDR,
    ap_bssid MACADDR NOT NULL,
    latency_ms DOUBLE PRECISION,
    jitter_ms DOUBLE PRECISION,
    packet_loss_rate DOUBLE PRECISION,
    mac_retry_rate DOUBLE PRECISION NOT NULL,
    ap_view_rssi DOUBLE PRECISION NOT NULL,
    ap_view_snr DOUBLE PRECISION NOT NULL,
    sta_view_rssi DOUBLE PRECISION NOT NULL,
    sta_view_snr DOUBLE PRECISION NOT NULL,
    uplink_throughput_mbps DOUBLE PRECISION,
    downlink_throughput_mbps DOUBLE PRECISION,
    packet_count BIGINT,
    last_update_seconds DOUBLE PRECISION
);

-- FAST LOOP TABLES
CREATE TABLE fast_loop_updates (
    fast_loop_update_id SERIAL PRIMARY KEY,
    simulation_id TEXT NOT NULL,
    batch_timestamp_unix BIGINT NOT NULL,
    batch_sim_time_sec DOUBLE PRECISION NOT NULL,
    network_score DOUBLE PRECISION,
    status TEXT DEFAULT 'accepted' CHECK (status IN ('accepted', 'rejected')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE fast_loop_changes (
    fast_loop_change_id SERIAL PRIMARY KEY,
    fast_loop_update_id INTEGER NOT NULL REFERENCES fast_loop_updates(fast_loop_update_id) ON DELETE CASCADE,
    event_id TEXT NOT NULL,
    event_type TEXT NOT NULL CHECK (event_type IN ('channel_switch', 'channel_width_switch', 'power_switch', 'obss_pd_switch')),
    sim_timestamp_sec DOUBLE PRECISION NOT NULL,
    ap_node_id INTEGER NOT NULL,
    bssid MACADDR,
    old_channel INTEGER,
    new_channel INTEGER,
    old_channel_width INTEGER,
    new_channel_width INTEGER,
    old_tx_power DOUBLE PRECISION,
    new_tx_power DOUBLE PRECISION,
    old_obss_pd DOUBLE PRECISION,
    new_obss_pd DOUBLE PRECISION,
    reason TEXT
);

-- SLOW LOOP TABLES
CREATE TABLE slow_loop_updates (
    slow_loop_update_id SERIAL PRIMARY KEY,
    simulation_id TEXT NOT NULL,
    step INTEGER NOT NULL,
    sim_time_start DOUBLE PRECISION NOT NULL,
    sim_time_end DOUBLE PRECISION NOT NULL,
    global_objective DOUBLE PRECISION,
    status TEXT DEFAULT 'proposed' CHECK (status IN ('proposed', 'accepted', 'rejected')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE slow_loop_changes (
    slow_loop_config_id SERIAL PRIMARY KEY,
    slow_loop_update_id INTEGER NOT NULL REFERENCES slow_loop_updates(slow_loop_update_id) ON DELETE CASCADE,
    bssid MACADDR NOT NULL,
    proposed_channel INTEGER,
    proposed_channel_width INTEGER,
    proposed_power_dbm DOUBLE PRECISION
);

-- SENSING TABLES
CREATE TABLE sensing_logs (
    sensing_log_id SERIAL PRIMARY KEY,
    timestamp_unix BIGINT NOT NULL,
    sim_time_seconds DOUBLE PRECISION NOT NULL,
    time_stamp_seconds TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE sensing_results (
    sensing_result_id SERIAL PRIMARY KEY,
    sensing_log_id INTEGER NOT NULL REFERENCES sensing_logs(sensing_log_id) ON DELETE CASCADE,
    bssid MACADDR NOT NULL,
    channel INTEGER NOT NULL,
    band TEXT CHECK (band IN ('BAND_2_4GHZ', 'BAND_5GHZ', 'BAND_6GHZ')) DEFAULT 'BAND_2_4GHZ',
    interference_type TEXT,
    confidence DOUBLE PRECISION,
    interference_bandwidth DOUBLE PRECISION,
    interference_center_frequency DOUBLE PRECISION,
    interference_duty_cycle DOUBLE PRECISION DEFAULT NULL
);

-- ROAMING HISTORY TABLE
CREATE TABLE roaming_history (
    roam_id SERIAL PRIMARY KEY,
    snapshot_id INTEGER NOT NULL REFERENCES snapshots(snapshot_id) ON DELETE CASCADE,
    sta_id MACADDR NOT NULL,
    from_bssid MACADDR NOT NULL,
    to_bssid MACADDR NOT NULL
);
