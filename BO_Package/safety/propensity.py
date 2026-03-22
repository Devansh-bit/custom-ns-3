"""Propensity score calculation

Estimates the probability of selecting a given configuration based on
trial similarity in the parameter space.
"""

import logging
import numpy as np
from typing import Dict, List

logger = logging.getLogger('bayesian_optimizer.safety.propensity')


class PropensityCalculator:
    """Calculates propensity scores based on trial similarity"""

    def __init__(self, num_aps: int, min_propensity: float = 1e-10):
        """Initialize propensity calculator

        Args:
            num_aps: Number of APs in the system
            min_propensity: Minimum propensity score to avoid division by zero
        """
        self.num_aps = num_aps
        self.min_propensity = min_propensity

    def calculate_propensity(
        self,
        current_action: Dict[str, List],
        trial_history: List[Dict]
    ) -> float:
        """Calculate propensity score π(a*|s) based on trial similarity

        Uses normalized parameter space distance to estimate how likely
        the current configuration would be selected.

        Args:
            current_action: Dictionary with 'channels', 'tx_power', 'obss_pd' lists
            trial_history: List of historical trials with 'action' and 'outcome'

        Returns:
            Propensity score in range [0.1, 1.0]
        """
        if not trial_history or len(trial_history) <= 1:
            # No history yet, return neutral propensity
            logger.debug("No trial history, using neutral propensity 0.5")
            return 0.5

        # Calculate similarity to each historical trial
        # Using normalized L2 distance in parameter space
        similarities = []

        for hist_trial in trial_history[:-1]:  # Exclude current trial
            hist_action = hist_trial.get('action', {})

            # Channel similarity (discrete): fraction of APs with same channel
            channel_matches = sum(
                1 for i in range(self.num_aps)
                if current_action['channels'][i] == hist_action.get('channels', [])[i]
            )
            channel_sim = channel_matches / self.num_aps if self.num_aps > 0 else 0.0

            # TX Power similarity (continuous): normalized distance
            # Range: [13, 25] dBm → span of 12 dBm
            tx_diffs = [
                abs(current_action['tx_power'][i] - hist_action.get('tx_power', [0]*self.num_aps)[i]) / 12.0
                for i in range(self.num_aps)
            ]
            tx_sim = 1.0 - (sum(tx_diffs) / self.num_aps) if self.num_aps > 0 else 0.0

            # OBSS-PD similarity (continuous): normalized distance
            # Range: [-80, -60] dBm → span of 20 dBm
            obss_diffs = [
                abs(current_action['obss_pd'][i] - hist_action.get('obss_pd', [0]*self.num_aps)[i]) / 20.0
                for i in range(self.num_aps)
            ]
            obss_sim = 1.0 - (sum(obss_diffs) / self.num_aps) if self.num_aps > 0 else 0.0

            # Combined similarity (average of all three dimensions)
            overall_sim = (channel_sim + tx_sim + obss_sim) / 3.0
            similarities.append(overall_sim)

        # Propensity = average similarity to historical trials
        # Higher similarity → higher propensity (more likely to be selected)
        avg_similarity = np.mean(similarities) if similarities else 0.5

        # Transform to propensity: add small constant to avoid zero
        # π(a|s) = 0.1 + 0.9 × similarity
        # This gives range [0.1, 1.0] which is numerically stable
        propensity = 0.1 + 0.9 * avg_similarity

        propensity = max(propensity, self.min_propensity)

        logger.debug(
            f"Propensity score: {propensity:.6f} "
            f"(avg similarity: {avg_similarity:.4f} over {len(similarities)} trials)"
        )

        return propensity

    def calculate_fallback_propensity(
        self,
        available_channels: List[int],
        tx_power_range: tuple,
        obss_pd_range: tuple
    ) -> float:
        """Calculate fallback propensity based on parameter space size

        Used when trial history is insufficient.

        Args:
            available_channels: List of available channels
            tx_power_range: (min, max) TX power in dBm
            obss_pd_range: (min, max) OBSS-PD in dBm

        Returns:
            Propensity score based on uniform distribution assumption
        """
        # Assume uniform distribution over parameter space
        # Propensity for each AP's parameters
        channel_propensity = 1.0 / len(available_channels) if available_channels else 1.0
        tx_range = tx_power_range[1] - tx_power_range[0]
        tx_propensity = 1.0 / tx_range if tx_range > 0 else 1.0
        obss_range = obss_pd_range[1] - obss_pd_range[0]
        obss_propensity = 1.0 / abs(obss_range) if obss_range != 0 else 1.0

        # Combined propensity for all APs (product over independent choices)
        propensity = 1.0
        for _ in range(self.num_aps):
            propensity *= channel_propensity * tx_propensity * obss_propensity

        # Ensure minimum propensity
        propensity = max(propensity, self.min_propensity)

        logger.debug(
            f"Fallback propensity: {propensity:.10f} "
            f"(uniform over {len(available_channels)} channels, "
            f"{tx_range} dBm TX, {abs(obss_range)} dBm OBSS-PD, {self.num_aps} APs)"
        )

        return propensity
