import React, { useEffect } from 'react';
import { Play, Hammer, RefreshCw, Pause, LogOut } from 'lucide-react';
import { useRouter } from 'next/navigation';
import { SaveTemplateButton } from './SaveTemplateButton';

interface ControlPanelProps {
  isBuilding: boolean;
  isRunning: boolean;
  inRunTransition?: boolean;
  isEmpty?: boolean;
  isBuilt?: boolean;
  hasChanges?: boolean;
  isNavigatingAway?: boolean;
  showTemplateSelection?: boolean;
  onBuild: () => void;
  onRun: () => void;
  onStop: () => void;
  onNavigateAway?: () => void;
  // Save template props
  onSaveTemplate?: () => void;
  hasUnsavedChanges?: boolean;
  nodesCount?: number;
}

export const ControlPanel: React.FC<ControlPanelProps> = ({
  isBuilding,
  isRunning,
  inRunTransition = false,
  isEmpty = false,
  isBuilt = false,
  hasChanges = false,
  isNavigatingAway = false,
  showTemplateSelection = false,
  onBuild,
  onRun,
  onStop,
  onNavigateAway,
  onSaveTemplate,
  hasUnsavedChanges = false,
  nodesCount = 0,
}) => {
  const router = useRouter();
  
  // Prefetch dashboard route and CanvasPreview component when component mounts
  useEffect(() => {
    if (typeof window !== 'undefined') {
      // Prefetch the dashboard route
      router.prefetch('/');
      
      // Preload CanvasPreview component and dashboard components
      Promise.all([
        import('@/components/CanvasPreview'),
        import('@/components/APHealth'),
        import('@/components/BO'),
        import('@/components/ClientPieChart'),
        import('@/components/QoEMetrics'),
      ]).catch(() => {
        // Silently fail if prefetch fails
      });
    }
  }, [router]);
  
  return (
    <div className="bg-card border-t border-border flex flex-col md:flex-row items-center justify-between gap-4 z-20 shrink-0 h-[72px]" style={{ padding: '16px' }}>
      
      <div className="flex items-center gap-3">
        <button
          onClick={onBuild}
          disabled={isBuilding || isRunning || inRunTransition || isEmpty || (isBuilt && !hasChanges)}
          className={`
            flex items-center gap-2 px-6 py-2.5 rounded-lg font-bold text-sm tracking-wide transition-all shadow-sm
            ${isBuilding
              ? 'bg-muted text-muted-foreground cursor-wait'
              : isRunning || inRunTransition
                ? 'bg-muted text-muted-foreground/70 cursor-not-allowed opacity-50'
                : isEmpty
                  ? 'bg-muted text-muted-foreground cursor-not-allowed opacity-50'
                  : isBuilt && !hasChanges
                    ? 'bg-muted text-muted-foreground cursor-not-allowed opacity-50'
                    : 'bg-card border-2 border-green-500 text-green-600 dark:text-green-400 hover:bg-green-500/10 hover:shadow-md active:scale-95'
            }
          `}
        >
          {isBuilding ? <RefreshCw size={18} className="animate-spin" /> : <Hammer size={18} />}
          {hasChanges && isBuilt ? 'REBUILD' : 'BUILD'}
        </button>

        <button
          onClick={isRunning ? onStop : onRun}
          disabled={isRunning ? false : (isBuilding || isEmpty || !isBuilt || hasChanges || isNavigatingAway)}
          className={`
            flex items-center gap-2 px-6 py-2.5 rounded-lg font-bold text-sm tracking-wide transition-all shadow-sm min-w-[100px]
            ${isRunning
              ? 'bg-red-500 text-white hover:bg-red-600 hover:shadow-lg cursor-pointer'
              : isEmpty
                ? 'bg-muted text-muted-foreground cursor-not-allowed opacity-50'
                : !isBuilt || hasChanges || isNavigatingAway
                  ? 'bg-muted text-muted-foreground cursor-not-allowed opacity-50'
                  : 'bg-brand-accent text-white -foreground hover:bg-brand-accent/90 hover:shadow-lg active:scale-95'
            }
            ${isBuilding && !isRunning ? 'opacity-50 cursor-not-allowed' : ''}
          `}
        >
          {isRunning ? <Pause size={18} /> : <Play size={18} />}
          {isRunning ? 'STOP' : 'RUN'}
        </button>

        {/* Save Template Button - appears when there are unsaved changes */}
        {onSaveTemplate && (
          <SaveTemplateButton
            onSave={onSaveTemplate}
            hasUnsavedChanges={hasUnsavedChanges}
            nodesCount={nodesCount}
          />
        )}
      </div>

      {/* Exit to Dashboard Button (Replacing Timer) */}
      <button 
        className="flex items-center gap-2 px-4 py-2 text-muted-foreground hover:text-foreground hover:bg-accent rounded-lg transition-colors text-sm font-medium ml-auto"
        onMouseEnter={() => {
          // Prefetch on hover for faster navigation
          if (typeof window !== 'undefined') {
            router.prefetch('/');
            // Preload components
            Promise.all([
              import('@/components/CanvasPreview'),
              import('@/components/APHealth'),
              import('@/components/BO'),
            ]).catch(() => {});
          }
        }}
        onClick={() => {
          if (onNavigateAway) {
            onNavigateAway();
          }
          // Set transition flag for smooth animation from top-right to bottom-left
          if (typeof window !== 'undefined') {
            localStorage.setItem('canvas-preview-transition', 'true');
          }
          window.location.href = '/';
        }}
      >
        <LogOut size={16} />
        Back to Dashboard
      </button>

    </div>
  );
};
