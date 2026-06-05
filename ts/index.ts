import { McpServer } from "./mcp_server.js";

async function main() {
    console.log("Codegraph-X CLI Initialized.");
    const server = new McpServer();
    await server.run();
}

main().catch(console.error);
