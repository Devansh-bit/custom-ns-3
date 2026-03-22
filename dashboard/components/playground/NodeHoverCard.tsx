import React from 'react';
import { createPortal } from 'react-dom';
import { NetworkNode, NodeType, APMetrics } from '@/lib/types';
import { fetchSimulationAPMetrics, fetchSimulationClientMetrics, ClientMetrics } from '@/lib/api-playground';

interface NodeHoverCardProps {
  node: NetworkNode;
  nodeType: NodeType;
  isVisible: boolean;
  position: { x: number; y: number };
  nodeElement?: HTMLElement | null;
  isRunning?: boolean;
}

export const NodeHoverCard: React.FC<NodeHoverCardProps> = ({
  node,
  nodeType,
  isVisible,
  position,
  nodeElement,
  isRunning = false
}) => {
  const [apMetrics, setApMetrics] = React.useState<APMetrics | null>(null);
  const [clientMetrics, setClientMetrics] = React.useState<ClientMetrics | null>(null);
  const [loading, setLoading] = React.useState(false);
  const [error, setError] = React.useState<string | null>(null);
  const [screenPosition, setScreenPosition] = React.useState<{ x: number; y: number } | null>(null);

  // Calculate screen position from canvas position
  React.useEffect(() => {
    if (!isVisible || !nodeElement || !isRunning) {
      setScreenPosition(null);
      return;
    }

    const updatePosition = () => {
      if (!nodeElement) return;
      const rect = nodeElement.getBoundingClientRect();
      // Get the center of the node
      const centerX = rect.left + rect.width / 2;
      const centerY = rect.top + rect.height / 2;
      
      // Position above and to the right
      setScreenPosition({
        x: centerX + 50, // Shift right more
        y: centerY - 16  // Position above
      });
    };

    // Use requestAnimationFrame to ensure DOM is updated
    const rafId = requestAnimationFrame(() => {
      updatePosition();
    });
    
    // Update on scroll/resize
    window.addEventListener('scroll', updatePosition, true);
    window.addEventListener('resize', updatePosition);
    
    return () => {
      cancelAnimationFrame(rafId);
      window.removeEventListener('scroll', updatePosition, true);
      window.removeEventListener('resize', updatePosition);
    };
  }, [isVisible, nodeElement, isRunning]);

  React.useEffect(() => {
    if (!isVisible || !node.id || !isRunning) {
      setApMetrics(null);
      setClientMetrics(null);
      setLoading(false);
      setError(null);
      return;
    }

    let cancelled = false;
    const loadMetrics = async () => {
      setLoading(true);
      setError(null);
      try {
        if (nodeType === 'ap') {
          const metrics = await fetchSimulationAPMetrics(node.id!);
          if (!cancelled) {
            setApMetrics(metrics);
          }
        } else if (nodeType === 'client') {
          const metrics = await fetchSimulationClientMetrics(node.id!);
          if (!cancelled) {
            setClientMetrics(metrics);
          }
        }
      } catch (e: any) {
        if (!cancelled) {
          setError(e?.message || 'Failed to load metrics');
        }
      } finally {
        if (!cancelled) {
          setLoading(false);
        }
      }
    };

    loadMetrics();
    return () => {
      cancelled = true;
    };
  }, [isVisible, nodeType, node.id, isRunning]);

  // Don't show card when simulation is not running or not visible
  if (!isRunning || !isVisible || !screenPosition) return null;

  const cardStyle: React.CSSProperties = {
    position: 'fixed',
    left: `${screenPosition.x}px`,
    top: `${screenPosition.y}px`,
    transform: 'translate(-30%, -100%)', // Position above, shifted right
    zIndex: 9999,
    pointerEvents: 'none',
  };

  const cardContent = nodeType === 'ap' ? (
    <>
      <div className="text-xs font-semibold text-popover-foreground mb-1.5">AP Metrics</div>
      {!isRunning ? (
        <div className="space-y-1 text-xs">
          <div className="flex justify-between">
            <span className="text-muted-foreground">Name:</span>
            <span className="text-popover-foreground font-medium">{node.name}</span>
          </div>
          {node.channel && (
            <div className="flex justify-between">
              <span className="text-muted-foreground">Channel:</span>
              <span className="text-popover-foreground font-medium">{node.channel}</span>
            </div>
          )}
          {node.power && (
            <div className="flex justify-between">
              <span className="text-muted-foreground">Power:</span>
              <span className="text-popover-foreground font-medium">{node.power} dBm</span>
            </div>
          )}
          <div className="text-xs text-muted-foreground mt-2">Start simulation to see live metrics</div>
        </div>
      ) : loading ? (
        <div className="text-xs text-muted-foreground">Loading...</div>
      ) : error ? (
        <div className="text-xs text-destructive">{error}</div>
      ) : apMetrics ? (
        <div className="space-y-1 text-xs">
          <div className="flex justify-between">
            <span className="text-muted-foreground">Clients:</span>
            <span className="text-popover-foreground font-medium">{apMetrics.count_clients ?? '—'}</span>
          </div>
          <div className="flex justify-between">
            <span className="text-muted-foreground">Channel:</span>
            <span className="text-popover-foreground font-medium">{apMetrics.channel ?? '—'}</span>
          </div>
          <div className="flex justify-between">
            <span className="text-muted-foreground">TX Power:</span>
            <span className="text-popover-foreground font-medium">
              {apMetrics.phy_tx_power_level ? `${apMetrics.phy_tx_power_level} dBm` : '—'}
            </span>
          </div>
          <div className="flex justify-between">
            <span className="text-muted-foreground">Throughput:</span>
            <span className="text-popover-foreground font-medium">
              {apMetrics.uplink_throughput_mbps && apMetrics.downlink_throughput_mbps
                ? `${(apMetrics.uplink_throughput_mbps + apMetrics.downlink_throughput_mbps).toFixed(2)} Mbps`
                : '—'}
            </span>
          </div>
          <div className="flex justify-between">
            <span className="text-muted-foreground">Channel Width:</span>
            <span className="text-popover-foreground font-medium">{apMetrics.channel_width ?? '—'} MHz</span>
          </div>
        </div>
      ) : (
        <div className="text-xs text-muted-foreground">No metrics available</div>
      )}
    </>
  ) : nodeType === 'client' ? (
    <>
      <div className="text-xs font-semibold text-popover-foreground mb-1.5">Client Metrics</div>
      {!isRunning ? (
        <div className="space-y-1 text-xs">
          <div className="flex justify-between">
            <span className="text-muted-foreground">Name:</span>
            <span className="text-popover-foreground font-medium">{node.name}</span>
          </div>
          <div className="text-xs text-muted-foreground mt-2">Start simulation to see live metrics</div>
        </div>
      ) : loading ? (
        <div className="text-xs text-muted-foreground">Loading...</div>
      ) : error ? (
        <div className="text-xs text-destructive">{error}</div>
      ) : clientMetrics ? (
        <div className="space-y-1 text-xs">
          <div className="flex justify-between">
            <span className="text-muted-foreground">Latency:</span>
            <span className="text-popover-foreground font-medium">
              {clientMetrics.latency !== undefined ? `${clientMetrics.latency.toFixed(2)} ms` : '—'}
            </span>
          </div>
          <div className="flex justify-between">
            <span className="text-muted-foreground">Jitter:</span>
            <span className="text-popover-foreground font-medium">
              {clientMetrics.jitter !== undefined ? `${clientMetrics.jitter.toFixed(2)} ms` : '—'}
            </span>
          </div>
          <div className="flex justify-between">
            <span className="text-muted-foreground">Packet Loss:</span>
            <span className="text-popover-foreground font-medium">
              {clientMetrics.packet_loss_rate !== undefined ? `${(clientMetrics.packet_loss_rate * 100).toFixed(2)}%` : '—'}
            </span>
          </div>
          <div className="flex justify-between">
            <span className="text-muted-foreground">RSSI:</span>
            <span className="text-popover-foreground font-medium">
              {clientMetrics.rssi !== undefined ? `${clientMetrics.rssi.toFixed(0)} dBm` : '—'}
            </span>
          </div>
          <div className="flex justify-between">
            <span className="text-muted-foreground">SNR:</span>
            <span className="text-popover-foreground font-medium">
              {clientMetrics.snr !== undefined ? `${clientMetrics.snr.toFixed(0)} dB` : '—'}
            </span>
          </div>
        </div>
      ) : (
        <div className="text-xs text-muted-foreground">No metrics available</div>
      )}
    </>
  ) : null;

  if (!cardContent) return null;

  const card = (
    <div
      className="bg-popover border border-border rounded-lg shadow-lg p-2 min-w-fit z-[9999]"
      style={cardStyle}
    >
      {cardContent}
    </div>
  );

  // Use portal to render outside the transform container
  if (typeof window !== 'undefined') {
    return createPortal(card, document.body);
  }

  return null;
};

