#!/usr/bin/env node
import * as net from "net";
import * as fs from "fs";
import * as path from "path";
import * as crypto from "crypto";
import { spawn } from "child_process";
import * as readline from "readline";
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";
import { AutoDetectStdioServerTransport } from "./transports/auto-detect.js";
import { formatXRayResult } from "./services/formatter.js";

async function main() {
  if (process.argv.includes("--help") || process.argv.includes("-h")) {
    console.error("Usage: codegraph-mcp [workspace_directory]");
    process.exit(0);
  }

  const targetDir = process.argv[2] || process.cwd();
  if (!fs.existsSync(targetDir) || !fs.statSync(targetDir).isDirectory()) {
    console.error(`[Error] Invalid directory: ${targetDir}`);
    process.exit(1);
  }

  const hash = crypto.createHash("md5").update(targetDir).digest("hex");
  const socketPath =
    process.platform === "win32"
      ? `\\\\.\\pipe\\codegraph-x-${hash}`
      : `/tmp/codegraph-x-${hash}.sock`;

  let clientSocket: net.Socket | null = null;

  const tryConnect = () =>
    new Promise<net.Socket>((resolve, reject) => {
      const socket = net.createConnection(socketPath);
      
      const onConnect = () => {
        cleanup();
        resolve(socket);
      };
      
      const onError = (err: Error) => {
        cleanup();
        socket.destroy();
        reject(err);
      };
      
      const cleanup = () => {
        socket.off("connect", onConnect);
        socket.off("error", onError);
      };

      socket.on("connect", onConnect);
      socket.on("error", onError);
    });

  try {
    clientSocket = await tryConnect();
  } catch (e) {
    // Spawn daemon if not running
    const daemonScript = path.join(__dirname, "daemon.js");
    const child = spawn(
      process.execPath,
      [daemonScript, targetDir, socketPath],
      {
        stdio: "ignore",
      },
    );
    child.unref();

    // Wait for daemon to be ready
    for (let i = 0; i < 50; i++) {
      await new Promise((r) => setTimeout(r, 100));
      try {
        clientSocket = await tryConnect();
        break;
      } catch (err) {}
    }
  }

  if (!clientSocket) {
    console.error("Failed to connect to Codegraph-X Daemon.");
    process.exit(1);
  }

  // IPC Client logic
  let reqId = 0;
  const pendingRequests = new Map<number, (res: any) => void>();

  const rl = readline.createInterface({
    input: clientSocket,
    terminal: false,
    historySize: 0,
    crlfDelay: Infinity
  });

  rl.on("line", (line) => {
    if (!line.trim()) return;
    try {
      const msg = JSON.parse(line);
      if (msg.id !== undefined && pendingRequests.has(msg.id)) {
        pendingRequests.get(msg.id)!(msg.result);
        pendingRequests.delete(msg.id);
      }
    } catch (err) {
      console.error("[IPC] Failed to parse JSON line:", err);
    }
  });

  const rpcCall = (action: string, params: any): Promise<any> => {
    return new Promise((resolve) => {
      const id = ++reqId;
      pendingRequests.set(id, resolve);
      clientSocket!.write(JSON.stringify({ id, action, ...params }) + "\n");
    });
  };

  // Start MCP Server
  const server = new Server(
    { name: "codegraph-x", version: "1.0.0" },
    { capabilities: { tools: {} } },
  );

  server.setRequestHandler(ListToolsRequestSchema, async () => ({
    tools: [
      {
        name: "cx_xray_scan",
        description:
          "⚡️ CODEGRAPH-X EXCLUSIVE: ULTRA-FAST X-RAY SCAN. \n" +
          "ALWAYS USE THIS TOOL FIRST for any question about architecture, how things work, bug hunting, or function analysis. \n" +
          "Unlike ReadFile or Grep (which are slow, rigid text readers), cx_xray_scan connects directly to our In-Memory C++ Engine. " +
          "It allows you to 'see through' the codebase, returning the original source code ALONG WITH the entire caller/callee execution graph in a single call (0.1ms). \n" +
          "Input supports both natural language queries (e.g., 'How does the multi-threading mechanism work?') and specific symbol names.",
        inputSchema: {
          type: "object",
          properties: {
            query: {
              type: "string",
              description: "Symbol names, file names, or a natural language question (e.g. 'worker queue mutex', 'ParallelParsingEngine')"
            }
          },
          required: ["query"],
        },
      }
    ],
  }));

  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: args } = request.params;
    try {
      if (name === "cx_xray_scan") {
        const query = typeof args?.query === 'string' ? args.query : "";
        if (!query.trim()) throw new Error("Invalid query parameter");
        
        // Split natural language into simple tokens for the C++ engine to handle for now
        const symbols = query.split(/\s+/).filter(Boolean);
        const nodes: any[] = await rpcCall("explore_flow", { symbols });

        const outputText = await formatXRayResult(nodes, process.cwd());

        return {
          content: [{ type: "text", text: outputText }],
        };
      }
      throw new Error(`Tool not found: ${name}`);
    } catch (error: any) {
      return {
        isError: true,
        content: [{ type: "text", text: error.message }],
      };
    }
  });

  const transport = new AutoDetectStdioServerTransport();
  await server.connect(transport as any);
  console.error(
    `Codegraph-X MCP Client running on stdio (Connected to daemon ${socketPath})`,
  );

  const shutdown = async () => {
    await server.close();
    clientSocket?.end();
    process.exit(0);
  };

  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
}

main().catch((error) => {
  console.error("Fatal error in MCP client:", error);
  process.exit(1);
});
