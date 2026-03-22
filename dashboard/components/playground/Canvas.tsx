import React, { useRef, useState, useEffect } from 'react';
import { NetworkNode, NodeType } from '@/lib/types';
import { NetworkNode as NetworkNodeComponent } from '@/components/playground/NetworkNode';
import { PlacementHint } from '@/components/playground/PlacementHint';
import { BuildingOverlay } from '@/components/playground/BuildingOverlay';
import { CanvasRulers } from '@/components/playground/CanvasRulers';
import { ConnectionLines } from '@/components/playground/ConnectionLines';

interface CanvasProps {
  nodes: NetworkNode[];
  onAddNode: (x: number, y: number) => void;
  onDeleteNode: (id: string) => void;
  onUpdateNode: (id: string, x: number, y: number, isDragEnd?: boolean) => void;
  placeMode: NodeType | null;
  isRunning: boolean;
  isStarting: boolean;
  isStopping: boolean;
  activeSessionId?: string | null;
  showScale?: boolean;
  activeTemplateId?: string | null;
  onCanvasFocus?: () => void;
  onCanvasBlur?: () => void;
  onMousePosChange?: (pos: { x: number; y: number }) => void;
  onZoomControlsReady?: (controls: { zoomIn: () => void; zoomOut: () => void; resetView: () => void; fitToView: () => void; isFitted: () => boolean }) => void;
  onFittedChange?: (isFitted: boolean) => void;
  hoveredApId: number | null;
  setHoveredApId: React.Dispatch<React.SetStateAction<number | null>>;
  hoveredClientId: number | null;
  setHoveredClientId: React.Dispatch<React.SetStateAction<number | null>>;
  connectedClientIds: Set<number>;
  clientToApMap: Map<number, number>;
}

