"""
WiFi AP RL - Actor, Critic, PPO, Memory, and Graph Components

This file contains all model code for WiFi AP configuration RL:
- MLP: Multi-layer perceptron helper
- GATlayer, GAT: Graph Attention Network
- APGraphBatch: Batching utility for AP graphs
- Memory, RolloutBuffer: Experience storage
- APActor: Dual-head actor (channel + power)
- APCritic: Value network with global pooling
- PPO: Proximal Policy Optimization algorithm

Adapted from GOODRL/policy/actor3.py for WiFi AP configuration.
"""

import os
import sys
from copy import deepcopy
from collections import deque
from dataclasses import dataclass
from typing import List, Optional, Dict, Any

import torch
import torch.nn as nn
import torch.nn.functional as F
import numpy as np
from torch.nn import Sequential, Linear, ReLU
from torch.distributions.categorical import Categorical
from torch_geometric.nn import GATConv, global_mean_pool


# ============================================================================
# Device Configuration
# ============================================================================

device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')


# ============================================================================
# MLP - Multi-Layer Perceptron (from actor3.py lines 299-317)
# ============================================================================

class MLP(nn.Module):
    """Standard MLP with configurable layers and activation."""

    def __init__(self, input_dim, hidden_dim, output_dim, layer_nums,
                 activation_fn=ReLU, dropout=0.0):
        super(MLP, self).__init__()
        layers = []
        in_dim = input_dim
        for _ in range(layer_nums - 1):
            layers.append(nn.Linear(in_dim, hidden_dim))
            layers.append(activation_fn())
            if dropout > 0:
                layers.append(nn.Dropout(dropout))
            in_dim = hidden_dim
        # Output layer
        layers.append(nn.Linear(in_dim, output_dim))
        self.fc = nn.Sequential(*layers)

    def forward(self, x):
        return self.fc(x)


# ============================================================================
# GATlayer - Single GAT Convolution Layer (from actor3.py lines 319-327)
# ============================================================================

class GATlayer(nn.Module):
    """Single Graph Attention layer with dropout."""

    def __init__(self, in_chnl, out_chnl, dropout, concat, heads=2):
        super().__init__()
        self.dropout = dropout
        self.conv = GATConv(in_chnl, out_chnl, heads=heads,
                           dropout=dropout, concat=concat)

    def forward(self, h_node, edge_index):
        h_node = F.elu(self.conv(
            F.dropout(h_node, p=self.dropout, training=self.training).float(),
            edge_index
        ))
        return h_node


# ============================================================================
# GAT - Multi-layer Graph Attention Network (from actor3.py lines 329-355)
# ============================================================================

class GAT(nn.Module):
    """Multi-layer Graph Attention Network."""

    def __init__(self, in_dim, hidden_dim, dropout, layers_gat=2, heads=2):
        super().__init__()
        self.layers_gat = layers_gat
        self.GAT_layers = torch.nn.ModuleList()

        if layers_gat == 1:
            self.GAT_layers.append(
                GATlayer(in_dim, hidden_dim, dropout, concat=False, heads=heads)
            )
        else:
            # First GAT layer
            self.GAT_layers.append(
                GATlayer(in_dim, hidden_dim, dropout, concat=True, heads=heads)
            )
            # Middle GAT layers
            for _ in range(layers_gat - 2):
                self.GAT_layers.append(
                    GATlayer(heads * hidden_dim, hidden_dim, dropout,
                            concat=True, heads=heads)
                )
            # Last GAT layer
            self.GAT_layers.append(
                GATlayer(heads * hidden_dim, hidden_dim, dropout,
                        concat=False, heads=1)
            )

    def forward(self, x, edge_index):
        h_node = self.GAT_layers[0](x, edge_index)
        for layer in range(1, self.layers_gat):
            h_node = self.GAT_layers[layer](h_node, edge_index)
        return h_node


# ============================================================================
# APGraphBatch - Graph Batching for AP Networks
# Adapted from BatchGraph in actor3.py for simpler AP-only graphs
# ============================================================================

