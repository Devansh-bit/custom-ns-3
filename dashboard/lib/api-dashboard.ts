import { APMetrics } from '@/lib/types';
import axios from 'axios';

// Base API URL - adjust this based on your API server configuration
export const API_BASE_URL = `http://${process.env.NEXT_PUBLIC_API_URL || 'localhost:8000'}`;

// Create axios instance with default configuration
const apiClient = axios.create({
  baseURL: API_BASE_URL,
  headers: {
    'Content-Type': 'application/json',
  },
});

export type DataMode = 'replay' | 'simulation';

/**
 * Switch data source mode (replay or simulation)
 * @param mode - The mode to switch to ('replay' or 'simulation')
 * @returns Promise containing the response message
 */
export async function switchDataMode(mode: DataMode): Promise<{ message: string }> {
  try {
    const response = await apiClient.post<{ message: string }>('/data/switch', { to: mode });
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error switching data mode to ${mode}:`, error.message);
      throw new Error(`Failed to switch data mode: ${error.message}`);
    }
    throw error;
  }
}

// ============== REPLAY CONTROL API ==============

export interface ReplayState {
  current_time: number;
  is_playing: boolean;
  speed: number;
  min_time: number;
  max_time: number;
}

/**
 * Get current replay state
 */
export async function fetchReplayState(): Promise<ReplayState> {
  const response = await apiClient.get<ReplayState>('/replay/state');
  return response.data;
}

/**
 * Start/resume replay playback
 */
export async function replayPlay(): Promise<{ message: string; current_time: number }> {
  const response = await apiClient.post('/replay/play');
  return response.data;
}

/**
 * Pause replay playback
 */
export async function replayPause(): Promise<{ message: string; current_time: number }> {
  const response = await apiClient.post('/replay/pause');
  return response.data;
}

/**
 * Seek to a specific time in replay
 * @param time - The simulation time to seek to (in seconds)
 */
export async function replaySeek(time: number): Promise<{ message: string; current_time: number }> {
  const response = await apiClient.post('/replay/seek', { time });
  return response.data;
}

/**
 * Set replay playback speed
 * @param speed - Speed multiplier (1, 2, 5, 10, or 20)
 */
export async function replaySetSpeed(speed: number): Promise<{ message: string; speed: number }> {
  const response = await apiClient.post('/replay/speed', { speed });
  return response.data;
}

/**
 * Reset replay to the beginning
 */
export async function replayReset(): Promise<{ message: string; current_time: number }> {
  const response = await apiClient.post('/replay/reset');
  return response.data;
}

/**
 * Fetch simulation uptime in seconds
 * @returns Promise containing the simulation uptime (sim_time_seconds) or null if not available
 */
export async function fetchSimulationUptime(): Promise<number | null> {
  try {
    const response = await apiClient.get<{ time: { snapshot_id: number; snapshot_id_unix: number; sim_time_seconds: number } } | { message: string }>('/simulation/uptime');
    // Check if response has a message (error case)
    if ('message' in response.data) {
      return null;
    }
    // Extract sim_time_seconds from the time object
    return response.data.time?.sim_time_seconds ?? null;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      // Don't log 404s or 403s as errors - simulation might not be running
      if (error.response?.status === 404 || error.response?.status === 403) {
        return null;
      }
      console.error('Error fetching simulation uptime:', error.message);
    }
    return null;
  }
}


/**
 * Fetch list of Access Point BSSIDs from the API
 * @param limit - Optional limit on the number of AP BSSIDs to return
 * @returns Promise containing array of AP BSSID strings (MAC addresses)
 */
export async function fetchAPList(limit?: number): Promise<string[]> {
  try {
    const params = limit ? { limit } : {};
    const response = await apiClient.get<string[]>('/ap/list', { params });
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error fetching AP list:', error.message);
      throw new Error(`Failed to fetch AP list: ${error.message}`);
    }
    throw error;
  }
}

/**
 * Fetch metrics for a specific Access Point
 * @param bssid - The BSSID (MAC address) of the AP
 * @returns Promise containing AP metrics data
 */
export async function fetchAPMetrics(bssid: string): Promise<APMetrics> {
  try {
    const response = await apiClient.get<APMetrics>(`/ap/metrics/${bssid}`);
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error fetching AP metrics for ${bssid}:`, error.message);
      throw new Error(`Failed to fetch AP metrics: ${error.message}`);
    }
    throw error;
  }
}

// Type definitions for STA/client data
export interface ConnectedClient {
  mac_address: string;
  ap_view_rssi: number | null;
  ap_view_snr: number | null;
  latency_ms: number | null;
  jitter_ms: number | null;
  packet_loss_rate: number | null;
}

