"""Main pipeline: build one in-memory 100ms image from the single .tr file in `data/`,
run inference on it, and output JSON + tensor like `inference.py` does.

This script does NOT write the generated spectrogram image to disk (unless debug enabled).
It writes the model outputs to `inference_output/` (JSON + tensor) for parity with the
existing `inference.py` behavior.
"""
from __future__ import annotations

import os
import glob
import json
import numpy as np
import torch

from dutycycle_model import DeepSpectrum
from inference import decode_predictions
from image_one import create_in_memory_image
from dutycycle_config import TECH_ORDER, TECH_START_IDX, TECH_TO_BINS
from PIL import Image


def find_single_tr(data_dir: str = 'data') -> str:
    files = sorted(glob.glob(os.path.join(data_dir, '*.tr')))
    if not files:
        raise FileNotFoundError(f'No .tr files found in {data_dir}')
    if len(files) > 1:
        print('Warning: more than one .tr file found; using the first one:', files[0])
    return files[0]


def run_pipeline(data_dir: str = '../data', output_dir: str = '../outputs/inference', weights: str | None = None, device_str: str | None = None, debug_save_image: bool = False):
    os.makedirs(output_dir, exist_ok=True)
    tensors_out = os.path.join(output_dir, 'tensors')
    os.makedirs(tensors_out, exist_ok=True)

    tr_path = find_single_tr(data_dir)
    print('Using TR file:', tr_path)

    # Prepare base name and create a single 100 ms image in memory
    base = os.path.splitext(os.path.basename(tr_path))[0]
    spectrogram, pil_img, rgb_img = create_in_memory_image(tr_path, start_time=0.0, window_size=0.1, image_size=256)

    # Always save the non-grayscale RGB image for inspection
    try:
        rgb_pil = Image.fromarray(rgb_img)
        # Ensure saved RGB image is 256x256
        rgb_pil = rgb_pil.resize((256, 256), Image.BILINEAR)
        rgb_path = os.path.join(output_dir, f'{base}_rgb.png')
        rgb_pil.save(rgb_path)
        print('Saved RGB debug image to', rgb_path)
    except Exception as e:
        print('Could not save RGB image:', e)

    # Build model and predict
    device = torch.device(device_str if device_str is not None and torch.cuda.is_available() else ('cuda' if torch.cuda.is_available() else 'cpu'))
    ds = DeepSpectrum(device=device, pretrained_weights_path=weights)
    if getattr(ds, 'master_model', None) is None:
        raise RuntimeError('DeepSpectrum.master_model not available')

    # spectrogram is HxW float32 in [0,1] -> ds.predict accepts numpy
    with torch.no_grad():
        preds = ds.predict(spectrogram)

    raw_np = preds.cpu().numpy()
    if raw_np.ndim == 3 and raw_np.shape[0] == 1:
        raw_save = raw_np[0]
    else:
        raw_save = raw_np

    # Save tensor to disk for parity with inference.py
    base = os.path.splitext(os.path.basename(tr_path))[0]
    tensor_path = os.path.join(tensors_out, f'{base}.npy')
    np.save(tensor_path, raw_save)

    # Print tensor details partitioned by technology
    try:
        print(f'Raw tensor shape: {raw_save.shape}')
        if raw_save.ndim == 2:
            for tech in TECH_ORDER:
                start = int(TECH_START_IDX.get(tech, 0))
                bins = int(TECH_TO_BINS.get(tech, 0))
                end = start + bins
                print('-' * 60)
                print(f'{tech.upper()} rows {start}:{end} (count={bins}):')
                try:
                    slice_arr = raw_save[start:end]
                    print(np.array2string(slice_arr, precision=6, separator=', ', max_line_width=200))
                except Exception as e:
                    print(f'  Could not format rows for {tech}: {e}')
            print('-' * 60)
        else:
            print('Unexpected tensor ndim:', raw_save.ndim)
    except Exception as e:
        print('Could not print tensor details:', e)

    # Decode and save JSON
    detections = decode_predictions(raw_save)
    json_path = os.path.join(output_dir, f'output_{base}.json')
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump({'detections': detections}, f, ensure_ascii=False, indent=2)

    print('Pipeline complete.')
    print('JSON output:', json_path)
    print('Tensor output:', tensor_path)
    return {'json': json_path, 'tensor': tensor_path, 'detections': detections}


if __name__ == '__main__':
    # Simple run with defaults. Adjust weights or device by editing variables below or
    # by setting environment variables MAIN_WEIGHTS and MAIN_DEVICE.
    script_dir = os.path.dirname(os.path.abspath(__file__))
    examples_dir = os.path.dirname(script_dir)

    DATA_DIR = os.environ.get('MAIN_DATA_DIR', os.path.join(examples_dir, 'data'))
    OUT_DIR = os.environ.get('MAIN_OUT_DIR', os.path.join(examples_dir, 'outputs', 'inference'))
    WEIGHTS = os.environ.get('MAIN_WEIGHTS', os.path.join(examples_dir, 'CNN-model-weights', 'bay2.pt'))
    DEVICE = os.environ.get('MAIN_DEVICE', None)

    run_pipeline(data_dir=DATA_DIR, output_dir=OUT_DIR, weights=WEIGHTS, device_str=DEVICE, debug_save_image=False)
