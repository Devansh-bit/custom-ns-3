#!/usr/bin/env python3
"""
Send sensing snapshots derived from combined_detections_new.json to the
/sensing API endpoint, following the schema defined in deliverable_apis/README.md.

Features:
- Entries are sorted lexicographically by their simulation time.
- Simulation time is translated to milliseconds for every payload.
- Inter-send delays follow the same intervals as provided by sim_time.
"""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Sequence

import requests


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stream combined detections to the /sensing endpoint."
    )
    parser.add_argument(
        "--json-path",
        default=(
            Path(__file__).parent / "combined_detections_new.json"
        ).as_posix(),
        help="Path to combined_detections_new.json",
    )
    parser.add_argument(
        "--base-url",
        default="http://localhost:8000",
        help="Base URL of the API server (default: http://localhost:8000)",
    )
    parser.add_argument(
        "--endpoint",
        default="/sensing",
        help="Endpoint path for posting sensing snapshots (default: /sensing)",
    )
    parser.add_argument(
        "--respect-intervals",
        action="store_true",
        help="Sleep between payloads using the delta derived from sim_time.",
    )
    parser.add_argument(
        "--interval-scale",
        type=float,
        default=1.0,
        help="Scale factor for inter-snapshot sleep duration (seconds).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print payloads instead of sending them.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=10.0,
        help="Request timeout in seconds (default: 10s).",
    )
    return parser.parse_args()


def load_detections(json_path: str | Path) -> List[Dict[str, Any]]:
    with Path(json_path).open() as f:
        data = json.load(f)
    if not isinstance(data, Sequence):
        raise ValueError("combined detections file must contain a list")
    return sorted(data, key=lambda item: (f"{item.get('sim_time', 0):020.6f}", item.get("unix_time", 0)))


def band_to_enum(band_label: str) -> str:
    if band_label.lower().startswith("2.4"):
        return "BAND_2_4GHZ"
    if band_label.lower().startswith("5"):
        return "BAND_5GHZ"
    return band_label.upper()


def to_iso8601(unix_ts: float) -> str:
    return datetime.fromtimestamp(unix_ts, tz=timezone.utc).isoformat()


def to_sim_time_ms(sim_time: float) -> int:
    """Interpret sim_time as ms if it's effectively an integer, else seconds."""
    if sim_time is None:
        return 0
    if abs(sim_time - round(sim_time)) < 1e-6:
        return int(round(sim_time))
    return int(round(sim_time * 1000))


def build_channels(entry: Dict[str, Any], sim_time_ms: int) -> List[Dict[str, Any]]:
    detections = entry.get("detections") or []
    channels: List[Dict[str, Any]] = []
    for det in detections:
        channels.append(
            {
                "channel": entry.get("channel"),
                "band": band_to_enum(entry.get("band", "")),
                "interference_type": det.get("technology", "UNKNOWN").upper(),
                "confidence_score": det.get("confidence", 0.0),
                "interference_bandwidth": det.get("bandwidth_mhz")
                if det.get("bandwidth_mhz") is not None
                else (det.get("bandwidth_hz", 0.0) / 1e6),
                "interference_center_frequency": det.get("center_frequency_mhz")
                if det.get("center_frequency_mhz") is not None
                else (det.get("center_frequency_hz", 0.0) / 1e6),
                "interference_duty_cycle": det.get("duty_cycle"),
                "sim_time_ms": sim_time_ms,
            }
        )
    if not channels:
        channels.append(
            {
                "channel": entry.get("channel"),
                "band": band_to_enum(entry.get("band", "")),
                "interference_type": "UNKNOWN",
                "confidence_score": 0.0,
                "interference_bandwidth": None,
                "interference_center_frequency": None,
                "interference_duty_cycle": None,
                "sim_time_ms": sim_time_ms,
            }
        )
    return channels


def build_payload(entry: Dict[str, Any]) -> Dict[str, Any]:
    sim_time_ms = to_sim_time_ms(entry.get("sim_time", 0.0))
    detections_payload: List[Dict[str, Any]] = []
    detections = entry.get("detections") or []
    for det in detections:
        detections_payload.append(
            {
                "channel": entry.get("channel"),
                "band": entry.get("band"),
                "technology": det.get("technology"),
                # "center_frequency_hz": det.get("center_frequency_hz"),
                "center_frequency_mhz": det.get("center_frequency_mhz"),
                # "bandwidth_hz": det.get("bandwidth_hz"),
                "bandwidth_mhz": det.get("bandwidth_mhz"),
                "confidence": det.get("confidence"),
                "duty_cycle": det.get("duty_cycle"),
            }
        )
    payload = {
        "unix_time": entry.get("unix_time"),
        "sim_time": sim_time_ms,
        "ap_macaddr": entry.get("ap_macaddr"),
        "detections": detections_payload,
    }
    return payload


def stream_payloads(
    entries: Sequence[Dict[str, Any]],
    base_url: str,
    endpoint: str,
    *,
    respect_intervals: bool,
    interval_scale: float,
    timeout: float,
    dry_run: bool,
) -> None:
    url = f"{base_url.rstrip('/')}{endpoint}"
    prev_sim_time = None
    for idx, entry in enumerate(entries):
        if respect_intervals and prev_sim_time is not None:
            delta = entry.get("sim_time", 0.0) - prev_sim_time
            if delta > 0:
                time.sleep(delta * interval_scale)
        prev_sim_time = entry.get("sim_time", 0.0)

        payload = build_payload(entry)
        if dry_run:
            print(f"[DRY-RUN] Payload #{idx+1} -> {url}")
            print(json.dumps(payload, indent=2))
            continue

        response = requests.post(url, json=payload, timeout=timeout)
        try:
            response.raise_for_status()
        except requests.HTTPError as exc:
            raise RuntimeError(
                f"Failed to POST payload #{idx+1} (status {response.status_code}): {response.text}"
            ) from exc
        print(f"Sent payload #{idx+1} | sim_time_ms={payload['sim_time']} | status={response.status_code}")


def main() -> None:
    args = parse_args()
    entries = load_detections(args.json_path)
    stream_payloads(
        entries,
        args.base_url,
        args.endpoint,
        respect_intervals=args.respect_intervals,
        interval_scale=args.interval_scale,
        timeout=args.timeout,
        dry_run=args.dry_run,
    )


if __name__ == "__main__":
    main()

