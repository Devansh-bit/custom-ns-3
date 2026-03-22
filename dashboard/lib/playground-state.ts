import { NetworkNode, OptimizerType, KPIMetrics } from '@/types';

export interface PlaygroundState {
  nodes: NetworkNode[];
  activeTemplateId: string | null;
  templateNodeIds: string[];
  templateStates: Record<string, NetworkNode[]>;
  optimizer: OptimizerType;
  simulationState: 'idle' | 'building' | 'running';
  timeOfDay: number;
  kpiMetrics: KPIMetrics;
}

const STORAGE_KEY = 'playground-state';

/**
 * Load persisted state from localStorage
 */
export const loadPersistedState = (): Partial<PlaygroundState> => {
  if (typeof window === 'undefined') return {};
  try {
    const stored = localStorage.getItem(STORAGE_KEY);
    if (stored) {
      return JSON.parse(stored);
    }
  } catch (e) {
    console.error('Failed to load persisted state:', e);
  }
  return {};
};

/**
 * Save state to localStorage
 */
export const savePersistedState = (state: Partial<PlaygroundState>, activeSessionId: string | null): void => {
  if (typeof window === 'undefined') return;
  try {
    const stateToSave: PlaygroundState = {
      nodes: activeSessionId ? [] : (state.nodes || []),
      activeTemplateId: state.activeTemplateId || null,
      templateNodeIds: Array.from((state.templateNodeIds as Set<string>) || []),
      templateStates: state.templateStates || {},
      optimizer: state.optimizer || 'Bayesian Optimization',
      simulationState: state.simulationState || 'idle',
      timeOfDay: state.timeOfDay || 25,
      kpiMetrics: state.kpiMetrics || {
        throughput: 0,
        retryRate: 0,
        packetretryRate: 0,
        latency: 0,
        churn: 0,
        score: 0
      }
    };
    localStorage.setItem(STORAGE_KEY, JSON.stringify(stateToSave));
  } catch (e) {
    console.error('Failed to save state:', e);
  }
};

/**
 * Create a log entry
 */
export const createLogEntry = (
  message: string,
  type: 'info' | 'success' | 'warning' | 'error' = 'info',
  source: 'System' | 'Planner' | 'AI' | 'Device' | 'Roaming' | 'RRM' | 'Metrics' | 'Sensing' = 'System'
) => {
  return {
    id: Math.random().toString(36).substring(2, 11),
    timestamp: Math.floor(Date.now() / 1000),
    message,
    type,
    source
  };
};

