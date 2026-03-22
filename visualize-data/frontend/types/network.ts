export interface AP {
  id: number;
  bssid: string;
  x: number;
  y: number;
  z: number;
  channel: number;
  band: string;
  utilization: number;
  tx_power: number;
  client_count: number;
  throughput: number;
  // Display positions for smooth animation
  displayX?: number;
  displayY?: number;
}

export interface STA {
  id: number;
  address: string;
  x: number;
  y: number;
  z: number;
  connected_ap: string;
  rssi: number;
  snr: number;
  throughput_up: number;
  throughput_down: number;
  latency: number;
  jitter: number;
  packet_loss: number;
  // Display positions for smooth animation
  displayX?: number;
  displayY?: number;
}

export interface NetworkData {
  sim_time: number;
  aps: AP[];
  stas: STA[];
}

export type HoveredNode = (AP & { type: 'ap' }) | (STA & { type: 'sta' }) | null;
