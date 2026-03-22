"use client";

import { useContext, useEffect, useState } from "react";
import { DataModeContext } from "@/lib/contexts";

export function DataModeToggle() {
  const dataModeContext = useContext(DataModeContext);
  const [isMounted, setIsMounted] = useState(false);

  // Prevent hydration mismatch by only rendering after mount
  useEffect(() => {
    setIsMounted(true);
  }, []);

  if (!dataModeContext || !isMounted) {
    // Return a placeholder with same structure to prevent layout shift
    return (
      <div className="relative w-full bg-slate-800/50 rounded-lg p-1 h-10">
        <div className="grid grid-cols-2 gap-1 h-full relative">
          <button disabled className="relative z-10 text-sm font-medium text-slate-500 transition-colors">
            Simulation
          </button>
          <button disabled className="relative z-10 text-sm font-medium text-slate-500 transition-colors">
            Replay
          </button>
        </div>
      </div>
    );
  }

  const { mode, setMode } = dataModeContext;
  const isSimulation = mode === 'simulation';

  return (
    <div className="relative w-full bg-slate-800/50 rounded-lg p-1 h-10">
      <div className="grid grid-cols-2 gap-1 h-full relative">
        {/* Animated background indicator */}
        <div
          className={`absolute top-1 bottom-1 left-1 w-[calc(50%-0.25rem)] rounded-md transition-all duration-300 ease-in-out ${
            isSimulation
              ? 'bg-blue-500 translate-x-0'
              : 'bg-emerald-500 translate-x-[calc(100%+0.25rem)]'
          }`}
        />

        {/* Simulation option */}
        <button
          onClick={() => setMode('simulation')}
          className={`relative z-10 text-sm font-medium transition-colors ${
            isSimulation
              ? 'text-white'
              : 'text-slate-400 hover:text-slate-300'
          }`}
        >
          Simulation
        </button>

        {/* Replay option */}
        <button
          onClick={() => setMode('replay')}
          className={`relative z-10 text-sm font-medium transition-colors ${
            !isSimulation
              ? 'text-white'
              : 'text-slate-400 hover:text-slate-300'
          }`}
        >
          Replay
        </button>
      </div>
    </div>
  );
}

