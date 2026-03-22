#!/usr/bin/env python3
"""
Configuration for Spectrum UDP Server
"""

# Server settings
DEFAULT_HOST = '0.0.0.0'  # Listen on all interfaces
DEFAULT_PORT = 9000
MAX_PACKET_SIZE = 65536  # Maximum UDP packet size

# Data processing
QUEUE_MAX_SIZE = 1000  # Maximum queue size per AP
SAVE_TO_FILE = True
OUTPUT_DIR = 'spectrum_data'

# Logging
LOG_LEVEL = 'INFO'  # DEBUG, INFO, WARNING, ERROR
LOG_FORMAT = '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
LOG_FILE = 'spectrum_server.log'

# CNN Integration (placeholder for future use)
ENABLE_CNN = False
CNN_MODEL_PATH = 'model.pth'
CNN_BATCH_SIZE = 32
