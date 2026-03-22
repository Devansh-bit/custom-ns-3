import React from 'react';
import { ZoomIn, ZoomOut, Expand, Ruler } from 'lucide-react';

interface ZoomControlsProps {
  onZoomIn: () => void;
  onZoomOut: () => void;
  onFitToView: () => void;
  isFitToViewDisabled?: boolean;
  showScale?: boolean;
  onToggleScale?: () => void;
}

export const ZoomControls: React.FC<ZoomControlsProps> = ({
  onZoomIn,
  onZoomOut,
  onFitToView,
  isFitToViewDisabled = false,
  showScale = false,
  onToggleScale
}) => {
  return (
    <div className="bg-card/90 backdrop-blur rounded-lg shadow-md border border-border items-center p-0.5 mb-4.5 flex flex-row gap-0.5">
      <button 
        onClick={onZoomOut} 
        className="p-1.25 hover:bg-accent rounded text-muted-foreground" 
      >
        <ZoomOut size={16} />
      </button>
      <button 
        onClick={onFitToView} 
        disabled={isFitToViewDisabled}
        className="p-1.25 hover:bg-accent rounded text-muted-foreground disabled:text-muted-foreground/70 disabled:cursor-default" 
      >
        <Expand size={16} />
      </button>
      <button 
        onClick={onZoomIn} 
        className="p-1.25 hover:bg-accent rounded text-muted-foreground" 
      >
        <ZoomIn size={16} />
      </button>
      {onToggleScale && (
        <button 
          onClick={onToggleScale} 
          className={`p-1.25 hover:bg-accent rounded transition-colors ${
            showScale ? 'text-primary bg-primary/10' : 'text-muted-foreground'
          }`}
        >
          <Ruler size={16} />
        </button>
      )}
    </div>
  );
};
