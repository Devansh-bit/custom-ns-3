import React from 'react';

interface BuildingOverlayProps {
  isStarting: boolean;
  isStopping: boolean;
}

export const BuildingOverlay: React.FC<BuildingOverlayProps> = ({ isStarting, isStopping }) => {
  if (!isStarting && !isStopping) return null;

  return (
    <div className="absolute inset-0 bg-background/80 z-50 flex flex-col items-center justify-center backdrop-blur-sm">
      <div className="flex flex-col items-center justify-center p-8">
        <div className="w-16 h-16 border-4 border-brand-blue border-t-transparent rounded-full animate-spin mb-4"></div>
        {isStarting && (
          <>
            <h3 className="text-lg font-bold text-brand-blue">Starting Simulation...</h3>
            <p className="text-muted-foreground text-sm">Initializing network topology</p>
          </>
        )}
        {isStopping && (
          <>
            <h3 className="text-lg font-bold text-brand-blue">Stopping Simulation...</h3>
            <p className="text-muted-foreground text-sm">Terminating simulation processes</p>
          </>
        )}
      </div>
    </div>
  );
};

