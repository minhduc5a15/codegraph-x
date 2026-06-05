#include <iostream>
#include <string>
#include <vector>

#include "InMemoryGraphEngine.hpp"
#include "parallel_parser.hpp"
#include "parser.hpp"

int main() {
    std::cout << "Codegraph-X Native CLI Initialized." << std::endl;

    // Verify tree-sitter linkage
    test_parser_linkage();

    std::vector<std::string> mock_files;
    for (int i = 1; i <= 100; ++i) {
        mock_files.push_back("file" + std::to_string(i) + ".cpp");
    }

    ParallelParsingEngine engine;
    std::cout << "Starting parallel parsing of " << mock_files.size() << " files..." << std::endl;
    engine.execute(mock_files);

    std::cout << "Parallel parsing finished." << std::endl;
    std::cout << "Total Nodes collected: " << engine.get_nodes().size() << std::endl;
    std::cout << "Total Edges collected: " << engine.get_edges().size() << std::endl;

    return 0;
}
