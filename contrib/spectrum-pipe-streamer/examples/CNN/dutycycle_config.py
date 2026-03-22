import numpy as np
from typing import Dict

# Frequency range in MHz for normalization
FREQ_MIN_MHZ = 2400.0
FREQ_MAX_MHZ = 2500.0
FREQ_RANGE_MHZ = FREQ_MAX_MHZ - FREQ_MIN_MHZ  # 100 MHz


# Technology bin layout (row ordering): WiFi (14), Zigbee (16), Cordless (1), Bluetooth (40), Microwave (1)
TECH_TO_BINS = {
    "wifi": 14,
    "zigbee": 16,
    "cordless": 1,
    "bluetooth": 40,
    "microwave": 1,
}

TECH_ORDER = ["wifi", "zigbee", "cordless", "bluetooth", "microwave"]


def _tech_start_idx() -> Dict[str, int]:
    starts: Dict[str, int] = {}
    idx = 0
    for tech in TECH_ORDER:
        starts[tech] = idx
        idx += TECH_TO_BINS[tech]
    return starts


TECH_START_IDX = _tech_start_idx()
TOTAL_ROWS = sum(TECH_TO_BINS.values())  # 72


def _parse_ghz_str(s: str) -> float:
    """Parse central frequency like '2.450 GHz' to MHz float."""
    s = str(s).strip().lower().replace("ghz", "").strip()
    try:
        ghz = float(s)
    except ValueError:
        return 0.0
    return ghz * 1000.0


def _parse_mhz_str(s: str) -> float:
    """Parse bandwidth like '20 MHz' to MHz float."""
    s = str(s).strip().lower().replace("mhz", "").strip()
    try:
        return float(s)
    except ValueError:
        return 0.0


def _bin_index(cf_mhz: float, bins: int) -> int:
    if bins == 1:
        return 0
    # Clamp to [FREQ_MIN_MHZ, FREQ_MAX_MHZ)
    cf_mhz_clamped = min(max(cf_mhz, FREQ_MIN_MHZ), FREQ_MAX_MHZ - 1e-9)
    width = FREQ_RANGE_MHZ / float(bins)
    rel = (cf_mhz_clamped - FREQ_MIN_MHZ) / width
    idx = int(rel)
    if idx < 0:
        idx = 0
    if idx > bins - 1:
        idx = bins - 1
    return idx


def _norm_cf(cf_mhz: float) -> float:
    return (cf_mhz - FREQ_MIN_MHZ) / FREQ_RANGE_MHZ


def _norm_bw(bw_mhz: float) -> float:
    # Normalize by the full span (100 MHz)
    return bw_mhz / FREQ_RANGE_MHZ


# Defaults for time-windowing (seconds)
DURATION = 31.0
DEFAULT_WINDOW_SIZE = 0.1
DEFAULT_STRIDE = 0.01

# import numpy as np
# from typing import Dict

# # Frequency range in MHz for normalization
# FREQ_MIN_MHZ = 5100.0
# FREQ_MAX_MHZ = 6000.0
# FREQ_RANGE_MHZ = FREQ_MAX_MHZ - FREQ_MIN_MHZ  # 100 MHz


# # Technology bin layout (row ordering): WiFi (14), Zigbee (16), Cordless (1), Bluetooth (40), Microwave (1)
# TECH_TO_BINS = {
#     "wifi": 14,
#     "radar": 1
# }

# TECH_ORDER = ["wifi", "radar"]


# def _tech_start_idx() -> Dict[str, int]:
#     starts: Dict[str, int] = {}
#     idx = 0
#     for tech in TECH_ORDER:
#         starts[tech] = idx
#         idx += TECH_TO_BINS[tech]
#     return starts


# TECH_START_IDX = _tech_start_idx()
# TOTAL_ROWS = sum(TECH_TO_BINS.values())  # 72


# def _parse_ghz_str(s: str) -> float:
#     """Parse central frequency like '2.450 GHz' to MHz float."""
#     s = str(s).strip().lower().replace("ghz", "").strip()
#     try:
#         ghz = float(s)
#     except ValueError:
#         return 0.0
#     return ghz * 1000.0


# def _parse_mhz_str(s: str) -> float:
#     """Parse bandwidth like '20 MHz' to MHz float."""
#     s = str(s).strip().lower().replace("mhz", "").strip()
#     try:
#         return float(s)
#     except ValueError:
#         return 0.0


# def _bin_index(cf_mhz: float, bins: int) -> int:
#     if bins == 1:
#         return 0
#     # Clamp to [FREQ_MIN_MHZ, FREQ_MAX_MHZ)
#     cf_mhz_clamped = min(max(cf_mhz, FREQ_MIN_MHZ), FREQ_MAX_MHZ - 1e-9)
#     width = FREQ_RANGE_MHZ / float(bins)
#     rel = (cf_mhz_clamped - FREQ_MIN_MHZ) / width
#     idx = int(rel)
#     if idx < 0:
#         idx = 0
#     if idx > bins - 1:
#         idx = bins - 1
#     return idx


# def _norm_cf(cf_mhz: float) -> float:
#     return (cf_mhz - FREQ_MIN_MHZ) / FREQ_RANGE_MHZ


# def _norm_bw(bw_mhz: float) -> float:
#     # Normalize by the full span (100 MHz)
#     return bw_mhz / FREQ_RANGE_MHZ


# # Defaults for time-windowing (seconds)
# DURATION = 4
# DEFAULT_WINDOW_SIZE = 0.1
# DEFAULT_STRIDE = 0.01