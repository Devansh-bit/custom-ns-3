'use client';

import { useState, useCallback } from 'react';
import { useWebSocket } from '@/hooks/useWebSocket';
import NetworkCanvas from '@/components/NetworkCanvas';
import StatsPanel from '@/components/StatsPanel';
import Tooltip from '@/components/Tooltip';
import { HoveredNode } from '@/types/network';

export default function Home() {
  const { data, status } = useWebSocket();
  const [hoveredNode, setHoveredNode] = useState<HoveredNode>(null);
  const [tooltipPos, setTooltipPos] = useState({ x: 0, y: 0 });

  const handleHover = useCallback((node: HoveredNode, x: number, y: number) => {
    setHoveredNode(node);
    setTooltipPos({ x, y });
  }, []);

  return (
    <main>
      <NetworkCanvas data={data} onHover={handleHover} />
      <StatsPanel data={data} status={status} />
      <Tooltip node={hoveredNode} x={tooltipPos.x} y={tooltipPos.y} />
    </main>
  );
}
