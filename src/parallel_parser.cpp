#include "parallel_parser.hpp"

#include <tree_sitter/api.h>

#include <cstring>
#include <fstream>

extern "C" const TSLanguage* tree_sitter_cpp();

class FileBuffer {
public:
    explicit FileBuffer(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return;

        std::streamsize size = file.tellg();
        if (size <= 0) return;

        file.seekg(0, std::ios::beg);
        buffer.resize(static_cast<size_t>(size));
        if (file.read(buffer.data(), size)) {
            valid = true;
        }
    }

    bool is_valid() const { return valid; }
    const char* data() const { return buffer.data(); }
    size_t size() const { return buffer.size(); }

private:
    std::vector<char> buffer;
    bool valid = false;
};

#include <unordered_set>

ParallelParsingEngine::~ParallelParsingEngine() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        stop_workers = true;
    }
    cv.notify_all();
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ParallelParsingEngine::initialize_workers() {
    if (workers_initialized) return;
    unsigned int thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) thread_count = 2;
    for (unsigned int i = 0; i < thread_count; ++i) {
        workers.emplace_back(&ParallelParsingEngine::worker_thread_func, this);
    }
    workers_initialized = true;
}

void ParallelParsingEngine::execute(const std::vector<std::string>& files_to_parse) {
    initialize_workers();

    // Xóa Data Cũ (Stale Data)
    {
        std::lock_guard<std::mutex> lock(map_mutex);
        std::unordered_set<std::string> valid_files(files_to_parse.begin(), files_to_parse.end());
        for (auto it = file_data_map.begin(); it != file_data_map.end();) {
            if (valid_files.find(it->first) == valid_files.end()) {
                it = file_data_map.erase(it);
            } else {
                ++it;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        while (!task_queue.empty()) task_queue.pop();
        for (const auto& file : files_to_parse) {
            task_queue.push(file);
        }
        active_tasks = files_to_parse.size();
    }

    if (files_to_parse.empty()) return;
    cv.notify_all();

    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        cv_main.wait(lock, [this] { return active_tasks == 0; });
    }
}

void ParallelParsingEngine::worker_thread_func() {
    TSParser* local_parser = ts_parser_new();
    ts_parser_set_language(local_parser, tree_sitter_cpp());

    while (true) {
        std::string file_path;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            cv.wait(lock, [this] { return !task_queue.empty() || stop_workers; });

            if (task_queue.empty() && stop_workers) {
                break;
            }

            if (!task_queue.empty()) {
                file_path = std::move(task_queue.front());
                task_queue.pop();
            } else {
                continue;
            }
        }

        FileBuffer file_buffer(file_path);
        if (file_buffer.is_valid()) {
            std::vector<TempNodeRecord> local_nodes;
            std::vector<UnresolvedEdge> local_edges;

            TSTree* syntax_tree = ts_parser_parse_string(local_parser, nullptr, file_buffer.data(), file_buffer.size());
            if (syntax_tree) {
                process_syntax_tree(syntax_tree, file_path, file_buffer.data(), local_nodes, local_edges);
                ts_tree_delete(syntax_tree);
            }

            // Store in per-file map
            {
                std::lock_guard<std::mutex> lock(map_mutex);
                FileData data;
                data.nodes = std::move(local_nodes);
                data.edges = std::move(local_edges);
                file_data_map[file_path] = std::move(data);
            }
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            active_tasks--;
            if (active_tasks == 0) {
                cv_main.notify_all();
            }
        }
    }

    ts_parser_delete(local_parser);
}

void ParallelParsingEngine::build_flat_graph() {
    std::lock_guard<std::mutex> lock(map_mutex);
    global_nodes.clear();
    global_edges.clear();

    std::unordered_map<std::string, uint32_t> global_symbols;
    uint32_t current_id = 0;

    for (const auto& pair : file_data_map) {
        const auto& data = pair.second;
        for (const auto& n : data.nodes) {
            TempNodeRecord updated_node = n;
            updated_node.node_id = current_id;
            if (n.type == NodeType::FUNCTION || n.type == NodeType::CLASS || n.type == NodeType::METHOD) {
                if (!n.name.empty()) {
                    global_symbols[n.name] = current_id;
                }
            }
            global_nodes.push_back(updated_node);
            current_id++;
        }
    }

    uint32_t local_offset = 0;
    for (const auto& pair : file_data_map) {
        const auto& data = pair.second;
        for (const auto& e : data.edges) {
            RawEdge new_edge;
            uint32_t absolute_source_id = e.source_node_id + local_offset;
            new_edge.source_node_id = absolute_source_id;
            new_edge.type = e.type;

            auto it = global_symbols.find(e.target_symbol);
            if (it != global_symbols.end()) {
                new_edge.target_node_id = it->second;
                global_edges.push_back(new_edge);
            } else {
                if (absolute_source_id < global_nodes.size()) {
                    std::string source_name = global_nodes[absolute_source_id].name;
                    size_t last_colon = source_name.rfind("::");
                    if (last_colon != std::string::npos) {
                        std::string scope_prefix = source_name.substr(0, last_colon + 2);
                        std::string scoped_target = scope_prefix + e.target_symbol;
                        auto it2 = global_symbols.find(scoped_target);
                        if (it2 != global_symbols.end()) {
                            new_edge.target_node_id = it2->second;
                            global_edges.push_back(new_edge);
                        }
                    }
                }
            }
        }
        local_offset += static_cast<uint32_t>(data.nodes.size());
    }
}

