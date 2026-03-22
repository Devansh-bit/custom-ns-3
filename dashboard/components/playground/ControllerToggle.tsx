"use client";

import React, { useState, useRef } from 'react';
import { Wifi, Waypoints } from 'lucide-react';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';

interface ControllerToggleProps {
  isRLEnabled: boolean;
  onRLToggle: () => void;
  disabled?: boolean;
  onSelectTest?: (testType: 'dfs' | 'throughput' | 'interference') => void;
  onOpenGraphView?: () => void;
}

export const ControllerToggle: React.FC<ControllerToggleProps> = ({
  isRLEnabled,
  onRLToggle,
  disabled = false,
  onSelectTest,
  onOpenGraphView
}) => {
  const [isOpen, setIsOpen] = useState(false);
  const timeoutRef = useRef<NodeJS.Timeout | null>(null);

  const handleMouseEnter = () => {
    if (!onSelectTest || disabled) return;
    if (timeoutRef.current) {
      clearTimeout(timeoutRef.current);
      timeoutRef.current = null;
    }
    setIsOpen(true);
  };

  const handleMouseLeave = () => {
    if (!onSelectTest) return;
    timeoutRef.current = setTimeout(() => {
      setIsOpen(false);
    }, 150);
  };

  return (
    <div className={`flex items-center bg-card rounded-lg shadow-sm border border-border overflow-hidden ${
      disabled ? 'opacity-50' : ''
    }`}>
      {/* Left Button: CONTROLLER with WiFi icon - opens stress test dropdown */}
      <div
        onMouseEnter={handleMouseEnter}
        onMouseLeave={handleMouseLeave}
        className="relative"
      >
        <DropdownMenu open={isOpen} onOpenChange={setIsOpen} modal={false}>
          <DropdownMenuTrigger asChild>
            <button
              disabled={disabled}
              className={`flex items-center gap-2 px-3 py-2 transition-colors border-r border-border ${
                disabled ? 'cursor-not-allowed' : 'hover:bg-accent cursor-pointer'
              }`}
            >
              <span className="text-xs font-semibold text-foreground uppercase tracking-wide">Controller</span>
            </button>
          </DropdownMenuTrigger>

          {onSelectTest && (
            <DropdownMenuContent
              side="bottom"
              align="center"
              className="min-w-[200px]"
            >
            <DropdownMenuItem
              onClick={() => !disabled && onSelectTest('dfs')}
              disabled={disabled}
              className="flex items-center justify-between cursor-pointer"
            >
              <span className="font-medium text-sm">Forced DFS Test</span>
              <svg xmlns="http://www.w3.org/2000/svg" className="w-5 h-5 shrink-0" fill="currentColor" viewBox="0 0 16 16">
                <path d="M6.634 1.135A7 7 0 0 1 15 8a.5.5 0 0 1-1 0 6 6 0 1 0-6.5 5.98v-1.005A5 5 0 1 1 13 8a.5.5 0 0 1-1 0 4 4 0 1 0-4.5 3.969v-1.011A2.999 2.999 0 1 1 11 8a.5.5 0 0 1-1 0 2 2 0 1 0-2.5 1.936v-1.07a1 1 0 1 1 1 0V15.5a.5.5 0 0 1-1 0v-.518a7 7 0 0 1-.866-13.847"/>
              </svg>
            </DropdownMenuItem>

            <DropdownMenuItem
              onClick={() => !disabled && onSelectTest('throughput')}
              disabled={disabled}
              className="flex items-center justify-between cursor-pointer"
            >
              <span className="font-medium text-sm">Max Throughput Test</span>
              <svg xmlns="http://www.w3.org/2000/svg" className="w-5 h-5 shrink-0" fill="currentColor" viewBox="0 0 16 16">
                <path d="M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5"/>
                <path d="M7.646 1.146a.5.5 0 0 1 .708 0l3 3a.5.5 0 0 1-.708.708L8.5 2.707V11.5a.5.5 0 0 1-1 0V2.707L5.354 4.854a.5.5 0 1 1-.708-.708z"/>
              </svg>
            </DropdownMenuItem>

            <DropdownMenuItem
              onClick={() => !disabled && onSelectTest('interference')}
              disabled={disabled}
              className="flex items-center justify-between cursor-pointer"
            >
              <span className="font-medium text-sm">Interference Test</span>
              <svg xmlns="http://www.w3.org/2000/svg" className="w-5 h-5 shrink-0" fill="currentColor" viewBox="0 0 16 16">
                <path d="M6.354 5.5H4a3 3 0 0 0 0 6h3a3 3 0 0 0 2.83-4H9q-.13 0-.25.031A2 2 0 0 1 7 10.5H4a2 2 0 1 1 0-4h1.535c.218-.376.495-.714.82-1z"/>
                <path d="M9 5.5a3 3 0 0 0-2.83 4h1.098A2 2 0 0 1 9 6.5h3a2 2 0 1 1 0 4h-1.535a4 4 0 0 1-.82 1H12a3 3 0 1 0 0-6z"/>
                <path fillRule="evenodd" d="m12.096 8.854-.708-.708L13.5 6.034l.708.708zm-8.192 0L2.793 7.742 1.354 9.177l1.111 1.111zM10 8a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0m-3 0a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0"/>
              </svg>
            </DropdownMenuItem>

            <DropdownMenuItem
              onClick={() => {
                if (!disabled && onOpenGraphView) {
                  setIsOpen(false);
                  onOpenGraphView();
                }
              }}
              disabled={disabled || !onOpenGraphView}
              className="flex items-center justify-between cursor-pointer"
            >
              <span className="font-medium text-sm">Open Graph View</span>
              <Waypoints className="w-5 h-5 shrink-0" />
            </DropdownMenuItem>
          </DropdownMenuContent>
        )}
        </DropdownMenu>
      </div>

      {/* Right Button: RL toggle with WiFi icon - toggles RL ON/OFF */}
      <button
        onClick={onRLToggle}
        disabled={disabled}
        className={`flex items-center gap-2 px-3 py-2 transition-colors ${
          disabled ? 'cursor-not-allowed' : 'hover:bg-accent cursor-pointer'
        }`}
      >
        <span className="text-xs font-semibold text-foreground uppercase tracking-wide">RL</span>
        <Wifi size={18} className="text-foreground" />
        <span className="font-medium text-sm text-foreground">
          {isRLEnabled ? 'ON' : 'OFF'}
        </span>
      </button>
    </div>
  );
};