class APGraphBatch:
    """
    Batching utility for AP interaction graphs.

    Handles single graph processing and batch processing for training.
    """

    def __init__(self, normalize=False, normalize_values=None):
        self.ap_features = None
        self.edge_index = None
        self.batch_idx = None
        self.target_ap_idx = None
        self.normalize = normalize
        self.normalize_values = normalize_values

    def from_single(self, ap_features, edge_index, target_ap_idx):
        """
        Process a single graph state.

        Args:
            ap_features: numpy array [num_aps, feature_dim]
            edge_index: numpy array [2, num_edges]
            target_ap_idx: int, which AP to select for action
        """
        if self.normalize and self.normalize_values is not None:
            ap_features = ap_features / np.array(self.normalize_values)

        self.ap_features = torch.from_numpy(ap_features).float().to(device)
        self.edge_index = torch.from_numpy(edge_index.astype(np.int64)).to(device)
        self.target_ap_idx = torch.tensor([target_ap_idx], dtype=torch.long).to(device)
        self.batch_idx = torch.zeros(ap_features.shape[0], dtype=torch.long).to(device)

        return self

    def batch_process(self, data_list: list):
        """
        Batch multiple APGraphBatch instances for training.

        Args:
            data_list: List of APGraphBatch objects

        Returns:
            self with concatenated tensors
        """
        ap_features_list = []
        edge_index_list = []
        batch_idx_list = []
        target_ap_idx_list = []

        total_nodes = 0
        for i, data in enumerate(data_list):
            ap_features_list.append(data.ap_features)
            edge_index_list.append(data.edge_index + total_nodes)
            batch_idx_list.append(
                torch.full((data.ap_features.shape[0],), i, dtype=torch.long, device=device)
            )
            target_ap_idx_list.append(data.target_ap_idx + total_nodes)
            total_nodes += data.ap_features.shape[0]

        self.ap_features = torch.cat(ap_features_list, dim=0)
        self.edge_index = torch.cat(edge_index_list, dim=-1)
        self.batch_idx = torch.cat(batch_idx_list, dim=0)
        self.target_ap_idx = torch.cat(target_ap_idx_list, dim=0)

        return self

    def clean(self):
        """Clear all stored tensors."""
        self.ap_features = None
        self.edge_index = None
        self.batch_idx = None
        self.target_ap_idx = None


# ============================================================================
# Buffer - Named tuple for batch data (from actor3.py lines 133-151)
# ============================================================================

@dataclass
class Buffer:
    """Batch data container for PPO training."""
    states: list                    # List of APGraphBatch
    channel_actions: torch.Tensor   # Channel action indices
    power_actions: torch.Tensor     # Power action indices
    rewards: torch.Tensor
    dones: torch.Tensor
    log_probs: torch.Tensor
    v_states: list                  # List of APGraphBatch for critic
    returns: torch.Tensor
    advantages: torch.Tensor
    target_ap_idxs: torch.Tensor    # Which AP was selected
    costs: torch.Tensor             # RCPO: constraint costs


# ============================================================================
# Memory - Experience Replay Buffer (from actor3.py lines 153-250)
# ============================================================================

