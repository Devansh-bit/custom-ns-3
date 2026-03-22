const SESSIONS_KEY = 'playground-sessions';
const SAVED_TEMPLATES_KEY = 'saved-templates';

/**
 * Save sessions to localStorage
 */
export const saveSessionsToStorage = (sessions: any[]): void => {
  if (typeof window === 'undefined') return;
  try {
    localStorage.setItem(SESSIONS_KEY, JSON.stringify(sessions));
  } catch (e) {
    console.error('Failed to save sessions:', e);
  }
};

/**
 * Save templates to localStorage
 */
export const saveTemplatesToStorage = (templates: any[]): void => {
  if (typeof window === 'undefined') return;
  try {
    localStorage.setItem(SAVED_TEMPLATES_KEY, JSON.stringify(templates));
  } catch (e) {
    console.error('Failed to save templates:', e);
  }
};

