import { AGENT_CONFIGS, AgentConfig } from './constants';
import { readJson, writeJson, setDeepValue, getDeepValue } from './file-ops';
import * as fs from 'fs';
import * as path from 'path';

export interface DetectionResult {
  id: string;
  name: string;
  isInstalledGlobal: boolean;
  isInstalledLocal: boolean;
  globalConfigPath: string;
  localConfigPath: string;
  alreadyConfigured: boolean;
}

function getMcpCommandConfig() {
  // Use the global 'codegraph-x' command (simulating NPM publish)
  // instead of hardcoding absolute paths.
  return {
    type: 'stdio',
    command: 'codegraph-x',
    args: ['mcp'],
  };
}

export function detectAgents(): DetectionResult[] {
  const results: DetectionResult[] = [];

  for (const [id, config] of Object.entries(AGENT_CONFIGS)) {
    const globalPath = config.paths.global();
    const localPath = config.paths.local();

    // Check if directories exist
    const isInstalledGlobal = fs.existsSync(path.dirname(globalPath)) || fs.existsSync(globalPath);
    const isInstalledLocal = fs.existsSync(localPath) || fs.existsSync(path.dirname(localPath));

    // Check if configured (just checking global for now as an indicator)
    let alreadyConfigured = false;
    if (fs.existsSync(globalPath)) {
      const data = readJson(globalPath);
      const val = getDeepValue(data, config.mcpKey);
      if (val && val.command === 'codegraph-x') {
        alreadyConfigured = true;
      }
    }

    if (isInstalledGlobal || isInstalledLocal) {
      results.push({
        id,
        name: config.name,
        isInstalledGlobal,
        isInstalledLocal,
        globalConfigPath: globalPath,
        localConfigPath: localPath,
        alreadyConfigured,
      });
    }
  }

  return results;
}

export function installAgentConfig(agentId: string, scope: 'global' | 'local'): boolean {
  const config = AGENT_CONFIGS[agentId];
  if (!config) return false;

  const targetPath = scope === 'global' ? config.paths.global() : config.paths.local();
  const data = readJson(targetPath);

  // Inject MCP Server config
  setDeepValue(data, config.mcpKey, getMcpCommandConfig());

  writeJson(targetPath, data);

  // Handle extra permissions (like Claude settings.json)
  // For MVP, we can keep it simple. If we needed to write to a secondary file
  // we would define it in constants.ts and process it here.

  return true;
}