class Memory:
    """
    Experience memory with deque-based storage.

    Stores transitions for online PPO training.
    Key change from actor3.py: Stores (channel_action, power_action) separately.
    """

    def __init__(self, max_len):
        self.state_mb = deque(maxlen=max_len)
        self.channel_action_mb = deque(maxlen=max_len)
        self.power_action_mb = deque(maxlen=max_len)
        self.reward_mb = deque(maxlen=max_len)
        self.done_mb = deque(maxlen=max_len)
        self.log_mb = deque(maxlen=max_len)
        self.v_state_mb = deque(maxlen=max_len)
        self.target_ap_idx_mb = deque(maxlen=max_len)
        self.cost_mb = deque(maxlen=max_len)  # RCPO: constraint costs

    def record(self, state, channel_action, power_action, reward, done,
               log_prob, v_state, target_ap_idx, cost=0.0):
        """Record a single transition."""
        self.state_mb.append(state)
        self.channel_action_mb.append(channel_action)
        self.power_action_mb.append(power_action)
        self.reward_mb.append(reward)
        self.done_mb.append(done)
        self.log_mb.append(log_prob)
        self.v_state_mb.append(v_state)
        self.target_ap_idx_mb.append(target_ap_idx)
        self.cost_mb.append(cost)  # RCPO: store constraint cost

    def clear_memory(self):
        """Clear all stored transitions."""
        self.state_mb.clear()
        self.channel_action_mb.clear()
        self.power_action_mb.clear()
        self.reward_mb.clear()
        self.done_mb.clear()
        self.log_mb.clear()
        self.v_state_mb.clear()
        self.target_ap_idx_mb.clear()
        self.cost_mb.clear()  # RCPO: clear costs

    def compute_returns(self, gamma=0.99, normalize_rewards=1.0, rcpo_lambda=0.0):
        """
        Compute discounted returns using penalized rewards for RCPO.

        Per RCPO paper (Eq. 11, Definition 3):
            target_t = r_t - λ*c_t + γ*V(s_{t+1})

        The critic learns value of penalized rewards, not raw rewards.

        Args:
            gamma: Discount factor
            normalize_rewards: Divide rewards by this value
            rcpo_lambda: Lagrange multiplier for RCPO constraint penalty
        """
        # RCPO: Compute penalized rewards r_hat = r - lambda * c
        penalized_rewards = [
            (r - rcpo_lambda * c) / normalize_rewards
            for r, c in zip(self.reward_mb, self.cost_mb)
        ]
        discounted_sums = [0] * len(penalized_rewards)
        future_return = 0

        for t in reversed(range(len(penalized_rewards))):
            future_return = penalized_rewards[t] + gamma * future_return
            discounted_sums[t] = future_return

        return discounted_sums

    def compute_returns_sliding(self, gamma=0.99, window_steps=256,
                                normalize_rewards=1.0):
        """
        Compute returns with sliding window (from actor3.py compute_returns_new).

        Args:
            gamma: Discount factor
            window_steps: Window size for sliding computation
            normalize_rewards: Divide rewards by this value
        """
        norm_reward_mb = np.array(self.reward_mb) / normalize_rewards
        gamma_powers = np.array([gamma ** i for i in range(window_steps + 1)])
        discounted_sums = np.convolve(norm_reward_mb, gamma_powers[::-1], mode='valid')
        discounted_sums = discounted_sums[:len(norm_reward_mb) - window_steps]
        return discounted_sums.tolist()

    def compute_gae(self, values, gamma=0.99, gae_lambda=0.95,
                    normalize_rewards=1.0):
        """
        Compute Generalized Advantage Estimation.

        Args:
            values: Value estimates for each state
            gamma: Discount factor
            gae_lambda: GAE lambda parameter
            normalize_rewards: Divide rewards by this value
        """
        norm_reward_mb = [r / normalize_rewards for r in self.reward_mb]
        advantage_mb = [0] * len(norm_reward_mb)
        returns_mb = [0] * len(norm_reward_mb)

        future_advantage = 0
        next_value = 0

        for t in reversed(range(len(norm_reward_mb))):
            delta = norm_reward_mb[t] + gamma * next_value - values[t]
            future_advantage = delta + gamma * gae_lambda * future_advantage
            advantage_mb[t] = deepcopy(future_advantage)
            returns_mb[t] = values[t] + advantage_mb[t]
            next_value = values[t]

        return returns_mb, advantage_mb


# ============================================================================
# RolloutBuffer - Sampler for PPO Training (from actor3.py lines 252-296)
# ============================================================================

