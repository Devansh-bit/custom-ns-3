"""
Behavior Cloning / Imitation Learning for APActor

Trains the APActor network via supervised learning on expert demonstrations.
Follows the exact pattern from GOODRL/step1.py.

Usage:
    python sft_actor.py --dataset /path/to/expert_data.json --epochs 100 --lr 1e-3
"""

import json
import torch
import torch.nn as nn
import numpy as np
import argparse
import time
import random
from copy import deepcopy
from torch.distributions.categorical import Categorical

from actor_wifi import APActor, APGraphBatch, device


def set_seed(seed):
    """Set random seeds for reproducibility."""
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def load_dataset(path):
    """
    Load expert demonstration dataset from JSON.

    Expected format (list of samples):
    [
        {
            "state": {
                "ap_features": [[ch, pwr, clients, util], ...],  # [num_aps, 4]
                "edge_index": [[src...], [dst...]],              # [2, num_edges]
                "target_ap_idx": int
            },
            "channel_action": int,  # 0, 1, or 2
            "power_action": int     # 0-6
        },
        ...
    ]
    """
    with open(path, 'r') as f:
        data = json.load(f)
    print(f"Loaded {len(data)} samples from {path}")
    return data


def get_action_logits(actor, batch_states):
    """
    Get raw logits from actor for BC training.

    Args:
        actor: APActor model
        batch_states: APGraphBatch with batched graph data

    Returns:
        channel_logits: [batch_size, num_channels]
        power_logits: [batch_size, num_power_levels]
        entropy: [batch_size] combined entropy
    """
    # Make edges bidirectional
    edge_index = torch.cat([batch_states.edge_index, batch_states.edge_index.flip(0)], dim=-1)

    # Encode all APs through GAT
    ap_embeddings = actor.gat(batch_states.ap_features, edge_index)

    # Select target AP embeddings
    target_embed = ap_embeddings[batch_states.target_ap_idx]

    # Get logits from both heads
    channel_logits = actor.channel_head(target_embed)
    power_logits = actor.power_head(target_embed)

    # Compute entropy for regularization
    channel_dist = Categorical(logits=channel_logits)
    power_dist = Categorical(logits=power_logits)
    entropy = channel_dist.entropy() + power_dist.entropy()

    return channel_logits, power_logits, entropy


def sample_to_graph_batch(sample):
    """
    Convert a single dataset sample to APGraphBatch.

    Args:
        sample: dict with 'state' containing ap_features, edge_index, target_ap_idx

    Returns:
        APGraphBatch object
    """
    state = sample['state']
    ap_features = np.array(state['ap_features'], dtype=np.float32)
    edge_index = np.array(state['edge_index'], dtype=np.int64)
    target_ap_idx = state['target_ap_idx']

    batch = APGraphBatch()
    batch.from_single(ap_features, edge_index, target_ap_idx)
    return batch


