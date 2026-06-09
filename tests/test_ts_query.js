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

try {
    const query = new Query(Cpp, `(call_expression function: (qualified_identifier name: (identifier) @target))`);
    const matches = query.matches(tree.rootNode);
    console.log("Matches:", matches.length);
} catch (e) {
    console.error("Query Error:", e.message);
}