export interface ChannelUtilizationDataPoint {
  sim_time_seconds: number;
  channel_utilization: number | null;
}

export interface STAMetrics {
  aggregate_metrics: {
    avg_latency_ms: number | null;
    avg_jitter_ms: number | null;
    avg_packet_loss_rate: number | null;
    total_clients: number;
  };
  client_metrics: {
    mac_address: string;
    ap_bssid: string;
    latency_ms: number | null;
    jitter_ms: number | null;
    packet_loss_rate: number | null;
    ap_view_rssi: number | null;
    ap_view_snr: number | null;
  }[];
}

export interface STATimeSeriesDataPoint {
  sim_time_seconds: number;
  value?: number; // For specific client queries
  avg_value?: number; // For aggregate queries
  min_value?: number; // For aggregate queries
  max_value?: number; // For aggregate queries
  client_count?: number; // For aggregate queries
}

export type MetricType = 'latency' | 'jitter' | 'packet_loss_rate' | 'rssi' | 'snr';

/**
 * Fetch STA list for a specific AP
 * Note: This endpoint currently returns AP metrics, not STA list
 * @param apBssid - The BSSID of the AP
 * @param limit - Optional limit on results
 * @returns Promise containing AP metrics data
 */
export async function fetchSTAList(
  apBssid: string,
  limit?: number
): Promise<APMetrics> {
  try {
    const params: Record<string, string | number> = { ap_bssid: apBssid };
    if (limit) params.limit = limit;
    const response = await apiClient.get<APMetrics>('/sta/list', { params });
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error fetching STA list for ${apBssid}:`, error.message);
      throw new Error(`Failed to fetch STA list: ${error.message}`);
    }
    throw error;
  }
}

/**
 * Fetch list of clients connected to a specific AP
 * @param bssid - The BSSID (MAC address) of the AP
 * @returns Promise containing array of connected clients with their metrics
 */
export async function fetchAPClients(bssid: string): Promise<ConnectedClient[]> {
  try {
    const response = await apiClient.get<ConnectedClient[]>(`/ap/${bssid}/clients`);
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error fetching clients for AP ${bssid}:`, error.message);
      throw new Error(`Failed to fetch AP clients: ${error.message}`);
    }
    throw error;
  }
}

/**
 * Fetch channel utilization history for a specific AP
 * @param bssid - The BSSID (MAC address) of the AP
 * @param timeResolution - Duration in minutes to retrieve history (default: 10)
 * @returns Promise containing array of channel utilization data points
 */
export async function fetchAPChannelUtilization(
  bssid: string,
  timeResolution: number = 10
): Promise<ChannelUtilizationDataPoint[]> {
  try {
    const params = { timeResolution: timeResolution };
    const response = await apiClient.get<ChannelUtilizationDataPoint[]>(
      `/ap/${bssid}/channel-utilization`,
      { params }
    );
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error fetching channel utilization for AP ${bssid}:`, error.message);
      throw new Error(`Failed to fetch channel utilization: ${error.message}`);
    }
    throw error;
  }
}

/**
 * Fetch STA metrics across all clients on the network
 * @returns Promise containing aggregate and individual client metrics
 */
export async function fetchSTAMetrics(): Promise<STAMetrics> {
  try {
    const response = await apiClient.get<STAMetrics>('/sta/metrics');
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error fetching STA metrics:', error.message);
      throw new Error(`Failed to fetch STA metrics: ${error.message}`);
    }
    throw error;
  }
}

/**
 * Fetch time series data for STA metrics (latency, jitter, or packet loss)
 * @param metricType - Type of metric ('latency', 'jitter', or 'packet_loss')
 * @param clientMacAddress - Optional MAC address to filter by specific client
 * @param timeResolution - Duration in minutes (default: 10)
 * @returns Promise containing time series data points
 */
export async function fetchSTATimeSeries(
  metricType: MetricType,
  clientMacAddress?: string,
  timeResolution: number = 10
): Promise<STATimeSeriesDataPoint[]> {
  try {
    let url = '/sta/time-series';
    const params: Record<string, string | number> = {
        timeResolution: timeResolution,
    };

    if (clientMacAddress) {
      url = `/sta/time-series/${clientMacAddress}/${metricType}/individual`;
    } else {
      params.metric_type = metricType;
    }

    const response = await apiClient.get<STATimeSeriesDataPoint[]>(
      url,
      { params }
    );
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error fetching STA time series for ${metricType}:`, error.message);
      throw new Error(`Failed to fetch STA time series: ${error.message}`);
    }
    throw error;
  }
}

