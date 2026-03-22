import React from 'react';
import { NodeType } from '@/types';

interface PlacementHintProps {
  placeMode?: NodeType;
  mousePos: { x: number; y: number };
  scale: number;
}

export const PlacementHint: React.FC<PlacementHintProps> = ({
  placeMode,
  mousePos,
  scale
}) => {
  if (!placeMode) return null;

  // Calculate size inversely proportional to zoom scale (with dampening)
  // When zoomed in (scale > 1), hint should be smaller but not too small
  // When zoomed out (scale < 1), hint should be larger
  const baseFontSize = 12; // text-xs = 12px
  const basePaddingX = 8;  // px-2 = 8px
  const basePaddingY = 4;  // py-1 = 4px
  const baseOffset = 16;   // offset from cursor

  // Use square root of scale for less dramatic scaling - makes it bigger on zoom in
  const scaleFactor = Math.sqrt(scale);
  const fontSize = baseFontSize / scaleFactor;
  const paddingX = basePaddingX / scaleFactor;
  const paddingY = basePaddingY / scaleFactor;
  const offset = baseOffset / scaleFactor;

  return (
    <div 
      className="absolute pointer-events-none z-50 bg-brand-blue text-white rounded-full shadow-lg whitespace-nowrap"
      style={{ 
        left: `${mousePos.x}%`, 
        top: `${100 - mousePos.y}%`,
        fontSize: `${fontSize}px`,
        paddingLeft: `${paddingX}px`,
        paddingRight: `${paddingX}px`,
        paddingTop: `${paddingY}px`,
        paddingBottom: `${paddingY}px`,
        transform: `translate(${offset}px, -50%)`,
        transformOrigin: 'center center'
      }}
    >
      Place {placeMode.toUpperCase()}
    </div>
  );
};

