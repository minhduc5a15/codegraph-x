import * as fs from "fs";
import * as path from "path";

export async function formatXRayResult(nodes: any[], cwd: string): Promise<string> {
  if (!nodes || nodes.length === 0) {
    return "No relevant symbols found in the In-Memory Graph.";
  }

  let outputText = "";

  // Group by file
  const byFile: Record<string, any[]> = {};
  for (const node of nodes) {
    if (!byFile[node.path]) byFile[node.path] = [];
    byFile[node.path].push(node);
  }

  for (const [filePath, fileNodes] of Object.entries(byFile)) {
    const resolvedPath = path.resolve(filePath);
    const resolvedCwd = path.resolve(cwd);

    // Path Traversal Protection: Ensure the file is within the workspace
    if (!resolvedPath.startsWith(resolvedCwd)) {
      console.warn(`[Security] Ignored file outside of workspace: ${filePath}`);
      continue;
    }

    const relPath = path.relative(resolvedCwd, resolvedPath);
    outputText += `## File: ${relPath}\n\n`;

    try {
      // Use Non-blocking I/O (Promises)
      const fileContent = await fs.promises.readFile(resolvedPath, "utf-8");
      const lines = fileContent.split("\n");

      // Sort nodes by startLine
      fileNodes.sort((a, b) => a.startLine - b.startLine);

      for (const node of fileNodes) {
        outputText += `### Symbol: ${node.name} (L${node.startLine}-${node.endLine})\n`;
        outputText += "```cpp\n";
        
        const startIdx = Math.max(0, node.startLine - 1);
        const endIdx = Math.min(lines.length, node.endLine);
        const slice = lines.slice(startIdx, endIdx);

        const MAX_LINES = 150;
        const isTruncated = slice.length > MAX_LINES;
        const displaySlice = isTruncated ? slice.slice(0, MAX_LINES) : slice;

        for (let i = 0; i < displaySlice.length; i++) {
          outputText += `${startIdx + i + 1}\t${displaySlice[i]}\n`;
        }

        if (isTruncated) {
          outputText += `... [${slice.length - MAX_LINES} lines truncated for brevity]\n`;
        }
        outputText += "```\n\n";

        if (node.neighbors && node.neighbors.length > 0) {
          outputText += `**Dependencies:**\n`;
          for (const n of node.neighbors) {
            outputText += `- ${n.name}\n`;
          }
          outputText += "\n";
        }
      }
    } catch (err) {
      outputText += `*(Error reading file)*\n\n`;
    }
  }

  return outputText;
}
