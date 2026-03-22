'use client';

import { HoveredNode } from '@/types/network';
import styles from './Tooltip.module.css';

interface Props {
  node: HoveredNode;
  x: number;
  y: number;
}

function getRssiClass(rssi: number): string {
  if (rssi > -50) return styles.good;
  if (rssi > -70) return styles.warning;
  return styles.bad;
}

function getLatencyClass(latency: number): string {
  if (latency < 10) return styles.good;
  if (latency < 50) return styles.warning;
  return styles.bad;
}

function getLossClass(loss: number): string {
  if (loss < 0.01) return styles.good;
  if (loss < 0.05) return styles.warning;
  return styles.bad;
}

function getUtilClass(util: number): string {
  if (util < 0.5) return styles.good;
  if (util < 0.8) return styles.warning;
  return styles.bad;
}

function getInterfererTypeLabel(type: string): string {
  const labels: Record<string, string> = {
    bluetooth: 'Bluetooth',
    microwave: 'Microwave Oven',
    zigbee: 'ZigBee',
    other: 'Other',
  };
  return labels[type] || type;
}

export default function Tooltip({ node, x, y }: Props) {
  if (!node) return null;

  // Adjust position to stay on screen
  const left = Math.min(x + 15, window.innerWidth - 300);
  const top = Math.min(y + 15, window.innerHeight - 300);

  return (
    <div className={styles.tooltip} style={{ left, top }}>
      {node.type === 'ap' ? (
        <>
          <div className={styles.title}>Access Point {node.id}</div>
          <div className={styles.metric}>
            <span className={styles.label}>BSSID</span>
            <span className={styles.value}>{node.bssid}</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Channel</span>
            <span className={styles.value}>{node.channel} ({node.band})</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>TX Power</span>
            <span className={styles.value}>{node.tx_power?.toFixed(1)} dBm</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Utilization</span>
            <span className={`${styles.value} ${getUtilClass(node.utilization)}`}>
              {(node.utilization * 100).toFixed(1)}%
            </span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Clients</span>
            <span className={styles.value}>{node.client_count}</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Throughput</span>
            <span className={styles.value}>{node.throughput?.toFixed(1)} Mbps</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Position</span>
            <span className={styles.value}>({node.x?.toFixed(1)}, {node.y?.toFixed(1)})</span>
          </div>
        </>
      ) : node.type === 'sta' ? (
        <>
          <div className={styles.title}>Station {node.id}</div>
          <div className={styles.metric}>
            <span className={styles.label}>Address</span>
            <span className={styles.value}>{node.address}</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Connected AP</span>
            <span className={styles.value}>{node.connected_ap?.slice(-8)}</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>RSSI</span>
            <span className={`${styles.value} ${getRssiClass(node.rssi)}`}>
              {node.rssi?.toFixed(1)} dBm
            </span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>SNR</span>
            <span className={styles.value}>{node.snr?.toFixed(1)} dB</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Throughput (Up)</span>
            <span className={styles.value}>{node.throughput_up?.toFixed(2)} Mbps</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Throughput (Down)</span>
            <span className={styles.value}>{node.throughput_down?.toFixed(2)} Mbps</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Latency</span>
            <span className={`${styles.value} ${getLatencyClass(node.latency)}`}>
              {node.latency?.toFixed(2)} ms
            </span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Jitter</span>
            <span className={styles.value}>{node.jitter?.toFixed(2)} ms</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Packet Loss</span>
            <span className={`${styles.value} ${getLossClass(node.packet_loss)}`}>
              {(node.packet_loss * 100).toFixed(2)}%
            </span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Position</span>
            <span className={styles.value}>({node.x?.toFixed(1)}, {node.y?.toFixed(1)})</span>
          </div>
        </>
      ) : (
        <>
          <div className={styles.title}>Interferer {node.id}</div>
          <div className={styles.metric}>
            <span className={styles.label}>Type</span>
            <span className={styles.value}>{getInterfererTypeLabel(node.type)}</span>
          </div>
          {node.power_dbm !== undefined && (
            <div className={styles.metric}>
              <span className={styles.label}>TX Power</span>
              <span className={styles.value}>{node.power_dbm?.toFixed(1)} dBm</span>
            </div>
          )}
          <div className={styles.metric}>
            <span className={styles.label}>Status</span>
            <span className={styles.value}>{node.active ? 'Active' : 'Inactive'}</span>
          </div>
          <div className={styles.metric}>
            <span className={styles.label}>Position</span>
            <span className={styles.value}>({node.x?.toFixed(1)}, {node.y?.toFixed(1)})</span>
          </div>
        </>
      )}
    </div>
  );
}
