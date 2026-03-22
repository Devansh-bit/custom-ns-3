// Network visualization types

export interface AP {
  id: number;
  bssid: string;
  x: number;
  y: number;
  z?: number;
  channel: number;
  band: string;
  utilization: number;
  tx_power?: number;
  client_count: number;
  throughput?: number;
}

export interface STA {
  id: number;
  address: string;
  x: number;
  y: number;
  z?: number;
  connected_ap: string;
  rssi: number;
  snr?: number;
  throughput_up?: number;
  throughput_down?: number;
  latency: number;
  jitter?: number;
  packet_loss: number;
}

export interface Interferer {
  id: number;
  type: 'bluetooth' | 'microwave' | 'zigbee' | 'other';
  x: number;
  y: number;
  z?: number;
  power_dbm?: number;
  active: boolean;
}

export interface NetworkData {
  sim_time: number;
  aps: AP[];
  stas: STA[];
  interferers?: Interferer[];
}

export type HoveredNode = (AP & { type: 'ap' }) | (STA & { type: 'sta' }) | (Interferer & { type: 'interferer' }) | null;
