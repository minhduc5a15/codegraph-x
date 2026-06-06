import { Codegraph, GraphData } from "./Codegraph.js";
import { platform, arch } from "os";
import { join } from "path";

export * from "./Codegraph.js";

let addon: any = null;
const platformKey = `${platform()}-${arch()}`;

try {
  switch (platformKey) {
    case "darwin-arm64":
      addon = require("@codegraph-x/core-darwin-arm64");
      break;
    case "darwin-x64":
      addon = require("@codegraph-x/core-darwin-x64");
      break;
    case "linux-x64":
      addon = require("@codegraph-x/core-linux-x64");
      break;
    case "win32-x64":
      addon = require("@codegraph-x/core-win32-x64");
      break;
    default:
      addon = require(
        join(__dirname, "..", "build", "Release", "codegraph_addon.node"),
      );
  }
} catch (error) {
  try {
    addon = require(join(__dirname, "..", "build", "codegraph_addon.node"));
  } catch (fallbackError) {
    throw new Error(
      `Codegraph-X: Failed to load pre-compiled binary for platform ${platformKey}. ${error}`,
    );
  }
}

export function setupWatchdog(ppid: number) {
  addon.SetupWatchdog(ppid);
}

export async function updateWorkspace(files: string[]): Promise<Codegraph> {
  const rawData: GraphData = await addon.UpdateWorkspace(files);
  return new Codegraph(rawData);
}
