'use client';

import { NetworkData } from '@/types/network';
import { ConnectionStatus } from '@/hooks/useWebSocket';
import styles from './StatsPanel.module.css';

interface Props {
  data: NetworkData | null;
  status: ConnectionStatus;
}

export default function StatsPanel({ data, status }: Props) {
  return (
    <div className={styles.panel}>
      <h3 className={styles.title}>Network Status</h3>

      <div className={styles.stat}>
        <span className={styles.label}>Simulation Time</span>
        <span className={styles.value}>{data?.sim_time.toFixed(1) ?? '0.0'}s</span>
      </div>

      <div className={styles.stat}>
        <span className={styles.label}>Access Points</span>
        <span className={styles.value}>{data?.aps.length ?? 0}</span>
      </div>

      <div className={styles.stat}>
        <span className={styles.label}>Stations</span>
        <span className={styles.value}>{data?.stas.length ?? 0}</span>
      </div>

      <div className={styles.stat}>
        <span className={styles.label}>WebSocket</span>
        <span className={`${styles.status} ${styles[status]}`}>
          {status === 'connected' ? 'Connected' : status === 'connecting' ? 'Connecting...' : 'Disconnected'}
        </span>
      </div>
    </div>
  );
}
