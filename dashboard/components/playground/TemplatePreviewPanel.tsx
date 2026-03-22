import React, { useMemo } from 'react';
import { Wifi, Smartphone, Microwave, Bluetooth, Radio } from 'lucide-react';
import { Template, NetworkNode, NodeType } from '@/types';

interface TemplatePreviewPanelProps {
  hoveredTemplate: string | null;
  filterType: 'saved' | 'default';
  allTemplates: Template[];
  savedTemplates: Template[];
}

// Helper function to render node icons
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

// Helper function to get node styles
const getNodeStyle = (node: NetworkNode) => {
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

export const TemplatePreviewPanel: React.FC<TemplatePreviewPanelProps> = ({
  hoveredTemplate,
  filterType,
  allTemplates,
  savedTemplates
}) => {
  return (
    <div className="flex-1 h-full pl-8 overflow-hidden flex flex-col min-w-0 pt-5 dark:bg-muted/50">
      <div className="p-4 pl-0 border-b border-border shrink-0">
        <h3 className="font-semibold text-lg text-foreground">
          {(() => {
            // Determine which template to show in preview
            let previewTemplate: Template | undefined;
            if (hoveredTemplate === 'empty') {
              return 'Blank Canvas Preview';
            } else if (hoveredTemplate) {
              // Check both allTemplates and savedTemplates
              previewTemplate = allTemplates.find((t: Template) => t.id === hoveredTemplate) 
                || savedTemplates.find((t: Template) => t.id === hoveredTemplate);
            } else if (filterType === 'saved' && savedTemplates.length > 0) {
              // Show first saved template when viewing saved templates and nothing is hovered
              previewTemplate = savedTemplates[0];
            }
            return previewTemplate ? `${previewTemplate.name} Preview` : 'Blank Canvas Preview';
          })()}
        </h3>
      </div>
      <div className="flex-1 overflow-hidden relative bg-muted/50 min-h-0">
        {/* Scaled Background Grid - Creates scrollable height */}
        {(() => {
          // Determine which template to show in preview
          let previewTemplate: Template | undefined;
          if (hoveredTemplate && hoveredTemplate !== 'empty') {
            previewTemplate = allTemplates.find((t: Template) => t.id === hoveredTemplate) 
              || savedTemplates.find((t: Template) => t.id === hoveredTemplate);
          } else if (!hoveredTemplate && filterType === 'saved' && savedTemplates.length > 0) {
            previewTemplate = savedTemplates[0];
          }
          
          if (!previewTemplate || !previewTemplate.nodes || previewTemplate.nodes.length === 0) {
            return (
              <div className="absolute inset-0 opacity-30 pointer-events-none"
                style={{
                  backgroundImage: 'radial-gradient(#94a3b8 1.5px, transparent 1.5px)',
                  backgroundSize: '24px 24px',
                  minHeight: '100%'
                }}
              ></div>
            );
          }
          
          // Calculate bounds and scale to fit all nodes
          const nodes = previewTemplate.nodes;
          
          if (nodes.length === 0) {
            return (
              <div className="absolute inset-0 opacity-30 pointer-events-none"
                style={{
                  backgroundImage: 'radial-gradient(#94a3b8 1.5px, transparent 1.5px)',
                  backgroundSize: '24px 24px',
                  width: '100%',
                  height: '100%'
                }}
              ></div>
            );
          }
          
          // Calculate bounds to fit all nodes
          const minX = Math.min(...nodes.map(n => n.x));
          const maxX = Math.max(...nodes.map(n => n.x));
          const minY = Math.min(...nodes.map(n => n.y));
          const maxY = Math.max(...nodes.map(n => n.y));
          
          // Add padding around bounds
          const padding = 15;
          const viewMinX = Math.max(0, minX - padding);
          const viewMaxX = Math.min(100, maxX + padding);
          const viewMinY = Math.max(0, minY - padding);
          const viewMaxY = Math.min(100, maxY + padding);
          
          const viewWidth = viewMaxX - viewMinX;
          const viewHeight = viewMaxY - viewMinY;
          
          // Scale to fit viewport in container
          const scaleX = 100 / viewWidth;
          const scaleY = 100 / viewHeight;
          const scale = Math.min(scaleX, scaleY, 0.75);
          
          // Center point of viewport
          const viewCenterX = (viewMinX + viewMaxX) / 2;
          const viewCenterY = (viewMinY + viewMaxY) / 2;
          
          return (
            <>
              <div 
                className="absolute inset-0 opacity-30 pointer-events-none"
                style={{
                  backgroundImage: 'radial-gradient(#94a3b8 1.5px, transparent 1.5px)',
                  backgroundSize: '24px 24px',
                  width: '100%',
                  height: '100%'
                }}
              ></div>
              
              {/* Viewport container */}
              <div 
                className="absolute top-1/2 left-1/2"
                style={{ 
                  width: '100%',
                  height: '100%',
                  transform: `translate(-50%, -50%) scale(${scale}) translate(${(50 - viewCenterX) / scale}%, ${(50 - viewCenterY) / scale}%)`,
                  transformOrigin: 'center center'
                }}
              >
                {/* Full canvas */}
                <div style={{ position: 'relative', width: '100%', height: '100%' }}>
                  {/* Preview Nodes */}
                  {nodes.map((node: NetworkNode) => (
                    <div 
                      key={node.id}
                      className="network-node absolute flex flex-col items-center"
                      style={{ 
                        left: `${node.x}%`, 
                        top: `${node.y}%`, 
                        transform: 'translate(-50%, -50%)'
                      }}
                    >
                      {/* Main Node Icon */}
                      <div className={`
                        relative z-10 ${node.type === 'ap' ? 'w-14 h-14 rounded-3xl' : node.type === 'client' ? 'w-10 h-10 rounded-lg' : 'w-12 h-12 rounded-xl'} ${node.type === 'client' ? 'border' : 'border-2'} shadow-lg flex items-center justify-center
                        ${getNodeStyle(node)}
                      `}>
                        {node.type === 'ap' ? (
                          <Wifi size={24} />
                        ) : (
                          renderIcon(node.type, node.type === 'client' ? 20 : 24)
                        )}
                      </div>

                      {/* Label */}
                      <div className="mt-2 px-2 py-0.5 bg-card/90 backdrop-blur-sm rounded border border-border shadow-sm text-[10px] font-semibold text-card-foreground whitespace-nowrap z-20 pointer-events-none">
                        {node.name}
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            </>
          );
        })()}
      </div>
      <div className="p-4 border-t border-border">
        {(() => {
          // Determine which template to show stats for
          let previewTemplate: Template | undefined;
          if (hoveredTemplate === 'empty') {
            return (
              <div className="text-sm text-muted-foreground">
                <span className="font-medium">Nodes:</span> 0 · <span className="font-medium">APs:</span> 0 · <span className="font-medium">Clients:</span> 0
              </div>
            );
          } else if (hoveredTemplate) {
            // Check both allTemplates and savedTemplates
            previewTemplate = allTemplates.find((t: Template) => t.id === hoveredTemplate) 
              || savedTemplates.find((t: Template) => t.id === hoveredTemplate);
            if (previewTemplate) {
              return (
                <div className="text-sm text-muted-foreground">
                  <span className="font-medium">Nodes:</span> {previewTemplate.nodes.length} · <span className="font-medium">APs:</span> {previewTemplate.nodes.filter((n: NetworkNode) => n.type === 'ap').length} · <span className="font-medium">Clients:</span> {previewTemplate.nodes.filter((n: NetworkNode) => n.type === 'client').length}
                </div>
              );
            }
          } else if (filterType === 'saved' && savedTemplates.length > 0) {
            // Show first saved template when viewing saved templates and nothing is hovered
            previewTemplate = savedTemplates[0];
            return (
              <div className="text-sm text-muted-foreground">
                <span className="font-medium">Nodes:</span> {previewTemplate.nodes.length} · <span className="font-medium">APs:</span> {previewTemplate.nodes.filter((n: NetworkNode) => n.type === 'ap').length} · <span className="font-medium">Clients:</span> {previewTemplate.nodes.filter((n: NetworkNode) => n.type === 'client').length}
              </div>
            );
          }
          return (
            <div className="text-sm text-muted-foreground">
              <span className="font-medium">Nodes:</span> 0 · <span className="font-medium">APs:</span> 0 · <span className="font-medium">Clients:</span> 0
            </div>
          );
        })()}
      </div>
    </div>
  );
};

