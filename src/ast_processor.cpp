#include "ast_processor.hpp"

#include <tree_sitter/api.h>

#include <string>

static std::string get_node_text(const TSNode& node, const char* source_code) {
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (end > start) {
        return std::string{source_code + start, end - start};
    }
    return "";
}

static TSNode resolve_identifier_node(const TSNode& node, const UnwrapConfig& config) {
    if (ts_node_is_null(node)) return node;
    if (config.accept_types.empty()) return node;

    std::vector<TSNode> queue = {node};
    while (!queue.empty()) {
        const TSNode current = queue.front();
        queue.erase(queue.begin());

        const char* type = ts_node_type(current);
        if (config.accept_types.contains(type)) {
            return current;
        }

        const uint32_t child_count = ts_node_child_count(current);
        for (uint32_t i = 0; i < child_count; ++i) {
            TSNode child = ts_node_child(current, i);
            const char* child_type = ts_node_type(child);
            if (!config.skip_types.contains(child_type)) {
                queue.push_back(child);
            }
        }
    }
    return node;  // Fallback to the original node if no identifier found
}

struct ScopeState {
    uint32_t end_byte;
    uint32_t scope_id;
    uint32_t parent_node_id;
};

void ASTProcessor::process_syntax_tree(
    const TSTree* tree,
    uint32_t file_path_offset,
    const std::string& file_path,
    const char* source_code,
    std::vector<ParallelParsingEngine::TempNodeRecord>& local_nodes,
    std::vector<ParallelParsingEngine::UnresolvedEdge>& local_edges,
    const TSQuery* query,
    TSQueryCursor* query_cursor,
    bool file_scoped,
    const UnwrapConfig& unwrap_config,
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

    uint32_t root_scope_id = 0;
    if (file_scoped) {
        root_scope_id = global_scope_interner.get_or_create_scope(0, file_path, global_pool);
    }
    file_node.scope_id = root_scope_id;
    local_nodes[0].scope_id = root_scope_id;

    const TSNode root = ts_tree_root_node(tree);
    if (ts_node_is_null(root)) return;

    std::vector<ScopeState> scope_stack;
    scope_stack.push_back({0xFFFFFFFF, root_scope_id, file_node.node_id});

    ts_query_cursor_exec(query_cursor, query, root);
    TSQueryMatch match;

    while (ts_query_cursor_next_match(query_cursor, &match)) {
        TSNode def_node = {0};
        TSNode name_node = {0};
        TSNode ref_node = {0};
        std::string def_type;
        std::string ref_type;

        for (uint16_t i = 0; i < match.capture_count; ++i) {
            uint32_t name_len;
            const char* capture_name = ts_query_capture_name_for_id(query, match.captures[i].index, &name_len);
            std::string c_name(capture_name, name_len);

            if (c_name.starts_with("definition.")) {
                def_node = match.captures[i].node;
                def_type = c_name.substr(11);
            } else if (c_name.starts_with("reference.")) {
                ref_node = match.captures[i].node;
                ref_type = c_name.substr(10);
            } else if (c_name == "name") {
                name_node = match.captures[i].node;
            }
        }

        TSNode main_node = !ts_node_is_null(def_node) ? def_node : (!ts_node_is_null(ref_node) ? ref_node : name_node);
        if (ts_node_is_null(main_node)) continue;

        uint32_t match_start_byte = ts_node_start_byte(main_node);
        while (scope_stack.size() > 1 && scope_stack.back().end_byte <= match_start_byte) {
            scope_stack.pop_back();
        }

        uint32_t current_scope = scope_stack.back().scope_id;
        uint32_t current_parent_id = scope_stack.back().parent_node_id;

        if (!def_type.empty() && !ts_node_is_null(name_node)) {
            TSNode real_name_node = resolve_identifier_node(name_node, unwrap_config);
            std::string name_str = get_node_text(real_name_node, source_code);

            if (def_type == "namespace") {
                uint32_t new_scope = global_scope_interner.get_or_create_scope(current_scope, name_str, global_pool);
                scope_stack.push_back({ts_node_end_byte(def_node), new_scope, current_parent_id});
            } else {
                auto ntype = NodeType::CLASS;
                if (def_type == "function") ntype = NodeType::FUNCTION;
                if (def_type == "method") ntype = NodeType::METHOD;
                if (def_type == "class" || def_type == "struct") ntype = NodeType::CLASS;

                ParallelParsingEngine::TempNodeRecord node_rec{};
                node_rec.node_id = static_cast<uint32_t>(local_nodes.size());
                node_rec.name_offset = local_pool.get_or_add(name_str);
                node_rec.path_offset = file_path_offset;
                node_rec.start_line = ts_node_start_point(def_node).row + 1;
                node_rec.end_line = ts_node_end_point(def_node).row + 1;
                node_rec.type = ntype;
                node_rec.start_column = ts_node_start_point(def_node).column;
                node_rec.flags = 0;
                node_rec.scope_id = current_scope;

                local_nodes.push_back(node_rec);

                if (ntype == NodeType::CLASS) {
                    uint32_t new_scope =
                        global_scope_interner.get_or_create_scope(current_scope, name_str, global_pool);
                    scope_stack.push_back({ts_node_end_byte(def_node), new_scope, node_rec.node_id});
                } else if (ntype == NodeType::FUNCTION || ntype == NodeType::METHOD) {
                    scope_stack.push_back({ts_node_end_byte(def_node), current_scope, node_rec.node_id});
                }
            }
        }

        if (!ref_type.empty() && !ts_node_is_null(name_node)) {
            TSNode real_name_node = resolve_identifier_node(name_node, unwrap_config);
            std::string name_str = get_node_text(real_name_node, source_code);
            ParallelParsingEngine::UnresolvedEdge edge{};
            edge.source_local_index = current_parent_id;
            edge.source_scope_id = current_scope;
            edge.target_symbol_offset = local_pool.get_or_add(name_str);

            if (ref_type == "call") {
                edge.expected_target_kind = NodeType::FUNCTION;
                edge.type = EdgeType::CALLS;
                local_edges.push_back(edge);
            } else if (ref_type == "class" || ref_type == "base" || ref_type == "type") {
                edge.expected_target_kind = NodeType::CLASS;
                edge.type = EdgeType::INHERITS;
                local_edges.push_back(edge);
            }
        }
    }
}