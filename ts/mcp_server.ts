#!/usr/bin/env node
import * as fs from 'fs';
import * as path from 'path';
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";
import { analyzeWorkspace } from "./index.js";

function collectSourceFiles(dir: string, fileList: string[] = []): string[] {
    const files = fs.readdirSync(dir);
    for (const file of files) {
        if (file === 'node_modules' || file === '.git') continue;
        const fullPath = path.join(dir, file);
        try {
            const stat = fs.statSync(fullPath);
            if (stat.isDirectory()) {
                collectSourceFiles(fullPath, fileList);
            } else {
                const ext = path.extname(file);
                if (ext === '.cpp' || ext === '.hpp' || ext === '.c' || ext === '.h') {
                    fileList.push(fullPath);
                }
            }
        } catch (error) {
            console.error(`[Warning] Skipping file/dir ${fullPath} due to access error`);
            continue;
        }
    }
    return fileList;
}

async function main() {
  if (process.argv.includes('--help') || process.argv.includes('-h')) {
    console.log("Usage: codegraph-mcp [workspace_directory]");
    process.exit(0);
  }

  const targetDir = process.argv[2] || process.cwd();

  if (!fs.existsSync(targetDir) || !fs.statSync(targetDir).isDirectory()) {
    console.error(`[Error] Invalid directory: ${targetDir}`);
    process.exit(1);
  }

  const filesArray = collectSourceFiles(targetDir);
  const graph = analyzeWorkspace(filesArray);

  const server = new Server(
    {
      name: "codegraph-x",
      version: "1.0.0",
    },
    {
      capabilities: {
        tools: {},
      },
    }
  );

  // Register list of tools
  server.setRequestHandler(ListToolsRequestSchema, async () => {
    return {
      tools: [
        {
          name: "get_node_info",
          description: "Retrieve metadata for a specific node in the graph by its ID.",
          inputSchema: {
            type: "object",
            properties: {
              nodeId: { type: "number" },
            },
            required: ["nodeId"],
          },
        },
        {
          name: "get_node_neighbors",
          description: "Retrieve the list of outgoing edges (neighbors) for a specific node.",
          inputSchema: {
            type: "object",
            properties: {
              nodeId: { type: "number" },
            },
            required: ["nodeId"],
          },
        },
      ],
    };
  });

  // Handle tool calls
  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: args } = request.params;

    try {
      if (name === "get_node_info") {
        const nodeId = Number(args?.nodeId);
        if (isNaN(nodeId)) throw new Error("Invalid nodeId");

        const node = graph.getNode(nodeId);
        return {
          content: [{ type: "text", text: JSON.stringify(node, null, 2) }],
        };
      }

      if (name === "get_node_neighbors") {
        const nodeId = Number(args?.nodeId);
        if (isNaN(nodeId)) throw new Error("Invalid nodeId");

        const { cursor, startIdx, endIdx } = graph.getEdgeCursor(nodeId);
        const neighbors = [];
        for (let i = startIdx; i < endIdx; i++) {
          cursor.moveTo(i);
          neighbors.push({
            targetId: cursor.targetId,
            type: cursor.type,
          });
        }

        return {
          content: [{ type: "text", text: JSON.stringify(neighbors, null, 2) }],
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

  // Connect to stdio transport
  const transport = new StdioServerTransport();
  await server.connect(transport);

  console.error("Codegraph-X MCP Server running on stdio");

  // Graceful shutdown
  const shutdown = async () => {
    await server.close();
    process.exit(0);
  };

  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
}

main().catch((error) => {
  console.error("Fatal error in MCP server:", error);
  process.exit(1);
});
