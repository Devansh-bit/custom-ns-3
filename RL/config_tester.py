#!/usr/bin/env python3
"""
AP Config Tester - Hardcoded Configuration Testing with Kafka
==============================================================

Sends 4 hardcoded AP configurations (worst → best quality) to NS3 via Kafka,
collects logs, calculates objective values, and plots results.

Usage:
    python config_tester.py
    python config_tester.py --logs-per-config 20 --settling-time 20

Requirements:
    pip install kafka-python matplotlib numpy
"""

import os
import sys
import json
import time
import argparse
import numpy as np
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Any, Optional, Tuple

# Optional plotting
try:
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("[WARN] matplotlib not available - plotting disabled")


# =============================================================================
# CHANNEL TO BANDWIDTH MAPPING (from main.py COLOR_CHANNEL_MAP)
# =============================================================================
# Color 0: {20: [36, 40, 44, 48], 40: [38, 46], 80: [42]}
# Color 1: {20: [52, 56, 60, 64], 40: [54, 62], 80: [58]}
# Color 2: {20: [100, 104, 108, 112], 40: [102, 110], 80: [106]}
# Color 3: {20: [116, 120, 124, 128], 40: [118, 126], 80: [122]}
# Color 4: {20: [132, 136, 140, 144], 40: [134, 142], 80: [138]}
# Color 5: {20: [149, 153, 157, 161], 40: [151, 159], 80: [155]}
#
# Channel → Width mapping:
#   36  → 20 MHz (Color 0)
#   54  → 40 MHz (Color 1)  
#   106 → 80 MHz (Color 2)
#   134 → 40 MHz (Color 4)
#
# Power options: [13.0, 15.0, 17.0, 19.0, 21.0, 23.0, 25.0] dBm
# =============================================================================

# =============================================================================
# HARDCODED AP CONFIGURATIONS (4 configs, each with 4 APs)
# Channels: progressively better separation from config1 to config4
# Using correct bandwidth mapping and varied power levels
# =============================================================================

# Config 1: WORST - All APs on same channel 36/20MHz (maximum interference)
# Powers: varied [17, 23, 15, 19] dBm
CONFIG_1 = {
    "name": "C1-[36,36,36,36]",
    "description": "All APs on ch36/20MHz - maximum co-channel interference",
    "channels": [36, 36, 36, 36],
    "aps": [
        {"bssid": "00:00:00:00:00:01", "channel": 36, "width": 20, "power": 17.0},
        {"bssid": "00:00:00:00:00:02", "channel": 36, "width": 20, "power": 23.0},
        {"bssid": "00:00:00:00:00:03", "channel": 36, "width": 20, "power": 15.0},
        {"bssid": "00:00:00:00:00:04", "channel": 36, "width": 20, "power": 19.0},
    ]
}

# Config 2: One AP separated to channel 54/40MHz
# Powers: varied [21, 19, 13, 25] dBm
CONFIG_2 = {
    "name": "C2-[36,54,36,36]",
    "description": "AP2 on ch54/40MHz, others on ch36/20MHz",
    "channels": [36, 54, 36, 36],
    "aps": [
        {"bssid": "00:00:00:00:00:01", "channel": 36, "width": 20, "power": 21.0},
        {"bssid": "00:00:00:00:00:02", "channel": 54, "width": 40, "power": 19.0},  # 54 → 40MHz
        {"bssid": "00:00:00:00:00:03", "channel": 36, "width": 20, "power": 13.0},
        {"bssid": "00:00:00:00:00:04", "channel": 36, "width": 20, "power": 25.0},
    ]
}

# Config 3: Three different channels (36/20, 54/40, 106/80)
# Powers: varied [15, 21, 17, 23] dBm
CONFIG_3 = {
    "name": "C3-[36,54,106,36]",
    "description": "ch36/20MHz, ch54/40MHz, ch106/80MHz (one duplicate)",
    "channels": [36, 54, 106, 36],
    "aps": [
        {"bssid": "00:00:00:00:00:01", "channel": 36, "width": 20, "power": 15.0},
        {"bssid": "00:00:00:00:00:02", "channel": 54, "width": 40, "power": 21.0},  # 54 → 40MHz
        {"bssid": "00:00:00:00:00:03", "channel": 106, "width": 80, "power": 17.0}, # 106 → 80MHz
        {"bssid": "00:00:00:00:00:04", "channel": 36, "width": 20, "power": 23.0},
    ]
}

