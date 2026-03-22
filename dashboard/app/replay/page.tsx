"use client";

import { useState } from "react";
import { SidebarLeft } from "@/components/playground/SidebarLeft";
import { SidebarRight } from "@/components/playground/SidebarRight";
import { ControlPanel } from "@/components/playground/ControlPanel";
import { SimulationPanel } from '@/components/replay/SimulationPanel';
import { NetworkNode, Session, KPIMetrics, OptimizerType } from '@/lib/types';
import { LogEntry } from '@/lib/api-playground';

export default function ReplayPage() {
  // Minimal state for sidebars
  const [sessions, setSessions] = useState<Session[]>([]);
  const [activeSessionId, setActiveSessionId] = useState<string | null>(null);
  const [nodes, setNodes] = useState<NetworkNode[]>([]);
  const [templateStates] = useState<Record<string, NetworkNode[]>>({});
  const [simulationState] = useState<'unsaved' | 'building' | 'built' | 'starting' | 'running' | 'stopping'>('unsaved');
  const [logs] = useState<LogEntry[]>([]);
  const [kpiMetrics] = useState<KPIMetrics>({
    throughput: 0,
    retryRate: 0,
    packetretryRate: 0,
    latency: 0,
    churn: 0,
    score: 0,
  });
  const [optimizer, setOptimizer] = useState<OptimizerType>('Bayesian Optimization');

  // Handler stubs
  const handleShowTemplateSelection = () => {
    console.log("Create session");
  };

  const handleLoadSession = (session: Session) => {
    setActiveSessionId(session.id);
    console.log("Load session:", session);
  };

  const handleDeleteSession = (sessionId: string) => {
    setSessions((prev) => prev.filter((s) => s.id !== sessionId));
    if (activeSessionId === sessionId) {
      setActiveSessionId(null);
    }
    console.log("Delete session:", sessionId);
  };

  const handleRenameSession = (sessionId: string, newName: string) => {
    setSessions((prev) =>
      prev.map((s) => (s.id === sessionId ? { ...s, name: newName } : s))
    );
    console.log("Rename session:", sessionId, newName);
  };

  const handlePrevious = () => {
    console.log("Previous");
  };

  const handlePause = () => {
    console.log("Pause/Play");
  };

  const handleNext = () => {
    console.log("Next");
  };

  const handleReset = () => {
    console.log("Reset Simulation");
  };

  return (
    <div
      className="flex h-screen bg-background font-sans text-foreground overflow-hidden relative"
      style={{ touchAction: "pan-x pan-y" }}
    >
      {/* Left Sidebar (Sessions) - Fixed Width, full screen height */}
      <SidebarLeft
        onCreateSession={handleShowTemplateSelection}
        onLoadSession={handleLoadSession}
        onDeleteSession={handleDeleteSession}
        onRenameSession={handleRenameSession}
        activeSessionId={activeSessionId}
        sessions={sessions}
        disabled={false}
        nodes={nodes}
        templateStates={templateStates}
        simulationState={
          simulationState === 'unsaved' || simulationState === 'built' || simulationState === 'starting' || simulationState === 'stopping'
            ? 'idle'
            : (simulationState === 'building' || simulationState === 'running' ? simulationState : 'idle')
        }
      />

      <div className="flex flex-col flex-1 overflow-hidden relative">
        <div className="flex flex-1 overflow-hidden relative">
          {/* Main Content Area */}
          <div className="flex-1 flex flex-col relative z-0 min-w-0">
            <div className="flex-1 overflow-auto">
              <div className="p-6">
                <h1 className="text-foreground text-2xl font-bold">Replay Page</h1>
              </div>
            </div>

            {/* Controls Footer - Disabled */}
            <ControlPanel
              isBuilding={false}
              isRunning={false}
              inRunTransition={false}
              isEmpty={true}
              isBuilt={false}
              hasChanges={false}
              isNavigatingAway={false}
              showScale={true}
              showTemplateSelection={false}
              onBuild={() => {}}
              onRun={() => {}}
              onStop={() => {}}
              onToggleScale={() => {}}
              onNavigateAway={() => {}}
            />
          </div>

          {/* Right Sidebar (Dashboard & Logs) - Fixed Width */}
          <SidebarRight
            logs={logs}
            kpiMetrics={kpiMetrics}
            optimizer={optimizer}
            setOptimizer={setOptimizer}
            simulationState={simulationState}
          />
        </div>
      </div>

      {/* Simulation Panel at bottom center */}
      <SimulationPanel
        onPrevious={handlePrevious}
        onPause={handlePause}
        onNext={handleNext}
        onReset={handleReset}
        isPaused={false}
      />
    </div>
  );
}
