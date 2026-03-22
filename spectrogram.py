#!/usr/bin/env python3
"""
ISM Band Spectrogram visualization for ns-3 spectrum .tr files.

Units:
  - X-axis: Time (seconds)
  - Y-axis: Frequency (MHz)
  - Color: Power Spectral Density (dBm/Hz)

Supports both formats:
  - 3-column: time frequency power
  - 4-column: node_id time frequency power
"""

import numpy as np
import matplotlib.pyplot as plt
import argparse

# WiFi 2.4 GHz channel center frequencies (MHz)
WIFI_CHANNELS_24 = {
    1: 2412, 2: 2417, 3: 2422, 4: 2427, 5: 2432,
    6: 2437, 7: 2442, 8: 2447, 9: 2452, 10: 2457,
    11: 2462, 12: 2467, 13: 2472, 14: 2484
}

# WiFi 5 GHz channel center frequencies (MHz)
WIFI_CHANNELS_5 = {
    36: 5180, 40: 5200, 44: 5220, 48: 5240,
    52: 5260, 56: 5280, 60: 5300, 64: 5320,
    100: 5500, 104: 5520, 108: 5540, 112: 5560,
    116: 5580, 120: 5600, 124: 5620, 128: 5640,
    132: 5660, 136: 5680, 140: 5700, 144: 5720,
    149: 5745, 153: 5765, 157: 5785, 161: 5805, 165: 5825
}

def load_tr_file(filepath):
    """Load .tr file, auto-detecting 3 or 4 column format."""
    data = np.loadtxt(filepath, comments='#')
    if data.ndim == 1:
        data = data.reshape(1, -1)

    num_cols = data.shape[1]
    if num_cols == 4:
        return {
            'format': '4-column',
            'node_id': data[:, 0].astype(int),
            'time': data[:, 1],
            'frequency': data[:, 2],
            'power': data[:, 3]
        }
    elif num_cols == 3:
        return {
            'format': '3-column',
            'node_id': np.zeros(len(data), dtype=int),
            'time': data[:, 0],
            'frequency': data[:, 1],
            'power': data[:, 2]
        }
    else:
        raise ValueError(f"Unexpected column count: {num_cols}")

def psd_to_dbm_hz(psd_w_hz):
    """Convert PSD (W/Hz) to dBm/Hz."""
    with np.errstate(divide='ignore', invalid='ignore'):
        dbm_hz = 10 * np.log10(psd_w_hz) + 30
        dbm_hz = np.where(psd_w_hz <= 0, -200, dbm_hz)
    return dbm_hz