# Config 4: BEST - All four APs on different channels with optimal widths
# ch36/20MHz, ch54/40MHz, ch106/80MHz, ch134/40MHz
# Powers: varied [19, 17, 21, 15] dBm
CONFIG_4 = {
    "name": "C4-[36,54,106,134]",
    "description": "All different: ch36/80, ch54/80, ch106/80, ch134/80 - no interference",
    "channels": [36, 54, 106, 134],
    "aps": [
        {"bssid": "00:00:00:00:00:01", "channel": 42, "width": 80, "power": 19.0},
        {"bssid": "00:00:00:00:00:02", "channel": 58, "width": 80, "power": 17.0},  # 54 → 40MHz
        {"bssid": "00:00:00:00:00:03", "channel": 106, "width": 80, "power": 21.0}, # 106 → 80MHz
        {"bssid": "00:00:00:00:00:04", "channel": 122, "width": 80, "power": 15.0}, # 134 → 40MHz
    ]
}

ALL_CONFIGS = [CONFIG_1, CONFIG_2, CONFIG_3, CONFIG_4]


# =============================================================================
# OBJECTIVE FUNCTION (matches main.py / optuna_clean.py)
# =============================================================================

MAX_P50_THROUGHPUT = 400.0   # Mbps
MAX_P95_LOSS_RATE = 1.0      # 0-1 (percentage/100)
MAX_P95_RTT = 1000.0         # ms
MAX_P95_JITTER = 50.0        # ms

def compute_per_ap_objective(ap_data: Dict[str, Any], verbose: bool = False) -> Optional[float]:
    conn_metrics = ap_data.get('connection_metrics', {})
    if not conn_metrics:
        return None
    throughputs = []
    loss_rates = []
    rtts = []
    jitters = []
    for client_key, metrics in conn_metrics.items():
        # Use snake_case keys (from NS3 logs) with camelCase fallback
        tp_up = metrics.get('uplink_throughput_mbps') or metrics.get('uplinkThroughputMbps') or 0
        tp_down = metrics.get('downlink_throughput_mbps') or metrics.get('downlinkThroughputMbps') or 0
        tp_up = min(max(float(tp_up), 0), 200)
        tp_down = min(max(float(tp_down), 0), 200)
        throughputs.append(min(tp_up + tp_down, MAX_P50_THROUGHPUT))
        
        loss = metrics.get('packet_loss_rate') or metrics.get('packetLossRate') or 0
        loss = min(max(float(loss), 0), MAX_P95_LOSS_RATE)
        loss_rates.append(loss)
        
        rtt = metrics.get('mean_rtt_latency') or metrics.get('meanRTTLatency') or 0
        rtt = min(max(float(rtt), 0), MAX_P95_RTT)
        rtts.append(rtt)
        
        jitter = metrics.get('jitter_ms') or metrics.get('jitterMs') or 0
        jitter = min(max(float(jitter), 0), MAX_P95_JITTER)
        jitters.append(jitter)
    if not throughputs:
        return None
    p50_throughput = np.percentile(throughputs, 50)
    p95_loss = np.percentile(loss_rates, 95) if loss_rates else 0.0
    p95_rtt = np.percentile(rtts, 95) if rtts else 0.0
    p95_jitter = np.percentile(jitters, 95) if jitters else 0.0
    norm_throughput = p50_throughput / MAX_P50_THROUGHPUT
    norm_loss = p95_loss / MAX_P95_LOSS_RATE
    norm_rtt = p95_rtt / MAX_P95_RTT
    norm_jitter = p95_jitter / MAX_P95_JITTER
    objective = (
        0.35 * norm_throughput +
        0.10 * (1 - norm_jitter) +
        0.35 * (1 - norm_loss) +
        0.20 * (1 - norm_rtt)
    )
    if verbose:
        print(f"    p50_throughput: {p50_throughput:.2f} Mbps (norm: {norm_throughput:.3f})")
        print(f"    p95_loss: {p95_loss:.4f} (norm: {norm_loss:.3f})")
        print(f"    p95_rtt: {p95_rtt:.2f} ms (norm: {norm_rtt:.3f})")
        print(f"    p95_jitter: {p95_jitter:.2f} ms (norm: {norm_jitter:.3f})")
        print(f"    objective: {objective:.4f}")
    return objective

