const Parser = require('tree-sitter');
const Cpp = require('tree-sitter-cpp');

const parser = new Parser();
parser.setLanguage(Cpp);

const sourceCode = `
class A : public B, virtual C {};
void ns::free_func(int a, float b) {}
`;

const tree = parser.parse(sourceCode);
console.log(tree.rootNode.toString());
