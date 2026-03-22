import json
import subprocess
import time
import os
import fcntl
import select
from pathlib import Path
import shutil

import matplotlib.pyplot as plt
import numpy as np

# ---- Load configs from RL/config_tester.py ----
import sys
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
from RL import config_tester

CONFIGS = config_tester.ALL_CONFIGS

# ============== SPEED SETTINGS ==============
CONSUMER_INIT_WAIT = 2       # Seconds to wait for consumer (was 5)
POLL_INTERVAL = 0.5          # Seconds between progress checks (was 2)
EARLY_STOP_TIME = 15         # Seconds before early stop (was 30)
SIM_TIME_SECONDS = 120       # Override simulation time for faster runs
MIN_LOGS_REQUIRED = 10       # Stop early if we have enough logs
# ============================================

# Paths
BASE_CONFIG_PATH = "config-simulation.json"
SIM_CONFIG_PATH = "tmp-sim-config.json"
LOGS_DIR = Path("./output_logs")
LOGS_DIR.mkdir(exist_ok=True)
CONSUMER_LOG_FILE = LOGS_DIR / "gat_consumer_logs_tmp.json"
# NS3 command - arguments must be in quoted string
def get_sim_cmd(config_path):
    """Build NS3 run command with config path."""
    return ["./ns3", "run", f"final-simulation/examples/basic-simulation --config={config_path}"]
KAFKA_CONSUMER_CMD = [
    "python3", "kafka-rl-consumer.py",
    "--log-file", str(CONSUMER_LOG_FILE),
]

def count_logs(log_file):
    """Count number of log entries in file."""
    if not log_file.exists():
        return 0
    try:
        with open(log_file, 'r') as f:
            return sum(1 for line in f if line.strip())
    except Exception:
        return 0

def update_config(base_config, aps_cfg):
    """Return new config-dict with APs updated from APS config."""
    new_config = dict(base_config)
    
    # Override simulation time for faster runs
    new_config['simulationTime'] = SIM_TIME_SECONDS
    
    aps = []
    for i, ap in enumerate(aps_cfg):
        # RL/config_tester.py: {"bssid":..., "channel":..., "width":..., "power":...}
        ap_entry = {
            "nodeId": i,
            "position": base_config['aps'][i]['position'] if 'aps' in base_config and i < len(base_config['aps']) else {"x":0,"y":0,"z":3},
            "leverConfig": {
                "txPower": float(ap.get("power", 20.0)),
                "channel": int(ap.get("channel", 36)),
                "ccaThreshold": -82.0,
                "obsspdThreshold": -72.0
            }
        }
        aps.append(ap_entry)
    new_config['aps'] = aps
    return new_config

def run_simulation(sim_cfg, config_name, stop_early=False, early_stop_time=EARLY_STOP_TIME):
    # Write config to file
    with open(SIM_CONFIG_PATH, 'w') as f:
        json.dump(sim_cfg, f, indent=2)
    print(f"[INFO] Config written (simTime={sim_cfg.get('simulationTime', '?')}s)")
    
    # Remove previous logs
    if CONSUMER_LOG_FILE.exists():
        CONSUMER_LOG_FILE.unlink()
    
    # Start Kafka consumer (logs metrics to file)
    print(f"[INFO] Starting Kafka consumer...")
    consumer_proc = subprocess.Popen(
        KAFKA_CONSUMER_CMD + ["--broker", "localhost:9092", "--topic", "ns3-metrics"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
    )
    # Wait briefly to ensure consumer is ready
    print(f"[INFO] Waiting {CONSUMER_INIT_WAIT}s for consumer...")
    time.sleep(CONSUMER_INIT_WAIT)
    
    # Start basic-simulation with new config (simulate as external process)
    sim_cmd = get_sim_cmd(SIM_CONFIG_PATH)
    print(f"[INFO] Starting NS3: {' '.join(sim_cmd)}")
    sim_proc = subprocess.Popen(
        sim_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
    )
    print(f"[INFO] Started simulation '{config_name}' (PID {sim_proc.pid})")
    
    # Monitor progress while simulation runs
    start_time = time.time()
    last_log_count = 0
    
    # Make stdout non-blocking so we can read simulation output without hanging
    fd = sim_proc.stdout.fileno()
    fl = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)
    
    if stop_early:
        # Monitor progress during early stop period
        while time.time() - start_time < early_stop_time:
            time.sleep(POLL_INTERVAL)
            elapsed = time.time() - start_time
            log_count = count_logs(CONSUMER_LOG_FILE)
            
            # Try to read any simulation output
            try:
                output = sim_proc.stdout.read()
                if output:
                    for line in output.strip().split('\n')[:3]:
                        print(f"[NS3] {line[:80]}")
            except:
                pass
            
            # Early exit if we have enough logs
            if log_count >= MIN_LOGS_REQUIRED:
                print(f"[FAST] Got {log_count} logs, stopping early!")
                break
            
            if log_count > last_log_count:
                print(f"[{elapsed:.0f}s] {log_count} logs (+{log_count - last_log_count})")
                last_log_count = log_count
        sim_proc.terminate()
        print(f"[INFO] Stopped '{config_name}' after {time.time() - start_time:.1f}s")
    else:
        # Monitor progress while waiting for simulation to complete
        while sim_proc.poll() is None:
            time.sleep(POLL_INTERVAL)
            elapsed = time.time() - start_time
            log_count = count_logs(CONSUMER_LOG_FILE)
            
            # Try to read any simulation output
            try:
                output = sim_proc.stdout.read()
                if output:
                    for line in output.strip().split('\n')[:3]:
                        print(f"[NS3] {line[:80]}")
            except:
                pass
            
            # Early exit if we have enough logs
            if log_count >= MIN_LOGS_REQUIRED:
                print(f"[FAST] Got {log_count} logs, stopping early!")
                sim_proc.terminate()
                break
            
            if log_count > last_log_count:
                print(f"[{elapsed:.0f}s] {log_count} logs (+{log_count - last_log_count})")
                last_log_count = log_count
        print(f"[INFO] Completed after {time.time() - start_time:.1f}s")
    
    # Stop consumer
    consumer_proc.terminate()
    print(f"[INFO] Consumer stopped for '{config_name}'")
    # Wait for processes to exit
    try:
        sim_proc.wait(timeout=10)
    except Exception:
        sim_proc.kill()
    try:
        consumer_proc.wait(timeout=10)
    except Exception:
        consumer_proc.kill()
    # Gather logs
    collected_logs = []
    if CONSUMER_LOG_FILE.exists():
        with open(CONSUMER_LOG_FILE, 'r') as f:
            for line in f:
                try:
                    collected_logs.append(json.loads(line.strip()))
                except Exception:
                    continue
    return collected_logs

