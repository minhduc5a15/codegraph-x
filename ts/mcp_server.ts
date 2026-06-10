#!/usr/bin/env node
import * as net from 'net';
import * as fs from 'fs';
import * as path from 'path';
import * as crypto from 'crypto';
import { spawn } from 'child_process';
import * as readline from 'readline';
import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { CallToolRequestSchema, ListToolsRequestSchema } from '@modelcontextprotocol/sdk/types.js';
import { AutoDetectStdioServerTransport } from './transports/auto-detect.js';
import { formatXRayResult } from './services/formatter.js';

async function main() {
  if (process.argv.includes('--help') || process.argv.includes('-h')) {
    console.error('Usage: codegraph-mcp [workspace_directory]');
    process.exit(0);
  }

  const targetDir = process.argv[2] || process.cwd();
  if (!fs.existsSync(targetDir) || !fs.statSync(targetDir).isDirectory()) {
    console.error(`[Error] Invalid directory: ${targetDir}`);
    process.exit(1);
  }

  const hash = crypto.createHash('md5').update(targetDir).digest('hex');
  const socketPath = process.platform === 'win32' ? `\\\\.\\pipe\\codegraph-x-${hash}` : `/tmp/codegraph-x-${hash}.sock`;

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
        socket.off('connect', onConnect);
        socket.off('error', onError);
      };

      socket.on('connect', onConnect);
      socket.on('error', onError);
    });

  try {
    clientSocket = await tryConnect();
  } catch (e) {
    // Spawn daemon if not running
    const daemonScript = path.join(__dirname, 'daemon.js');
    const child = spawn(process.execPath, [daemonScript, targetDir, socketPath], {
      stdio: 'ignore',
    });
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
    console.error('Failed to connect to Codegraph-X Daemon.');
    process.exit(1);
  }

  // IPC Client logic
  let reqId = 0;
  const pendingRequests = new Map<number, { resolve: (res: any) => void; reject: (err: any) => void }>();

  const rl = readline.createInterface({
    input: clientSocket,
    terminal: false,
    historySize: 0,
    crlfDelay: Infinity,
  });

  rl.on('line', (line) => {
    if (!line.trim()) return;
    try {
      const msg = JSON.parse(line);
      if (msg.id !== undefined && pendingRequests.has(msg.id)) {
        if (msg.error) {
          pendingRequests.get(msg.id)!.reject(new Error(msg.error));
        } else {
          pendingRequests.get(msg.id)!.resolve(msg.result);
        }
        pendingRequests.delete(msg.id);
      }
    } catch (err) {
      console.error('[IPC] Failed to parse JSON line:', err);
    }
  });

  const rpcCall = (action: string, params: any): Promise<any> => {
    return new Promise((resolve, reject) => {
      const id = ++reqId;
      pendingRequests.set(id, { resolve, reject });
      clientSocket!.write(JSON.stringify({ id, action, ...params }) + '\n');
    });
  };

  // Start MCP Server
  const server = new Server({ name: 'codegraph-x', version: '1.0.0' }, { capabilities: { tools: {} } });

  server.setRequestHandler(ListToolsRequestSchema, async () => ({
    tools: [
      {
        name: 'explore_codebase',
        description:
          'PRIMARY TOOL — ALWAYS call FIRST for almost any question about the codebase: how does X work, architecture, a bug, where/what is X, or surveying an area. \n' +
          'DO NOT USE SearchText or ReadFile before using this tool. \n' +
          'This tool connects directly to an In-Memory C++ Engine, returning structural metadata, execution call paths, and the most important source code blocks instantly. \n' +
          'If you need to view the full source code of a specific node omitted in the results, use the `read_node` tool with the ID provided in the scan results.',
        inputSchema: {
          type: 'object',
          properties: {
            query: {
              type: 'string',
              description: "Symbol names, file names, or a natural language question (e.g. 'worker queue mutex', 'How does the secure allocator work?')",
            },
          },
          required: ['query'],
        },
      },
      {
        name: 'read_node',
        description: 'Fetches the full raw source code for a specific node/symbol discovered by explore_codebase. You must provide the exact integer node_id.',
        inputSchema: {
          type: 'object',
          properties: {
            node_id: {
              type: 'number',
              description: 'The integer ID of the node to read, obtained from cx_xray_scan results.',
            },
          },
          required: ['node_id'],
        },
      },
    ],
  }));

  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: args } = request.params;
    try {
      if (name === 'explore_codebase' || name === 'cx_xray_scan') {
        const query = typeof args?.query === 'string' ? args.query : '';
        if (!query.trim()) throw new Error('Invalid query parameter');

        const nodes: any[] = await rpcCall('explore_flow', { query });

        const outputText = await formatXRayResult(nodes, process.cwd());

        return {
          content: [{ type: 'text', text: outputText }],
        };
      } else if (name === 'read_node' || name === 'cx_read_node') {
        const parsedId = Number(args?.node_id);
        const node_id = !isNaN(parsedId) ? parsedId : -1;
        if (node_id < 0) throw new Error('Invalid node_id parameter');

        const outputText = await rpcCall('read_node', { node_id });
        return {
          content: [{ type: 'text', text: outputText }],
        };
      }
      throw new Error(`Tool not found: ${name}`);
    } catch (error: any) {
      return {
        isError: true,
        content: [{ type: 'text', text: error.message }],
      };
    }
  });

  const transport = new AutoDetectStdioServerTransport();
  await server.connect(transport as any);
  console.error(`Codegraph-X MCP Client running on stdio (Connected to daemon ${socketPath})`);

  const shutdown = async () => {
    await server.close();
    clientSocket?.end();
    process.exit(0);
  };

  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

main().catch((error) => {
  console.error('Fatal error in MCP client:', error);
  process.exit(1);
});
