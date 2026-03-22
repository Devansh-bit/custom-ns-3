import { NetworkNode } from '@/lib/types';
import { sendBuildConfiguration } from './api-playground';

/**
 * Prepare build configuration from nodes
 */
export const prepareBuildConfiguration = (nodes: NetworkNode[]) => {
  const aps: { nodeId: number; position: { x: number; y: number; z: number } }[] = [];
  const waypoints: { id: number | undefined, x: number; y: number; z: number }[] = [];
  const stas: { nodeId: number; initialWaypointId: number }[] = [];
  const interferences: { nodeId: number; type: 'ble' | 'zigbee' | 'microwave'; position: { x: number; y: number; z: number } }[] = [];

  for (const n of nodes) {
    if (n.type === "ap") {
      const nodeId = n.id;
      aps.push({ nodeId, position: { x: n.x / 10, y: n.y / 10, z: 1 } });
    } else if (n.type === "client") {
      const position: { id: number | undefined, x: number, y: number, z: number } = { id: undefined, x: n.x / 10, y: n.y / 10, z: 1 };
      let position_index = waypoints.findIndex(wp =>
        wp.x === position.x && wp.y === position.y && wp.z === position.z
      );
      if (position_index === -1) {
        position.id = waypoints.length;
        waypoints.push(position);
        position_index = waypoints.length - 1;
      }
      stas.push({ nodeId: n.id, initialWaypointId: position_index });
    } else if (n.type === "ble" || n.type === "zigbee" || n.type === "microwave") {
      // Interference nodes: send coordinates
      const nodeId = n.id;
      interferences.push({
        nodeId,
        type: n.type,
        position: { x: n.x / 10, y: n.y / 10, z: 1 }
      });
    }
  }

  return { aps, stas, waypoints, interferences };
};

/**
 * Convert config (aps, stas, waypoints) to NetworkNode array
 * This is the reverse of prepareBuildConfiguration
 */
export const configToNodes = (config: any): NetworkNode[] => {
  const nodes: NetworkNode[] = [];
  
  // Convert APs
  if (config.aps && Array.isArray(config.aps)) {
    for (const ap of config.aps) {
      nodes.push({
        id: ap.nodeId,
        type: 'ap',
        x: ap.position.x * 10, // Convert back from config scale to canvas scale (0-100)
        y: ap.position.y * 10,
        name: `AP-${ap.nodeId}`,
        channel: ap.leverConfig?.channel,
        power: ap.leverConfig?.txPower
      });
    }
  }
  
  // Convert STAs (clients) using waypoints
  if (config.stas && Array.isArray(config.stas) && config.waypoints && Array.isArray(config.waypoints)) {
    for (const sta of config.stas) {
      const waypoint = config.waypoints[sta.initialWaypointId];
      if (waypoint) {
        const nodeId = typeof sta.nodeId === 'string' ? parseInt(sta.nodeId, 10) : (typeof sta.nodeId === 'number' ? sta.nodeId : 0);
        nodes.push({
          id: nodeId,
          type: 'client',
          x: waypoint.x * 10, // Convert back from config scale to canvas scale (0-100)
          y: waypoint.y * 10,
          name: `Client-${sta.nodeId}`,
          status: 'good'
        });
      }
    }
  }
  
  // Convert interferences
  const interferers = config.virtualInterferers?.interferers || config.interferences;
  if (interferers && Array.isArray(interferers)) {
    for (const interference of interferers) {
      const nodeId = typeof interference.nodeId === 'string' ? parseInt(interference.nodeId, 10) : (typeof interference.nodeId === 'number' ? interference.nodeId : 0);
      nodes.push({
        id: nodeId,
        type: interference.type as any,
        x: interference.position.x * 10, // Convert back from config scale to canvas scale (0-100)
        y: interference.position.y * 10,
        name: `${interference.type}-${interference.nodeId}`,
        power: interference.power
      });
    }
  }
  
  return nodes;
};

/**
 * Send build configuration to server
 */
export const sendBuildConfig = (
  nodes: NetworkNode[],
  sessionName?: string,
  createdAt?: number
): void => {
  const { aps, stas, waypoints, interferences } = prepareBuildConfiguration(nodes);
  sendBuildConfiguration({
    session_name: sessionName || 'unnamed-session',
    modified_at: createdAt ? new Date(createdAt) : new Date(),
    aps: aps as any,
    stas: stas,
    waypoints: waypoints,
    virtualInterferers: {
      interferers: interferences
    }
  });
};

