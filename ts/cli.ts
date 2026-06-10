#!/usr/bin/env node
import { Command } from 'commander';
import { runInstaller } from './installer/cli';

const program = new Command();

program.name('codegraph-x').description('Codegraph-X: High performance code graph engine').version('1.0.0');

program
  .command('install')
  .description('Interactive installer to configure MCP servers for AI agents')
  .action(async () => {
    await runInstaller();
  });

program
  .command('mcp [targetDir]')
  .description('Start the MCP Server on stdio')
  .action((targetDir) => {
    process.argv[2] = targetDir || process.cwd();
    require('./mcp_server');
  });

program.parse();
