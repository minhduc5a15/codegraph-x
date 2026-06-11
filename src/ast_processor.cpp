#include "ast_processor.hpp"

#include <tree_sitter/api.h>

#include <cstring>

static constexpr size_t NAME_FIELD_LEN = sizeof("name") - 1;
static constexpr size_t BASE_CLASS_CLAUSE_FIELD_LEN = sizeof("base_class_clause") - 1;
static constexpr size_t DECLARATOR_FIELD_LEN = sizeof("declarator") - 1;

static std::string get_node_text(const TSNode& node, const char* source_code) {
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (end > start) {
        return std::string{source_code + start, end - start};
    }
    return "";
}

static TSNode get_declarator_name(const TSNode& node) {
    const char* type = ts_node_type(node);
    if (strcmp(type, "identifier") == 0 || strcmp(type, "field_identifier") == 0 ||
        strcmp(type, "type_identifier") == 0 || strcmp(type, "qualified_identifier") == 0) {
        return node;
    }
    const uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        TSNode child = ts_node_child(node, i);
        const TSNode found = get_declarator_name(child);
        if (!ts_node_is_null(found)) return found;
    }
    constexpr TSNode null_node = {};
    return null_node;
}

static inline void handle_namespace(
    const TSNode& current,
    const char* source_code,
    GlobalScopeInterner& interner,
    StringPool& global_pool,
    uint32_t& current_scope,
    bool& pushed_scope
) {
    const TSNode id_node = ts_node_child_by_field_name(current, "name", NAME_FIELD_LEN);
    if (!ts_node_is_null(id_node)) {
        const std::string ns_name = get_node_text(id_node, source_code);
        current_scope = interner.get_or_create_scope(current_scope, ns_name, global_pool);
        pushed_scope = true;
    }
}

static inline void handle_class_specifier(
    const TSNode& current,
    const char* source_code,
    std::vector<ParallelParsingEngine::TempNodeRecord>& local_nodes,
    std::vector<ParallelParsingEngine::UnresolvedEdge>& local_edges,
    StringPool& local_pool,
    GlobalScopeInterner& interner,
    StringPool& global_pool,
    const uint32_t file_path_offset,
    uint32_t& current_node_id,
    uint32_t& current_scope,
    bool& pushed_scope
) {
    const TSNode id_node = ts_node_child_by_field_name(current, "name", NAME_FIELD_LEN);
    if (!ts_node_is_null(id_node)) {
        const std::string class_name = get_node_text(id_node, source_code);
        const uint32_t name_offset = local_pool.get_or_add(class_name);

        ParallelParsingEngine::TempNodeRecord class_node{};
        class_node.node_id = static_cast<uint32_t>(local_nodes.size());
        class_node.name_offset = name_offset;
        class_node.path_offset = file_path_offset;
        class_node.start_line = ts_node_start_point(current).row + 1;
        class_node.end_line = ts_node_end_point(current).row + 1;
        class_node.type = NodeType::CLASS;
        class_node.start_column = ts_node_start_point(current).column;
        class_node.flags = 0;
        class_node.scope_id = current_scope;

        current_node_id = class_node.node_id;
        current_scope = interner.get_or_create_scope(current_scope, class_name, global_pool);
        pushed_scope = true;
        local_nodes.push_back(class_node);

        TSNode base_clause = ts_node_child_by_field_name(current, "base_class_clause", BASE_CLASS_CLAUSE_FIELD_LEN);
        if (ts_node_is_null(base_clause)) {
            for (uint32_t i = 0; i < ts_node_child_count(current); ++i) {
                const TSNode child = ts_node_child(current, i);
                if (strcmp(ts_node_type(child), "base_class_clause") == 0) {
                    base_clause = child;
                    break;
                }
            }
        }

        if (!ts_node_is_null(base_clause)) {
            const uint32_t child_count = ts_node_child_count(base_clause);
            for (uint32_t i = 0; i < child_count; ++i) {
                TSNode child = ts_node_child(base_clause, i);
                TSNode base_id = get_declarator_name(child);
                if (!ts_node_is_null(base_id)) {
                    ParallelParsingEngine::UnresolvedEdge edge{};
                    edge.source_local_index = class_node.node_id;
                    edge.source_scope_id = class_node.scope_id;
                    edge.target_symbol_offset = local_pool.get_or_add(get_node_text(base_id, source_code));
                    edge.expected_target_kind = NodeType::CLASS;
                    edge.type = EdgeType::INHERITS;
                    local_edges.push_back(edge);
                }
            }
        }
    }
}

