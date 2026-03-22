#!/usr/bin/env python3
import json
import time
import argparse
import os
from pathlib import Path
from typing import Dict, Any, Optional
from collections import defaultdict


class GATNodeDataProcessor:

    def __init__(self, log_file: str, ewma_alpha: float = 0.3, poll_interval: float = 1.0):
        self.log_file = Path(log_file)
        self.ewma_alpha = ewma_alpha
        self.poll_interval = poll_interval

        # EWMA state for each AP
        # Structure: {ap_bssid: {field_name: ewma_value}}
        self.ewma_state: Dict[str, Dict[str, float]] = {}

        # EWMA state for edges (RSSI between AP pairs)
        # Structure: {(ap1_bssid, ap2_bssid): ewma_rssi}
        self.edge_ewma_state: Dict[tuple, float] = {}

        # Track AP channels (for reference, not EWMA'd)
        self.ap_channels: Dict[str, int] = {}

        # Count of logs processed
        self.logs_processed = 0

        # Fixed power value
        self.fixed_power = 17

    def _ewma_update(self, old_value: Optional[float], new_value: float) -> float:
        """Update EWMA value"""
        if old_value is None:
            return new_value
        return self.ewma_alpha * new_value + (1 - self.ewma_alpha) * old_value

    def _get_max_channel_util_for_channel(self, ap_metrics: Dict, target_channel: int) -> float:
        channel_key = str(target_channel)
        max_util = 0.0

        for ap_bssid, ap_data in ap_metrics.items():
            scanning_data = ap_data.get('scanning_channel_data', {})
            if channel_key in scanning_data:
                channel_info = scanning_data[channel_key]
                util = channel_info.get('channel_utilization', 0.0)
                max_util = max(max_util, util)

        return max_util

    def _get_num_aps_on_channel(self, scanning_data: Dict, target_channel: int) -> int:
        channel_key = str(target_channel)

        if channel_key not in scanning_data:
            return 1  # At least self

        channel_info = scanning_data[channel_key]
        bssid_count = channel_info.get('bssid_count', 0)

        return bssid_count + 1

    def _extract_rssi_between_aps(self, ap_metrics: Dict) -> Dict[tuple, float]:
        rssi_pairs = {}

        for ap_bssid, ap_data in ap_metrics.items():
            scanning_data = ap_data.get('scanning_channel_data', {})

            for channel_key, channel_info in scanning_data.items():
                for neighbor in channel_info.get('neighbors', []):
                    neighbor_bssid = neighbor.get('bssid')
                    rssi = neighbor.get('rssi')

                    if neighbor_bssid and rssi is not None:
                        # Store as (from_ap, to_ap) -> rssi
                        rssi_pairs[(ap_bssid, neighbor_bssid)] = rssi

        return rssi_pairs

    def process_log_entry(self, log_entry: Dict[str, Any]) -> None:
        """Process a single log entry and update EWMA state"""
        data = log_entry.get('data', {})
        ap_metrics = data.get('ap_metrics', {})

        if not ap_metrics:
            return

        # Process each AP
        for ap_bssid, ap_data in ap_metrics.items():
            channel = ap_data.get('channel', 0)
            num_clients = ap_data.get('associated_clients', 0)
            scanning_data = ap_data.get('scanning_channel_data', {})

            # Get channel utilization: max across ALL APs' scanning data for this channel
            channel_util = self._get_max_channel_util_for_channel(ap_metrics, channel)
            print(f"AP {ap_bssid} on channel {channel} has channel utilization {channel_util}%")

            # Get number of APs on the same channel
            num_aps_on_channel = self._get_num_aps_on_channel(scanning_data, channel)

            # Initialize EWMA state for this AP if needed
            if ap_bssid not in self.ewma_state:
                self.ewma_state[ap_bssid] = {}

            # Update EWMA for each field
            state = self.ewma_state[ap_bssid]
            state['channel_utilization'] = self._ewma_update(
                state.get('channel_utilization'), channel_util
            )
            state['num_aps_on_channel'] = self._ewma_update(
                state.get('num_aps_on_channel'), num_aps_on_channel
            )
            state['num_clients'] = self._ewma_update(
                state.get('num_clients'), num_clients
            )

            # Track channel (not EWMA'd, just latest)
            self.ap_channels[ap_bssid] = channel

        # Process RSSI between AP pairs
        rssi_pairs = self._extract_rssi_between_aps(ap_metrics)
        for pair, rssi in rssi_pairs.items():
            self.edge_ewma_state[pair] = self._ewma_update(
                self.edge_ewma_state.get(pair), rssi
            )

        self.logs_processed += 1

    def get_final_dict(self) -> Dict[str, Any]:
        result = {
            'nodes': {},
            'ap_order': [],
            'edges': []
        }

        # Get sorted list of AP BSSIDs for consistent ordering
        ap_list = sorted(self.ewma_state.keys())
        result['ap_order'] = ap_list
        num_aps = len(ap_list)

        # Build nodes dict
        for ap_bssid, state in self.ewma_state.items():
            result['nodes'][ap_bssid] = {
                'channel': self.ap_channels.get(ap_bssid, 0),
                'power': self.fixed_power,
                'channel_utilization': round(state.get('channel_utilization', 0.0), 2),
                'num_aps_on_channel': round(state.get('num_aps_on_channel', 0), 2),
                'num_clients': round(state.get('num_clients', 0), 2)
            }

        for i, ap_i in enumerate(ap_list):
            row = []
            for j, ap_j in enumerate(ap_list):
                if i == j:
                    # Self-connection: use 0.0
                    row.append(0.0)
                else:
                    # Get RSSI from ap_i to ap_j
                    rssi = self.edge_ewma_state.get((ap_i, ap_j))
                    if rssi is not None:
                        row.append(round(rssi, 2))
                    else:
                        # No RSSI data available, use 0.0 (or could use a sentinel like -100)
                        row.append(0.0)
            result['edges'].append(row)

        return result

    def read_and_remove_logs(self) -> list:
        if not self.log_file.exists():
            return []

        logs = []

        try:
            with open(self.log_file, 'r') as f:
                lines = f.readlines()

            if not lines:
                return []

            # Parse all log entries
            for line in lines:
                line = line.strip()
                if line:
                    try:
                        log_entry = json.loads(line)
                        logs.append(log_entry)
                    except json.JSONDecodeError as e:
                        print(f"Warning: Failed to parse log line: {e}")

            with open(self.log_file, 'w') as f:
                pass  # Truncate file

            return logs

        except Exception as e:
            print(f"Error reading log file: {e}")
            return []

    def clear_log_file(self) -> None:
        """Clear the log file completely"""
        try:
            if self.log_file.exists():
                with open(self.log_file, 'w') as f:
                    pass  # Truncate file
                print(f"Cleared log file: {self.log_file}")
        except Exception as e:
            print(f"Error clearing log file: {e}")

    def run(self, max_empty_checks: int = 10):
        print(f"GAT Node Data Processor started")
        print(f"Watching log file: {self.log_file}")
        print(f"EWMA alpha: {self.ewma_alpha}")
        print(f"Poll interval: {self.poll_interval}s")
        print("-" * 60)

        # Clear log file at start to ensure clean slate
        self.clear_log_file()

        empty_count = 0

        while True:
            logs = self.read_and_remove_logs()

            if logs:
                empty_count = 0
                for log_entry in logs:
                    self.process_log_entry(log_entry)
                print(f"Processed {len(logs)} log entries. Total: {self.logs_processed}")
            else:
                empty_count += 1
                if empty_count >= max_empty_checks:
                    print(f"\nNo new logs for {max_empty_checks} checks. Simulation appears complete.")
                    break
                print(f"No new logs (empty check {empty_count}/{max_empty_checks})")

            time.sleep(self.poll_interval)

        # Clear log file at end
        self.clear_log_file()

        # Print final result
        print("\n" + "=" * 60)
        print("FINAL GAT NODE DATA (EWMA)")
        print("=" * 60)

        final_dict = self.get_final_dict()
        print(json.dumps(final_dict, indent=2))

        print("\n" + "=" * 60)
        print(f"Total logs processed: {self.logs_processed}")
        print(f"Number of APs: {len(final_dict['nodes'])}")
        print(f"Number of edges: {len(final_dict['edges'])}")
        print("=" * 60)

        return final_dict


def main():
    parser = argparse.ArgumentParser(description='GAT Node Data Processor')
    parser.add_argument('--log-file', type=str, default='./output_logs/gat_consumer_logs.json',
                        help='Path to the log file written by bayesian-optimizer-consumer (default: ./output_logs/gat_consumer_logs.json)')
    parser.add_argument('--ewma-alpha', type=float, default=0.3,
                        help='EWMA smoothing factor (0-1). Default: 0.3')
    parser.add_argument('--poll-interval', type=float, default=1.0,
                        help='Poll interval in seconds. Default: 1.0')
    parser.add_argument('--max-empty-checks', type=int, default=10,
                        help='Max consecutive empty reads before exit. Default: 10')

    args = parser.parse_args()

    processor = GATNodeDataProcessor(
        log_file=args.log_file,
        ewma_alpha=args.ewma_alpha,
        poll_interval=args.poll_interval
    )

    processor.run(max_empty_checks=args.max_empty_checks)


if __name__ == '__main__':
    main()