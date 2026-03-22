import { NetworkNode, Session, Template } from '@/types';
import constantsData from '@/constants.json';
const defaultTemplates = constantsData.defaultTemplates;

const SESSIONS_KEY = 'playground-sessions';
const SAVED_TEMPLATES_KEY = 'saved-templates';

// Helper: Load sessions from localStorage (synchronous, safe for initialization)
export const loadSessionsSync = (): Session[] => {
  if (typeof window === 'undefined') return [];
  try {
    const stored = localStorage.getItem(SESSIONS_KEY);
    if (stored) {
      return JSON.parse(stored);
    }
  } catch (e) {
    console.error('Failed to load sessions:', e);
  }
  return [];
};

// Helper: Load saved templates from localStorage (synchronous, safe for initialization)
export const loadSavedTemplatesSync = (): Template[] => {
  if (typeof window === 'undefined') return [];
  try {
    const stored = localStorage.getItem(SAVED_TEMPLATES_KEY);
    if (stored) {
      return JSON.parse(stored);
    }
  } catch (e) {
    console.error('Failed to load saved templates:', e);
  }
  return [];
};

// Helper: Deep clone nodes
export const cloneNodes = (nodes: NetworkNode[]): NetworkNode[] => {
  return JSON.parse(JSON.stringify(nodes));
};

// Helper: Create a new session
export const createNewSession = (
  template: Template,
  existingSessions: Session[]
): Session => {
  const sessionId = Math.random().toString(36).substring(2, 11);
  const templateName = template.name;
  
  // Find a unique session name by checking all existing sessions
  let sessionNumber = 1;
  let sessionName = `${templateName} ${sessionNumber}`;
  
  // Keep incrementing until we find a unique name
  while (existingSessions.some(s => s.name === sessionName)) {
    sessionNumber++;
    sessionName = `${templateName} ${sessionNumber}`;
  }

  return {
    id: sessionId,
    name: sessionName,
    templateId: template.id,
    createdAt: Date.now(),
    lastModified: Date.now(),
    nodes: cloneNodes(template.nodes)
  };
};

// Helper: Create empty session
export const createEmptySession = (existingSessions: Session[]): Session => {
  const sessionId = Math.random().toString(36).substring(2, 11);
  
  // Find a unique session name by checking all existing sessions
  let sessionNumber = 1;
  let sessionName = `New session ${sessionNumber}`;
  
  // Keep incrementing until we find a unique name
  while (existingSessions.some(s => s.name === sessionName)) {
    sessionNumber++;
    sessionName = `New session ${sessionNumber}`;
  }

  return {
    id: sessionId,
    name: sessionName,
    templateId: 'empty',
    createdAt: Date.now(),
    lastModified: Date.now(),
    nodes: []
  };
};

// Helper: Find template by ID
export const findTemplateById = (
  templateId: string,
  savedTemplates: Template[]
): Template | undefined => {
  return [...defaultTemplates, ...savedTemplates].find(t => t.id === templateId);
};

// Helper: Reset all simulation-related state
export const getDefaultKPIMetrics = () => ({
  throughput: 0,
  retryRate: 0,
  packetretryRate: 0,
  latency: 0,
  churn: 0,
  score: 0
});

