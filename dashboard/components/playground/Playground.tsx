import React, { useRef, useState, useEffect } from 'react';
import { NetworkNode, NodeType, Template, Session } from '@/lib/types';
import { SimulationTime } from '@/components/playground/SimulationTime';
import { TemplateSelectionPanel } from '@/components/playground/TemplateSelectionPanel';
import { Canvas } from '@/components/playground/Canvas';
import { ZoomControls } from '@/components/playground/ZoomControls';
import { MouseCoordinates } from '@/components/playground/MouseCoordinates';
import { ToolSelectionToolbar } from '@/components/playground/ToolSelectionToolbar';
import { UndoRedoReset } from '@/components/playground/UndoRedoReset';
import { ThemeSwitch } from '@/components/ThemeSwitch';
import { ControllerToggle } from '@/components/playground/ControllerToggle';
import { GraphViewOverlay } from '@/components/GraphViewModal';

interface PlaygroundProps {
  nodes: NetworkNode[];
  onAddNode: (x: number, y: number) => void;
  onDeleteNode: (id: string) => void;
  onUpdateNode: (id: string, x: number, y: number, isDragEnd?: boolean) => void;
  placeMode: NodeType | null;
  isRunning: boolean;
  isBuilding: boolean;
  isStarting: boolean;
  isStopping: boolean;
  timeOfDay: number;
  activeSessionId?: string | null;
  activeSessionName?: string | null;
  showScale?: boolean;
  onToggleScale?: () => void;
  onDeleteTemplate?: (templateId: string) => void;
  onRenameTemplate?: (templateId: string, newName: string) => void;
  showTemplateSelection?: boolean;
  onSelectTemplate?: (templateId: string) => void;
  onCloseTemplateSelection?: () => void;
  savedTemplates?: Template[];
  allTemplates?: Template[];
  sessions?: Session[];
  onCanvasFocus?: () => void;
  onCanvasBlur?: () => void;
  onUndo?: () => void;
  onRedo?: () => void;
  onReset?: () => void;
  canUndo?: boolean;
  canRedo?: boolean;
  canReset?: boolean;
  isDisabled?: boolean;
  onSelectTool?: (tool: NodeType | null) => void;
  activeTemplateId?: string | null;
  hoveredApId: number | null;
  setHoveredApId: React.Dispatch<React.SetStateAction<number | null>>;
  hoveredClientId: number | null;
  setHoveredClientId: React.Dispatch<React.SetStateAction<number | null>>;
  connectedClientIds: Set<number>;
  clientToApMap: Map<number, number>;
  isControllerEnabled?: boolean;
  onToggleController?: () => void;
  onStressTest?: (testType: 'dfs' | 'throughput' | 'interference') => void;
}

