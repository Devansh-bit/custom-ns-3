#!/usr/bin/env python3
"""
RL Logger - Structured logging for RL training.

Produces two log formats:
1. JSONL file - for analysis/publishing (one JSON object per line)
2. Text file - for merging with NS3 logs (matches NS3 log format)

The logs focus on the TARGET AP only (the one being modified each step).

Usage:
    from rl_logger import RLLogger

    rl_logger = RLLogger(output_dir="./rl_output_logs")

    # Log each step
    rl_logger.log_step(...)

    # Close at end
    rl_logger.close()
"""

import json
import time
from pathlib import Path
from datetime import datetime
from typing import Dict, Any, Optional
from dataclasses import dataclass, asdict


@dataclass
class SimTimeInfo:
    """Simulation time information for log correlation."""
    batch_start_sim_time: Optional[float]
    batch_end_sim_time: Optional[float]
    effect_at_sim_time: Optional[float]  # JOIN KEY for merging with NS3 logs


@dataclass
class TargetAPState:
    """Current state of the target AP BEFORE action."""
    bssid: str
    channel: int
    power: float
    channel_utilization: float
    num_clients: float
    num_aps_on_channel: float
    # Connection metrics
    p50_throughput: float
    p75_loss_rate: float
    p75_rtt: float
    p75_jitter: float


@dataclass
class ActionInfo:
    """Action taken by the RL agent on target AP."""
    channel_number: int
    channel_width: int
    width_action_idx: int
    power_dbm: float
    power_action_idx: int
    target_ap_color: int
    log_prob: float
    action_source: str  # "RANDOM (warmup)" or "PPO Policy"


@dataclass
class RewardInfo:
    """Reward computation details."""
    current_objective: Optional[float]
    previous_objective: Optional[float]
    delta_reward: float
    baseline_objective: Optional[float]
    pct_improvement_vs_baseline: Optional[float]
    best_objective: Optional[float]