def compute_global_objective(log_entry: Dict[str, Any], verbose: bool = False) -> Tuple[Optional[float], Dict[str, float]]:
    data = log_entry.get('data', {})
    ap_metrics = data.get('ap_metrics', {})
    if not ap_metrics:
        return None, {}
    per_ap_objectives = {}
    all_throughputs = []
    all_losses = []
    all_rtts = []
    all_jitters = []
    for ap_bssid, ap_data in ap_metrics.items():
        conn_metrics = ap_data.get('connection_metrics', {})
        if not conn_metrics:
            continue
        for client_key, metrics in conn_metrics.items():
            # Use snake_case keys (from NS3 logs) with camelCase fallback
            tp_up = metrics.get('uplink_throughput_mbps') or metrics.get('uplinkThroughputMbps') or 0
            tp_down = metrics.get('downlink_throughput_mbps') or metrics.get('downlinkThroughputMbps') or 0
            tp_up = min(max(float(tp_up), 0), 200)
            tp_down = min(max(float(tp_down), 0), 200)
            all_throughputs.append(min(tp_up + tp_down, MAX_P50_THROUGHPUT))
            
            loss = metrics.get('packet_loss_rate') or metrics.get('packetLossRate') or 0
            all_losses.append(min(max(float(loss), 0), MAX_P95_LOSS_RATE))
            
            rtt = metrics.get('mean_rtt_latency') or metrics.get('meanRTTLatency') or 0
            all_rtts.append(min(max(float(rtt), 0), MAX_P95_RTT))
            
            jitter = metrics.get('jitter_ms') or metrics.get('jitterMs') or 0
            all_jitters.append(min(max(float(jitter), 0), MAX_P95_JITTER))
        per_ap_obj = compute_per_ap_objective(ap_data, verbose=False)
        if per_ap_obj is not None:
            per_ap_objectives[ap_bssid] = per_ap_obj
    if not all_throughputs:
        return None, per_ap_objectives
    p50_throughput = np.percentile(all_throughputs, 50)
    p95_loss = np.percentile(all_losses, 95) if all_losses else 0.0
    p95_rtt = np.percentile(all_rtts, 95) if all_rtts else 0.0
    p95_jitter = np.percentile(all_jitters, 95) if all_jitters else 0.0
    norm_throughput = p50_throughput / MAX_P50_THROUGHPUT
    norm_loss = p95_loss / MAX_P95_LOSS_RATE
    norm_rtt = p95_rtt / MAX_P95_RTT
    norm_jitter = p95_jitter / MAX_P95_JITTER
    global_objective = (
        0.35 * norm_throughput +
        0.10 * (1 - norm_jitter) +
        0.35 * (1 - norm_loss) +
        0.20 * (1 - norm_rtt)
    )
    if verbose:
        print(f"\n  [GLOBAL OBJECTIVE]")
        print(f"    Total clients: {len(all_throughputs)}")
        print(f"    p50_throughput: {p50_throughput:.2f} Mbps")
        print(f"    p95_loss: {p95_loss:.4f}")
        print(f"    p95_rtt: {p95_rtt:.2f} ms")
        print(f"    p95_jitter: {p95_jitter:.2f} ms")
        print(f"    GLOBAL OBJECTIVE: {global_objective:.4f}")
    return global_objective, per_ap_objectives


# =============================================================================
# KAFKA INTERFACE
# =============================================================================

