import { Codegraph, GraphData } from './Codegraph.js';

export * from './Codegraph.js';

// @ts-ignore
const addon = require('../build/codegraph_addon.node');

export function analyzeWorkspace(files: string[]): Codegraph {
    const rawData: GraphData = addon.ParseWorkspace(files);
    return new Codegraph(rawData);
}