export const Canvas: React.FC<CanvasProps> = ({
  nodes,
  onAddNode,
  onDeleteNode,
  onUpdateNode,
  placeMode,
  isRunning,
  isStarting,
  isStopping,
  activeSessionId,
  showScale = true,
  activeTemplateId,
  onCanvasFocus,
  onCanvasBlur,
  onMousePosChange,
  onZoomControlsReady,
  onFittedChange,
  hoveredApId,
  setHoveredApId,
  hoveredClientId,
  setHoveredClientId,
  connectedClientIds,
  clientToApMap
}) => {
  const containerRef = useRef<HTMLDivElement>(null);
  const topLevelRef = useRef<HTMLDivElement>(null);

  // Canvas State
  const [transform, setTransform] = useState({ x: 0, y: 0, scale: 1 });
  const [isPanning, setIsPanning] = useState(false);
  const [startPan, setStartPan] = useState({ x: 0, y: 0 });
  const [lastPinchDistance, setLastPinchDistance] = useState<number | null>(null);
  const pinchUpdateRef = useRef<number | null>(null);
  const lastUpdateTimeRef = useRef<number>(0);
  const transformRef = useRef(transform);
  const fittedTransformRef = useRef<{ x: number; y: number; scale: number } | null>(null);
  const fitToViewRef = useRef<(() => void) | null>(null);

  // Drag State (only for clients)
  const [draggedNode, setDraggedNode] = useState<string | null>(null);
  const [dragStart, setDragStart] = useState({ x: 0, y: 0, nodeX: 0, nodeY: 0 });
  const [dragInitialPos, setDragInitialPos] = useState<{ x: number; y: number } | null>(null);
  const isHandlingMouseUpRef = useRef(false);


  // Mouse position for placement hint
  const [mousePos, setMousePos] = useState({ x: 0, y: 0 });

  // Keep ref in sync with state
  useEffect(() => {
    transformRef.current = transform;
  }, [transform]);

  // Fit view when template/session is loaded (only on template/session change, not on node reset)
  const prevActiveSessionIdRef = useRef<string | null | undefined>(activeSessionId);
  const prevActiveTemplateIdRef = useRef<string | null | undefined>(activeTemplateId);
  const shouldFitToViewRef = useRef(false); // Flag to indicate we should fit to view after nodes load

  useEffect(() => {
    const currentSessionId = activeSessionId;
    const prevSessionId = prevActiveSessionIdRef.current;
    const currentTemplateId = activeTemplateId;
    const prevTemplateId = prevActiveTemplateIdRef.current;

    // Only reset view when session or template actually changes, not when nodes are reset
    if (prevSessionId !== currentSessionId && currentSessionId) {
      // Session changed - set flag to fit to view after nodes are loaded
      fittedTransformRef.current = null; // Clear fitted state
      shouldFitToViewRef.current = true; // Mark that we should fit to view
      prevActiveSessionIdRef.current = currentSessionId;
      prevActiveTemplateIdRef.current = currentTemplateId;
      
      // Reset view immediately
      setTransform({ x: 0, y: 0, scale: 1 });
      return;
    }

    if (prevTemplateId !== currentTemplateId && currentTemplateId) {
      // Template changed - set flag to fit to view after nodes are loaded
      fittedTransformRef.current = null; // Clear fitted state
      shouldFitToViewRef.current = true; // Mark that we should fit to view
      prevActiveSessionIdRef.current = currentSessionId;
      prevActiveTemplateIdRef.current = currentTemplateId;
      
      // Reset view immediately
      setTransform({ x: 0, y: 0, scale: 1 });
      return;
    }

    // Update refs without resetting view for other changes (like node reset)
    prevActiveSessionIdRef.current = currentSessionId;
    prevActiveTemplateIdRef.current = currentTemplateId;
  }, [activeSessionId, activeTemplateId]);

  // Auto-fit to view when nodes are loaded after session/template change
  useEffect(() => {
    if (shouldFitToViewRef.current && nodes.length > 0 && fitToViewRef.current) {
      // Clear the flag
      shouldFitToViewRef.current = false;
      
      // Wait for nodes to be rendered, then fit to view
      requestAnimationFrame(() => {
        requestAnimationFrame(() => {
          if (fitToViewRef.current) {
            fitToViewRef.current();
          }
        });
      });
    }
  }, [nodes]);

  // Attach wheel event listener once
  useEffect(() => {
    const handleWheel = (e: WheelEvent) => {
      // Don't prevent default if scrolling in template selection overlay
      const target = e.target as HTMLElement;
      if (target.closest('.template-selection-scrollable')) {
        return; // Allow normal scrolling in template selection
      }
      
      e.preventDefault();
      e.stopPropagation();

      fittedTransformRef.current = null; // Clear fitted state when wheel zooming
      const scaleAmount = -e.deltaY * 0.05;
      const currentScale = transformRef.current.scale;
      const newScale = Math.min(Math.max(0.5, currentScale * (1 + scaleAmount)), 4);

      setTransform(prev => ({ ...prev, scale: newScale }));
    };

    const element = topLevelRef.current;
    if (element) {
      element.addEventListener('wheel', handleWheel, { passive: false });
      return () => {
        element.removeEventListener('wheel', handleWheel);
      };
    }
  }, []);

  // Expose zoom controls to parent
  useEffect(() => {
    if (onZoomControlsReady) {
      const zoomIn = () => {
        if (!containerRef.current) return;
        const rect = containerRef.current.getBoundingClientRect();
        
        // Clear fitted state when user manually zooms
        fittedTransformRef.current = null;
        
        // Use ref to get current transform state (more accurate)
        const currentTransform = transformRef.current;
        const currentScale = currentTransform.scale;
        const newScale = Math.min(currentScale * 1.2, 4);
        const scaleRatio = newScale / currentScale;
        
        // Zoom towards center: scale translation proportionally to keep center fixed
        // This ensures the canvas doesn't shift when zooming
        const newX = currentTransform.x * scaleRatio;
        const newY = currentTransform.y * scaleRatio;
        
        setTransform({ x: newX, y: newY, scale: newScale });
      };
      
      const zoomOut = () => {
        if (!containerRef.current) return;
        const rect = containerRef.current.getBoundingClientRect();
        
        // Clear fitted state when user manually zooms
        fittedTransformRef.current = null;
        
        // Use ref to get current transform state (more accurate)
        const currentTransform = transformRef.current;
        const currentScale = currentTransform.scale;
        const newScale = Math.max(currentScale / 1.2, 0.5);
        const scaleRatio = newScale / currentScale;
        
        // Zoom towards center: scale translation proportionally to keep center fixed
        // This ensures the canvas doesn't shift when zooming
        const newX = currentTransform.x * scaleRatio;
        const newY = currentTransform.y * scaleRatio;
        
        setTransform({ x: newX, y: newY, scale: newScale });
      };
      
      const resetView = () => {
        // Clear fitted state when user manually resets
        fittedTransformRef.current = null;
        setTransform({ x: 0, y: 0, scale: 1 });
      };
      
      const fitToView = () => {
        if (!nodes || nodes.length === 0 || !containerRef.current) {
          setTransform({ x: 0, y: 0, scale: 1 });
          return;
        }

        const { width: viewWidth, height: viewHeight } = containerRef.current.getBoundingClientRect();
        const paddingTop = 56;
        const rulerLeftWidth = 24;
        const rulerBottomHeight = 28;
        const rulerRightWidth = 0; // no ruler on right
        const rulerTopHeight = paddingTop;
        const topGap = viewWidth * 0.02; // 2vw gap at top

        const visibleWidth = viewWidth - rulerLeftWidth - rulerRightWidth;
        const visibleHeight = viewHeight - rulerTopHeight - rulerBottomHeight - topGap;
        
        let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
        nodes.forEach(node => {
          // Node sizes are based on Tailwind classes in NetworkNode.tsx (w-14, w-10, w-12)
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
        const minBboxSize = nodes.length <= 3 ? 30 : 5; // Larger minimum for 3 or fewer nodes
        
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

        const checkVisibility = (scale: number, translateX: number, translateY: number, padding: number = 0) => {
            const tempTransform = { scale, x: translateX, y: translateY };
            
            return nodes.every(node => {
                const nodeSize = (node.type === 'ap' ? 56 : (node.type === 'client' ? 40 : 48));
                const nodeRadius = nodeSize / 2;
                const scaledRadius = nodeRadius * tempTransform.scale;

                const nodeX_px = node.x / 100 * viewWidth;
                const nodeY_px = node.y / 100 * viewHeight;

                const screenX = tempTransform.scale * (nodeX_px - viewWidth / 2) + viewWidth / 2 + tempTransform.x;
                const screenY = tempTransform.scale * (nodeY_px - viewHeight / 2) + viewHeight / 2 + tempTransform.y;
                
                return (screenX - scaledRadius > rulerLeftWidth + padding) &&
                       (screenX + scaledRadius < viewWidth - rulerRightWidth - padding) &&
                       (screenY - scaledRadius > rulerTopHeight + padding) &&
                       (screenY + scaledRadius < viewHeight - rulerBottomHeight - padding);
            });
        };

        const getCenteringTransform = (targetScale: number) => {
            const bboxCenterXPercent = (minX + maxX) / 2;
            const bboxCenterYPercent = (minY + maxY) / 2;
            const nodesCenterX = (bboxCenterXPercent / 100) * viewWidth;
            const nodesCenterY = (bboxCenterYPercent / 100) * viewHeight;
            
            const visibleViewCenterX = rulerLeftWidth + visibleWidth / 2;
            const visibleViewCenterY = rulerTopHeight + topGap + visibleHeight / 2;

            const additionalBottomPaddingPx = 50;
            const effectiveViewCenterY = visibleViewCenterY - (additionalBottomPaddingPx / 2);
            
            const translateX = visibleViewCenterX - (targetScale * (nodesCenterX - viewWidth / 2) + viewWidth / 2);
            const translateY = effectiveViewCenterY - (targetScale * (nodesCenterY - viewHeight / 2) + viewHeight / 2);
            
            return { x: translateX, y: translateY, scale: targetScale };
        };

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
        const fitTransform = getCenteringTransform(clampedProposedFitScale);

        const defaultScale = 0.9; // Maximum scale allowed for fit to view

        // Check visibility at defaultScale with consistent padding (convert percentage padding to pixels)
        const paddingPx = Math.max(40, Math.min(paddedBboxWidthPx, paddedBboxHeightPx) * (paddingPercent / 100));
        const defaultTransform = getCenteringTransform(defaultScale);
        const isVisibleAtDefaultScale = checkVisibility(defaultTransform.scale, defaultTransform.x, defaultTransform.y, paddingPx);

        let finalTransform;
        if (proposedFitScale <= defaultScale && isVisibleAtDefaultScale) {
            // If content naturally fits at a scale <= defaultScale, and is visible at defaultScale with padding, prefer defaultScale.
            finalTransform = defaultTransform;
            setTransform(defaultTransform);
        } else {
            // Otherwise, use the calculated fit-to-view transform.
            finalTransform = fitTransform;
            setTransform(fitTransform);
        }
        
        // Store the fitted transform so we can check if view is already fitted
        fittedTransformRef.current = finalTransform;
      };
      
      const isFitted = () => {
        if (!fittedTransformRef.current || !nodes || nodes.length === 0) return false;
        
        const current = transformRef.current;
        const fitted = fittedTransformRef.current;
        
        // Compare with tolerance for floating point errors
        const tolerance = 0.01; // Small tolerance for comparison
        const scaleMatch = Math.abs(current.scale - fitted.scale) < tolerance;
        const xMatch = Math.abs(current.x - fitted.x) < 1; // 1px tolerance for position
        const yMatch = Math.abs(current.y - fitted.y) < 1;
        
        return scaleMatch && xMatch && yMatch;
      };
      
      // Store fitToView in ref so it can be called when session changes
      fitToViewRef.current = fitToView;
      
      onZoomControlsReady({ zoomIn, zoomOut, resetView, fitToView, isFitted });
    }
  }, [onZoomControlsReady, nodes]);

  // Track fitted state and notify parent when it changes
  useEffect(() => {
    if (!onFittedChange) return;

    const checkFittedState = () => {
      if (!fittedTransformRef.current || !nodes || nodes.length === 0) {
        onFittedChange(false);
        return;
      }

      const current = transformRef.current;
      const fitted = fittedTransformRef.current;

      // Compare with tolerance for floating point errors
      const tolerance = 0.01;
      const scaleMatch = Math.abs(current.scale - fitted.scale) < tolerance;
      const xMatch = Math.abs(current.x - fitted.x) < 1;
      const yMatch = Math.abs(current.y - fitted.y) < 1;

      const isFitted = scaleMatch && xMatch && yMatch;
      onFittedChange(isFitted);
    };

    // Check immediately
    checkFittedState();

    // Check whenever transform changes
    const interval = setInterval(checkFittedState, 100);

    return () => clearInterval(interval);
  }, [transform, nodes, onFittedChange]);

  // Clear fitted state when nodes change (added/deleted/moved significantly)
  const prevNodesLengthRef = useRef(nodes.length);
  useEffect(() => {
    // Only clear if nodes were added or removed (not just moved)
    if (prevNodesLengthRef.current !== nodes.length) {
      fittedTransformRef.current = null;
      prevNodesLengthRef.current = nodes.length;
    }
  }, [nodes.length]);

  // Calculate distance between two touch points
  const getPinchDistance = (touches: React.TouchList): number => {
    if (touches.length < 2) return 0;
    const touch1 = touches[0];
    const touch2 = touches[1];
    const dx = touch2.clientX - touch1.clientX;
    const dy = touch2.clientY - touch1.clientY;
    return Math.sqrt(dx * dx + dy * dy);
  };

  const handleTouchStart = (e: React.TouchEvent) => {
    // Only handle pinch zoom (2 touches), ignore single touch to prevent shake
    if (e.touches.length === 2 && containerRef.current) {
      // Check if both touches are within the playground container
      const rect = containerRef.current.getBoundingClientRect();
      const touch1 = e.touches[0];
      const touch2 = e.touches[1];
      const isWithinBounds =
        touch1.clientX >= rect.left && touch1.clientX <= rect.right &&
        touch1.clientY >= rect.top && touch1.clientY <= rect.bottom &&
        touch2.clientX >= rect.left && touch2.clientX <= rect.right &&
        touch2.clientY >= rect.top && touch2.clientY <= rect.bottom;

      if (isWithinBounds) {
        e.preventDefault();
        e.stopPropagation();
        const distance = getPinchDistance(e.touches);
        if (distance > 10) { // Minimum distance threshold to prevent jitter
          setLastPinchDistance(distance);
        }
      }
    } else if (e.touches.length === 1) {
      // Single touch - don't prevent default to allow normal scrolling/panning
      // This prevents shake when touching and holding
      return;
    }
  };

  const handleTouchMove = (e: React.TouchEvent) => {
    // Only handle pinch zoom (2 touches)
    if (e.touches.length === 2 && lastPinchDistance !== null && containerRef.current) {
      e.preventDefault();
      e.stopPropagation();
      
      // Check if at least one touch is within the playground container (less strict for pinch out)
      const rect = containerRef.current.getBoundingClientRect();
      const touch1 = e.touches[0];
      const touch2 = e.touches[1];
      const touch1InBounds =
        touch1.clientX >= rect.left && touch1.clientX <= rect.right &&
        touch1.clientY >= rect.top && touch1.clientY <= rect.bottom;
      const touch2InBounds =
        touch2.clientX >= rect.left && touch2.clientX <= rect.right &&
        touch2.clientY >= rect.top && touch2.clientY <= rect.bottom;

      // Allow pinch if at least one touch is in bounds (fingers can move outside when pinching out)
      if (touch1InBounds || touch2InBounds) {
        const currentDistance = getPinchDistance(e.touches);
        if (currentDistance > 10 && lastPinchDistance > 10) { // Minimum threshold to prevent jitter
          const now = performance.now();
          // Throttle updates to max 60fps (16ms between updates)
          if (now - lastUpdateTimeRef.current < 16) {
            // Cancel previous animation frame if exists
            if (pinchUpdateRef.current !== null) {
              cancelAnimationFrame(pinchUpdateRef.current);
            }
            // Schedule update for next frame
            pinchUpdateRef.current = requestAnimationFrame(() => {
              fittedTransformRef.current = null; // Clear fitted state when pinch zooming
              const scaleChange = currentDistance / lastPinchDistance;
              const currentScale = transformRef.current.scale;
              const newScale = Math.min(Math.max(0.5, currentScale * scaleChange), 4);
              
              setTransform(prev => {
                // Only update if scale actually changed significantly to reduce jitter
                if (Math.abs(prev.scale - newScale) > 0.02) {
                  return { ...prev, scale: newScale };
                }
                return prev;
              });
              setLastPinchDistance(currentDistance);
              lastUpdateTimeRef.current = performance.now();
              pinchUpdateRef.current = null;
            });
            return;
          }
          
          // Immediate update if enough time has passed
          fittedTransformRef.current = null; // Clear fitted state when pinch zooming
          const scaleChange = currentDistance / lastPinchDistance;
          const currentScale = transformRef.current.scale;
          const newScale = Math.min(Math.max(0.5, currentScale * scaleChange), 4);
          
          setTransform(prev => {
            // Only update if scale actually changed significantly to reduce jitter
            if (Math.abs(prev.scale - newScale) > 0.02) {
              return { ...prev, scale: newScale };
            }
            return prev;
          });
          setLastPinchDistance(currentDistance);
          lastUpdateTimeRef.current = now;
        }
      } else {
        // Only reset if both touches are completely outside
        setLastPinchDistance(null);
      }
    } else if (e.touches.length === 1) {
      // Single touch - don't prevent default to allow normal scrolling
      return;
    }
  };

  const handleTouchEnd = (e: React.TouchEvent) => {
    if (e.touches.length < 2) {
      setLastPinchDistance(null);
      // Cancel any pending animation frame
      if (pinchUpdateRef.current !== null) {
        cancelAnimationFrame(pinchUpdateRef.current);
        pinchUpdateRef.current = null;
      }
    }
  };

  const handleMouseDown = (e: React.MouseEvent) => {
    // Left click on background drags if not in place mode
    if (!placeMode && !(e.target as HTMLElement).closest('.network-node') && !(e.target as HTMLElement).closest('.control-overlay')) {
      fittedTransformRef.current = null; // Clear fitted state when panning starts
      setIsPanning(true);
      setStartPan({ x: e.clientX - transform.x, y: e.clientY - transform.y });
    }
  };

  const handleNodeMouseDown = (e: React.MouseEvent, node: NetworkNode) => {
    // Allow dragging for all nodes when simulation is not running and not in place mode
    if (!isRunning && !placeMode) {
      e.stopPropagation();
      setDraggedNode(node.id);
      setDragInitialPos({ x: node.x, y: node.y });
      if (containerRef.current) {
        const rect = containerRef.current.getBoundingClientRect();
        setDragStart({
          x: e.clientX,
          y: e.clientY,
          nodeX: node.x,
          nodeY: node.y
        });
      }
    }
  };

  const handleMouseMove = (e: React.MouseEvent) => {
    if (containerRef.current) {
      const rect = containerRef.current.getBoundingClientRect();
      
      // Calculate mouse position in the transformed coordinate system.
      // This ensures the placement hint follows the cursor accurately,
      // accounting for both panning (transform.x, transform.y) and
      // zooming (transform.scale) with center transform-origin.
      const currentTransform = transformRef.current;
      
      // Get mouse position relative to container (accounting for top padding)
      const paddingTop = 56;
      const mouseX = e.clientX - rect.left;
      const mouseY = e.clientY - rect.top - paddingTop;
      
      // Get container center (accounting for padding)
      const centerX = rect.width / 2;
      const centerY = (rect.height - paddingTop) / 2;
      
      // Reverse the transform: translate(tx, ty) scale(s) with origin-center
      // 1. Remove translation (translation is in screen pixels)
      const translatedX = mouseX - currentTransform.x;
      const translatedY = mouseY - currentTransform.y;
      
      // 2. Convert to center-origin coordinates
      const relativeX = translatedX - centerX;
      const relativeY = translatedY - centerY;
      
      // 3. Apply inverse scale
      const unzoomedX = relativeX / currentTransform.scale;
      const unzoomedY = relativeY / currentTransform.scale;
      
      // 4. Convert back to top-left origin
      const canvasX = unzoomedX + centerX;
      const canvasY = unzoomedY + centerY;
      
      // Convert to percentage (0-100)
      // For hint positioning, use full height to match container (hint is positioned relative to full container)
      const virtualX = (canvasX / rect.width) * 100;
      const virtualY = (canvasY / rect.height) * 100;
      
      // Invert Y for the Mouse Position Display (Y=0 at bottom)
      const invertedY = 100 - virtualY; 

      const newMousePos = { x: virtualX, y: invertedY };
      setMousePos(newMousePos);
      
      if (onMousePosChange) {
        // Still send 0-100 coordinates to backend (for node placement, etc.)
        // Use the original calculation for backend consistency
        onMousePosChange({ x: virtualX, y: invertedY });
      }

      if (draggedNode) {
        // Direct update for smoother dragging without queued frames
        // Calculate new position for dragged node
        const deltaX = (e.clientX - dragStart.x) / currentTransform.scale / rect.width * 100;
        const deltaY = (e.clientY - dragStart.y) / currentTransform.scale / rect.height * 100;
        // Allow dragging anywhere on the canvas, even when zoomed or panned
        const newX = dragStart.nodeX + deltaX;
        const newY = dragStart.nodeY + deltaY;
        
        // Don't save to history during drag, only update position
        onUpdateNode(draggedNode, newX, newY, false);
      } else if (isPanning) {
        // Direct update for smoother panning
        setTransform(prev => ({
          ...prev,
          x: e.clientX - startPan.x,
          y: e.clientY - startPan.y
        }));
      }

      // Clear hover if mouse moves over background (not over a node)
      // This fixes potential stuck hover states
      if (isRunning) {
        const target = e.target as HTMLElement;
        // Check if target is within a network node
        const isNode = target.closest('.network-node');
        
        // If not over a node, and we have a hover state, clear it
        if (!isNode) {
          setHoveredApId(null);
          setHoveredClientId(null);
        }
      }
    }
  };

  const handleMouseUp = () => {
    // Prevent multiple calls
    if (isHandlingMouseUpRef.current) return;
    isHandlingMouseUpRef.current = true;

    // Use requestAnimationFrame to batch state updates and prevent infinite loops
    requestAnimationFrame(() => {
    // If we were dragging a node, save to history now (only once when drag ends)
    if (draggedNode && dragInitialPos) {
      const node = nodes.find(n => n.id === draggedNode);
      // Only save to history if the position actually changed
      if (node && (Math.abs(node.x - dragInitialPos.x) > 0.1 || Math.abs(node.y - dragInitialPos.y) > 0.1)) {
        // Save final position to history
        onUpdateNode(draggedNode, node.x, node.y, true);
      }
    }
    setIsPanning(false);
    setDraggedNode(null);
    setDragInitialPos(null);
      
      // Reset flag after a short delay to allow state updates to complete
      setTimeout(() => {
        isHandlingMouseUpRef.current = false;
      }, 0);
    });
  };

  const handleCanvasMouseLeave = () => {
    handleMouseUp();
    
    // Clear hover states when mouse leaves the canvas area to prevent stuck hovers
    if (isRunning) {
      setHoveredApId(null);
      setHoveredClientId(null);
    }
  };

  const handleCanvasClick = (e: React.MouseEvent) => {
    if (isPanning) return; 
    
    // Filter out clicks on UI elements
    if ((e.target as HTMLElement).closest('.network-node') || 
        (e.target as HTMLElement).closest('.control-overlay') ||
        (e.target as HTMLElement).closest('button')) return;

    // Clear hovers on background click
    if (isRunning) {
      setHoveredApId(null);
      setHoveredClientId(null);
    }
        
    if (placeMode && containerRef.current) {
      const rect = containerRef.current.getBoundingClientRect();
      const currentTransform = transformRef.current;
      
      // Use the exact same calculation as handleMouseMove for consistency
      // This ensures the node is placed exactly where the placement hint shows
      const paddingTop = 56;
      const mouseX = e.clientX - rect.left;
      const mouseY = e.clientY - rect.top - paddingTop;
      
      // Get container center (accounting for padding)
      const centerX = rect.width / 2;
      const centerY = (rect.height - paddingTop) / 2;
      
      // Reverse the transform: translate(tx, ty) scale(s) with origin-center
      // 1. Remove translation (translation is in screen pixels)
      const translatedX = mouseX - currentTransform.x;
      const translatedY = mouseY - currentTransform.y;
      
      // 2. Convert to center-origin coordinates
      const relativeX = translatedX - centerX;
      const relativeY = translatedY - centerY;
      
      // 3. Apply inverse scale
      const unzoomedX = relativeX / currentTransform.scale;
      const unzoomedY = relativeY / currentTransform.scale;
      
      // 4. Convert back to top-left origin
      const canvasX = unzoomedX + centerX;
      const canvasY = unzoomedY + centerY;
      
      // Convert to percentage (0-100) - use full height to match handleMouseMove
      const virtualX = (canvasX / rect.width) * 100;
      const virtualY = (canvasY / rect.height) * 100;
      
      // Place node at calculated position (use virtualY directly, not inverted)
      // Nodes use top: y% where y=0 is top, so we use virtualY as-is
      onAddNode(virtualX, virtualY);
    }
  };



  return (
    <div 
      ref={topLevelRef}
      className="absolute inset-0 w-full h-full"
      onMouseMove={handleMouseMove}
      onMouseDown={(e) => {
        handleMouseDown(e);
        // Only focus if there are nodes (session exists)
        if (nodes.length > 0 && onCanvasFocus) {
          onCanvasFocus();
        }
      }}
      onMouseUp={handleMouseUp}
      onMouseLeave={handleCanvasMouseLeave}
      onClick={(e) => {
        // Only focus canvas when clicked if there are nodes (session exists)
        // Don't focus on UI elements or when no session exists
        if (nodes.length > 0 &&
            !(e.target as HTMLElement).closest('.control-overlay') && 
            !(e.target as HTMLElement).closest('button') &&
            !(e.target as HTMLElement).closest('.network-node') &&
            onCanvasFocus) {
          onCanvasFocus();
        }
      }}
      style={{ touchAction: 'none' }}
    >
      {/* --- CANVAS --- */}
      <div 
        ref={containerRef}
        className={`absolute inset-0 w-full h-full overflow-visible ${placeMode ? 'cursor-crosshair' : isPanning ? 'cursor-grabbing' : 'cursor-grab'}`}
        style={{ paddingTop: '56px', touchAction: 'none' }}
        onClick={handleCanvasClick}
        onTouchStart={handleTouchStart}
        onTouchMove={handleTouchMove}
        onTouchEnd={handleTouchEnd}
      >
        <div style={{ display: showScale ? 'block' : 'none' }}>
          <CanvasRulers containerRef={containerRef} transform={transform} />
        </div>
        <BuildingOverlay isStarting={isStarting} isStopping={isStopping} />

        {/* Transformable Container */}
        <div 
          className="absolute origin-center"
            style={{ 
                transform: `translate(${transform.x}px, ${transform.y}px) scale(${transform.scale})`,
                width: '100%',
                height: '100%'
            }}
        >
            {/* Scaled Background Grid */}
            <div className="absolute -inset-[100%] w-[300%] h-[300%] opacity-30 pointer-events-none"
                style={{
                    backgroundImage: 'radial-gradient(var(--border) 1.5px, transparent 1.5px)',
                    backgroundSize: '24px 24px'
                }}
            ></div>
            
            <PlacementHint 
              placeMode={placeMode || undefined} 
              mousePos={mousePos} 
              scale={transform.scale} 
            />

            <ConnectionLines
              hoveredApId={hoveredApId}
              hoveredClientId={hoveredClientId}
              clientToApMap={clientToApMap}
              nodes={nodes}
              connectedClientIds={connectedClientIds}
              isRunning={isRunning}
              containerRef={containerRef}
            />

            {nodes.map((node, index) => (
              <NetworkNodeComponent
                key={`${activeSessionId || activeTemplateId || 'default'}-${node.id}-${index}`}
                node={node}
                isRunning={isRunning}
                placeMode={placeMode}
                onDelete={onDeleteNode}
                onMouseDown={handleNodeMouseDown}
                setHoveredApId={setHoveredApId}
                setHoveredClientId={setHoveredClientId}
                hoveredApId={hoveredApId}
                hoveredClientId={hoveredClientId}
                connectedClientIds={connectedClientIds}
                clientToApMap={clientToApMap}
              />
            ))}
        </div>
      </div>
    </div>
  );
};