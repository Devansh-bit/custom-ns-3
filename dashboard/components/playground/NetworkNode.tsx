import React, { useState, useRef, useMemo } from 'react';
import { Wifi, Smartphone, Microwave, Bluetooth, Radio, Zap, X } from 'lucide-react';
import { NetworkNode as NetworkNodeType, NodeType } from '@/lib//types';
import { NodeHoverCard } from './NodeHoverCard';

interface NetworkNodeProps {
  node: NetworkNodeType;
  isRunning: boolean;
  placeMode: NodeType | null;
  onDelete: (id: string) => void;
  onMouseDown: (e: React.MouseEvent, node: NetworkNodeType) => void;
  setHoveredApId: React.Dispatch<React.SetStateAction<number | null>>;
  setHoveredClientId: React.Dispatch<React.SetStateAction<number | null>>;
  hoveredApId: number | null;
  hoveredClientId: number | null;
  connectedClientIds: Set<number>;
  clientToApMap: Map<number, number>;
}

const renderIcon = (type: NodeType, size: number) => {
  switch (type) {
    case 'ap': return <Wifi size={size} />;
    case 'client': return <Smartphone size={size} />;
    case 'ble': return <Bluetooth size={size} />;
    case 'zigbee': return <Radio size={size} />;
    case 'microwave': return <Microwave size={size} />;
    default: return <div />;
  }
};

const getNodeStyle = (node: NetworkNodeType) => {
  switch (node.type) {
    case 'ap': return 'bg-blue-200 dark:bg-blue-900/50 text-blue-700 dark:text-blue-300 border-blue-400 dark:border-blue-600';
    case 'client':
      if (node.status === 'poor') return 'bg-red-100 dark:bg-red-900/50 text-red-600 dark:text-red-300 border-red-300 dark:border-red-600';
      if (node.status === 'fair') return 'bg-yellow-100 dark:bg-yellow-900/50 text-yellow-600 dark:text-yellow-300 border-yellow-300 dark:border-yellow-600';
      return 'bg-card text-card-foreground border-border';
    case 'microwave':
    case 'ble':
    case 'zigbee': return 'bg-orange-100 dark:bg-orange-900/50 text-orange-600 dark:text-orange-300 border-orange-300 dark:border-orange-600';
    default: return 'bg-muted text-muted-foreground';
  }
};