class RolloutBuffer(Memory):
    """
    Buffer for sampling batches during PPO training.

    Inherits from Memory and adds sampling functionality.
    """

    def __init__(self, memory: Memory, returns: list, advantages: list, length: int):
        super().__init__(0)
        self.returns = deepcopy(returns)
        self.advantages = deepcopy(advantages)

        # Copy data from memory
        self.state_mb = list(memory.state_mb)[:length]
        self.channel_action_mb = list(memory.channel_action_mb)[:length]
        self.power_action_mb = list(memory.power_action_mb)[:length]
        self.reward_mb = list(memory.reward_mb)[:length]
        self.done_mb = list(memory.done_mb)[:length]
        self.log_mb = list(memory.log_mb)[:length]
        self.v_state_mb = list(memory.v_state_mb)[:length]
        self.target_ap_idx_mb = list(memory.target_ap_idx_mb)[:length]
        self.cost_mb = list(memory.cost_mb)[:length]  # RCPO: constraint costs

    def get(self, batch_size=64, inorder=False):
        """
        Generator that yields batches for training.

        Args:
            batch_size: Size of each batch
            inorder: If True, iterate in order; otherwise shuffle
        """
        memory_len = len(self.returns)

        if inorder:
            indices = np.arange(memory_len)
        else:
            indices = np.random.permutation(memory_len)

        start_idx = 0
        while start_idx < memory_len:
            end_idx = min(start_idx + batch_size, memory_len)
            yield self._get_samples(indices[start_idx:end_idx])
            start_idx += batch_size

    def _get_samples(self, batch_inds) -> Buffer:
        """Get a batch of samples by indices."""
        return Buffer(
            states=[self.state_mb[i] for i in batch_inds],
            channel_actions=torch.tensor(
                [self.channel_action_mb[i] for i in batch_inds],
                dtype=torch.long, device=device
            ),
            power_actions=torch.tensor(
                [self.power_action_mb[i] for i in batch_inds],
                dtype=torch.long, device=device
            ),
            rewards=torch.tensor(
                [self.reward_mb[i] for i in batch_inds],
                dtype=torch.float32, device=device
            ),
            dones=torch.tensor(
                [self.done_mb[i] for i in batch_inds],
                dtype=torch.bool, device=device
            ),
            log_probs=torch.tensor(
                [self.log_mb[i] for i in batch_inds],
                dtype=torch.float32, device=device
            ),
            v_states=[self.v_state_mb[i] for i in batch_inds],
            returns=torch.tensor(
                [self.returns[i] for i in batch_inds],
                dtype=torch.float32, device=device
            ),
            advantages=torch.tensor(
                [self.advantages[i] for i in batch_inds],
                dtype=torch.float32, device=device
            ),
            target_ap_idxs=torch.tensor(
                [self.target_ap_idx_mb[i] for i in batch_inds],
                dtype=torch.long, device=device
            ),
            costs=torch.tensor(
                [self.cost_mb[i] for i in batch_inds],
                dtype=torch.float32, device=device
            ),
        )

    def get_slice(self, n):
        """Keep only first n samples."""
        self.returns = self.returns[:n]
        self.advantages = self.advantages[:n]
        self.state_mb = self.state_mb[:n]
        self.channel_action_mb = self.channel_action_mb[:n]
        self.power_action_mb = self.power_action_mb[:n]
        self.reward_mb = self.reward_mb[:n]
        self.done_mb = self.done_mb[:n]
        self.log_mb = self.log_mb[:n]
        self.v_state_mb = self.v_state_mb[:n]
        self.target_ap_idx_mb = self.target_ap_idx_mb[:n]

    def get_reversed_slice(self, n):
        """Keep only last n samples."""
        self.returns = self.returns[-n:]
        self.advantages = self.advantages[-n:]
        self.state_mb = self.state_mb[-n:]
        self.channel_action_mb = self.channel_action_mb[-n:]
        self.power_action_mb = self.power_action_mb[-n:]
        self.reward_mb = self.reward_mb[-n:]
        self.done_mb = self.done_mb[-n:]
        self.log_mb = self.log_mb[-n:]
        self.v_state_mb = self.v_state_mb[-n:]
        self.target_ap_idx_mb = self.target_ap_idx_mb[-n:]

    def update_advantages(self, new_vals: list):
        """Update advantages using new value estimates."""
        self.advantages = [a - v for a, v in zip(self.returns, new_vals)]


# ============================================================================
# APActor - Dual-Head Actor for Channel + Power Selection
# ============================================================================

