"""Main Network Optimizer

Orchestrates the Bayesian optimization process for WiFi network parameters.
"""

import logging
import time
import hashlib
from typing import List, Dict, Tuple, Optional
import optuna

from ..config.manager import ConfigManager
from ..config.models import OptimizerConfig, APConfig
from ..integrations.kafka_client import KafkaClient
from ..integrations.api_client import APIClient
from ..metrics.collector import MetricsCollector
from ..metrics.processor import MetricsProcessor
from ..safety.propensity import PropensityCalculator
from ..safety.doubly_robust import DoublyRobustEstimator
from ..core.objective import ObjectiveCalculator
from ..utils.time_utils import sim_time_to_clock, check_time_constraint

logger = logging.getLogger('bayesian_optimizer.optimizer')


class NetworkOptimizer:
    """Main optimizer orchestrating Bayesian optimization for WiFi networks"""

    def __init__(self, config: OptimizerConfig):
        """Initialize network optimizer

        Args:
            config: Optimizer configuration
        """
        self.config = config

        # Initialize components
        self.config_manager = ConfigManager(config)
        self.kafka_client = KafkaClient(
            broker=config.kafka_broker,
            topic=config.kafka_topic,
            key=config.kafka_key
        )
        self.api_client = APIClient(base_url=config.api_base_url)
        self.metrics_collector = MetricsCollector(log_dir=config.log_dir)
        self.metrics_processor = MetricsProcessor(ewma_alpha=config.ewma_alpha)
        self.propensity_calculator = PropensityCalculator(
            num_aps=config.num_aps,
            min_propensity=config.dr_min_propensity
        )
        self.dr_estimator = DoublyRobustEstimator(
            confidence_threshold=config.dr_confidence_threshold,
            bootstrap_samples=config.dr_bootstrap_samples,
            min_improvement=config.dr_min_improvement
        )
        self.objective_calculator = ObjectiveCalculator(
            weight_throughput=config.weight_throughput,
            weight_retry=config.weight_retry,
            weight_loss=config.weight_loss,
            weight_rtt=config.weight_rtt,
            max_p50_throughput=config.max_p50_throughput,
            max_p95_retry_rate=config.max_p95_retry_rate,
            max_p95_loss_rate=config.max_p95_loss_rate,
            max_p95_rtt=config.max_p95_rtt
        )

        # State tracking
        self.trial_count = 0
        self.trial_history: List[Dict] = []
        self.planner_ids: Dict[int, str] = {}
        self.baseline_objective: float = 0.0
        self.best_objective_so_far: float = 0.0
        self.config_churn_without_dr: int = 0
        self.config_churn_with_dr: int = 0

        # Load simulation config
        self.sim_config = self.config_manager.load_simulation_config()

    def initialize(self) -> bool:
        """Initialize connections to external systems

        Returns:
            True if initialization successful, False otherwise
        """
        logger.info("=" * 80)
        logger.info("INITIALIZING NETWORK OPTIMIZER")
        logger.info("=" * 80)

        # Connect to Kafka
        if not self.kafka_client.connect():
            logger.error("Failed to connect to Kafka")
            return False

        # Optionally check API health
        # self.api_client.health_check()  # Non-blocking

        logger.info("Initialization complete")
        logger.info("=" * 80)
        return True

    def objective(self, trial: optuna.Trial) -> float:
        """Optuna objective function

        This is the main entry point called by Optuna for each trial.

        Args:
            trial: Optuna trial object

        Returns:
            Objective value (higher is better), or -inf on failure
        """
        self.trial_count += 1

        # Generate planner ID for this trial
        planner_id = self._generate_planner_id(self.trial_count)

        logger.info("")
        logger.info("#" * 80)
        logger.info(f"PLANNER ID #{planner_id}")
        logger.info("#" * 80)

        try:
            # Get current simulation time
            start_sim_time = self.metrics_collector.get_current_sim_time()
            if start_sim_time is None:
                logger.error("Could not get current simulation time")
                return float('-inf')

            # Check time constraint
            if not check_time_constraint(
                start_sim_time,
                self.sim_config.clock_start_time,
                self.config.start_time_hour,
                self.config.end_time_hour
            ):
                hour, minute, second = sim_time_to_clock(
                    start_sim_time,
                    self.sim_config.clock_start_time,
                    self.config.start_time_hour
                )
                logger.warning("=" * 80)
                logger.warning("TIME CONSTRAINT EXCEEDED")
                logger.warning("=" * 80)
                logger.warning(f"Current clock time: {hour:02d}:{minute:02d}:{second:02d}")
                logger.warning(
                    f"Allowed window: {self.config.start_time_hour:02d}:00 - "
                    f"{self.config.end_time_hour:02d}:00"
                )
                logger.warning("Stopping optimization study...")
                logger.warning("=" * 80)
                trial.study.stop()
                return float('-inf')

        except Exception as e:
            logger.error(f"Error checking time constraint: {e}", exc_info=True)
            return float('-inf')

        try:
            # Suggest parameters for all APs (dynamic based on num_aps)
            channels = [
                self.config.available_channels[
                    trial.suggest_categorical(
                        f'channel_idx_{i}',
                        list(range(len(self.config.available_channels)))
                    )
                ]
                for i in range(self.config.num_aps)
            ]

            tx_power = [
                trial.suggest_float(
                    f'tx_power_{i}',
                    self.config.tx_power_range[0],
                    self.config.tx_power_range[1]
                )
                for i in range(self.config.num_aps)
            ]

            obss_pd = [
                trial.suggest_float(
                    f'obss_pd_{i}',
                    self.config.obss_pd_range[0],
                    self.config.obss_pd_range[1]
                )
                for i in range(self.config.num_aps)
            ]

            # Wait for network to settle (only for first trial)
            if self.trial_count == 1:
                logger.info(f"Waiting {self.config.settling_time}s for network to settle (Trial 1 only)")
                time.sleep(self.config.settling_time)

            # Log trial timing
            hour, minute, second = sim_time_to_clock(
                start_sim_time,
                self.sim_config.clock_start_time,
                self.config.start_time_hour
            )
            logger.info(f"Current simulation clock time: {hour:02d}:{minute:02d}:{second:02d}")

            # Send configuration
            ap_configs = [
                APConfig(
                    bssid=self.sim_config.ap_bssids[i],
                    channel=channels[i],
                    tx_power=tx_power[i],
                    obss_pd=obss_pd[i]
                )
                for i in range(self.config.num_aps)
            ]

            if not self.kafka_client.send_configuration(ap_configs, self.trial_count):
                logger.error("Failed to send configuration")
                return float('-inf')

            # Wait for configuration to take effect
            # Critical: Allow network to recover after channel switches
            logger.info("Waiting 18s for network stabilization...")
            time.sleep(18)

            # Collect logs
            target_sim_time = start_sim_time + self.config.evaluation_window
            logger.info("=" * 80)
            logger.info(f"COLLECTING LOGS FOR {self.config.evaluation_window}s sim time window")
            logger.info("=" * 80)

            logs = self.metrics_collector.collect_logs_until_sim_time(
                start_sim_time,
                target_sim_time
            )

            if not logs:
                logger.error("No logs collected - stopping study")
                trial.study.stop()
                return float('-inf')

            # Save sample log for debugging
            self.metrics_collector.save_sample_log(logs)

            # Clear log file
            self.metrics_collector.clear_log_file()

            # Calculate objective
            objective_value = self._calculate_objective(logs, channels)

            # Store trial history for DR estimation
            action_params = {
                'channels': channels,
                'tx_power': tx_power,
                'obss_pd': obss_pd
            }
            self.trial_history.append({
                'trial_num': self.trial_count,
                'action': action_params,
                'outcome': objective_value
            })

            # Store baseline (Trial 1)
            if self.trial_count == 1:
                self.baseline_objective = objective_value
                self.best_objective_so_far = objective_value
                logger.info(f"Baseline objective set: {objective_value:.4f}")

            # Check if we beat the previous best
            if objective_value > self.best_objective_so_far:
                self._handle_improvement(
                    trial,
                    objective_value,
                    action_params,
                    logs
                )

            # Reset processor states for next trial
            self.metrics_processor.reset_states()

            return objective_value

        except Exception as e:
            logger.error(f"Error in objective function: {e}", exc_info=True)
            return float('-inf')

    def _calculate_objective(self, logs: List[Dict], channels: List[int]) -> float:
        """Calculate objective function from logs

        Args:
            logs: List of log entries
            channels: Channel configuration

        Returns:
            Objective value
        """
        # Process logs with EWMA
        self.metrics_processor.process_logs(logs)

        # Calculate per-AP metrics
        per_ap_metrics = self.metrics_processor.calculate_per_ap_metrics()

        # Calculate objective
        objective_value, _, _, _, _ = self.objective_calculator.calculate(per_ap_metrics)

        return objective_value

    def _handle_improvement(
        self,
        trial: optuna.Trial,
        objective_value: float,
        action_params: Dict,
        logs: List[Dict]
    ) -> None:
        """Handle when a new best objective is achieved

        Args:
            trial: Optuna trial object
            objective_value: New objective value
            action_params: Action parameters that achieved this value
            logs: Log entries for this trial
        """
        logger.info("=" * 80)
        logger.info("NEW BEST OBJECTIVE ACHIEVED!")
        logger.info("=" * 80)
        logger.info(f"Previous Best: {self.best_objective_so_far:.4f}")
        logger.info(f"New Best: {objective_value:.4f}")
        logger.info(f"Improvement: {objective_value - self.best_objective_so_far:+.4f}")

        # Track config churn WITHOUT DR gate
        self.config_churn_without_dr += 1

        # Calculate propensity
        propensity = self.propensity_calculator.calculate_propensity(
            action_params,
            self.trial_history
        )

        # Run DR safety gate
        should_deploy, dr_uplift, lower_bound, upper_bound = self.dr_estimator.run_safety_gate(
            baseline_objective=self.baseline_objective,
            candidate_objective=objective_value,
            propensity=propensity,
            trial_history=self.trial_history
        )

        # Only deploy if DR gate approves
        if should_deploy:
            # Track config churn WITH DR gate
            self.config_churn_with_dr += 1

            # Calculate per-AP metrics for API report
            per_ap_metrics = self.metrics_processor.calculate_per_ap_metrics()
            per_ap_report = self.metrics_processor.build_per_ap_report(per_ap_metrics)

            # Build and send improvement report
            report = self.api_client.build_improvement_report(
                planner_id=int(self._generate_planner_id(self.trial_count), 16),
                baseline_objective=self.baseline_objective,
                candidate_objective=objective_value,
                dr_uplift=dr_uplift,
                confidence_interval_lower=lower_bound,
                confidence_interval_upper=upper_bound,
                should_deploy=should_deploy,
                per_ap_metrics=per_ap_report,
                additional_data={
                    'propensity': propensity,
                    'channels': action_params['channels'],
                    'tx_power': action_params['tx_power'],
                    'obss_pd': action_params['obss_pd']
                }
            )

            self.api_client.send_improvement_report(report, self.trial_count)

            # Update best objective
            self.best_objective_so_far = objective_value

        else:
            logger.info("DR safety gate blocked deployment - best objective unchanged")

    def _generate_planner_id(self, trial_number: int) -> str:
        """Generate a 12-digit hexadecimal planner ID from trial number

        Args:
            trial_number: The trial number to hash

        Returns:
            12-digit hexadecimal string
        """
        # Check if we already generated an ID for this trial
        if trial_number in self.planner_ids:
            return self.planner_ids[trial_number]

        # Create non-deterministic hash using trial number and timestamp
        hash_input = f"planner_trial_{trial_number}_time_{time.time()}".encode('utf-8')
        hash_obj = hashlib.sha256(hash_input)
        hex_digest = hash_obj.hexdigest()

        # Take first 12 characters and store it
        planner_id = hex_digest[:12]
        self.planner_ids[trial_number] = planner_id

        return planner_id

    def get_baseline_params(self) -> Dict:
        """Get baseline parameters for first trial

        Returns:
            Dictionary of baseline parameters for Optuna
        """
        return self.config_manager.get_baseline_config()

    def close(self) -> None:
        """Close connections and cleanup"""
        logger.info("Closing optimizer connections...")
        self.kafka_client.close()

        # Log final statistics
        logger.info("=" * 80)
        logger.info("OPTIMIZATION SUMMARY")
        logger.info("=" * 80)
        logger.info(f"Total trials: {self.trial_count}")
        logger.info(f"Baseline objective: {self.baseline_objective:.4f}")
        logger.info(f"Best objective: {self.best_objective_so_far:.4f}")
        logger.info(f"Total improvement: {self.best_objective_so_far - self.baseline_objective:+.4f}")
        logger.info(f"Config churn without DR: {self.config_churn_without_dr}")
        logger.info(f"Config churn with DR: {self.config_churn_with_dr}")
        logger.info(f"DR gate reduction: {self.config_churn_without_dr - self.config_churn_with_dr}")
        logger.info("=" * 80)
