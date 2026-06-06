#!/usr/bin/env node
import * as net from "net";
import * as fs from "fs";
import * as path from "path";
import * as crypto from "crypto";
import { spawn } from "child_process";
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";

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

  clientSocket.on("data", (chunk) => {
    buffer += chunk.toString();
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
        name: "get_node_info",
        description:
          "Retrieve metadata for a specific node in the graph by its ID.",
        inputSchema: {
          type: "object",
          properties: { nodeId: { type: "number" } },
          required: ["nodeId"],
        },
      },
      {
        name: "get_node_neighbors",
        description:
          "Retrieve the list of outgoing edges (neighbors) for a specific node.",
        inputSchema: {
          type: "object",
          properties: { nodeId: { type: "number" } },
          required: ["nodeId"],
        },
      },
    ],
  }));

  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: args } = request.params;
    try {
      if (name === "get_node_info" || name === "get_node_neighbors") {
        const nodeId = Number(args?.nodeId);
        if (isNaN(nodeId)) throw new Error("Invalid nodeId");

        const result = await rpcCall(name, { nodeId });
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

  const transport = new StdioServerTransport();
  await server.connect(transport);
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
