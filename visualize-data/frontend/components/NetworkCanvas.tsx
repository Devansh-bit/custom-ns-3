'use client';

import { useEffect, useRef, useCallback } from 'react';
import { NetworkData, AP, STA, HoveredNode } from '@/types/network';

const LERP_FACTOR = 0.08;
const AP_RADIUS = 24;
const STA_RADIUS = 10;
const PADDING = 80;

// Channel colors
const CHANNEL_COLORS: Record<number, string> = {
  1: '#ff6b6b', 6: '#feca57', 11: '#48dbfb',
  36: '#1dd1a1', 40: '#5f27cd', 44: '#ff9ff3', 48: '#54a0ff',
  52: '#00d2d3', 56: '#ff6b6b', 60: '#feca57', 64: '#48dbfb',
  100: '#1dd1a1', 104: '#5f27cd', 108: '#ff9ff3', 112: '#54a0ff',
  149: '#00d2d3', 153: '#ff6b6b', 157: '#feca57', 161: '#48dbfb', 165: '#1dd1a1',
};

function getChannelColor(channel: number): string {
  return CHANNEL_COLORS[channel] || '#888888';
}

function lerp(current: number, target: number): number {
  return current + (target - current) * LERP_FACTOR;
}

interface AnimatedNode {
  displayX: number;
  displayY: number;
  targetX: number;
  targetY: number;
}

interface Props {
  data: NetworkData | null;
  onHover: (node: HoveredNode, x: number, y: number) => void;
}