def compute_objective(logs, verbose=True):
    """Calculate global-objective value from Optuna-style logs using config_tester objective."""
    obj_values = []
    print(f"[INFO] Computing objectives from {len(logs)} log entries...")
    for i, entry in enumerate(logs):
        data = entry.get('data', {})
        # For each AP in this log, compute per-AP objective, then avg
        per_ap_objs = []
        aps = data.get("aps", [])
        for ap_data in aps:
            v = config_tester.compute_per_ap_objective(ap_data)
            if v is not None:
                per_ap_objs.append(v)
        if per_ap_objs:
            # In config_tester.py: global = mean of AP objectives
            obj = np.mean(per_ap_objs)
            obj_values.append(obj)
            if verbose and (i + 1) % 10 == 0:
                print(f"[OBJECTIVE] Entry {i+1}/{len(logs)} | Objective: {obj:.4f} | Running avg: {np.mean(obj_values):.4f}")
    
    if obj_values:
        print(f"[RESULT] Computed {len(obj_values)} objectives | Mean: {np.mean(obj_values):.4f} | Min: {np.min(obj_values):.4f} | Max: {np.max(obj_values):.4f}")
    else:
        print(f"[WARN] No valid objectives computed from logs")
    
    return np.array(obj_values)

def plot_objectives(all_observations, config_names):
    plt.figure(figsize=(9, 6))
    for i, (obs, name) in enumerate(zip(all_observations, config_names)):
        if len(obs) == 0:
            continue
        plt.plot(range(1, len(obs)+1), obs, marker='o', label=f"{name} (avg {np.mean(obs):.3f})")
    plt.xlabel("Log entry")
    plt.ylabel("Objective")
    plt.title("Simulation Objectives per Config")
    plt.legend()
    plt.grid()
    plt.tight_layout()
    plt.show()

def main():
    print(f"[SPEED] simTime={SIM_TIME_SECONDS}s, earlyStop={EARLY_STOP_TIME}s, minLogs={MIN_LOGS_REQUIRED}")
    
    # Load base config file
    with open(BASE_CONFIG_PATH, "r") as f:
        base_config = json.load(f)
    config_names = []
    all_objectives = []
    
    for cfg_idx, config in enumerate(CONFIGS):
        print("\n" + "="*60)
        print(f"[{cfg_idx+1}/{len(CONFIGS)}] {config.get('name','unnamed')}")
        print("="*60)
        aps_cfg = config.get("aps", [])
        sim_cfg = update_config(base_config, aps_cfg)
        config_names.append(config.get('name', f"Config-{cfg_idx}"))
        
        # All configs use early stopping for speed
        logs = run_simulation(sim_cfg, config_names[-1], stop_early=True)
        objs = compute_objective(logs, verbose=False)
        all_objectives.append(objs)
        
        avg = np.mean(objs) if len(objs) > 0 else float('nan')
        print(f">>> {config_names[-1]}: {len(objs)} samples, mean={avg:.4f}")
    
    print("\n[INFO] Plotting results...")
    plot_objectives(all_objectives, config_names)

if __name__ == "__main__":
    main()

