def canvas_loader(data_array):
    #data = np.loadtxt(path
    data = np.asarray(data_array)

    # unpack columns
    time, freq, energy = data[:, 0], data[:, 1], data[:, 2]
    unique_times = np.unique(time)
    unique_freqs = np.unique(freq)

    T = len(unique_times)
    F = len(unique_freqs)

    assert T * F == len(data), "Data is not a perfect grid!"

    # reshape to (freq, time)
    canvas = energy.reshape(T, F).T

    # column-wise normalization
    col_min = canvas.min(axis=0, keepdims=True)
    col_max = canvas.max(axis=0, keepdims=True)
    canvas_std = (canvas - col_min) / (col_max - col_min + 1e-12)

    return canvas_std


"""
Integrated spectrum sensing pipeline with MAB/PPO processing.
Handles NS3 pipe streaming, CNN inference, and RL decision making in one process.
"""
import logging
import os

# Create logs directory
os.makedirs(os.path.join(os.path.dirname(__file__), "logs"), exist_ok=True)

logging.basicConfig(
filename=os.path.join(os.path.dirname(__file__), "logs", "pipeline.log"), # Log file name
level=logging.INFO, # Log level (DEBUG / INFO / ERROR)
format="%(asctime)s - %(levelname)s - %(message)s"
)


logging.info("Program started — logging enabled")

from spectrum_pipe_reader_csv import SpectrumPipeServer
from collections import deque
import numpy as np
import threading
import time
from CNN.image_one import create_in_memory_image
from CNN.dutycycle_model import DeepSpectrum, DeepSpectrumModel
import torch
from CNN.inference import decode_predictions
import json
from CNN.dutycycle_config import TECH_ORDER, TECH_START_IDX, TECH_TO_BINS
import uuid
import atexit
import sys
import config

# Import MAB/PPO components
from MAB.MAB import SpectrogramEnv, MABRunner, PairDiscountedBetaThompsonAgent, EpsilonGreedyChooser, PPO

output_dir = os.path.join(os.path.dirname(__file__), "outputs", "inference", "tr_files")

# ===== MAB/PPO Configuration =====
WINDOW_WIDTH = 20
NUM_CHANNELS = 14
CHANNEL_SIZE = 20

# ===== MAB/PPO Data Storage =====
mab_data = {0: [], 1: []}  # Dual-band: 0=2.4GHz, 1=5GHz

# Initialize MAB Runner
runner = MABRunner(
    data=mab_data,
    window_width=WINDOW_WIDTH,
    num_channels=NUM_CHANNELS,
    channel_size=CHANNEL_SIZE,
    dynamic_dwell=True
)

# Generate pairs for BOTH bands (0=2.4GHz, 1=5GHz)
pairs = []
for band1 in range(2):
    for ch1 in range(1, NUM_CHANNELS):
        for band2 in range(2):
            for ch2 in range(1, NUM_CHANNELS):
                if (band1, ch1) != (band2, ch2):
                    pairs.append(((band1, ch1), (band2, ch2)))

# Initialize MAB/PPO agents
pair_agent = PairDiscountedBetaThompsonAgent(pairs=pairs)
chooser = EpsilonGreedyChooser(epsilon=0.05)
state_dim = int(runner.channel_size) * int(runner.window_width)
ppo_agent = PPO(
    state_dim=state_dim,
    action_dim=20,
    lr=3e-4,
    gamma=0.99,
    clip_eps=0.2
)

EPISODE_COUNTER = 0

def split_band_into_channels(num_channels, channel_width):
    """Split band into channel ranges."""
    channel_ranges = []
    for i in range(num_channels):
        start = i * channel_width
        end = (i + 1) * channel_width
        channel_ranges.append((start, end))
    return channel_ranges