export default function NetworkCanvas({ data, onHover }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const animationRef = useRef<number>(0);
  const nodesRef = useRef<Map<string, AnimatedNode>>(new Map());
  const transformRef = useRef({ scale: 1, offsetX: 0, offsetY: 0 });

  // Update targets when data changes
  useEffect(() => {
    if (!data) return;

    const nodes = nodesRef.current;

    // Update AP positions
    data.aps.forEach((ap) => {
      const key = `ap-${ap.bssid}`;
      const existing = nodes.get(key);
      if (existing) {
        existing.targetX = ap.x;
        existing.targetY = ap.y;
      } else {
        nodes.set(key, {
          displayX: ap.x,
          displayY: ap.y,
          targetX: ap.x,
          targetY: ap.y,
        });
      }
    });

    // Update STA positions
    data.stas.forEach((sta) => {
      const key = `sta-${sta.address}`;
      const existing = nodes.get(key);
      if (existing) {
        existing.targetX = sta.x;
        existing.targetY = sta.y;
      } else {
        nodes.set(key, {
          displayX: sta.x,
          displayY: sta.y,
          targetX: sta.x,
          targetY: sta.y,
        });
      }
    });
  }, [data]);

  // Calculate transform
  const calculateTransform = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas || !data) return;

    const allNodes = [...data.aps, ...data.stas];
    if (allNodes.length === 0) {
      transformRef.current = { scale: 1, offsetX: canvas.width / 2, offsetY: canvas.height / 2 };
      return;
    }

    let minX = Infinity, maxX = -Infinity;
    let minY = Infinity, maxY = -Infinity;

    allNodes.forEach((node) => {
      minX = Math.min(minX, node.x);
      maxX = Math.max(maxX, node.x);
      minY = Math.min(minY, node.y);
      maxY = Math.max(maxY, node.y);
    });

    const rangeX = maxX - minX || 1;
    const rangeY = maxY - minY || 1;
    const dpr = window.devicePixelRatio || 1;
    const width = canvas.width / dpr;
    const height = canvas.height / dpr;

    const scaleX = (width - PADDING * 2) / rangeX;
    const scaleY = (height - PADDING * 2) / rangeY;
    const scale = Math.min(scaleX, scaleY);

    const centerX = (minX + maxX) / 2;
    const centerY = (minY + maxY) / 2;

    transformRef.current = {
      scale,
      offsetX: width / 2 - centerX * scale,
      offsetY: height / 2 - centerY * scale,
    };
  }, [data]);

  // World to screen coordinates
  const worldToScreen = useCallback((x: number, y: number) => {
    const t = transformRef.current;
    return {
      x: x * t.scale + t.offsetX,
      y: y * t.scale + t.offsetY,
    };
  }, []);

  // Animation loop
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const resize = () => {
      const dpr = window.devicePixelRatio || 1;
      canvas.width = window.innerWidth * dpr;
      canvas.height = window.innerHeight * dpr;
      canvas.style.width = `${window.innerWidth}px`;
      canvas.style.height = `${window.innerHeight}px`;
      ctx.scale(dpr, dpr);
      calculateTransform();
    };

    resize();
    window.addEventListener('resize', resize);

    const animate = () => {
      const dpr = window.devicePixelRatio || 1;
      const width = canvas.width / dpr;
      const height = canvas.height / dpr;

      // Clear with gradient background
      const gradient = ctx.createLinearGradient(0, 0, 0, height);
      gradient.addColorStop(0, '#0a0a12');
      gradient.addColorStop(1, '#12121f');
      ctx.fillStyle = gradient;
      ctx.fillRect(0, 0, width, height);

      // Draw grid
      drawGrid(ctx, width, height);

      if (data) {
        calculateTransform();
        const nodes = nodesRef.current;

        // Interpolate all positions
        nodes.forEach((node) => {
          node.displayX = lerp(node.displayX, node.targetX);
          node.displayY = lerp(node.displayY, node.targetY);
        });

        // Draw connections
        data.stas.forEach((sta) => {
          const ap = data.aps.find((a) => a.bssid === sta.connected_ap);
          if (!ap) return;

          const staNode = nodes.get(`sta-${sta.address}`);
          const apNode = nodes.get(`ap-${ap.bssid}`);
          if (!staNode || !apNode) return;

          const staPos = worldToScreen(staNode.displayX, staNode.displayY);
          const apPos = worldToScreen(apNode.displayX, apNode.displayY);

          const rssi = sta.rssi || -70;
          const opacity = Math.max(0.2, Math.min(0.8, (rssi + 90) / 60));

          const lineGradient = ctx.createLinearGradient(staPos.x, staPos.y, apPos.x, apPos.y);
          lineGradient.addColorStop(0, `rgba(79, 143, 255, ${opacity * 0.5})`);
          lineGradient.addColorStop(1, `rgba(79, 143, 255, ${opacity})`);

          ctx.strokeStyle = lineGradient;
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.moveTo(staPos.x, staPos.y);
          ctx.lineTo(apPos.x, apPos.y);
          ctx.stroke();
        });

        // Draw APs
        data.aps.forEach((ap) => {
          const node = nodes.get(`ap-${ap.bssid}`);
          if (!node) return;

          const pos = worldToScreen(node.displayX, node.displayY);
          const color = getChannelColor(ap.channel);

          // Outer glow
          ctx.beginPath();
          ctx.arc(pos.x, pos.y, AP_RADIUS + 12, 0, Math.PI * 2);
          ctx.fillStyle = `${color}22`;
          ctx.fill();

          // Inner glow
          ctx.beginPath();
          ctx.arc(pos.x, pos.y, AP_RADIUS + 4, 0, Math.PI * 2);
          ctx.fillStyle = `${color}44`;
          ctx.fill();

          // Main circle with gradient
          const apGradient = ctx.createRadialGradient(
            pos.x - AP_RADIUS / 3, pos.y - AP_RADIUS / 3, 0,
            pos.x, pos.y, AP_RADIUS
          );
          apGradient.addColorStop(0, `${color}ff`);
          apGradient.addColorStop(1, `${color}aa`);

          ctx.beginPath();
          ctx.arc(pos.x, pos.y, AP_RADIUS, 0, Math.PI * 2);
          ctx.fillStyle = apGradient;
          ctx.fill();

          // Border
          ctx.strokeStyle = 'rgba(255, 255, 255, 0.5)';
          ctx.lineWidth = 2;
          ctx.stroke();

          // Channel label
          ctx.fillStyle = '#fff';
          ctx.font = 'bold 11px system-ui';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          ctx.fillText(`CH${ap.channel}`, pos.x, pos.y);

          // AP ID
          ctx.font = '10px system-ui';
          ctx.fillStyle = 'rgba(255, 255, 255, 0.7)';
          ctx.fillText(`AP${ap.id}`, pos.x, pos.y + AP_RADIUS + 14);
        });

        // Draw STAs
        data.stas.forEach((sta) => {
          const node = nodes.get(`sta-${sta.address}`);
          if (!node) return;

          const pos = worldToScreen(node.displayX, node.displayY);

          // Glow
          ctx.beginPath();
          ctx.arc(pos.x, pos.y, STA_RADIUS + 6, 0, Math.PI * 2);
          ctx.fillStyle = 'rgba(255, 255, 255, 0.15)';
          ctx.fill();

          // Main circle
          const staGradient = ctx.createRadialGradient(
            pos.x - STA_RADIUS / 3, pos.y - STA_RADIUS / 3, 0,
            pos.x, pos.y, STA_RADIUS
          );
          staGradient.addColorStop(0, '#ffffff');
          staGradient.addColorStop(1, '#cccccc');

          ctx.beginPath();
          ctx.arc(pos.x, pos.y, STA_RADIUS, 0, Math.PI * 2);
          ctx.fillStyle = staGradient;
          ctx.fill();

          // Border
          ctx.strokeStyle = 'rgba(79, 143, 255, 0.5)';
          ctx.lineWidth = 1.5;
          ctx.stroke();
        });
      }

      animationRef.current = requestAnimationFrame(animate);
    };

    animate();

    return () => {
      window.removeEventListener('resize', resize);
      cancelAnimationFrame(animationRef.current);
    };
  }, [data, calculateTransform, worldToScreen]);

  // Mouse hover detection
  const handleMouseMove = useCallback(
    (e: React.MouseEvent<HTMLCanvasElement>) => {
      if (!data) return;

      const rect = canvasRef.current?.getBoundingClientRect();
      if (!rect) return;

      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;
      const nodes = nodesRef.current;

      // Check STAs first
      for (const sta of data.stas) {
        const node = nodes.get(`sta-${sta.address}`);
        if (!node) continue;
        const pos = worldToScreen(node.displayX, node.displayY);
        const dist = Math.hypot(pos.x - x, pos.y - y);
        if (dist <= STA_RADIUS + 4) {
          onHover({ ...sta, type: 'sta' }, e.clientX, e.clientY);
          return;
        }
      }

      // Check APs
      for (const ap of data.aps) {
        const node = nodes.get(`ap-${ap.bssid}`);
        if (!node) continue;
        const pos = worldToScreen(node.displayX, node.displayY);
        const dist = Math.hypot(pos.x - x, pos.y - y);
        if (dist <= AP_RADIUS + 4) {
          onHover({ ...ap, type: 'ap' }, e.clientX, e.clientY);
          return;
        }
      }

      onHover(null, 0, 0);
    },
    [data, onHover, worldToScreen]
  );

  return (
    <canvas
      ref={canvasRef}
      onMouseMove={handleMouseMove}
      onMouseLeave={() => onHover(null, 0, 0)}
      style={{ display: 'block', cursor: 'crosshair' }}
    />
  );
}

function drawGrid(ctx: CanvasRenderingContext2D, width: number, height: number) {
  ctx.strokeStyle = 'rgba(79, 143, 255, 0.08)';
  ctx.lineWidth = 1;

  const spacing = 50;

  for (let x = 0; x < width; x += spacing) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
  }

  for (let y = 0; y < height; y += spacing) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }
}
