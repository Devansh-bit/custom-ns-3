"use client";

import { useEffect, useState, useRef, useCallback } from "react";
import { useRouter, usePathname } from "next/navigation";
import { NetworkNode, Session, NodeType } from "@/types";
import { Wifi, Smartphone, Microwave, Bluetooth, Radio, X, Maximize2, Zap } from "lucide-react";
import { fetchSessions, fetchCurrentSimulation, fetchSessionConfig } from "@/lib/api-playground";
import { configToNodes } from "@/lib/playground-build";

const SESSIONS_KEY = 'playground-sessions';
const ACTIVE_SESSION_KEY = 'active-session-id';
const PREVIEW_VISIBLE_KEY = 'canvas-preview-visible';
const PLAYGROUND_STATE_KEY = 'playground-state';

const renderIcon = (type: NodeType, size: number) => {
  switch (type) {
    case 'ap': return <Wifi size={size} />;
    case 'client': return <Smartphone size={size} />;
    case 'ble': return <Bluetooth size={size} />;
    case 'zigbee': return <Radio size={size} />;
    case 'microwave': return <Microwave size={size} />;
    default: return <div />;
  }
};

const getNodeStyle = (node: NetworkNode) => {
  switch (node.type) {
    case 'ap': return 'bg-blue-200 text-blue-700 border-blue-400 dark:bg-blue-900 dark:text-blue-300 dark:border-blue-600';
    case 'client':
      if (node.status === 'poor') return 'bg-red-100 text-red-600 border-red-300 dark:bg-red-900/50 dark:text-red-400 dark:border-red-700';
      if (node.status === 'fair') return 'bg-yellow-100 text-yellow-600 border-yellow-300 dark:bg-yellow-900/50 dark:text-yellow-400 dark:border-yellow-700';
      return 'bg-white text-slate-700 border-slate-300 dark:bg-slate-800 dark:text-slate-300 dark:border-slate-600';
    case 'microwave':
    case 'ble':
    case 'zigbee': return 'bg-orange-100 text-orange-600 border-orange-300 dark:bg-orange-900/50 dark:text-orange-400 dark:border-orange-700';
    default: return 'bg-gray-200 dark:bg-gray-700';
  }
};

