"""Run inference on images in `images/` using the project's model.

This script:
- Loads images from an `images/` directory (JPEG/PNG)
- Loads the DeepSpectrum model from `dutycycle_model.DeepSpectrum` (optionally with weights)
- Preprocesses images to 1x256x256 grayscale tensors
- Runs model.predict to obtain raw predictions
- Decodes predictions locally (DO NOT import or call the model's decode helpers)
- Saves per-image JSON files and per-image tensor .npy files under `inference_output/`

Usage: run the script directly. You can set env vars or modify defaults below.
"""
from __future__ import annotations

import os
import glob
import json
from typing import List, Dict, Any

import numpy as np
from PIL import Image
import torch

from CNN.dutycycle_model import DeepSpectrum
from CNN.dutycycle_config import (
    FREQ_MIN_MHZ,
    FREQ_RANGE_MHZ,
    TECH_START_IDX,
    TECH_TO_BINS,
    TECH_ORDER,
)


def load_image_as_tensor(path: str, image_size=(256, 256), device: torch.device = torch.device('cpu')) -> torch.Tensor:
    img = Image.open(path).convert('L')
    img = img.resize(image_size, Image.BILINEAR)
    arr = np.array(img, dtype=np.float32) / 255.0
    tensor = torch.from_numpy(arr).unsqueeze(0).unsqueeze(0)  # shape [1,1,H,W]
    return tensor.to(device)


def decode_predictions(preds: np.ndarray, confidence_threshold: float = 0.5, include_duty_cycle: bool = False) -> List[Dict[str, Any]]:
    """Decode model predictions into a list of detection dicts.

    This function intentionally does NOT call any decode helper from `dutycycle_model`.
    It assumes the prediction format per anchor is [confidence, norm_center, norm_bw, duration_or_dc]
    where norm_center is normalized across 2400-2500 MHz and norm_bw is normalized by 100 MHz.
    """
    detections: List[Dict[str, Any]] = []

    # preds can be torch tensor or numpy; ensure numpy and shape [Nm, 4] or [1, Nm, 4]
    if isinstance(preds, torch.Tensor):
        preds = preds.cpu().numpy()

    if preds.ndim == 3 and preds.shape[0] == 1:
        preds = preds[0]

    for idx, p in enumerate(preds):
        try:
            confidence = float(p[0])
            if confidence < confidence_threshold:
                continue

            norm_cf = float(p[1])
            norm_bw = float(p[2])
            duration_or_dc = float(p[3]) if p.shape[0] > 3 else 0.0

            # Convert normalized center frequency (0..1) -> MHz then Hz
            cf_mhz = float(np.clip(norm_cf, 0.0, 1.0)) * FREQ_RANGE_MHZ + FREQ_MIN_MHZ
            cf_hz = cf_mhz * 1e6

            # Bandwidth normalized by 100 MHz in this codebase
            bw_mhz = float(max(0.0, norm_bw)) * FREQ_RANGE_MHZ
            bw_hz = bw_mhz * 1e6

            # Determine technology by anchor index using TECH_START_IDX / TECH_TO_BINS
            tech = 'unknown'
            for tname, start in TECH_START_IDX.items():
                bins = TECH_TO_BINS.get(tname, 0)
                if idx >= start and idx < start + bins:
                    tech = tname
                    break

            # Determine human-friendly band
            if 2.4e9 <= cf_hz < 2.5e9:
                band = '2.4GHz'
            elif 5.0e9 <= cf_hz < 6.0e9:
                band = '5GHz'
            elif 900e6 <= cf_hz < 1000e6:
                band = '900MHz'
            elif 433e6 <= cf_hz < 435e6:
                band = '433MHz'
            else:
                band = 'Unknown'

            det: Dict[str, Any] = {
                'technology': tech,
                'center_frequency_hz': float(cf_hz),
                'center_frequency_mhz': float(cf_mhz),
                'bandwidth_hz': float(bw_hz),
                'bandwidth_mhz': float(bw_mhz),
                'band': band,
                'confidence': float(confidence),
            }
            raw_val = float(p[3]) if p.shape[0] > 3 else 0.0
            det['duty_cycle'] = float(raw_val) / 10.0
            detections.append(det)
        except Exception:
            # skip malformed anchor predictions
            continue

    return detections


