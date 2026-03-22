import React, { useEffect, useState, useRef } from 'react';
import { NetworkNode } from '@/lib/types';

interface ConnectionLinesProps {
  hoveredApId: number | null;
  hoveredClientId: number | null;
  clientToApMap: Map<number, number>;
  nodes: NetworkNode[];
  connectedClientIds: Set<number>;
  isRunning: boolean;
  containerRef: React.RefObject<HTMLDivElement | null>;
}

interface LineData {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  clientId: number;
}

export const ConnectionLines: React.FC<ConnectionLinesProps> = ({
  hoveredApId,
  hoveredClientId,
  clientToApMap,
  nodes,
  connectedClientIds,
  isRunning,
  containerRef
}) => {
  const [lines, setLines] = useState<LineData[]>([]);
  const animationFrameRef = useRef<number | null>(null);
  const svgRef = useRef<SVGSVGElement>(null);

  useEffect(() => {
    // Cleanup previous animation frame if any
    if (animationFrameRef.current !== null) {
      cancelAnimationFrame(animationFrameRef.current);
      animationFrameRef.current = null;
    }

    // If neither AP nor Client is hovered, clear lines and exit
    if (hoveredApId === null && hoveredClientId === null) {
      setLines([]);
      return;
    }

    const updatePositions = () => {
      if (!containerRef.current || !svgRef.current) return;

      const svg = svgRef.current;
      const svgRect = svg.getBoundingClientRect();
      
      // Calculate scale to handle CSS transforms on parent
      // Use 1 as fallback if client dimensions are 0
      const scaleX = svg.clientWidth > 0 ? svgRect.width / svg.clientWidth : 1;
      const scaleY = svg.clientHeight > 0 ? svgRect.height / svg.clientHeight : 1;

      const newLines: LineData[] = [];

      // Case 1: AP Hovered - Show all connected clients
      if (hoveredApId !== null) {
        const apNode = nodes.find(node => node.id === hoveredApId && node.type === 'ap');
        if (apNode) {
          const connectedClients = nodes.filter(
            node => node.type === 'client' && connectedClientIds.has(node.id)
          );
          
          if (connectedClients.length > 0) {
            // Get AP element position
            const apElement = containerRef.current!.querySelector(`[data-node-id="${apNode.id}"]`) as HTMLElement;
            if (apElement) {
              const apRect = apElement.getBoundingClientRect();
              const apCenterX = (apRect.left + apRect.width / 2 - svgRect.left) / scaleX;
              const apCenterY = (apRect.top + apRect.height / 2 - svgRect.top) / scaleY;

              connectedClients.forEach((client) => {
                const clientElement = containerRef.current!.querySelector(`[data-node-id="${client.id}"]`) as HTMLElement;
                if (!clientElement) return;

                const clientRect = clientElement.getBoundingClientRect();
                const clientCenterX = (clientRect.left + clientRect.width / 2 - svgRect.left) / scaleX;
                const clientCenterY = (clientRect.top + clientRect.height / 2 - svgRect.top) / scaleY;

                newLines.push({
                  x1: apCenterX,
                  y1: apCenterY,
                  x2: clientCenterX,
                  y2: clientCenterY,
                  clientId: client.id
                });
              });
            }
          }
        }
      }
      // Case 2: Client Hovered - Show line to connected AP
      else if (hoveredClientId !== null) {
        const apId = clientToApMap.get(hoveredClientId);
        if (apId !== undefined) {
          const apNode = nodes.find(node => node.id === apId && node.type === 'ap');
          const clientNode = nodes.find(node => node.id === hoveredClientId && node.type === 'client');
          
          if (apNode && clientNode) {
            const apElement = containerRef.current!.querySelector(`[data-node-id="${apNode.id}"]`) as HTMLElement;
            const clientElement = containerRef.current!.querySelector(`[data-node-id="${clientNode.id}"]`) as HTMLElement;
            
            if (apElement && clientElement) {
              const apRect = apElement.getBoundingClientRect();
              const apCenterX = (apRect.left + apRect.width / 2 - svgRect.left) / scaleX;
              const apCenterY = (apRect.top + apRect.height / 2 - svgRect.top) / scaleY;

              const clientRect = clientElement.getBoundingClientRect();
              const clientCenterX = (clientRect.left + clientRect.width / 2 - svgRect.left) / scaleX;
              const clientCenterY = (clientRect.top + clientRect.height / 2 - svgRect.top) / scaleY;

              newLines.push({
                x1: apCenterX,
                y1: apCenterY,
                x2: clientCenterX,
                y2: clientCenterY,
                clientId: clientNode.id
              });
            }
          }
        }
      }

      setLines(newLines);
      
      if (isRunning) {
        animationFrameRef.current = requestAnimationFrame(updatePositions);
      }
    };

    // Initial update
    updatePositions();

    return () => {
      if (animationFrameRef.current !== null) {
        cancelAnimationFrame(animationFrameRef.current);
        animationFrameRef.current = null;
      }
    };
  }, [hoveredApId, hoveredClientId, nodes, isRunning, containerRef, connectedClientIds, clientToApMap]);

  return (
    <svg
      ref={svgRef}
      className="absolute inset-0 w-full h-full pointer-events-none"
      style={{
        zIndex: 0, // Behind nodes
        overflow: 'visible',
      }}
    >
      {lines.map((line) => (
        <line
          key={`connection-${hoveredApId}-${line.clientId}`}
          x1={line.x1}
          y1={line.y1}
          x2={line.x2}
          y2={line.y2}
          stroke="rgb(59, 130, 246)"
          strokeOpacity="0.6"
          strokeWidth="2"
          strokeDasharray="5,5"
        >
          {/* Animated dash offset for a flowing effect */}
          <animate
            attributeName="stroke-dashoffset"
            from="0"
            to="10"
            dur="1s"
            repeatCount="indefinite"
          />
        </line>
      ))}
    </svg>
  );
};