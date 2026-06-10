#include "parallel_parser.hpp"

#include <tree_sitter/api.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <ranges>
#include <unordered_set>

#include "ast_processor.hpp"
#include "file_buffer.hpp"
#include "flat_symbol_map.hpp"

extern "C" const TSLanguage* tree_sitter_cpp();

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
    worker_pools.resize(thread_count);
    for (unsigned int i = 0; i < thread_count; ++i) {
        workers.emplace_back(&ParallelParsingEngine::worker_thread_func, this, i);
    }
    workers_initialized = true;
}

void ParallelParsingEngine::execute(const std::vector<std::string>& files_to_parse) {
    initialize_workers();

    // Remove Stale Data
    {
        std::lock_guard<std::mutex> lock(map_mutex);
        const std::unordered_set<std::string> valid_files(files_to_parse.begin(), files_to_parse.end());
        for (auto it = file_data_map.begin(); it != file_data_map.end();) {
            if (!valid_files.contains(it->first)) {
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
        active_tasks = static_cast<int>(files_to_parse.size());
    }

    if (files_to_parse.empty()) return;
    cv.notify_all();

    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        cv_main.wait(lock, [this] { return active_tasks == 0; });
    }
}

void ParallelParsingEngine::worker_thread_func(int worker_id) {
    StringPool& local_pool = worker_pools[worker_id];
    TSParser* local_parser = ts_parser_new();
    ts_parser_set_language(local_parser, tree_sitter_cpp());

    uint32_t error_offset;
    TSQueryError error_type;
    const auto query_src =
        "(call_expression function: (identifier) @target)\n"
        "(call_expression function: (field_expression field: (field_identifier) @target))\n"
        "(call_expression function: (qualified_identifier) @target)";
    TSQuery* call_query = ts_query_new(tree_sitter_cpp(), query_src, strlen(query_src), &error_offset, &error_type);
    TSQueryCursor* query_cursor = ts_query_cursor_new();

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
                const uint32_t file_path_offset = local_pool.get_or_add(file_path);
                ASTProcessor::process_syntax_tree(
                    syntax_tree,
                    file_path_offset,
                    file_buffer.data(),
                    local_nodes,
                    local_edges,
                    call_query,
                    query_cursor,
                    local_pool
                );
                ts_tree_delete(syntax_tree);
            }

            // Store in per-file map
            {
                std::lock_guard<std::mutex> lock(map_mutex);
                FileData data;
                data.worker_id = worker_id;
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

    ts_query_cursor_delete(query_cursor);
    ts_query_delete(call_query);
    ts_parser_delete(local_parser);
}

void ParallelParsingEngine::merge_local_graphs(
    std::vector<std::pair<FileData*, uint32_t>>& file_tasks,
    uint32_t& current_id,
    class FlatSymbolMultiMap& flat_symbol_map
) {
    std::vector<std::unordered_map<uint32_t, uint32_t>> offset_maps(worker_pools.size());
    for (size_t wid = 0; wid < worker_pools.size(); ++wid) {
        for (const auto& [str, local_off] : worker_pools[wid].lookup) {
            offset_maps[wid][local_off] = global_pool.get_or_add(str);
        }
        std::unordered_map<std::string, uint32_t, StringHash, std::equal_to<>>().swap(worker_pools[wid].lookup);
    }

    for (auto& val : file_data_map | std::views::values) {
        auto& data = val;
        file_tasks.emplace_back(&data, current_id);
        const auto& off_map = offset_maps[data.worker_id];

        for (auto& n : data.nodes) {
            TempNodeRecord updated_node = std::move(n);
            updated_node.node_id = current_id;
            updated_node.name_offset = off_map.at(updated_node.name_offset);
            updated_node.path_offset = off_map.at(updated_node.path_offset);
            for (auto& scope_off : updated_node.enclosing_scopes) {
                scope_off = off_map.at(scope_off);
            }

            if (updated_node.type == NodeType::FUNCTION || updated_node.type == NodeType::CLASS ||
                updated_node.type == NodeType::METHOD) {
                flat_symbol_map.insert(updated_node.name_offset, current_id);
            }
            global_nodes.push_back(std::move(updated_node));
            current_id++;
        }
        for (auto& e : data.edges) {
            e.target_symbol_offset = off_map.at(e.target_symbol_offset);
        }
    }
}

void ParallelParsingEngine::resolve_cross_references(
    const std::vector<std::pair<FileData*, uint32_t>>& file_tasks, class FlatSymbolMultiMap& flat_symbol_map
) {
    uint32_t external_path_offset = global_pool.get_or_add("external");

    auto resolve_worker = [this, &flat_symbol_map, external_path_offset](FileData* data, const uint32_t local_offset) {
        data->resolved_edges.clear();
        data->resolved_external_nodes.clear();
        thread_local std::string query_buffer;

        for (const auto& [source_node_id, target_symbol_offset, type] : data->edges) {
            const uint32_t absolute_source_id = source_node_id + local_offset;
            const auto& source_node = global_nodes[absolute_source_id];

            bool resolved = false;

            for (int i = static_cast<int>(source_node.enclosing_scopes.size()); i >= 0; --i) {
                query_buffer.clear();
                for (int j = 0; j < i; ++j) {
                    query_buffer += global_pool.resolve(source_node.enclosing_scopes[j]);
                    query_buffer += "::";
                }
                query_buffer += global_pool.resolve(target_symbol_offset);

                auto lookup_it = global_pool.lookup.find(query_buffer);
                if (lookup_it != global_pool.lookup.end()) {
                    const uint32_t fqn_offset = lookup_it->second;
                    auto matched_nodes = flat_symbol_map.find_all(fqn_offset);

                    if (!matched_nodes.empty()) {
                        resolved = true;
                        const size_t match_count = matched_nodes.size();
                        const EdgeType edge_type =
                            (match_count > 1 && type == EdgeType::CALLS) ? EdgeType::AMBIGUOUS_CALL : type;

                        for (const uint32_t match_id : matched_nodes) {
                            RawEdge new_edge{};
                            new_edge.source_node_id = absolute_source_id;
                            new_edge.target_node_id = match_id;
                            new_edge.type = edge_type;
                            data->resolved_edges.push_back(new_edge);
                        }
                        break;
                    }
                }
            }

            if (!resolved) {
                TempNodeRecord external_node;
                external_node.node_id = 0;
                external_node.name_offset = target_symbol_offset;
                external_node.path_offset = external_path_offset;
                external_node.start_line = 0;
                external_node.end_line = 0;
                external_node.type = NodeType::EXTERNAL;

                RawEdge new_edge{};
                new_edge.source_node_id = absolute_source_id;
                new_edge.target_node_id = 0;
                new_edge.type = type;

                data->resolved_external_nodes.push_back({data->resolved_edges.size(), external_node});
                data->resolved_edges.push_back(new_edge);
            }
        }
    };

    std::vector<std::thread> resolve_threads;
    std::atomic<size_t> task_index{0};
    unsigned int thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) thread_count = 2;

    for (unsigned int i = 0; i < thread_count; ++i) {
        resolve_threads.emplace_back([&]() {
            while (true) {
                const size_t idx = task_index.fetch_add(1);
                if (idx >= file_tasks.size()) break;
                resolve_worker(file_tasks[idx].first, file_tasks[idx].second);
            }
        });
    }

    for (auto& t : resolve_threads) {
        t.join();
    }
}

