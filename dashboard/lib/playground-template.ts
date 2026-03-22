import { NetworkNode, Template } from '@/types';
import constantsData from '@/constants.json';
const defaultTemplates = constantsData.defaultTemplates;

/**
 * Check if current nodes match template defaults
 */
export const canResetToTemplate = (
  nodes: NetworkNode[],
  templateId: string | null,
  savedTemplates: Template[]
): boolean => {
  const id = templateId || 'empty';

  // For empty sessions, can reset if there are any nodes
  if (id === 'empty') {
    return nodes.length > 0;
  }

  // Find template
  const template = [...defaultTemplates, ...savedTemplates].find((t: Template) => t.id === id);
  if (!template) return nodes.length > 0;

  const defaultNodes = template.nodes;

  // Compare node counts
  if (nodes.length !== defaultNodes.length) return true;

  // Compare nodes by creating maps of node IDs
  const currentNodeIds = new Set(nodes.map(n => n.id));
  const defaultNodeIds = new Set(defaultNodes.map(n => n.id));

  // If IDs don't match, can reset
  if (currentNodeIds.size !== defaultNodeIds.size) return true;
  for (const id of defaultNodeIds) {
    if (!currentNodeIds.has(id)) return true;
  }

  // Check if positions match (allowing small floating point differences)
  const nodeMap = new Map(nodes.map(n => [n.id, n]));
  for (const defaultNode of defaultNodes) {
    const currentNode = nodeMap.get(defaultNode.id);
    if (!currentNode) return true;
    if (Math.abs(currentNode.x - defaultNode.x) > 0.1 ||
      Math.abs(currentNode.y - defaultNode.y) > 0.1) {
      return true;
    }
  }

  return false; // Current state matches defaults
};

/**
 * Generate template name for saved template
 */
export const generateTemplateName = (
  baseTemplate: Template | null,
  savedTemplates: Template[]
): { name: string; description: string } => {
  let templateName = '';
  let description = 'Custom template';

  if (baseTemplate) {
    // Extract base template number from name (e.g., "Template 2" -> 2, "Template 2(1)" -> 2)
    const baseMatch = baseTemplate.name.match(/Template (\d+)/);
    if (baseMatch) {
      const baseNumber = baseMatch[1];
      // Count how many templates exist with this base number (including versions)
      const versionsFromBase = savedTemplates.filter(t => {
        const match = t.name.match(new RegExp(`^Template ${baseNumber}\\(\\d+\\)$`));
        return match !== null;
      });
      const versionNumber = versionsFromBase.length + 1;
      templateName = `Template ${baseNumber}(${versionNumber})`;
      description = `Based on ${baseTemplate.name}`;
    } else {
      // If base template doesn't follow the pattern, use simple numbering
      const existingCount = savedTemplates.length;
      const templateNumber = existingCount + 1;
      templateName = `Template ${templateNumber}`;
      description = `Based on ${baseTemplate.name}`;
    }
  } else {
    // No base template - use simple numbering
    const existingCount = savedTemplates.length;
    const templateNumber = existingCount + 1;
    templateName = `Template ${templateNumber}`;
  }

  return { name: templateName, description };
};

