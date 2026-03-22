"""REST API client for improvement reporting

Handles communication with the RRM API endpoint.
"""

import logging
import requests
from typing import Dict, Any, Optional

logger = logging.getLogger('bayesian_optimizer.api')


class APIClient:
    """REST API client for sending improvement reports"""

    def __init__(
        self,
        base_url: str = "http://localhost:8000",
        timeout: int = 10
    ):
        """Initialize API client

        Args:
            base_url: Base URL for API
            timeout: Request timeout in seconds
        """
        self.base_url = base_url.rstrip('/')
        self.timeout = timeout

    def send_improvement_report(
        self,
        report: Dict[str, Any],
        trial_number: int = 0
    ) -> bool:
        """Send improvement report to /rrm endpoint

        Args:
            report: Improvement report data
            trial_number: Current trial number (for logging)

        Returns:
            True if sent successfully, False otherwise
        """
        endpoint = f"{self.base_url}/rrm"

        try:
            logger.info(f"Sending improvement report for Trial {trial_number}")
            logger.debug(f"  Endpoint: {endpoint}")

            response = requests.post(
                endpoint,
                json=report,
                headers={'Content-Type': 'application/json'},
                timeout=self.timeout
            )

            response.raise_for_status()

            logger.info(f"Improvement report sent successfully (status: {response.status_code})")

            # Log key metrics from report
            if 'dr_uplift' in report:
                logger.info(f"  DR Uplift: {report['dr_uplift']:+.4f}")
            if 'confidence_interval_lower' in report:
                logger.info(f"  CI Lower Bound: {report['confidence_interval_lower']:+.4f}")
            if 'should_deploy' in report:
                logger.info(f"  Deployment Decision: {'DEPLOY' if report['should_deploy'] else 'BLOCK'}")

            return True

        except requests.exceptions.Timeout:
            logger.error(f"API request timed out after {self.timeout}s")
            return False

        except requests.exceptions.ConnectionError as e:
            logger.error(f"Failed to connect to API: {e}")
            return False

        except requests.exceptions.HTTPError as e:
            logger.error(f"HTTP error from API: {e}")
            logger.error(f"  Response: {e.response.text if e.response else 'N/A'}")
            return False

        except Exception as e:
            logger.error(f"Unexpected error sending improvement report: {e}")
            return False

    def health_check(self) -> bool:
        """Check if API is reachable

        Returns:
            True if API is healthy, False otherwise
        """
        try:
            endpoint = f"{self.base_url}/health"
            response = requests.get(endpoint, timeout=5)
            response.raise_for_status()

            logger.info(f"API health check passed: {endpoint}")
            return True

        except Exception as e:
            logger.warning(f"API health check failed: {e}")
            return False

    def build_improvement_report(
        self,
        planner_id: int,
        baseline_objective: float,
        candidate_objective: float,
        dr_uplift: float,
        confidence_interval_lower: float,
        confidence_interval_upper: float,
        should_deploy: bool,
        per_ap_metrics: Dict[str, Dict[str, float]],
        additional_data: Optional[Dict[str, Any]] = None
    ) -> Dict[str, Any]:
        """Build improvement report structure

        Args:
            planner_id: Planner/trial ID
            baseline_objective: Baseline objective value
            candidate_objective: Candidate objective value
            dr_uplift: Doubly-robust uplift estimate
            confidence_interval_lower: Lower bound of CI
            confidence_interval_upper: Upper bound of CI
            should_deploy: Deployment decision
            per_ap_metrics: Per-AP metric breakdown
            additional_data: Optional additional data

        Returns:
            Improvement report dictionary
        """
        report = {
            'planner_id': planner_id,
            'baseline_objective': baseline_objective,
            'candidate_objective': candidate_objective,
            'naive_uplift': candidate_objective - baseline_objective,
            'dr_uplift': dr_uplift,
            'confidence_interval_lower': confidence_interval_lower,
            'confidence_interval_upper': confidence_interval_upper,
            'should_deploy': should_deploy,
            'per_ap_metrics': per_ap_metrics
        }

        if additional_data:
            report.update(additional_data)

        return report
