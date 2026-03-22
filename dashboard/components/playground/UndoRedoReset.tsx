import React from 'react';
import { Undo2, Redo2, RotateCcw } from 'lucide-react';

interface UndoRedoResetProps {
  onUndo: () => void;
  onRedo: () => void;
  onReset: () => void;
  canUndo: boolean;
  canRedo: boolean;
  canReset: boolean;
  isDisabled: boolean;
}

export const UndoRedoReset: React.FC<UndoRedoResetProps> = ({
  onUndo,
  onRedo,
  onReset,
  canUndo,
  canRedo,
  canReset,
  isDisabled
}) => {
  return (
    <div className="flex items-center gap-2">
      <button
        onClick={onUndo}
        disabled={!canUndo || isDisabled}
        className={`
          flex items-center justify-center px-2 py-1.5 rounded-lg border font-medium transition-all duration-200 text-sm
          ${isDisabled || !canUndo
            ? 'bg-muted/50 border-border text-muted-foreground cursor-not-allowed opacity-50'
            : 'bg-card border-border text-foreground hover:border-orange-500 hover:text-white hover:bg-orange-500/10 cursor-pointer'
          }
        `}
      >
        <Undo2 size={14} />
      </button>
      <button
        onClick={onRedo}
        disabled={!canRedo || isDisabled}
        className={`
          flex items-center justify-center px-2 py-1.5 rounded-lg border font-medium transition-all duration-200 text-sm
          ${isDisabled || !canRedo
            ? 'bg-muted/50 border-border text-muted-foreground cursor-not-allowed opacity-50'
            : 'bg-card border-border text-foreground hover:border-orange-500 hover:text-white hover:bg-orange-500/10 cursor-pointer'
          }
        `}
      >
        <Redo2 size={14} />
      </button>
      <button
        onClick={onReset}
        disabled={!canReset || isDisabled}
        className={`
          flex items-center px-2 py-1.5 rounded-lg border font-medium transition-all duration-200 text-sm
          ${isDisabled || !canReset
            ? 'bg-muted/50 border-border text-muted-foreground cursor-not-allowed opacity-50'
            : 'bg-card border-border text-foreground hover:border-orange-500 hover:text-white hover:bg-orange-500/10 cursor-pointer'
          }
        `}
      >
        <RotateCcw size={14} />
      </button>
    </div>
  );
};

