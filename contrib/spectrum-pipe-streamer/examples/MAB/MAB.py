from typing import Dict, List, Tuple, Any, Optional
import logging
import numpy as np
from collections import deque
import math
import random
from collections import namedtuple

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.distributions import Categorical
import logging
logger = logging.getLogger(__name__)

DFS_FREQUENCIES_MHZ = [5260, 5280, 5300, 5320, 5500, 5520, 5540, 5560,
                       5580, 5600, 5620, 5640, 5660, 5680, 5700, 5720]
DFS_FREQUENCY_TOLERANCE_MHZ = 10.0
DFS_CHANNEL_TOLERANCE_MHZ = 20.0
DFS_OVERRIDE_DWELL_MS = 7594.67

# Reproducibility
SEED = 42
random.seed(SEED)
np.random.seed(SEED)
torch.manual_seed(SEED)

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

CHANNEL_IDX = 0
DWELL_TIME = 0
# current_canvas = None  # global variable to hold the latest canvas
# def receive_canvas():
#     if current_canvas is not None:
#         return current_canvas
#     else:
#         raise ValueError("No canvas data available yet.")
#create two pointers to each channel and band
class Channel():
  def __init__(self,
               start: int,
               end: int,
               ):
    self.start = start
    self.end = end

    # can add more attributes that could be later utilised for channel specfic reward component

