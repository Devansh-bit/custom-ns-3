import React from 'react';
import { Wifi, Smartphone, Bluetooth, Radio, Microwave } from 'lucide-react';
import { NodeType } from '@/lib/types';

interface ToolSelectionToolbarProps {
  placeMode: NodeType | null;
  onSelectTool: (tool: NodeType | null) => void;
  isDisabled: boolean;
}

export const ToolSelectionToolbar: React.FC<ToolSelectionToolbarProps> = ({
  placeMode,
  onSelectTool,
  isDisabled
}) => {
  return (
    <div className="absolute bottom-24 sm:bottom-8 left-1/2 -translate-x-1/2 z-30 max-w-[calc(100vw-2rem)]">
      <div className="bg-blue-100 dark:bg-blue-900/30 rounded-xl px-1 sm:px-2 py-1.5 sm:py-2 shadow-md border border-border flex items-center gap-0.5 sm:gap-2 flex-nowrap">
        {/* AP Button - Sub-container */}
        <div className={`
          rounded-lg border p-1 transition-all duration-200
          ${isDisabled
            ? 'bg-card border-border opacity-50'
            : placeMode === 'ap'
              ? 'bg-brand-blue/20 border-brand-blue'
              : 'bg-card border-border hover:bg-accent'
          }
        `}>
          <button
            onClick={() => !isDisabled && onSelectTool(placeMode === 'ap' ? null : 'ap')}
            disabled={isDisabled}
            className={`
              flex items-center gap-0.5 sm:gap-1 px-1 sm:px-2 py-1 rounded-md transition-all duration-200 text-xs sm:text-sm whitespace-nowrap w-full
              ${isDisabled
                ? 'text-muted-foreground cursor-not-allowed'
                : placeMode === 'ap'
                  ? 'text-brand-blue'
                  : 'text-foreground'
              }
            `}
          >
            <Wifi size={14} className="sm:w-4 sm:h-4" />
            <span className="hidden sm:inline">AP</span>
          </button>
        </div>

        {/* Client Button - Sub-container */}
        <div className={`
          rounded-lg border p-1 transition-all duration-200
          ${isDisabled
            ? 'bg-card border-border opacity-50'
            : placeMode === 'client'
              ? 'bg-brand-blue/20 border-brand-blue'
              : 'bg-card border-border hover:bg-accent'
          }
        `}>
          <button
            onClick={() => !isDisabled && onSelectTool(placeMode === 'client' ? null : 'client')}
            disabled={isDisabled}
            className={`
              flex items-center gap-0.5 sm:gap-1 px-1 sm:px-2 py-1 rounded-md transition-all duration-200 text-xs sm:text-sm whitespace-nowrap w-full
              ${isDisabled
                ? 'text-muted-foreground cursor-not-allowed'
                : placeMode === 'client'
                  ? 'text-brand-blue'
                  : 'text-foreground'
              }
            `}
          >
            <Smartphone size={14} className="sm:w-4 sm:h-4" />
            <span className="hidden sm:inline">Client</span>
          </button>
        </div>

        {/* Separator */}
        <div className="w-px h-5 sm:h-6 bg-border"></div>

        {/* Interferers Group */}
        <div className="flex items-center gap-0.5 sm:gap-2">
          {/* BLE Button - Sub-container */}
          <div className={`
            rounded-lg border p-1 transition-all duration-200
            ${isDisabled
              ? 'bg-card border-border opacity-50'
              : placeMode === 'ble'
                ? 'bg-orange-500/20 border-orange-500'
                : 'bg-card border-border hover:bg-accent'
            }
          `}>
            <button
              onClick={() => !isDisabled && onSelectTool(placeMode === 'ble' ? null : 'ble')}
              disabled={isDisabled}
              className={`
                flex items-center gap-0.5 sm:gap-1 px-1 sm:px-2 py-1 rounded-md transition-all duration-200 text-xs sm:text-sm whitespace-nowrap w-full
                ${isDisabled
                  ? 'text-muted-foreground cursor-not-allowed'
                  : placeMode === 'ble'
                    ? 'text-orange-500'
                    : 'text-foreground'
                }
              `}
            >
              <Bluetooth size={14} className="sm:w-4 sm:h-4" />
              <span className="hidden sm:inline">BLE</span>
            </button>
          </div>

          {/* Zigbee Button - Sub-container */}
          <div className={`
            rounded-lg border p-1 transition-all duration-200
            ${isDisabled
              ? 'bg-card border-border opacity-50'
              : placeMode === 'zigbee'
                ? 'bg-orange-500/20 border-orange-500'
                : 'bg-card border-border hover:bg-accent'
            }
          `}>
            <button
              onClick={() => !isDisabled && onSelectTool(placeMode === 'zigbee' ? null : 'zigbee')}
              disabled={isDisabled}
              className={`
                flex items-center gap-0.5 sm:gap-1 px-1 sm:px-2 py-1 rounded-md transition-all duration-200 text-xs sm:text-sm whitespace-nowrap w-full
                ${isDisabled
                  ? 'text-muted-foreground cursor-not-allowed'
                  : placeMode === 'zigbee'
                    ? 'text-orange-500'
                    : 'text-foreground'
                }
              `}
            >
              <Radio size={14} className="sm:w-4 sm:h-4" />
              <span className="hidden sm:inline">Zigbee</span>
            </button>
          </div>

          {/* Microwave Button - Sub-container */}
          <div className={`
            rounded-lg border p-1 transition-all duration-200
            ${isDisabled
              ? 'bg-card border-border opacity-50'
              : placeMode === 'microwave'
                ? 'bg-orange-500/20 border-orange-500'
                : 'bg-card border-border hover:bg-accent'
            }
          `}>
            <button
              onClick={() => !isDisabled && onSelectTool(placeMode === 'microwave' ? null : 'microwave')}
              disabled={isDisabled}
              className={`
                flex items-center gap-0.5 sm:gap-1 px-1 sm:px-2 py-1 rounded-md transition-all duration-200 text-xs sm:text-sm whitespace-nowrap w-full
                ${isDisabled
                  ? 'text-muted-foreground cursor-not-allowed'
                  : placeMode === 'microwave'
                    ? 'text-orange-500'
                    : 'text-foreground'
                }
              `}
            >
              <Microwave size={14} className="sm:w-4 sm:h-4" />
              <span className="hidden sm:inline md:hidden">MW</span>
              <span className="hidden md:inline">Microwave</span>
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};

