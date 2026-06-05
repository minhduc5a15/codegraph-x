const { ParseWorkspace } = require('../build/codegraph_addon.node');

const files = ["src/main.cpp", "src/InMemoryGraphEngine.cpp"];
console.log("Parsing files:", files);

const graphData = ParseWorkspace(files);

const nodesArray = new Uint8Array(graphData.nodes); // NodeRecord is 24 bytes
const offsetsArray = new Uint32Array(graphData.offsets);
const edgesArray = new Uint32Array(graphData.edges); // EdgeRecord (target_id + type)
const stringPoolArray = new Uint8Array(graphData.stringPool);

console.log("Graph Data Received (Zero-copy):");
console.log("- Nodes Buffer Bytes:", graphData.nodes.byteLength);
console.log("- Offsets Count:", offsetsArray.length);
console.log("- Edges Count:", edgesArray.length / 2); // Roughly, EdgeRecord size varies
console.log("- String Pool Bytes:", stringPoolArray.length);

if (offsetsArray.length > 0) {
    console.log("Success: JS can read C++ memory directly!");
}
