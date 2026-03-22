import React, { useState, useEffect } from 'react';
import { Save, Check } from 'lucide-react';

interface SaveTemplateButtonProps {
  onSave: () => void;
  hasUnsavedChanges: boolean;
  nodesCount: number;
}

export const SaveTemplateButton: React.FC<SaveTemplateButtonProps> = ({
  onSave,
  hasUnsavedChanges,
  nodesCount
}) => {
  const [justSaved, setJustSaved] = useState(false);

  // Reset justSaved when there are new unsaved changes
  useEffect(() => {
    if (hasUnsavedChanges && justSaved) {
      setJustSaved(false);
    }
  }, [hasUnsavedChanges, justSaved]);

  // Auto-hide after 3 seconds when saved
  useEffect(() => {
    if (justSaved) {
      const timer = setTimeout(() => {
        setJustSaved(false);
      }, 3000);
      return () => clearTimeout(timer);
    }
  }, [justSaved]);

  // Don't render if no save function, no nodes, or nothing to show
  if (!onSave || nodesCount === 0 || (!hasUnsavedChanges && !justSaved)) {
    return null;
  }

  return (
    <button
      onClick={() => {
        onSave();
        setJustSaved(true);
      }}
      className={`p-2 rounded-lg shadow-sm border transition-all flex items-center ${
        justSaved
          ? 'bg-green-500 text-white border-green-600'
          : 'bg-card border-border text-muted-foreground hover:text-blue-600 hover:bg-blue-500/10'
      }`}
    >
      {justSaved ? <Check size={18} /> : <Save size={18} />}
      {!justSaved && <span className="ml-1.5 text-xs font-medium">Save</span>}
      {justSaved && <span className="ml-1.5 text-xs font-medium">Saved!</span>}
    </button>
  );
};