class ConfigTester:
    """Send configs to NS3 via Kafka and collect/analyze results."""
    def __init__(
        self,
        kafka_broker: str = 'localhost:9092',
        commands_topic: str = 'optimization-commands',
        simulation_id: str = 'basic-sim',
        log_file: str = './output_logs/gat_consumer_logs.json',
        logs_per_config: int = 20,
        settling_time: float = 20.0,
        verbose: bool = True
    ):
        self.kafka_broker = kafka_broker
        self.commands_topic = commands_topic
        self.simulation_id = simulation_id
        self.log_file = Path(log_file)
        self.logs_per_config = logs_per_config
        self.settling_time = settling_time
        self.verbose = verbose
        self.producer = None
        self.results = []
    def connect(self) -> bool:
        try:
            from kafka import KafkaProducer
            self.producer = KafkaProducer(
                bootstrap_servers=[self.kafka_broker],
                value_serializer=lambda v: json.dumps(v).encode('utf-8'),
                key_serializer=lambda k: k.encode('utf-8') if k else None
            )
            print(f"✓ Connected to Kafka: {self.kafka_broker}")
            print(f"✓ Commands topic: {self.commands_topic}")
            print(f"✓ Simulation ID: {self.simulation_id}")
            return True
        except Exception as e:
            print(f"✗ Failed to connect to Kafka: {e}")
            return False
    def build_kafka_command(self, config: Dict[str, Any]) -> Dict[str, Any]:
        ap_parameters = {}
        for ap in config['aps']:
            ap_parameters[ap['bssid']] = {
                'channel_number': ap['channel'],
                'tx_power_start_dbm': float(ap['power']),
                'tx_power_end_dbm': float(ap['power']),
                'channel_width_mhz': ap['width'],
                'band': 'BAND_5GHZ'
            }
        return {
            'timestamp_unix': int(time.time()),
            'simulation_id': self.simulation_id,
            'command_type': 'UPDATE_AP_PARAMETERS',
            'ap_parameters': ap_parameters
        }
    def send_config(self, config: Dict[str, Any]) -> bool:
        if not self.producer:
            print("[ERROR] Not connected to Kafka")
            return False
        try:
            kafka_cmd = self.build_kafka_command(config)
            future = self.producer.send(
                self.commands_topic,
                key=self.simulation_id,
                value=kafka_cmd
            )
            future.get(timeout=10)
            self.producer.flush()
            return True
        except Exception as e:
            print(f"[ERROR] Failed to send config: {e}")
            return False
    def get_current_log_count(self) -> int:
        if not self.log_file.exists():
            return 0
        with open(self.log_file, 'r') as f:
            return sum(1 for _ in f)
    def wait_for_logs(self, initial_count: int, target_count: int, timeout: float = 300.0) -> List[Dict[str, Any]]:
        """Wait for new logs; only logs with which objectives are later calculated will be printed when calc is done."""
        start_time = time.time()
        while time.time() - start_time < timeout:
            current_count = self.get_current_log_count()
            new_logs = current_count - initial_count
            if new_logs >= target_count:
                with open(self.log_file, 'r') as f:
                    all_lines = f.readlines()
                new_entries = []
                for line in all_lines[initial_count:initial_count + target_count]:
                    try:
                        entry = json.loads(line.strip())
                        new_entries.append(entry)
                    except json.JSONDecodeError:
                        continue
                return new_entries
            if self.verbose:
                elapsed = time.time() - start_time
                print(f"  Waiting for logs: {new_logs}/{target_count} (elapsed: {elapsed:.1f}s)", end='\r')
            time.sleep(1.0)
        print(f"\n  [WARN] Timeout waiting for logs (got {current_count - initial_count})")
        return []
    def run_test(self, configs: List[Dict[str, Any]] = None):
        if configs is None:
            configs = ALL_CONFIGS
        print("\n" + "=" * 80)
        print("AP CONFIGURATION TESTER")
        print("=" * 80)
        print(f"Configs to test: {len(configs)}")
        print(f"Logs per config: {self.logs_per_config}")
        print(f"Settling time: {self.settling_time}s")
        print(f"Log file: {self.log_file}")
        print("=" * 80)
        if not self.connect():
            return
        self.log_file.parent.mkdir(parents=True, exist_ok=True)
        if not self.log_file.exists():
            self.log_file.touch()
        for idx, config in enumerate(configs):
            print(f"\n{'─' * 80}")
            print(f"CONFIG {idx + 1}/{len(configs)}: {config['name']}")
            print(f"Description: {config['description']}")
            print("─" * 80)
            print("AP Settings:")
            print(f"  {'BSSID':<20} {'Channel':>8} {'Width':>8} {'Power':>8}")
            print(f"  {'-'*20} {'-'*8} {'-'*8} {'-'*8}")
            for ap in config['aps']:
                print(f"  {ap['bssid']:<20} {ap['channel']:>8} {ap['width']:>6}MHz {ap['power']:>6.1f}dBm")
            initial_count = self.get_current_log_count()
            print(f"\nCurrent log count: {initial_count}")
            print(f"Sending config to NS3...")
            if not self.send_config(config):
                print("[ERROR] Failed to send config, skipping")
                continue
            print(f"✓ Config sent! Waiting {self.settling_time}s for NS3 to apply...")
            time.sleep(self.settling_time)
            print(f"Collecting {self.logs_per_config} log entries...")
            new_logs = self.wait_for_logs(initial_count, self.logs_per_config)
            if not new_logs:
                print("[WARN] No logs collected for this config")
                self.results.append((config['name'], [], {}, []))
                continue
            print(f"\n✓ Collected {len(new_logs)} log entries")
            per_log_objectives = []
            all_per_ap = {}
            for log_idx, log_entry in enumerate(new_logs):
                global_obj, per_ap_obj = compute_global_objective(log_entry, verbose=False)
                if global_obj is not None:
                    print(f"\n--- Log Entry {log_idx+1}: Used for Objective Calculation ---")
                    data = log_entry.get('data', {})
                    ap_metrics = data.get('ap_metrics', {})
                    for ap_bssid, ap_data in ap_metrics.items():
                        conn_metrics = ap_data.get('connection_metrics', {})
                        if conn_metrics:
                            print(f"  AP {ap_bssid}:")
                            for client_key, metrics in conn_metrics.items():
                                tp_up = metrics.get('uplinkThroughputMbps', None)
                                tp_down = metrics.get('downlinkThroughputMbps', None)
                                loss = metrics.get('packetLossRate', None)
                                rtt = metrics.get('meanRTTLatency', None)
                                jitter = metrics.get('jitterMs', None)
                                print(f"    Client: {client_key}")
                                print(f"      Uplink: {tp_up} Mbps, Downlink: {tp_down} Mbps")
                                print(f"      Loss: {loss}, RTT: {rtt} ms, Jitter: {jitter} ms")
                    print(f"    Log {log_idx + 1}: Objective = {global_obj:.4f}")
                per_log_objectives.append({
                    'log_idx': log_idx + 1,
                    'global_objective': global_obj,
                    'per_ap': per_ap_obj
                })
                if global_obj is not None:
                    for ap_bssid, obj in per_ap_obj.items():
                        if ap_bssid not in all_per_ap:
                            all_per_ap[ap_bssid] = []
                        all_per_ap[ap_bssid].append(obj)
            valid_objectives = [o['global_objective'] for o in per_log_objectives if o['global_objective'] is not None]
            if valid_objectives:
                avg_objective = np.mean(valid_objectives)
                std_objective = np.std(valid_objectives)
                print(f"\n[RESULTS] Config: {config['name']}")
                print(f"  Average Objective: {avg_objective:.4f} ± {std_objective:.4f}")
                print(f"  Per-Log Values: {[f'{o:.3f}' for o in valid_objectives]}")
                print(f"\n  Per-AP Objectives (averaged across logs):")
                for ap_bssid, values in all_per_ap.items():
                    avg = np.mean(values)
                    print(f"    {ap_bssid}: {avg:.4f}")
                self.results.append((config['name'], per_log_objectives, all_per_ap, new_logs))
            else:
                print("[WARN] Could not calculate objective (no valid data)")
                self.results.append((config['name'], per_log_objectives, {}, new_logs))
        self._print_summary()
        self._save_results()
        if HAS_MATPLOTLIB:
            self._plot_results()
    def _print_summary(self):
        print("\n" + "=" * 80)
        print("FINAL SUMMARY")
        print("=" * 80)
        print(f"\n{'Config':<25} {'Avg Obj':>10} {'Min':>8} {'Max':>8} {'Logs':>6}")
        print("-" * 60)
        summary_data = []
        for name, per_log_objs, per_ap, logs in self.results:
            valid_objs = [o['global_objective'] for o in per_log_objs if o['global_objective'] is not None]
            if valid_objs:
                avg_obj = np.mean(valid_objs)
                min_obj = np.min(valid_objs)
                max_obj = np.max(valid_objs)
                summary_data.append((name, avg_obj, valid_objs))
                print(f"{name:<25} {avg_obj:>10.4f} {min_obj:>8.4f} {max_obj:>8.4f} {len(valid_objs):>6}")
            else:
                print(f"{name:<25} {'N/A':>10} {'N/A':>8} {'N/A':>8} {0:>6}")
        if summary_data:
            ranked = sorted(summary_data, key=lambda x: x[1], reverse=True)
            print("\n" + "-" * 60)
            print("RANKING (best to worst by average objective):")
            for rank, (name, avg_obj, _) in enumerate(ranked, 1):
                print(f"  {rank}. {name}: {avg_obj:.4f}")
        print("=" * 80)
    def _save_results(self):
        output_dir = Path("./output_logs")
        output_dir.mkdir(exist_ok=True)
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        output_file = output_dir / f"config_test_results_{timestamp}.json"
        results_data = {
            'timestamp': timestamp,
            'simulation_id': self.simulation_id,
            'logs_per_config': self.logs_per_config,
            'settling_time': self.settling_time,
            'results': []
        }
        for name, per_log_objs, per_ap, logs in self.results:
            valid_objs = [o['global_objective'] for o in per_log_objs if o['global_objective'] is not None]
            avg_per_ap = {}
            for ap_bssid, values in per_ap.items():
                avg_per_ap[ap_bssid] = float(np.mean(values)) if values else None
            results_data['results'].append({
                'config_name': name,
                'per_log_objectives': [o['global_objective'] for o in per_log_objs],
                'avg_objective': float(np.mean(valid_objs)) if valid_objs else None,
                'std_objective': float(np.std(valid_objs)) if valid_objs else None,
                'per_ap_objectives': avg_per_ap,
                'log_count': len(logs)
            })
        with open(output_file, 'w') as f:
            json.dump(results_data, f, indent=2)
        print(f"\n✓ Results saved to: {output_file}")
    def _plot_results(self):
        plot_data = []
        for name, per_log_objs, per_ap, logs in self.results:
            valid_objs = [o['global_objective'] for o in per_log_objs if o['global_objective'] is not None]
            if valid_objs:
                plot_data.append((name, valid_objs))
        if not plot_data:
            print("[WARN] No valid results to plot")
            return
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))
        colors = ['#d62728', '#ff7f0e', '#2ca02c', '#1f77b4']
        markers = ['o', 's', '^', 'D']
        for i, (name, objectives) in enumerate(plot_data):
            log_indices = list(range(1, len(objectives) + 1))
            ax1.plot(log_indices, objectives, 
                    marker=markers[i % len(markers)], 
                    color=colors[i % len(colors)],
                    linewidth=2, markersize=8,
                    label=name)
            for j, obj in enumerate(objectives):
                ax1.annotate(f'{obj:.3f}', (log_indices[j], obj),
                           textcoords="offset points", xytext=(0, 8),
                           ha='center', fontsize=8)
        ax1.set_xlabel('Log Entry #', fontsize=12)
        ax1.set_ylabel('Objective Value', fontsize=12)
        ax1.set_title('Objective per Log Entry\n(Each config shows individual measurements)', fontsize=12)
        ax1.legend(loc='best', fontsize=10)
        ax1.grid(True, alpha=0.3)
        ax1.set_ylim(0, 1.0)
        names = [d[0] for d in plot_data]
        avgs = [np.mean(d[1]) for d in plot_data]
        stds = [np.std(d[1]) for d in plot_data]
        bars = ax2.bar(range(len(names)), avgs, 
                      color=colors[:len(names)], 
                      edgecolor='black', linewidth=1.5,
                      yerr=stds, capsize=5)
        for i, (bar, avg, std) in enumerate(zip(bars, avgs, stds)):
            height = bar.get_height()
            ax2.text(bar.get_x() + bar.get_width()/2., height + std + 0.02,
                    f'{avg:.4f}', ha='center', va='bottom', fontsize=10, fontweight='bold')
        ax2.set_xticks(range(len(names)))
        ax2.set_xticklabels(names, rotation=15, ha='right', fontsize=10)
        ax2.set_xlabel('Configuration', fontsize=12)
        ax2.set_ylabel('Average Objective Value', fontsize=12)
        ax2.set_title('Average Objective per Config\n(Error bars = std dev)', fontsize=12)
        ax2.set_ylim(0, 1.0)
        ax2.grid(axis='y', alpha=0.3)
        ax2.axhline(y=0.5, color='gray', linestyle='--', alpha=0.5, label='Baseline')
        plt.tight_layout()
        output_dir = Path("./output_logs")
        output_dir.mkdir(exist_ok=True)
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        plot_file = output_dir / f"config_test_plot_{timestamp}.png"
        plt.savefig(plot_file, dpi=150, bbox_inches='tight')
        print(f"✓ Plot saved to: {plot_file}")
        plt.show()
        self._plot_timeline(plot_data, colors, markers)
    def _plot_timeline(self, plot_data, colors, markers):
        fig, ax = plt.subplots(figsize=(12, 5))
        all_objectives = []
        config_boundaries = [0]
        config_names = []
        for name, objectives in plot_data:
            all_objectives.extend(objectives)
            config_boundaries.append(len(all_objectives))
            config_names.append(name)
        x = list(range(1, len(all_objectives) + 1))
        for i, (name, objectives) in enumerate(plot_data):
            start = config_boundaries[i]
            end = config_boundaries[i + 1]
            segment_x = list(range(start + 1, end + 1))
            ax.plot(segment_x, objectives,
                   marker=markers[i % len(markers)],
                   color=colors[i % len(colors)],
                   linewidth=2, markersize=8,
                   label=name)
            ax.axvspan(start + 0.5, end + 0.5, alpha=0.1, color=colors[i % len(colors)])
        for i, boundary in enumerate(config_boundaries[1:-1], 1):
            ax.axvline(x=boundary + 0.5, color='gray', linestyle='--', alpha=0.5)
        ax.set_xlabel('Log Entry # (Sequential)', fontsize=12)
        ax.set_ylabel('Objective Value', fontsize=12)
        ax.set_title('Objective Timeline Across All Configurations\n(Configs applied sequentially)', fontsize=12)
        ax.legend(loc='upper left', fontsize=10)
        ax.grid(True, alpha=0.3)
        ax.set_ylim(0, 1.0)
        ax.set_xlim(0.5, len(all_objectives) + 0.5)
        plt.tight_layout()
        output_dir = Path("./output_logs")
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        timeline_file = output_dir / f"config_test_timeline_{timestamp}.png"
        plt.savefig(timeline_file, dpi=150, bbox_inches='tight')
        print(f"✓ Timeline plot saved to: {timeline_file}")
        plt.show()


