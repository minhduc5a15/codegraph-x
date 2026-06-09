import * as fs from "fs";
import * as path from "path";

export async function formatXRayResult(nodes: any[], cwd: string): Promise<string> {
  if (!nodes || nodes.length === 0) {
    return "No relevant symbols found in the In-Memory Graph.";
  }

  let outputText = "";
  let totalLinesPrinted = 0;
  const MAX_TOTAL_LINES = 1000;
  const resolvedCwd = path.resolve(cwd);

  // Separate nodes into Entry Points and Related Symbols
  const entryPoints = nodes.filter(n => n.depth === 0);
  const relatedSymbols = nodes.filter(n => n.depth > 0);

  // Group related symbols by file
  const relatedByFile: Record<string, any[]> = {};
  for (const node of relatedSymbols) {
    if (!relatedByFile[node.path]) relatedByFile[node.path] = [];
    relatedByFile[node.path].push(node);
  }

  outputText += `## X-Ray Scan Results\n\n`;
  outputText += `> [!TIP]\n`;
  outputText += `> To see the full source code of any node below, use the \`cx_read_node\` tool with its ID. For example: \`{"node_id": 123}\`\n\n`;

  outputText += `### Entry Points\n`;
  for (const node of entryPoints) {
    const relPath = node.path === "external" ? "external" : path.relative(resolvedCwd, path.resolve(node.path));
    outputText += `- **${node.name}** (ID: ${node.id}) - \`${relPath}:${node.startLine}\`\n`;
  }
  outputText += `\n`;

  if (relatedSymbols.length > 0) {
    outputText += `### Related Symbols\n`;
    for (const [filePath, fileNodes] of Object.entries(relatedByFile)) {
      const relPath = filePath === "external" ? "external" : path.relative(resolvedCwd, path.resolve(filePath));
      const symbolsStr = fileNodes.map(n => `\`${n.name}\` (ID: ${n.id}, L${n.startLine})`).join(", ");
      outputText += `- **${relPath}**: ${symbolsStr}\n`;
    }
    outputText += `\n`;
  }

  // Code Blocks (Budget Allocation)
  outputText += `### Source Code\n\n`;
  
  // Sort nodes: entry points first, then by depth, then by startLine
  const priorityNodes = [...nodes].sort((a, b) => {
    if (a.depth !== b.depth) return a.depth - b.depth;
    if (a.path !== b.path) return a.path.localeCompare(b.path);
    return a.startLine - b.startLine;
  });

  for (const node of priorityNodes) {
    if (node.type === 0 || node.path === "external") continue; // Skip files and externals

    const resolvedPath = path.resolve(node.path);
    if (!resolvedPath.startsWith(resolvedCwd)) continue;

    const nodeLineCount = node.endLine - node.startLine + 1;
    
    // Stop printing code completely if budget is exhausted
    if (totalLinesPrinted >= MAX_TOTAL_LINES) {
       break;
    }

    const relPath = path.relative(resolvedCwd, resolvedPath);
    outputText += `#### ${node.name} (ID: ${node.id}) - \`${relPath}:L${node.startLine}-L${node.endLine}\`\n`;

    try {
      const fileContent = await fs.promises.readFile(resolvedPath, "utf-8");
      const lines = fileContent.split("\n");

      outputText += "```cpp\n";
      const startIdx = Math.max(0, node.startLine - 1);
      const endIdx = Math.min(lines.length, node.endLine);
      const slice = lines.slice(startIdx, endIdx);

      const MAX_LINES_PER_NODE = 150;
      const isTruncated = slice.length > MAX_LINES_PER_NODE;
      const displaySlice = isTruncated ? slice.slice(0, MAX_LINES_PER_NODE) : slice;

      for (let i = 0; i < displaySlice.length; i++) {
        outputText += `${displaySlice[i]}\n`;
      }

      if (isTruncated) {
        outputText += `// ... [${slice.length - MAX_LINES_PER_NODE} lines omitted for brevity. Use cx_read_node to view full body] ...\n`;
      }
      outputText += "```\n\n";

      totalLinesPrinted += displaySlice.length;
    } catch (err) {
      outputText += `*(Error reading file)*\n\n`;
    }
  }

  // Call Paths
  outputText += `### Call Paths\n\n`;
  for (const node of priorityNodes) {
    if (node.callers && node.callers.length > 0) {
      const uniqueCallers = Array.from(new Set<string>(node.callers.map((c: any) => c.name))).slice(0, 5);
      outputText += `- \`${node.name}\` is called by: ${uniqueCallers.map(c => `\`${c}\``).join(", ")}${node.callers.length > 5 ? '...' : ''}\n`;
    }
    if (node.neighbors && node.neighbors.length > 0) {
      const uniqueNeighbors = Array.from(new Set<string>(node.neighbors.map((n: any) => n.name))).slice(0, 5);
      outputText += `- \`${node.name}\` calls: ${uniqueNeighbors.map(c => `\`${c}\``).join(", ")}${node.neighbors.length > 5 ? '...' : ''}\n`;
    }
  }

  return outputText;
}
