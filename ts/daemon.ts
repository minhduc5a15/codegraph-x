import * as net from "net";
import * as fs from "fs";
import * as path from "path";
import ignore from "ignore";
import { updateWorkspace, setupWatchdog, Codegraph } from "./index.js";

async function main() {
  // 1. Setup OS watchdog to prevent zombie process
  setupWatchdog(process.ppid);

  const targetDir = process.argv[2];
  const socketPath = process.argv[3];

  if (!targetDir || !socketPath) {
    console.error("Usage: daemon.js [targetDir] [socketPath]");
    process.exit(1);
  }

  // 2. Ensure clean socket
  if (process.platform !== "win32" && fs.existsSync(socketPath)) {
    try {
      fs.unlinkSync(socketPath);
    } catch (e) {}
  }

  // 3. Initialize Ignore Filter
  const ig = ignore().add(['node_modules', '.git', 'vendor', 'third_party', 'build', 'dist', 'out', 'bin', 'obj']);
  const gitignorePath = path.join(targetDir, '.gitignore');
  if (fs.existsSync(gitignorePath)) {
    ig.add(fs.readFileSync(gitignorePath, 'utf8'));
  }
  const codegraphignorePath = path.join(targetDir, '.codegraphignore');
  if (fs.existsSync(codegraphignorePath)) {
    ig.add(fs.readFileSync(codegraphignorePath, 'utf8'));
  }

  function collectSourceFiles(dir: string, fileList: string[] = []): string[] {
    try {
      const files = fs.readdirSync(dir);
      for (const file of files) {
        const fullPath = path.join(dir, file);
        const relPath = path.relative(targetDir, fullPath);
        
        // Ignore if matched
        if (ig.ignores(relPath)) continue;

        try {
          const stat = fs.statSync(fullPath);
          if (stat.isDirectory()) {
            collectSourceFiles(fullPath, fileList);
          } else {
            const ext = path.extname(file);
            if (
              ext === ".cpp" ||
              ext === ".hpp" ||
              ext === ".c" ||
              ext === ".h"
            ) {
              fileList.push(fullPath);
            }
          }
        } catch (error) {
          continue;
        }
      }
    } catch (e) {}
    return fileList;
  }

  let graph: Codegraph | null = null;

  const MAX_BUFFER_SIZE = 50 * 1024 * 1024; // 50MB

  // 5. IPC Server
  const server = net.createServer((socket) => {
    let buffer = "";
    socket.on("data", async (chunk) => {
      buffer += chunk.toString();

      if (buffer.length > MAX_BUFFER_SIZE) {
        console.error("Payload too large, destroying socket to prevent OOM.");
        socket.destroy();
        return;
      }

      const lines = buffer.split("\n");
      buffer = lines.pop() || "";

      for (const line of lines) {
        if (!line.trim()) continue;
        let req: any = null;
        try {
          req = JSON.parse(line);
          let result = null;
          if (graph) {
            if (req.action === "explore_flow") {
              result = graph.exploreFlow(req.symbols || []);
            } else if (req.action === "read_node") {
              if (typeof req.node_id !== "number") {
                result = "*(Invalid request: node_id must be a number)*";
              } else {
                const node = graph.getNode(req.node_id);
                if (!node || node.type === 0 || !node.path || !fs.existsSync(node.path)) {
                  result = `*(Node ID ${req.node_id} not found or invalid)*`;
                } else {
                  try {
                    const content = await fs.promises.readFile(node.path, "utf-8");
                    const fileLines = content.split("\n");
                    const startIdx = Math.max(0, node.startLine - 1);
                    const endIdx = Math.min(fileLines.length, node.endLine);
                    const slice = fileLines.slice(startIdx, endIdx);
                    result = "```cpp\n" + slice.join("\n") + "\n```";
                  } catch (e) {
                    result = `*(Error reading file ${node.path})*`;
                  }
                }
              }
            }
          }
          socket.write(JSON.stringify({ id: req.id, result }) + "\n");
        } catch (e) {
          socket.write(JSON.stringify({ id: req?.id, error: String(e) }) + "\n");
        }
      }
    });
  });

  server.listen(socketPath, async () => {
    console.log(`Daemon listening on ${socketPath}`);

    // 3. Initial parse
    let allFiles = collectSourceFiles(targetDir);
    graph = await updateWorkspace(allFiles);

    // 4. Incremental Sync (Debounced File Watcher)
    let debounceTimer: NodeJS.Timeout | null = null;
    try {
      fs.watch(targetDir, { recursive: true }, (eventType, filename) => {
        if (
          filename &&
          (filename.endsWith(".cpp") ||
            filename.endsWith(".hpp") ||
            filename.endsWith(".c") ||
            filename.endsWith(".h"))
        ) {
          if (debounceTimer) clearTimeout(debounceTimer);
          debounceTimer = setTimeout(async () => {
            allFiles = collectSourceFiles(targetDir);
            graph = await updateWorkspace(allFiles);
          }, 100);
        }
      });
    } catch (e) {
      // Ignore recursive watch errors on unsupported platforms
    }
  });
}

main().catch(console.error);
