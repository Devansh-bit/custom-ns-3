#!/usr/bin/env python3
"""
Test script to simulate sending spectrum data to the server
Useful for testing the Python server without running ns-3
"""

import socket
import json
import time
import numpy as np
import argparse


def send_handshake(sock, server_addr, node_id, mac, freq_start, freq_resolution, num_bins):
    """Send initial handshake packet."""
    packet = {
        'type': 'init',
        'node_id': node_id,
        'mac': mac,
        'freq_start': freq_start,
        'freq_resolution': freq_resolution,
        'num_bins': num_bins
    }

    data = json.dumps(packet).encode('utf-8')
    sock.sendto(data, server_addr)
    print(f"Sent handshake for node {node_id}")


def send_data_packet(sock, server_addr, node_id, timestamp, psd_data):
    """Send spectrum data packet."""
    packet = {
        'type': 'data',
        'node_id': node_id,
        'timestamp': timestamp,
        'psd': psd_data.tolist()
    }

    data = json.dumps(packet).encode('utf-8')
    sock.sendto(data, server_addr)


def generate_test_spectrum(num_bins, timestamp, noise_level=1e-12):
    """Generate synthetic spectrum data."""
    # Create frequency bins
    psd = np.ones(num_bins) * noise_level

    # Add some WiFi signals (Gaussian peaks)
    center1 = int(num_bins * 0.37)  # ~2.437 GHz
    width1 = int(num_bins * 0.02)   # 20 MHz
    psd[center1-width1:center1+width1] += 1e-10 * np.exp(
        -((np.arange(-width1, width1) / (width1/3))**2)
    )

    # Add Bluetooth (random peaks)
    for _ in range(5):
        pos = np.random.randint(0, num_bins)
        psd[max(0, pos-5):min(num_bins, pos+5)] += np.random.uniform(1e-11, 5e-11)

    # Add noise
    psd += np.random.normal(0, noise_level/10, num_bins)

    return psd


def main():
    parser = argparse.ArgumentParser(description='Test Spectrum UDP Server')
    parser.add_argument('--host', type=str, default='127.0.0.1',
                        help='Server host (default: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=9000,
                        help='Server port (default: 9000)')
    parser.add_argument('--num-aps', type=int, default=2,
                        help='Number of APs to simulate (default: 2)')
    parser.add_argument('--duration', type=float, default=5.0,
                        help='Test duration in seconds (default: 5.0)')
    parser.add_argument('--interval', type=float, default=0.1,
                        help='Interval between packets in seconds (default: 0.1)')

    args = parser.parse_args()

    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_addr = (args.host, args.port)

    print(f"Testing Spectrum UDP Server at {args.host}:{args.port}")
    print(f"Simulating {args.num_aps} APs for {args.duration} seconds")
    print(f"Sending packets every {args.interval} seconds\n")

    # Spectrum parameters
    freq_start = 2.4e9
    freq_resolution = 100e3
    num_bins = 1001

    # Send handshakes for all APs
    for node_id in range(args.num_aps):
        mac = f"00:00:00:00:00:{node_id:02x}"
        send_handshake(sock, server_addr, node_id, mac, freq_start, freq_resolution, num_bins)

    time.sleep(0.1)

    # Send data packets
    start_time = time.time()
    packet_count = 0

    while (time.time() - start_time) < args.duration:
        timestamp = time.time() - start_time

        # Send data for each AP
        for node_id in range(args.num_aps):
            psd_data = generate_test_spectrum(num_bins, timestamp)
            send_data_packet(sock, server_addr, node_id, timestamp, psd_data)
            packet_count += 1

        print(f"\rSent {packet_count} packets (t={timestamp:.2f}s)...", end='', flush=True)

        time.sleep(args.interval)

    print(f"\n\nTest complete! Sent {packet_count} total packets")
    sock.close()


if __name__ == '__main__':
    main()
