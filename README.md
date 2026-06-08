# Codegraph-X ⚡️

[![npm version](https://badge.fury.io/js/@codegraph-x%2Fcore.svg)](https://badge.fury.io/js/@codegraph-x%2Fcore)

**Codegraph-X** is an ultra-fast, high-performance in-memory source code graph engine designed specifically to supercharge AI Agents (like Gemini, Cursor, Cline). 

By acting as an [MCP (Model Context Protocol)](https://modelcontextprotocol.io/) Server, it grants AI the ability to perform **"X-Ray Scans"** across your codebase in milliseconds. Instead of relying on slow, rigid textual searches (`grep`) or reading raw files line-by-line, AI agents can query the semantic execution flow, caller/callee graphs, and architecture directly from RAM.

> ⚠️ **EARLY PREVIEW / WORK IN PROGRESS**
> 
> Codegraph-X is currently in its early stages of development. **Currently, it ONLY supports parsing and analyzing C++ codebases.** Support for additional languages (TypeScript, Python, Rust, Go, etc.) is actively planned for future releases.

---

## 🚀 Features

- **In-Memory C++ Engine:** Builds and stores the entire codebase semantic graph directly in RAM for `0.1ms` query resolution.
- **X-Ray Vision for AI:** Exposes the `cx_xray_scan` MCP tool, delivering highly-structured, relevant source code chunks alongside their dependency graphs straight to the AI's context window.
- **Zero-Copy Node Bindings:** Utilizes Node-API (`node-addon-api`) to bridge the C++ graph memory directly to the Node.js MCP server without expensive serialization overhead.
- **Multi-threaded Parsing:** Leverages all available CPU cores to parse large codebases in parallel.
- **Interactive Installer:** One-command setup (`codegraph-x install`) to automatically integrate with popular AI IDEs and CLI agents.

## 📦 Installation

To install Codegraph-X globally, run:

```bash
npm install -g @codegraph-x/core
```

## 🛠 Setup & Integration

Codegraph-X comes with a built-in interactive installer to automatically configure itself with your favorite AI tools.

```bash
codegraph-x install
```

The installer will detect your installed AI agents (Gemini CLI, Cursor, Cline, etc.) and configure them to start the Codegraph-X MCP Server automatically.

## 💻 Manual Usage (For AI Agents)

Once configured, your AI agent will automatically spawn Codegraph-X in the background using the following command:

```bash
codegraph-x mcp
```

The AI can then invoke the `cx_xray_scan` tool to understand your codebase architecture instantly.

## 🏗 Roadmap

- [ ] Support for JavaScript/TypeScript (via Tree-sitter)
- [ ] Support for Python
- [ ] Cross-file symbol resolution enhancements
- [ ] Incremental graph updates (Watch mode)
- [ ] More granular CLI administrative commands

---

*Built with ❤️ for the AI Coding era.*
