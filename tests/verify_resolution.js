const { Codegraph, NodeType, EdgeType } = require('../dist/Codegraph.js');
const addon = require('../build/codegraph_addon.node');
const path = require('path');

async function updateWorkspace(files) {
    return new Codegraph(await addon.UpdateWorkspace(files));
}

async function run() {
    const testFile = path.resolve(__dirname, 'test_code.cpp');
    const graph = await updateWorkspace([testFile]);

    console.log("All Nodes:");
    for (let i = 0; i < graph.nodeCount; i++) {
        try {
            const node = graph.getNode(i);
            console.log(`Node ${i}: ${node.name}`);
        } catch(e) {}
    }

    const innerFuncs = graph.searchNodesByName('outer::inner::inner_func');
    if (innerFuncs.length === 0) {
        console.error("Failed to find inner_func");
        return;
    }

    const innerFuncId = innerFuncs[0].id;
    const { cursor, startIdx, endIdx } = graph.getEdgeCursor(innerFuncId);

    console.log("Edges for inner_func:");
    for (let i = startIdx; i < endIdx; i++) {
        cursor.moveTo(i);
        const targetNode = graph.getNode(cursor.targetId);
        console.log(`- type=${cursor.type} -> ${targetNode.name}`);
    }
}
run();
