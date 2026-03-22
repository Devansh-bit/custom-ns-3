#!/usr/bin/env python3
"""
Spectrum Pipe Reader - Reads binary PSD data from ns-3 spectrum-pipe-streamer

Binary format per message:
  - node_id: uint32 (4 bytes)
  - timestamp: double (8 bytes) - milliseconds
  - num_values: uint32 (4 bytes)
  - psd_values: double[num_values] (8 * num_values bytes)
"""
import struct
import sys
import os
import signal

def read_psd_message(pipe_fd):
    """Read one PSD message from the pipe"""
    # Read header: node_id (4) + timestamp (8) + num_values (4) = 16 bytes
    header = os.read(pipe_fd, 16)
    if len(header) < 16:
        return None
    
    node_id, timestamp, num_values = struct.unpack('<IdI', header)
    
    # Read PSD values
    psd_size = num_values * 8
    psd_data = os.read(pipe_fd, psd_size)
    if len(psd_data) < psd_size:
        return None
    
    psd_values = struct.unpack(f'<{num_values}d', psd_data)
    
    return {
        'node_id': node_id,
        'timestamp_ms': timestamp,
        'num_values': num_values,
        'psd_values': psd_values
    }

def psd_to_dbm(psd_watts_hz, bandwidth_hz=100e3):
    """Convert PSD (W/Hz) to power in dBm over bandwidth"""
    import math
    power_watts = psd_watts_hz * bandwidth_hz
    if power_watts <= 0:
        return -200  # Floor value
    return 10 * math.log10(power_watts) + 30  # Convert to dBm

def main():
    pipe_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ns3-spectrum/spectrum.pipe"
    full_output = "--full" in sys.argv or "-f" in sys.argv
    csv_output = "--csv" in sys.argv

    # Spectrum config (should match simulation)
    START_FREQ = 2.4e9   # Hz
    RESOLUTION = 100e3   # Hz (100 kHz per bin)

    print(f"Opening pipe: {pipe_path}")
    print("Waiting for ns-3 simulation to connect...")

    # Open pipe for reading (blocks until writer connects)
    pipe_fd = os.open(pipe_path, os.O_RDONLY)
    print("Connected! Reading PSD data...\n")

    if csv_output:
        print("timestamp_ms,freq_hz,psd_w_hz,psd_dbm")

    msg_count = 0
    try:
        while True:
            msg = read_psd_message(pipe_fd)
            if msg is None:
                print("End of stream or incomplete message")
                break

            msg_count += 1
            timestamp = msg['timestamp_ms']
            psd_values = msg['psd_values']

            # Find peak PSD
            max_psd = max(psd_values)
            max_idx = psd_values.index(max_psd)
            max_freq = START_FREQ + max_idx * RESOLUTION

            if csv_output:
                # Output all bins as CSV
                for i, psd in enumerate(psd_values):
                    freq = START_FREQ + i * RESOLUTION
                    dbm = psd_to_dbm(psd, RESOLUTION)
                    print(f"{timestamp:.1f},{freq:.0f},{psd:.6e},{dbm:.1f}")
            elif full_output:
                # Show all frequency bins with significant power
                print(f"\n[{msg_count}] Node {msg['node_id']} @ {timestamp:.1f}ms:")
                print(f"  Peak: {max_freq/1e9:.4f} GHz = {psd_to_dbm(max_psd):.1f} dBm")
                print(f"  Active bins (> -150 dBm):")
                for i, psd in enumerate(psd_values):
                    dbm = psd_to_dbm(psd, RESOLUTION)
                    if dbm > -150:  # Only show significant values
                        freq = START_FREQ + i * RESOLUTION
                        print(f"    {freq/1e9:.4f} GHz: {dbm:.1f} dBm ({psd:.2e} W/Hz)")
            else:
                # Summary only
                print(f"[{msg_count}] Node {msg['node_id']} @ {timestamp:.1f}ms: "
                      f"{msg['num_values']} bins, peak={psd_to_dbm(max_psd):.1f} dBm @ {max_freq/1e9:.4f} GHz")

    except KeyboardInterrupt:
        print(f"\nStopped. Received {msg_count} messages.")
    finally:
        os.close(pipe_fd)

if __name__ == "__main__":
    main()
