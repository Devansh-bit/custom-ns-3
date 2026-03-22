#!/usr/bin/env python3
"""
Bayesian Optimizer - Main Entry Point

This is the primary entry point for running the WiFi network Bayesian optimizer.

Usage:
    python3BO_Package/main.py --num-aps 6 --study-time 10000 --evaluation-window 5

    Required arguments:
        --num-aps: Number of APs in the config file (defines search space size)

    Optional arguments:
        --study-time: Study duration in minutes (default: 10000)
        --evaluation-window: Evaluation window in sim seconds (default: 5)
        --settling-time: Network settling time in real seconds (default: 5)
        --config-file: Path to config file (default: config-simulation.json)
        --api-url: API base URL (default: http://localhost:8000)
        --log-level: Logging level (default: INFO)

Example:
    # For 6 APs with 10000 minute study time
    python3 BO_Package/main.py --num-aps 6 --study-time 10000

    # For 4 APs with custom settings
    python3 BO_Package/main.py --num-aps 4 --study-time 5000 --evaluation-window 10
"""

import argparse
import sys
import os
import logging
import optuna

# Add parent directory to path if running as script
if __name__ == '__main__':
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from BO_Package.config.models import OptimizerConfig
from BO_Package.core.optimizer import NetworkOptimizer
from BO_Package.utils.logger import setup_logging


def parse_arguments():
    """Parse command line arguments

    Returns:
        Parsed arguments
    """
    parser = argparse.ArgumentParser(
        description='Bayesian Optimizer for WiFi Network Parameter Tuning',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Run with 6 APs (check config file for actual AP count)
  python BO_Package/main.py --num-aps 6

  # Run with 4 APs and custom evaluation window
  python BO_Package/main.py --num-aps 4 --evaluation-window 10

  # Run with 6 APs and custom study time
  python BO_Package/main.py --num-aps 6 --study-time 5000
        """
    )

    # Required arguments
    parser.add_argument(
        '--num-aps',
        type=int,
        required=True,
        help='Number of APs (defines search space size). Check your config file and set this accordingly.'
    )

    # Optional arguments - Study configuration
    parser.add_argument(
        '--study-time',
        type=int,
        default=10000,
        help='Study duration in minutes (default: 10000)'
    )

    parser.add_argument(
        '--evaluation-window',
        type=int,
        default=5,
        help='Evaluation window in simulation seconds (default: 5)'
    )

    parser.add_argument(
        '--settling-time',
        type=int,
        default=60,
        help='Network settling time in real seconds for Trial 1 (default: 60)'
    )

    # Optional arguments - File paths
    parser.add_argument(
        '--config-file',
        type=str,
        default='config-simulation.json',
        help='Path to simulation config file (default: config-simulation.json)'
    )

    parser.add_argument(
        '--log-dir',
        type=str,
        default='output_logs',
        help='Directory for log files (default: output_logs)'
    )

    # Optional arguments - External systems
    parser.add_argument(
        '--kafka-broker',
        type=str,
        default='localhost:9092',
        help='Kafka broker address (default: localhost:9092)'
    )

    parser.add_argument(
        '--api-url',
        type=str,
        default='http://localhost:8000',
        help='API base URL (default: http://localhost:8000)'
    )

    # Optional arguments - Optuna
    parser.add_argument(
        '--optuna-seed',
        type=int,
        default=42,
        help='Optuna random seed (default: 42)'
    )

    # Optional arguments - Logging
    parser.add_argument(
        '--log-level',
        type=str,
        default='INFO',
        choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
        help='Logging level (default: INFO)'
    )

    return parser.parse_args()


def main():
    """Main entry point"""
    # Parse arguments
    args = parse_arguments()

    # Setup logging
    log_level = getattr(logging, args.log_level)
    logger = setup_logging(
        log_dir=args.log_dir,
        log_level=log_level,
        console_level=log_level,
        file_level=logging.DEBUG  # Always capture DEBUG in file
    )

    logger.info("=" * 80)
    logger.info("BAYESIAN OPTIMIZER FOR WIFI NETWORK OPTIMIZATION")
    logger.info("=" * 80)
    logger.info(f"Configuration:")
    logger.info(f"  Number of APs: {args.num_aps}")
    logger.info(f"  Study time: {args.study_time} minutes")
    logger.info(f"  Evaluation window: {args.evaluation_window} seconds (sim time)")
    logger.info(f"  Settling time: {args.settling_time} seconds (real time, Trial 1 only)")
    logger.info(f"  Config file: {args.config_file}")
    logger.info(f"  Kafka broker: {args.kafka_broker}")
    logger.info(f"  API URL: {args.api_url}")
    logger.info(f"  Log level: {args.log_level}")
    logger.info("=" * 80)

    try:
        # Create optimizer configuration
        optimizer_config = OptimizerConfig(
            num_aps=args.num_aps,
            study_time=args.study_time,
            evaluation_window=args.evaluation_window,
            settling_time=args.settling_time,
            config_file=args.config_file,
            log_dir=args.log_dir,
            kafka_broker=args.kafka_broker,
            api_base_url=args.api_url,
            optuna_seed=args.optuna_seed
        )

        # Create optimizer
        optimizer = NetworkOptimizer(config=optimizer_config)

        # Initialize connections
        if not optimizer.initialize():
            logger.error("Failed to initialize optimizer")
            return 1

        # Create Optuna study
        logger.info("Creating Optuna study...")
        study = optuna.create_study(
            direction='maximize',
            sampler=optuna.samplers.TPESampler(seed=args.optuna_seed)
        )

        # Get baseline parameters
        baseline_params = optimizer.get_baseline_params()
        logger.info("Enqueuing baseline configuration for Trial 1...")
        logger.info(f"Baseline params: {baseline_params}")
        study.enqueue_trial(baseline_params)

        # Run optimization
        logger.info("=" * 80)
        logger.info(f"STARTING OPTIMIZATION (study time: {args.study_time} minutes)")
        logger.info("=" * 80)

        # Convert study time from minutes to seconds for Optuna timeout
        study_timeout_seconds = args.study_time * 60

        study.optimize(
            optimizer.objective,
            timeout=study_timeout_seconds
        )

        # Cleanup
        optimizer.close()

        logger.info("=" * 80)
        logger.info("OPTIMIZATION COMPLETE")
        logger.info("=" * 80)
        logger.info(f"Best objective value: {study.best_value:.4f}")
        logger.info(f"Best parameters: {study.best_params}")
        logger.info("=" * 80)

        return 0

    except KeyboardInterrupt:
        logger.warning("")
        logger.warning("=" * 80)
        logger.warning("OPTIMIZATION INTERRUPTED BY USER")
        logger.warning("=" * 80)
        return 130  # Standard exit code for SIGINT

    except Exception as e:
        logger.error("=" * 80)
        logger.error("FATAL ERROR")
        logger.error("=" * 80)
        logger.error(f"Error: {e}", exc_info=True)
        logger.error("=" * 80)
        return 1


if __name__ == '__main__':
    sys.exit(main())