class Band:
    def __init__(self, window: np.ndarray, num_channels: int, channel_size: int):
        """
        window: 2D array (freq_bins x time_steps)
        num_channels: how many channels you want inside this band
        channel_size: number of frequency bins per channel
        """
        if window.ndim != 2:
            raise ValueError("window must be a 2D array (freq x time)")
        self.window = window.copy()
        self.freq_range = window.shape[0]
        self.temporal_range = window.shape[1]
        self.channels: List[Channel] = []
        self.overlap = [] # this contains tuples which is basically pairs
        self.channel_formulation(num_channels, channel_size)
        self.find_overlap(70)

    def channel_formulation(self, num_channels: int, channel_size: int):
        if channel_size <= 0:
            raise ValueError("channel_size must be > 0")
        if self.freq_range < channel_size:
            raise ValueError("channel_size cannot be larger than freq range")

        if num_channels <= 0:
            raise ValueError("num_channels must be > 0")

        # Place channels evenly across frequency axis without overlap when possible.
        if num_channels == 1:
            starts = [ (self.freq_range - channel_size) // 2 ]
        else:
            max_start = self.freq_range - channel_size
            # step might be fractional; use integer spacing
            step = max(1, max_start // (num_channels - 1))
            starts = [min(int(round(i * step)), max_start) for i in range(num_channels)]

        # ensure strictly non-decreasing and clipped
        starts = np.maximum.accumulate(np.array(starts, dtype=int)).tolist()
        starts = [max(0, min(s, self.freq_range - channel_size)) for s in starts]

        self.channels = [Channel(s, s + channel_size) for s in starts]

    def find_overlap(self, threshold: float):
        """
        For each channel i, include pairs (i,j) for j>i where
        overlap(i,j) >= threshold percent of channel i's height.
        Returns list of (i,j) with j>i, sorted.
        """
        K = len(self.channels)

        s = np.array([int(ch.start) for ch in self.channels], dtype=int)
        e = np.array([int(ch.end)   for ch in self.channels], dtype=int)
        pairs = []
        for i in range(K):
            hi = max(1, e[i] - s[i])  # denominator = height of channel i
            for j in range(i+1, K):
                ov = max(0, min(e[i], e[j]) - max(s[i], s[j]))
                pct = 100.0 * ov / hi
                if pct >= float(threshold):
                    pairs.append((int(i), int(j)))

        pairs = sorted(pairs)
        self.overlap = pairs

class Stream:
    def __init__(self):
        # current absolute time index for sampling (column index in canvases)
        self.time = 0

    def sense(self, canvas: np.ndarray, channel: Channel, dwell_time: int = 1) -> np.ndarray:
        """
        Read slice from the raw canvas (full spectrogram) for the given channel and current time.
        Returns a (channel_size x dwell_time) array.
        """
        if dwell_time < 1:
            raise ValueError("dwell_time must be >= 1")
        t0 = self.time
        t1 = self.time + dwell_time
        # Clip to canvas bounds
        t1 = min(t1, canvas.shape[1])
        # slice frequency rows channel.start:channel.end, columns t0:t1
        sensed = canvas[channel.start:channel.end, t0:t1]
        # If we requested more columns than available, pad with last column
        if sensed.shape[1] < dwell_time:
            if sensed.shape[1] == 0:
                # nothing left: pad with zeros (or repeat last known)
                pad = np.zeros((channel.end - channel.start, dwell_time))
            else:
                last_col = sensed[:, -1][:, None]
                pad_cols = np.repeat(last_col, dwell_time - sensed.shape[1], axis=1)
                pad = np.concatenate([sensed, pad_cols], axis=1)
            sensed = pad
        # do NOT advance time here — caller (update/step) controls time increments
        return sensed

    def advance_time(self, dt: int):
        if dt < 0:
            raise ValueError("dt must be non-negative")
        self.time += dt

    def update(self, bands: Dict[int, Band], canvases: Dict[int, np.ndarray],
               action: Tuple[int, int], dwell_time: int = 1) -> np.ndarray:
        """
        action = (band_index, channel_index)
        updates the windows for all bands: the selected band's window incorporates the new sensed energy
        other bands are rolled and filled with repeated last column (no new sensed energy).
        Returns the sensed_energy array used for reward calculation.
        """
        band_index, channel_index = action
        if band_index not in bands:
            raise KeyError(f"band {band_index} not in bands")
        band = bands[band_index]
        if channel_index < 0 or channel_index >= len(band.channels):
            raise IndexError("invalid channel index for chosen band")

        canvas = canvases[band_index]
        # get sensed energy for chosen band/channel at current time
        channel = band.channels[channel_index]
        sensed_energy = self.sense(canvas, channel, dwell_time=dwell_time)

        # shift / update chosen band's window
        W = band.window.shape[1]
        # ensure dwell_time <= W (if larger, we still behave: shift by min(dwell_time,W))
        shift = min(dwell_time, W)
        last_col = band.window[:, -1][:, None]
        new_cols = np.repeat(last_col, shift, axis=1)
        # place sensed energy into the frequency rows corresponding to the channel
        # if sensible shapes mismatch, crop/pad as needed
        csz = channel.end - channel.start
        if sensed_energy.shape != (csz, shift):
            # adapt sensed_energy to (csz, shift)
            # crop or pad with last col of sensed_energy
            se = np.zeros((csz, shift))
            use_cols = min(sensed_energy.shape[1], shift)
            se[:, :use_cols] = sensed_energy[:, :use_cols]
            if use_cols < shift:
                last = se[:, use_cols-1][:, None] if use_cols > 0 else np.zeros((csz,1))
                se[:, use_cols:] = np.repeat(last, shift - use_cols, axis=1)
            sensed_energy = se

        new_cols[channel.start:channel.end, :] = sensed_energy
        window_up = np.empty_like(band.window)
        if shift < W:
            window_up[:, :W-shift] = band.window[:, shift:]
            window_up[:, W-shift:] = new_cols
        else:  # shift == W: window becomes entirely new_cols (repeated last)
            window_up[:, :] = np.repeat(new_cols[:, -1][:, None], W, axis=1)

        bands[band_index].window = window_up

        # update untouched bands: shift left by 'shift' and pad with previous last column
        for i, b in bands.items():
            if i == band_index:
                continue
            wu = b.window
            W2 = wu.shape[1]
            s2 = min(shift, W2)
            last_col2 = wu[:, -1][:, None]
            new_cols2 = np.repeat(last_col2, s2, axis=1)
            window_u = np.empty_like(wu)
            if s2 < W2:
                window_u[:, :W2-s2] = wu[:, s2:]
                window_u[:, W2-s2:] = new_cols2
            else:
                window_u[:, :] = np.repeat(new_cols2[:, -1][:, None], W2, axis=1)
            bands[i].window = window_u

        # advance global time
        self.advance_time(shift)
        return sensed_energy

class SpectrogramEnv:
    def __init__(self,
                 data: Dict[int, List[np.ndarray]],
                 window_width: int,
                 num_channels: int,
                 channel_size: int,
                 dynamic_dwell: bool = False):
        """
        data: dict mapping band_id -> list of canvases (each canvas is freq x time)
        window_width: number of columns to keep in the sliding window per band
        """
        self.data = data
        self.window_width = int(window_width)
        self.num_channels = int(num_channels)
        self.channel_size = int(channel_size)
        self.dynamic_dwell_flag = bool(dynamic_dwell)

        self.stream = Stream()
        self.canvases: Dict[int, np.ndarray] = {}
        self.bands: Dict[int, Band] = {}
        self.reset()

    def reset(self):
        self.canvases = {}
        self.bands = {}
        # iterate over keys so that arbitrary band ids are supported
        for i in self.data.keys():
            if len(self.data[i]) == 0:
                raise ValueError(f"No canvases provided for band {i}")
            choice = np.random.randint(0, len(self.data[i]))
            canvas = self.data[i][choice]
            if canvas.ndim != 2:
                raise ValueError("Each canvas must be a 2D array (freq x time)")
            if canvas.shape[1] < self.window_width:
                raise ValueError("Canvas shorter than requested window_width")
            self.canvases[i] = canvas
            window = canvas[:, :self.window_width].copy()
            self.bands[i] = Band(window, self.num_channels, self.channel_size)
        self.stream.time = 0

    def step(self, action: Tuple[int, int], dwell_time: int = 1) -> float:
        """
        action: (band_index, channel_index)
        returns: reward (float) = sum of sensed energy (optionally minus dynamic penalty)
        """
        band_index, _ = action
        if band_index not in self.canvases:
            raise KeyError("Invalid band index in action")

        # clamp dwell_time against remaining columns in the chosen canvas
        canvas_width = self.canvases[band_index].shape[1]
        remaining = max(0, canvas_width - self.stream.time)
        if remaining == 0:
            # nothing left to sense; return zero reward
            return 0.0
        dwell_time = max(1, min(int(dwell_time), remaining))

        penalty = 0.0
        if self.dynamic_dwell_flag:
            penalty, dwell_time = self._dynamic_dwell_reward(dwell_time, remaining)

        sensed = self.stream.update(self.bands, self.canvases, action, dwell_time)
        reward = float(np.sum(sensed)) - float(penalty)
        return reward

    def _dynamic_dwell_reward(self, dwell_time: int, remaining: int) -> Tuple[float, int]:
        """
        Example penalty logic: penalize dwell_time outside [1,20], also ensure not beyond remaining columns.
        Returns (penalty, adjusted_dwell_time).
        """
        penalty = 0.0
        if dwell_time < 1:
            penalty = 1 - dwell_time
            dwell_time = 1
        elif dwell_time > 20:
            penalty = dwell_time - 20
            dwell_time = 20

        if dwell_time > remaining:
            # clip to remaining; does not add penalty beyond clipping
            dwell_time = remaining
        global DWELL_TIME
        DWELL_TIME = dwell_time
        
        return penalty, dwell_time

    def get_action_space(self) -> Dict[int, int]:
        """Return number of channels available per band."""
        return {i: len(b.channels) for i, b in self.bands.items()}

    def render_windows(self):
        """Utility to inspect current windows (returns deep copies)."""
        return {i: b.window.copy() for i, b in self.bands.items()}


# Cell 2 — Channel-window adapters (state prep)
def get_channel_window_from_obs(obs, channel_index):
    """
    Extract the 2D window for a selected channel from the current observation.
    Tries a few common layouts:
      - obs['channels'] with shape like (C, R, Cols) or (R, Cols, C)
      - raw ndarray with a channel dimension either first or last
      - if already 2D, return as-is
    """
    # try dict style
    if isinstance(obs, dict) and 'channels' in obs:
        ch = np.array(obs['channels'])
        if ch.ndim == 3:
            # (C, R, Cols)
            if channel_index < ch.shape[0]:
                return ch[channel_index]
            # (R, Cols, C)
            if channel_index < ch.shape[-1]:
                return ch[..., channel_index]
        if ch.ndim == 2:
            return ch

    # try ndarray style
    arr = np.array(obs)
    if arr.ndim == 3:
        if channel_index < arr.shape[-1]:
            return arr[..., channel_index]
        if channel_index < arr.shape[0]:
            return arr[channel_index]
    if arr.ndim == 2:
        return arr

    # fallback
    return np.array(obs)


def prepare_state_from_window(window_2d):
    """
    Per your spec: do NOT flatten the 2D window.
    Instead, take the mean along rows, so state_dim == num_columns.
    Returns 1D float32 array.
    """
    arr = np.array(window_2d, dtype=np.float32)
    if arr.ndim == 1:
        return arr.copy()
    return arr.mean(axis=0)

# # Cell 4 — PPO network + algorithm (discrete actions 0..19 → dwell 1..20)
Transition = namedtuple('Transition', ['state', 'action', 'logp', 'reward', 'done', 'value'])

class ActorCritic(nn.Module):
    def __init__(self, state_dim, action_dim, hidden_sizes=(128, 128)):
        super().__init__()
        layers = []
        last = state_dim
        for h in hidden_sizes:
            layers += [nn.Linear(last, h), nn.ReLU()]
            last = h
        self.shared = nn.Sequential(*layers)
        self.actor = nn.Linear(last, action_dim)
        self.critic = nn.Linear(last, 1)

    def forward(self, x):
        x = self.shared(x)
        logits = self.actor(x)
        value = self.critic(x).squeeze(-1)
        return logits, value


class PPO:
    def __init__(self, state_dim, action_dim=20, lr=3e-4, gamma=0.99, lam=0.95,
                 clip_eps=0.18, epochs=8, batch_size=64):
        self.net = ActorCritic(state_dim, action_dim).to(device)
        self.opt = optim.Adam(self.net.parameters(), lr=lr)
        self.gamma = gamma
        self.lam = lam
        self.clip_eps = clip_eps
        self.epochs = epochs
        self.batch_size = batch_size
        self.action_dim = action_dim

    def select_action(self, state):
        s = torch.from_numpy(state.astype(np.float32)).to(device)
        logits, value = self.net(s)
        probs = torch.softmax(logits, dim=-1)
        dist = Categorical(probs)
        a = dist.sample()
        return int(a.cpu().item()), float(dist.log_prob(a).cpu().item()), float(value.cpu().item())

    def compute_gae(self, rewards, values, dones):
        advs = np.zeros_like(rewards, dtype=np.float32)
        lastgaelam = 0.0
        for t in reversed(range(len(rewards))):
            nonterminal = 1.0 - dones[t]
            next_val = values[t+1] if (t + 1) < len(values) else 0.0
            delta = rewards[t] + self.gamma * next_val * nonterminal - values[t]
            lastgaelam = delta + self.gamma * self.lam * nonterminal * lastgaelam
            advs[t] = lastgaelam
        returns = advs + values
        return advs, returns

    def update(self, transitions):
        states = np.stack([t.state for t in transitions])
        actions = np.array([t.action for t in transitions], dtype=np.int64)
        old_logps = np.array([t.logp for t in transitions], dtype=np.float32)
        rewards = np.array([t.reward for t in transitions], dtype=np.float32)
        dones = np.array([t.done for t in transitions], dtype=np.float32)
        values = np.array([t.value for t in transitions], dtype=np.float32)

        advs, returns = self.compute_gae(rewards, values, dones)
        advs = (advs - advs.mean()) / (advs.std() + 1e-8)

        N = len(states)
        idx = np.arange(N)

        for _ in range(self.epochs):
            np.random.shuffle(idx)
            for start in range(0, N, self.batch_size):
                mb = idx[start:start + self.batch_size]
                s = torch.from_numpy(states[mb]).to(device)
                a = torch.from_numpy(actions[mb]).to(device)
                oldlp = torch.from_numpy(old_logps[mb]).to(device)
                adv = torch.from_numpy(advs[mb]).to(device)
                ret = torch.from_numpy(returns[mb]).to(device)

                logits, vals = self.net(s)
                probs = torch.softmax(logits, dim=-1)
                dist = Categorical(probs)
                newlp = dist.log_prob(a)

                ratio = (newlp - oldlp).exp()
                surr1 = ratio * adv
                surr2 = torch.clamp(ratio, 1.0 - self.clip_eps, 1.0 + self.clip_eps) * adv
                policy_loss = -torch.min(surr1, surr2).mean()
                value_loss = ((vals - ret) ** 2).mean()
                entropy = dist.entropy().mean()

                loss = policy_loss + 0.5 * value_loss - 0.01 * entropy

                self.opt.zero_grad()
                loss.backward()
                nn.utils.clip_grad_norm_(self.net.parameters(), 0.5)
                self.opt.step()

# Cell 5 — MAB→PPO controller (your integration point)
class MAB_PPO_Controller:
    """
    Wires the two agents without touching your env:
      - MAB picks channel
      - PPO sees only the selected channel's window (row-mean state)
      - PPO picks dwell (1..20)
      - Env steps with {'channel', 'dwell'}
      - Both agents get the same reward (as requested)
    """
    def __init__(self, env, mab_agent, ppo_agent, action_mapper=None):
        self.env = env
        self.mab = mab_agent
        self.ppo = ppo_agent
        self.action_mapper = action_mapper or (lambda a: int(a) + 1)  # 0..19 -> 1..20

    def run_episode(self, max_steps=100, ppo_update_every=256):
        obs = self.env.reset()
        done = False
        total_reward = 0.0
        transitions = []
        steps = 0

        while not done and steps < max_steps:
            # 1) MAB selects channel
            channel = self.mab.select(obs)

            # 2) Build PPO state from that channel window
            window = get_channel_window_from_obs(obs, channel)
            state = prepare_state_from_window(window)
            if state.ndim != 1:
                state = state.ravel()

            # 3) PPO selects dwell (discrete)
            a_idx, logp, val = self.ppo.select_action(state)
            dwell = self.action_mapper(a_idx)

            # 4) Step env with combined action
            action = {'channel': int(channel), 'dwell': int(dwell)}
            obs_next, reward, done, info = self.env.step(action)

            # 5) Store and update
            total_reward += float(reward)
            transitions.append(
                Transition(state=state, action=a_idx, logp=logp,
                           reward=float(reward), done=done, value=val)
            )
            self.mab.update(channel, float(reward))

            obs = obs_next
            steps += 1

            # PPO update on a simple cadence
            if len(transitions) >= ppo_update_every:
                self.ppo.update(transitions)
                transitions = []

        if transitions:
            self.ppo.update(transitions)

        return total_reward
    
# Cell 6 — Save / Load utilities
def save_models(mab, ppo, path_prefix='mab_ppo_checkpoint'):
    torch.save({'ppo': ppo.net.state_dict()}, f'{path_prefix}_ppo.pt')
    np.savez_compressed(f'{path_prefix}_mab.npz', counts=mab.counts, values=mab.values)

def load_models(mab, ppo, path_prefix='mab_ppo_checkpoint'):
    d = torch.load(f'{path_prefix}_ppo.pt', map_location=device)
    ppo.net.load_state_dict(d['ppo'])
    z = np.load(f'{path_prefix}_mab.npz')
    mab.counts = z['counts']
    mab.values = z['values']


def dwell_index_to_ms(index: int) -> float:
    index = max(1, min(int(index), 20))
    seconds = 0.050 + (index - 1) * (0.150 / 19)
    return seconds * 1000.0


class MABRunner:
    def __init__(self,
                 data: Dict[int, List[np.ndarray]],
                 window_width: int,
                 num_channels: int,
                 channel_size: int,
                 dynamic_dwell: bool = False,
                 freq_axes: Optional[Dict[int, np.ndarray]] = None):
        self.data = data
        self.window_width = int(window_width)
        self.num_channels = int(num_channels)
        self.channel_size = int(channel_size)
        self.dynamic_dwell = bool(dynamic_dwell)
        self.freq_axes = freq_axes or {}
        self.last_dwell_time = 1
        self.last_dwell_time_ms = dwell_index_to_ms(1)

    def normalize_reward(self, reward_raw: float, max_band_sum: float):
        r = float(reward_raw) / float(max_band_sum + 1e-12)
        if r < 0.0:
          return 0.0
        if r > 1.0:
            return 1.0
        return r

    def compute_max_ch_sum(self,
                           canvases: Dict[int, np.ndarray],
                           env_bands: Dict[int, Any],
                           stream_time: int,
                           dwell_time: int):

        # determine time horizon safely from canvases (assume all canvases have same time dim)
        first_canvas = next(iter(canvases.values()))
        time_horizon = first_canvas.shape[1]
        t0 = stream_time
        t1 = min(time_horizon, stream_time + dwell_time)

        sums = []
        # iterate env_bands to get channel ranges, use canvases to get the data slice
        for band_id, band_obj in env_bands.items():
            canvas = canvases[band_id]
            for ch in band_obj.channels:
                sl = canvas[ch.start:ch.end, t0:t1]
                sums.append(float(np.sum(sl)))
        return float(np.max(sums))

    def oracle(self, bands: Dict[int, np.ndarray],
           stream_time: int,
           dwell_time: int,
           env_bands: Dict[int, Any],
           max_band_sum: float):
      """
      Compute oracle (best channel) for the time-span [stream_time:stream_time+dwell_time).
      Returns (oracle_reward, oracle_channel_index, per_band_channel_rewards)
      per_band_channel_rewards are normalized using same normalization as agent reward.
      """
      # determine time horizon safely from canvases (assume same time dim for all)
      first_canvas = next(iter(bands.values()))
      time_horizon = first_canvas.shape[1]
      t0 = stream_time
      t1 = min(time_horizon, stream_time + dwell_time)

      per_band_channel_rewards: Dict[int, List[float]] = {}
      for band_id, canvas in bands.items():
          rewards = []
          band_obj = env_bands[band_id]
          for ch in band_obj.channels:
              sl = canvas[ch.start:ch.end, t0:t1]
              raw = float(np.sum(sl))
              rewards.append(self.normalize_reward(raw, max_band_sum))
          per_band_channel_rewards[band_id] = rewards

      best_band = None
      best_ch = None
      best_r = -float('inf')
      for b, lst in per_band_channel_rewards.items():
          for idx, rr in enumerate(lst):
              if rr > best_r:
                  best_r = rr
                  best_band = b
                  best_ch = idx

      if best_band is None:
          return 0.0, (-1, -1), per_band_channel_rewards

      return float(best_r), (best_band, int(best_ch)), per_band_channel_rewards


    def run_on_multi_band_canvas(self,
                                 env: Any,
                                 bands: Dict[int, np.ndarray],
                                 GETS: bool,
                                 agent: Optional[Any] = None,
                                 pair_agent: Optional[Any] = None,
                                 chooser: Optional[Any] = None,
                                 ppo_agent: Optional[Any] = None,
                                 freeze_learning: bool = False,
                                 dwell_time = 1) -> Dict[str, Any]:
        # set env canvases and build Band objects for each band
        env.canvases = bands
        UPDATE_PPO = 256

        env.bands = {}
        for band_id, canvas in bands.items():
            initial_window = canvas[:, :self.window_width].copy()
            env.bands[band_id] = Band(initial_window, self.num_channels, self.channel_size)

        # reset stream time to 0
        env.stream.time = 0

        # metrics
        steps = 0
        cum_reward = 0.0
        cum_oracle = 0.0
        coverage_hits = 0
        per_step_rewards = []

        # main loop: drive until stream.time reaches end of canvases' time dimension
        time_horizon = next(iter(bands.values())).shape[1] - self.window_width
        max_reward_sum = 0.0
        while env.stream.time < time_horizon:
            t0 = env.stream.time
            # choose action

            if GETS:
                pair_idx = int(pair_agent.select_arm())
                pairs = pair_agent.get_pairs()
                pair = pairs[pair_idx]
                chooser_out = chooser.select_channel(pair)
                #CHANNEL_NO_FROM_HERE
                band_id, channel_idx = int(chooser_out[0]), int(chooser_out[1])
                global CHANNEL_IDX
                CHANNEL_IDX = channel_idx
                self.last_selected_channel = channel_idx
            else:
                raw = agent.select_arm()
                band_id, channel_idx = int(raw[0]), int(raw[1])
                self.last_selected_channel = channel_idx

            action = (band_id, channel_idx)

            self.last_selected_band = band_id
            logger.info(f"Step {steps}: selected band {band_id}, channel {channel_idx} at time {env.stream.time}")
            

            # perform sensing via stream.update

            # sensed = env.stream.update(env.bands, env.canvases, action, dwell_time)

            # # reward raw is sum of sensed
            # reward_raw = float(np.sum(sensed))

            if ppo_agent is not None:
                band_obj = env.bands[band_id]
                ch = band_obj.channels[channel_idx]
                channel_window = band_obj.window[ch.start:ch.end, :]   # shape (channel_height, window_width)
                state = channel_window.ravel().astype(np.float32)

                action_sel, logp_sel, value_sel = ppo_agent.select_action(state)

                try:
                    action_idx = int(action_sel)
                except Exception:
                    action_idx = int(round(float(action_sel)))
                dwell_time = action_idx + 1
                
                total_time = next(iter(bands.values())).shape[1]
                remaining = max(1, total_time - env.stream.time - self.window_width)
                dwell_time = max(1, min(int(dwell_time), int(remaining)))
                self.last_dwell_time = dwell_time
                reported_dwell_ms = dwell_index_to_ms(dwell_time)
                if self.freq_axes and band_id in self.freq_axes:
                    freqs = self.freq_axes[band_id]
                    if isinstance(freqs, np.ndarray) and freqs.size > 0:
                        start_idx = max(0, min(int(ch.start), freqs.size - 1))
                        end_idx = max(0, min(int(max(ch.end - 1, ch.start)), freqs.size - 1))
                        lo_idx, hi_idx = sorted([start_idx, end_idx])
                        freq_min_mhz = float(freqs[lo_idx]) / 1e6
                        freq_max_mhz = float(freqs[hi_idx]) / 1e6
                        center_idx = int((ch.start + ch.end) / 2)
                        center_idx = max(0, min(center_idx, freqs.size - 1))
                        center_freq_mhz = float(freqs[center_idx]) / 1e6
                        if band_id == 1:
                            freq_lo = freq_min_mhz - DFS_CHANNEL_TOLERANCE_MHZ
                            freq_hi = freq_max_mhz + DFS_CHANNEL_TOLERANCE_MHZ
                            for dfs_freq in DFS_FREQUENCIES_MHZ:
                                if (abs(center_freq_mhz - dfs_freq) <= DFS_FREQUENCY_TOLERANCE_MHZ) or (freq_lo <= dfs_freq <= freq_hi):
                                    reported_dwell_ms = DFS_OVERRIDE_DWELL_MS
                                    break
                else:
                    reported_dwell_ms = dwell_index_to_ms(dwell_time)

                self.last_dwell_time = dwell_time
                self.last_dwell_time_ms = reported_dwell_ms
                logger.info(f"  PPO selected dwell time idx={dwell_time} ms={reported_dwell_ms:.2f}")


            reward_raw = env.step(action, dwell_time)

            start_new = t0 + self.window_width

            # compute max across channels for the same time-span used for sensing
            max_reward_sum = self.compute_max_ch_sum(bands, env.bands, start_new, dwell_time)

            # SAFETY: clamp very-small max to avoid division explosion (keeps normalization stable)
            max_band_sum_safe = max(float(max_reward_sum), 1e-8)

            # normalized reward
            r = self.normalize_reward(reward_raw, max_band_sum_safe)


            # oracle across all bands for this time-span
            oracle_r, (oracle_band, oracle_ch), per_band_ch = self.oracle(env.canvases, t0, dwell_time, env.bands, max_band_sum_safe)

            # metrics
            cum_reward += r
            cum_oracle += oracle_r
            per_step_rewards.append(r)
            if (band_id == oracle_band) and (channel_idx == oracle_ch):
                coverage_hits += 1


            if GETS:
              chooser.update((band_id, channel_idx), r)
              pair_agent.update(pair_idx, r)
            else:
              agent.update((band_id, channel_idx), r)

            # agent updates
            if not freeze_learning and ppo_agent is not None:
              done_flag = (env.stream.time >= time_horizon)
              ppo_transitions = []
              tr = Transition(state=state,
                            action=int(action_idx),
                            logp=float(logp_sel),
                            reward=float(r),
                            done=float(done_flag),
                            value=float(value_sel))
              ppo_transitions.append(tr)


            if not freeze_learning and ppo_agent is not None and len(ppo_transitions) >= UPDATE_PPO:
                ppo_agent.update(ppo_transitions)
                ppo_transitions = []

            steps += 1

        avg_reward = float(np.mean(per_step_rewards))
        coverage = float(coverage_hits) / float(steps)
        regret = float(cum_oracle - cum_reward)

        return {
            'steps': steps,
            'cumulative_reward': float(cum_reward),
            'average_reward': avg_reward,
            'cumulative_oracle_reward': float(cum_oracle),
            'coverage': coverage,
            'regret': regret,
            'max_band_sum': float(max_reward_sum),
            'last_selected_band': self.last_selected_band,
            'last_selected_channel': self.last_selected_channel,
            'dwell_time': self.last_dwell_time,
            'dwell_time_ms': getattr(self, 'last_dwell_time_ms', dwell_index_to_ms(self.last_dwell_time))

        }

    def train_sequential(self,
                        agent: Optional[Any] = None,
                        pair_agent: Optional[Any] = None,
                        chooser: Optional[Any] = None,
                        ppo_agent: Optional[Any] = None,
                        GETS: bool = False,
                        freeze_learning: bool = False,
                        dynamic_dwell: bool = False,
                        dwell_time: int = 1,
                        ) -> Dict[int, List[Dict[str, Any]]]:
        """Train sequentially across bands using the same canvas index from every band.

        The data dict is assumed to have lists of the same length for every band. We iterate
        over canvas index `i` and take `data[band][i]` for every band to form the multi-band
        episode input.

        Returns a dict: canvas_index -> list of per-episode metric dicts (one entry per episode)
        (Note: using canvas_index as key keeps results aligned across bands since episodes combine bands.)
        """
        # construct an Env instance from your notebook's SpectrogramEnv
        env = SpectrogramEnv(data=self.data,
                            window_width=self.window_width,
                            num_channels=self.num_channels,
                            channel_size=self.channel_size,
                            dynamic_dwell=self.dynamic_dwell)

        # determine number of canvases (assume equal lengths)
        first_band = next(iter(self.data.keys()))
        n_canvases = len(self.data[first_band])

        results: Dict[int, List[Dict[str, Any]]] = {}
        for i in range(n_canvases):
            # assemble canvases dict for this index
            canvases_i: Dict[int, np.ndarray] = {band_id: self.data[band_id][i] for band_id in self.data.keys()}
            res = self.run_on_multi_band_canvas(env=env,
                                                bands=canvases_i,
                                                GETS=GETS,
                                                agent=agent,
                                                pair_agent=pair_agent,
                                                chooser=chooser,
                                                ppo_agent=ppo_agent,
                                                freeze_learning=freeze_learning,
                                                dwell_time=dwell_time)
            results[i] = [res]
        return results


    def evaluate(self,
                agent: Any,
                canvases: Dict[int, np.ndarray],
                GETS: bool = False,
                pair_agent: Optional[Any] = None,
                chooser: Optional[Any] = None,
                dynamic_dwell: bool = False,
                ppo_agent: Any = None,
                dwell_time: int = 1
                ) -> Dict[str, Any]:
        """Evaluate agent on a multi-band episode with learning frozen.
        Simply calls run_on_multi_band_canvas with freeze_learning=True.
        """
        env = SpectrogramEnv(data=self.data,
                            window_width=self.window_width,
                            num_channels=self.num_channels,
                            channel_size=self.channel_size,
                            dynamic_dwell=self.dynamic_dwell)
        return self.run_on_multi_band_canvas(env=env,
                                            bands=canvases,
                                            GETS=GETS,
                                            agent=agent,
                                            pair_agent=pair_agent,
                                            chooser=chooser,
                                            ppo_agent=ppo_agent,
                                            freeze_learning=True,
                                            dwell_time=dwell_time)
class PairDiscountedBetaThompsonAgent:
    """
    Discounted Beta-Thompson agent for pair-arms.

    - pairs: list of pairs, where each pair is ((band,chan), (band,chan))
    - alpha/beta are maintained per pair index (Beta posterior)
    - gamma in (0,1] is the discount factor applied to all alpha/beta at each update
    Methods:
      - set_pairs(pairs)
      - get_pairs() -> pairs
      - select_arm() -> index (int)
      - update(idx, reward)
      - reset()
    """
    def __init__(self,
                 pairs: Optional[List[Tuple[Tuple[int,int], Tuple[int,int]]]] = None,
                 alpha0: float = 1.0,
                 beta0: float = 1.0,
                 gamma: float = 0.995):
        self.alpha0 = float(alpha0)
        self.beta0 = float(beta0)
        self.gamma = float(gamma)
        self.pairs: List[Tuple[Tuple[int,int], Tuple[int,int]]] = []
        self.alpha: List[float] = []
        self.beta: List[float] = []
        if pairs is not None:
            self.set_pairs(pairs)

    def set_pairs(self, pairs: List[Tuple[Tuple[int,int], Tuple[int,int]]]):
        """Set the list of pair-arms and reinitialize alpha/beta."""
        self.pairs = [(tuple(p[0]), tuple(p[1])) for p in pairs]
        n = len(self.pairs)
        self.alpha = [self.alpha0 for _ in range(n)]
        self.beta  = [self.beta0  for _ in range(n)]

    def get_pairs(self) -> List[Tuple[Tuple[int,int], Tuple[int,int]]]:
        return self.pairs

    def select_arm(self) -> int:
        """Thompson sampling: draw theta ~ Beta(alpha,beta) for each pair and return best index."""
        eps = 1e-10
        samples = []
        for i in range(len(self.pairs)):
            aa = self.alpha[i] if self.alpha[i] > eps else eps
            bb = self.beta[i]  if self.beta[i]  > eps else eps
            samples.append(float(np.random.beta(aa, bb)))
        best_idx = int(np.argmax(samples))
        return best_idx

    def update(self, idx: int, reward: float):
        """Discount all parameters, then update pair idx with clamped reward in [0,1]."""
        # discount
        for i in range(len(self.alpha)):
            self.alpha[i] *= self.gamma
            self.beta[i]  *= self.gamma

        # clamp reward
        r = float(reward)
        if r < 0.0: r = 0.0
        if r > 1.0: r = 1.0

        # fractional update
        self.alpha[idx] += r
        self.beta[idx]  += (1.0 - r)

        # enforce tiny positive floor to avoid zero/negative params
        eps = 1e-10
        for i in range(len(self.alpha)):
            if self.alpha[i] < eps: self.alpha[i] = eps
            if self.beta[i]  < eps: self.beta[i]  = eps

    def reset(self):
        """Reset all pair posterior params to the priors."""
        for i in range(len(self.alpha)):
            self.alpha[i] = self.alpha0
            self.beta[i]  = self.beta0

class EpsilonGreedyChooser:
    """Epsilon-greedy chooser that, given a pair (two channel tuples), selects one of them.

    - Maintains per-channel value estimates Q and counts N, discounting on update.
    - Methods: select_channel(pair) -> (band_id, channel_idx), update(channel, reward), reset(), set_channels(channels)
    """
    def __init__(self, epsilon: float = 0.1, gamma: float = 0.99):
        self.epsilon = float(epsilon)
        self.gamma = float(gamma)
        self.Q = {}  # channel -> estimate
        self.N = {}  # channel -> (float) effective count

    def set_channels(self, channels: List[Tuple[int,int]]):
        for ch in channels:
            t = tuple(ch)
            if t not in self.Q:
                self.Q[t] = 0.0
                self.N[t] = 0.0

    def select_channel(self, pair: Tuple[Tuple[int,int], Tuple[int,int]]):
        # assume pair is two channel tuples; choose epsilon-random else greedily by Q
        if np.random.rand() < self.epsilon:
            # random choice
            if np.random.rand() < 0.5:
                return pair[0]
            return pair[1]
        # greedy
        a = tuple(pair[0])
        b = tuple(pair[1])
        qa = self.Q.get(a, 0.0)
        qb = self.Q.get(b, 0.0)
        if qa >= qb:
            return a
        return b

    def update(self, channel: Tuple[int,int], reward: float):
        ch = tuple(channel)
        if ch not in self.Q:
            self.Q[ch] = 0.0
            self.N[ch] = 0.0
        # discount all counts and Qs
        for k in list(self.Q.keys()):
            self.Q[k] *= self.gamma
            self.N[k] *= self.gamma
        # incremental update on discounted counts
        self.N[ch] += 1.0
        # incremental mean update using effective count
        self.Q[ch] += (float(reward) - self.Q[ch]) / float(self.N[ch])

    def reset(self):
        for k in list(self.Q.keys()):
            self.Q[k] = 0.0
            self.N[k] = 0.0


# canvas_parts = [current_canvas]
# # verify shapes align in frequency dimension
# freqs = [c.shape[0] for c in canvas_parts]
# if len(set(freqs)) != 1:
#     raise RuntimeError("Frequency dimension mismatch across files: " + str(freqs))
# canvas = np.concatenate(canvas_parts, axis=1)   # freq x (sum of times)

# # Data structure exactly as you gave it
# data = {0: canvas_parts}

#TESTING NOT DONE T_T

# -------------------
# Aggregation helper
# -------------------
def aggregate_results(results_dict: Dict[int, List[Dict[str, Any]]]) -> Dict[str, Any]:
    total_steps = 0
    total_cum = 0.0
    total_oracle = 0.0
    total_regret = 0.0
    sum_cov_hits = 0.0

    for idx, recs in results_dict.items():
        for r in recs:
            s = int(r.get('steps', 0))
            total_steps += s
            total_cum += float(r.get('cumulative_reward', 0.0))
            total_oracle += float(r.get('cumulative_oracle_reward', 0.0))
            total_regret += float(r.get('regret', 0.0))
            sum_cov_hits += float(r.get('coverage', 0.0)) * s

    avg_reward = (total_cum / float(total_steps)) if total_steps > 0 else 0.0
    coverage = (sum_cov_hits / float(total_steps)) if total_steps > 0 else 0.0

    return {
        'total_steps': total_steps,
        'total_cumulative_reward': total_cum,
        'total_cumulative_oracle': total_oracle,
        'total_regret': total_regret,
        'average_reward_per_step': avg_reward,
        'coverage': coverage
    }

def run_agents_and_print(runner: Any, DYNAMIC_DWELL = False):
    # build env once to extract arms/pairs
    env = SpectrogramEnv(data=runner.data,
                         window_width=runner.window_width,
                         num_channels=runner.num_channels,
                         channel_size=runner.channel_size,
                         dynamic_dwell=runner.dynamic_dwell)

    arms = []
    for band_id, band_obj in env.bands.items():
        for ch_idx, ch in enumerate(band_obj.channels):
            arms.append((int(band_id), int(ch_idx)))

    pairs = []
    for band_id, band_obj in env.bands.items():
        for (i, j) in getattr(band_obj, 'overlap', []):
            pairs.append(((int(band_id), int(i)), (int(band_id), int(j))))

    results_final = []

    # list of (name, instance, is_pair)
    agents_to_run = [
        ("pair_discounted_beta_thompson", PairDiscountedBetaThompsonAgent(pairs=pairs, alpha0=1.0, beta0=1.0,gamma=0.995), True)
    ]

    chooser = EpsilonGreedyChooser(epsilon=0.05)
    chooser.set_channels(arms)


    state_dim = int(runner.channel_size) * int(runner.window_width)
    ppo_agent = PPO(
        state_dim=state_dim,
        action_dim=20,
        lr=3e-4,
        gamma=0.99,
        clip_eps=0.2
    )

    final_test_results = []

    for name, inst, is_pair in agents_to_run:
        if inst is None:
            continue
        # if pair agent: create chooser
        print(f"Currently running {name}")
        if is_pair:
            if DYNAMIC_DWELL:
              raw_results = runner.train_sequential(agent=None,
                                                  pair_agent=inst,
                                                  chooser=chooser,
                                                  ppo_agent = ppo_agent,
                                                  GETS=True,
                                                  freeze_learning=False,
                                                  dynamic_dwell=runner.dynamic_dwell,
                                                  dwell_time=1)
            else:
              raw_results = runner.train_sequential(agent=None,
                                                  pair_agent=inst,
                                                  chooser=chooser,
                                                  GETS=True,
                                                  freeze_learning=False,
                                                  dynamic_dwell=runner.dynamic_dwell,
                                                  dwell_time=1)
        else:
            if DYNAMIC_DWELL:
              raw_results = runner.train_sequential(agent=inst,
                                                  pair_agent=None,
                                                  chooser=None,
                                                  ppo_agent = ppo_agent,
                                                  GETS=False,
                                                  freeze_learning=False,
                                                  dynamic_dwell=runner.dynamic_dwell,
                                                  dwell_time=1)
            else:
              raw_results = runner.train_sequential(agent=inst,
                                                  pair_agent=None,
                                                  chooser=None,
                                                  GETS=False,
                                                  freeze_learning=False,
                                                  dynamic_dwell=runner.dynamic_dwell,
                                                  dwell_time=1)
        # aggregate per-canvas -> results1
        results1 = aggregate_results(raw_results)
        results1['agent'] = name
        results_final.append(results1)
        return results_final

        # test_result = []
        # if is_pair:
        #     if DYNAMIC_DWELL:
        #       test_result = runner.evaluate(agent = None,
        #                                     canvases = test_data,
        #                                     GETS = True,
        #                                     pair_agent = inst,
        #                                     chooser = chooser,
        #                                     dynamic_dwell = True,
        #                                     ppo_agent=ppo_agent,
        #                                     dwell_time = 1)

        #     else:
        #       test_result = runner.evaluate(agent = None,
        #                                     canvases = test_data,
        #                                     GETS = True,
        #                                     pair_agent = inst,
        #                                     chooser = chooser,
        #                                     dynamic_dwell = False,
        #                                     dwell_time = 1)
        # else:
        #     if DYNAMIC_DWELL:
        #       test_result = runner.evaluate(agent = inst,
        #                                     canvases = test_data,
        #                                     GETS = False,
        #                                     pair_agent = None,
        #                                     chooser = None,
        #                                     dynamic_dwell = True,
        #                                     ppo_agent=ppo_agent,
        #                                     dwell_time = 1)
        #     else:
        #       test_result = runner.evaluate(agent = inst,
        #                                     canvases = test_data,
        #                                     GETS = False,
        #                                     pair_agent = None,
        #                                     chooser = None,
        #                                     dynamic_dwell = False,
        #                                     dwell_time = 1)

        # results2 = aggregate_results({0: [test_result]})
        # results2['agent'] = name
        # final_test_results.append(results2)

    # # sort results_final by total_cumulative_reward desc and print
    # results_final.sort(key=lambda x: x['total_cumulative_reward'], reverse=True)

    # print("\\nAgent ranking by total cumulative reward(train):\\n")
    # for r in results_final:
    #     print(f"Agent: {r['agent']}")
    #     print(f"  Total steps: {r['total_steps']}")
    #     print(f"  Cumulative reward: {r['total_cumulative_reward']:.6f}")
    #     print(f"  Cumulative oracle: {r['total_cumulative_oracle']:.6f}")
    #     print(f"  Regret: {r['total_regret']:.6f}")
    #     print(f"  Avg reward/step: {r['average_reward_per_step']:.6f}")
    #     print(f"  Coverage: {r['coverage']:.6f}\\n")


    # final_test_results.sort(key=lambda x: x['total_cumulative_reward'], reverse=True)

    # print("\\nAgent ranking by total cumulative reward(test):\\n")
    # for r in final_test_results:
    #     print(f"Agent: {r['agent']}")
    #     print(f"  Total steps: {r['total_steps']}")
    #     print(f"  Cumulative reward: {r['total_cumulative_reward']:.6f}")
    #     print(f"  Cumulative oracle: {r['total_cumulative_oracle']:.6f}")
    #     print(f"  Regret: {r['total_regret']:.6f}")
    #     print(f"  Avg reward/step: {r['average_reward_per_step']:.6f}")
    #     print(f"  Coverage: {r['coverage']:.6f}\\n")

    # return results_final, final_test_results

# print("Results without dynamic dwell")
# # ---------- Runner / env params (tune per your notebook) ----------
# WINDOW_WIDTH = 256       # example; set to what your notebook expects
# NUM_CHANNELS = 14         # set accordingly
# CHANNEL_SIZE = 256        # set accordingly
# DYNAMIC_DWELL = False

# # instantiate runner (uses your notebook's SpectrogramEnv when available)
# runner = MABRunner(
#     data=data,
#     window_width=WINDOW_WIDTH,
#     num_channels=NUM_CHANNELS,
#     channel_size=CHANNEL_SIZE,
#     dynamic_dwell=DYNAMIC_DWELL
# )

# results_final, final_test_results = run_agents_and_print(runner,False)

# print("Done")

# print("Results with dynamic dwell")
# # ---------- Runner / env params (tune per your notebook) ----------
# WINDOW_WIDTH = 256       # example; set to what your notebook expects
# NUM_CHANNELS = 14         # set accordingly
# CHANNEL_SIZE = 256        # set accordingly
# DYNAMIC_DWELL = True

# # instantiate runner (uses your notebook's SpectrogramEnv when available)
# runner = MABRunner(
#     data=data,
#     window_width=WINDOW_WIDTH,
#     num_channels=NUM_CHANNELS,
#     channel_size=CHANNEL_SIZE,
#     dynamic_dwell=DYNAMIC_DWELL
# )

# results_final, final_test_results = run_agents_and_print(runner,True)

# print("Done")

# import pandas as pd
# df = pd.DataFrame(results_final)
# df

# import pandas as pd
# df = pd.DataFrame(results_final)

# # If some expected keys are missing, create them with sensible defaults
# expected_cols = [
#     'agent',
#     'total_steps',
#     'total_cumulative_reward',
#     'average_reward_per_step',
#     'coverage'
# ]
# for c in expected_cols:
#     if c not in df.columns:
#         df[c] = 0.0

# # Sort by cumulative reward descending
# df = df.sort_values(by='total_cumulative_reward', ascending=False).reset_index(drop=True)

# # Friendly formatting
# df_display = df[expected_cols].copy()
# df_display['total_steps'] = df_display['total_steps'].astype(int)
# df_display['total_cumulative_reward'] = df_display['total_cumulative_reward'].map('{:,.2f}'.format)
# df_display['average_reward_per_step'] = df_display['average_reward_per_step'].map('{:.2f}'.format)
# df_display['coverage'] = df_display['coverage'].map('{:.2f}'.format)

# # Print a clear header and the table
# print("=== Agent ranking by TOTAL CUMULATIVE REWARD (descending) ===\n")
# print(f"Total cumulative reward of Oracle: {df['total_cumulative_oracle'].iloc[0]}")
# print(df_display.to_string(index=False))

# #return type to file should be DWELL_TIME, CHANNEL_IDX