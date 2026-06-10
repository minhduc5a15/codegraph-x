import * as path from 'path';
import * as os from 'os';

export interface AgentConfig {
  id: string;
  name: string;
  // Path can be a function returning the absolute path or a static string
  paths: {
    global: () => string;
    local: () => string;
  };
  mcpKey: string;
  permissionsKey?: string;
  permissionsRequired?: string[];
}

export const AGENT_CONFIGS: Record<string, AgentConfig> = {
  gemini: {
    id: 'gemini',
    name: 'Gemini CLI',
    paths: {
      global: () => path.join(os.homedir(), '.gemini', 'settings.json'),
      local: () => path.join(process.cwd(), '.gemini', 'settings.json'),
    },
    mcpKey: 'mcpServers.codegraph-x',
  },
  claude: {
    id: 'claude',
    name: 'Claude Code',
    paths: {
      global: () => path.join(os.homedir(), '.claude.json'),
      local: () => path.join(process.cwd(), '.mcp.json'),
    },
    mcpKey: 'mcpServers.codegraph-x',
    permissionsKey: 'permissions.allow',
    permissionsRequired: ['mcp__codegraph-x__codegraph_explore'],
  },
};