export type HistogramBin = {
  range: string;
  clients: number;
};

export type HistogramData = {
  label: string;
  data: HistogramBin[];
};

/**
 * Fetch histogram data for QoE metrics with bins from backend
 * @param metricType - Type of metric (latency, jitter, packet_loss, rssi, snr)
 * @returns Promise containing histogram data with bins
 */
export async function fetchQoEHistogram(metricType: MetricType): Promise<HistogramData> {
  try {
    const response = await apiClient.get<HistogramData>('/sta/metrics/histogram', {
      params: { metric_type: metricType }
    });
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error fetching QoE histogram for ${metricType}:`, error.message);
      throw new Error(`Failed to fetch QoE histogram: ${error.message}`);
    }
    throw error;
  }
}

export type ChannelInterferenceRow = {
  channel: number;                        // Channel number
  non_wifi_channel_utilization: number;   // Non-WiFi channel utilization percentage
};

export type ChannelDetails = {
  channel: number;
  quality: number;           // Quality (%)
  wifi: number;              // WiFi (%)
  zigbee: number;            // Zigbee (%)
  bluetooth: number;         // Bluetooth (%)
  microwave: number;         // Microwave (%)
  total_non_wifi: number;    // Total non-WiFi (%)
  utilization: number;      // Utilization (%)
};

export async function fetchChannelInterference(bssid: string): Promise<ChannelInterferenceRow[]> {
  const { data } = await apiClient.get<ChannelInterferenceRow[]>("/home/channel-interference", {
    params: { bssid }
  });
  return Array.isArray(data) ? data : [];
}

/**
 * Fetch detailed channel information including quality, interference types, and utilization
 * @param channel - The channel number
 * @returns Promise containing detailed channel metrics
 */
export async function fetchChannelDetails(channel: number): Promise<ChannelDetails> {
  try {
    const response = await apiClient.get<ChannelDetails>(`/channel/${channel}/details`);
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error fetching channel details for channel ${channel}:`, error.message);
      // Return dummy data for now until API is implemented
      return {
        channel,
        quality: 75,
        wifi: 60,
        zigbee: 20,
        bluetooth: 10,
        microwave: 5,
        total_non_wifi: 35,
        utilization: 45
      };
    }
    // Return dummy data on error
    return {
      channel,
      quality: 75,
      wifi: 60,
      zigbee: 20,
      bluetooth: 10,
      microwave: 5,
      total_non_wifi: 35,
      utilization: 45
    };
  }
}

export type BandCountRow = {
  band: string;
  sta_count: number;
};

/**
 * Fetch client counts grouped by band (2.4 GHz, 5 GHz, etc.)
 * @returns Promise containing array of band count objects
 */
export async function fetchBandCounts(): Promise<BandCountRow[]> {
  try {
    const response = await apiClient.get<BandCountRow[]>("/sta/count-by-band");
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error fetching band counts:', error.message);
      throw new Error(`Failed to fetch band counts: ${error.message}`);
    }
    throw error;
  }
}

export type MetricCountRow = {
  category: string;
  sta_count: number;
};

export type ClientMetricType = 'throughput' | 'rssi';

/**
 * Fetch client counts grouped by a specific metric
 * @param metricType - The type of metric to group by ('throughput' or 'rssi')
 * @returns Promise containing array of metric count objects
 *
 * Categories by metric type:
 * - throughput: low (0-3mbps), medium (3-6mbps), high (6-10mbps), very_high (10+mbps)
 * - rssi: good (>-50dBm), fair (-50 to -65dBm), poor (<-65dBm)
 */
export async function fetchMetricCounts(metricType: ClientMetricType): Promise<MetricCountRow[]> {
  try {
    const response = await apiClient.get<MetricCountRow[]>(`/sta/count-by-metric/${metricType}`);
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error(`Error fetching ${metricType} counts:`, error.message);
      throw new Error(`Failed to fetch ${metricType} counts: ${error.message}`);
    }
    throw error;
  }
}

export interface ClientQualitySummary {
  audio: ApplicationQualitySummary,
  video: ApplicationQualitySummary,
  transaction: ApplicationQualitySummary,
  total: number,
}

export interface ApplicationQualitySummary {
  latency: {
    good: number;
    okayish: number;
    bad: number;
  };
  jitter: {
    good: number;
    okayish: number;
    bad: number;
  };
  packet_loss: {
    good: number;
    okayish: number;
    bad: number;
  };
}

export interface NetworkScore {
  sim_time_seconds: number;
  network_score: number;
}

/**
 * Get counts of clients categorized as good, okayish, or bad based on latency,
 * jitter, and packet loss thresholds at the most recent snapshot.
 * @returns Promise containing client quality summary data
 */