@dataclass
class RLStepLog:
    """Complete log entry for one RL step (target AP focused)."""
    # Step identification
    step: int
    batch_number: int
    timestamp_unix: float
    timestamp_iso: str

    # Simulation time (for correlation with NS3)
    sim_time: SimTimeInfo

    # Target AP info
    target_ap_bssid: str
    selection_reason: str
    target_ap_state: TargetAPState

    # Action taken on target AP
    action: ActionInfo

    # Reward
    reward: RewardInfo

    # Global summary
    global_objective: Optional[float]
    num_aps: int

    # Training metadata
    warmup_phase: bool
    critic_warmstart_phase: bool
    buffer_size: int

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary, handling nested dataclasses."""
        return {
            'step': self.step,
            'batch_number': self.batch_number,
            'timestamp_unix': self.timestamp_unix,
            'timestamp_iso': self.timestamp_iso,
            'sim_time': asdict(self.sim_time),
            'target_ap_bssid': self.target_ap_bssid,
            'selection_reason': self.selection_reason,
            'target_ap_state': asdict(self.target_ap_state),
            'action': asdict(self.action),
            'reward': asdict(self.reward),
            'global_objective': self.global_objective,
            'num_aps': self.num_aps,
            'warmup_phase': self.warmup_phase,
            'critic_warmstart_phase': self.critic_warmstart_phase,
            'buffer_size': self.buffer_size
        }


class RLLogger:
    """
    Structured logger for RL training.

    Writes two formats:
    1. JSONL file - for analysis/publishing (one JSON object per line)
    2. Text file - for merging with NS3 logs (matches NS3 log format)

    Focuses on TARGET AP only (the one being modified each step).
    """

    def __init__(
        self,
        output_dir: str = "./rl_output_logs",
        prefix: str = "rl_training_logs",
        verbose: bool = True
    ):
        """
        Initialize the RL logger.

        Args:
            output_dir: Directory to write log files
            prefix: Prefix for log file names
            verbose: Print log messages to console
        """
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.verbose = verbose

        # Create timestamped log files
        self.log_timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        self.json_log_file = self.output_dir / f"{prefix}_{self.log_timestamp}.jsonl"
        self.text_log_file = self.output_dir / f"{prefix}_{self.log_timestamp}.log"

        # Open files for writing
        self._json_file = open(self.json_log_file, 'w')
        self._text_file = open(self.text_log_file, 'w')

        # Track statistics
        self.steps_logged = 0

        if verbose:
            print(f"[RLLogger] JSON logging to: {self.json_log_file}")
            print(f"[RLLogger] Text logging to: {self.text_log_file} (for NS3 merge)")

    def _format_optional(self, value: Optional[float], fmt: str = ".4f") -> str:
        """Format optional float value."""
        if value is None:
            return "None"
        return f"{value:{fmt}}"

    def _format_text_log(self, log: RLStepLog) -> str:
        """Format log entry as text for NS3 merge."""
        lines = []

        # Effect time for sorting
        effect_time = log.sim_time.effect_at_sim_time or 0.0

        # Header
        lines.append("=" * 80)
        lines.append(f"[{effect_time:8.2f}] [RL_STEP        ] step={log.step} batch={log.batch_number} "
                    f"timestamp={log.timestamp_iso} unix={log.timestamp_unix:.2f}")
        lines.append(f"[{effect_time:8.2f}] [RL_SIM_TIME    ] "
                    f"batch_start={self._format_optional(log.sim_time.batch_start_sim_time, '.2f')}s "
                    f"batch_end={self._format_optional(log.sim_time.batch_end_sim_time, '.2f')}s "
                    f"effect_at={self._format_optional(log.sim_time.effect_at_sim_time, '.2f')}s")
        lines.append("=" * 80)

        # Target AP selection
        lines.append(f"[{effect_time:8.2f}] [RL_TARGET_AP   ] {log.target_ap_bssid} "
                    f"selected_reason={log.selection_reason}")

        # Target AP state BEFORE action
        s = log.target_ap_state
        lines.append(f"[{effect_time:8.2f}] [RL_BEFORE      ] "
                    f"ch={s.channel} pwr={s.power:.2f}dBm util={s.channel_utilization:.2f}% "
                    f"clients={s.num_clients:.0f} aps_on_ch={s.num_aps_on_channel:.0f} | "
                    f"tput_p50={s.p50_throughput:.2f}Mbps loss_p75={s.p75_loss_rate:.2f}% "
                    f"rtt_p75={s.p75_rtt:.2f}ms jitter_p75={s.p75_jitter:.2f}ms")

        # Action taken
        a = log.action
        lines.append(f"[{effect_time:8.2f}] [RL_ACTION      ] "
                    f"ch={a.channel_number} width={a.channel_width}MHz width_idx={a.width_action_idx} | "
                    f"pwr={a.power_dbm:.2f}dBm pwr_idx={a.power_action_idx} | "
                    f"color={a.target_ap_color} log_prob={a.log_prob:.4f} source={a.action_source}")

        # Reward
        r = log.reward
        pct_str = self._format_optional(r.pct_improvement_vs_baseline, '+.2f') + "%" if r.pct_improvement_vs_baseline is not None else "None"
        lines.append(f"[{effect_time:8.2f}] [RL_REWARD      ] "
                    f"current={self._format_optional(r.current_objective)} "
                    f"previous={self._format_optional(r.previous_objective)} "
                    f"delta={r.delta_reward:+.4f} | "
                    f"baseline={self._format_optional(r.baseline_objective)} "
                    f"pct_vs_baseline={pct_str} best={self._format_optional(r.best_objective)}")

        # Global summary
        lines.append(f"[{effect_time:8.2f}] [RL_GLOBAL      ] "
                    f"global_obj={self._format_optional(log.global_objective)} num_aps={log.num_aps}")

        # Training metadata
        lines.append(f"[{effect_time:8.2f}] [RL_TRAINING    ] "
                    f"warmup_phase={log.warmup_phase} critic_warmstart={log.critic_warmstart_phase} "
                    f"buffer_size={log.buffer_size}")

        lines.append("=" * 80)
        lines.append("")  # Empty line for readability

        return "\n".join(lines)

    def log_step(
        self,
        step: int,
        batch_number: int,
        gat_state: Dict[str, Any],
        target_ap_bssid: str,
        selection_reason: str,
        channel_number: int,
        channel_width: int,
        width_action_idx: int,
        power_dbm: float,
        power_action_idx: int,
        target_ap_color: int,
        log_prob: float,
        action_source: str,
        current_objective: Optional[float],
        previous_objective: Optional[float],
        delta_reward: float,
        baseline_objective: Optional[float],
        pct_improvement: Optional[float],
        best_objective: Optional[float],
        per_ap_objectives: Dict[str, Dict[str, float]],
        warmup_phase: bool,
        critic_warmstart_phase: bool,
        buffer_size: int
    ):
        """
        Log a single RL step with target AP focused data.

        Args:
            step: Current step number
            batch_number: GAT batch number
            gat_state: State from GATStateProvider (includes sim_time_range)
            target_ap_bssid: Selected target AP
            selection_reason: Why this AP was selected
            channel_number: Channel number to apply
            channel_width: Channel width in MHz
            width_action_idx: Width action index (PPO output)
            power_dbm: Power in dBm
            power_action_idx: Power action index (PPO output)
            target_ap_color: Graph coloring color for target AP
            log_prob: Log probability of action
            action_source: "RANDOM (warmup)" or "PPO Policy"
            current_objective: Current global objective
            previous_objective: Previous global objective
            delta_reward: Reward (current - previous)
            baseline_objective: Baseline objective at t=1
            pct_improvement: % improvement vs baseline
            best_objective: Best objective seen so far
            per_ap_objectives: Dict of per-AP objective components (unused, kept for API compat)
            warmup_phase: Whether in random warmup phase
            critic_warmstart_phase: Whether in critic-only training phase
            buffer_size: Current replay buffer size
        """
        now = time.time()

        # Extract sim_time_range from gat_state
        sim_time_range = gat_state.get('sim_time_range', {})
        sim_time_info = SimTimeInfo(
            batch_start_sim_time=sim_time_range.get('start') if sim_time_range else None,
            batch_end_sim_time=sim_time_range.get('end') if sim_time_range else None,
            effect_at_sim_time=sim_time_range.get('effect_at') if sim_time_range else None
        )

        # Extract TARGET AP features only
        nodes = gat_state.get('nodes', {})
        connection_metrics = gat_state.get('connection_metrics', {})
        ap_bssids = gat_state.get('ap_order', list(nodes.keys()))

        target_node = nodes.get(target_ap_bssid, {})
        target_conn = connection_metrics.get(target_ap_bssid, {})

        target_ap_state = TargetAPState(
            bssid=target_ap_bssid,
            channel=target_node.get('channel', 0),
            power=target_node.get('power', 17.0),
            channel_utilization=target_node.get('channel_utilization', 0.0),
            num_clients=target_node.get('num_clients', 0.0),
            num_aps_on_channel=target_node.get('num_aps_on_channel', 0.0),
            p50_throughput=target_conn.get('p50_throughput', 0.0),
            p75_loss_rate=target_conn.get('p75_loss_rate', 0.0),
            p75_rtt=target_conn.get('p75_rtt', 0.0),
            p75_jitter=target_conn.get('p75_jitter', 0.0)
        )

        # Create action info
        action_info = ActionInfo(
            channel_number=channel_number,
            channel_width=channel_width,
            width_action_idx=width_action_idx,
            power_dbm=power_dbm,
            power_action_idx=power_action_idx,
            target_ap_color=target_ap_color,
            log_prob=log_prob,
            action_source=action_source
        )

        # Create reward info
        reward_info = RewardInfo(
            current_objective=current_objective,
            previous_objective=previous_objective,
            delta_reward=delta_reward,
            baseline_objective=baseline_objective,
            pct_improvement_vs_baseline=pct_improvement,
            best_objective=best_objective
        )

        # Create complete log entry
        log_entry = RLStepLog(
            step=step,
            batch_number=batch_number,
            timestamp_unix=now,
            timestamp_iso=datetime.fromtimestamp(now).isoformat(),
            sim_time=sim_time_info,
            target_ap_bssid=target_ap_bssid,
            selection_reason=selection_reason,
            target_ap_state=target_ap_state,
            action=action_info,
            reward=reward_info,
            global_objective=current_objective,
            num_aps=len(ap_bssids),
            warmup_phase=warmup_phase,
            critic_warmstart_phase=critic_warmstart_phase,
            buffer_size=buffer_size
        )

        # Write JSON log (JSONL format)
        json_line = json.dumps(log_entry.to_dict())
        self._json_file.write(json_line + '\n')
        self._json_file.flush()

        # Write text log (for NS3 merge)
        text_output = self._format_text_log(log_entry)
        self._text_file.write(text_output)
        self._text_file.flush()

        self.steps_logged += 1

    def get_log_file_path(self) -> str:
        """Return the path to the JSON log file."""
        return str(self.json_log_file)

    def get_text_log_file_path(self) -> str:
        """Return the path to the text log file."""
        return str(self.text_log_file)

    def close(self):
        """Close log files."""
        if self._json_file:
            self._json_file.close()
        if self._text_file:
            self._text_file.close()
        if self.verbose:
            print(f"[RLLogger] Closed log files. Total steps logged: {self.steps_logged}")
            print(f"[RLLogger] JSON: {self.json_log_file}")
            print(f"[RLLogger] Text: {self.text_log_file}")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False