# =============================================================================
# MAIN
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description='Test AP configurations and measure objectives')
    parser.add_argument('--broker', type=str, default='localhost:9092',
                       help='Kafka broker address')
    parser.add_argument('--topic', type=str, default='optimization-commands',
                       help='Kafka commands topic')
    parser.add_argument('--simulation-id', type=str, default='basic-sim',
                       help='NS3 simulation ID')
    parser.add_argument('--log-file', type=str, 
                       default=str(Path(__file__).parent.parent / 'output_logs' / 'gat_consumer_logs.json'),
                       help='Path to consumer log file')
    parser.add_argument('--logs-per-config', type=int, default=20,
                       help='Number of log entries to collect per config')
    parser.add_argument('--settling-time', type=float, default=20.0,
                       help='Seconds to wait after sending config')
    parser.add_argument('--quiet', action='store_true',
                       help='Reduce output verbosity')
    args = parser.parse_args()
    print("\n" + "=" * 80)
    print("   AP CONFIGURATION TESTER - Hardcoded Config Evaluation")
    print("=" * 80)
    print(f"\nThis script will:")
    print(f"  1. Send 4 hardcoded AP configs to NS3 (worst → best)")
    print(f"  2. Wait {args.settling_time}s for each config to be applied")
    print(f"  3. Collect {args.logs_per_config} log entries per config")
    print(f"  4. Calculate objective function values")
    print(f"  5. Plot and save results")
    print("\n" + "=" * 80)
    try:
        from kafka import KafkaProducer
    except ImportError:
        print("\n✗ Error: kafka-python is not installed")
        print("  Install with: pip install kafka-python")
        sys.exit(1)
    tester = ConfigTester(
        kafka_broker=args.broker,
        commands_topic=args.topic,
        simulation_id=args.simulation_id,
        log_file=args.log_file,
        logs_per_config=args.logs_per_config,
        settling_time=args.settling_time,
        verbose=not args.quiet
    )
    tester.run_test(ALL_CONFIGS)
if __name__ == '__main__':
    main()