export async function fetchClientQuality(): Promise<ClientQualitySummary> {
  try {
    const response = await apiClient.get<ClientQualitySummary>('/sta/client-quality');
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error fetching client quality summary:', error.message);
      throw new Error(`Failed to fetch client quality summary: ${error.message}`);
    }
    throw error;
  }
}

export async function fetchNetworkScore(timeResolution: number = 10): Promise<NetworkScore[]> {
  try {
    const response = await apiClient.get<NetworkScore[]>('/network/health-time-series', {
      params: { time_resolution: timeResolution },
    });
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error fetching network score:', error.message);
      throw new Error(`Failed to fetch network score: ${error.message}`);
    }
    throw error;
  }
}

export interface UnifiedHealthResponse {
  ap_health: Record<string, number>;
  network_score: number | null;
  last_reset_sim_time: number;
  current_sim_time: number;
  time_until_reset: number;
  ap_count: number;
}

export async function fetchNetworkHealthUnified(timeResolution: number = 30): Promise<UnifiedHealthResponse> {
  try {
    const response = await apiClient.get<UnifiedHealthResponse>('/network/health-unified', {
      params: { time_resolution: timeResolution }
    });
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error fetching unified health:', error.message);
      throw new Error(`Failed to fetch unified health: ${error.message}`);
    }
    throw error;
  }
}

// Type definitions for RRM/Network Planner Updates
export interface RRMChange {
  ap_id: string; // BSSID
  // Updated values
  updated_tx_power: number | null;
  updated_channel: number | null;
  updated_channel_width: number | null;
  updated_obss_pd_threshold: number | null; // null for slow loop updates
  // Old values (for fast loop to show changes)
  old_tx_power?: number | null;
  old_channel?: number | null;
  old_channel_width?: number | null;
  old_obss_pd_threshold?: number | null;
}

export interface NetworkPlannerUpdate {
  planner_id: number;
  time_of_update: number; // timestamp_unix
  status: "accepted" | "rejected" | "proposed";
  delta_network_score: number | null;
  type: "fast" | "slow"; // Fast Loop (ns3) or Slow Loop (RL)
  changes: RRMChange[];
}

/**
 * Fetch list of network planner updates
 * @returns Promise containing array of network planner updates
 */
export async function fetchRRMUpdates(): Promise<NetworkPlannerUpdate[]> {
  try {
    const response = await apiClient.get<NetworkPlannerUpdate[]>('/network/rrm-updates');
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error fetching RRM updates:', error.message);
      throw new Error(`Failed to fetch RRM updates: ${error.message}`);
    }
    throw error;
  }
}

export async function fetchClientDistribution(): Promise<Record<MetricType, HistogramData>> {
  try {
    const response = await apiClient.get('/network/client-distribution');
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error fetching client distribution:', error.message);
      throw new Error(`Failed to fetch client distribution: ${error.message}`);
    }
    throw error;
  }
}

// =============================================================================
// RL (Reinforcement Learning) Control API
// =============================================================================

export interface RLStatus {
  status: 'running' | 'stopped';
  running: boolean;
  simulationId: string | null;
  pid: number | null;
  exitCode?: number;
}

/**
 * Start the RL training process for the current simulation
 * @returns Promise containing the start result with PID
 */
export async function startRL(): Promise<{ status: string; message: string; simulationId: string; pid: number }> {
  try {
    const response = await apiClient.post('/rl/start');
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      const errorMsg = error.response?.data?.message || error.message;
      console.error('Error starting RL process:', errorMsg);
      throw new Error(`Failed to start RL: ${errorMsg}`);
    }
    throw error;
  }
}

/**
 * Stop the running RL training process
 * @returns Promise containing the stop result
 */
export async function stopRL(): Promise<{ status: string; message: string }> {
  try {
    const response = await apiClient.post('/rl/stop');
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      const errorMsg = error.response?.data?.message || error.message;
      console.error('Error stopping RL process:', errorMsg);
      throw new Error(`Failed to stop RL: ${errorMsg}`);
    }
    throw error;
  }
}

/**
 * Get the current status of the RL training process
 * @returns Promise containing the RL process status
 */
export async function getRLStatus(): Promise<RLStatus> {
  try {
    const response = await apiClient.get<RLStatus>('/rl/status');
    return response.data;
  } catch (error) {
    if (axios.isAxiosError(error)) {
      console.error('Error fetching RL status:', error.message);
      // Return stopped status on error
      return {
        status: 'stopped',
        running: false,
        simulationId: null,
        pid: null
      };
    }
    throw error;
  }
}
