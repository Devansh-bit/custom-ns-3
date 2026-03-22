import React, { useState, useEffect, useRef } from 'react';
import { createPortal } from 'react-dom';
import { Layout, Building2, GraduationCap, Coffee, MessageSquare, Plus, Trash2, History, MoreHorizontal, Pencil, EllipsisVertical } from 'lucide-react';
import constantsData from '@/constants.json';
const defaultTemplates = constantsData.defaultTemplates;
import { Template, NetworkNode, Session } from '@/lib/types';

interface SidebarLeftProps {
  onCreateSession: () => void;
  onLoadSession: (session: Session) => void;
  onDeleteSession?: (sessionId: string) => void;
  onRenameSession?: (sessionId: string, newName: string) => void;
  onNavigateAway?: () => void;
  activeSessionId: string | null;
  sessions: Session[];
  disabled: boolean;
  nodes: NetworkNode[];
  templateStates: Record<string, NetworkNode[]>;
  simulationState?: 'idle' | 'building' | 'running';
}

export const SidebarLeft: React.FC<SidebarLeftProps> = ({ 
  onCreateSession,
  onLoadSession,
  onDeleteSession,
  onRenameSession,
  onNavigateAway,
  activeSessionId,
  sessions,
  disabled,
  nodes,
  templateStates,
  simulationState = 'idle'
}) => {
  const [isMounted, setIsMounted] = useState(false);
  const [editingSessionId, setEditingSessionId] = useState<string | null>(null);
  const [editingName, setEditingName] = useState('');
  const [openMenuId, setOpenMenuId] = useState<string | null>(null);
  const [menuPosition, setMenuPosition] = useState<{ top: number; left: number } | null>(null);
  const editInputRef = useRef<HTMLInputElement>(null);
  const sessionsContainerRef = useRef<HTMLDivElement>(null);
  const activeSessionRef = useRef<HTMLDivElement>(null);
  const menuRefs = useRef<Map<string, HTMLDivElement>>(new Map());

  // Only auto-open sidebar when loading an existing session, not when creating a new one
  // Track previous activeSessionId to detect when a session is loaded (not created)
  const prevActiveSessionIdRef = useRef<string | null>(null);
  
  useEffect(() => {
    // Scroll to active session after a brief delay to ensure DOM is updated
    if (activeSessionId) {
      setTimeout(() => {
        if (activeSessionRef.current && sessionsContainerRef.current) {
          activeSessionRef.current.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
        }
      }, 100);
    }
    prevActiveSessionIdRef.current = activeSessionId;
  }, [activeSessionId]);

  // Prevent hydration mismatch by only rendering counts after mount
  useEffect(() => {
    setIsMounted(true);
  }, []);

  // Calculate current counts from playground nodes (for active template)
  const currentApCount = nodes.filter(n => n.type === 'ap').length;
  const currentClientCount = nodes.filter(n => n.type === 'client').length;

  // Get saved counts for a template
  const getTemplateCounts = (templateId: string) => {
    const savedState = templateStates[templateId];
    if (savedState) {
      return {
        apCount: savedState.filter(n => n.type === 'ap').length,
        clientCount: savedState.filter(n => n.type === 'client').length
      };
    }
    return null;
  };

  const getIcon = (id: string) => {
    switch(id) {
      case 'office': return <Building2 size={16} />;
      case 'classroom': return <GraduationCap size={16} />;
      case 'cafe': return <Coffee size={16} />;
      default: return <Layout size={16} />;
    }
  };

  // Handle double-click to start editing
  const handleDoubleClick = (e: React.MouseEvent, session: Session) => {
    e.stopPropagation();
    if (disabled || !onRenameSession) return;
    setEditingSessionId(session.id);
    setEditingName(session.name);
    // Focus input after state update
    setTimeout(() => {
      editInputRef.current?.focus();
      editInputRef.current?.select();
    }, 0);
  };

  // Handle save on blur or Enter key
  const handleSaveEdit = (sessionId: string) => {
    if (!onRenameSession) return;
    const trimmedName = editingName.trim();
    if (trimmedName && trimmedName !== sessions.find(s => s.id === sessionId)?.name) {
      onRenameSession(sessionId, trimmedName);
    }
    setEditingSessionId(null);
    setEditingName('');
  };

  // Handle cancel edit
  const handleCancelEdit = () => {
    setEditingSessionId(null);
    setEditingName('');
  };

  // Focus input when editing starts
  useEffect(() => {
    if (editingSessionId && editInputRef.current) {
      editInputRef.current.focus();
      editInputRef.current.select();
    }
  }, [editingSessionId]);

  // Close menu when clicking outside or pressing Escape
  useEffect(() => {
    const handleClickOutside = (event: MouseEvent) => {
      const clickedElement = event.target as HTMLElement;
      
      // Don't close if clicking on the menu button container or the button itself
      const menuOptionsContainer = clickedElement.closest('.menu-options-button');
      if (menuOptionsContainer) {
        return;
      }
      
      // Don't close if clicking on Rename or Delete buttons - let their handlers close it
      const menuButton = clickedElement.closest('.menu-dropdown button');
      if (menuButton) {
        return;
      }
      
      // Close menu if clicking anywhere else
      setOpenMenuId(null);
      setMenuPosition(null);
    };

    const handleEscapeKey = (event: KeyboardEvent) => {
      if (event.key === 'Escape' && openMenuId) {
        setOpenMenuId(null);
        setMenuPosition(null);
      }
    };

    if (openMenuId) {
      // Use mousedown to catch clicks before they bubble
      document.addEventListener('mousedown', handleClickOutside, true);
      document.addEventListener('keydown', handleEscapeKey);
      return () => {
        document.removeEventListener('mousedown', handleClickOutside, true);
        document.removeEventListener('keydown', handleEscapeKey);
      };
    }
  }, [openMenuId]);

  const handleMenuClick = (e: React.MouseEvent, sessionId: string) => {
    e.stopPropagation();
    e.preventDefault();
    // Toggle menu: if already open for this session, close it; otherwise open it
    if (openMenuId === sessionId) {
      setOpenMenuId(null);
      setMenuPosition(null);
    } else {
      const menuElement = menuRefs.current.get(sessionId);
      if (menuElement) {
        const rect = menuElement.getBoundingClientRect();
        setMenuPosition({
          top: rect.bottom + 4,
          left: rect.right - 30
        });
      }
      setOpenMenuId(sessionId);
    }
  };

  // Update menu position when scrolling or resizing
  useEffect(() => {
    if (openMenuId && menuPosition) {
      const updatePosition = () => {
        const menuElement = menuRefs.current.get(openMenuId);
        if (menuElement) {
          const rect = menuElement.getBoundingClientRect();
          setMenuPosition({
            top: rect.bottom + 4,
            left: rect.right - 30
          });
        }
      };

      window.addEventListener('scroll', updatePosition, true);
      window.addEventListener('resize', updatePosition);
      
      // Also listen to scroll on the sessions container
      if (sessionsContainerRef.current) {
        sessionsContainerRef.current.addEventListener('scroll', updatePosition);
      }

      return () => {
        window.removeEventListener('scroll', updatePosition, true);
        window.removeEventListener('resize', updatePosition);
        if (sessionsContainerRef.current) {
          sessionsContainerRef.current.removeEventListener('scroll', updatePosition);
        }
      };
    }
  }, [openMenuId, menuPosition]);

  const handleRenameClick = (e: React.MouseEvent, session: Session) => {
    e.stopPropagation();
    setOpenMenuId(null);
    setMenuPosition(null);
    if (!disabled && onRenameSession) {
      setEditingSessionId(session.id);
      setEditingName(session.name);
      setTimeout(() => {
        editInputRef.current?.focus();
        editInputRef.current?.select();
      }, 0);
    }
  };

  const handleDeleteClick = (e: React.MouseEvent, sessionId: string) => {
    e.stopPropagation();
    setOpenMenuId(null);
    setMenuPosition(null);
    if (!disabled && onDeleteSession) {
      onDeleteSession(sessionId);
    }
  };

  // Add flicker animation style
  useEffect(() => {
    const style = document.createElement('style');
    style.textContent = `
      @keyframes fastFlicker {
        0%, 100% { opacity: 1; }
        50% { opacity: 0.5; }
      }
      .flicker-box {
        animation: fastFlicker 2.0s cubic-bezier(0.4, 0, 0.6, 1) infinite;
      }
    `;
    style.setAttribute('data-flicker', 'true');
    if (!document.head.querySelector('style[data-flicker]')) {
      document.head.appendChild(style);
    }
    return () => {
      const existingStyle = document.head.querySelector('style[data-flicker]');
      if (existingStyle) {
        document.head.removeChild(existingStyle);
      }
    };
  }, []);

  return (
    <div className="w-64 bg-[#0F172B] border-r border-slate-700/50 shadow-sm relative flex flex-col shrink-0 z-[60] text-white h-screen" style={{ overflowX: 'hidden', overflowY: 'visible' }}>
      {/* Arista RRM Playground Header */}
      <div className="p-4 border-b border-slate-700/50 flex items-center justify-between cursor-default" onClick={() => {
          if (onNavigateAway) {
            onNavigateAway();
          }
          window.location.href = '/';
        }}>
        <div className="flex flex-col leading-tight">
          <span className="font-bold text-xl tracking-tighter text-white" >RRM+</span>
          <span className="text-xs font-medium text-blue-400 uppercase tracking-widest">RRM Playground</span>
        </div>
      </div>


      <div ref={sessionsContainerRef} className="flex-1 px-4 pt-4 pb-2 space-y-4 overflow-y-auto sidebar-scroll" style={{ overflowX: 'hidden', overflowY: openMenuId ? 'hidden' : 'auto' }}>
        {!isMounted ? (
          // Render placeholder during SSR to prevent hydration mismatch
          <div className="flex flex-col items-center justify-center py-8 text-center px-4 border border-slate-700/50 rounded-xl bg-slate-800/30">
            <div className="w-12 h-12 bg-slate-700/50 rounded-full flex items-center justify-center mb-3">
              <History className="text-[#90A1B9]" size={24} />
            </div>
            <p className="text-sm font-medium text-white mb-1">Loading sessions...</p>
          </div>
        ) : sessions.length === 0 ? (
              <div className="flex flex-col items-center justify-center py-8 text-center px-4 border border-slate-700/50 rounded-xl bg-slate-800/30">
                <div className="w-12 h-12 bg-slate-700/50 rounded-full flex items-center justify-center mb-3">
                  <History className="text-[#90A1B9]" size={24} />
                </div>
                <p className="text-sm font-medium text-white mb-1">No active sessions</p>
                <p className="text-xs text-[#90A1B9] mt-1 mb-4">Start a new session by clicking Create Session.</p>
                <button 
                  onClick={() => !disabled && onCreateSession()}
                  disabled={disabled}
                  className={`
                    text-xs bg-blue-500 text-white px-4 py-2 rounded-md font-bold hover:bg-blue-600 transition-colors
                    ${disabled ? 'opacity-50 cursor-not-allowed' : 'cursor-pointer'}
                  `}
                >
                  Create Session
                </button>
              </div>
        ) : (
          <>
            <button
              onClick={() => !disabled && onCreateSession()}
              disabled={disabled}
              className={`
                w-full p-3 rounded-xl border-2 border-dashed transition-all duration-200
                ${disabled
                  ? 'border-slate-700/30 bg-slate-800/20 text-slate-600 cursor-not-allowed opacity-50'
                  : 'border-slate-700/50 bg-slate-800/30 text-[#90A1B9] hover:border-blue-500/50 hover:bg-blue-500/10 hover:text-blue-400 cursor-pointer'
                }
              `}
            >
              <div className="flex items-center justify-center gap-2">
                <Plus size={18} />
                <span className="font-medium text-sm">Create Session</span>
              </div>
            </button>
            
            {sessions.map((session) => {
              const template = defaultTemplates.find(t => t.id === session.templateId);
              const sessionTime = isMounted 
                ? new Date(session.lastModified || session.createdAt).toLocaleTimeString([], {hour: '2-digit', minute:'2-digit'})
                : '';
              const isActive = activeSessionId === session.id;
              const getStatusColor = () => {
                if (!isActive) return 'border-slate-700/50';
                if (simulationState === 'running') return 'border-green-500';
                if (simulationState === 'building') return 'border-yellow-500';
                return 'border-blue-500/50';
              };
              const getBgColor = () => {
                if (!isActive) return 'bg-slate-800/50';
                if (simulationState === 'running') return 'bg-green-500/10';
                if (simulationState === 'building') return 'bg-yellow-500/10';
                return 'bg-blue-500/10';
              };
              const shouldFlicker = isActive && (simulationState === 'running' || simulationState === 'building');
              return (
                <div
                  key={session.id}
                  ref={isActive ? activeSessionRef : null}
                  onClick={() => !disabled && onLoadSession(session)}
                    className={`
                    group relative rounded-xl transition-all duration-200 w-full border-2 mb-4
                    ${getBgColor()} ${getStatusColor()} ${isActive ? 'shadow-md' : ''}
                    ${!isActive ? 'hover:border-blue-500/50 hover:bg-slate-800/70' : ''}
                    ${disabled && !isActive ? 'opacity-50 cursor-not-allowed grayscale' : editingSessionId === session.id ? '' : 'cursor-pointer'}
                    ${shouldFlicker ? 'flicker-box' : ''}
                  `}
                  style={{ overflow: 'visible', overflowX: 'visible', overflowY: 'visible' }}
                >
                  <div className="p-3 flex items-start gap-3 relative" style={{ overflow: 'visible' }}>
                    <div className={`p-2 rounded-lg shrink-0 ${
                      activeSessionId === session.id 
                        ? simulationState === 'running' 
                          ? 'bg-green-500/20' 
                          : simulationState === 'building'
                            ? 'bg-yellow-500/20'
                            : 'bg-blue-500/20'
                        : 'bg-slate-700/50'
                    }`}>
                      {template && getIcon(template.id)}
                    </div>
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        {editingSessionId === session.id ? (
                          <input
                            ref={editInputRef}
                            type="text"
                            value={editingName}
                            onChange={(e) => setEditingName(e.target.value)}
                            onBlur={() => handleSaveEdit(session.id)}
                            onKeyDown={(e) => {
                              if (e.key === 'Enter') {
                                handleSaveEdit(session.id);
                              } else if (e.key === 'Escape') {
                                handleCancelEdit();
                              }
                            }}
                            className={`font-bold text-sm bg-slate-700 text-white px-1 py-0.5 rounded border border-blue-400 focus:outline-none focus:ring-1 focus:ring-blue-400 w-full ${
                              activeSessionId === session.id 
                                ? simulationState === 'running' 
                                  ? 'text-green-400' 
                                  : simulationState === 'building'
                                    ? 'text-yellow-400'
                                    : 'text-blue-400'
                                : 'text-white'
                            }`}
                            onClick={(e) => e.stopPropagation()}
                            onDoubleClick={(e) => e.stopPropagation()}
                          />
                        ) : (
                          <h3 
                            className={`font-bold text-sm truncate ${
                              editingSessionId === session.id ? 'cursor-text' : 'cursor-default'
                            } ${
                              activeSessionId === session.id 
                                ? simulationState === 'running' 
                                  ? 'text-green-400' 
                                  : simulationState === 'building'
                                    ? 'text-yellow-400'
                                    : 'text-blue-400'
                                : 'text-white'
                            }`}
                            onDoubleClick={(e) => handleDoubleClick(e, session)}
                          >
                            {session.name}
                          </h3>
                        )}
                      </div>
                      <div className="flex items-center gap-2 mt-0.5 min-w-0">
                        <p className="text-[10px] text-[#90A1B9] whitespace-nowrap shrink-0">
                          {sessionTime} • {session.nodeCount ?? session.nodes?.length ?? 0} nodes
                        </p>
                      </div>
                    </div>
                    {/* Ellipsis Menu */}
                    <div 
                      className="relative shrink-0 flex flex-col items-center justify-center mt-2 menu-options-button" 
                      ref={(el) => {
                        if (el) {
                          menuRefs.current.set(session.id, el);
                        } else {
                          menuRefs.current.delete(session.id);
                        }
                      }}
                      style={{ overflow: 'visible' }}
                    >
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          e.preventDefault();
                          handleMenuClick(e, session.id);
                        }}
                        onMouseDown={(e) => {
                          e.stopPropagation();
                          e.preventDefault();
                        }}
                        className={`rounded-lg transition-colors relative flex items-center justify-center p-1 ${
                          openMenuId === session.id
                            ? 'bg-slate-700/70 text-blue-400'
                            : 'hover:bg-slate-700/50 text-white hover:text-white'
                        }`}
                        style={{ opacity: 1, cursor: 'pointer' }}
                      >
                        <EllipsisVertical size={16} style={{ opacity: 1 }} />
                      </button>
                      {openMenuId === session.id && menuPosition && typeof document !== 'undefined' && createPortal(
                        <div 
                          className="fixed border border-slate-700 rounded-lg shadow-2xl menu-dropdown"
                          style={{ 
                            top: `${menuPosition.top}px`,
                            left: `${menuPosition.left}px`,
                            zIndex: 9999, 
                            backgroundColor: '#1e293b',
                            position: 'fixed',
                            opacity: 1,
                            minWidth: '160px',
                            width: 'auto',
                            isolation: 'isolate'
                          }}
                          onMouseDown={(e) => e.stopPropagation()}
                          onClick={(e) => e.stopPropagation()}
                        >
                          <div style={{ backgroundColor: '#1e293b' }} className="p-1 rounded-lg">
                            <button
                              onClick={(e) => {
                                e.stopPropagation();
                                e.preventDefault();
                                handleRenameClick(e, session);
                              }}
                              onMouseDown={(e) => {
                                e.stopPropagation();
                                e.preventDefault();
                              }}
                              disabled={disabled}
                              className="w-full flex items-center gap-2 px-3 py-2 text-sm text-white hover:bg-slate-700 transition-all disabled:opacity-50 disabled:cursor-not-allowed outline-none focus:outline-none border-0 focus:ring-0 focus:ring-offset-0 whitespace-nowrap rounded-lg"
                              style={{ border: 'none', boxShadow: 'none' }}
                            >
                              <Pencil size={14} />
                              <span>Rename</span>
                            </button>
                            <button
                              onClick={(e) => {
                                e.stopPropagation();
                                e.preventDefault();
                                handleDeleteClick(e, session.id);
                              }}
                              onMouseDown={(e) => {
                                e.stopPropagation();
                                e.preventDefault();
                              }}
                              disabled={disabled}
                              className="w-full flex items-center gap-2 px-3 py-2 text-sm text-red-400 hover:bg-slate-700 transition-all disabled:opacity-50 disabled:cursor-not-allowed outline-none focus:outline-none border-0 focus:ring-0 focus:ring-offset-0 whitespace-nowrap rounded-lg"
                              style={{ border: 'none', boxShadow: 'none' }}
                            >
                              <Trash2 size={14} />
                              <span>Delete</span>
                            </button>
                          </div>
                        </div>,
                        document.body
                      )}
                    </div>
                  </div>
                </div>
              );
            })}
          </>
        )}
      </div>
    </div>
  );
};
