import { NetworkNode } from '@/types';
import type { MutableRefObject } from 'react';

export interface HistoryState {
  history: Record<string, NetworkNode[][]>;
  historyIndex: Record<string, number>;
  historyIndexRef: MutableRefObject<Record<string, number>>;
}

/**
 * Save nodes state to history for undo/redo functionality
 */
export const saveToHistory = (
  newNodes: NetworkNode[],
  templateId: string,
  historyState: HistoryState
): { history: Record<string, NetworkNode[][]>; historyIndex: Record<string, number> } => {
  const { history, historyIndex, historyIndexRef } = historyState;
  const id = templateId || 'empty';

  let templateHistory = history[id] || [];

  // If history doesn't exist, initialize it with empty state
  if (templateHistory.length === 0) {
    templateHistory = [[]]; // Start with empty state at index 0
    historyIndexRef.current = {
      ...historyIndexRef.current,
      [id]: 0
    };
  }

  // Get current index - use ref first, then fallback to state, then -1
  let currentIndex = historyIndexRef.current[id] ?? historyIndex[id] ?? -1;
  
  // If currentIndex is -1 but we have history, set it to the last index
  if (currentIndex === -1 && templateHistory.length > 0) {
    currentIndex = templateHistory.length - 1;
    historyIndexRef.current = {
      ...historyIndexRef.current,
      [id]: currentIndex
    };
  }

  // Remove any future history if we're not at the end
  const trimmedHistory = templateHistory.slice(0, currentIndex + 1);

  // Check if nodes have actually changed compared to the current history entry
  // This prevents saving duplicate states
  const currentHistoryEntry = trimmedHistory[currentIndex];
  if (currentHistoryEntry && currentIndex >= 0) {
    // Compare new nodes with current history entry
    const nodesChanged = JSON.stringify(currentHistoryEntry) !== JSON.stringify(newNodes);
    if (!nodesChanged) {
      // Nodes haven't changed, don't save to history - just return current state
      return {
        history: {
          ...history,
          [id]: trimmedHistory
        },
        historyIndex: {
          ...historyIndex,
          [id]: currentIndex
        }
      };
    }
  }

  // Add new state (deep clone to avoid reference issues)
  const clonedNodes = JSON.parse(JSON.stringify(newNodes));
  const updatedHistory = [...trimmedHistory, clonedNodes];

  // Limit history to 50 states
  const limitedHistory = updatedHistory.slice(-50);
  const newIndex = limitedHistory.length - 1;

  // Update historyIndex and ref
  const updatedHistoryIndex = {
    ...historyIndex,
    [id]: newIndex
  };
  historyIndexRef.current = updatedHistoryIndex;

  return {
    history: {
      ...history,
      [id]: limitedHistory
    },
    historyIndex: updatedHistoryIndex
  };
};

/**
 * Get previous state from history (undo)
 */
export const getUndoState = (
  templateId: string,
  historyState: HistoryState,
  currentNodes: NetworkNode[]
): { nodes: NetworkNode[]; index: number } | null => {
  const { history, historyIndex, historyIndexRef } = historyState;
  const id = templateId || 'empty';
  const templateHistory = history[id] || [];
  // Use ref to get the most current index
  const currentIndex = historyIndexRef.current[id] ?? historyIndex[id] ?? -1;

  // Only allow undo if we can go back at least one step
  if (currentIndex > 0 && templateHistory.length > 0) {
    const prevIndex = currentIndex - 1;
    const prevNodes = templateHistory[prevIndex];
    // Ensure we have valid nodes array
    if (prevNodes && Array.isArray(prevNodes)) {
      return { nodes: prevNodes, index: prevIndex };
    }
  }
  return null;
};

/**
 * Get next state from history (redo)
 */
export const getRedoState = (
  templateId: string,
  historyState: HistoryState
): { nodes: NetworkNode[]; index: number } | null => {
  const { history, historyIndex, historyIndexRef } = historyState;
  const id = templateId || 'empty';
  const templateHistory = history[id] || [];
  // Use ref to get the most current index
  const currentIndex = historyIndexRef.current[id] ?? historyIndex[id] ?? -1;

  if (currentIndex < templateHistory.length - 1) {
    const nextIndex = currentIndex + 1;
    const nextNodes = templateHistory[nextIndex];
    return { nodes: nextNodes, index: nextIndex };
  }
  return null;
};

/**
 * Clear history for a template
 */
export const clearHistory = (
  templateId: string,
  historyState: HistoryState
): { history: Record<string, NetworkNode[][]>; historyIndex: Record<string, number> } => {
  const { history, historyIndex, historyIndexRef } = historyState;
  const id = templateId || 'empty';

  const updatedHistory = { ...history };
  delete updatedHistory[id];

  const updatedHistoryIndex = { ...historyIndex };
  delete updatedHistoryIndex[id];

  historyIndexRef.current = { ...historyIndexRef.current };
  delete historyIndexRef.current[id];

  return {
    history: updatedHistory,
    historyIndex: updatedHistoryIndex
  };
};

/**
 * Initialize history for a template
 */
export const initializeHistory = (
  templateId: string,
  initialNodes: NetworkNode[],
  historyState: HistoryState
): { history: Record<string, NetworkNode[][]>; historyIndex: Record<string, number> } => {
  const { history, historyIndex, historyIndexRef } = historyState;
  const id = templateId || 'empty';

  const updatedHistory = {
    ...history,
    [id]: [JSON.parse(JSON.stringify(initialNodes))]
  };

  const updatedHistoryIndex = {
    ...historyIndex,
    [id]: 0
  };

  historyIndexRef.current = updatedHistoryIndex;

  return {
    history: updatedHistory,
    historyIndex: updatedHistoryIndex
  };
};