def run_inference(
    images_dir: str = 'images',
    output_dir: str = 'inference_output',
    weights_path: str | None = None,
    device_str: str | None = None,
    confidence_threshold: float = 0.5,
    include_duty_cycle: bool = False,
):
    os.makedirs(output_dir, exist_ok=True)
    tensors_out = os.path.join(output_dir, 'tensors')
    os.makedirs(tensors_out, exist_ok=True)

    device = torch.device(device_str if device_str is not None and torch.cuda.is_available() else ('cuda' if torch.cuda.is_available() else 'cpu'))

    # Initialize model (DeepSpectrum will attempt to load backbone).
    ds = DeepSpectrum(device=device, pretrained_weights_path=weights_path)

    if getattr(ds, 'master_model', None) is None:
        raise RuntimeError('DeepSpectrum.master_model is not available; cannot run inference')

    image_files = sorted([p for p in glob.glob(os.path.join(images_dir, '*')) if p.lower().endswith(('.png', '.jpg', '.jpeg'))])
    if not image_files:
        print(f'No images found in {images_dir}.')
        return

    summary = {}
    for path in image_files:
        base = os.path.splitext(os.path.basename(path))[0]
        try:
            img_t = load_image_as_tensor(path, device=device)
            with torch.no_grad():
                preds = ds.predict(img_t)  # returns torch tensor [1, Nm, 4]

            # Save raw tensor (squeezed to [Nm,4])
            raw_np = preds.cpu().numpy()
            if raw_np.ndim == 3 and raw_np.shape[0] == 1:
                raw_save = raw_np[0]
            else:
                raw_save = raw_np
            np.save(os.path.join(tensors_out, f'{base}.npy'), raw_save)

            # Print tensor info image-wise in technology chunks (print all rows)
            try:
                print(f'Raw tensor shape for {base}: {raw_save.shape}')
                if raw_save.ndim == 2:
                    total_rows = raw_save.shape[0]
                    print(f'Printing all {total_rows} rows split by technology:')
                    for tech in TECH_ORDER:
                        start = int(TECH_START_IDX.get(tech, 0))
                        bins = int(TECH_TO_BINS.get(tech, 0))
                        end = start + bins
                        print('-' * 60)
                        print(f'{tech.upper()} rows {start}:{end} (count={bins}):')
                        # Use numpy array2string for compact multi-row printing
                        try:
                            slice_arr = raw_save[start:end]
                            print(np.array2string(slice_arr, precision=6, separator=', ', max_line_width=200))
                        except Exception as e:
                            print(f'  Could not format rows for {tech}: {e}')
                    print('-' * 60)
                else:
                    print(f'Warning: unexpected raw tensor ndim={raw_save.ndim} for {base}')
            except Exception as e:
                print(f'Could not print tensor details for {base}: {e}')

            # Decode locally
            # Decode predictions; pass include_duty_cycle option so Zigbee entries get duty_cycle
            detections = decode_predictions(raw_save, confidence_threshold=confidence_threshold, include_duty_cycle=include_duty_cycle)

            # Save per-image JSON
            out_json_path = os.path.join(output_dir, f'output_{base}.json')
            with open(out_json_path, 'w', encoding='utf-8') as f:
                json.dump({'detections': detections}, f, ensure_ascii=False, indent=2)

            summary[base] = {'num_detections': len(detections), 'json': out_json_path, 'tensor': os.path.join(tensors_out, f'{base}.npy')}
            print(f'Processed {base}: {len(detections)} detections')
        except Exception as e:
            print(f'Error processing {path}: {e}')

    # Save summary
    with open(os.path.join(output_dir, 'summary.json'), 'w', encoding='utf-8') as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    print('Inference complete. Outputs in', output_dir)


if __name__ == '__main__':
    # Simple, easy-to-edit entry point. Modify the values below directly
    # or set environment variables (INFERENCE_IMAGES, INFERENCE_OUT, INFERENCE_WEIGHTS,
    # INFERENCE_DEVICE, INFERENCE_CONF) to override.

    IMAGES_DIR = 'images'
    OUTPUT_DIR = 'inference_output'
    WEIGHTS = "bay2.pt"  # e.g. 'master_model.pt'
    DEVICE = None   # e.g. 'cpu' or 'cuda'
    CONF = 0.5

    # Allow quick overrides with environment variables
    IMAGES_DIR = os.environ.get('INFERENCE_IMAGES', IMAGES_DIR)
    OUTPUT_DIR = os.environ.get('INFERENCE_OUT', OUTPUT_DIR)
    WEIGHTS = os.environ.get('INFERENCE_WEIGHTS', WEIGHTS)
    DEVICE = os.environ.get('INFERENCE_DEVICE', DEVICE)
    try:
        CONF = float(os.environ.get('INFERENCE_CONF', CONF))
    except Exception:
        pass

    # Parse optional duty-cycle output flag (env var INFERENCE_DUTY). Accepts '1','true','yes' (case-insensitive)
    raw_duty = os.environ.get('INFERENCE_DUTY', '0')
    include_duty = str(raw_duty).lower() in ('1', 'true', 'yes', 'y')

    print(f'Running inference with images="{IMAGES_DIR}", out="{OUTPUT_DIR}", weights="{WEIGHTS}", device="{DEVICE}", conf={CONF}, include_duty_cycle={include_duty}')
    run_inference(images_dir=IMAGES_DIR, output_dir=OUTPUT_DIR, weights_path=WEIGHTS, device_str=DEVICE, confidence_threshold=CONF, include_duty_cycle=include_duty)