void ParallelParsingEngine::finalize_global_structures(
    std::vector<std::pair<FileData*, uint32_t>>& file_tasks, uint32_t& current_id
) {
    for (const auto& key : file_tasks | std::views::keys) {
        FileData* data = key;
        for (auto& [edge_index, node] : data->resolved_external_nodes) {
            const uint32_t new_id = current_id++;
            node.node_id = new_id;
            global_nodes.push_back(node);
            data->resolved_edges[edge_index].target_node_id = new_id;
        }

        std::ranges::sort(data->resolved_edges, [](const RawEdge& a, const RawEdge& b) {
            if (a.source_node_id != b.source_node_id) return a.source_node_id < b.source_node_id;
            if (a.target_node_id != b.target_node_id) return a.target_node_id < b.target_node_id;
            return a.type < b.type;
        });
        auto last =
            std::ranges::unique(data->resolved_edges, [](const RawEdge& a, const RawEdge& b) {
                return a.source_node_id == b.source_node_id && a.target_node_id == b.target_node_id && a.type == b.type;
            }).begin();
        data->resolved_edges.erase(last, data->resolved_edges.end());
    }

    size_t total_edges = 0;
    for (const auto& key : file_tasks | std::views::keys) {
        total_edges += key->resolved_edges.size();
    }
    global_edges.reserve(total_edges);

    for (const auto& key : file_tasks | std::views::keys) {
        FileData* data = key;
        global_edges.insert(global_edges.end(), data->resolved_edges.begin(), data->resolved_edges.end());
    }

    for (auto& val : file_data_map | std::views::values) {
        std::vector<UnresolvedEdge>().swap(val.edges);
        std::vector<RawEdge>().swap(val.resolved_edges);
        std::vector<TempNodeRecord>().swap(val.nodes);
        std::vector<UnresolvedExternal>().swap(val.resolved_external_nodes);
    }
}

void ParallelParsingEngine::build_flat_graph() {
    std::lock_guard<std::mutex> lock(map_mutex);
    global_nodes.clear();
    global_edges.clear();

    size_t total_nodes = 0;
    for (const auto& val : file_data_map | std::views::values) total_nodes += val.nodes.size();
    FlatSymbolMultiMap flat_symbol_map(total_nodes);
    global_nodes.reserve(total_nodes);

    uint32_t current_id = 0;
    std::vector<std::pair<FileData*, uint32_t>> file_tasks;

    merge_local_graphs(file_tasks, current_id, flat_symbol_map);
    resolve_cross_references(file_tasks, flat_symbol_map);
    finalize_global_structures(file_tasks, current_id);
}
