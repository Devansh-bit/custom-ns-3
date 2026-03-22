import { APMetrics, Session } from '@/lib/types';
import axios from 'axios';

// Base API URL - adjust this based on your API server configuration
export const API_BASE_URL = `http://${process.env.NEXT_PUBLIC_API_URL || 'localhost:8000'}`;
export const API_WS_URL = `ws://${process.env.NEXT_PUBLIC_API_URL || 'localhost:8000'}`;

// Create axios instance with default configuration
const apiClient = axios.create({
  baseURL: API_BASE_URL,
  headers: {
    'Content-Type': 'application/json',
  },
});

export interface BuildConfiguration {
  session_name: string
  modified_at: Date
  aps: {
    nodeId: number,
    position: { x: number, y: number, z: number }
  }[],
  stas: any,
  waypoints: any,
  virtualInterferers?: {
    interferers:
    {
      nodeId: number,
      type: 'ble' | 'zigbee' | 'microwave',
      position: { x: number, y: number, z: number }
    }[]
  }
};

/**
 * Send build configuration to the server
 * @param config - The configuration to send to the serer
 * @returns Promise containing array of AP BSSID strings (MAC addresses)
 */
export async function sendBuildConfiguration(config: BuildConfiguration) {
  try {
    await apiClient.post('/playground/config', config);
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error fetching AP list:', error.message);
      throw new Error(`Failed to fetch AP list: ${error.message}`);
    }
    throw error;
  }
}

export interface LogEntry {
  id: string;
  timestamp: number;
  message: string;
  type: 'info' | 'success' | 'warning' | 'error';
  source: 'System' | 'Planner' | 'AI' | 'Device' | 'Roaming' | 'RRM' | 'Metrics' | 'Sensing';
}

/**
 * Fetch AP metrics from the simulation database for a specific AP.
 * @param nodeId - The ID of the AP node.
 * @param limit - Optional limit for the number of results.
 * @returns Promise containing the AP metrics.
 */
export async function fetchSimulationAPMetrics(nodeId: number, limit?: number): Promise<APMetrics> {
  try {
    const response = await apiClient.get(`/simulation/ap/metrics/${nodeId}`, { params: { limit } });
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error fetching simulation AP metrics for ${nodeId}:`, error.message);
      throw new Error(`Failed to fetch simulation AP metrics: ${error.message}`);
    }
    throw error;
  }
}

export interface ClientMetrics {
  latency?: number;
  jitter?: number;
  packet_loss_rate?: number;
  rssi?: number;
  snr?: number;
}

/**
 * Fetch Client metrics from the simulation database for a specific Client.
 * @param nodeId - The ID of the Client node.
 * @returns Promise containing the Client metrics.
 */
export async function fetchSimulationClientMetrics(nodeId: number): Promise<ClientMetrics> {
  try {
    const response = await apiClient.get(`/simulation/client/metrics/${nodeId}`);
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error fetching simulation Client metrics for ${nodeId}:`, error.message);
      throw new Error(`Failed to fetch simulation Client metrics: ${error.message}`);
    }
    throw error;
  }
}

export interface NetworkMetrics {
  throughput: number | null;
  latency: number | null;
  jitter: number | null;
  loss_rate: number | null;
}

/**
 * Fetch logs from the server
 * @param limit - Maximum number of logs to return (default: 100)
 * @returns Promise containing array of log entries
 */
export async function fetchInitialLogs(limit: number = 100): Promise<LogEntry[]> {
  try {
    const response = await apiClient.get('/playground/logs/initial', { params: { limit } });
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error fetching logs:', error.message);
      throw new Error(`Failed to fetch logs: ${error.message}`);
    }
    throw error;
  }
}

/**
 * Fetch recent logs from the server that are newer than the given IDs
 * @param sinceRoamId - The roam_id to fetch roaming logs after
 * @param sinceRrmId - The rrm_change_id to fetch RRM changes after
 * @param sinceSensingId - The sensing_result_id to fetch sensing logs after
 * @returns Promise containing array of log entries newer than the given IDs
 */
export async function fetchRecentLogs(sinceRoamId: number, sinceRrmId: number, sinceSensingId: number): Promise<LogEntry[]> {
  const response = await apiClient.get('/playground/logs/recent', {
    params: { since_roam_id: sinceRoamId, since_rrm_id: sinceRrmId, since_sensing_id: sinceSensingId }
  });
  return response.data;
}

/**
 * Start the simulation
 * @param sessionName - The name of the session to start
 * @returns Promise containing the response status
 */
export async function startSimulation(sessionName: string): Promise<{ status: string; message: string }> {
  try {
    const response = await apiClient.post(`/simulation/start/${sessionName}`);
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error starting simulation:', error.message);
      throw new Error(`Failed to start simulation: ${error.message}`);
    }
    throw error;
  }
}

