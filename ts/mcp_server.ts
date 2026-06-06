import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";
import { analyzeWorkspace } from "./index.js";
import * as fs from "fs";
import * as path from "path";

function getAllFiles(dirPath: string, arrayOfFiles: string[] = []): string[] {
  const files = fs.readdirSync(dirPath);

  files.forEach((file) => {
    const fullPath = path.join(dirPath, file);
    if (fs.statSync(fullPath).isDirectory()) {
      if (!["node_modules", "build", ".git", "dist", "vendor"].includes(file)) {
        getAllFiles(fullPath, arrayOfFiles);
      }
    } else {
      const ext = path.extname(file);
      if ([".cpp", ".hpp", ".h", ".c", ".ts", ".js"].includes(ext)) {
        arrayOfFiles.push(fullPath);
      }
    }
  });

  return arrayOfFiles;
}

async function main() {
  const files = getAllFiles(process.cwd());
  console.error(`Found ${files.length} source files in workspace.`);

  // Initialize graph engine
  const graph = analyzeWorkspace(files);

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
