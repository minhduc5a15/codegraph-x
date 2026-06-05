#include <iostream>
#include "InMemoryGraphEngine.hpp"
#include "parser.hpp"

int main() {
    std::cout << "Codegraph-X Native CLI Initialized." << std::endl;
    
    // Verify tree-sitter linkage
    test_parser_linkage();
    
    InMemoryGraphEngine engine;
    return 0;
}
