import React from 'react';

interface MouseCoordinatesProps {
  x: number;
  y: number;
}

export const MouseCoordinates: React.FC<MouseCoordinatesProps> = ({ x, y }) => {
  // Divide by 10 to convert from 0-100 scale to 0-10 scale
  const xRuler = (x / 10).toFixed(1);
  const yRuler = (y / 10).toFixed(1);
  
  return (
    <div className="bg-popover/80 backdrop-blur-sm text-popover-foreground px-3 py-1.5 rounded-md text-xs font-mono shadow-md mb-3">
      X: {xRuler}  Y: {yRuler}
    </div>
  );
};

