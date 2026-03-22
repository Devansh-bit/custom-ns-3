"""Create a single in-memory spectrogram image (100 ms) from a .tr file.

This module exposes `create_in_memory_image(tr_path, start_time=0.0, window_size=0.1)`
which returns a grayscale numpy array (H, W) with values in [0,1] suitable for the
model's `predict` function (it accepts numpy arrays or torch tensors).

It re-uses the GPU-accelerated routines in `image_creation.py` when available.
"""
from __future__ import annotations

import os
import numpy as np
from PIL import Image

# try:
#     # reuse functions from image_creation if present
#     from CNN.image_creation import read_tr_file, create_spectrogram_image
# except Exception:
read_tr_file = None
create_spectrogram_image = None


def create_in_memory_image(tr_path: str, start_time: float = 0.0, window_size: float = 0.1, image_size: int = 256):
    """Create one spectrogram image for the given time window from the .tr file.

    Args:
        tr_path: path to the .tr file (text file with columns time, freq(Hz), power)
        start_time: window start (seconds)
        window_size: duration of window in seconds (default 0.1 -> 100 ms)
        image_size: output image size (square, pixels)

    Returns:
        spectrogram: np.ndarray shape (H, W) dtype float32 normalized to [0,1]
        pil_img: PIL.Image in mode 'L' (grayscale) for convenience
    """
    if not os.path.exists(tr_path):
        raise FileNotFoundError(f"TR file not found: {tr_path}")

    if read_tr_file is None or create_spectrogram_image is None:
        # Minimal fallback: implement a simple CPU-based reader and fake spectrogram
        data = np.loadtxt(tr_path)
        if data.size == 0:
            raise ValueError("Empty TR file")
        times = data[:, 0]
        freqs = data[:, 1]
        powers = data[:, 2]

        file_start = float(times.min())
        file_end = float(times.max())
        file_duration = file_end - file_start

        # If the file is longer than the requested window_size, use the first window_size
        # of the file (i.e., from file_start to file_start + window_size).
        if file_duration > window_size:
            win_start = file_start
            win_end = file_start + window_size
        else:
            win_start = start_time
            win_end = start_time + window_size

        # select points in window
        mask = (times >= win_start) & (times < win_end)
        times_w = times[mask]
        freqs_w = freqs[mask]
        pows_w = powers[mask]

        if len(times_w) == 0:
            # return an empty zero image (and an RGB copy)
            arr = np.zeros((image_size, image_size), dtype=np.float32)
            pil = Image.fromarray((arr * 255).astype(np.uint8), mode='L')
            rgb = pil.convert('RGB')
            return arr, pil, np.array(rgb, dtype=np.uint8)

        # compute simple 2D histogram
        t_bins = np.linspace(start_time, start_time + window_size, image_size + 1)
        f_bins = np.linspace(freqs.min(), freqs.max(), image_size + 1)
        H, _, _ = np.histogram2d(freqs_w, times_w, bins=[f_bins, t_bins], weights=pows_w)
        # convert to dB-like scale
        H_db = 10 * np.log10(H + 1e-20)
        H_db = np.clip(H_db, -120, 0)
        H_norm = (H_db + 120) / 120
        H_norm = np.flipud(H_norm)
        arr = H_norm.astype(np.float32)
        # Map to RGB using viridis for easier inspection
        import matplotlib.cm as cm
        cmap = cm.get_cmap('viridis')
        rgb_img = (cmap(arr)[..., :3] * 255).astype(np.uint8)
        pil = Image.fromarray(rgb_img).convert('L')
        return arr, pil, rgb_img

    # Use image_creation helper which returns an RGB uint8 image
    times, freqs, pows = read_tr_file(tr_path)
    # times/freqs/pows are torch tensors (possibly on GPU)
    try:
        file_start = float(times.min().item())
        file_end = float(times.max().item())
    except Exception:
        # fallback if tensors are small or not torch
        file_start = float(times.min())
        file_end = float(times.max())
    file_duration = file_end - file_start

    if file_duration > window_size:
        win_start = file_start
        win_end = file_start + window_size
    else:
        win_start = start_time
        win_end = start_time + window_size

    freq_start = float(freqs.min().item() if hasattr(freqs, 'min') else freqs.min())
    freq_end = float(freqs.max().item() if hasattr(freqs, 'max') else freqs.max())

    rgb = create_spectrogram_image(
        times, freqs, pows,
        time_start=win_start,
        time_end=win_end,
        freq_start=freq_start,
        freq_end=freq_end,
        image_size=image_size,
    )

    # rgb is uint8 HxWx3. Convert to grayscale and normalize to [0,1]
    # rgb is uint8 HxWx3. Convert to grayscale and normalize to [0,1]
    pil_gray = Image.fromarray(rgb, mode='RGB').convert('L')
    arr = np.array(pil_gray, dtype=np.float32) / 255.0

    return arr, pil_gray, rgb


if __name__ == '__main__':
    # quick manual test (will write to disk if run directly)
    import glob
    tr_files = sorted(glob.glob(os.path.join('data', '*.tr')))
    if not tr_files:
        print('No .tr files found in data/')
    else:
        arr, pil = create_in_memory_image(tr_files[0])
        print('Created in-memory image shape:', arr.shape)
        pil.save('debug_one.png')
        print('Wrote debug_one.png for inspection')
