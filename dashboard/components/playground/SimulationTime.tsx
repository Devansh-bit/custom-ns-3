import React, { useState, useEffect } from 'react';
import { Sun, Moon } from 'lucide-react';

interface SimulationTimeProps {
  timeOfDay: number;
}

export const SimulationTime: React.FC<SimulationTimeProps> = ({ timeOfDay }) => {
  const [isMounted, setIsMounted] = useState(false);

  // Prevent hydration mismatch by only showing actual time after mount
  useEffect(() => {
    setIsMounted(true);
  }, []);

  return (
    <div className="flex items-center gap-2 bg-card rounded-lg px-3 py-2 shadow-sm border border-border">
        <span className="text-xs font-semibold text-foreground uppercase tracking-wide">TIME</span>
        <div className="w-px h-4 bg-border"></div>
        {(() => {
          // Use default value during SSR and initial render to prevent hydration mismatch
          const displayTimeOfDay = isMounted ? timeOfDay : 25;
          const isNight = displayTimeOfDay > 75 || displayTimeOfDay < 25;
          return isNight ? (
            <Moon size={18} className="text-muted-foreground" />
          ) : (
            <Sun size={18} className="text-yellow-500" />
          );
        })()}
        <span className="font-medium text-foreground text-sm">
          {isMounted 
            ? Math.floor((timeOfDay / 100) * 24).toString().padStart(2, '0') + ':00'
            : '06:00'
          }
        </span>
    </div>
  );
};