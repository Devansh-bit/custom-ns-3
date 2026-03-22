"""
Configuration file for spectrum sensing pipeline.
Contains all band-specific parameters for 2.4 GHz and 5 GHz bands.
"""

# ===== Shared EWMA Parameters =====
BASELINE = -96.92
OFFSET = 5
ALPHA_FAST = 0.25
ALPHA_SLOW = 0.05

# ===== Time Parameters =====
TIME_RES = 0.001    # 1 ms
PAUSE_DURATION = 0.1  # hold logging 100 ms (CNN dwell)

# ===== Canvas Parameters =====
T = 50  # frames per canvas

# ===== Band-Specific Configuration =====
BAND_CONFIG = {
    0: {  # 2.4 GHz
        "name": "2.4GHz",
        "freq_start": 2400e6,          # 2.4 GHz in Hz
        "freq_resolution": 100e3,      # 100 kHz resolution
        "model_path": "bay2.pt",       # CNN model weights file
        "tech_names": ["WiFi", "BT", "Microwave", "Zigbee"],  # Detection classes
    },
    1: {  # 5 GHz
        "name": "5GHz",
        "freq_start": 5150e6,          # 5.15 GHz in Hz
        "freq_resolution": 1000e3,     # 1 MHz resolution
        "model_path": "5ghz.pt",       # CNN model weights file
        "tech_names": ["WiFi", "Radar", "Microwave", "Zigbee"],  # Detection classes
    }
}

# ===== Socket Configuration =====
HOST = "127.0.0.1"
CANVAS_PORT = 65430
FEEDBACK_PORT = 65431

# ===== Minimum Dwell Time =====
MIN_DWELL_TIME = 1.0  # seconds

# ===== CNN Buffer Size =====
CNN_BUFFER_SIZE = 20  # frames to collect before CNN inference