static inline void handle_function_definition(
    const TSNode& current,
    const char* source_code,
    std::vector<ParallelParsingEngine::TempNodeRecord>& local_nodes,
    StringPool& local_pool,
    const uint32_t file_path_offset,
    uint32_t& current_node_id,
    const uint32_t current_scope
) {
    const TSNode declarator = ts_node_child_by_field_name(current, "declarator", DECLARATOR_FIELD_LEN);
    const TSNode id_node = get_declarator_name(declarator);
    if (!ts_node_is_null(id_node)) {
        const std::string func_name = get_node_text(id_node, source_code);

        ParallelParsingEngine::TempNodeRecord fn_node{};
        fn_node.node_id = static_cast<uint32_t>(local_nodes.size());
        fn_node.name_offset = local_pool.get_or_add(func_name);
        fn_node.path_offset = file_path_offset;
        fn_node.start_line = ts_node_start_point(current).row + 1;
        fn_node.end_line = ts_node_end_point(current).row + 1;
        fn_node.type = NodeType::FUNCTION;
        fn_node.start_column = ts_node_start_point(current).column;
        fn_node.flags = 0;
        fn_node.scope_id = current_scope;

        current_node_id = fn_node.node_id;
        local_nodes.push_back(fn_node);
    }
}

static void handle_call_expression(
    const TSNode& current,
    const char* source_code,
    std::vector<ParallelParsingEngine::UnresolvedEdge>& local_edges,
    StringPool& local_pool,
    const uint32_t current_node_id,
    const uint32_t current_scope,
    const TSQuery* call_query,
    TSQueryCursor* query_cursor
) {
    ts_query_cursor_exec(query_cursor, call_query, current);
    TSQueryMatch match;
    if (ts_query_cursor_next_match(query_cursor, &match)) {
        if (match.capture_count > 0) {
            const TSNode id_node = match.captures[0].node;
            ParallelParsingEngine::UnresolvedEdge edge{};
            edge.source_local_index = current_node_id;
            edge.source_scope_id = current_scope;
            edge.target_symbol_offset = local_pool.get_or_add(get_node_text(id_node, source_code));
            edge.expected_target_kind = NodeType::FUNCTION;
            edge.type = EdgeType::CALLS;
            local_edges.push_back(edge);
        }
    }
}

void ASTProcessor::process_syntax_tree(
    const TSTree* tree,
    const uint32_t file_path_offset,
    const char* source_code,
    std::vector<ParallelParsingEngine::TempNodeRecord>& local_nodes,
    std::vector<ParallelParsingEngine::UnresolvedEdge>& local_edges,
    const TSQuery* call_query,
    TSQueryCursor* query_cursor,
    StringPool& local_pool,
    GlobalScopeInterner& global_scope_interner,
    StringPool& global_pool
) {
    ParallelParsingEngine::TempNodeRecord file_node{};
    file_node.node_id = static_cast<uint32_t>(local_nodes.size());
    file_node.type = NodeType::FILE;
    file_node.start_column = 0;
    file_node.flags = 0;
    file_node.path_offset = file_path_offset;
    file_node.name_offset = file_path_offset;
    file_node.scope_id = 0;
    local_nodes.push_back(file_node);

    const TSNode root = ts_tree_root_node(tree);
    if (ts_node_is_null(root)) return;

    TSTreeCursor cursor = ts_tree_cursor_new(root);
    bool reached_root = false;

    struct State {
        uint32_t node_id;
        uint32_t scope_id;
        bool pushed_scope;
    };
    std::vector<State> state_stack;
    state_stack.push_back({file_node.node_id, 0, false});

    while (!reached_root) {
        TSNode current = ts_tree_cursor_current_node(&cursor);
        const char* type = ts_node_type(current);

        uint32_t current_node_id = state_stack.back().node_id;
        uint32_t current_scope_id = state_stack.back().scope_id;
        bool pushed_scope = false;

        if (strcmp(type, "namespace_definition") == 0) {
            handle_namespace(current, source_code, global_scope_interner, global_pool, current_scope_id, pushed_scope);
        } else if (strcmp(type, "class_specifier") == 0 || strcmp(type, "struct_specifier") == 0) {
            handle_class_specifier(
                current,
                source_code,
                local_nodes,
                local_edges,
                local_pool,
                global_scope_interner,
                global_pool,
                file_path_offset,
                current_node_id,
                current_scope_id,
                pushed_scope
            );
        } else if (strcmp(type, "function_definition") == 0) {
            handle_function_definition(
                current, source_code, local_nodes, local_pool, file_path_offset, current_node_id, current_scope_id
            );
        } else if (strcmp(type, "call_expression") == 0) {
            handle_call_expression(
                current,
                source_code,
                local_edges,
                local_pool,
                current_node_id,
                current_scope_id,
                call_query,
                query_cursor
            );
        }

        if (ts_tree_cursor_goto_first_child(&cursor)) {
            state_stack.push_back({current_node_id, current_scope_id, pushed_scope});
            continue;
        }

        if (ts_tree_cursor_goto_next_sibling(&cursor)) {
            continue;
        }

        do {
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                reached_root = true;
                break;
            }
            state_stack.pop_back();
        } while (!ts_tree_cursor_goto_next_sibling(&cursor));
    }

    ts_tree_cursor_delete(&cursor);
}