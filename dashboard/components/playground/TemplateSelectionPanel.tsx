import React, { useRef, useState, useEffect } from 'react';
import { createPortal } from 'react-dom';
import { X, Trash2, EllipsisVertical, Pencil } from 'lucide-react';
import { Template, NetworkNode, Session } from '@/lib/types';
import { TemplatePreviewPanel } from '@/components/playground/TemplatePreviewPanel';

interface TemplateSelectionPanelProps {
  showTemplateSelection: boolean;
  onSelectTemplate?: (templateId: string) => void;
  onCloseTemplateSelection?: () => void;
  savedTemplates?: Template[];
  allTemplates?: Template[];
  onDeleteTemplate?: (templateId: string) => void;
  onRenameTemplate?: (templateId: string, newName: string) => void;
  sessions?: Session[];
}

const CLICK_DELAY_MS = 300;

export const TemplateSelectionPanel: React.FC<TemplateSelectionPanelProps> = ({
  showTemplateSelection,
  onSelectTemplate,
  onCloseTemplateSelection,
  savedTemplates = [],
  allTemplates = [],
  onDeleteTemplate,
  onRenameTemplate,
  sessions = []
}) => {
  const [hoveredTemplate, setHoveredTemplate] = useState<string | null>(null);
  const [filterType, setFilterType] = useState<'saved' | 'default'>('default');
  const [editingTemplateId, setEditingTemplateId] = useState<string | null>(null);
  const [editingTemplateName, setEditingTemplateName] = useState('');
  const [openMenuId, setOpenMenuId] = useState<string | null>(null);
  
  const templateEditInputRef = useRef<HTMLInputElement>(null);
  const clickTimeoutRef = useRef<NodeJS.Timeout | null>(null);
  const menuRefs = useRef<Map<string, HTMLDivElement>>(new Map());
  const [menuPosition, setMenuPosition] = useState<{ top: number; left: number } | null>(null);

  // Set default preview to first saved template when viewing saved templates
  useEffect(() => {
    if (filterType === 'saved' && savedTemplates.length > 0 && !hoveredTemplate) {
      setHoveredTemplate(savedTemplates[0].id);
    } else if (filterType === 'default' && hoveredTemplate && savedTemplates.some(t => t.id === hoveredTemplate)) {
      setHoveredTemplate(null);
    }
  }, [filterType, savedTemplates, hoveredTemplate]);

  // Close menu when clicking outside
  useEffect(() => {
    const handleClickOutside = (event: MouseEvent) => {
      const clickedElement = event.target as HTMLElement;
      
      if (clickedElement.closest('button.menu-options-button')) {
        return;
      }
      
      const menuButton = clickedElement.closest('.menu-dropdown button');
      if (menuButton) {
        return;
      }
      
      setOpenMenuId(null);
      setMenuPosition(null);
      setHoveredTemplate(null);
    };

    if (openMenuId) {
      document.addEventListener('mousedown', handleClickOutside, true);
      return () => {
        document.removeEventListener('mousedown', handleClickOutside, true);
      };
    }
  }, [openMenuId]);
  
  // Handle ESC key to close menu
  useEffect(() => {
    const handleEscKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && openMenuId) {
        setOpenMenuId(null);
        setMenuPosition(null);
        setHoveredTemplate(null);
      }
    };

    if (openMenuId) {
      document.addEventListener('keydown', handleEscKey);
      return () => {
        document.removeEventListener('keydown', handleEscKey);
      };
    }
  }, [openMenuId]);

  // Focus input when editing starts
  useEffect(() => {
    if (editingTemplateId && templateEditInputRef.current) {
      templateEditInputRef.current.focus();
      templateEditInputRef.current.select();
    }
  }, [editingTemplateId]);

  // Cleanup timeout on unmount
  useEffect(() => {
    return () => {
      if (clickTimeoutRef.current) {
        clearTimeout(clickTimeoutRef.current);
      }
    };
  }, []);

  // Template editing handlers
  const handleTemplateDoubleClick = (e: React.MouseEvent, template: Template) => {
    e.stopPropagation();
    const isSaved = savedTemplates.some(t => t.id === template.id);
    if (!isSaved || !onRenameTemplate) return;
    
    setEditingTemplateId(template.id);
    setEditingTemplateName(template.name);
    setTimeout(() => {
      templateEditInputRef.current?.focus();
      templateEditInputRef.current?.select();
    }, 0);
  };

  const handleSaveTemplateEdit = (templateId: string) => {
    if (!onRenameTemplate) return;
    
    const trimmedName = editingTemplateName.trim();
    const template = savedTemplates.find(t => t.id === templateId);
    
    if (trimmedName && template && trimmedName !== template.name) {
      onRenameTemplate(templateId, trimmedName);
    }
    
    setEditingTemplateId(null);
    setEditingTemplateName('');
  };

  const handleCancelTemplateEdit = () => {
    setEditingTemplateId(null);
    setEditingTemplateName('');
  };

  // Menu handlers
  const handleMenuClick = (e: React.MouseEvent, templateId: string) => {
    e.stopPropagation();
    e.preventDefault();
    
    if (openMenuId === templateId) {
      // Close menu on reclick
      setOpenMenuId(null);
      setMenuPosition(null);
      setHoveredTemplate(null);
    } else {
      setOpenMenuId(templateId);
      setHoveredTemplate(templateId); // Keep template selected/hovered while menu is open
      // Calculate menu position for portal from button element
      const buttonElement = e.currentTarget as HTMLElement;
      const rect = buttonElement.getBoundingClientRect();
      setMenuPosition({
        top: rect.bottom + 4,
        left: rect.right -5 
      });
    }
  };
  
  // Update menu position when scrolling
  useEffect(() => {
    if (!openMenuId || !menuPosition) return;
    
    const updatePosition = () => {
      const menuButton = menuRefs.current.get(openMenuId);
      if (menuButton) {
        const rect = menuButton.getBoundingClientRect();
        setMenuPosition({
          top: rect.bottom + 4,
          left: rect.right - 5
        });
      }
    };
    
    // Listen to scroll on the scrollable container
    const scrollContainer = document.querySelector('.template-selection-scrollable');
    if (scrollContainer) {
      scrollContainer.addEventListener('scroll', updatePosition, true);
      window.addEventListener('scroll', updatePosition, true);
      window.addEventListener('resize', updatePosition);
    }
    
    return () => {
      if (scrollContainer) {
        scrollContainer.removeEventListener('scroll', updatePosition, true);
      }
      window.removeEventListener('scroll', updatePosition, true);
      window.removeEventListener('resize', updatePosition);
    };
  }, [openMenuId, menuPosition]);
  
  // Disable scrolling when menu is open
  useEffect(() => {
    if (openMenuId) {
      const scrollContainer = document.querySelector('.template-selection-scrollable') as HTMLElement;
      if (scrollContainer) {
        const preventScroll = (e: WheelEvent | TouchEvent) => {
          e.preventDefault();
        };
        scrollContainer.addEventListener('wheel', preventScroll, { passive: false });
        scrollContainer.addEventListener('touchmove', preventScroll, { passive: false });
        return () => {
          scrollContainer.removeEventListener('wheel', preventScroll);
          scrollContainer.removeEventListener('touchmove', preventScroll);
        };
      }
    }
  }, [openMenuId]);

  const handleRenameClick = (e: React.MouseEvent, template: Template) => {
    e.stopPropagation();
    setOpenMenuId(null);
    setMenuPosition(null);
    setHoveredTemplate(null); // Clear hover when action is selected
    if (onRenameTemplate) {
      setEditingTemplateId(template.id);
      setEditingTemplateName(template.name);
      setTimeout(() => {
        templateEditInputRef.current?.focus();
        templateEditInputRef.current?.select();
      }, 0);
    }
  };

  const handleDeleteClick = (e: React.MouseEvent, template: Template) => {
    e.stopPropagation();
    setOpenMenuId(null);
    setMenuPosition(null);
    setHoveredTemplate(null); // Clear hover when action is selected
    if (onDeleteTemplate) {
      onDeleteTemplate(template.id);
    }
  };

  // Template selection handlers
  const handleTemplateClick = (e: React.MouseEvent, template: Template) => {
    e.stopPropagation();
    
    if (editingTemplateId === template.id) return;
    
    if (clickTimeoutRef.current) {
      clearTimeout(clickTimeoutRef.current);
      clickTimeoutRef.current = null;
      return;
    }
    
    clickTimeoutRef.current = setTimeout(() => {
      clickTimeoutRef.current = null;
      if (onSelectTemplate && !editingTemplateId) {
        onSelectTemplate(template.id);
      }
    }, CLICK_DELAY_MS);
  };

  const handleTemplateDoubleClickWrapper = (e: React.MouseEvent, template: Template) => {
    e.stopPropagation();
    
    if (clickTimeoutRef.current) {
      clearTimeout(clickTimeoutRef.current);
      clickTimeoutRef.current = null;
    }
    
    handleTemplateDoubleClick(e, template);
  };

  // Helper functions
  const calculateCardCount = (): number => {
    if (filterType === 'default') {
      return allTemplates.length + 1;
    }
    return savedTemplates.length;
  };

  const getTopPadding = (cardCount: number): string => {
    if (cardCount === 0) return 'pt-24';
    if (cardCount <= 2) return 'pt-40';
    if (cardCount <= 4) return 'pt-35';
    return 'pt-26';
  };

  const getFilteredTemplates = (): Template[] => {
    if (filterType === 'saved') {
      return savedTemplates;
    }
    return allTemplates;
  };

  const getNodeCounts = (template: Template) => {
    const apCount = template.nodes.filter((n: NetworkNode) => n.type === 'ap').length;
    const clientCount = template.nodes.filter((n: NetworkNode) => n.type === 'client').length;
    return { apCount, clientCount };
  };

  if (!showTemplateSelection || !onSelectTemplate || !onCloseTemplateSelection) {
    return null;
  }

  const cardCount = calculateCardCount();
  const topPadding = getTopPadding(cardCount);
  const filteredTemplates = getFilteredTemplates();

  // Render helpers
  const renderEmptyCanvasCard = () => (
                <div
                  onClick={(e) => {
                    e.stopPropagation();
                    if (onSelectTemplate) {
                      onSelectTemplate('empty');
                    }
                  }}
                  onMouseEnter={() => setHoveredTemplate('empty')}
                  onMouseLeave={() => setHoveredTemplate(null)}
                  className="group bg-card border-2 border-dashed border-border rounded-xl p-2.5 mb-4 hover:border-brand-blue hover:shadow-md hover:shadow-primary/10 cursor-pointer transition-all duration-200 ease-in-out hover:-translate-y-0.5 min-h-[80px]"
                >
                  <div className="flex items-center gap-4">
                    <div className="w-16 h-16 bg-gradient-to-br from-blue-50 to-blue-100 dark:from-blue-900/50 dark:to-blue-800/50 rounded-xl relative overflow-hidden shrink-0 border border-border shadow-sm group-hover:shadow-md transition-shadow duration-300">
          <div 
            className="absolute inset-0 opacity-30" 
            style={{ 
              backgroundImage: 'radial-gradient(circle, #94a3b8 1.5px, transparent 1px)', 
              backgroundSize: '8px 8px' 
            }}
          />
      </div>
                    <div className="flex-1 min-w-0 border-dashed">
          <h3 className="font-semibold text-sm text-foreground mb-2 group-hover:text-primary transition-colors duration-300">
            Blank Canvas
          </h3>
                      <div className="text-xs font-medium text-muted-foreground">
                        0 APs · 0 Clients
                      </div>
                    </div>
                  </div>
                </div>
  );

  const renderMenuDropdown = (template: Template) => {
    if (openMenuId !== template.id || !menuPosition) return null;
    
    const dropdownContent = (
      <div
        className="fixed w-40 bg-popover border border-border rounded-lg shadow-lg overflow-hidden menu-dropdown z-[1000]"
        style={{ 
          top: `${menuPosition.top}px`,
          left: `${menuPosition.left}px`
        }}
        onMouseDown={(e) => e.stopPropagation()}
        onClick={(e) => e.stopPropagation()}
      >
        <button
          onClick={(e) => {
            e.stopPropagation();
            e.preventDefault();
            handleRenameClick(e, template);
          }}
          onMouseDown={(e) => {
            e.stopPropagation();
            e.preventDefault();
          }}
          className="w-full flex items-center gap-2 px-3 py-2 text-sm text-popover-foreground hover:bg-accent transition-colors outline-none focus:outline-none border-0 focus:ring-0 focus:ring-offset-0 rounded-none"
          style={{ border: 'none', boxShadow: 'none', borderRadius: '0' }}
        >
          <Pencil size={14} />
          <span>Rename</span>
        </button>
        {onDeleteTemplate && (
          <button
            onClick={(e) => {
              e.stopPropagation();
              e.preventDefault();
              handleDeleteClick(e, template);
            }}
            onMouseDown={(e) => {
              e.stopPropagation();
              e.preventDefault();
            }}
            className="w-full flex items-center gap-2 px-3 py-2 text-sm text-destructive hover:bg-accent transition-colors outline-none focus:outline-none border-0 focus:ring-0 focus:ring-offset-0 rounded-none"
            style={{ border: 'none', boxShadow: 'none', borderRadius: '0' }}
          >
            <Trash2 size={14} />
            <span>Delete</span>
          </button>
        )}
      </div>
    );
    
    // Render in portal to avoid clipping
    if (typeof window !== 'undefined') {
      return createPortal(dropdownContent, document.body);
    }
    return null;
  };

  const renderTemplatePreview = (template: Template) => {
    const apNodes = template.nodes.filter((n: NetworkNode) => n.type === 'ap');
    const clientNodes = template.nodes.filter((n: NetworkNode) => n.type === 'client');
    const interfererNodes = template.nodes.filter((n: NetworkNode) => n.type !== 'ap' && n.type !== 'client');

    return (
                      <div className="w-16 h-16 bg-gradient-to-br from-blue-50 to-blue-100 dark:from-blue-900/50 dark:to-blue-800/50 rounded-xl relative overflow-hidden shrink-0 border border-border shadow-sm group-hover:shadow-md transition-shadow duration-300">
        <div 
          className="absolute inset-0 opacity-30" 
          style={{ 
            backgroundImage: 'radial-gradient(circle, #94a3b8 1.5px, transparent 1px)', 
            backgroundSize: '8px 8px' 
          }}
        />
        {apNodes.map((node: NetworkNode) => (
                    <div key={node.id} className="absolute" style={{ left: `${node.x}%`, top: `${node.y}%` }}>
            <div className="absolute -translate-x-1/2 -translate-y-1/2 w-6 h-6 border border-blue-400/60 rounded-full" />
            <div className="absolute -translate-x-1/2 -translate-y-1/2 w-4 h-4 border border-blue-400/80 rounded-full" />
            <div className="absolute -translate-x-1/2 -translate-y-1/2 w-2 h-2 bg-blue-600 rounded-full z-10" />
                    </div>
                  ))}
        {clientNodes.map((node: NetworkNode) => (
                    <div 
                      key={node.id}
                      className="absolute w-1.5 h-1.5 bg-red-500 -translate-x-1/2 -translate-y-1/2"
                      style={{ left: `${node.x}%`, top: `${node.y}%` }}
                    />
                  ))}
        {interfererNodes.map((node: NetworkNode) => (
                    <div 
                      key={node.id}
                      className="absolute w-1 h-1 bg-red-500 rounded-full -translate-x-1/2 -translate-y-1/2"
                      style={{ left: `${node.x}%`, top: `${node.y}%` }}
                    />
                  ))}
                </div>
    );
  };

  const renderTemplateCard = (template: Template) => {
    const { apCount, clientCount } = getNodeCounts(template);
    const isSaved = savedTemplates.some(t => t.id === template.id);
    const isEditing = editingTemplateId === template.id && isSaved;

    return (
      <div
        key={template.id}
        onClick={(e) => handleTemplateClick(e, template)}
        onDoubleClick={(e) => handleTemplateDoubleClickWrapper(e, template)}
        onMouseEnter={() => {
          // Only update hover if menu is not open for this template
          if (openMenuId !== template.id) {
            setHoveredTemplate(template.id);
          }
        }}
        onMouseLeave={() => {
          // Don't clear hover if menu is open for this template
          if (openMenuId !== template.id) {
            setHoveredTemplate(null);
          }
        }}
        className={`group relative bg-card border-2 rounded-xl p-3.5 mb-4 cursor-pointer min-h-[80px] ${
          openMenuId === template.id 
            ? 'border-primary shadow-md shadow-primary/10' 
            : 'border-border hover:border-brand-blue hover:shadow-md hover:shadow-primary/10'
        } ${
          openMenuId !== template.id ? 'hover:-translate-y-0.5 transition-transform duration-200 ease-in-out' : ''
        }`}
      >
        {isSaved && (
          <div 
            className="absolute top-1/2 right-4 z-50 -translate-y-1/2" 
            ref={(el) => {
              if (el) {
                menuRefs.current.set(template.id, el);
              } else {
                menuRefs.current.delete(template.id);
              }
            }}
            style={{ 
              overflow: 'visible',
              width: 'fit-content',
              height: 'fit-content'
            }}
            onMouseEnter={(e) => {
              e.stopPropagation();
              // Prevent card hover effect when hovering over menu area
            }}
          >
            <button
              onClick={(e) => {
                e.stopPropagation();
                e.preventDefault();
                handleMenuClick(e, template.id);
              }}
              className={`menu-options-button group/ellipsis rounded-lg transition-colors relative flex items-center justify-center p-0.5 ${
                openMenuId === template.id
                  ? 'bg-primary/20 text-primary'
                  : 'hover:bg-accent text-muted-foreground hover:text-white'
              }`}
              onMouseDown={(e) => e.stopPropagation()}
              onMouseEnter={(e) => {
                e.stopPropagation();
                // Prevent card hover transform
              }}
              style={{ zIndex: 100, transform: 'translateY(0)', width: 'fit-content', height: 'fit-content' }}
            >
              <EllipsisVertical 
                size={16} 
                className={`transition-colors ${
                  openMenuId === template.id
                    ? 'text-primary'
                    : 'text-muted-foreground group-hover/ellipsis:text-foreground'
                }`}
                style={{ opacity: 1 }} 
              />
            </button>
          </div>
        )}
        
        <div className="flex items-center gap-4">
          {renderTemplatePreview(template)}
          
                      <div className="flex-1 min-w-0 pr-8">
                        <div className="flex items-center gap-2 mb-2 min-w-0">
              {isEditing ? (
                            <input
                              ref={templateEditInputRef}
                              type="text"
                              value={editingTemplateName}
                              onChange={(e) => setEditingTemplateName(e.target.value)}
                  onBlur={() => handleSaveTemplateEdit(template.id)}
                              onKeyDown={(e) => {
                                if (e.key === 'Enter') {
                      handleSaveTemplateEdit(template.id);
                                } else if (e.key === 'Escape') {
                                  handleCancelTemplateEdit();
                                }
                              }}
                              className="font-semibold text-sm text-foreground bg-background px-1 py-0.5 rounded border border-primary focus:outline-none focus:ring-1 focus:ring-primary whitespace-nowrap"
                              style={{ width: `${Math.max(editingTemplateName.length * 7 + 20, 120)}px`, minWidth: '120px' }}
                              onClick={(e) => e.stopPropagation()}
                              onDoubleClick={(e) => e.stopPropagation()}
                            />
                          ) : (
                            <h3 
                              className={`font-semibold text-sm text-foreground group-hover:text-black dark:group-hover:text-white transition-colors duration-300 whitespace-nowrap ${
                                isSaved ? 'cursor-text' : ''
                              }`}
                  onDoubleClick={(e) => handleTemplateDoubleClick(e, template)}
                            >
                  {template.name}
                            </h3>
                          )}
                        </div>
                        <div className="text-xs font-medium text-muted-foreground whitespace-nowrap">
                          {apCount} APs · {clientCount} Clients
                  </div>
                </div>
              </div>
            </div>
          );
  };

  return (
    <>
      <style>{`
        .template-selection-scrollable::-webkit-scrollbar {
          width: 6px;
        }
        .template-selection-scrollable::-webkit-scrollbar-track {
          background: transparent;
          margin-top: 48px;
          margin-bottom: 8px;
        }
        .template-selection-scrollable::-webkit-scrollbar-thumb {
          background: rgba(148, 163, 184, 0.3);
          border-radius: 3px;
          border: 1px solid transparent;
          background-clip: padding-box;
        }
        .template-selection-scrollable::-webkit-scrollbar-thumb:hover {
          background: rgba(148, 163, 184, 0.5);
        }
      `}</style>
      <div 
        className="absolute inset-0 z-50 bg-background"
        onClick={(e) => {
          if (e.target === e.currentTarget) {
            onCloseTemplateSelection();
          }
        }}
      >
      <button
        onClick={onCloseTemplateSelection}
        disabled={sessions.length === 0}
        className={`absolute top-4 right-4 p-2 bg-card rounded-lg shadow-sm border border-border transition-colors z-10 ${
          sessions.length === 0
            ? 'text-muted-foreground cursor-not-allowed opacity-50'
            : 'text-muted-foreground hover:text-black hover:bg-accent'
        }`}
      >
        <X size={20} />
      </button>
      
      <div className="flex items-start h-full pt-0 w-full overflow-hidden" onClick={(e) => e.stopPropagation()}>
        <div className="w-auto min-w-75 max-w-75 h-full flex flex-col shrink-0 border-r border-border bg-muted/50 overflow-hidden">
          <div 
            className={`flex-1 overflow-y-auto overflow-x-hidden template-selection-scrollable pl-7 pr-7 ${topPadding}`} 
            style={{ 
              scrollBehavior: 'smooth',
              pointerEvents: openMenuId ? 'none' : 'auto',
              overflowY: openMenuId ? 'hidden' : 'auto'
            }}
          >
            <div className="space-y-6">
              {filterType === 'default' && renderEmptyCanvasCard()}
              {filteredTemplates.map(renderTemplateCard)}
            </div>
          </div>
          
          {savedTemplates.length > 0 && (
            <div className="pl-7 pr-7 pb-3 pt-4 shrink-0">
              <button
                onClick={(e) => {
                  e.stopPropagation();
                  setFilterType(filterType === 'default' ? 'saved' : 'default');
                }}
                className={`w-full group bg-card border-2 border-border rounded-xl p-2.5 font-medium text-sm transition-all duration-200 ease-in-out hover:-translate-y-0.5 hover:border-brand-blue hover:shadow-md hover:shadow-primary/10 hover:bg-primary/10  hover:text-black dark:hover:text-white text-foreground ${
                  filterType !== 'default'
                    ? 'bg-primary border-primary text-primary-foreground shadow-md hover:shadow-lg hover:shadow-primary/10'
                    : ''
                }`}
              >
                <div className="flex items-center justify-center gap-2">
                  <span className="font-semibold">
                    {filterType === 'saved' ? 'Default Templates' : 'Saved Templates'}
                  </span>
                </div>
              </button>
            </div>
          )}
        </div>
        
        <TemplatePreviewPanel
          hoveredTemplate={hoveredTemplate}
          filterType={filterType}
          allTemplates={allTemplates}
          savedTemplates={savedTemplates}
        />
      </div>

      {/* Render menu dropdown in portal to avoid clipping */}
      {openMenuId && (() => {
        const template = [...savedTemplates, ...allTemplates].find(t => t.id === openMenuId);
        if (template) {
          return renderMenuDropdown(template);
        }
        return null;
      })()}
    </div>
    </>
  );
};
