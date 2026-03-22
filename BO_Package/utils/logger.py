"""Centralized logging configuration

Replaces print statements with proper Python logging module.
Provides structured logging with levels, file and console handlers.
"""

import logging
import sys
from datetime import datetime
from pathlib import Path
from typing import Optional


def setup_logging(
    log_dir: str = "output_logs",
    log_level: int = logging.INFO,
    console_level: Optional[int] = None,
    file_level: Optional[int] = None
) -> logging.Logger:
    """Setup centralized logging for the optimizer

    Args:
        log_dir: Directory for log files
        log_level: Default logging level
        console_level: Console handler level (defaults to log_level)
        file_level: File handler level (defaults to log_level)

    Returns:
        Configured logger instance
    """
    # Create log directory
    log_path = Path(log_dir)
    log_path.mkdir(exist_ok=True)

    # Create logger
    logger = logging.getLogger('bayesian_optimizer')
    logger.setLevel(logging.DEBUG)  # Capture everything, filter at handler level

    # Remove existing handlers to avoid duplicates
    logger.handlers.clear()

    # Set handler levels
    if console_level is None:
        console_level = log_level
    if file_level is None:
        file_level = log_level

    # Create formatters
    detailed_formatter = logging.Formatter(
        fmt='%(asctime)s | %(levelname)-8s | %(name)s:%(funcName)s:%(lineno)d | %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )

    simple_formatter = logging.Formatter(
        fmt='%(levelname)-8s | %(message)s'
    )

    # Console handler (simpler format)
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(console_level)
    console_handler.setFormatter(simple_formatter)
    logger.addHandler(console_handler)

    # File handler (detailed format)
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    log_file = log_path / f"optuna_optimization_{timestamp}.log"
    file_handler = logging.FileHandler(log_file, mode='w', encoding='utf-8')
    file_handler.setLevel(file_level)
    file_handler.setFormatter(detailed_formatter)
    logger.addHandler(file_handler)

    # Log initialization
    logger.info("=" * 80)
    logger.info("Bayesian Optimizer - Logging Initialized")
    logger.info("=" * 80)
    logger.info(f"Log file: {log_file}")
    logger.info(f"Console level: {logging.getLevelName(console_level)}")
    logger.info(f"File level: {logging.getLevelName(file_level)}")
    logger.info("=" * 80)

    return logger


def get_logger(name: Optional[str] = None) -> logging.Logger:
    """Get a logger instance

    Args:
        name: Logger name (uses module name if None)

    Returns:
        Logger instance
    """
    if name:
        return logging.getLogger(f'bayesian_optimizer.{name}')
    return logging.getLogger('bayesian_optimizer')
