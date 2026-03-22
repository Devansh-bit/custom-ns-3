import React from 'react';
import { Trash2 } from 'lucide-react';

interface DeleteSessionButtonProps {
  onDelete: (sessionId: string) => void;
  activeSessionId: string | null;
}

export const DeleteSessionButton: React.FC<DeleteSessionButtonProps> = ({
  onDelete,
  activeSessionId
}) => {
  if (!activeSessionId) {
    return null;
  }

  const handleClick = () => {
    if (activeSessionId) {
      onDelete(activeSessionId);
    }
  };

  return (
    <button
      onClick={handleClick}
      className="p-2 bg-card rounded-lg shadow-sm border border-border text-muted-foreground hover:text-destructive hover:bg-destructive/10 transition-colors"
    >
      <Trash2 size={18} />
    </button>
  );
};