# Global state for RL decisions
BAND_IDX = 0  # Selected band (0=2.4GHz, 1=5GHz)
CHANNEL_IDX = 0
DWELL_TIME = 0
channel_range = []
T = config.T  # frames per canvas
live_canvas_buffer_2_4 = deque(maxlen=T)  # 2.4 GHz buffer
live_canvas_buffer_5 = deque(maxlen=T)    # 5 GHz buffer
latest_canvas_2_4 = None  # Store latest 2.4 GHz canvas
latest_canvas_5 = None    # Store latest 5 GHz canvas
canvas_ready_2_4 = False  # Flag to indicate 2.4 GHz canvas is ready
canvas_ready_5 = False    # Flag to indicate 5 GHz canvas is ready
canvas_lock = threading.Lock()  # Lock for thread-safe canvas updates
DWELL_TIMER = 0
channel_selection_time = None  # Track when channel was selected for minimum dwell enforcement

# ===== EWMA parameters (from config) =====
baseline = config.BASELINE
offset = config.OFFSET
alpha_fast = config.ALPHA_FAST
alpha_slow = config.ALPHA_SLOW
TIME_RES = config.TIME_RES
PAUSE_DURATION = config.PAUSE_DURATION

# ===== Band-specific state stored in dictionaries =====
# Each band has its own EWMA state, CNN buffer, etc.
# Runtime state is initialized here, but configuration comes from config.py
band_state = {}
for band_id, band_cfg in config.BAND_CONFIG.items():
    band_state[band_id] = {
        # Configuration from config.py
        "name": band_cfg["name"],
        "freq_start": band_cfg["freq_start"],
        "freq_resolution": band_cfg["freq_resolution"],
        "model_path": band_cfg["model_path"],
        "tech_names": band_cfg["tech_names"],
        
        # Runtime state (initialized here)
        "fast": baseline,
        "slow": baseline + 10 * offset,
        "cooldown": 0,
        "cnn_trigger_ts": None,
        "cnn_triggered": False,
        "cnn_buffer": [],
        "fast_ewma": [],
        "slow_ewma": [],
        "ewma_rssi_series": [],
        "last_timestamp": None,
        "canvas_buffer": live_canvas_buffer_2_4 if band_id == 0 else live_canvas_buffer_5,
        "build_canvas_func": None,  # Will be set later
        "cnn_model": None,  # Cached CNN model
    }

tech_stats = {}
total_detections = 0

# Pre-load CNN models at startup to avoid repeated loading
def init_cnn_models():
    """Initialize CNN models for both bands at startup"""
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    for band_id, state in band_state.items():
        model_file = state["model_path"]
        model_path = os.path.join(os.path.dirname(__file__), "CNN-model-weights", model_file)
        try:
            ds = DeepSpectrum(device=device, pretrained_weights_path=model_path)
            state["cnn_model"] = ds
            print(f"[{state['name']}] CNN model loaded: {model_file}")
        except Exception as e:
            print(f"[{state['name']}] Failed to load CNN model: {e}")
            state["cnn_model"] = None

# Initialize models
init_cnn_models()

def compute_rssi_from_amplitudes(freq_amplitudes, noise_floor=-1e-15):
    power_corr = [max(p - noise_floor, 1e-20) for p in freq_amplitudes]
    total_power = sum(power_corr)
    return 10 * np.log10(total_power * 1000 + 1e-15)


