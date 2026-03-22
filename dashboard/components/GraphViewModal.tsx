'use client';

import { useState, useCallback, useEffect, useMemo } from 'react';
import { X } from 'lucide-react';
import { useWebSocket } from '@/hooks/useWebSocket';
import NetworkCanvas from '@/components/NetworkCanvas';
import StatsPanel from '@/components/StatsPanel';
import Tooltip from '@/components/Tooltip';
import { HoveredNode, Interferer } from '@/types/network';
import { NetworkNode } from '@/lib/types';

interface GraphViewOverlayProps {
  isOpen: boolean;
  onClose: () => void;
  nodes?: NetworkNode[]; // Pass nodes from Playground to get interferers
}

export function GraphViewOverlay({ isOpen, onClose, nodes = [] }: GraphViewOverlayProps) {
  const { data, status } = useWebSocket();
  const [hoveredNode, setHoveredNode] = useState<HoveredNode>(null);
  const [tooltipPos, setTooltipPos] = useState({ x: 0, y: 0 });

  // Extract interferers from nodes (same as main canvas)
  // Interferer types in nodes are: 'ble', 'zigbee', 'microwave'
  const interferers = useMemo<Interferer[]>(() => {
    const typeMap: Record<string, Interferer['type']> = {
      ble: 'bluetooth',
      zigbee: 'zigbee',
      microwave: 'microwave',
    };
    return nodes
      .filter(n => n.type === 'ble' || n.type === 'zigbee' || n.type === 'microwave')
      .map((n, idx) => ({
        id: idx,
        type: typeMap[n.type as string] || 'other',
        x: n.x / 10, // Convert from canvas coords (0-100) to world coords
        y: n.y / 10,
        z: 0,
        power_dbm: n.power,
        active: true,
      }));
  }, [nodes]);

  // Merge WebSocket data with interferers from config
  const enrichedData = useMemo(() => {
    if (!data) return null;
    return {
      ...data,
      interferers: interferers.length > 0 ? interferers : data.interferers,
    };
  }, [data, interferers]);

  const handleHover = useCallback((node: HoveredNode, x: number, y: number) => {
    setHoveredNode(node);
    setTooltipPos({ x, y });
  }, []);

  // Handle escape key to close overlay
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && isOpen) {
        onClose();
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [isOpen, onClose]);

  if (!isOpen) return null;

  return (
    <div
      className="absolute inset-0 z-40 overflow-hidden rounded-lg"
      style={{
        background: 'linear-gradient(180deg, #0a0a12 0%, #12121f 100%)',
      }}
    >
      {/* Close Button */}
      <button
        onClick={onClose}
        className="absolute top-3 right-3 z-50 p-2 rounded-lg bg-black/40 hover:bg-black/60 transition-colors text-white/70 hover:text-white"
      >
        <X size={18} />
      </button>

      {/* Title */}
      <div className="absolute top-3 left-3 z-50">
        <h2 className="text-sm font-semibold text-white/90">Network Graph View</h2>
        <p className="text-xs text-white/50">Real-time topology</p>
      </div>

      {/* Network Canvas - Full Overlay Size */}
      <div className="w-full h-full">
        <NetworkCanvas data={enrichedData} onHover={handleHover} />
      </div>

      {/* Stats Panel - Compact for overlay */}
      <div className="absolute top-12 right-3 z-50 scale-90 origin-top-right">
        <StatsPanel data={enrichedData} status={status} />
      </div>

      {/* Tooltip */}
      <Tooltip node={hoveredNode} x={tooltipPos.x} y={tooltipPos.y} />

      {/* Legend - Compact */}
      <div className="absolute bottom-3 left-3 z-50 bg-black/60 backdrop-blur-sm rounded-lg p-2 border border-white/10">
        <div className="flex flex-wrap gap-2 text-[10px]">
          <div className="flex items-center gap-1">
            <div className="w-2.5 h-2.5 rounded-full bg-[#4f8fff]" />
            <span className="text-white/70">AP</span>
          </div>
          <div className="flex items-center gap-1">
            <div className="w-2 h-2 rounded-full bg-white border border-blue-400/50" />
            <span className="text-white/70">Client</span>
          </div>
          <div className="flex items-center gap-1">
            <div className="w-2.5 h-2.5 rounded bg-yellow-500/80" style={{ transform: 'rotate(45deg)' }} />
            <span className="text-white/70">Interferer</span>
          </div>
        </div>
      </div>
    </div>
  );
}