def plot_spectrogram(data, node_ids=None, output_file='spectrogram.png',
                     vmin=-130, vmax=-60, show_wifi_channels=True):
    """Plot ISM band spectrogram - Combined view across all APs."""

    all_node_ids = np.unique(data['node_id'])
    print(f"File format: {data['format']}")
    print(f"Nodes in data: {all_node_ids.tolist()}")
    print(f"Mode: Combined (max power across all APs)")

    # Get all unique times and frequencies
    all_freq_mhz = data['frequency'] / 1e6
    unique_times = np.unique(data['time'])
    unique_freqs_mhz = np.unique(all_freq_mhz)

    # Determine band
    avg_freq = np.mean(unique_freqs_mhz)
    is_5ghz = avg_freq > 4000
    band_name = "5 GHz" if is_5ghz else "2.4 GHz"
    wifi_channels = WIFI_CHANNELS_5 if is_5ghz else WIFI_CHANNELS_24

    print(f"\nBand: {band_name}")
    print(f"Time range: {unique_times[0]:.3f}s - {unique_times[-1]:.3f}s")
    print(f"Frequency range: {unique_freqs_mhz[0]:.1f} - {unique_freqs_mhz[-1]:.1f} MHz")
    print(f"Time samples: {len(unique_times)}")
    print(f"Frequency bins: {len(unique_freqs_mhz)}")

    # Create single figure
    fig, ax = plt.subplots(1, 1, figsize=(14, 6))

    # Build 2D spectrogram matrix (take MAX across all APs)
    spectrogram = np.full((len(unique_freqs_mhz), len(unique_times)), -200.0)

    time_to_idx = {t: i for i, t in enumerate(unique_times)}
    freq_to_idx = {f: i for i, f in enumerate(unique_freqs_mhz)}

    # Process all data points, taking max at each (time, freq)
    for t, f, p in zip(data['time'], all_freq_mhz, data['power']):
        t_idx = time_to_idx.get(t)
        f_idx = freq_to_idx.get(f)
        if t_idx is not None and f_idx is not None:
            psd_dbm = psd_to_dbm_hz(p)
            spectrogram[f_idx, t_idx] = max(spectrogram[f_idx, t_idx], psd_dbm)

    # Replace -200 with nan for display
    spectrogram = np.where(spectrogram <= -199, np.nan, spectrogram)

    # Plot
    im = ax.imshow(spectrogram,
                   aspect='auto',
                   origin='lower',
                   extent=[unique_times[0], unique_times[-1],
                           unique_freqs_mhz[0], unique_freqs_mhz[-1]],
                   cmap='viridis',
                   interpolation='nearest',
                   vmin=vmin,
                   vmax=vmax)

    ax.set_xlabel('Time (seconds)', fontsize=12)
    ax.set_ylabel('Frequency (MHz)', fontsize=12)
    ax.set_title(f'ISM Band Spectrogram - {band_name}', fontsize=14)

    # Add WiFi channel markers
    if show_wifi_channels:
        for ch, freq in wifi_channels.items():
            if unique_freqs_mhz[0] <= freq <= unique_freqs_mhz[-1]:
                ax.axhline(y=freq, color='white', linestyle='--',
                          alpha=0.3, linewidth=0.5)
                if (not is_5ghz and ch in [1, 6, 11]) or \
                   (is_5ghz and ch in [36, 52, 100, 149]):
                    ax.text(unique_times[-1] + 0.5, freq, f'Ch{ch}',
                           fontsize=8, va='center', color='white',
                           bbox=dict(boxstyle='round,pad=0.2',
                                    facecolor='gray', alpha=0.7))

    ax.grid(True, alpha=0.2, linestyle='--')

    # Colorbar
    cbar = plt.colorbar(im, ax=ax, pad=0.1)
    cbar.set_label('Power Spectral Density (dBm/Hz)', fontsize=11)

    # Stats
    valid_data = spectrogram[~np.isnan(spectrogram)]
    if len(valid_data) > 0:
        print(f"PSD range: [{valid_data.min():.1f}, {valid_data.max():.1f}] dBm/Hz")

    plt.tight_layout()
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"\nSpectrogram saved to: {output_file}")

def main():
    parser = argparse.ArgumentParser(
        description='ISM Band Spectrogram Visualization',
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('tr_file', help='Path to .tr file')
    parser.add_argument('--node', '-n', type=str, default=None,
                       help='Node ID(s) to plot (e.g., "0" or "0,1,2")')
    parser.add_argument('--output', '-o', type=str, default='spectrogram.png',
                       help='Output filename')
    parser.add_argument('--vmin', type=float, default=-130,
                       help='Min color scale (dBm/Hz)')
    parser.add_argument('--vmax', type=float, default=-60,
                       help='Max color scale (dBm/Hz)')
    parser.add_argument('--no-wifi-markers', action='store_true',
                       help='Hide WiFi channel markers')

    args = parser.parse_args()

    node_ids = None
    if args.node is not None:
        node_ids = [int(n.strip()) for n in args.node.split(',')]

    print(f"Loading: {args.tr_file}")
    data = load_tr_file(args.tr_file)

    plot_spectrogram(data, node_ids=node_ids, output_file=args.output,
                    vmin=args.vmin, vmax=args.vmax,
                    show_wifi_channels=not args.no_wifi_markers)

if __name__ == "__main__":
    main()