def send_to_cnn(psd_frames, ts_trigger, band_id, base_name=None):
    """
    Unified CNN inference function for both 2.4 GHz and 5 GHz bands.
    
    Parameters:
        psd_frames: list of PSD arrays (freq bins) - typically 20 frames for 20ms
        ts_trigger: timestamp when CNN was triggered
        band_id: 0 for 2.4 GHz, 1 for 5 GHz
        base_name: optional base name for output files
    """
    global total_detections, tech_stats
    
    state = band_state[band_id]
    band_name = state["name"]
    freq_start = state["freq_start"]
    freq_resolution = state["freq_resolution"]
    model_file = state["model_path"]
    tech_names = state["tech_names"]
    
    # Convert list -> numpy array (time x freq)
    psd_array = np.array(psd_frames)  # shape: (T, F)
    T_frames, F = psd_array.shape

    # Prepare file paths
    if base_name is None:
        base_name = f"cnn_{band_name}_{int(time.time()*1000)}_{uuid.uuid4().hex[:6]}"

    # Generate frequency axis
    freq_axis_hz = np.linspace(freq_start, freq_start + (F - 1) * freq_resolution, F)

    # Write TR file
    tr_path = os.path.join(output_dir, f"{base_name}.tr")
    with open(tr_path, "w") as f:
        for t in range(T_frames):
            timestamp = t * TIME_RES
            for bin_idx in range(F):
                freq = freq_axis_hz[bin_idx]
                power = psd_array[t, bin_idx]
                f.write(f"{timestamp:.6f} {freq:.4e} {power:.4e}\n")

    logging.info(f"[{band_name}] Created TR file: {tr_path} with shape {psd_array.shape}")

    # Create spectrogram
    try:
        spectrogram, pil_img, rgb_img = create_in_memory_image(
            tr_path,
            start_time=0.0,
            window_size=T_frames * TIME_RES,
            image_size=256
        )
        logging.info(f"[{band_name}] Spectrogram shape: {spectrogram.shape}")
    except Exception as e:
        logging.error(f"[{band_name}] Error creating image: {e}")
        flat_data = []
        for t in range(T_frames):
            for f_idx in range(F):
                flat_data.append([t*TIME_RES, freq_axis_hz[f_idx], psd_array[t, f_idx]])
        flat_array = np.array(flat_data)
        canvas = canvas_loader(flat_array)
        spectrogram = canvas.astype(np.float32)

    # CNN inference using cached model
    try:
        ds = state["cnn_model"]
        if ds is None:
            raise RuntimeError("CNN model not loaded")

        with torch.no_grad():
            preds = ds.predict(spectrogram)
        print(f"[{band_name} CNN] Predictions: {preds[0][0]}")
        logging.info(f"[{band_name} CNN] Inference done")
    except Exception as e:
        logging.error(f"[{band_name} CNN] Inference failed: {e}")
        print(f"[{band_name} CNN] ERROR: {e}")
        raise  # Don't create dummy predictions

    # Process predictions
    raw_np = preds.cpu().numpy() if hasattr(preds, "cpu") else np.array(preds)

    if raw_np.ndim == 3:
        raw_save = raw_np[0]
        pred_array = raw_np[0].flatten()
    elif raw_np.ndim == 2:
        raw_save = raw_np
        pred_array = raw_np.flatten()
    else:
        raw_save = raw_np
        pred_array = raw_np

    # Extract detections
    detections = []
    for tech_idx, tech_name in enumerate(tech_names):
        if tech_idx < len(pred_array):
            conf = float(pred_array[tech_idx])
        else:
            conf = 0.0

        detections.append({
            "technology": tech_name,
            "confidence": conf
        })

        # Update statistics
        total_detections += 1
        if tech_name not in tech_stats:
            tech_stats[tech_name] = {"max": conf, "sum": conf, "count": 1}
        else:
            tech_stats[tech_name]["max"] = max(tech_stats[tech_name]["max"], conf)
            tech_stats[tech_name]["sum"] += conf
            tech_stats[tech_name]["count"] += 1

    # Save outputs
    json_dir = os.path.join(os.path.dirname(__file__), "outputs", "inference", "json")
    tensor_dir = os.path.join(os.path.dirname(__file__), "outputs", "inference", "tensors")
    os.makedirs(json_dir, exist_ok=True)
    os.makedirs(tensor_dir, exist_ok=True)

    json_path = os.path.join(json_dir, f"output_{base_name}.json")
    tensor_path = os.path.join(tensor_dir, f"{base_name}.npy")

    with open(json_path, "w", encoding="utf-8") as f:
        json.dump({"detections": detections}, f, ensure_ascii=False, indent=2)

    np.save(tensor_path, raw_save)

    unix_timestamp = time.time()
    logging.info(f"[{band_name} CNN] Output saved | timestamp={float(ts_trigger):.3f}s | unix_time={unix_timestamp:.3f} | JSON={json_path} | NPY={tensor_path}")
    logging.info(f"[{band_name} CNN] Detections:\n" + json.dumps(detections, indent=2))

    return {"json": json_path, "tensor": tensor_path, "detections": detections}


