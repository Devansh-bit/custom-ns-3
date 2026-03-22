"""CNN inference module for spectrum detection."""
import os
import time
import uuid
import json
import logging
import numpy as np
import torch
import threading
from CNN.image_one import create_in_memory_image
from CNN.dutycycle_model import DeepSpectrum
from utils import canvas_loader
import config


def init_cnn_models(band_state):
    """Initialize CNN models for both bands at startup."""
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


def process_predictions(preds, tech_names, tech_stats, total_detections):
    """Process CNN predictions into detection format."""
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

    detections = []
    for tech_idx, tech_name in enumerate(tech_names):
        conf = float(pred_array[tech_idx]) if tech_idx < len(pred_array) else 0.0
        detections.append({"technology": tech_name, "confidence": conf})
        
        # Update statistics
        total_detections[0] += 1
        if tech_name not in tech_stats:
            tech_stats[tech_name] = {"max": conf, "sum": conf, "count": 1}
        else:
            tech_stats[tech_name]["max"] = max(tech_stats[tech_name]["max"], conf)
            tech_stats[tech_name]["sum"] += conf
            tech_stats[tech_name]["count"] += 1
    
    return detections, raw_save


def create_spectrogram_from_psd(psd_array, freq_axis_hz, T_frames, band_name):
    """Create spectrogram from PSD array."""
    tr_path = os.path.join(os.path.dirname(__file__), "outputs", "inference", "tr_files", 
                          f"temp_{band_name}_{uuid.uuid4().hex[:6]}.tr")
    
    # Write TR file
    with open(tr_path, "w") as f:
        for t in range(T_frames):
            timestamp = t * config.TIME_RES
            for bin_idx in range(len(freq_axis_hz)):
                freq = freq_axis_hz[bin_idx]
                power = psd_array[t, bin_idx]
                f.write(f"{timestamp:.6f} {freq:.4e} {power:.4e}\n")

    # Create spectrogram
    try:
        spectrogram, _, _ = create_in_memory_image(
            tr_path, start_time=0.0, window_size=T_frames * config.TIME_RES, image_size=256
        )
        logging.info(f"[{band_name}] Spectrogram shape: {spectrogram.shape}")
        os.remove(tr_path)  # Clean up temp file
        return spectrogram
    except Exception as e:
        logging.error(f"[{band_name}] Error creating image: {e}")
        # Fallback to canvas_loader
        flat_data = []
        for t in range(T_frames):
            for f_idx in range(len(freq_axis_hz)):
                flat_data.append([t*config.TIME_RES, freq_axis_hz[f_idx], psd_array[t, f_idx]])
        flat_array = np.array(flat_data)
        canvas = canvas_loader(flat_array)
        return canvas.astype(np.float32)


def send_to_cnn(psd_frames, ts_trigger, band_id, band_state, tech_stats, total_detections, base_name=None):
    """Unified CNN inference function for both bands."""
    state = band_state[band_id]
    band_name = state["name"]
    freq_start = state["freq_start"]
    freq_resolution = state["freq_resolution"]
    tech_names = state["tech_names"]
    
    psd_array = np.array(psd_frames)
    T_frames, F = psd_array.shape

    if base_name is None:
        base_name = f"cnn_{band_name}_{int(time.time()*1000)}_{uuid.uuid4().hex[:6]}"

    freq_axis_hz = np.linspace(freq_start, freq_start + (F - 1) * freq_resolution, F)
    spectrogram = create_spectrogram_from_psd(psd_array, freq_axis_hz, T_frames, band_name)

    # CNN inference
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
        raise  # Don't create dummy predictions, let caller handle

    detections, raw_save = process_predictions(preds, tech_names, tech_stats, total_detections)

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
    logging.info(f"[{band_name} CNN] Output saved | timestamp={float(ts_trigger):.3f}s | "
                 f"unix_time={unix_timestamp:.3f} | JSON={json_path} | NPY={tensor_path}")
    logging.info(f"[{band_name} CNN] Detections:\n" + json.dumps(detections, indent=2))

    return {"json": json_path, "tensor": tensor_path, "detections": detections}