/**
 * Stop the simulation
 * @returns Promise containing the response status
 */
export async function stopSimulation(): Promise<{ status: string; message: string }> {
  try {
    const response = await apiClient.post('/simulation/stop');
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error stopping simulation:', error.message);
      throw new Error(`Failed to stop simulation: ${error.message}`);
    }
    throw error;
  }
}

/**
 * Send a stress test command to the running simulation
 * @param command - The stress test command type
 * @param parameters - Optional parameters for the stress test
 * @returns Promise containing the response status
 */
export async function sendStressTestCommand(
  command: 'HIGH_INTERFERENCE' | 'FORCE_DFS' | 'HIGH_THROUGHPUT',
  parameters: Record<string, any> = {}
): Promise<{ status: string; message: string; simulationId?: string; timestamp?: number }> {
  try {
    const response = await apiClient.post('/simulation/stress-test', {
      command,
      parameters
    });
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error sending stress test command:', error.message);
      const errorMsg = error.response?.data?.message || error.message;
      throw new Error(`Failed to send stress test command: ${errorMsg}`);
    }
    throw error;
  }
}

/**
 * Fetch sessions from the server
 * @returns Promise containing array of sessions (raw API response)
 */
export async function fetchSessions(): Promise<any[] | undefined> {
  try {
    const response = await apiClient.get('/playground/sessions/list');
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      throw error;
    }
  }
}

/**
 * Fetch connected client IDs for a specific AP from the simulation database.
 * @param apNodeId - The ID of the AP node.
 * @returns Promise containing an array of client node IDs.
 */
export async function fetchConnectedClientIds(apNodeId: number): Promise<number[]> {
  try {
    const response = await apiClient.get(`/simulation/sta/list/${apNodeId}`);
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error fetching connected clients for AP ${apNodeId}:`, error.message);
      throw new Error(`Failed to fetch connected clients: ${error.message}`);
    }
    throw error;
  }
}

/**
 * Fetch the current running simulation info
 * @returns Promise containing the current simulation config with session_name, or null if no simulation is running
 */
export async function fetchCurrentSimulation(): Promise<any | null> {
  try {
    const response = await apiClient.get('/playground/current');
    // Check if simulation is running using the running field
    if (response.data?.running === true) {
      return response.data;
    }
    // If running is false or not present, return null
    return null;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      // Network errors (no response) are expected when API is unavailable - handle silently
      if (!error.response) {
        // Network error (connection refused, timeout, etc.) - no simulation available
        return null;
      }
      // 403 or other HTTP errors mean no simulation is running - handle silently
      if (error.response?.status === 403 || error.response?.status === 404) {
        return null;
      }
      // Only log unexpected server errors (5xx) in development
      if (error.response?.status >= 500 && process.env.NODE_ENV === 'development') {
        console.error('Error fetching current simulation:', error.message);
      }
      return null;
    }
    return null;
  }
}

/**

 * Fetch config for a specific session by name

 * @param sessionName - The name of the session (e.g., "config-simulation")

 * @returns Promise containing the config with aps, stas, and waypoints

 */

export async function fetchSessionConfig(sessionName: string): Promise<any | null> {

  try {

    const response = await apiClient.get(`/playground/config/${sessionName}`);

    return response.data;

  } catch (error) {

    if (axios.isAxiosError(error)) {
      // Don't log 404s as errors - this is expected for new sessions
      if (error.response?.status === 404) {
        return null; // Return null instead of throwing for new sessions
      }

      console.error(`Error fetching config for ${sessionName}:`, error.message);

      throw new Error(`Failed to fetch config: ${error.message}`);

    }

    throw error;

  }

}



/**

 * Rename a session

 * @param oldName - The current name of the session

 * @param newName - The new name for the session

 * @returns Promise containing the response status

 */

export async function renameSession(oldName: string, newName: string): Promise<{ status: string; message: string }> {

  try {

    const response = await apiClient.patch(`/playground/config/${oldName}`, { new_name: newName });

    return response.data;

  } catch (error) {

    if (axios.isAxiosError(error)) {

      console.error(`Error renaming session ${oldName}:`, error.message);

      throw new Error(`Failed to rename session: ${error.message}`);

    }

    throw error;

  }

}



/**

 * Delete a session

 * @param sessionName - The name of the session to delete

 * @returns Promise containing the response status

 */

export async function deleteSession(sessionName: string): Promise<{ status: string; message: string }> {

  try {

    const response = await apiClient.delete(`/playground/config/${sessionName}`);

    return response.data;

  } catch (error) {

    if (axios.isAxiosError(error)) {

      console.error(`Error deleting session ${sessionName}:`, error.message);

      throw new Error(`Failed to delete session: ${error.message}`);

    }

    throw error;

  }

}





