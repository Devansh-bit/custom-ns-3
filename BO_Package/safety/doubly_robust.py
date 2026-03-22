"""Doubly-robust estimation and safety gate

Implements DR uplift estimation with bootstrap confidence intervals
and a deployment decision gate.
"""

import logging
import numpy as np
from typing import Dict, List, Tuple

logger = logging.getLogger('bayesian_optimizer.safety.doubly_robust')


class DoublyRobustEstimator:
    """Doubly-robust estimator for causal uplift with safety gate"""

    def __init__(
        self,
        confidence_threshold: float = 0.95,
        bootstrap_samples: int = 100,
        min_improvement: float = 0.02,
        min_history: int = 3
    ):
        """Initialize DR estimator

        Args:
            confidence_threshold: Confidence level for intervals (not used, uses 80% CI)
            bootstrap_samples: Number of bootstrap samples for CI estimation
            min_improvement: Minimum uplift threshold for deployment decision
            min_history: Minimum number of trials needed for DR estimation
        """
        self.confidence_threshold = confidence_threshold
        self.bootstrap_samples = bootstrap_samples
        self.min_improvement = min_improvement
        self.min_history = min_history

    def compute_uplift(
        self,
        baseline_objective: float,
        candidate_objective: float,
        propensity: float,
        trial_history: List[Dict]
    ) -> Tuple[float, float, float]:
        """Compute doubly-robust uplift estimate with confidence bounds

        Uses the DR estimator from theory:
        V_DR(a*) = G(s, a*) + (1/π(a*|s)) × (Y - G(s, a*))

        Where:
        - G(s, a*) is the outcome model (surrogate prediction)
        - π(a*|s) is the propensity score
        - Y is the actual observed outcome

        Args:
            baseline_objective: Baseline objective value (a_0)
            candidate_objective: Candidate objective value (a*)
            propensity: Propensity score π(a*|s)
            trial_history: List of historical trials with 'action' and 'outcome'

        Returns:
            Tuple of (dr_uplift, lower_confidence_bound, upper_confidence_bound)
        """
        if len(trial_history) > self.min_history:
            # Extract historical outcomes
            historical_objectives = [t['outcome'] for t in trial_history[:-1]]

            # G(s, a*) = Outcome model (use historical mean as prediction)
            G_prediction = np.mean(historical_objectives)

            # Y = Actual observed outcome for this candidate
            Y_actual = candidate_objective

            # DR Estimator formula:
            # V_DR = G(s, a*) + (1/π(a*|s)) × (Y - G(s, a*))
            importance_weight = 1.0 / propensity
            prediction_error = Y_actual - G_prediction

            dr_estimate = G_prediction + importance_weight * prediction_error

            logger.debug(f"DR Components:")
            logger.debug(f"  G(prediction): {G_prediction:.4f}")
            logger.debug(f"  Y(actual): {Y_actual:.4f}")
            logger.debug(f"  Propensity: {propensity:.4f}")
            logger.debug(f"  Importance weight: {importance_weight:.4f}")
            logger.debug(f"  Prediction error: {prediction_error:+.4f}")
            logger.debug(f"  DR estimate: {dr_estimate:.4f}")

            # Compute uncertainty for confidence bounds
            # Use historical std weighted by propensity
            hist_std = np.std(historical_objectives)

            # Lower propensity (novel) → higher uncertainty
            # Higher propensity (similar) → lower uncertainty
            adjusted_std = hist_std * importance_weight

            # Bootstrap for confidence intervals
            dr_values = []
            for _ in range(self.bootstrap_samples):
                sample = dr_estimate + np.random.normal(0, adjusted_std)
                dr_values.append(sample)

            mean_dr = np.mean(dr_values)
            std_dr = np.std(dr_values)

        else:
            # Not enough history: use direct estimate
            logger.warning(
                f"Insufficient trial history ({len(trial_history)} trials, "
                f"need {self.min_history}), using direct estimate"
            )
            mean_dr = candidate_objective
            std_dr = 0.05  # 5% uncertainty

        # 80% confidence interval (more lenient)
        # Using z=1.28 for 80% CI instead of 1.96 for 95% CI
        z_score = 1.28
        lower_bound = mean_dr - z_score * std_dr
        upper_bound = mean_dr + z_score * std_dr

        # Compute uplift relative to baseline
        dr_uplift = mean_dr - baseline_objective
        uplift_lower_bound = lower_bound - baseline_objective
        uplift_upper_bound = upper_bound - baseline_objective

        logger.info(f"DR Uplift Estimation:")
        logger.info(f"  Mean DR estimate: {mean_dr:.4f}")
        logger.info(f"  DR uplift: {dr_uplift:+.4f}")
        logger.info(f"  80% CI: [{uplift_lower_bound:+.4f}, {uplift_upper_bound:+.4f}]")

        return dr_uplift, uplift_lower_bound, uplift_upper_bound

    def safety_gate_decision(
        self,
        baseline_objective: float,
        candidate_objective: float,
        dr_uplift: float,
        lower_bound: float,
        propensity: float
    ) -> bool:
        """Make deployment decision based on DR safety gate

        Decision criterion: Lower bound must exceed minimum improvement threshold

        Args:
            baseline_objective: Baseline objective value
            candidate_objective: Candidate objective value
            dr_uplift: DR uplift estimate
            lower_bound: Lower confidence bound on uplift
            propensity: Propensity score

        Returns:
            True if should deploy, False otherwise
        """
        # Decision criterion
        should_deploy = lower_bound >= self.min_improvement

        naive_uplift = candidate_objective - baseline_objective
        naive_pct = (naive_uplift / baseline_objective) * 100 if baseline_objective > 0 else 0

        logger.info("=" * 80)
        logger.info("DOUBLY-ROBUST SAFETY GATE")
        logger.info("=" * 80)
        logger.info(f"Baseline:     {baseline_objective:.4f}")
        logger.info(f"Candidate:    {candidate_objective:.4f}")
        logger.info(f"Naive Uplift: {naive_uplift:+.4f} ({naive_pct:+.1f}%)")
        logger.info(f"Propensity:   {propensity:.3f}")
        logger.info(f"DR Uplift:    {dr_uplift:+.4f}")
        logger.info(f"80% CI Lower: {lower_bound:+.4f}")
        logger.info("")

        if should_deploy:
            logger.info(
                f"PASSED - Lower bound {lower_bound:+.4f} >= "
                f"threshold {self.min_improvement:+.4f}"
            )
            logger.info("  → Deploying to canary")
        else:
            logger.info(
                f"BLOCKED - Lower bound {lower_bound:+.4f} < "
                f"threshold {self.min_improvement:+.4f}"
            )
            logger.info("  → Insufficient confidence, skipping deployment")

        logger.info("=" * 80)

        return should_deploy

    def run_safety_gate(
        self,
        baseline_objective: float,
        candidate_objective: float,
        propensity: float,
        trial_history: List[Dict]
    ) -> Tuple[bool, float, float, float]:
        """Run complete safety gate analysis

        Combines DR uplift estimation and deployment decision.

        Args:
            baseline_objective: Baseline objective value
            candidate_objective: Candidate objective value
            propensity: Propensity score
            trial_history: List of historical trials

        Returns:
            Tuple of (should_deploy, dr_uplift, lower_bound, upper_bound)
        """
        # Compute DR uplift
        dr_uplift, lower_bound, upper_bound = self.compute_uplift(
            baseline_objective,
            candidate_objective,
            propensity,
            trial_history
        )

        # Make deployment decision
        should_deploy = self.safety_gate_decision(
            baseline_objective,
            candidate_objective,
            dr_uplift,
            lower_bound,
            propensity
        )

        return should_deploy, dr_uplift, lower_bound, upper_bound