# ----------------------- MAB/PPO PROCESSING -------------------------------- #
def display_mab_decision(result):
    """Display and log MAB prediction results."""
    global EPISODE_COUNTER
    
    band_idx = result["band"]
    channel_idx = result["channel_idx"]
    dwell_time = result["dwell_time"]
    channel_range = result["channel_range"]

    # Convert DWELL_TIME (1-20) to milliseconds (50-200ms)
    dwell_ms = 50 + (dwell_time - 1) * (200 - 50) / 19
    dwell_sec = dwell_ms / 1000.0

    # Determine band name
    band_name = "2.4 GHz" if band_idx == 0 else "5 GHz"

    # Create formatted display
    print("\n" + "="*80)
    print("                      MAB PREDICTION RECEIVED")
    print("="*80)
    print(f"  Episode:              {EPISODE_COUNTER}")
    print(f"  Selected Band:        {band_name} (Band {band_idx})")
    print(f"  Selected Channel:     Channel {channel_idx}")
    print(f"  Channel Range:        Bins [{channel_range[0]} - {channel_range[1]}]")
    print(f"  Dwell Time:           {dwell_time} → {dwell_ms:.1f} ms ({dwell_sec:.3f} s)")
    print(f"  Decision Timestamp:   {time.strftime('%H:%M:%S')}")
    print("="*80 + "\n")

    # Log to file
    logging.info("="*80)
    logging.info(f"MAB PREDICTION RECEIVED (Episode {EPISODE_COUNTER})")
    logging.info(f"Band: {band_name} (idx={band_idx})")
    logging.info(f"Channel: {channel_idx}, Range: {channel_range}")
    logging.info(f"Dwell Time: {dwell_time} → {dwell_ms:.1f}ms ({dwell_sec:.3f}s)")
    logging.info("="*80)

    return {
        "band_idx": band_idx,
        "band_name": band_name,
        "channel_idx": channel_idx,
        "channel_range": channel_range,
        "dwell_time_raw": dwell_time,
        "dwell_ms": dwell_ms,
        "dwell_sec": dwell_sec,
    }


def process_mab_decision(canvas_dict):
    """Process dual-band canvas through MAB/PPO and return decision."""
    global BAND_IDX, CHANNEL_IDX, DWELL_TIME, channel_range, channel_selection_time, DWELL_TIMER, EPISODE_COUNTER
    
    # Add canvases to MAB data
    for band_id, canvas in canvas_dict.items():
        mab_data[band_id].append(canvas)
    
    # Check if we have at least one canvas in each band for dual-band training
    min_canvases = min(len(mab_data[0]), len(mab_data[1]))
    
    if min_canvases >= 1:
        # Run MAB/PPO training
        result = runner.train_sequential(
            pair_agent=pair_agent,
            chooser=chooser,
            ppo_agent=ppo_agent,
            GETS=True,
            dynamic_dwell=True,
        )
        
        EPISODE_COUNTER += 1
        episode_result = result[0][0]
        
        # Generate channel ranges
        channel_ranges = split_band_into_channels(
            num_channels=NUM_CHANNELS,
            channel_width=CHANNEL_SIZE
        )
        
        # Create response
        response = {
            "band": episode_result["last_selected_band"],
            "channel_idx": episode_result["last_selected_channel"],
            "dwell_time": episode_result["dwell_time"],
            "channel_range": channel_ranges[episode_result["last_selected_channel"]]
        }
        
        # Display decision
        mab_info = display_mab_decision(response)
        
        # Update global state
        BAND_IDX = response["band"]
        CHANNEL_IDX = response["channel_idx"]
        DWELL_TIME = response["dwell_time"]
        channel_range = response["channel_range"]
        channel_selection_time = time.time()
        DWELL_TIMER = mab_info["dwell_sec"]
        
        print(f"[MAB] Decision processed → band={response['band']}, channel={response['channel_idx']}, dwell={response['dwell_time']}")
        logging.info(f"[MAB] Episode {EPISODE_COUNTER} decision: band={response['band']}, channel={response['channel_idx']}, dwell={response['dwell_time']}")


