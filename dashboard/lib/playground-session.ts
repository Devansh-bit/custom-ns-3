import { NetworkNode, Session, Template } from '@/types';
import { cloneNodes, findTemplateById } from '@/app/playground/utils';
import { initializeHistory as initializeHistoryUtil } from './playground-history';
import type { MutableRefObject } from 'react';

export interface SessionState {
  history: Record<string, NetworkNode[][]>;
  historyIndex: Record<string, number>;
  historyIndexRef: MutableRefObject<Record<string, number>>;
}

/**
 * Reset all simulation-related state to defaults
 */
export const getResetSimulationState = () => ({
  simulationState: 'idle' as const,
  placeMode: null as null,
  logs: [] as any[],
  timeOfDay: 25,
  isBuilt: false,
  nodesAtBuildTime: [] as NetworkNode[]
});

/**
 * Load nodes from session (with fallback to template defaults)
 */
export const loadSessionNodes = (
  session: Session,
  savedTemplates: Template[]
): { nodes: NetworkNode[]; templateNodeIds: Set<string> } => {
  if (session.templateId === 'empty') {
    return {
      nodes: session.nodes && session.nodes.length > 0 ? cloneNodes(session.nodes) : [],
      templateNodeIds: new Set()
    };
  }

  const template = findTemplateById(session.templateId, savedTemplates);
  if (template) {
    const nodesToLoad = session.nodes && session.nodes.length > 0
      ? cloneNodes(session.nodes)
      : cloneNodes(template.nodes);
    return {
      nodes: nodesToLoad,
      templateNodeIds: new Set(template.nodes.map(n => n.id))
    };
  }

  // Template not found - load session nodes directly
  return {
    nodes: session.nodes && session.nodes.length > 0 ? cloneNodes(session.nodes) : [],
    templateNodeIds: new Set()
  };
};

/**
 * Initialize history for a session
 */
export const initializeSessionHistory = (
  templateId: string,
  nodes: NetworkNode[],
  sessionState: SessionState
): { history: Record<string, NetworkNode[][]>; historyIndex: Record<string, number> } => {
  const id = templateId || 'empty';
  const initialNodes = nodes.length > 0 ? nodes : [];
  
  return initializeHistoryUtil(id, initialNodes, sessionState);
};

/**
 * Clear all application state (used when deleting sessions)
 */
export const getClearedState = () => ({
  activeSessionId: null as null,
  nodes: [] as NetworkNode[],
  activeTemplateId: null as null,
  templateNodeIds: new Set<string>(),
  lastSavedNodes: [] as NetworkNode[],
  ...getResetSimulationState()
});

/**
 * Find session to restore from history
 */
export const findSessionToRestore = (
  sessionHistory: string[],
  sessions: Session[]
): Session | null => {
  if (sessionHistory.length === 0) return null;
  const lastSessionId = sessionHistory[sessionHistory.length - 1];
  return sessions.find(s => s.id === lastSessionId) || null;
};

