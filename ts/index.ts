import { Codegraph, GraphData, setNativeAddon } from './codegraph.js';
import { platform, arch } from 'os';
import { join } from 'path';
import { registerAllLanguages } from './languages/index.js';

export * from './codegraph.js';

const platformKey = `${platform()}-${arch()}`;
let _addon: any = null;

try {
  _addon = require(join(__dirname, '..', 'build', 'Release', 'codegraph_addon.node'));
} catch (e1) {
  try {
    _addon = require(join(__dirname, '..', 'build', 'codegraph_addon.node'));
  } catch (e2) {
    switch (platformKey) {
      case 'darwin-arm64':
        _addon = require('@codegraph-x/core-darwin-arm64');
        break;
      case 'darwin-x64':
        _addon = require('@codegraph-x/core-darwin-x64');
        break;
      case 'linux-x64':
        _addon = require('@codegraph-x/core-linux-x64');
        break;
      case 'win32-x64':
        _addon = require('@codegraph-x/core-win32-x64');
        break;
      default:
        throw new Error(`Platform ${platformKey} is not supported. ${e2}`);
    }
  }
}

export const addon = _addon;

if (addon) {
  setNativeAddon(addon);
  registerAllLanguages();
}

export function setupWatchdog(ppid: number) {
  addon.SetupWatchdog(ppid);
}

export async function updateWorkspace(files: string[]): Promise<Codegraph> {
  const rawData: GraphData = await addon.UpdateWorkspace(files);
  return new Codegraph(rawData);
}

export function searchSubstring(query: string): number[] {
  return addon.SearchSubstring(query);
}

export function searchPathSubstring(query: string): number[] {
  return addon.SearchPathSubstring(query);
}

export function searchFuzzy(query: string): number[] {
  return addon.SearchFuzzy(query);
}