export const Playground: React.FC<PlaygroundProps> = ({
  nodes,
  onAddNode,
  onDeleteNode,
  onUpdateNode,
  placeMode,
  isRunning,
  isBuilding,
  isStarting,
  isStopping,
  timeOfDay,
  activeSessionId,
  activeSessionName,
  showScale = true,
  onToggleScale,
  onDeleteTemplate,
  onRenameTemplate,
  showTemplateSelection,
  onSelectTemplate,
  onCloseTemplateSelection,
  savedTemplates = [],
  allTemplates = [],
  sessions = [],
  onCanvasFocus,
  onCanvasBlur,
  onUndo,
  onRedo,
  onReset,
  canUndo = false,
  canRedo = false,
  canReset = false,
  isDisabled = false,
  onSelectTool,
  activeTemplateId,
  hoveredApId,
  setHoveredApId,
  hoveredClientId,
  setHoveredClientId,
  connectedClientIds,
  clientToApMap,
  isControllerEnabled = false,
  onToggleController,
  onStressTest
}) => {
  const [mousePos, setMousePos] = useState({ x: 0, y: 0 });
  const [isMounted, setIsMounted] = useState(false);
  const zoomControlsRef = useRef<{ zoomIn: () => void; zoomOut: () => void; resetView: () => void; fitToView: () => void; } | null>(null);
  const [isFitted, setIsFitted] = useState(false);
  const [showGraphView, setShowGraphView] = useState(false);
  
  // Prevent hydration mismatch by only rendering session header after mount
  useEffect(() => {
    setIsMounted(true);
  }, []);

  return (
    <div 
      className="relative flex-1 bg-secondary overflow-visible select-none z-0"
    >
      
      {/* --- UI OVERLAYS (Fixed Position) --- */}

      {/* Template Selection Panel */}
      <TemplateSelectionPanel
        showTemplateSelection={showTemplateSelection || false}
        onSelectTemplate={onSelectTemplate}
        onCloseTemplateSelection={onCloseTemplateSelection}
        savedTemplates={savedTemplates}
        allTemplates={allTemplates}
        onDeleteTemplate={onDeleteTemplate}
        onRenameTemplate={onRenameTemplate}
        sessions={sessions}
      />

      {/* Top Bar: Undo/Redo/Reset and Session Heading */}
      <div className="absolute top-0 left-0 right-0 z-30 control-overlay flex items-center justify-between px-5 bg-card shadow-sm border-b border-border py-2.5">
        {/* Left: Undo/Redo/Reset Buttons */}
        <div className="flex items-center flex-shrink-0">
          {onUndo && onRedo && onReset && (
            <UndoRedoReset
              onUndo={onUndo}
              onRedo={onRedo}
              onReset={onReset}
              canUndo={canUndo}
              canRedo={canRedo}
              canReset={canReset}
              isDisabled={isDisabled}
            />
          )}
        </div>
        
        {/* Center: Session Name Heading - Only render after mount to prevent hydration mismatch */}
        {isMounted && activeSessionId && activeSessionName && (
          <div className="absolute left-1/2 -translate-x-1/2 z-10 top-0">
            <h2 className="text-lg font-semibold text-foreground whitespace-nowrap bg-card/90 px-3 py-2 rounded-md">
              {activeSessionName}
            </h2>
          </div>
        )}
        
        {/* Right: Theme Switch */}
        <div className="flex-shrink-0">
          <ThemeSwitch />
        </div>
      </div>

      {/* Tool Selection Toolbar */}
      {onSelectTool && (
        <ToolSelectionToolbar
          placeMode={placeMode}
          onSelectTool={onSelectTool}
          isDisabled={isDisabled}
        />
      )}

      {/* Timer Widget - Below buttons */}
      <div className="absolute top-17 left-4 z-30 control-overlay flex items-center gap-2 ml-7">
        <SimulationTime timeOfDay={timeOfDay} />
        {/* Recording indicator when simulation is running */}
        {isRunning && (
          <div className="relative">
            <div className="w-3 h-3 bg-red-500 rounded-full animate-pulse"></div>
            <div className="absolute inset-0 w-3 h-3 bg-red-500 rounded-full animate-ping"></div>
          </div>
        )}
      </div>

      {/* Controller Toggle - Right side aligned with time widget, always visible but disabled when not running */}
      {onToggleController && (
        <div className="absolute top-17 right-4 z-30 control-overlay">
          <ControllerToggle
            isRLEnabled={isControllerEnabled}
            onRLToggle={onToggleController}
            disabled={!isRunning}
            onSelectTest={(testType) => {
              console.log('Selected stress test:', testType);
              if (onStressTest) {
                onStressTest(testType);
              }
            }}
            onOpenGraphView={() => setShowGraphView(true)}
          />
        </div>
      )}

      {/* Bottom Right: View Controls & Coordinates */}
      <div className="absolute bottom-20 sm:bottom-4 right-4 z-30 flex items-center gap-3 control-overlay">
        <ZoomControls
          onZoomIn={() => zoomControlsRef.current?.zoomIn()}
          onZoomOut={() => zoomControlsRef.current?.zoomOut()}
          onFitToView={() => zoomControlsRef.current?.fitToView()}
          isFitToViewDisabled={isFitted}
          showScale={showScale}
          onToggleScale={onToggleScale}
        />
        <MouseCoordinates x={mousePos.x} y={mousePos.y} />
      </div>

      {/* Canvas Component */}
      <Canvas
        nodes={nodes}
        onAddNode={onAddNode}
        onDeleteNode={onDeleteNode}
        onUpdateNode={onUpdateNode}
        placeMode={placeMode}
        isRunning={isRunning}
        isStarting={isStarting}
        isStopping={isStopping}
        activeSessionId={activeSessionId}
        showScale={showScale}
        activeTemplateId={activeTemplateId}
        onCanvasFocus={onCanvasFocus}
        onCanvasBlur={onCanvasBlur}
        onMousePosChange={setMousePos}
        onFittedChange={setIsFitted}
        onZoomControlsReady={(controls) => {
          zoomControlsRef.current = controls;
        }}
        hoveredApId={hoveredApId}
        setHoveredApId={setHoveredApId}
        hoveredClientId={hoveredClientId}
        setHoveredClientId={setHoveredClientId}
        connectedClientIds={connectedClientIds}
        clientToApMap={clientToApMap}
      />

      {/* Graph View Overlay - Shows real-time network topology over the canvas */}
      <GraphViewOverlay
        isOpen={showGraphView}
        onClose={() => setShowGraphView(false)}
        nodes={nodes}
      />
    </div>
  );
};