def to_seconds(ts):
    """
    Convert incoming timestamp to seconds if it looks like milliseconds.
    Heuristic: if ts > 1000 -> treat as milliseconds and divide by 1000.
    Otherwise assume it's already in seconds.
    """
    try:
        if ts is None:
            return None
        return ts / 1000.0 if ts > 1000 else float(ts)
    except Exception:
        return float(ts)

def print_final_summary():
    print("\n===== FINAL DETECTION REPORT =====", flush=True)
    logging.info("===== FINAL DETECTION REPORT =====")
    logging.info(f"Total detections: {total_detections}")

    for tech, stats in tech_stats.items():
        avg = stats["sum"] / stats["count"]
        logging.info(f"{tech:10s} | max={stats['max']:.3f}  avg={avg:.3f}  n={stats['count']}")

    logging.info("===================================")


summary_started = False
TR_OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "outputs", "inference", "tr_files")
os.makedirs(TR_OUTPUT_DIR, exist_ok=True)


def build_canvas_for_T_frames(band_id):
    """Build T-frame canvas for specified band"""
    global latest_canvas_2_4, canvas_ready_2_4, latest_canvas_5, canvas_ready_5

    state = band_state[band_id]
    band_name = state["name"]
    canvas_buffer = state["canvas_buffer"]

    print(f"[{band_name}] Building canvas with {len(canvas_buffer)} frames", flush=True)

    # Extract timestamps and PSDs
    timestamps = [t for t, _ in canvas_buffer]
    psds = [p for _, p in canvas_buffer]

    # Stack into canvas (T x num_bins)
    canvas = np.array(psds, dtype=np.float64)

    with canvas_lock:
        if band_id == 0:
            latest_canvas_2_4 = canvas
            canvas_ready_2_4 = True
        else:
            latest_canvas_5 = canvas
            canvas_ready_5 = True

        print(f"[{band_name}] Canvas ready! Shape: {canvas.shape}", flush=True)

        # Check if both canvases are ready
        if canvas_ready_2_4 and canvas_ready_5:
            threading.Thread(target=process_combined_canvas, daemon=True).start()


def process_combined_canvas():
    """Process combined dual-band canvas through MAB/PPO directly (no sockets)"""
    global canvas_ready_2_4, canvas_ready_5, latest_canvas_2_4, latest_canvas_5

    with canvas_lock:
        if not (canvas_ready_2_4 and canvas_ready_5):
            return

        # Create combined dictionary
        combined_canvas = {
            0: latest_canvas_2_4,  # 2.4 GHz
            1: latest_canvas_5     # 5 GHz
        }

        print(f"[COMBINED] Processing dual-band canvas through MAB: 2.4GHz={latest_canvas_2_4.shape}, 5GHz={latest_canvas_5.shape}", flush=True)

        # Process through MAB/PPO directly
        process_mab_decision(combined_canvas)

        # Reset flags
        canvas_ready_2_4 = False
        canvas_ready_5 = False

        print(f"[COMBINED] MAB processing complete at {time.strftime('%H:%M:%S')}", flush=True)


