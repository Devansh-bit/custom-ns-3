import React, { useState } from 'react';
import { Wifi, Smartphone, Microwave, Bluetooth, Radio, Info, BarChart3, Undo2, Redo2, RotateCcw, Monitor, Laptop, Tablet, Tv, Watch, Car } from 'lucide-react';
import { ToolItem, NodeType } from '@/types';

interface TopBarProps {
  selectedTool: NodeType | null;
  onSelectTool: (tool: NodeType | null) => void;
  onUndo?: () => void;
  onRedo?: () => void;
  onReset?: () => void;
  canUndo?: boolean;
  canRedo?: boolean;
  canReset?: boolean;
  hasActiveTemplate?: boolean;
  disabled?: boolean;
}

const TOOLS: ToolItem[] = [
  { type: 'ap', label: 'AP', icon: Wifi, category: 'Infrastructure', description: 'Access Point with dedicated scanning radio' },
  { type: 'client', label: 'Client', icon: Smartphone, category: 'Clients', description: 'WiFi Client (Phone/Laptop/IoT)' },
  { type: 'ble', label: 'BLE', icon: Bluetooth, category: 'Interferers', description: 'Bluetooth Low Energy Source' },
  { type: 'zigbee', label: 'Zigbee', icon: Radio, category: 'Interferers', description: 'Zigbee Smart Device' },
  { type: 'microwave', label: 'Microwave', icon: Microwave, category: 'Interferers', description: 'High-power 2.4GHz Interferer' },
];

