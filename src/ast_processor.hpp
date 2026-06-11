#pragma once

#include <vector>

#include "parallel_parser.hpp"
#include "scope_interner.hpp"
#include "string_pool.hpp"

struct TSTree;
struct TSQuery;
struct TSQueryCursor;

class ASTProcessor {
public:
    static void process_syntax_tree(
        const TSTree* tree,
        uint32_t file_path_offset,
        const char* source_code,
        std::vector<ParallelParsingEngine::TempNodeRecord>& local_nodes,
        std::vector<ParallelParsingEngine::UnresolvedEdge>& local_edges,
        const TSQuery* call_query,
        TSQueryCursor* query_cursor,
        StringPool& local_pool,
        GlobalScopeInterner& global_scope_interner,
        StringPool& global_pool
    );
};
