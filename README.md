# Codegraph-X

A high-performance, in-memory code graph engine that gives AI agents structural understanding of C++ codebases — instantly.

Codegraph-X parses source code into a persistent dependency graph held entirely in RAM, then exposes it as an [MCP](https://modelcontextprotocol.io/) (Model Context Protocol) server. Instead of grepping through files, your AI agent queries call paths, class hierarchies, and symbol relationships through a native C++ engine with sub-millisecond latency.

> **Early Stage** — Currently supports **C++ projects only**. APIs may change. Language support for TypeScript, Python, and Rust is planned.

---

## Why Codegraph-X?

Traditional code search (`grep`, `ripgrep`, `SearchText`) finds *text*. Codegraph-X finds *structure*:

| Capability | grep / text search | Codegraph-X |
|---|---|---|
| "Who calls `processTransaction`?" | Manual multi-file search | Instant caller/callee graph |
| "Show me the class hierarchy of `BaseAgent`" | Not possible | Direct traversal |
| "Find symbols related to `mutex worker queue`" | Keyword matches in comments & strings | Fuzzy token scoring on symbol names |
| Latency | Disk I/O per query | Zero-copy, in-memory (~μs) |

---

## Architecture

```
┌──────────────────────────────────────────────────────┐
│  AI Agent (Cursor, Claude Code, Gemini CLI, etc.)    │
│                      ▲ MCP (stdio)                   │
├──────────────────────┼───────────────────────────────┤
│  MCP Server          │  ts/mcp_server.ts             │
│                      ▼ IPC (Unix socket)             │
├──────────────────────────────────────────────────────┤
│  Daemon              ts/daemon.ts                    │
│  ┌────────────────────────────────────────────┐      │
│  │  C++ Native Addon (Node-API)               │      │
│  │  ┌──────────────────────────────────────┐  │      │
│  │  │  InMemoryGraphEngine                 │  │      │
│  │  │  • Nodes: contiguous NodeRecord[]    │  │      │
│  │  │  • Edges: CSR (offsets + targets)    │  │      │
│  │  │  • Strings: zero-copy string_pool    │  │      │
│  │  │  • Search: zero-alloc fuzzy scoring  │  │      │
│  │  └──────────────────────────────────────┘  │      │
│  │  ParallelParsingEngine                     │      │
│  │  (Tree-sitter API sourced from npm)        │      │
│  └────────────────────────────────────────────┘      │
└──────────────────────────────────────────────────────┘
```

**Key design decisions:**

- **Zero-copy string pool** — All symbol names and file paths live in a single `std::vector<char>`. `NodeRecord` stores offsets, not pointers or `std::string`. No heap fragmentation, no GC pressure.
- **CSR adjacency** — Edges stored in Compressed Sparse Row format. O(1) neighbor lookup per node.
- **Zero-allocation search** — Fuzzy token matching runs directly on `string_view` references into the pool using `std::search` with a case-insensitive comparator. No intermediate string copies in the hot loop.
- **Process isolation** — The C++ engine runs in a long-lived daemon process. The MCP server connects via Unix socket IPC — crash-safe, restartable, no re-parse on reconnect.

---

## MCP Tools

Codegraph-X exposes two tools to any MCP-compatible AI agent:

### `explore_codebase`

The primary entry point. Accepts a natural-language query and returns matching symbols, their source code, call paths (callers + callees), and structural metadata.

```json
{ "query": "worker queue mutex" }
```

Returns entry points ranked by fuzzy relevance, related symbols grouped by file, source code blocks (budget-limited), and full call path graphs.

### `read_node`

Fetches the complete source code for a specific node discovered by `explore_codebase`.

```json
{ "node_id": 42 }
```

---

## Getting Started

### Prerequisites

- **Node.js** 18+
- **CMake** 3.26+ with a **C++20** compiler (GCC 12+, Clang 15+, or MSVC 2022)

### Install as a Dependency

```bash
npm install @codegraph-x/core
```

Pre-built native binaries are available for **macOS (arm64, x64)**, **Linux (x64)**, and **Windows (x64)**.

### Build from Source

```bash
git clone https://github.com/minhduc5a15/codegraph-x.git
cd codegraph-x
npm install
npm run build        # Compiles TypeScript + C++ native addon
```

### Install for AI Agents

Interactive installer that auto-configures MCP for supported agents:

```bash
npx codegraph-x install
```

Currently supports **Gemini CLI** and **Claude Code**. The installer writes the appropriate MCP server config to each agent's settings file.

### Manual Usage

Start the MCP server on stdio, pointed at your C++ project:

```bash
npx codegraph-mcp /path/to/your/cpp/project
```

Or via the CLI:

```bash
node dist/cli.js mcp /path/to/your/cpp/project
```

---

## How the Search Works

When a query like `"best move algorithm expectimax"` arrives:

1. The raw query string passes through MCP → IPC → Daemon → C++ with **zero client-side processing**.
2. The C++ engine lowercases and tokenizes: `["best", "move", "algorithm", "expectimax"]`
3. For each of the N nodes in the graph:
   - Each token is matched (case-insensitive substring) against the node's **name** (+2 points) and **path** (+1 point)
   - All comparisons use `std::search` on `string_view` — **zero heap allocation**
4. Candidates are sorted by score, top 50 returned.
5. The daemon performs BFS expansion on the result set (callers/callees up to depth 3).

This finds `getBestMove`, `ExpectimaxAgent`, and any file in an `algorithm/` directory — all in a single pass.

---

## Project Structure

```
src/                          # C++ core
├── in_memory_graph_engine.*  # Graph storage, CSR edges, fuzzy search
├── parallel_parser.*         # Multi-threaded Tree-sitter parsing
├── ast_processor.*           # AST → node/edge extraction
├── binding.cpp               # Node-API bridge
├── string_pool.hpp           # Shared string interning pool
├── flat_symbol_map.hpp       # Open-addressing hash map for symbol resolution
├── file_buffer.hpp           # RAII file reader
└── watchdog.*                # Parent-death detection (anti-zombie)

ts/                           # TypeScript layer
├── index.ts                  # Public API + addon loader
├── codegraph.ts              # Graph traversal (BFS, edge cursors)
├── daemon.ts                 # Long-lived daemon (IPC server + file watcher)
├── mcp_server.ts             # MCP protocol handler (stdio)
├── cli.ts                    # CLI entry point
├── services/formatter.ts     # Output formatting for AI consumption
├── installer/                # Interactive MCP config installer
└── transports/               # MCP transport layer

vendor/                       # Vendored dependencies
├── tree-sitter/              # Tree-sitter core (C)
└── tree-sitter-cpp/          # C++ grammar
```