export const TopBar: React.FC<TopBarProps> = ({ selectedTool, onSelectTool, onUndo, onRedo, onReset, canUndo = false, canRedo = false, canReset = false, hasActiveTemplate = false, disabled = false }) => {
  const [hoveredTool, setHoveredTool] = useState<string | null>(null);
  const [hoveredClientType, setHoveredClientType] = useState<'static' | 'mobile' | null>(null);
  const [hoveredClientSubtype, setHoveredClientSubtype] = useState<string | null>(null);

  // Client subtypes
  const staticClientTypes = [
    { id: 'desktop', label: 'Desktop', icon: Monitor },
    { id: 'laptop', label: 'Laptop', icon: Laptop },
    { id: 'tv', label: 'Smart TV', icon: Tv },
  ];

  const mobileClientTypes = [
    { id: 'phone', label: 'Phone', icon: Smartphone },
    { id: 'tablet', label: 'Tablet', icon: Tablet },
    { id: 'watch', label: 'Smart Watch', icon: Watch },
    { id: 'iot', label: 'IoT Device', icon: Radio },
  ];

  // Group tools by category
  const infraTools = TOOLS.filter(t => t.category === 'Infrastructure');
  const clientTools = TOOLS.filter(t => t.category === 'Clients');
  const interfererTools = TOOLS.filter(t => t.category === 'Interferers');

  const renderToolGroup = (tools: ToolItem[], label: string, isFirstGroup?: boolean) => (
    <div className={`flex items-center gap-2 border-r-0 ${isFirstGroup ? 'pr-0' : 'pr-8'}`}>
      {tools.map((tool) => (
        <div key={tool.type} className="relative group">
          <button
            onClick={() => !disabled && onSelectTool(selectedTool === tool.type ? null : tool.type)}
            onMouseEnter={() => !disabled && setHoveredTool(tool.type)}
            onMouseLeave={() => setHoveredTool(null)}
            disabled={disabled}
            className={`
              flex items-center gap-3 px-3 py-2 rounded-lg border font-medium transition-all duration-200 text-sm
              ${disabled 
                ? 'bg-slate-50 border-slate-200 text-slate-400 cursor-not-allowed opacity-50'
                : selectedTool === tool.type 
                  ? 'bg-blue-500 border-blue-500 text-white shadow-inner ring-2 ring-blue-400/50' 
                  : 'bg-slate-100 border-slate-300 text-slate-700 hover:border-blue-500 hover:text-white hover:bg-slate-200'
              }
            `}
          >
            <tool.icon size={16} />
            <span>{tool.label}</span>
          </button>

          {/* Info Tooltip */}
          {hoveredTool === tool.type && !disabled && (
            <div className="absolute top-full mt-3 left-1/2 -translate-x-1/2 w-64 bg-slate-800 text-white text-sm rounded-md shadow-xl p-3 z-50 animate-fade-in pointer-events-none">
              <div className="absolute -top-1 left-1/2 -translate-x-1/2 w-2 h-2 bg-slate-800 rotate-45"></div>
              <div className="flex items-start gap-2">
                <Info size={16} className="mt-0.5 text-brand-accent shrink-0" />
                <div>
                  <p className="font-semibold text-brand-accent mb-1">{tool.category}</p>
                  <p className="text-slate-300 leading-snug text-xs">{tool.description}</p>
                </div>
              </div>
            </div>
          )}
        </div>
      ))}
    </div>
  );

  return (
    <div className="w-full flex justify-between z-20 relative shrink-0">
      <div className="flex-1 p-3 flex  justify-between">
        <div className="flex  gap-4 overflow-x-auto no-scrollbar flex-1">

                      {/* Undo/Redo/Reset Buttons */}
                      {hasActiveTemplate && (
              <div className="flex items-center gap-2 ml-2 pl-2 pr-65">
                <button
                  onClick={onUndo}
                  disabled={!canUndo || disabled}
                  className={`
                    flex items-center justify-center px-2 py-1 rounded-lg border font-medium transition-all duration-200 text-sm
                    ${disabled || !canUndo
                      ? 'bg-slate-50 border-slate-200 text-slate-400 cursor-not-allowed opacity-50'
                      : 'bg-slate-100 border-slate-300 text-slate-700 hover:border-orange-500 hover:text-white hover:bg-slate-200 cursor-pointer'
                    }
                  `}
                >
                  <Undo2 size={16} />
                </button>
                <button
                  onClick={onRedo}
                  disabled={!canRedo || disabled}
                  className={`
                    flex items-center justify-center px-2 py-1 rounded-lg border font-medium transition-all duration-200 text-sm
                    ${disabled || !canRedo
                      ? 'bg-slate-50 border-slate-200 text-slate-400 cursor-not-allowed opacity-50'
                      : 'bg-slate-100 border-slate-300 text-slate-700 hover:border-orange-500 hover:text-white hover:bg-slate-200 cursor-pointer'
                    }
                  `}
                >
                  <Redo2 size={16} />
                </button>
                <button
                  onClick={onReset}
                  disabled={!canReset || disabled}
                  className={`
                    flex items-center px-2 py-1 rounded-lg border font-medium transition-all duration-200 text-sm
                    ${disabled || !canReset
                      ? 'bg-slate-50 border-slate-200 text-slate-400 cursor-not-allowed opacity-50'
                      : 'bg-slate-100 border-slate-300 text-slate-700 hover:border-orange-500 hover:text-white hover:bg-slate-200 cursor-pointer'
                    }
                  `}
                >
                  <RotateCcw size={16} />
                </button>
              </div>
            )}

          {/* Tool Categories */}
          <div className="flex items-center gap-2 border-r-0">
            {renderToolGroup(infraTools, 'Infrastructure', true)}
            
            {/* Clients with Dropdown */}
            <div 
              className="relative group"
              onMouseEnter={() => !disabled && setHoveredTool('client')}
              onMouseLeave={() => {
                setHoveredTool(null);
                setHoveredClientType(null);
                setHoveredClientSubtype(null);
              }}
            >
              <button
                onClick={() => !disabled && onSelectTool(selectedTool === 'client' ? null : 'client')}
                disabled={disabled}
                className={`
                  flex items-center gap-3 px-3 py-2 rounded-lg border font-medium transition-all duration-200 text-sm
                  ${disabled
                    ? 'bg-slate-50 border-slate-200 text-slate-400 cursor-not-allowed opacity-50'
                    : selectedTool === 'client' 
                      ? 'bg-blue-500 border-blue-500 text-white shadow-inner ring-2 ring-blue-400/50' 
                      : 'bg-slate-100 border-slate-300 text-slate-700 hover:border-blue-500 hover:text-white hover:bg-slate-200'
                  }
                `}
              >
                <Smartphone size={16} />
                <span>Client</span>
              </button>

              {/* Dropdown Menu */}
              {hoveredTool === 'client' && !disabled && (
                <div className="absolute top-full left-0 mt-2 w-52 bg-white rounded-lg shadow-2xl border border-slate-200 z-50 animate-fade-in overflow-hidden">
                  {/* Static Option */}
                  <div 
                    className="relative"
                    onMouseEnter={() => setHoveredClientType('static')}
                    onMouseLeave={() => setHoveredClientType(null)}
                  >
                    <div className={`px-4 py-3 cursor-pointer border-b border-slate-100 transition-colors ${
                      hoveredClientType === 'static' ? 'bg-blue-50' : 'hover:bg-slate-50'
                    }`}>
                      <div className="flex items-center justify-between">
                        <span className="font-semibold text-slate-800">Static</span>
                        <span className="text-slate-400 text-lg">›</span>
                      </div>
                    </div>
                    
                    {/* Static Submenu */}
                    {hoveredClientType === 'static' && (
                      <div className="absolute left-full top-0 ml-2 w-52 bg-white rounded-lg shadow-2xl border border-slate-200 z-50 animate-fade-in overflow-hidden">
                        {staticClientTypes.map((subtype) => (
                          <div
                            key={subtype.id}
                            onMouseEnter={() => setHoveredClientSubtype(subtype.id)}
                            onMouseLeave={() => setHoveredClientSubtype(null)}
                            onClick={() => onSelectTool('client')}
                            className={`px-4 py-3 cursor-pointer flex items-center gap-3 border-b border-slate-100 last:border-b-0 transition-colors ${
                              hoveredClientSubtype === subtype.id ? 'bg-blue-100' : 'hover:bg-blue-50'
                            }`}
                          >
                            <subtype.icon size={18} className={`${hoveredClientSubtype === subtype.id ? 'text-blue-600' : 'text-slate-600'}`} />
                            <span className={`text-sm font-medium ${hoveredClientSubtype === subtype.id ? 'text-blue-700' : 'text-slate-700'}`}>{subtype.label}</span>
                          </div>
                        ))}
                      </div>
                    )}
                  </div>

                  {/* Mobile Option */}
                  <div 
                    className="relative"
                    onMouseEnter={() => setHoveredClientType('mobile')}
                    onMouseLeave={() => setHoveredClientType(null)}
                  >
                    <div className={`px-4 py-3 cursor-pointer transition-colors ${
                      hoveredClientType === 'mobile' ? 'bg-blue-50' : 'hover:bg-slate-50'
                    }`}>
                      <div className="flex items-center justify-between">
                        <span className="font-semibold text-slate-800">Mobile</span>
                        <span className="text-slate-400 text-lg">›</span>
                      </div>
                    </div>
                    
                    {/* Mobile Submenu */}
                    {hoveredClientType === 'mobile' && (
                      <div className="absolute left-full top-0 ml-2 w-52 bg-white rounded-lg shadow-2xl border border-slate-200 z-50 animate-fade-in overflow-hidden">
                        {mobileClientTypes.map((subtype) => (
                          <div
                            key={subtype.id}
                            onMouseEnter={() => setHoveredClientSubtype(subtype.id)}
                            onMouseLeave={() => setHoveredClientSubtype(null)}
                            onClick={() => onSelectTool('client')}
                            className={`px-4 py-3 cursor-pointer flex items-center gap-3 border-b border-slate-100 last:border-b-0 transition-colors ${
                              hoveredClientSubtype === subtype.id ? 'bg-blue-100' : 'hover:bg-blue-50'
                            }`}
                          >
                            <subtype.icon size={18} className={`${hoveredClientSubtype === subtype.id ? 'text-blue-600' : 'text-slate-600'}`} />
                            <span className={`text-sm font-medium ${hoveredClientSubtype === subtype.id ? 'text-blue-700' : 'text-slate-700'}`}>{subtype.label}</span>
                          </div>
                        ))}
                      </div>
                    )}
                  </div>
                </div>
              )}
            </div>
            {/* Distinct Interferers Block */}
            <div className="flex items-center gap-3 pl-2 bg-orange-100 py-1 px-3 rounded-lg border border-orange-300">
              <span className="text-[10px] font-bold text-orange-700 uppercase tracking-wider mr-1">Interferers</span>
              {interfererTools.map((tool) => (
                <div key={tool.type} className="relative group">
                  <button
                    onClick={() => !disabled && onSelectTool(selectedTool === tool.type ? null : tool.type)}
                    onMouseEnter={() => !disabled && setHoveredTool(tool.type)}
                    onMouseLeave={() => setHoveredTool(null)}
                    disabled={disabled}
                    className={`
                      flex items-center gap-2 px-1.5 py-0.5 rounded-lg border font-medium transition-all duration-200 text-sm
                      ${disabled
                        ? 'bg-slate-50 border-slate-200 text-slate-400 cursor-not-allowed opacity-50'
                        : selectedTool === tool.type
                          ? 'bg-orange-600 border-orange-500 text-white shadow-inner ring-2 ring-orange-400/50'
                          : 'bg-slate-100 border-slate-300 text-slate-700 hover:border-orange-500 hover:text-white hover:bg-slate-200'
                      }
                    `}
                  >
                    <tool.icon size={16} />
                    <span className="hidden lg:inline py-0.25">{tool.label}</span>
                  </button>

                  {/* Info Tooltip */}
                  {hoveredTool === tool.type && (
                    <div className="absolute top-full mt-3 left-1/2 -translate-x-1/2 w-64 bg-slate-800 text-white text-sm rounded-md shadow-xl p-3 z-50 animate-fade-in pointer-events-none">
                      <div className="absolute -top-1 left-1/2 -translate-x-1/2 w-2 h-2 bg-slate-800 rotate-45"></div>
                      <div className="flex items-start gap-2">
                        <Info size={16} className="mt-0.5 text-brand-accent shrink-0" />
                        <div>
                          <p className="font-semibold text-brand-accent mb-1">{tool.category}</p>
                          <p className="text-slate-300 leading-snug text-xs">{tool.description}</p>
                        </div>
                      </div>
                    </div>
                  )}
                </div>
              ))}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