class APActor(nn.Module):
    """
    Actor network for AP configuration with two discrete action heads.

    Architecture:
    1. GAT encodes all AP nodes -> embeddings [num_aps, hidden_dim]
    2. Select target AP embedding via index (gradient flows through indexing)
    3. Two separate MLP heads output logits for channel and power

    Action space sizes (num_channels, num_power_levels) are passed in constructor,
    NOT hardcoded. Define them in your main.py.
    """

    def __init__(self,
                 input_dim_ap: int,
                 hidden_dim: int,
                 gnn_layers: int,
                 mlp_layers: int,
                 num_channels: int,
                 num_power_levels: int,
                 heads: int = 1,
                 dropout: float = 0.0):
        super().__init__()

        self.hidden_dim = hidden_dim
        self.num_channels = num_channels
        self.num_power_levels = num_power_levels

        # GAT encoder
        self.gat = GAT(
            in_dim=input_dim_ap,
            hidden_dim=hidden_dim,
            dropout=dropout,
            layers_gat=gnn_layers,
            heads=heads
        )

        # Two action heads
        self.channel_head = MLP(hidden_dim, hidden_dim, num_channels, mlp_layers)
        self.power_head = MLP(hidden_dim, hidden_dim, num_power_levels, mlp_layers)

    def forward(self,
                ap_features: torch.Tensor,
                edge_index: torch.Tensor,
                target_ap_idx: torch.Tensor,
                deterministic: bool = False) -> Dict[str, torch.Tensor]:
        """
        Forward pass to select actions.

        Args:
            ap_features: [num_aps, input_dim_ap] or batched
            edge_index: [2, num_edges]
            target_ap_idx: Which AP(s) to select for action
            deterministic: If True, select argmax actions

        Returns:
            Dict with channel_action, power_action, log_prob, entropy
        """
        # Make edges bidirectional (pattern from actor3.py line 565)
        edge_index = torch.cat([edge_index, edge_index.flip(0)], dim=-1)

        # Encode all APs through GAT
        ap_embeddings = self.gat(ap_features, edge_index)  # [num_aps, hidden_dim]

        # Select target AP embedding (gradient preserving via indexing)
        target_embed = ap_embeddings[target_ap_idx]  # [batch, hidden_dim] or [hidden_dim]

        # Get logits from both heads
        channel_logits = self.channel_head(target_embed)
        power_logits = self.power_head(target_embed)

        # Create distributions
        channel_dist = Categorical(logits=channel_logits)
        power_dist = Categorical(logits=power_logits)

        # Sample actions
        if deterministic:
            channel_action = channel_logits.argmax(dim=-1)
            power_action = power_logits.argmax(dim=-1)
        else:
            channel_action = channel_dist.sample()
            power_action = power_dist.sample()

        # Combined log prob and entropy (sum of independent distributions)
        log_prob = channel_dist.log_prob(channel_action) + power_dist.log_prob(power_action)
        entropy = channel_dist.entropy() + power_dist.entropy()

        return {
            'channel_action': channel_action,
            'power_action': power_action,
            'log_prob': log_prob,
            'entropy': entropy,
        }

    def evaluate_actions(self,
                        ap_features: torch.Tensor,
                        edge_index: torch.Tensor,
                        target_ap_idx: torch.Tensor,
                        channel_action: torch.Tensor,
                        power_action: torch.Tensor):
        """
        Evaluate log probability of given actions (for PPO update).

        Returns:
            log_prob: Combined log probability
            entropy: Combined entropy
        """
        edge_index = torch.cat([edge_index, edge_index.flip(0)], dim=-1)
        ap_embeddings = self.gat(ap_features, edge_index)
        target_embed = ap_embeddings[target_ap_idx]

        channel_dist = Categorical(logits=self.channel_head(target_embed))
        power_dist = Categorical(logits=self.power_head(target_embed))

        log_prob = channel_dist.log_prob(channel_action) + power_dist.log_prob(power_action)
        entropy = channel_dist.entropy() + power_dist.entropy()

        return log_prob, entropy


# ============================================================================
# APCritic - Value Network with Global Pooling
# ============================================================================

class APCritic(nn.Module):
    """
    Critic network for value estimation.

    Uses global mean pooling over all AP embeddings to get graph-level value.
    """

    def __init__(self,
                 input_dim_ap: int,
                 hidden_dim: int,
                 gnn_layers: int,
                 mlp_layers: int,
                 heads: int = 1,
                 dropout: float = 0.0):
        super().__init__()

        self.gat = GAT(
            in_dim=input_dim_ap,
            hidden_dim=hidden_dim,
            dropout=dropout,
            layers_gat=gnn_layers,
            heads=heads
        )

        self.value_head = MLP(hidden_dim, hidden_dim, 1, mlp_layers)

    def forward(self,
                ap_features: torch.Tensor,
                edge_index: torch.Tensor,
                batch_idx: Optional[torch.Tensor] = None) -> torch.Tensor:
        """
        Compute state value.

        Args:
            ap_features: [num_aps, input_dim_ap]
            edge_index: [2, num_edges]
            batch_idx: [num_aps] batch assignment for batched graphs

        Returns:
            Value tensor [batch_size] or scalar
        """
        edge_index = torch.cat([edge_index, edge_index.flip(0)], dim=-1)
        ap_embeddings = self.gat(ap_features, edge_index)

        if batch_idx is not None:
            # Batched: use global_mean_pool
            pooled = global_mean_pool(ap_embeddings, batch_idx)
        else:
            # Single graph: simple mean
            pooled = ap_embeddings.mean(dim=0, keepdim=True)

        return self.value_head(pooled).squeeze(-1)


