"use client";
import { useState, useEffect, useCallback, useRef, useMemo } from "react";
import { useRouter } from "next/navigation";
import { SidebarLeft } from "@/components/playground/SidebarLeft";
import { SidebarRight } from "@/components/playground/SidebarRight";
import { Playground } from "@/components/playground/Playground";
import { ControlPanel } from "@/components/playground/ControlPanel";
import { NetworkNode, NodeType, Template, OptimizerType, Session, } from "@/lib/types";
import constantsData from "@/constants.json";
const defaultTemplates = constantsData.defaultTemplates;
import {
  startSimulation,
  stopSimulation,
  fetchSessions,
  fetchSessionConfig,
  fetchCurrentSimulation,
  renameSession,
  deleteSession,
  LogEntry,
  fetchConnectedClientIds,
  NetworkMetrics,
  sendStressTestCommand,
} from "@/lib/api-playground";
import { startRL, stopRL, getRLStatus, switchDataMode } from "@/lib/api-dashboard";
import {
  cloneNodes,
  createNewSession,
  createEmptySession,
  findTemplateById,
  loadSessionsSync,
  loadSavedTemplatesSync,
} from "./utils";
import {
  saveToHistory as saveToHistoryUtil,
  getUndoState,
  getRedoState,
  clearHistory as clearHistoryUtil,
} from "@/lib/playground-history";
import {
  canResetToTemplate,
  generateTemplateName,
} from "@/lib/playground-template";
import { sendBuildConfig, configToNodes } from "@/lib/playground-build";
import {
  loadPersistedState,
  savePersistedState,
  createLogEntry,
} from "@/lib/playground-state";
import {
  getResetSimulationState,
  loadSessionNodes,
  initializeSessionHistory,
  getClearedState,
  findSessionToRestore,
} from "@/lib/playground-session";
import {
  saveTemplatesToStorage,
} from "@/lib/playground-storage";

import { API_WS_URL } from "@/lib/api-playground";

import { getRelativeIndex, nodeIdFromRelativeIndex } from "@/lib/utils";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Button } from "@/components/ui/button";
import { ThemeSwitch } from "@/components/ThemeSwitch";

