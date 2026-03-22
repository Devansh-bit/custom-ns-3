import React, { useRef, useEffect, useState, useCallback } from 'react';

import { AlertCircle, CheckCircle, Info, XCircle, ChevronLeft, ChevronRight } from 'lucide-react';
import { OptimizerType } from '@/types';
import { LogEntry, NetworkMetrics } from '@/lib/api-playground';

interface SidebarRightProps {
  logs: LogEntry[];
  optimizer: OptimizerType;
  setOptimizer: (opt: OptimizerType) => void;
  simulationState?: 'unsaved' | 'building' | 'built' | 'starting' | 'running' | 'stopping';
  networkMetrics: NetworkMetrics;
}

export const SidebarRight: React.FC<SidebarRightProps> = ({
  logs,
  optimizer,
  setOptimizer,
  simulationState = 'unsaved',
  networkMetrics
}) => {
  const sidebarRef = useRef<HTMLDivElement>(null);
  const [showOptimizerMenu, setShowOptimizerMenu] = useState(false);
  const [isOpen, setIsOpen] = useState(true);
  const [openedViaIcon, setOpenedViaIcon] = useState<'logs' | null>(null);
  const [sidebarWidth, setSidebarWidth] = useState(310); // Default width
  const [isDragging, setIsDragging] = useState(false);
  const [isMounted, setIsMounted] = useState(false);
  const optimizers: OptimizerType[] = ['Bayesian Optimization', 'Reinforcement Learning', 'Genetic Algorithm'];
  
  // Prevent hydration mismatch by only showing actual metrics after mount
  useEffect(() => {
    setIsMounted(true);
  }, []);

  // Auto-collapse sidebar when simulation is not running
  useEffect(() => {
    if (simulationState !== 'running') {
      setIsOpen(false);
    }
  }, [simulationState]);


  // Handle drag to resize sidebar
  const handleMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setIsDragging(true);
  }, []);

  useEffect(() => {
    if (!isDragging) return;

    let animationFrameId: number;
    
    const handleMouseMove = (e: MouseEvent) => {
      if (animationFrameId) {
        cancelAnimationFrame(animationFrameId);
      }
      
      animationFrameId = requestAnimationFrame(() => {
        if (!sidebarRef.current) return;
        
        const newWidth = window.innerWidth - e.clientX;
        
        // Constrain width between 200px and 50% of viewport
        const maxWidth = window.innerWidth * 0.5;
        const constrainedWidth = Math.min(Math.max(200, newWidth), maxWidth);
        setSidebarWidth(constrainedWidth);
      });
    };

    const handleMouseUp = () => {
      if (animationFrameId) {
        cancelAnimationFrame(animationFrameId);
      }
      setIsDragging(false);
    };

    // Use capture phase for better event handling
    document.addEventListener('mousemove', handleMouseMove, { passive: true });
    document.addEventListener('mouseup', handleMouseUp, { passive: true });
    
    // Prevent text selection while dragging
    document.body.style.userSelect = 'none';
    document.body.style.cursor = 'col-resize';

    return () => {
      if (animationFrameId) {
        cancelAnimationFrame(animationFrameId);
      }
      document.removeEventListener('mousemove', handleMouseMove);
      document.removeEventListener('mouseup', handleMouseUp);
      document.body.style.userSelect = '';
      document.body.style.cursor = '';
    };
  }, [isDragging]);

  const getIcon = (type: string) => {
    switch (type) {
      case 'error': return <XCircle size={14} className="text-red-500" />;
      case 'warning': return <AlertCircle size={14} className="text-amber-500" />;
      case 'success': return <CheckCircle size={14} className="text-green-500" />;
      default: return <Info size={14} className="text-blue-400" />;
    }
  };

  return (
    <div 
      ref={sidebarRef}
      className={`${isOpen ? '' : 'w-12'} bg-card border-l border-border shadow-sm relative flex flex-col shrink-0 z-10 h-full`}
      style={isOpen ? { 
        width: `${sidebarWidth}px`,
        transition: isDragging ? 'none' : 'width 0.2s ease-in-out',
        height: '100%'
      } : { transition: 'width 0.3s ease-in-out', height: '100%' }}
    >
      {/* --- TOP HEADER --- */}
      <div className={`p-2 bg-muted/50 border-b border-border flex items-center ${isOpen ? 'justify-between' : 'justify-center'}`}>
         <button 
            onClick={() => {
              // Only allow opening/closing when simulation is running
              if (simulationState === 'running') {
              setIsOpen(!isOpen);
              if (!isOpen) {
                setOpenedViaIcon(null); // Reset when opening via toggle button
              }
              }
            }}
            disabled={simulationState !== 'running'}
            className={`p-1.5 rounded-lg transition-colors ${
              simulationState === 'running' 
                ? 'hover:bg-accent text-muted-foreground hover:text-foreground cursor-pointer' 
                : 'text-muted-foreground/70 opacity-50'
            }`}
            title={simulationState !== 'running' ? 'Start simulation to open sidebar' : isOpen ? 'Collapse sidebar' : 'Expand sidebar'}
          >
            {isOpen ? <ChevronRight size={18} /> : <ChevronLeft size={18} />}
         </button>
         {isOpen && (
            <div className="flex items-center gap-2 flex-1 justify-center">
              {/* <Terminal className="text-slate-500" size={14} /> */}
              <h2 className="font-bold text-foreground text-sm uppercase tracking-wide ">
                Live Results
              </h2>
            </div>
        )}
        
        {/* Drag Handle with Notch Style */}
        {isOpen && (
          <div
            onMouseDown={handleMouseDown}
            // Positioning and size for the drag area
            className="absolute left-0 top-0 bottom-0 cursor-col-resize z-20 group"
            style={{ 
              width: '10px', // Make the hit area wider for easier dragging
              transform: 'translateX(-5px)', // Center the 10px wide hit area on the 1px boundary
            }}
          >
            {/* The central 'Notch' or 'Pill' element */}
            <div 
              className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 
                         w-2.5 h-7 bg-border rounded-full flex items-center justify-center 
                         group-hover:bg-primary transition-colors"
            >
              {/* Dots inside the notch for visual cue */}
              <div className="space-y-[2px] group-hover:[&>span]:bg-primary-foreground">
                <span className="block w-[2px] h-[2px] bg-primary-foreground/70 rounded-full transition-opacity"></span>
                <span className="block w-[2px] h-[2px] bg-primary-foreground/70 rounded-full transition-opacity"></span>
                <span className="block w-[2px] h-[2px] bg-primary-foreground/70 rounded-full transition-opacity"></span>
              </div>
            </div>
          </div>
        )}
        
        {/* Optimizer Dropdown  */}
        {/*
         {isOpen && (
            <div className="relative flex-1">
                <button 
                    onClick={() => setShowOptimizerMenu(!showOptimizerMenu)}
                    className="w-full flex items-center justify-between bg-slate-800/50 px-3 py-2 rounded-lg border border-slate-700/50 shadow-sm hover:border-blue-500 transition-colors text-white font-medium text-sm"
                >
                    <span className="truncate text-xs">{optimizer}</span>
                    <ChevronDown size={14} className="text-[#90A1B9] shrink-0 ml-2" />
                </button>
                {showOptimizerMenu && (
                    <div className="absolute top-full left-0 mt-1 w-full bg-[#0F172B] rounded-lg shadow-xl border border-slate-700/50 overflow-hidden animate-fade-in z-50">
                        {optimizers.map(opt => (
                            <button
                                key={opt}
                                onClick={() => { setOptimizer(opt); setShowOptimizerMenu(false); }}
                                className={`w-full text-left px-3 py-2 text-sm hover:bg-slate-700/50 transition-colors ${optimizer === opt ? 'text-blue-400 font-semibold bg-blue-500/10' : 'text-[#90A1B9]'}`}
                            >
                                {opt}
                            </button>
                        ))}
                    </div>
                )}
            </div>
         )}
         */}
      </div>
      {isOpen ? (
        <div className="flex-1 flex flex-col overflow-hidden">
          {/* --- METRICS SECTION (at top) --- */}
          <div className="p-3 border-b border-border">
            <h3 className="text-xs font-semibold text-muted-foreground uppercase tracking-wide mb-2">Metrics</h3>
            <div className="bg-background rounded-xl border border-border shadow-sm overflow-hidden">
              <div className="p-3 grid grid-cols-2 gap-3">
                <div className="flex flex-col">
                  <span className="text-[10px] text-muted-foreground uppercase font-semibold">Throughput</span>
                  <div className="flex items-baseline gap-1">
                    <span className="text-base font-bold text-foreground">
                      {simulationState === 'running' && isMounted && networkMetrics.throughput !== null
                        ? networkMetrics.throughput
                        : '--'}
                    </span>
                    {simulationState === 'running' && networkMetrics.throughput !== null && (
                      <span className="text-[10px] text-muted-foreground">Mbps</span>
                    )}
                  </div>
                </div>
                <div className="flex flex-col">
                  <span className="text-[10px] text-muted-foreground uppercase font-semibold">Latency</span>
                  <div className="flex items-baseline gap-1">
                    <span className="text-base font-bold text-foreground">
                      {simulationState === 'running' && isMounted && networkMetrics.latency !== null
                        ? networkMetrics.latency
                        : '--'}
                    </span>
                    {simulationState === 'running' && networkMetrics.latency !== null && (
                      <span className="text-[10px] text-muted-foreground">ms</span>
                    )}
                  </div>
                </div>
                <div className="flex flex-col">
                  <span className="text-[10px] text-muted-foreground uppercase font-semibold">Loss Rate</span>
                  <div className="flex items-baseline gap-1">
                    <span className={`text-base font-bold ${
                      simulationState === 'running' && isMounted && networkMetrics.loss_rate !== null && networkMetrics.loss_rate > 5
                        ? 'text-destructive'
                        : simulationState === 'running' && networkMetrics.loss_rate !== null
                        ? 'text-green-500'
                        : 'text-foreground'
                    }`}>
                      {simulationState === 'running' && isMounted && networkMetrics.loss_rate !== null
                        ? `${networkMetrics.loss_rate}%`
                        : '--'}
                    </span>
                  </div>
                </div>
                <div className="flex flex-col">
                  <span className="text-[10px] text-muted-foreground uppercase font-semibold">Jitter</span>
                  <div className="flex items-baseline gap-1">
                    <span className="text-base font-bold text-foreground">
                      {simulationState === 'running' && isMounted && networkMetrics.jitter !== null
                        ? networkMetrics.jitter
                        : '--'}
                    </span>
                    {simulationState === 'running' && networkMetrics.jitter !== null && (
                      <span className="text-[10px] text-muted-foreground">ms</span>
                    )}
                  </div>
                </div>
              </div>
            </div>
          </div>

          {/* --- LOGS SECTION --- */}
          <div className="flex-1 flex flex-col overflow-hidden">
            <div className="px-3 pt-3 pb-2">
              <h3 className="text-xs font-semibold text-muted-foreground uppercase tracking-wide">Logs</h3>
            </div>
            <div className="flex-1 overflow-y-auto p-2 space-y-2 bg-background log-scroll">
            {logs.length === 0 ? (
              <div className="flex flex-col items-center justify-center h-full text-muted-foreground text-xs italic">
                <span>Waiting for build...</span>
              </div>
            ) : (
              logs.map((log) => (
                <div key={log.id} className="flex gap-2 text-xs animate-fade-in-up group hover:bg-accent p-1.5 rounded -mx-1 transition-colors border-l-2 border-transparent hover:border-border">
                  <span className="text-muted-foreground font-mono shrink-0 w-12 opacity-70">{new Date(log.timestamp).toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' })}</span>
                  <div className="flex flex-col gap-0.5 min-w-0">
                    <div className="flex items-center gap-1 font-semibold">
                      {getIcon(log.type)}
                      <span className={
                        log.type === 'error' ? 'text-destructive' :
                        log.type === 'warning' ? 'text-yellow-500' :
                        log.type === 'success' ? 'text-green-500' : 'text-foreground'
                      }>{log.source}</span>
                    </div>
                    <p className="text-muted-foreground leading-relaxed font-mono text-[11px] break-words">{log.message}</p>
                  </div>
                </div>
              ))
            )}
            </div>
          </div>
        </div>
      ) : (
        // Collapsed View
        <div className="flex flex-col items-center gap-4 mt-4 px-2">
        </div>
      )}
    </div>
  );
};
