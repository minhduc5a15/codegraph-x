#!/usr/bin/env node
import * as net from "net";
import * as fs from "fs";
import * as path from "path";
import * as crypto from "crypto";
import { spawn } from "child_process";
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";

class LspStdioServerTransport {
  onclose?: () => void;
  onerror?: (error: Error) => void;
  onmessage?: (message: any) => void;
  private buffer = Buffer.alloc(0);

  async start() {
    process.stdin.on("data", (chunk) => {
      this.buffer = Buffer.concat([this.buffer, chunk]);
      this.processBuffer();
    });
    process.stdin.on("end", () => this.onclose?.());
    process.stdin.on("error", (e) => this.onerror?.(e));
  }

  private processBuffer() {
    while (true) {
      const match = this.buffer.toString('utf-8').match(/Content-Length:\s*(\d+)\r\n\r\n/i);
      if (!match) break;
      const headerLength = match[0].length;
      const contentLength = parseInt(match[1], 10);
      if (this.buffer.length < headerLength + contentLength) break;

      const payload = this.buffer.toString('utf-8', headerLength, headerLength + contentLength);
      this.buffer = this.buffer.subarray(headerLength + contentLength);

      try {
        const message = JSON.parse(payload);
        this.onmessage?.(message);
      } catch (e) {
        this.onerror?.(new Error("Parse error"));
      }
    }
  }

  async close() {
    process.stdin.pause();
    this.onclose?.();
  }

  async send(message: any) {
    const payload = JSON.stringify(message);
    process.stdout.write(`Content-Length: ${Buffer.byteLength(payload, 'utf-8')}\r\n\r\n${payload}`);
  }
}

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
      socket.on("connect", () => resolve(socket));
      socket.on("error", (e) => reject(e));
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
  let buffer = "";
  const MAX_BUFFER_SIZE = 50 * 1024 * 1024; // 50MB

  clientSocket.on("data", (chunk) => {
    buffer += chunk.toString();
    if (buffer.length > MAX_BUFFER_SIZE) {
      console.error("Payload too large, destroying socket to prevent OOM.");
      clientSocket?.destroy();
      process.exit(1);
    }
    const lines = buffer.split("\n");
    buffer = lines.pop() || "";
    for (const line of lines) {
      if (!line.trim()) continue;
      const msg = JSON.parse(line);
      if (msg.id !== undefined && pendingRequests.has(msg.id)) {
        pendingRequests.get(msg.id)!(msg.result);
        pendingRequests.delete(msg.id);
      }
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
        name: "explore-flow",
        description:
          "Extracts a unified hierarchical subgraph starting from a set of symbols. Use this to quickly understand the execution flow, caller context, or inheritance chain.",
        inputSchema: {
          type: "object",
          properties: {
            symbols: {
              type: "array",
              items: { type: "string" },
              description: "List of starting symbol names (e.g. ['MyClass', 'setup'])"
            }
          },
          required: ["symbols"],
        },
      }
    ],
  }));

  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: args } = request.params;
    try {
      if (name === "explore-flow") {
        const symbols = Array.isArray(args?.symbols) ? args.symbols : [];
        if (!symbols.length) throw new Error("Invalid symbols parameter");
        
        const result = await rpcCall("explore_flow", { symbols });
        return {
          content: [{ type: "text", text: JSON.stringify(result, null, 2) }],
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

  const transport = new LspStdioServerTransport();
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