export default function CanvasPreview() {
  const router = useRouter();
  const pathname = usePathname();
  const [session, setSession] = useState<Session | null>(null);
  const [nodes, setNodes] = useState<NetworkNode[]>([]);
  const [isVisible, setIsVisible] = useState(true);
  const [simulationState, setSimulationState] = useState<'idle' | 'building' | 'running'>('idle');
  const [currentSessionName, setCurrentSessionName] = useState<string | null>(null);
  const previewRef = useRef<HTMLDivElement>(null);


  // Default size with aspect ratio
  const DEFAULT_WIDTH = 180;
  const DEFAULT_HEIGHT = 144;
  const ASPECT_RATIO = DEFAULT_WIDTH / DEFAULT_HEIGHT;

  // Transition duration (snappy)
  const TRANSITION_DURATION = 500;



  // Default position at bottom-right (YouTube-style miniplayer)
  const RIGHT_MARGIN = 48; // Space from right edge
  const BOTTOM_MARGIN = 16; // Space from bottom edge
  const getDefaultPosition = () => {
    if (typeof window === 'undefined') return { x: 16, y: 16 };
    return {
      x: window.innerWidth - DEFAULT_WIDTH - RIGHT_MARGIN,
      y: window.innerHeight - DEFAULT_HEIGHT - BOTTOM_MARGIN
    };
  };

  // Get start position for transition (full screen top-left)
  const getStartPosition = () => {
    return { x: 0, y: 0 };
  };



  // Always start with default size and position - transition will animate from there
  const [size, setSize] = useState({ width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT });
  const [position, setPosition] = useState(() => getDefaultPosition());

  const [isDragging, setIsDragging] = useState(false);
  const [dragStart, setDragStart] = useState({ x: 0, y: 0 });
  const [isResizing, setIsResizing] = useState(false);
  const [resizeStart, setResizeStart] = useState({ x: 0, y: 0, width: 0, height: 0 });
  const [hasDragged, setHasDragged] = useState(false);
  const [mouseDownPos, setMouseDownPos] = useState({ x: 0, y: 0 });
  const [isTransitioning, setIsTransitioning] = useState(false);
  const transitionHandledRef = useRef(false);

  // Handle transition animation from full-screen to bottom-left (only once on mount)
  useEffect(() => {
    if (typeof window === 'undefined' || transitionHandledRef.current || !isVisible) return;

    // Only animate if coming from playground (we can infer this if we want, or just always animate on mount)
    // For now, let's animate if the flag is set or if we just mounted and want the effect
    const transitionFlag = localStorage.getItem('canvas-preview-transition');

    if (transitionFlag === 'true') {
      transitionHandledRef.current = true;

      // First, set initial position to full screen (WITHOUT transition enabled yet)
      const startPos = getStartPosition();
      setPosition(startPos);
      setSize({ width: window.innerWidth, height: window.innerHeight });

      // Wait for initial state to be painted (without transitions), then enable transitions and animate
      const timer1 = requestAnimationFrame(() => {
        requestAnimationFrame(() => {
          // Now enable transitions
          setIsTransitioning(true);

          // Small delay to ensure transition CSS is applied before changing position
          requestAnimationFrame(() => {
            // Now animate to target position
            const targetPosition = getDefaultPosition();
            setPosition(targetPosition);
            setSize({ width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT });

            // Clear transition flag and dispatch completion event after animation completes
            setTimeout(() => {
              localStorage.removeItem('canvas-preview-transition');
              setIsTransitioning(false);
              // Dispatch custom event to notify that transition is complete
              if (typeof window !== 'undefined') {
                window.dispatchEvent(new CustomEvent('canvas-preview-transition-complete'));
              }
            }, TRANSITION_DURATION);
          });
        });
      });

      return () => {
        if (timer1) cancelAnimationFrame(timer1);
      };
    }
  }, [isVisible]); // Run when component becomes visible




  // Reset position and size to default when component mounts or becomes visible (coming back to dashboard)
  useEffect(() => {
    if (typeof window === 'undefined') return;
    if (isVisible && !isTransitioning) {
      // Don't reset position if we're transitioning
      const transitionFlag = localStorage.getItem('canvas-preview-transition');
      if (transitionFlag !== 'true') {
        setPosition(getDefaultPosition());
        setSize({ width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT });
      }
    }
  }, [isVisible, isTransitioning]); // Reset when visible, but not during transition

  // Update position on window resize to keep it within viewport
  useEffect(() => {
    if (typeof window === 'undefined') return;

    const handleResize = () => {
      // Don't interfere during transition
      if (isTransitioning) return;

      // Keep position within viewport bounds
      const maxX = window.innerWidth - size.width;
      const maxY = window.innerHeight - size.height;

      setPosition((prev: { x: number; y: number }) => ({
        x: Math.max(0, Math.min(prev.x, maxX)),
        y: Math.max(0, Math.min(prev.y, maxY))
      }));
    };

    window.addEventListener('resize', handleResize);
    return () => window.removeEventListener('resize', handleResize);
  }, [size.width, size.height, isTransitioning]);

  // Don't save size - always reset to default when coming back


  // Track last loaded session ID to avoid unnecessary updates
  const lastSessionIdRef = useRef<string | null>(null);
  const lastNodesHashRef = useRef<string>('');

  // Function to load active session (only fetch from API on events, not polling)
  const loadActiveSession = useCallback(async () => {
    if (typeof window === 'undefined') return;

    try {
      // If simulation is running, use the session_name from /playground/current
      if (simulationState === 'running' && currentSessionName) {
        try {
          // Fetch the config for the running session
          const config = await fetchSessionConfig(currentSessionName);
          if (config) {
            // Convert config to nodes
            const sessionNodes = configToNodes(config);
            const nodesHash = JSON.stringify(sessionNodes);
            
            // Only update if nodes have changed
            if (lastNodesHashRef.current !== nodesHash) {
              lastSessionIdRef.current = currentSessionName;
              lastNodesHashRef.current = nodesHash;
              
              // Create a session object for the preview
              const runningSession: Session = {
                id: currentSessionName,
                name: currentSessionName,
                nodes: sessionNodes,
                templateId: '',
                createdAt: Date.now(),
                lastModified: Date.now()
              };
              
              setSession(runningSession);
              setNodes(sessionNodes);
            }
            return;
          }
        } catch (e) {
          console.error('Failed to fetch config for running session:', e);
        }
      }

      // Fallback to normal session loading when simulation is not running
      // Get active session ID
      const activeSessionId = localStorage.getItem(ACTIVE_SESSION_KEY);

      // Fetch sessions from the backend API (only when explicitly called via events)
      const sessionsData = await fetchSessions();
      const sessions: Session[] = sessionsData ? sessionsData.map((session: any) => {
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
        };
      }) : [];

      // Cache sessions in localStorage
      localStorage.setItem(SESSIONS_KEY, JSON.stringify(sessions));

      let selectedSession: Session | null = null;
      if (activeSessionId) {
        selectedSession = sessions.find(s => s.id === activeSessionId) || null;
      }

      // If active session was deleted or not found, only then fall back to most recently modified
      // This ensures the active session ID is always respected when it exists
      if (!selectedSession && sessions.length > 0 && !activeSessionId) {
        // Only use most recently modified if no active session ID was specified
        // Sort by lastModified descending and take the first one
        const sortedSessions = [...sessions].sort((a, b) =>
          (b.lastModified || 0) - (a.lastModified || 0)
        );
        selectedSession = sortedSessions[0];
      }

      if (selectedSession) {
        // Check if session or nodes have changed
        const nodesHash = JSON.stringify(selectedSession.nodes || []);
        const sessionChanged = lastSessionIdRef.current !== selectedSession.id;
        const nodesChanged = lastNodesHashRef.current !== nodesHash;

        if (sessionChanged || nodesChanged) {
          lastSessionIdRef.current = selectedSession.id;
          lastNodesHashRef.current = nodesHash;
          setSession(selectedSession);
          setNodes(selectedSession.nodes || []);
        }
      } else {
        // Create empty session to show preview frame even if no sessions exist
        if (lastSessionIdRef.current !== 'empty') {
          const emptySession: Session = {
            id: 'empty',
            name: 'No Session',
            nodes: [],
            templateId: '',
            createdAt: Date.now(),
            lastModified: Date.now()
          };
          lastSessionIdRef.current = 'empty';
          lastNodesHashRef.current = '';
          setSession(emptySession);
          setNodes([]);
        }
      }
    } catch (e) {
      console.error('Failed to load session preview:', e);
      // Still show preview frame even on error
      if (lastSessionIdRef.current !== 'empty') {
        const emptySession: Session = {
          id: 'empty',
          name: 'No Session',
          nodes: [],
          templateId: '',
          createdAt: Date.now(),
          lastModified: Date.now()
        };
        lastSessionIdRef.current = 'empty';
        lastNodesHashRef.current = '';
        setSession(emptySession);
        setNodes([]);
      }
    }
  }, [simulationState, currentSessionName]);

  // Function to load simulation state from /playground/current API
  const loadSimulationState = useCallback(async () => {
    if (typeof window === 'undefined') return;
    
    // Check /playground/current API to see if simulation is running
    try {
      const currentSim = await fetchCurrentSimulation();
      if (currentSim && currentSim.running === true) {
        setSimulationState('running');
        // Store the session_name from the current simulation
        if (currentSim.session_name) {
          setCurrentSessionName(currentSim.session_name);
        }
      } else {
        setSimulationState('idle');
        setCurrentSessionName(null);
      }
    } catch (e) {
      // If API check fails, assume simulation is not running
      setSimulationState('idle');
      setCurrentSessionName(null);
    }
  }, []);

  // Hide preview when on playground route or when no simulation is running
  useEffect(() => {
    if (typeof window === 'undefined') return;

    // Hide immediately if on playground route (no fade-in)
    if (pathname === '/playground') {
      setIsVisible(false);
      localStorage.setItem(PREVIEW_VISIBLE_KEY, 'false');
      return;
    }

    // Show preview when on dashboard (home page)
    // Always show it when user navigates to dashboard
    if (pathname === '/') {
      setIsVisible(true);
    } else if (pathname !== '/playground') {
      // Ensure it's visible on other pages too if mounted
      setIsVisible(true);
    }
  }, [pathname]);

  // Load session on mount and when active session changes
  useEffect(() => {
    if (typeof window === 'undefined') return;

    // Don't load session if we're on playground
    if (pathname === '/playground') return;

    // Load initial session and simulation state
    loadActiveSession();
    loadSimulationState();

    // Listen for storage changes (cross-tab updates)
    const handleStorageChange = (e: StorageEvent) => {
      if (e.key === ACTIVE_SESSION_KEY) {
        loadActiveSession();
      }
      if (e.key === PLAYGROUND_STATE_KEY) {
        loadSimulationState();
      }
    };

    // Listen for custom events (same-tab updates)
    const handleSessionChange = () => {
      loadActiveSession();
    };

    window.addEventListener('storage', handleStorageChange);
    window.addEventListener('session-changed', handleSessionChange);
    window.addEventListener('simulation-state-changed', loadSimulationState);

    return () => {
      window.removeEventListener('storage', handleStorageChange);
      window.removeEventListener('session-changed', handleSessionChange);
      window.removeEventListener('simulation-state-changed', loadSimulationState);
    };
  }, [loadActiveSession, loadSimulationState, pathname]);

  // Load simulation state once when page opens (no polling)
  useEffect(() => {
    if (pathname === '/playground' || typeof window === 'undefined') return;
    
    // Load simulation state only once when page opens
    loadSimulationState();
  }, [pathname, loadSimulationState]);

  // Reload session when simulation state or session name changes
  useEffect(() => {
    if (pathname === '/playground' || typeof window === 'undefined') return;
    loadActiveSession();
  }, [simulationState, currentSessionName, pathname, loadActiveSession]);

  useEffect(() => {
    if (!isDragging && !isResizing) return;

    let animationFrameId: number;

    const handleMouseMove = (e: MouseEvent) => {
      if (animationFrameId) {
        cancelAnimationFrame(animationFrameId);
      }

      animationFrameId = requestAnimationFrame(() => {
        if (isDragging) {
          const newX = e.clientX - dragStart.x;
          const newY = e.clientY - dragStart.y;

          // Track drag distance to distinguish click from drag
          const dragDistance = Math.sqrt(
            Math.pow(e.clientX - mouseDownPos.x, 2) +
            Math.pow(e.clientY - mouseDownPos.y, 2)
          );

          // If moved more than 5px, it's a drag
          if (dragDistance > 5) {
            setHasDragged(true);
          }

          // Constrain to viewport
          const maxX = window.innerWidth - size.width;
          const maxY = window.innerHeight - size.height;

          setPosition({
            x: Math.max(0, Math.min(newX, maxX)),
            y: Math.max(0, Math.min(newY, maxY))
          });
        } else if (isResizing) {
          // For top-right resize: drag up (negative deltaY) = increase size, drag down (positive deltaY) = decrease size
          const deltaY = resizeStart.y - e.clientY; // Inverted: negative when dragging up

          // Calculate size change based on vertical movement
          const sizeDelta = deltaY; // Positive when dragging up (increasing size)
          const scaleFactor = 1 + (sizeDelta / 200); // 200px drag = 100% size increase
          const newWidth = Math.max(200, Math.min(600, resizeStart.width * scaleFactor));
          const newHeight = newWidth / ASPECT_RATIO;

          // Constrain height as well
          const constrainedHeight = Math.max(150, Math.min(400, newHeight));
          const finalWidth = constrainedHeight * ASPECT_RATIO;
          const finalHeight = constrainedHeight;

          setSize({ width: finalWidth, height: finalHeight });

          // Maintain bottom margin - keep bottom edge fixed when resizing from top-right
          const newY = window.innerHeight - finalHeight - 16; // 16px bottom margin
          const newX = Math.max(0, Math.min(position.x, window.innerWidth - finalWidth));

          setPosition({
            x: newX,
            y: newY
          });
        }
      });
    };

    const handleMouseUp = () => {
      if (animationFrameId) {
        cancelAnimationFrame(animationFrameId);
      }
      setIsDragging(false);
      setIsResizing(false);
      // Reset hasDragged after a short delay to allow click detection
      setTimeout(() => setHasDragged(false), 100);
    };

    window.addEventListener('mousemove', handleMouseMove, { passive: true });
    window.addEventListener('mouseup', handleMouseUp);

    return () => {
      if (animationFrameId) {
        cancelAnimationFrame(animationFrameId);
      }
      window.removeEventListener('mousemove', handleMouseMove);
      window.removeEventListener('mouseup', handleMouseUp);
    };
  }, [isDragging, isResizing, dragStart, resizeStart, size.width, size.height, position.x, mouseDownPos]);

  const handleMouseDown = (e: React.MouseEvent) => {
    // Don't allow dragging during transition
    if (isTransitioning) return;
    if ((e.target as HTMLElement).closest('button')) return;
    if ((e.target as HTMLElement).closest('.resize-handle')) return;
    setIsDragging(true);
    setHasDragged(false); // Reset drag flag on new mouse down
    setMouseDownPos({ x: e.clientX, y: e.clientY }); // Store initial mouse position
    setDragStart({
      x: e.clientX - position.x,
      y: e.clientY - position.y
    });
  };

  const handleResizeStart = (e: React.MouseEvent) => {
    // Don't allow resizing during transition
    if (isTransitioning) return;
    e.stopPropagation();
    setIsResizing(true);
    setResizeStart({
      x: e.clientX,
      y: e.clientY,
      width: size.width,
      height: size.height
    });
    // Store the initial position when starting to resize
    setDragStart({
      x: position.x,
      y: position.y
    });
  };

  const handleClose = (e: React.MouseEvent) => {
    e.stopPropagation();
    setIsVisible(false);
    localStorage.setItem(PREVIEW_VISIBLE_KEY, 'false');
  };

  const handleClick = (e: React.MouseEvent) => {
    // Only navigate if not dragging, not resizing, didn't just drag, and not clicking on buttons or resize handle
    if (!isDragging && !isResizing && !hasDragged &&
      !(e.target as HTMLElement).closest('button') &&
      !(e.target as HTMLElement).closest('.resize-handle')) {
      router.push('/playground');
    }
  };

  if (!isVisible) {
    return null;
  }

  // If no session, don't render
  if (!session) {
    return null;
  }

  // Calculate preview dimensions (resizable)
  const previewWidth = size.width;
  const previewHeight = size.height;
  const contentHeight = previewHeight; // No header, full height for canvas

  // Calculate scale to fit nodes (fit to view) - matching Canvas fit-to-view logic
  let scale = 1;
  let translateX = 0;
  let translateY = 0;

  if (nodes.length > 0) {
    // Use preview dimensions as view dimensions (no rulers in preview)
    const viewWidth = previewWidth;
    const viewHeight = contentHeight;
    const visibleWidth = viewWidth;
    const visibleHeight = viewHeight;
    
    // Calculate bounding box of all nodes
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    nodes.forEach(node => {
      // Node sizes are based on Tailwind classes (w-14, w-10, w-12)
      const nodeSize = (node.type === 'ap' ? 56 : (node.type === 'client' ? 40 : 48)); // pixels
      const nodeRadius = nodeSize / 2;
      
      const radiusX_pc = nodeRadius / viewWidth * 100;
      const radiusY_pc = nodeRadius / viewHeight * 100;

      minX = Math.min(minX, node.x - radiusX_pc);
      maxX = Math.max(maxX, node.x + radiusX_pc);
      minY = Math.min(minY, node.y - radiusY_pc);
      maxY = Math.max(maxY, node.y + radiusY_pc);
    });

    // If there are very few nodes, use a larger minimum bounding box to prevent over-zooming
    const minBboxSize = nodes.length <= 3 ? 30 : 5;
    
    if (maxX - minX < minBboxSize) {
      const mid = (minX + maxX) / 2;
      minX = mid - minBboxSize;
      maxX = mid + minBboxSize;
    }
    if (maxY - minY < minBboxSize) {
      const mid = (minY + maxY) / 2;
      minY = mid - minBboxSize;
      maxY = mid + minBboxSize;
    }

    // Add padding around nodes - use percentage of the content bounding box
    const paddingPercent = 15; // 15% padding on each side (total 30% extra space)
    
    // Calculate padding in canvas coordinates (percentage)
    const bboxWidthPercent = maxX - minX;
    const bboxHeightPercent = maxY - minY;
    const paddingX = bboxWidthPercent * (paddingPercent / 100);
    const paddingY = bboxHeightPercent * (paddingPercent / 100);
    
    // Expand bounding box with padding
    const paddedMinX = minX - paddingX;
    const paddedMaxX = maxX + paddingX;
    const paddedMinY = minY - paddingY;
    const paddedMaxY = maxY + paddingY;
    
    const paddedBboxWidthPercent = paddedMaxX - paddedMinX;
    const paddedBboxHeightPercent = paddedMaxY - paddedMinY;
    const paddedBboxWidthPx = paddedBboxWidthPercent / 100 * viewWidth;
    const paddedBboxHeightPx = paddedBboxHeightPercent / 100 * viewHeight;

    const scaleX_fit = visibleWidth / paddedBboxWidthPx;
    const scaleY_fit = visibleHeight / paddedBboxHeightPx;
    const proposedFitScale = Math.min(scaleX_fit, scaleY_fit);
    
    // Limit maximum scale to 0.9 to prevent over-zooming
    const maxScale = 0.9;
    const clampedProposedFitScale = Math.min(Math.max(proposedFitScale, 0.5), maxScale);
    
    // Calculate centering transform (matching Canvas logic)
    const bboxCenterXPercent = (minX + maxX) / 2;
    const bboxCenterYPercent = (minY + maxY) / 2;
    const nodesCenterX = (bboxCenterXPercent / 100) * viewWidth;
    const nodesCenterY = (bboxCenterYPercent / 100) * viewHeight;
    
    const visibleViewCenterX = visibleWidth / 2;
    const visibleViewCenterY = visibleHeight / 2;
    
    translateX = visibleViewCenterX - (clampedProposedFitScale * (nodesCenterX - viewWidth / 2) + viewWidth / 2);
    translateY = visibleViewCenterY - (clampedProposedFitScale * (nodesCenterY - viewHeight / 2) + viewHeight / 2);
    
    scale = clampedProposedFitScale;
  } else {
    // If no nodes, use default scale
    const padding = 0.08;
  const usableWidth = previewWidth * (1 - 2 * padding);
  const usableHeight = contentHeight * (1 - 2 * padding);
    const scaleX = usableWidth / previewWidth;
    const scaleY = usableHeight / contentHeight;
    scale = Math.min(scaleX, scaleY);
  }

  // Icon scale - scale icons smoothly with preview panel size (very small increase)
  // Base icon sizes: AP=56px, Client=40px, Others=48px
  // Scale icons primarily with canvas scale, with minimal increase from preview size
  const previewSizeFactor = previewWidth / DEFAULT_WIDTH; // How much larger/smaller than default
  const baseIconScale = 0.7; // Smaller base scale to prevent overlap and show spacing
  // Use square root of preview size factor for gradual increase
  const gradualSizeFactor = 1 + (Math.sqrt(previewSizeFactor) - 1) * 0.5; // Moderate increase (50% of sqrt)
  // Icons scale primarily with canvas scale to maintain spacing, minimal preview size impact
  const iconScale = scale * baseIconScale * gradualSizeFactor;

  // Don't render anything when simulation is not running
  if (simulationState !== 'running') {
    return null;
  }

  return (
    <div
      ref={previewRef}
      onMouseDown={simulationState === 'running' ? handleMouseDown : undefined}
      onClick={simulationState === 'running' ? handleClick : undefined}
      className={`fixed z-50 border rounded-lg shadow-2xl transition-all duration-200 overflow-hidden group select-none ${simulationState === 'running'
          ? 'cursor-move hover:shadow-blue-500/20'
          : 'border-slate-200 bg-slate-50 cursor-not-allowed opacity-80 dark:border-slate-700 dark:bg-slate-800'
        }`}
      style={{
        width: simulationState === 'running' ? `${previewWidth}px` : '200px',
        height: simulationState === 'running' ? `${previewHeight}px` : '40px',
        left: `${position.x}px`,
        top: `${position.y}px`,
        borderColor: simulationState === 'running' ? 'var(--chart-1)' : undefined,
        transition: isTransitioning
          ? `left ${TRANSITION_DURATION}ms cubic-bezier(0.25, 0.46, 0.45, 0.94), top ${TRANSITION_DURATION}ms cubic-bezier(0.25, 0.46, 0.45, 0.94), width ${TRANSITION_DURATION}ms cubic-bezier(0.25, 0.46, 0.45, 0.94), height ${TRANSITION_DURATION}ms cubic-bezier(0.25, 0.46, 0.45, 0.94)`
          : undefined,
      }}
    >

      {/* Content for Running State */}
      {simulationState === 'running' ? (
        <>
          {/* Close button - top right */}
          <button
            onClick={handleClose}
            className="absolute top-2 right-2 z-40 text-slate-500 hover:text-white hover:bg-red-500 rounded p-1 transition-colors dark:text-slate-400"
            title="Close preview"
          >
            <X size={14} />
          </button>

          {/* Expand area - top corner (excluding resize handle area) */}
          <div
            onClick={(e) => {
              e.stopPropagation();
              router.push('/playground');
            }}
            onMouseDown={(e) => {
              // Don't prevent if clicking on resize handle or close button
              if ((e.target as HTMLElement).closest('.resize-handle') ||
                (e.target as HTMLElement).closest('button')) {
                return;
              }
              e.stopPropagation();
            }}
            className="absolute top-0 right-0 w-16 h-16 cursor-pointer z-30 group/expand"
            style={{
              clipPath: 'polygon(0 0, calc(100% - 40px) 0, calc(100% - 40px) calc(100% - 12px), 0 100%)',
              pointerEvents: 'auto'
            }}
            title="Expand to playground"
          >
            <Maximize2
              size={16}
              className="absolute top-2 right-2 text-slate-400/0 group-hover/expand:text-slate-600 dark:group-hover/expand:text-slate-400 transition-colors pointer-events-none"
            />
          </div>

          {/* Canvas Preview - Exact replica of canvas, scaled to fit */}
          <div
            className="relative bg-white dark:bg-slate-900 overflow-hidden pointer-events-none"
            style={{
              width: '100%',
              height: `${contentHeight}px`,
            }}
          >
            {/* Scaled Background Grid - exact same as canvas */}
            <div
              className="absolute -inset-[100%] w-[300%] h-[300%] opacity-30 dark:opacity-20 pointer-events-none"
              style={{
                backgroundImage: 'radial-gradient(#94a3b8 1.5px, transparent 1.5px)',
                backgroundSize: '24px 24px'
              }}
            />

            {/* Scaled container to fit nodes to view */}
            <div
              className="relative w-full h-full"
              style={{
                transform: `translate(${translateX}px, ${translateY}px) scale(${scale})`,
                transformOrigin: 'top left',
                width: `${100 / scale}%`,
                height: `${100 / scale}%`,
              }}
            >
              {/* Nodes preview - exact same styling as NetworkNode component, scaled */}
              {nodes.map((node) => (
                <div
                  key={node.id}
                  className="network-node absolute transform -translate-x-1/2 -translate-y-1/2 flex flex-col items-center"
                  style={{
                    left: `${node.x}%`,
                    top: `${node.y}%`
                  }}
                >
                  {/* Interference Animation for microwave/ble/zigbee when running */}
                  {simulationState === 'running' && (node.type === 'microwave' || node.type === 'ble' || node.type === 'zigbee') && (
                    <div
                      className="absolute text-orange-500 animate-pulse pointer-events-none"
                      style={{
                        top: `${-32 * iconScale}px`,
                      }}
                    >
                      <Zap size={24 * iconScale} className="fill-current" />
                    </div>
                  )}

                  {/* Client QoE Halo when running */}
                  {simulationState === 'running' && node.type === 'client' && (
                    <div
                      className={`absolute rounded-full opacity-20 animate-pulse pointer-events-none
                        ${node.status === 'good' ? 'bg-green-500' : node.status === 'poor' ? 'bg-red-500' : 'bg-yellow-500'}
                      `}
                      style={{
                        width: `${64 * iconScale}px`,
                        height: `${64 * iconScale}px`,
                      }}
                    ></div>
                  )}

                  {/* Main Node Icon - scaled proportionally with canvas */}
                  <div
                    className={`
                      relative z-10 ${node.type === 'ap' ? 'rounded-3xl' : node.type === 'client' ? 'rounded-lg' : 'rounded-xl'}
                      ${node.type === 'client' ? 'border' : 'border-2'} shadow-lg flex items-center justify-center ${node.type === 'ap' ? '' : 'bg-white dark:bg-slate-800'}
                      ${getNodeStyle(node)}
                      ${simulationState === 'running' && (node.type === 'microwave' || node.type === 'ble') ? 'animate-jam' : ''}
                    `}
                    style={{
                      width: `${(node.type === 'ap' ? 56 : node.type === 'client' ? 40 : 48) * iconScale}px`,
                      height: `${(node.type === 'ap' ? 56 : node.type === 'client' ? 40 : 48) * iconScale}px`,
                    }}
                  >
                    {/* Signal Animation for APs - matching Canvas animation */}
                    {simulationState === 'running' && node.type === 'ap' && (
                      <>
                        <div
                          className="absolute top-1/2 left-1/2 border border-blue-400/30 rounded-full animate-radiate pointer-events-none -translate-x-1/2 -translate-y-1/2"
                          style={{
                            width: `${192 * iconScale}px`,
                            height: `${192 * iconScale}px`,
                          }}
                        ></div>
                        <div
                          className="absolute top-1/2 left-1/2 border border-blue-400/20 rounded-full animate-radiate pointer-events-none [animation-delay:0.5s] -translate-x-1/2 -translate-y-1/2"
                          style={{
                            width: `${192 * iconScale}px`,
                            height: `${192 * iconScale}px`,
                          }}
                        ></div>
                      </>
                    )}

                    {node.type === 'ap' ? (
                      <Wifi size={24 * iconScale} className="text-blue-700" />
                    ) : (
                      renderIcon(node.type, (node.type === 'client' ? 20 : 24) * iconScale)
                    )}

                    {/* Badge for Clients - only show when running */}
                    {node.type === 'client' && simulationState === 'running' && (
                      <div
                        className={`absolute rounded-full border-2 border-white
                          ${node.status === 'good' ? 'bg-green-500' : node.status === 'poor' ? 'bg-red-500' : 'bg-yellow-400'}
                        `}
                        style={{
                          width: `${12 * iconScale}px`,
                          height: `${12 * iconScale}px`,
                          top: `${-4 * iconScale}px`,
                          right: `${-4 * iconScale}px`,
                        }}
                      ></div>
                    )}
                  </div>
                </div>
              ))}
            </div>
          </div>

          {/* Hover overlay */}
          <div className="absolute inset-0 bg-blue-500/0 group-hover:bg-blue-500/10 transition-colors flex items-center justify-center pointer-events-none">
            <span className="text-xs text-white opacity-0 group-hover:opacity-100 transition-opacity font-medium bg-blue-500/80 dark:bg-blue-600/80 px-3 py-1 rounded">
              Click to open Playground
            </span>
          </div>

          {/* Resize handle - top-right corner */}
          <div
            onMouseDown={handleResizeStart}
            className="absolute top-0 right-0 w-3 h-3 cursor-nesw-resize resize-handle bg-gray-400/30 hover:bg-gray-400/50 dark:bg-gray-600/30 dark:hover:bg-gray-600/50 transition-colors z-40"
            style={{
              clipPath: 'polygon(100% 0, 0 0, 100% 100%)',
              pointerEvents: 'auto'
            }}
          >
            <div className="absolute top-0.5 right-0.5 w-1.5 h-1.5 border-r-2 border-t-2 border-gray-500 dark:border-gray-400"></div>
          </div>
        </>
      ) : (
        /* Collapsed State */
        <div className="w-full h-full flex items-center justify-center">
          <div className="w-2 h-2 rounded-full bg-slate-300 dark:bg-slate-600"></div>
        </div>
      )}
    </div>
  );
}