def my_live_handler(node_id, timestamp, psd_values, band_id):
    """
    Unified live handler for both 2.4 GHz and 5 GHz bands.
    
    Parameters:
        node_id: NS3 node identifier
        timestamp: current timestamp from NS3
        psd_values: PSD array for current frame
        band_id: 0 for 2.4 GHz, 1 for 5 GHz
    """
    global summary_started
    global BAND_IDX, CHANNEL_IDX, DWELL_TIME, channel_range, DWELL_TIMER, channel_selection_time

    state = band_state[band_id]
    band_name = state["name"]
    canvas_buffer = state["canvas_buffer"]

    print(f"[{band_name}] Handler called @ {timestamp}", flush=True)

    # Start summary timer on first call (only once across both bands)
    if not summary_started:
        summary_started = True
        logging.info("[SUMMARY] First live handler triggered — starting 81-second timer")
        print("[DEBUG] starting 81-second summary timer", flush=True)
        threading.Timer(81.0, print_final_summary).start()

    # Initialize last_timestamp on first call
    if state["last_timestamp"] is None:
        state["last_timestamp"] = timestamp

    # Compute elapsed time
    elapsed = timestamp - state["last_timestamp"]
    state["last_timestamp"] = timestamp

    # Append live frame to band's buffer
    canvas_buffer.append((timestamp, psd_values))
    print(f"[{band_name}] Canvas buffer size = {len(canvas_buffer)}", flush=True)

    # Build canvas every T frames
    if len(canvas_buffer) == T:
        print(f"[{band_name}] Reached {T} frames! Building canvas...", flush=True)
        build_canvas_for_T_frames(band_id)
        canvas_buffer.clear()

    # ---- EWMA Detection (only if this band is selected by MAB) ----
    if BAND_IDX != band_id:  # Skip if MAB hasn't selected this band
        return

    # Skip if no channel range selected yet
    if not channel_range or len(channel_range) != 2:
        return

    # Update DWELL_TIMER
    if DWELL_TIMER > 0:
        DWELL_TIMER -= elapsed
        if DWELL_TIMER < 0:
            DWELL_TIMER = 0

    # DWELL complete check
    if DWELL_TIMER <= 0 and channel_range:
        if channel_selection_time is not None:
            time_since_selection = time.time() - channel_selection_time
            if time_since_selection < config.MIN_DWELL_TIME:
                remaining_min_time = config.MIN_DWELL_TIME - time_since_selection
                DWELL_TIMER = remaining_min_time
            else:
                print(f"\n[{band_name}] DWELL COMPLETE at t={timestamp:.3f}s\n")
                logging.info(f"[{band_name}] DWELL COMPLETE at t={timestamp:.3f}s")
                DWELL_TIMER = 0
                state["fast"] = baseline
                state["slow"] = baseline + 10 * offset
                channel_range = []
                channel_selection_time = None
                state["cooldown"] = 0
                return
        else:
            print(f"\n[{band_name}] DWELL COMPLETE at t={timestamp:.3f}s\n")
            logging.info(f"[{band_name}] DWELL COMPLETE at t={timestamp:.3f}s")
            DWELL_TIMER = 0
            state["fast"] = baseline
            state["slow"] = baseline + 10 * offset
            channel_range = []
            channel_selection_time = None
            state["cooldown"] = 0
            return

    # Extract selected frequency bins
    f_start, f_end = channel_range
    freq_amps = psd_values[f_start:f_end]

    # Compute RSSI
    rssi = compute_rssi_from_amplitudes(freq_amps)
    print(f"[{band_name}] RSSI: {rssi}")
    state["ewma_rssi_series"].append(rssi)

    # Cooldown
    if state["cooldown"] > 0:
        state["cooldown"] -= 1
    else:
        state["fast"] = alpha_fast * rssi + (1 - alpha_fast) * state["fast"]
        state["slow"] = alpha_slow * (rssi + offset) + (1 - alpha_slow) * state["slow"]

        state["fast_ewma"].append(state["fast"])
        state["slow_ewma"].append(state["slow"])

        # EWMA trigger - threshold crossing
        if rssi > baseline:
            state["cnn_trigger_ts"] = timestamp / 1000.0
            logging.info(f"[{band_name}] EWMA change detected → Trigger CNN @ {timestamp:.3f}ms")
            print(f"[{band_name}] t={timestamp:.3f}s | EWMA change → trigger CNN()")
            state["cooldown"] = int(PAUSE_DURATION / TIME_RES)
            state["cnn_triggered"] = True
            state["cnn_buffer"] = []

        # EWMA trigger - crossover
        fast_ewma = state["fast_ewma"]
        slow_ewma = state["slow_ewma"]
        if len(fast_ewma) >= 2 and fast_ewma[-2] <= slow_ewma[-2] and fast_ewma[-1] > slow_ewma[-1]:
            state["cnn_trigger_ts"] = timestamp / 1000.0
            logging.info(f"[{band_name}] EWMA crossover → Trigger CNN @ {timestamp:.3f}ms")
            print(f"[{band_name}] t={timestamp:.3f}s | EWMA crossover → trigger CNN()")
            state["cooldown"] = int(PAUSE_DURATION / TIME_RES)
            state["cnn_triggered"] = True
            state["cnn_buffer"] = []

    # Collect PSD frames for CNN
    if state["cnn_triggered"]:
        psd_values_np = np.array(psd_values)
        state["cnn_buffer"].append(psd_values_np.astype(np.float64))
        print(f"[{band_name} CNN] Buffer size: {len(state['cnn_buffer'])}")
        if len(state["cnn_buffer"]) >= config.CNN_BUFFER_SIZE:
            base_name = f"cnn_{band_name}_{int(time.time()*1000)}_{uuid.uuid4().hex[:6]}"
            threading.Thread(
                target=send_to_cnn,
                args=(state["cnn_buffer"].copy(), state["cnn_trigger_ts"], band_id, base_name),
                daemon=True
            ).start()
            state["cnn_buffer"].clear()
            state["cnn_triggered"] = False