# ============================================================================
# PPO - Proximal Policy Optimization (from actor3.py lines 778-1099)
# ============================================================================

class PPO:
    """
    PPO algorithm for WiFi AP configuration.

    Adapted from actor3.py PPO class with modifications for dual-head actions.
    """

    def __init__(self,
                 input_dim_ap: int,
                 hidden_dim: int,
                 gnn_layers: int,
                 mlp_layers: int,
                 num_channels: int,
                 num_power_levels: int,
                 heads: int = 1,
                 dropout: float = 0.0,
                 lr_actor: float = 1e-3,
                 lr_critic: float = 1e-3,
                 eps_clip: float = 0.2,
                 entropy_coef: float = 0.01,
                 value_coef: float = 0.5,
                 max_grad_norm: float = 0.5,
                 clip_value: float = 4.0):

        self.eps_clip = eps_clip
        self.entropy_coef = entropy_coef
        self.value_coef = value_coef
        self.max_grad_norm = max_grad_norm
        self.clip_value = clip_value

        # Actor
        self.actor = APActor(
            input_dim_ap=input_dim_ap,
            hidden_dim=hidden_dim,
            gnn_layers=gnn_layers,
            mlp_layers=mlp_layers,
            num_channels=num_channels,
            num_power_levels=num_power_levels,
            heads=heads,
            dropout=dropout
        ).to(device)

        # Critic
        self.critic = APCritic(
            input_dim_ap=input_dim_ap,
            hidden_dim=hidden_dim,
            gnn_layers=gnn_layers,
            mlp_layers=mlp_layers,
            heads=heads,
            dropout=dropout
        ).to(device)

        # Optimizers
        self.optimizer_actor = torch.optim.Adam(self.actor.parameters(), lr=lr_actor)
        self.optimizer_critic = torch.optim.Adam(self.critic.parameters(), lr=lr_critic)

        # Tracking
        self.entropy_count = 0
        self.grad_count = 0
        self.pre_grad_max = 0

    def train_critic(self, bufferdata: RolloutBuffer, batch_size: int = 64,
                     epochs: int = 4) -> tuple:
        """
        Train critic on collected experience.

        Returns:
            values: List of value estimates for all states
            losses: Tuple of (rmse_losses, mre_losses)
        """
        rmse_losses, mre_losses = [], []

        for _ in range(epochs):
            value_loss_list, rate_loss_list = [], []

            for rollout_data in bufferdata.get(batch_size):
                # Batch states
                batch_states = APGraphBatch().batch_process(rollout_data.v_states)

                # Get values
                vals = self.critic(
                    ap_features=batch_states.ap_features,
                    edge_index=batch_states.edge_index,
                    batch_idx=batch_states.batch_idx
                )

                # Compute loss
                v_loss = F.mse_loss(rollout_data.returns, vals)
                value_loss_list.append(v_loss.item() * vals.shape[0])
                rate_loss_list.append(
                    torch.sum(torch.abs(
                        (rollout_data.returns - vals) / (rollout_data.returns + 1e-8)
                    )).item()
                )

                # Update
                self.optimizer_critic.zero_grad()
                v_loss.backward()
                self.optimizer_critic.step()

            rmse_losses.append(np.sqrt(np.sum(value_loss_list) / len(bufferdata.state_mb)))
            mre_losses.append(100 * np.sum(rate_loss_list) / len(bufferdata.state_mb))

        # Get final values for all states
        batch_states = APGraphBatch().batch_process(bufferdata.v_state_mb)
        with torch.no_grad():
            values = self.critic(
                ap_features=batch_states.ap_features,
                edge_index=batch_states.edge_index,
                batch_idx=batch_states.batch_idx
            )

        return values.tolist(), (rmse_losses, mre_losses)

    def train_actor(self, bufferdata: RolloutBuffer, batch_size: int = 64,
                    epochs: int = 1, entropy_control: bool = True,
                    entropy_min: float = 0.6, entropy_max: float = 1.2,
                    grad_control: bool = True, rcpo_lambda: float = 0.0) -> tuple:
        """
        Train actor using PPO objective with RCPO constraint penalty.

        Args:
            bufferdata: RolloutBuffer containing transitions
            batch_size: Size of mini-batches
            epochs: Number of epochs
            entropy_control: Whether to apply entropy control
            entropy_min: Minimum entropy threshold
            entropy_max: Maximum entropy threshold
            grad_control: Whether to apply gradient control
            rcpo_lambda: Lagrange multiplier for RCPO constraint (0 = no constraint)

        Returns:
            Tuple of (pg_losses, entropy_losses, grad_changes)
        """
        pg_losses, entropy_losses, grad_changes = [], [], []

        for _ in range(epochs):
            pg_loss_list, entropy_loss_list = [], []

            for rollout_data in bufferdata.get(batch_size):
                # Batch states
                batch_states = APGraphBatch().batch_process(rollout_data.states)

                # Evaluate actions
                logprobs, ent_loss = self.actor.evaluate_actions(
                    ap_features=batch_states.ap_features,
                    edge_index=batch_states.edge_index,
                    target_ap_idx=batch_states.target_ap_idx,
                    channel_action=rollout_data.channel_actions,
                    power_action=rollout_data.power_actions
                )

                # Compute advantages
                advantages = rollout_data.advantages

                # PPO clipped objective
                ratio = torch.exp(logprobs - rollout_data.log_probs)
                surr1 = advantages * ratio
                surr2 = advantages * torch.clamp(ratio, 1 - self.eps_clip, 1 + self.eps_clip)
                p_loss = torch.min(surr1, surr2).mean()

                ent_loss = torch.mean(ent_loss)
                entropy_loss_list.append(ent_loss.item())

                # Entropy control
                e_coef = 0
                if entropy_control:
                    if entropy_loss_list[-1] < entropy_min:
                        e_coef = self.entropy_coef
                        self.entropy_count += 1
                    elif entropy_loss_list[-1] > entropy_max:
                        e_coef = -self.entropy_coef
                        self.entropy_count += 1

                # RCPO: Compute constraint penalty term
                # In RCPO, we add lambda * mean(costs) to the loss (since we're minimizing)
                # This encourages the policy to reduce constraint violations
                rcpo_penalty = rcpo_lambda * rollout_data.costs.mean()

                # Total loss: minimize (-PPO_objective - entropy_bonus + rcpo_penalty)
                # = maximize (PPO_objective + entropy_bonus - rcpo_penalty)
                loss = -p_loss - e_coef * ent_loss + rcpo_penalty
                pg_loss_list.append(loss.item())

                # Backward
                self.optimizer_actor.zero_grad()
                loss.backward()

                # Gradient monitoring
                l2_norm = []
                for _, param in self.actor.named_parameters():
                    if param.grad is not None:
                        l2_norm.append(torch.norm(param.grad).item())
                l2_norm_mean = np.mean(l2_norm) if l2_norm else 0

                # Gradient control
                if grad_control:
                    if l2_norm_mean <= 0.075 and (self.pre_grad_max == 0 or l2_norm_mean <= self.pre_grad_max):
                        grad_changes.append(l2_norm_mean)
                        self.optimizer_actor.step()
                    else:
                        self.grad_count += 1
                else:
                    grad_changes.append(l2_norm_mean)
                    self.optimizer_actor.step()

                # Weight clipping
                for param in self.actor.parameters():
                    param.data = torch.clamp(param.data, -self.clip_value, self.clip_value)

            pg_losses.append(np.mean(pg_loss_list))
            entropy_losses.append(np.mean(entropy_loss_list))

        # Update gradient tracking
        if len(grad_changes) == 0:
            self.pre_grad_max = 0
            grad_changes = [0]
        else:
            self.pre_grad_max = np.mean(grad_changes) + np.std(grad_changes)

        return (pg_losses, entropy_losses, grad_changes)

    def save(self, actor_path: str, critic_path: str):
        """Save actor and critic models."""
        torch.save(self.actor.state_dict(), actor_path)
        torch.save(self.critic.state_dict(), critic_path)

    def load(self, actor_path: str, critic_path: str):
        """Load actor and critic models."""
        self.actor.load_state_dict(torch.load(actor_path, map_location=device))
        self.critic.load_state_dict(torch.load(critic_path, map_location=device))