// Helper: Load persisted state from localStorage (safe to call during initialization)
export default function App() {
  const router = useRouter();
  // Load persisted state once at component initialization
  const initialPersistedState = loadPersistedState();

  // Prefetch dashboard route and components when playground loads
  useEffect(() => {
    if (typeof window !== 'undefined') {
      // Prefetch dashboard route
      router.prefetch('/');

      // Preload dashboard components and CanvasPreview
      Promise.all([
        import('@/components/CanvasPreview'),
        import('@/components/APHealth'),
        import('@/components/BO'),
        import('@/components/ClientPieChart'),
        import('@/components/QoEMetrics'),
        import('@/app/shell'),
      ]).catch(() => {
        // Silently fail if prefetch fails
      });
    }
  }, [router]);

  // Application State
  // Start with empty nodes - sessions will load their own nodes
  const [nodes, setNodes] = useState<NetworkNode[]>([]);
  const [activeTemplateId, setActiveTemplateId] = useState<string | null>(null);
  // Load sessions from API - start with empty array, will be populated on mount
  const [sessions, setSessions] = useState<Session[]>([]);
  const [savedTemplates, setSavedTemplates] = useState<Template[]>(() =>
    loadSavedTemplatesSync(),
  );
  // Initialize activeSessionId from localStorage if available
  const [activeSessionId, setActiveSessionId] = useState<string | null>(() => {
    if (typeof window === 'undefined') return null;
    return localStorage.getItem('active-session-id');
  });
  const [showTemplateSelection, setShowTemplateSelection] = useState(false);
  const [templateNodeIds, setTemplateNodeIds] = useState<Set<number>>(
    new Set(initialPersistedState.templateNodeIds || []),
  ); // Track template node IDs
  const [lastSavedNodes, setLastSavedNodes] = useState<NetworkNode[]>([]); // Track last saved state
  const [isCanvasFocused, setIsCanvasFocused] = useState(false);
  const [hoveredApId, setHoveredApId] = useState<number | null>(null);
  const [hoveredClientId, setHoveredClientId] = useState<number | null>(null);
  const [connectedClientIds, setConnectedClientIds] = useState<Set<number>>(new Set());
  const [clientToApMap, setClientToApMap] = useState<Map<number, number>>(new Map());
  const [isControllerEnabled, setIsControllerEnabled] = useState(false);

  // Simulation State
  const [simulationState, setSimulationState] = useState<
    "unsaved" | "building" | "built" | "starting" | "running" | "stopping"
  >("unsaved");

  // Poll RL status when simulation is running
  useEffect(() => {
    if (simulationState !== "running") {
      setIsControllerEnabled(false);
      return;
    }

    // Poll RL status every 2 seconds while simulation is running
    const pollRLStatus = async () => {
      try {
        const status = await getRLStatus();
        setIsControllerEnabled(status.running);
      } catch (e) {
        console.error('Failed to get RL status:', e);
      }
    };

    pollRLStatus(); // Initial check
    const interval = setInterval(pollRLStatus, 2000);
    return () => clearInterval(interval);
  }, [simulationState]);

  useEffect(() => {
    if (hoveredApId !== null && simulationState === "running") {
      fetchConnectedClientIds(hoveredApId)
        .then((metrics) => {
          if (!metrics || !Array.isArray(metrics.clients)) {
            console.warn("Invalid metrics received for connected clients", metrics);
            return;
          }
          let connectedClients = new Set<number>(metrics.clients);
          setConnectedClientIds(connectedClients);

          setClientToApMap(prev => {
            const next = new Map(prev);
            metrics.clients.forEach((clientId: number) => {
              next.set(clientId, hoveredApId);
            });
            return next;
          });
        })
        .catch(err => console.error(err));
    } else {
      setConnectedClientIds(new Set());
    }
  }, [hoveredApId, simulationState]);

  // Enable TopBar only when a session is created (has nodes or active session)
  useEffect(() => {
    if (nodes.length > 0 || activeSessionId) {
      setIsCanvasFocused(true);
    } else {
      setIsCanvasFocused(false);
    }
  }, [nodes.length, activeSessionId]);

  // Check for unsaved changes
  const hasUnsavedChanges = useMemo(() => {
    if (nodes.length === 0 && lastSavedNodes.length === 0) return false;
    if (nodes.length !== lastSavedNodes.length) return true;

    // Deep compare nodes
    const nodesStr = JSON.stringify(
      [...nodes].sort((a, b) => a.id - b.id),
    );
    const savedStr = JSON.stringify(
      [...lastSavedNodes].sort((a, b) => a.id - b.id),
    );
    return nodesStr !== savedStr;
  }, [nodes, lastSavedNodes]);

  // Compute active session name - ensures header always shows when session is active
  const activeSessionName = useMemo(() => {
    if (!activeSessionId || sessions.length === 0) return null;
    const session = sessions.find((s) => s.id === activeSessionId);
    return session?.name || null;
  }, [activeSessionId, sessions]);
  const [templateStates, setTemplateStates] = useState<
    Record<string, NetworkNode[]>
  >(initialPersistedState.templateStates || {}); // Save state per template
  const [placeMode, setPlaceMode] = useState<NodeType | null>(null);
  const [optimizer, setOptimizer] = useState<OptimizerType>(
    initialPersistedState.optimizer || "Bayesian Optimization",
  );

  // History for undo/redo (per template)
  const [history, setHistory] = useState<Record<string, NetworkNode[][]>>({});
  const [historyIndex, setHistoryIndex] = useState<Record<string, number>>({});
  const historyIndexRef = useRef<Record<string, number>>({});
  const isLoadingSessionRef = useRef(false);
  const sessionsRef = useRef<Session[]>([]);
  const nodesRef = useRef<NetworkNode[]>([]);
  // Session history stack - tracks navigation history for going back
  const [sessionHistory, setSessionHistory] = useState<string[]>([]);
  const sessionHistoryRef = useRef<string[]>([]);
  const nextNodeId = useRef(1);
  // Keep refs in sync with state
  useEffect(() => {
    historyIndexRef.current = historyIndex;
  }, [historyIndex]);

  // Ref for hovered AP to access inside WebSocket callback
  const hoveredApIdRef = useRef<number | null>(null);
  useEffect(() => {
    hoveredApIdRef.current = hoveredApId;
  }, [hoveredApId]);

  const wsRef = useRef<WebSocket | null>(null);

  const [nodesAtBuildTime, setNodesAtBuildTime] = useState<NetworkNode[]>([]); // Track nodes when build was completed
  const [simulationNodes, setSimulationNodes] = useState<NetworkNode[]>([]); // Track visual node changes during simulation only
  const [isNavigatingAway, setIsNavigatingAway] = useState(false); // Track if user is navigating away
  const [timeOfDay, setTimeOfDay] = useState(
    initialPersistedState.timeOfDay || 0,
  ); // Starts at 00:00 (midnight)
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [showScale, setShowScale] = useState(true); // Show scale rulers by default
  const [showStressToolkit, setShowStressToolkit] = useState(false); // Stress Action Toolkit visibility

  const [networkMetrics, setNetworkMetrics] = useState<NetworkMetrics>({
    throughput: null,
    latency: null,
    jitter: null,
    loss_rate: null,
  });

  // Reset network metrics when simulation stops
  useEffect(() => {
    if (simulationState !== "running") {
      setNetworkMetrics({
        throughput: null,
        latency: null,
        jitter: null,
        loss_rate: null,
      });
    }
  }, [simulationState]);

  useEffect(() => {
    if (simulationState !== "running") {
      return;
    }

    const ws = new WebSocket(`${API_WS_URL}/playground/logs`);
    wsRef.current = ws;

    ws.onopen = () => {
      addLog("Connected to real-time log stream.", "success", "System");
    };

    ws.onmessage = (event) => {
      try {
        const messages = JSON.parse(event.data);
        if (Array.isArray(messages)) {
          // Separate logs from position updates
          const logs = messages.filter(msg => msg.mtype === "log" || !msg.mtype);
          const positionUpdates = messages.filter(msg => msg.mtype === "pos_change");
          const apStatsUpdates = messages.filter(msg => msg.mtype === "ap_stats");
          const simStatusUpdates = messages.filter(msg => msg.mtype === "sim_status");

          // Process logs
          if (logs.length > 0) {
            mergeNewLogs(logs);
          }

          // Process simulation status (metrics & uptime)
          if (simStatusUpdates.length > 0) {
            const latestStatus = simStatusUpdates[simStatusUpdates.length - 1];
            if (latestStatus.metrics) {
              setNetworkMetrics(latestStatus.metrics);
            }
            if (latestStatus.uptime !== undefined) {
              const secondsInDay = 24 * 60 * 60;
              const timeInDay = latestStatus.uptime % secondsInDay;
              const timeOfDayValue = (timeInDay / secondsInDay) * 100;
              setTimeOfDay(timeOfDayValue);
            }
          }

          // Process position updates
          if (positionUpdates.length > 0) {
            // Map of client ID to AP ID for the current snapshot
            const currentSnapshotConnections = new Map<number, number>();

            positionUpdates.forEach(update => {
              if (update.ap_id !== undefined) {
                currentSnapshotConnections.set(update.node_id, update.ap_id);
              }
            });

            // Update clientToApMap with new connections
            setClientToApMap(prev => {
              const next = new Map(prev);
              currentSnapshotConnections.forEach((apId, clientId) => {
                next.set(clientId, apId);
              });
              return next;
            });

            setSimulationNodes((prev) => {
              const updated = [...prev];
              // Create a set of updated node IDs for quick lookup
              const updatedNodeIds = new Set(positionUpdates.map(u => u.node_id));

              return updated.map(node => {
                // Find update for this node if it exists
                const update = positionUpdates.find(u => u.node_id === node.id);

                if (update) {
                  // Node is active in this snapshot
                  return {
                    ...node,
                    x: update.position_x * 10,
                    y: update.position_y * 10,
                    status: 'good' // Connected/Active
                  };
                } else if (node.type === 'client') {
                  // Client node missing from snapshot - mark as poor/inactive
                  return {
                    ...node,
                    status: 'poor'
                  };
                }
                // Keep other nodes (APs, interferers) as is if not updated
                return node;
              });
            });

            // Update connectedClientIds based on hovered AP and current snapshot data
            if (hoveredApIdRef.current !== null) {
              const connectedToHovered = new Set<number>();
              currentSnapshotConnections.forEach((apId, clientId) => {
                if (apId === hoveredApIdRef.current) {
                  connectedToHovered.add(clientId);
                }
              });
              setConnectedClientIds(connectedToHovered);
            }
          }
        }
      } catch (error) {
        console.error("Failed to parse incoming log data:", error);
      }
    };

    ws.onerror = (error) => {
      console.error("WebSocket error:", error);
      addLog("Log stream connection error.", "error", "System");
    };

    ws.onclose = () => {
      addLog("Disconnected from real-time log stream.", "info", "System");
    };

    // Cleanup: close the WebSocket connection when the simulation stops or component unmounts.
    return () => {
      if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
        ws.close();
      }
      wsRef.current = null;
    };
  }, [simulationState]);

  // Initialize refs and load active session on mount
  useEffect(() => {
    if (typeof window === "undefined") return;

    const getPreliminaryData = async () => {
      const sessionsData = await fetchSessions();
      const sessions = sessionsData.map((session) => {
        // Use modified_at first, then modified, then created_at, fallback to current time
        const modifiedTime = session.modified_at
          ? (typeof session.modified_at === 'number' ? session.modified_at * 1000 : new Date(session.modified_at).getTime())
          : (session.modified
            ? (typeof session.modified === 'number' ? session.modified * 1000 : new Date(session.modified).getTime())
            : (session.created_at ? new Date(session.created_at).getTime() : Date.now()));

        return {
          id: session.session_name || session.id || `session-${Date.now()}`,
          name: session.session_name || session.name || 'Unnamed Session',
          templateId: session.templateId || 'empty',
          createdAt: session.created_at ? new Date(session.created_at).getTime() : (session.createdAt || modifiedTime),
          lastModified: modifiedTime,
          nodes: session.nodes || [],
          nodeCount: session.node_count || 0,
        }
      }).sort((a, b) => b.lastModified - a.lastModified); // Sort by most recently modified
      setSessions(sessions);
      sessionsRef.current = sessions;

      const currentSession = await fetchCurrentSimulation();
      if (currentSession) {
        setSimulationState("running");
        setActiveSessionId(currentSession.session_name);
      } else {
        setActiveSessionId(null);
        setShowTemplateSelection(true);
      }
    }
    getPreliminaryData();
  }, []); // Run only once on mount when coming from dashboard

  // Track the last roam_id, rrm_change_id, and sensing_result_id for polling
  const lastRoamIdRef = useRef<number>(0);
  const lastRrmIdRef = useRef<number>(0);
  const lastSensingIdRef = useRef<number>(0);

  // Helper: Extract max ID from logs by prefix
  const extractMaxLogId = useCallback(
    (logs: LogEntry[], prefix: string, ref: React.MutableRefObject<number>) => {
      const filteredLogs = logs.filter((log) => log.id.startsWith(prefix));
      if (filteredLogs.length === 0) return;

      const maxId = Math.max(
        ...filteredLogs.map((log) => {
          const match = log.id.match(new RegExp(`^${prefix}-(\\d+)$`));
          return match ? parseInt(match[1], 10) : 0;
        }),
      );
      if (maxId > ref.current) {
        ref.current = maxId;
      }
    },
    [],
  );

  // Update lastRoamIdRef, lastRrmIdRef, and lastSensingIdRef when logs change
  useEffect(() => {
    extractMaxLogId(logs, "roam", lastRoamIdRef);
    extractMaxLogId(logs, "rrm", lastRrmIdRef);
    extractMaxLogId(logs, "sensing", lastSensingIdRef);
  }, [logs, extractMaxLogId]);

  // Helper: Merge new logs with existing logs, avoiding duplicates
  const mergeNewLogs = useCallback((recentLogs: LogEntry[]) => {
    if (recentLogs.length === 0) return;
    setLogs((prev) => {
      const existingIds = new Set(prev.map((log) => log.id));
      const newLogs = recentLogs.filter((log) => !existingIds.has(log.id));
      return [...newLogs, ...prev];
    });
  }, []);


  // Helper to add logs
  const addLog = useCallback(
    (
      message: string,
      type: LogEntry["type"] = "info",
      source: LogEntry["source"] = "System",
    ) => {
      setLogs((prev) => [...prev, createLogEntry(message, type, source)]);
    },
    [],
  );

  // Helper to save state to history
  const saveToHistory = useCallback(
    (newNodes: NetworkNode[]) => {
      const result = saveToHistoryUtil(newNodes, activeTemplateId || "empty", {
        history,
        historyIndex,
        historyIndexRef,
      });
      setHistory(result.history);
      setHistoryIndex(result.historyIndex);
    },
    [activeTemplateId, history, historyIndex, historyIndexRef],
  );

  // Helper to update nextNodeId
  const updateNextNodeId = (nodes: NetworkNode[]) => {
    if (nodes.length > 0) {
      const maxId = Math.max(...nodes.map(n => n.id));
      nextNodeId.current = maxId + 1;
    } else {
      nextNodeId.current = 1;
    }
  };

  // Undo
  const handleUndo = useCallback(() => {
    const templateId = activeTemplateId || "empty";
    const undoState = getUndoState(
      templateId,
      {
        history,
        historyIndex,
        historyIndexRef,
      },
      nodes,
    );

    if (undoState) {
      setNodes(undoState.nodes);
      const newIndex = undoState.index;
      setHistoryIndex((prev) => ({
        ...prev,
        [templateId]: newIndex,
      }));
      // Update ref immediately to keep it in sync
      historyIndexRef.current = {
        ...historyIndexRef.current,
        [templateId]: newIndex,
      };
      setTemplateStates((prevStates) => ({
        ...prevStates,
        [templateId]: undoState.nodes,
      }));
      addLog("Undo: Reverted to previous state", "info", "System");
    }
  }, [activeTemplateId, history, historyIndex, historyIndexRef, nodes, addLog]);

  // Redo
  const handleRedo = useCallback(() => {
    const redoState = getRedoState(activeTemplateId || "empty", {
      history,
      historyIndex,
      historyIndexRef,
    });

    if (redoState) {
      setNodes(redoState.nodes);
      setHistoryIndex((prev) => ({
        ...prev,
        [activeTemplateId || "empty"]: redoState.index,
      }));
      setTemplateStates((prevStates) => ({
        ...prevStates,
        [activeTemplateId || "empty"]: redoState.nodes,
      }));
      addLog("Redo: Restored next state", "info", "System");
    }
  }, [activeTemplateId, history, historyIndex, historyIndexRef, addLog]);

  // Check if current state matches template defaults
  const canReset = useCallback(() => {
    return canResetToTemplate(nodes, activeTemplateId, savedTemplates);
  }, [activeTemplateId, nodes, savedTemplates]);

  // Reset to template defaults
  const handleReset = useCallback(() => {
    const templateId = activeTemplateId || "empty";

    // For empty sessions, reset to empty
    if (templateId === "empty") {
      setNodes([]);
      setTemplateStates((prevStates) => ({
        ...prevStates,
        empty: [],
      }));

      // Clear history for empty template
      const clearedHistory = clearHistoryUtil("empty", {
        history,
        historyIndex,
        historyIndexRef,
      });
      setHistory(clearedHistory.history);
      setHistoryIndex(clearedHistory.historyIndex);

      addLog("Reset: Cleared canvas", "info", "System");
      return;
    }

    // Find the template (check both default templates and saved templates)
    const template = [...defaultTemplates, ...savedTemplates].find(
      (t: Template) => t.id === templateId,
    );

    if (template) {
      const defaultNodes = template.nodes;
      setNodes(defaultNodes);

      // Reset template state
      setTemplateStates((prevStates) => ({
        ...prevStates,
        [templateId]: defaultNodes,
      }));

      // Reset template node IDs
      const newTemplateNodeIds = new Set<number>(
        template.nodes.map((n: NetworkNode) => n.id),
      );
      setTemplateNodeIds(newTemplateNodeIds);

      // Clear history for this template
      const clearedHistory = clearHistoryUtil(templateId, {
        history,
        historyIndex,
        historyIndexRef,
      });
      setHistory(clearedHistory.history);
      setHistoryIndex(clearedHistory.historyIndex);

      addLog(
        `Reset: Restored ${template.name} to default configuration`,
        "info",
        "System",
      );
    }
  }, [activeTemplateId, addLog]);

  // Keep refs in sync with state
  useEffect(() => {
    sessionsRef.current = sessions;
  }, [sessions]);

  useEffect(() => {
    sessionHistoryRef.current = sessionHistory;
  }, [sessionHistory]);

  useEffect(() => {
    nodesRef.current = nodes;
  }, [nodes]);

  // Helper: Update session's nodes
  const updateSessionNodes = useCallback(
    (sessionId: string, nodesToSave: NetworkNode[]) => {
      setSessions((prev) =>
        prev.map((s) => {
          if (s.id === sessionId) {
            return {
              ...s,
              nodes: JSON.parse(JSON.stringify(nodesToSave)),
              lastModified: Date.now(),
            };
          }
          return s;
        }),
      );
    },
    [],
  );

  // Update session's nodes when nodes change (for active session)
  useEffect(() => {
    if (isLoadingSessionRef.current || !activeSessionId) return;
    updateSessionNodes(activeSessionId, nodes);
  }, [nodes, activeSessionId, updateSessionNodes]);

  // Show template selection
  const handleShowTemplateSelection = () => {
    if (activeSessionId) {
      updateSessionNodes(activeSessionId, nodes);
      setSessionHistory((prev) => {
        if (prev.length === 0 || prev[prev.length - 1] !== activeSessionId) {
          return [...prev, activeSessionId];
        }
        return prev;
      });
    }
    setActiveSessionId(null);
    setNodes([]);
    setActiveTemplateId(null);
    setShowTemplateSelection(true);
  };

  // Close template selection and restore last active session from history
  const handleCloseTemplateSelection = () => {
    const currentHistory = sessionHistoryRef.current;

    // Try to restore from history first
    if (currentHistory.length > 0) {
      const sessionToRestore = findSessionToRestore(
        currentHistory,
        sessionsRef.current,
      );
      if (sessionToRestore) {
        setShowTemplateSelection(false);
        handleLoadSession(sessionToRestore, true);
        return;
      }
      // Session not found - remove invalid entry
      console.warn(`Session not found when trying to restore from history`);
      setSessionHistory((prev) => prev.slice(0, -1));
    }

    // If no sessions exist, show template selection
    if (sessions.length === 0) {
      setShowTemplateSelection(true);
      return;
    }

    // If no last active session, load the first session from the list
    const firstSession = sessions[0];
    if (firstSession) {
      setShowTemplateSelection(false);
      handleLoadSession(firstSession, true);
      return;
    }

    // Fallback: show template selection
    setShowTemplateSelection(true);
  };

  // Create session from template
  const handleCreateSession = (template: Template) => {
    setShowTemplateSelection(false);

    const defaultNodes = cloneNodes(template.nodes);
    updateNextNodeId(defaultNodes);
    const newSession = createNewSession(template, sessions);

    // Save current session before creating new one
    if (activeSessionId) {
      updateSessionNodes(activeSessionId, nodes);
    }

    // Update session to include nodes before adding to sessions array
    const sessionWithNodes = { ...newSession, nodes: defaultNodes };
    setSessions((prev) => [sessionWithNodes, ...prev]);

    // Set nodes and template state
    setNodes(defaultNodes);
    setTemplateNodeIds(new Set(template.nodes.map((n) => n.id)));
    setActiveTemplateId(template.id);
    setLastSavedNodes(cloneNodes(defaultNodes));
    setActiveSessionId(newSession.id);

    // Update template states
    setTemplateStates((prev) => ({
      ...prev,
      [template.id]: defaultNodes,
    }));

    // Initialize history
    const historyResult = initializeSessionHistory(template.id, defaultNodes, {
      history,
      historyIndex,
      historyIndexRef,
    });
    setHistory(historyResult.history);
    setHistoryIndex(historyResult.historyIndex);

    addLog(`Created session: ${newSession.name}`, "success", "System");
  };

  // Load session
  const handleLoadSession = useCallback(async (
    session: Session,
    skipHistoryUpdate: boolean = false,
  ) => {
    setShowTemplateSelection(false);
    isLoadingSessionRef.current = true;

    // Save current session's state before switching - use nodesRef to avoid dependency on nodes
    if (
      !skipHistoryUpdate &&
      activeSessionId &&
      activeSessionId !== session.id
    ) {
      // Use nodes from state at call time, not from dependency
      const currentNodes = nodesRef.current || nodes;
      updateSessionNodes(activeSessionId, currentNodes);
      setSessionHistory((prev) => {
        if (prev.length === 0 || prev[prev.length - 1] !== activeSessionId) {
          return [...prev, activeSessionId];
        }
        return prev;
      });
    }

    setActiveSessionId(session.id);

    // Try to fetch config from API first (session.name should match session_name from API)
    let nodesToLoad: NetworkNode[] = [];
    try {
      const config = await fetchSessionConfig(session.name);
      if (config) {
        nodesToLoad = configToNodes(config);
        updateNextNodeId(nodesToLoad);

        // Update session with loaded nodes
        setSessions((prev) =>
          prev.map((s) =>
            s.id === session.id
              ? { ...s, nodes: nodesToLoad }
              : s
          )
        );
      } else {
        // Config not found (404) - use fallback
        const loaded = loadSessionNodes(session, savedTemplates);
        nodesToLoad = loaded.nodes;
        updateNextNodeId(nodesToLoad);
      }
    } catch (error) {
      console.error('Failed to fetch session config, using fallback:', error);
      // Fallback to loading from session.nodes or template
      const loaded = loadSessionNodes(session, savedTemplates);
      nodesToLoad = loaded.nodes;
      updateNextNodeId(nodesToLoad);
    }

    const templateNodeIds = new Set(nodesToLoad.map(n => n.id));
    const template = findTemplateById(session.templateId, savedTemplates);

    // Set nodes and template state
    setNodes(nodesToLoad);
    setTemplateNodeIds(templateNodeIds);
    setActiveTemplateId(session.templateId);
    setLastSavedNodes(cloneNodes(nodesToLoad));

    // Update template states
    if (template) {
      setTemplateStates((prev) => ({
        ...prev,
        [template.id]: nodesToLoad,
      }));
    }

    // Initialize history
    const historyResult = initializeSessionHistory(
      session.templateId,
      nodesToLoad,
      {
        history,
        historyIndex,
        historyIndexRef,
      },
    );
    setHistory(historyResult.history);
    setHistoryIndex(historyResult.historyIndex);

    // Reset loading flag
    setTimeout(() => {
      isLoadingSessionRef.current = false;
    }, 150);

    if (!template && session.templateId !== "empty") {
      addLog(
        `Template not found for session "${session.name}". Loading session nodes directly.`,
        "warning",
        "System",
      );
    }
    addLog(`Loaded session: ${session.name}`, "info", "System");
  }, [
    activeSessionId,
    savedTemplates,
    addLog,
    updateNextNodeId,
    updateSessionNodes,
    setSessions,
    setNodes,
    setTemplateNodeIds,
    setActiveTemplateId,
    setLastSavedNodes,
    setTemplateStates,
    history,
    historyIndex,
    historyIndexRef,
    setSessionHistory,
    setShowTemplateSelection
  ]);

  // Auto-load session when activeSessionId is set and sessions are available
  // This ensures sessions are loaded with their config from the API when coming back from dashboard
  const prevActiveSessionIdRef = useRef<string | null>(null);
  useEffect(() => {
    // Don't auto-load if template selection is showing
    if (showTemplateSelection) return;
    if (!activeSessionId || sessions.length === 0) {
      prevActiveSessionIdRef.current = activeSessionId;
      return;
    }

    // Only auto-load if the session ID actually changed
    if (prevActiveSessionIdRef.current === activeSessionId) {
      return;
    }

    const session = sessions.find((s) => s.id === activeSessionId);
    if (session) {
      // Only auto-load if session doesn't have nodes loaded yet (empty or undefined)
      // This prevents re-loading when nodes are already set from a previous load
      const hasNodes = session.nodes && session.nodes.length > 0;
      if (!hasNodes) {
        prevActiveSessionIdRef.current = activeSessionId;
        handleLoadSession(session, true).catch((error) => {
          console.error('Failed to auto-load session:', error);
        });
      } else {
        prevActiveSessionIdRef.current = activeSessionId;
      }
    }
  }, [activeSessionId, sessions, showTemplateSelection, handleLoadSession]);

  // State for delete confirmation modal
  const [showDeleteConfirmModal, setShowDeleteConfirmModal] = useState(false);
  const [pendingDeleteSessionId, setPendingDeleteSessionId] = useState<string | null>(null);

  // This ensures position updates from WebSocket can be applied to the nodes
  useEffect(() => {
    if (
      simulationState === "running" &&
      nodes.length > 0 &&
      simulationNodes.length === 0
    ) {
      // Initialize simulationNodes with current nodes when simulation is already running
      setSimulationNodes(JSON.parse(JSON.stringify(nodes)));
      addLog("Reconnected to running simulation", "info", "System");
    }
  }, [simulationState, nodes, simulationNodes.length, addLog]);


  // Show delete confirmation modal
  const handleDeleteSession = (sessionId: string) => {
    const session = sessions.find(s => s.id === sessionId);
    if (!session) {
      addLog("Session not found", "error", "System");
      return;
    }
    setPendingDeleteSessionId(sessionId);
    setShowDeleteConfirmModal(true);
  };

  // Actually perform the deletion after confirmation
  const confirmDeleteSession = () => {
    if (!pendingDeleteSessionId) return;

    const sessionId = pendingDeleteSessionId;
    const session = sessions.find(s => s.id === sessionId);
    if (!session) {
      addLog("Session not found", "error", "System");
      setShowDeleteConfirmModal(false);
      setPendingDeleteSessionId(null);
      return;
    }

    const isUnbuilt = activeSessionId === sessionId && simulationState !== "built";

    // For unbuilt sessions, only delete locally (don't call API)
    if (isUnbuilt) {
      // Remove from sessions array
      setSessions((prev) => prev.filter((s) => s.id !== sessionId));
      setSessionHistory((prev) => prev.filter((id) => id !== sessionId));

      // Clear state and redirect to template selection (create session page)
      const clearedState = getClearedState();
      setActiveSessionId(clearedState.activeSessionId);
      setNodes(clearedState.nodes);
      setActiveTemplateId(clearedState.activeTemplateId);
      setTemplateNodeIds(clearedState.templateNodeIds);
      setLastSavedNodes(clearedState.lastSavedNodes);
      setSimulationState('unsaved');
      setPlaceMode(clearedState.placeMode);
      setLogs(clearedState.logs);
      setTimeOfDay(clearedState.timeOfDay);
      setNodesAtBuildTime(clearedState.nodesAtBuildTime);

      const clearedHistory = clearHistoryUtil("empty", {
        history,
        historyIndex,
        historyIndexRef,
      });
      setHistory(clearedHistory.history);
      setHistoryIndex(clearedHistory.historyIndex);

      // Redirect to template selection (create session page)
      setShowTemplateSelection(true);
      addLog("Session deleted", "info", "System");
      setShowDeleteConfirmModal(false);
      setPendingDeleteSessionId(null);
      return;
    }

    // For built sessions, delete from backend first, then update local state
    deleteSession(session.name).then(() => {
      if (activeSessionId === sessionId) {
        setSessions((prev) => {
          const updated = prev.filter((s) => s.id !== sessionId);
          setSessionHistory((prevHistory) => {
            const filteredHistory = prevHistory.filter((id) => id !== sessionId);
            const sessionToRestore = findSessionToRestore(
              filteredHistory,
              updated,
            );

            if (sessionToRestore) {
              setTimeout(() => handleLoadSession(sessionToRestore), 0);
              addLog(
                `Session deleted. Restored previous session: ${sessionToRestore.name}`,
                "info",
                "System",
              );
            } else {
              // Clear all state
              const clearedState = getClearedState();
              setActiveSessionId(clearedState.activeSessionId);
              setNodes(clearedState.nodes);
              setActiveTemplateId(clearedState.activeTemplateId);
              setTemplateNodeIds(clearedState.templateNodeIds);
              setLastSavedNodes(clearedState.lastSavedNodes);
              setSimulationState(clearedState.simulationState === 'idle' ? 'unsaved' : clearedState.simulationState);
              setPlaceMode(clearedState.placeMode);
              setLogs(clearedState.logs);
              setTimeOfDay(clearedState.timeOfDay);
              setNodesAtBuildTime(clearedState.nodesAtBuildTime);

              // Clear history for empty template
              const clearedHistory = clearHistoryUtil("empty", {
                history,
                historyIndex,
                historyIndexRef,
              });
              setHistory(clearedHistory.history);
              setHistoryIndex(clearedHistory.historyIndex);

              // Show template selection if no sessions exist to avoid blank page
              if (updated.length === 0) {
                setShowTemplateSelection(true);
              }
              addLog("Session deleted", "info", "System");
            }
            return filteredHistory;
          });
          return updated;
        });
      } else {
        setSessions((prev) => prev.filter((s) => s.id !== sessionId));
        setSessionHistory((prev) => prev.filter((id) => id !== sessionId));
        addLog("Session deleted", "info", "System");
      }
    }).catch(error => {
      // Only log error if it's not a 404 (session might not exist on backend yet)
      if (error.response?.status === 404) {
        // Session doesn't exist on backend, but delete locally anyway
        if (activeSessionId === sessionId) {
          setSessions((prev) => {
            const updated = prev.filter((s) => s.id !== sessionId);
            setSessionHistory((prevHistory) => {
              const filteredHistory = prevHistory.filter((id) => id !== sessionId);
              const sessionToRestore = findSessionToRestore(
                filteredHistory,
                updated,
              );

              if (sessionToRestore) {
                setTimeout(() => handleLoadSession(sessionToRestore), 0);
                addLog(
                  `Session deleted. Restored previous session: ${sessionToRestore.name}`,
                  "info",
                  "System",
                );
              } else {
                const clearedState = getClearedState();
                setActiveSessionId(clearedState.activeSessionId);
                setNodes(clearedState.nodes);
                setActiveTemplateId(clearedState.activeTemplateId);
                setTemplateNodeIds(clearedState.templateNodeIds);
                setLastSavedNodes(clearedState.lastSavedNodes);
                setSimulationState(clearedState.simulationState === 'idle' ? 'unsaved' : clearedState.simulationState);
                setPlaceMode(clearedState.placeMode);
                setLogs(clearedState.logs);
                setTimeOfDay(clearedState.timeOfDay);
                setNodesAtBuildTime(clearedState.nodesAtBuildTime);

                const clearedHistory = clearHistoryUtil("empty", {
                  history,
                  historyIndex,
                  historyIndexRef,
                });
                setHistory(clearedHistory.history);
                setHistoryIndex(clearedHistory.historyIndex);

                if (updated.length === 0) {
                  setShowTemplateSelection(true);
                }
                addLog("Session deleted", "info", "System");
              }
              return filteredHistory;
            });
            return updated;
          });
        } else {
          setSessions((prev) => prev.filter((s) => s.id !== sessionId));
          setSessionHistory((prev) => prev.filter((id) => id !== sessionId));
          addLog("Session deleted", "info", "System");
        }
      } else {
        const errorMessage = error.response?.data?.message || error.message || "Unknown error";
        addLog(`Failed to delete session: ${errorMessage}`, "error", "System");
      }
    });

    setShowDeleteConfirmModal(false);
    setPendingDeleteSessionId(null);
  };

  // Cancel delete confirmation
  const cancelDeleteSession = () => {
    setShowDeleteConfirmModal(false);
    setPendingDeleteSessionId(null);
  };

  // State for duplicate name modal
  const [showDuplicateNameModal, setShowDuplicateNameModal] = useState(false);
  const [pendingRename, setPendingRename] = useState<{ sessionId: string; newName: string } | null>(null);

  // Rename session
  const handleRenameSession = useCallback(
    (sessionId: string, newName: string) => {
      if (!newName.trim()) {
        addLog("Session name cannot be empty", "warning", "System");
        return;
      }
      const session = sessions.find(s => s.id === sessionId);
      if (!session) {
        addLog("Session not found", "error", "System");
        return;
      }

      // Check if the new name already exists (excluding the current session)
      const duplicateSession = sessions.find(s => s.id !== sessionId && s.name === newName.trim());
      if (duplicateSession) {
        setPendingRename({ sessionId, newName: newName.trim() });
        setShowDuplicateNameModal(true);
        return;
      }

      // Check if session is unbuilt (only update local state, don't call API)
      const isUnbuilt = activeSessionId === sessionId && simulationState !== "built";

      // Update local state for immediate feedback (for both built and unbuilt sessions)
      setSessions((prev) =>
        prev.map((s) => {
          if (s.id === sessionId) {
            return {
              ...s,
              name: newName.trim(),
              lastModified: Date.now(),
            };
          }
          return s;
        }),
      );

      // Only update backend if session is built
      if (isUnbuilt) {
        addLog(`Session renamed to "${newName.trim()}"`, "info", "System");
        return;
      }

      // Update backend for built sessions
      renameSession(session.name, newName.trim()).then(() => {
        addLog(`Session renamed to "${newName.trim()}"`, "info", "System");
      }).catch(error => {
        // Revert local state on error
        setSessions((prev) =>
          prev.map((s) => {
            if (s.id === sessionId) {
              return {
                ...s,
                name: session.name, // Revert to original name
                lastModified: session.lastModified,
              };
            }
            return s;
          }),
        );
        const errorMessage = error.response?.data?.message || error.message || "Unknown error";
        addLog(`Failed to rename session: ${errorMessage}`, "error", "System");
      });
    },
    [addLog, sessions, activeSessionId, simulationState],
  );

  // Handle cancel rename
  const handleCancelRename = useCallback(() => {
    setShowDuplicateNameModal(false);
    setPendingRename(null);
  }, []);

  // Rename template (saved templates only)
  const handleRenameTemplate = useCallback(
    (templateId: string, newName: string) => {
      if (!newName.trim()) {
        addLog("Template name cannot be empty", "warning", "System");
        return;
      }
      // Only allow renaming saved templates, not predefined ones
      const savedTemplate = savedTemplates.find((t) => t.id === templateId);
      if (!savedTemplate) {
        addLog("Cannot rename predefined templates", "warning", "System");
        return;
      }
      setSavedTemplates((prev) =>
        prev.map((t) => {
          if (t.id === templateId) {
            return {
              ...t,
              name: newName.trim(),
              savedAt: Date.now(),
            };
          }
          return t;
        }),
      );
      addLog(`Template renamed to "${newName.trim()}"`, "info", "System");
    },
    [savedTemplates, addLog],
  );

  // Save current session as template
  const handleSaveTemplate = useCallback(() => {
    if (nodes.length === 0) {
      addLog("Cannot save: Empty playground", "warning", "System");
      return;
    }

    const baseTemplate = activeTemplateId
      ? findTemplateById(activeTemplateId, savedTemplates) || null
      : null;
    const { name: templateName, description: baseDescription } =
      generateTemplateName(baseTemplate, savedTemplates);
    const description =
      !baseTemplate && activeSessionId
        ? `Saved from ${sessions.find((s) => s.id === activeSessionId)?.name || "session"}`
        : baseDescription;

    const newTemplate: Template = {
      id: `saved-${Date.now()}`,
      name: templateName,
      description,
      nodes: cloneNodes(nodes),
      savedAt: Date.now(),
    };

    setSavedTemplates((prev) => [...prev, newTemplate]);
    setLastSavedNodes(cloneNodes(nodes));

    addLog(
      `Template "${templateName}" saved successfully`,
      "success",
      "System",
    );
  }, [
    activeSessionId,
    activeTemplateId,
    nodes,
    sessions,
    savedTemplates,
    addLog,
  ]);

  // Delete template (saved templates only, predefined cannot be deleted from storage)
  const handleDeleteTemplate = useCallback(
    (templateId: string) => {
      const savedTemplate = savedTemplates.find((t) => t.id === templateId);
      if (savedTemplate) {
        setSavedTemplates((prev) => prev.filter((t) => t.id !== templateId));
        addLog(`Template "${savedTemplate.name}" deleted`, "info", "System");
      } else {
        const predefinedTemplate = defaultTemplates.find((t) => t.id === templateId);
        if (predefinedTemplate) {
          addLog(
            `Predefined template "${predefinedTemplate.name}" cannot be deleted`,
            "info",
            "System",
          );
        }
      }
    },
    [savedTemplates, addLog],
  );

  // Persist saved templates to localStorage
  useEffect(() => {
    saveTemplatesToStorage(savedTemplates);
  }, [savedTemplates]);

  // Add Node Manually
  const handleAddNode = (x: number, y: number) => {
    if (!placeMode) return;

    // Auto-create session if there's no active session
    if (!activeSessionId && !activeTemplateId) {
      const sessionId = Math.random().toString(36).substring(2, 11);

      // Find a unique session name by checking all existing sessions
      let sessionNumber = 1;
      let sessionName = `New session ${sessionNumber}`;

      // Keep incrementing until we find a unique name
      while (sessions.some(s => s.name === sessionName)) {
        sessionNumber++;
        sessionName = `New session ${sessionNumber}`;
      }

      const newSession: Session = {
        id: sessionId,
        name: sessionName,
        templateId: "empty", // Special template ID for empty sessions
        createdAt: Date.now(),
        lastModified: Date.now(),
        nodes: [],
      };

      setSessions((prev) => [newSession, ...prev]);
      setActiveSessionId(sessionId);
      setActiveTemplateId("empty");

      // Initialize history for empty template with initial empty state
      setHistory((prev) => ({
        ...prev,
        empty: [[]],
      }));
      setHistoryIndex((prev) => ({
        ...prev,
        empty: 0,
      }));
      historyIndexRef.current = {
        ...historyIndexRef.current,
        empty: 0,
      };
    }

    const id = nextNodeId.current++;
    const name = `${placeMode.toUpperCase()}-${id}`;
    const newNode: NetworkNode = {
      id,
      type: placeMode,
      x,
      y,
      name,
      status: "good",
      channel: placeMode === "ap" ? 36 : undefined,
    };

    setNodes((prev) => {
      const newNodes = [...prev, newNode];
      // Auto-save state for current template/session
      const templateId = activeTemplateId || "empty";
      setTemplateStates((prevStates) => ({
        ...prevStates,
        [templateId]: newNodes,
      }));
      saveToHistory(newNodes);
      return newNodes;
    });

    addLog(
      `Added ${name} at pos (${Math.round(x)}%, ${Math.round(y)}%)`,
      "success",
      "Planner",
    );
  };

  // Delete Node
  const handleDeleteNode = (id: string | number) => {
    const nodeId = typeof id === 'string' ? parseInt(id, 10) : id;
    if (isNaN(nodeId)) {
      console.error('Invalid node ID:', id);
      return;
    }
    const node = nodes.find((n) => n.id === nodeId);
    setNodes((prev) => {
      const newNodes = prev.filter((n) => n.id !== nodeId);
      // Auto-save state for current template
      if (activeTemplateId) {
        setTemplateStates((prevStates) => ({
          ...prevStates,
          [activeTemplateId]: newNodes,
        }));
        saveToHistory(newNodes);
      }
      return newNodes;
    });
    // Also remove from template tracking if it was a template node
    setTemplateNodeIds((prev) => {
      const newSet = new Set(prev);
      newSet.delete(nodeId);
      return newSet;
    });
    if (node) {
      addLog(`Removed ${node.name}`, "warning", "Planner");
    }
  };

  // Update Node Position (for dragging)
  const handleUpdateNode = (
    id: number,
    x: number,
    y: number,
    isDragEnd: boolean = false,
  ) => {
    setNodes((prev) => {
      const updatedNodes = prev.map((n) => (n.id === id ? { ...n, x, y } : n));
      // Auto-save state for current template
      if (activeTemplateId) {
        setTemplateStates((prevStates) => ({
          ...prevStates,
          [activeTemplateId]: updatedNodes,
        }));
        // Only save to history when drag ends, not during every mouse move
        if (isDragEnd) {
          saveToHistory(updatedNodes);
        }
      }
      return updatedNodes;
    });
  };

  // Build Simulation
  const handleBuild = () => {
    if (nodes.length === 0) {
      addLog("Cannot build: Canvas is empty.", "error", "System");
      return;
    }

    setPlaceMode(null); // Deselect placement mode
    setSimulationState("building");
    setLogs([]); // Reset logs for new run
    addLog(`Starting build with ${optimizer}...`, "info", "System");

    const activeSession = sessions.find((s) => s.id === activeSessionId);
    const sessionName = activeSession?.name || 'unnamed-session';
    const createdAt = activeSession?.createdAt || Date.now();

    sendBuildConfig(nodes, sessionName, createdAt);
    setSimulationState("built");
    setNodesAtBuildTime(JSON.parse(JSON.stringify(nodes))); // Store snapshot of nodes at build time
  };

  // Handle Escape key to deselect placement mode
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        setPlaceMode(null);
      }
    };

    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, []);



  // Helper: Handle random simulation events
  const handleRandomEvent = useCallback(() => {
    const rand = Math.random();
    const currentNodes = simulationNodes.length > 0 ? simulationNodes : nodes;

    if (rand > 0.8) {
      if (
        currentNodes.some((n) => n.type === "microwave" || n.type === "ble")
      ) {
        addLog(
          "Non-WiFi interference detected. Duty cycle spike > 40%.",
          "warning",
          "Device",
        );
        setSimulationNodes((prev) =>
          prev.map((n) =>
            n.type === "client" && Math.random() > 0.5
              ? { ...n, status: "poor" }
              : n,
          ),
        );
      }
    } else if (rand > 0.4 && rand < 0.5) {
      setSimulationNodes((prev) =>
        prev.map((n) => (n.type === "client" ? { ...n, status: "good" } : n)),
      );
      addLog(
        "Channel conditions normalized. Client QoE improved.",
        "success",
        "AI",
      );
    } else if (rand < 0.05) {
      addLog(`Optimizing AP TX Power via ${optimizer} model.`, "info", "AI");
    }
  }, [simulationNodes, nodes, addLog, optimizer]);

  // Handle Stress Tests
  const handleStressTest = useCallback(async (testType: 'dfs' | 'throughput' | 'interference') => {
    // Map frontend test types to backend commands
    const commandMap = {
      'dfs': 'FORCE_DFS',
      'throughput': 'HIGH_THROUGHPUT',
      'interference': 'HIGH_INTERFERENCE'
    } as const;

    const command = commandMap[testType];

    try {
      const response = await sendStressTestCommand(command);
      addLog(response.message, "success", "System");
    } catch (error) {
      const errorMessage = error instanceof Error ? error.message : String(error);
      addLog(errorMessage, "error", "System");
      console.error("Stress test error:", error);
    }
  }, [addLog]);

  return (
    <div
      className="flex h-screen bg-background font-sans text-foreground overflow-hidden relative"
      style={{ touchAction: "pan-x pan-y" }}
    >
      {/* Left Sidebar (Templates) - Fixed Width, full screen height */}
      <SidebarLeft
        onCreateSession={handleShowTemplateSelection}
        onLoadSession={handleLoadSession}
        onDeleteSession={handleDeleteSession}
        onRenameSession={handleRenameSession}
        onNavigateAway={() => {
          if (activeSessionId) {
            updateSessionNodes(activeSessionId, nodes);
          }
        }}
        activeSessionId={activeSessionId}
        sessions={sessions}
        disabled={simulationState === "running" || simulationState === "starting" || simulationState === "stopping"}
        nodes={nodes}
        templateStates={templateStates}
        simulationState={
          simulationState === 'unsaved' || simulationState === 'built' || simulationState === 'starting' || simulationState === 'stopping'
            ? 'idle'
            : (simulationState === 'building' || simulationState === 'running' ? simulationState : 'idle')
        }
      />

      <div
        className="flex flex-col flex-1 overflow-hidden relative"
        onClick={(e) => {
          // Deselect canvas when clicking outside (on the page container, not on child elements)
          if (e.target === e.currentTarget) {
            setIsCanvasFocused(false);
          }
        }}
      >
        <div className="flex flex-1 overflow-hidden relative">
          {/* Main Content Area */}
          <div className="flex-1 flex flex-col relative z-0 min-w-0">
            {/* Playground Canvas */}
            <Playground
              nodes={
                simulationState === "running" && simulationNodes.length > 0
                  ? simulationNodes
                  : nodes
              }
              onAddNode={handleAddNode}
              onDeleteNode={handleDeleteNode}
              onUpdateNode={handleUpdateNode}
              placeMode={placeMode}
              isRunning={simulationState === "running"}
              isBuilding={simulationState === "building"}
              isStarting={simulationState === "starting"}
              isStopping={simulationState === "stopping"}
              isBuilt={simulationState === "built"}
              timeOfDay={timeOfDay}
              activeSessionId={activeSessionId}
              activeSessionName={activeSessionName}
              showScale={showScale}
              onToggleScale={() => setShowScale((prev) => !prev)}
              activeTemplateId={activeTemplateId}
              onDeleteSession={handleDeleteSession}
              onCanvasFocus={() => setIsCanvasFocused(true)}
              onCanvasBlur={() => setIsCanvasFocused(false)}
              onSaveTemplate={handleSaveTemplate}
              onDeleteTemplate={handleDeleteTemplate}
              onRenameTemplate={handleRenameTemplate}
              showTemplateSelection={showTemplateSelection}
              hasUnsavedChanges={hasUnsavedChanges}
              savedTemplates={savedTemplates}
              sessions={sessions}
              allTemplates={defaultTemplates}
              onSelectTemplate={(templateId: string) => {
                if (templateId === "empty") {
                  const newSession = createEmptySession(sessions);
                  setSessions((prev) => [newSession, ...prev]);
                  setActiveSessionId(newSession.id);
                  setNodes([]);
                  setTemplateNodeIds(new Set());
                  setActiveTemplateId("empty");
                  nextNodeId.current = 1;

                  const historyResult = initializeSessionHistory("empty", [], {
                    history,
                    historyIndex,
                    historyIndexRef,
                  });
                  setHistory(historyResult.history);
                  setHistoryIndex(historyResult.historyIndex);
                  setShowTemplateSelection(false);
                  return;
                }

                const template = findTemplateById(templateId, savedTemplates);
                if (template) {
                  handleCreateSession(template);
                } else {
                  console.error("Template not found:", templateId);
                }
              }}
              onCloseTemplateSelection={handleCloseTemplateSelection}
              onUndo={handleUndo}
              onRedo={handleRedo}
              onReset={handleReset}
              canUndo={
                nodes.length > 0 &&
                  (activeTemplateId || "empty") &&
                  history[activeTemplateId || "empty"]
                  ? (historyIndexRef.current[activeTemplateId || "empty"] ??
                    historyIndex[activeTemplateId || "empty"] ??
                    -1) > 0
                  : false
              }
              canRedo={
                (activeTemplateId || "empty") &&
                  history[activeTemplateId || "empty"]
                  ? (historyIndexRef.current[activeTemplateId || "empty"] ??
                    historyIndex[activeTemplateId || "empty"] ??
                    -1) <
                  history[activeTemplateId || "empty"].length - 1
                  : false
              }
              canReset={canReset()}
              isDisabled={
                simulationState === "building" ||
                simulationState === "running" ||
                simulationState === "starting" ||
                simulationState === "stopping" ||
                !isCanvasFocused
              }
              onSelectTool={setPlaceMode}
              hoveredApId={hoveredApId}
              setHoveredApId={setHoveredApId}
              hoveredClientId={hoveredClientId}
              setHoveredClientId={setHoveredClientId}
              connectedClientIds={connectedClientIds}
              clientToApMap={clientToApMap}
              isControllerEnabled={isControllerEnabled}
              onToggleController={async () => {
                if (simulationState !== "running") return;
                try {
                  if (isControllerEnabled) {
                    await stopRL();
                    setIsControllerEnabled(false);
                  } else {
                    await startRL();
                    setIsControllerEnabled(true);
                  }
                } catch (e) {
                  console.error('Failed to toggle RL:', e);
                }
              }}
              onStressTest={handleStressTest}
            />

            {/* Controls Footer */}
            <ControlPanel
              isBuilding={simulationState === "building"}
              isRunning={simulationState === "running"}
              inRunTransition={simulationState === "starting" || simulationState == "stopping"}
              isEmpty={nodes.length === 0}
              isBuilt={simulationState == "built"}
              hasChanges={
                simulationState === "built" &&
                nodesAtBuildTime.length > 0 &&
                JSON.stringify(nodes) !== JSON.stringify(nodesAtBuildTime)
              }
              isNavigatingAway={isNavigatingAway}
              showTemplateSelection={showTemplateSelection}
              onBuild={handleBuild}
              onRun={async () => {
                setPlaceMode(null); // Deselect placement mode
                setSimulationState("starting");
                // Initialize simulation nodes with copy of current nodes
                setSimulationNodes(JSON.parse(JSON.stringify(nodes)));
                // Call the API to start simulation with session name
                try {
                  const sessionName = activeSessionName || 'unnamed-session';
                  const response = await startSimulation(sessionName);
                  addLog(response.message, "success", "System");
                  setSimulationState("running");
                  // Switch to simulation mode for dashboard
                  await switchDataMode('simulation');
                  localStorage.setItem('data-mode', 'simulation');
                } catch (error) {
                  addLog(
                    `Failed to start simulation: ${error}`,
                    "error",
                    "System",
                  );
                  console.error("Error starting simulation:", error);
                  setSimulationState("built"); // Reset to idle if API call fails
                }
              }}
              onStop={async () => {
                // Set stopping state to show stopping overlay
                setSimulationState("stopping");
                // Clear simulation nodes to restore original nodes and call the API to stop simulation
                setSimulationNodes([]);
                try {
                  const response = await stopSimulation();
                  addLog(response.message, "success", "System");
                  setSimulationState("built");
                  setNodesAtBuildTime(JSON.parse(JSON.stringify(nodes))); // Store snapshot after run
                  // Switch back to replay mode for dashboard
                  await switchDataMode('replay');
                  localStorage.setItem('data-mode', 'replay');
                } catch (error) {
                  addLog(
                    `Failed to stop simulation: ${error}`,
                    "error",
                    "System",
                  );
                  console.error("Error stopping simulation:", error);
                  setSimulationState("built"); // Still set to built even if API call fails
                  setNodesAtBuildTime(JSON.parse(JSON.stringify(nodes))); // Store snapshot after run
                }
              }}
              onSaveTemplate={handleSaveTemplate}
              hasUnsavedChanges={hasUnsavedChanges}
              nodesCount={nodes.length}
              onNavigateAway={() => {
                // Set navigating away to disable RUN button
                setIsNavigatingAway(true);
                // Stop simulation if running, starting, or stopping
                if (simulationState === "running" || simulationState === "starting" || simulationState === "stopping") {
                  setSimulationState("built");
                  setSimulationNodes([]);
                }
                // Save current session's state before navigating away
                if (activeSessionId) {
                  updateSessionNodes(activeSessionId, nodes);
                }
                // Don't clear active session - keep it so CanvasPreview shows the correct session
              }}
            />
          </div>

          {/* Right Sidebar (Dashboard & Logs) - Fixed Width */}
          <SidebarRight
            logs={logs}
            optimizer={optimizer}
            setOptimizer={setOptimizer}
            simulationState={simulationState}
            networkMetrics={networkMetrics}
          />
        </div>
      </div>

      {/* Duplicate Session Name Modal */}
      <Dialog open={showDuplicateNameModal} onOpenChange={(open) => { if (!open) handleCancelRename() }}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Session Name Already Exists</DialogTitle>
            <DialogDescription>
              A session with the name "{pendingRename?.newName}" already exists. Please use a different name.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button variant="outline" onClick={handleCancelRename}>
              OK
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* Delete Session Confirmation Modal */}
      <Dialog
        open={showDeleteConfirmModal}
        onOpenChange={(open) => {
          setShowDeleteConfirmModal(open);
          if (!open) {
            setPendingDeleteSessionId(null);
          }
        }}
      >
        <DialogContent className="bg-card">
          <DialogHeader>
            <DialogTitle className="text-foreground">Delete Session</DialogTitle>
            <DialogDescription>
              Are you sure you want to delete "{sessions.find(s => s.id === pendingDeleteSessionId)?.name || 'this session'}"? This action cannot be undone.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button
              variant="outline"
              onClick={cancelDeleteSession}
            >
              Cancel
            </Button>
            <Button
              variant="destructive"
              onClick={confirmDeleteSession}
            >
              Delete
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
