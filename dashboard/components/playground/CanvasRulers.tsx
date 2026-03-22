import React, { useRef, useEffect, useState, useCallback } from 'react';

interface CanvasRulersProps {
  containerRef: React.RefObject<HTMLDivElement | null>;
  transform: { x: number; y: number; scale: number };
}

export const CanvasRulers: React.FC<CanvasRulersProps> = ({ containerRef, transform }) => {
  const [xAxisTicks, setXAxisTicks] = useState<{ major: number[]; minor: number[] }>({ major: [], minor: [] });
  const [yAxisTicks, setYAxisTicks] = useState<{ major: number[]; minor: number[] }>({ major: [], minor: [] });

  // Function to calculate X-axis ticks
  const calculateXTicks = useCallback(() => {
    if (!containerRef.current) return;
    
    // Skip calculation if component is hidden (optimization)
    const isVisible = containerRef.current.offsetParent !== null;
    if (!isVisible) return;

    const width = containerRef.current.clientWidth;
    const scale = transform.scale;
    const panX = transform.x;
    const center = width / 2;
    
    // Calculate visible range in canvas coordinates (0-10)
    const leftCanvas = ((0 - panX - center) / scale + center) / width * 10;
    const rightCanvas = ((width - panX - center) / scale + center) / width * 10;
    const min = Math.min(leftCanvas, rightCanvas);
    const max = Math.max(leftCanvas, rightCanvas);
    const range = max - min;
    
    // Calculate nice interval for 10 ticks
    const interval = range / 10;
    const magnitude = Math.pow(10, Math.floor(Math.log10(interval)));
    const normalized = interval / magnitude;
    const niceInterval = (normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 5 ? 5 : 10) * magnitude;
    
    // Generate ticks
    const start = Math.floor(min / niceInterval) * niceInterval;
    const majorTicks: number[] = [];
    const minorTicks: number[] = [];
    
    for (let val = start; val <= max + niceInterval; val += niceInterval) {
      majorTicks.push(val);
      if (majorTicks.length > 1) {
        const prevVal = majorTicks[majorTicks.length - 2];
        const interval = val - prevVal;
        // Add 3 minor ticks at 0.25, 0.5, and 0.75 positions
        minorTicks.push(prevVal + interval * 0.25);
        minorTicks.push(prevVal + interval * 0.5);
        minorTicks.push(prevVal + interval * 0.75);
      }
    }
    
    setXAxisTicks({ major: majorTicks, minor: minorTicks });
  }, [containerRef, transform]);

  // Function to calculate Y-axis ticks
  const calculateYTicks = useCallback(() => {
    if (!containerRef.current) return;
    
    // Skip calculation if component is hidden (optimization)
    const isVisible = containerRef.current.offsetParent !== null;
    if (!isVisible) return;

    const height = containerRef.current.clientHeight;
    const rulerTopOffset = 60; // top-[60px] (below topbar)
    const rulerBottomOffset = 24; // bottom-6 = 24px
    const rulerHeight = height - rulerTopOffset - rulerBottomOffset;
    const scale = transform.scale;
    const panY = transform.y;
    const center = height / 2;
    
    // Calculate visible range in canvas coordinates (0-10)
    // Use rulerHeight to match the actual visible area of the Y-axis ruler
    // Note: Y-axis is inverted (0 at bottom, 10 at top)
    const topCanvas = ((rulerTopOffset - panY - center) / scale + center) / height * 10;
    const bottomCanvas = ((height - rulerBottomOffset - panY - center) / scale + center) / height * 10;
    // Invert Y values (since canvas Y=0 is at top, but we want 0 at bottom)
    const min = Math.min(10 - topCanvas, 10 - bottomCanvas);
    const max = Math.max(10 - topCanvas, 10 - bottomCanvas);
    const range = max - min;
    
    // Calculate nice interval for 10 ticks (same as X-axis)
    const interval = range / 10;
    const magnitude = Math.pow(10, Math.floor(Math.log10(interval)));
    const normalized = interval / magnitude;
    const niceInterval = (normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 5 ? 5 : 10) * magnitude;
    
    // Generate ticks
    const start = Math.floor(min / niceInterval) * niceInterval;
    const majorTicks: number[] = [];
    const minorTicks: number[] = [];
    
    for (let val = start; val <= max + niceInterval; val += niceInterval) {
      majorTicks.push(val);
      if (majorTicks.length > 1) {
        const prevVal = majorTicks[majorTicks.length - 2];
        const interval = val - prevVal;
        // Add 3 minor ticks at 0.25, 0.5, and 0.75 positions
        minorTicks.push(prevVal + interval * 0.25);
        minorTicks.push(prevVal + interval * 0.5);
        minorTicks.push(prevVal + interval * 0.75);
      }
    }
    
    setYAxisTicks({ major: majorTicks, minor: minorTicks });
  }, [containerRef, transform]);

  // Recalculate when transform changes
  useEffect(() => {
    calculateXTicks();
    calculateYTicks();
  }, [calculateXTicks, calculateYTicks]);

  // Listen to container resize events
  useEffect(() => {
    if (!containerRef.current) return;

    const resizeObserver = new ResizeObserver(() => {
      calculateXTicks();
      calculateYTicks();
    });

    resizeObserver.observe(containerRef.current);

    return () => {
      resizeObserver.disconnect();
    };
  }, [calculateXTicks, calculateYTicks]);

  if (!containerRef.current) return null;

  const width = containerRef.current.clientWidth;
  const scale = transform.scale;
  const panX = transform.x;
  const center = width / 2;

  return (
    <>
      {/* Fixed X-Axis Ruler (Bottom) */}
      <div className="absolute bottom-1 left-0 right-0 h-6 z-20 pointer-events-none">
        <div className="relative h-full">
          {xAxisTicks.major.map((val) => {
            const screenX = ((val / 10) * width - center) * scale + panX + center;
            if (screenX < -50 || screenX > width + 50) return null;
            return (
              <div key={val} className="absolute" style={{ left: `${screenX}px`, transform: 'translateX(-50%)' }}>
                <div className="h-3 w-px bg-foreground"></div>
                <div className="text-[10px] text-foreground font-mono whitespace-nowrap text-center" style={{ transform: 'translateX(-50%)', marginTop: '2px' }}>
                  {val.toFixed(val % 1 === 0 ? 0 : 1)}
                </div>
              </div>
            );
          })}
          {xAxisTicks.minor.map((val) => {
            const screenX = ((val / 10) * width - center) * scale + panX + center;
            if (screenX < -10 || screenX > width + 10) return null;
            // Check if this is the 0.5 tick (midpoint between two major ticks)
            const isMidpoint = xAxisTicks.major.some((major, idx) => {
              if (idx === 0) return false;
              const prevMajor = xAxisTicks.major[idx - 1];
              const midpoint = (prevMajor + major) / 2;
              return Math.abs(val - midpoint) < 0.0001; // Small epsilon for floating point comparison
            });
            return (
              <div key={val} className="absolute" style={{ left: `${screenX}px`, transform: 'translateX(-50%)' }}>
                <div className={`w-px bg-muted-foreground ${isMidpoint ? 'h-2' : 'h-1.5'}`}></div>
              </div>
            );
          })}
        </div>
      </div>

      {/* Fixed Y-Axis Ruler (Left) */}
      <div className="absolute top-[60px] bottom-6 left-0 z-20 pointer-events-none" style={{ width: '24px', minWidth: '24px' }}>
        {containerRef.current && (() => {
          const rect = containerRef.current.getBoundingClientRect();
          const height = rect.height;
          const rulerTopOffset = 60; // top-[60px] (below topbar)
          const rulerBottomOffset = 24; // bottom-6 = 24px
          const rulerHeight = height - rulerTopOffset - rulerBottomOffset;
          const scale = transform.scale;
          const panY = transform.y;
          const center = height / 2;
          
          return (
            <div className="relative h-full">
              {yAxisTicks.major.map((val) => {
                // Convert canvas Y value (0-10, 0 at bottom) to screen position
                // Canvas Y is inverted: 0 at bottom, 10 at top
                // Screen Y: 0 at top, height at bottom
                const canvasY = 10 - val; // Invert for screen coordinates
                const screenY = ((canvasY / 10) * height - center) * scale + panY + center;
                // Position relative to ruler container (accounting for top-14 offset)
                const positionInRuler = screenY - rulerTopOffset;
                
                // Allow values to render even slightly outside bounds to prevent clipping
                if (positionInRuler < -100 || positionInRuler > rulerHeight + 100) return null;
                return (
                  <div key={val} className="absolute flex items-center" style={{ top: `${positionInRuler}px`, left: '0', transform: 'translateY(-50%)', width: 'max-content' }}>
                    <div className="w-3 h-px bg-foreground flex-shrink-0"></div>
                    <div className="text-[10px] text-foreground font-mono ml-0.5 whitespace-nowrap flex-shrink-0" style={{ 
                      textAlign: 'left'
                    }}>
                      {val.toFixed(val % 1 === 0 ? 0 : 1)}
                    </div>
                  </div>
                );
              })}
              {yAxisTicks.minor.map((val) => {
                // Convert canvas Y value to screen position
                const canvasY = 10 - val; // Invert for screen coordinates
                const screenY = ((canvasY / 10) * height - center) * scale + panY + center;
                const positionInRuler = screenY - rulerTopOffset;
                
                if (positionInRuler < -10 || positionInRuler > rulerHeight + 10) return null;
                // Check if this is the 0.5 tick (midpoint between two major ticks)
                const isMidpoint = yAxisTicks.major.some((major, idx) => {
                  if (idx === 0) return false;
                  const prevMajor = yAxisTicks.major[idx - 1];
                  const midpoint = (prevMajor + major) / 2;
                  return Math.abs(val - midpoint) < 0.0001; // Small epsilon for floating point comparison
                });
                return (
                  <div key={val} className="absolute" style={{ top: `${positionInRuler}px`, left: '0', transform: 'translateY(-50%)' }}>
                    <div className={`h-px bg-muted-foreground ${isMidpoint ? 'w-2' : 'w-1.5'}`}></div>
                  </div>
                );
              })}
            </div>
          );
        })()}
      </div>
    </>
  );
};