# ----------------------- WRAPPER HANDLERS FOR EACH BAND -------------------------------- #
def my_live_handler_2_4ghz(node_id, timestamp, psd_values):
    """Wrapper for 2.4 GHz band that calls unified handler with band_id=0"""
    my_live_handler(node_id, timestamp, psd_values, band_id=0)


def my_live_handler_5ghz(node_id, timestamp, psd_values):
    """Wrapper for 5 GHz band that calls unified handler with band_id=1"""
    my_live_handler(node_id, timestamp, psd_values, band_id=1)


# ----------------------- START NS3 DUAL-BAND PIPES -------------------------------- #
# Create separate servers for 2.4 GHz and 5 GHz bands
spectrum_output_2_4 = os.path.join(os.path.dirname(__file__), "outputs", "spectrum_data", "2.4ghz")
spectrum_output_5 = os.path.join(os.path.dirname(__file__), "outputs", "spectrum_data", "5ghz")

server_2_4ghz = SpectrumPipeServer(base_path="/tmp/ns3-spectrum-2.4ghz", num_nodes=1, output_dir=spectrum_output_2_4)
server_2_4ghz.handle_live_data = my_live_handler_2_4ghz

server_5ghz = SpectrumPipeServer(base_path="/tmp/ns3-spectrum-5ghz", num_nodes=1, output_dir=spectrum_output_5)
server_5ghz.handle_live_data = my_live_handler_5ghz

logging.info("[2.4GHz] NS3 pipe streaming started at /tmp/ns3-spectrum-2.4ghz")
logging.info("[5GHz] NS3 pipe streaming started at /tmp/ns3-spectrum-5ghz")
logging.info("[MAB/PPO] Integrated MAB/PPO processing enabled (no socket communication)")

print("[DUAL-BAND] Starting 2.4 GHz and 5 GHz pipe servers...", flush=True)
print("[MAB/PPO] MAB/PPO processing integrated - no separate receiver.py needed", flush=True)

# START BOTH PIPE SERVERS ON BACKGROUND THREADS
threading.Thread(target=server_2_4ghz.start, daemon=True).start()
threading.Thread(target=server_5ghz.start, daemon=True).start()

# Keep main thread alive
while True:
    time.sleep(0.1)