export const NetworkNode: React.FC<NetworkNodeProps> = ({
  node,
  isRunning,
  placeMode,
  onDelete,
  onMouseDown,
  setHoveredApId,
  setHoveredClientId,
  hoveredApId,
  hoveredClientId,
  connectedClientIds,
  clientToApMap
}) => {
  const canDrag = !isRunning && !placeMode;
  const [isHovered, setIsHovered] = useState(false);
  const nodeRef = useRef<HTMLDivElement>(null);

  const handleMouseEnter = async () => {
    setIsHovered(true);
    if (node.type === 'ap') {
      setHoveredApId(node.id);
      setHoveredClientId(null); // Clear client hover if AP is hovered
    } else if (node.type === 'client') {
      setHoveredClientId(node.id);
      setHoveredApId(null); // Clear AP hover if client is hovered
    }
  };

  const handleMouseLeave = () => {
    setIsHovered(false);
    if (node.type === 'ap') {
      setHoveredApId((prev) => (prev === node.id ? null : prev));
    } else if (node.type === 'client') {
      setHoveredClientId((prev) => (prev === node.id ? null : prev));
    }
  };

  const shouldHighlight = useMemo(() => {
    if (!isRunning) return false;
    if (hoveredClientId !== null) {
      // If a client is hovered, highlight that client and its connected AP
      const connectedApId = clientToApMap.get(hoveredClientId);
      return (node.id === hoveredClientId && node.type === 'client') ||
             (node.id === connectedApId && node.type === 'ap');
    } else if (hoveredApId !== null) {
      // If an AP is hovered, highlight that AP and its connected clients
      return (node.id === hoveredApId && node.type === 'ap') ||
             (node.type === 'client' && connectedClientIds.has(node.id));
    }
    return false; // Nothing hovered
  }, [node.id, node.type, hoveredClientId, clientToApMap, hoveredApId, connectedClientIds, isRunning]);

  const shouldDim = useMemo(() => {
    if (!isRunning) return false;
    if (hoveredClientId !== null || hoveredApId !== null) {
      // Only dim clients that are not highlighted to keep infrastructure visible
      return node.type === 'client' && !shouldHighlight;
    }
    return false; // Nothing hovered, so no dimming
  }, [hoveredClientId, hoveredApId, shouldHighlight, isRunning, node.type]);




  return (
    <div
      ref={nodeRef}
      data-node-id={node.id}
      className={`network-node absolute transform -translate-x-1/2 -translate-y-1/2 flex flex-col items-center group transition-all duration-200 
        ${canDrag ? 'cursor-move' : ''} 
        ${shouldHighlight ? 'scale-110' : ''} 
        ${shouldDim ? 'opacity-30' : ''}`}
      style={{
        left: `${node.x}%`,
        top: `${node.y}%`,
        // Add smooth transition for position changes during simulation
        transition: isRunning && node.type === 'client'
          ? 'left 1s ease-in-out, top 1s ease-in-out, transform 0.2s, opacity 0.2s, scale 0.2s'
          : 'transform 0.2s, opacity 0.2s, scale 0.2s'
      }}
      onMouseDown={(e) => onMouseDown(e, node)}
      onMouseEnter={handleMouseEnter}
      onMouseLeave={handleMouseLeave}
    >
      {/* Signal Animation for APs */}
      {isRunning && node.type === 'ap' && (
        <>
          <div className="absolute top-1/2 left-1/2 w-48 h-48 border border-blue-400/30 rounded-full animate-radiate pointer-events-none -translate-x-1/2 -translate-y-1/2"></div>
          <div className="absolute top-1/2 left-1/2 w-48 h-48 border border-blue-400/20 rounded-full animate-radiate pointer-events-none [animation-delay:0.5s] -translate-x-1/2 -translate-y-1/2"></div>
        </>
      )}


      {/* Interference Animation */}
      {isRunning && (node.type === 'microwave' || node.type === 'ble' || node.type === 'zigbee') && (
        <div className="absolute -top-8 w-8 h-8 rounded-full bg-orange-500/20 text-orange-500 animate-pulse flex items-center justify-center">
          <Zap size={24} className="fill-current" />
        </div>
      )}

      {/* Client QoE Halo */}
      {isRunning && node.type === 'client' && (
        <div className={`absolute w-16 h-16 rounded-full opacity-20 animate-pulse-slow pointer-events-none
          ${node.status === 'good' ? 'bg-green-500' : node.status === 'poor' ? 'bg-red-500' : 'bg-yellow-500'}
        `}></div>
      )}

      {/* Main Node Icon */}
      <div className={`
        relative z-10 ${node.type === 'ap' ? 'w-14 h-14 rounded-3xl' : node.type === 'client' ? 'w-10 h-10 rounded-lg' : 'w-12 h-12 rounded-3xl'} 
        ${node.type === 'client' ? 'border' : 'border-2'} shadow-lg flex items-center justify-center
        ${getNodeStyle(node)}
        ${isRunning && (node.type === 'microwave' || node.type === 'ble') ? 'animate-jam' : ''}
      `}>
        {node.type === 'ap' ? (
          <Wifi size={24} />
        ) : (
          renderIcon(node.type, node.type === 'client' ? 20 : 24)
        )}

        {/* Badge for Clients */}
        {node.type === 'client' && (
          <div className={`absolute -top-1 -right-1 w-3 h-3 rounded-full border-2 border-card
            ${node.status === 'good' ? 'bg-green-500' : node.status === 'poor' ? 'bg-red-500' : 'bg-yellow-400'}
          `}></div>
        )}

        {/* DELETE BUTTON (Visible on hover) */}
        {!isRunning && (
          <button
            onClick={(e) => { e.stopPropagation(); onDelete(String(node.id)); }}
            className="absolute -top-3 -right-3 bg-destructive text-destructive-foreground rounded-full p-1 shadow-md opacity-0 group-hover:opacity-100 transition-all hover:bg-destructive/90 hover:scale-110 z-50"
          >
            <X size={12} />
          </button>
        )}
      </div>

      {/* Label */}
      <div className="mt-2 px-2 py-0.5 bg-card/90 backdrop-blur-sm rounded border border-border shadow-sm text-[10px] font-semibold text-card-foreground whitespace-nowrap z-20 pointer-events-none">
        {node.name}
        {isRunning && node.type === 'ap' && <span className="ml-1 text-primary">Ch:{node.channel}</span>}
      </div>

      {/* Hover Card - positioned above the node */}
      {(node.type === 'ap' || node.type === 'client') && (
        <NodeHoverCard
          node={node}
          nodeType={node.type}
          isVisible={isHovered}
          position={{ x: node.x, y: node.y }}
          nodeElement={nodeRef.current}
          isRunning={isRunning}
        />
      )}
    </div>
  );
};
