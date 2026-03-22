import { LucideIcon } from 'lucide-react';

export type NodeType = 'ap' | 'client' | 'interferer';

export interface NetworkNode {
  id: number;
  type: NodeType;
  x: number; //  0-100
  y: number; // 0-100
  name: string;
  status?: 'good' | 'fair' | 'poor' | 'offline'; // For clients
  channel?: number; // For APs
  power?: number; // For APs and interferences
}

export interface Template {
  id: string;
  name: string;
  description: string;
  nodes: NetworkNode[];
  savedAt?: number; // Timestamp when template was saved
}

export interface ToolItem {
  type: NodeType;
  label: string;
  icon: LucideIcon;
  description: string;
  category: 'Infrastructure' | 'Clients' | 'Interferers';
}

export type OptimizerType = 'Bayesian Optimization' | 'Reinforcement Learning' | 'Genetic Algorithm';

export interface KPIMetrics {
  throughput: number; // Mbps
  retryRate: number; // %
  packetretryRate: number; // %
  latency: number; // ms
  churn: number; // changes/day
  score: number; // 0-100
}

export interface Session {
  id: string;
  name: string;
  templateId: string;
  createdAt: number;
  lastModified: number;
  nodes: NetworkNode[];
  nodeCount?: number; // Optional: node count from API when nodes array is not available
}

export interface APMetrics {
  count_clients: number;
  uplink_throughput_mbps: number | null;
  downlink_throughput_mbps: number | null;
  channel: number | null;
  bytes_sent: number | null;
  bytes_received: number | null;
  phy_tx_power_level: number | null;
  last_update_seconds: number;
  channel_width: number;
  interference: number | null;
}
