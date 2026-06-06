#include "parser.hpp"

#include <tree_sitter/api.h>

#include <iostream>

extern "C" const TSLanguage *tree_sitter_cpp();

void test_parser_linkage() {
    TSParser *parser = ts_parser_new();
    if (!parser) {
        std::cerr << "Failed to create Tree-sitter parser." << std::endl;
        return;
    }

    if (ts_parser_set_language(parser, tree_sitter_cpp())) {
        std::cout << "Tree-sitter parser and C++ grammar linked successfully." << std::endl;
    } else {
        std::cerr << "Failed to set C++ language for Tree-sitter parser." << std::endl;
    }

    ts_parser_delete(parser);
}
