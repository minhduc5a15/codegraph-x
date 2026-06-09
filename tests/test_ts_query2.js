const Parser = require('tree-sitter');
const Cpp = require('tree-sitter-cpp');

const parser = new Parser();
parser.setLanguage(Cpp);

const sourceCode = `
namespace ns {
    void free_func() {}
}
int main() {
    ns::free_func();
}
`;

const tree = parser.parse(sourceCode);
const Query = Parser.Query;

const query = new Query(Cpp, `(call_expression function: (qualified_identifier name: (identifier) @target))`);

// find the call_expression node manually
function findCallExpr(node) {
    if (node.type === 'call_expression') return node;
    for (let child of node.children) {
        let r = findCallExpr(child);
        if (r) return r;
    }
    return null;
}

const callNode = findCallExpr(tree.rootNode);
console.log("Found callNode:", callNode.type);

const matches = query.matches(callNode);
console.log("Matches when rooted at callNode:", matches.length);