static std::string get_node_text(TSNode node, const char* source_code) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (end > start) {
        return std::string(source_code + start, end - start);
    }
    return "";
}

static TSNode find_first_identifier(TSNode node) {
    const char* type = ts_node_type(node);
    if (strcmp(type, "identifier") == 0 || strcmp(type, "field_identifier") == 0) {
        return node;
    }
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        TSNode child = ts_node_child(node, i);
        TSNode found = find_first_identifier(child);
        if (!ts_node_is_null(found)) return found;
    }
    TSNode null_node;
    std::memset(&null_node, 0, sizeof(null_node));
    return null_node;
}

void ParallelParsingEngine::process_syntax_tree(TSTree* tree, const std::string& file_path, const char* source_code,
                                                std::vector<TempNodeRecord>& local_nodes, std::vector<UnresolvedEdge>& local_edges) {
    TempNodeRecord file_node{};
    file_node.node_id = static_cast<uint32_t>(local_nodes.size());
    file_node.type = NodeType::FILE;
    file_node.path = file_path;
    file_node.name = file_path;
    local_nodes.push_back(file_node);

    TSNode root = ts_tree_root_node(tree);
    if (ts_node_is_null(root)) return;

    TSTreeCursor cursor = ts_tree_cursor_new(root);
    bool reached_root = false;

    struct State {
        uint32_t node_id;
        std::string fqn_prefix;
        bool created_scope;
    };
    std::vector<State> state_stack;
    state_stack.push_back({file_node.node_id, "", false});

    while (!reached_root) {
        TSNode current = ts_tree_cursor_current_node(&cursor);
        const char* type = ts_node_type(current);

        State current_state = state_stack.back();
        bool created_scope = false;

        if (strcmp(type, "namespace_definition") == 0) {
            TSNode id_node = ts_node_child_by_field_name(current, "name", 4);
            if (!ts_node_is_null(id_node)) {
                std::string ns_name = get_node_text(id_node, source_code);
                current_state.fqn_prefix += ns_name + "::";
                created_scope = true;
            }
        } else if (strcmp(type, "class_specifier") == 0 || strcmp(type, "struct_specifier") == 0) {
            TSNode id_node = ts_node_child_by_field_name(current, "name", 4);
            if (!ts_node_is_null(id_node)) {
                std::string class_name = get_node_text(id_node, source_code);

                TempNodeRecord class_node;
                class_node.node_id = static_cast<uint32_t>(local_nodes.size());
                class_node.name = current_state.fqn_prefix + class_name;
                class_node.path = file_path;
                class_node.start_line = ts_node_start_point(current).row + 1;
                class_node.end_line = ts_node_end_point(current).row + 1;
                class_node.type = NodeType::CLASS;

                current_state.node_id = class_node.node_id;
                current_state.fqn_prefix += class_name + "::";
                created_scope = true;
                local_nodes.push_back(class_node);

                TSNode base_clause = ts_node_child_by_field_name(current, "base_class_clause", 17);
                if (!ts_node_is_null(base_clause)) {
                    uint32_t child_count = ts_node_child_count(base_clause);
                    for (uint32_t i = 0; i < child_count; ++i) {
                        TSNode child = ts_node_child(base_clause, i);
                        if (strcmp(ts_node_type(child), "base_class_specifier") == 0) {
                            TSNode base_id = find_first_identifier(child);
                            if (!ts_node_is_null(base_id)) {
                                UnresolvedEdge edge;
                                edge.source_node_id = class_node.node_id;
                                edge.target_symbol = get_node_text(base_id, source_code);
                                edge.type = EdgeType::INHERITS;
                                local_edges.push_back(edge);
                            }
                        }
                    }
                }
            }
        } else if (strcmp(type, "function_definition") == 0) {
            TSNode declarator = ts_node_child_by_field_name(current, "declarator", 10);
            TSNode id_node = find_first_identifier(declarator);
            if (!ts_node_is_null(id_node)) {
                std::string func_name = get_node_text(id_node, source_code);

                TempNodeRecord fn_node;
                fn_node.node_id = static_cast<uint32_t>(local_nodes.size());
                fn_node.name = current_state.fqn_prefix + func_name;
                fn_node.path = file_path;
                fn_node.start_line = ts_node_start_point(current).row + 1;
                fn_node.end_line = ts_node_end_point(current).row + 1;
                fn_node.type = NodeType::FUNCTION;

                current_state.node_id = fn_node.node_id;
                created_scope = true;
                local_nodes.push_back(fn_node);
            }
        } else if (strcmp(type, "call_expression") == 0) {
            TSNode function_node = ts_node_child_by_field_name(current, "function", 8);
            TSNode id_node = find_first_identifier(function_node);
            if (!ts_node_is_null(id_node)) {
                UnresolvedEdge edge;
                edge.source_node_id = current_state.node_id;
                edge.target_symbol = get_node_text(id_node, source_code);
                edge.type = EdgeType::CALLS;
                local_edges.push_back(edge);
            }
        }

        current_state.created_scope = created_scope;

        if (ts_tree_cursor_goto_first_child(&cursor)) {
            state_stack.push_back(current_state);
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