def main(args):
    """Main training loop following GOODRL step1.py pattern."""

    total_start = time.time()

    # Set seed
    set_seed(args.seed)

    # Load dataset
    dataset = load_dataset(args.dataset)
    memory_len = len(dataset)

    # Initialize actor
    actor = APActor(
        input_dim_ap=args.input_dim_ap,
        hidden_dim=args.hidden_dim,
        gnn_layers=args.gnn_layers,
        mlp_layers=args.mlp_layers,
        num_channels=args.num_channels,
        num_power_levels=args.num_power_levels,
        heads=args.heads,
        dropout=args.dropout
    ).to(device)

    print(f"Initialized APActor on {device}")
    print(f"  input_dim_ap={args.input_dim_ap}, hidden_dim={args.hidden_dim}")
    print(f"  gnn_layers={args.gnn_layers}, mlp_layers={args.mlp_layers}")
    print(f"  num_channels={args.num_channels}, num_power_levels={args.num_power_levels}")

    # Optimizer
    optimizer = torch.optim.Adam(actor.parameters(), lr=args.lr)

    # Loss functions
    criterion_channel = nn.CrossEntropyLoss()
    criterion_power = nn.CrossEntropyLoss()

    # Training tracking
    best_loss = float('inf')
    cross_losses = []
    entropy_losses = []

    # Warmup steps to skip (like GOODRL)
    warmup = args.warmup_steps
    valid_indices = list(range(warmup, memory_len - warmup)) if warmup > 0 else list(range(memory_len))

    print(f"\nStarting training for {args.epochs} epochs...")
    print(f"  batch_size={args.batch_size}, lr={args.lr}, entropy_coef={args.entropy_coef}")
    print(f"  training samples: {len(valid_indices)} (skipping {warmup} warmup on each end)")
    print("-" * 60)

    for epoch in range(args.epochs):
        # Shuffle indices each epoch
        indices = np.random.permutation(valid_indices)

        epoch_channel_loss = []
        epoch_power_loss = []
        epoch_entropy = []

        start_idx = 0
        while start_idx < len(indices):
            end_idx = min(start_idx + args.batch_size, len(indices))
            batch_indices = indices[start_idx:end_idx]

            # Build batch of graph states
            temp_states = []
            for k in batch_indices:
                graph_batch = sample_to_graph_batch(dataset[k])
                temp_states.append(deepcopy(graph_batch))

            # Get expert actions
            channel_actions = torch.tensor(
                [dataset[k]['channel_action'] for k in batch_indices],
                dtype=torch.long, device=device
            )
            power_actions = torch.tensor(
                [dataset[k]['power_action'] for k in batch_indices],
                dtype=torch.long, device=device
            )

            # Batch process states
            batch_states = APGraphBatch().batch_process(temp_states)

            # Get actor logits
            channel_logits, power_logits, entropy = get_action_logits(actor, batch_states)

            # Compute losses
            ch_loss = criterion_channel(channel_logits, channel_actions)
            pwr_loss = criterion_power(power_logits, power_actions)
            ent_loss = torch.mean(entropy)

            epoch_channel_loss.append(ch_loss.item())
            epoch_power_loss.append(pwr_loss.item())
            epoch_entropy.append(ent_loss.item())

            # Combined loss (BC loss - entropy bonus)
            if args.entropy_coef > 0:
                loss = ch_loss + pwr_loss - args.entropy_coef * ent_loss
            else:
                loss = ch_loss + pwr_loss

            # Backprop
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

            # Weight clipping (like GOODRL)
            if args.clip_value > 0:
                for param in actor.parameters():
                    param.data = torch.clamp(param.data, -args.clip_value, args.clip_value)

            start_idx += args.batch_size

        # Epoch statistics
        mean_ch_loss = np.mean(epoch_channel_loss)
        mean_pwr_loss = np.mean(epoch_power_loss)
        mean_entropy = np.mean(epoch_entropy)
        mean_total_loss = mean_ch_loss + mean_pwr_loss

        cross_losses.append(mean_total_loss)
        entropy_losses.append(mean_entropy)

        elapsed = (time.time() - total_start) / 60

        print(f"Epoch {epoch+1:3d}/{args.epochs}: "
              f"ch_loss={mean_ch_loss:.4f}, pwr_loss={mean_pwr_loss:.4f}, "
              f"entropy={mean_entropy:.4f}, time={elapsed:.2f}min")

        # Save best model
        if mean_total_loss < best_loss:
            best_loss = mean_total_loss
            torch.save(actor.state_dict(), args.save_path)
            print(f"  -> Saved best model (loss={best_loss:.4f})")

    print("-" * 60)
    total_time = (time.time() - total_start) / 60
    print(f"Training complete in {total_time:.2f} minutes")
    print(f"Best loss: {best_loss:.4f}")
    print(f"Model saved to: {args.save_path}")

    return cross_losses, entropy_losses


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Behavior Cloning for APActor')

    # Dataset
    parser.add_argument('--dataset', type=str, required=True,
                        help='Path to JSON dataset file')

    # Training
    parser.add_argument('--epochs', type=int, default=100,
                        help='Number of training epochs')
    parser.add_argument('--batch_size', type=int, default=64,
                        help='Batch size')
    parser.add_argument('--lr', type=float, default=1e-3,
                        help='Learning rate')
    parser.add_argument('--entropy_coef', type=float, default=0.0,
                        help='Entropy bonus coefficient (0 to disable)')
    parser.add_argument('--clip_value', type=float, default=4.0,
                        help='Weight clipping value (0 to disable)')
    parser.add_argument('--warmup_steps', type=int, default=0,
                        help='Skip first/last N samples')
    parser.add_argument('--seed', type=int, default=42,
                        help='Random seed')

    # Model architecture
    parser.add_argument('--input_dim_ap', type=int, default=4,
                        help='Input feature dimension per AP')
    parser.add_argument('--hidden_dim', type=int, default=128,
                        help='Hidden dimension')
    parser.add_argument('--gnn_layers', type=int, default=2,
                        help='Number of GAT layers')
    parser.add_argument('--mlp_layers', type=int, default=3,
                        help='Number of MLP layers per head')
    parser.add_argument('--num_channels', type=int, default=3,
                        help='Number of channel width options')
    parser.add_argument('--num_power_levels', type=int, default=7,
                        help='Number of power level options')
    parser.add_argument('--heads', type=int, default=1,
                        help='Number of attention heads')
    parser.add_argument('--dropout', type=float, default=0.0,
                        help='Dropout rate')

    # Output
    parser.add_argument('--save_path', type=str, default='./actor_sft.pth',
                        help='Path to save trained model')

    args = parser.parse_args()
    main(args)
