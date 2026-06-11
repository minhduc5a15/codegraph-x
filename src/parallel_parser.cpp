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

    std::unordered_set<std::string> valid_files(files_to_parse.begin(), files_to_parse.end());
    {
        std::lock_guard<std::mutex> lock(map_mutex);
        for (auto it = file_data_map.begin(); it != file_data_map.end();) {
            if (!valid_files.contains(it->first)) {
                it = file_data_map.erase(it);
            } else {
                it->second.is_parsed_this_run = false;
                it->second.local_external_nodes.clear(); // We rebuild externals each run
                ++it;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        while (!task_queue.empty()) task_queue.pop();
        for (const auto& file : files_to_parse) {
            std::error_code ec;
            auto mtime = std::filesystem::last_write_time(file, ec);
            if (ec) continue;

            bool skip = false;
            {
                std::lock_guard<std::mutex> map_lock(map_mutex);
                auto it = file_data_map.find(file);
                if (it != file_data_map.end() && it->second.last_write_time == mtime) {
                    skip = true;
                }
            }
            if (!skip) {
                task_queue.push(file);
            }
        }
        active_tasks = static_cast<int>(task_queue.size());
    }

    if (active_tasks == 0) return;
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

        std::error_code ec;
        auto mtime = std::filesystem::last_write_time(file_path, ec);

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
                    local_pool,
                    global_scope_interner,
                    global_pool
                );
                ts_tree_delete(syntax_tree);
            }

            // Store in per-file map
            {
                std::lock_guard<std::mutex> lock(map_mutex);
                FileData data;
                data.worker_id = worker_id;
                data.last_write_time = mtime;
                data.is_parsed_this_run = true;
                data.nodes = std::move(local_nodes);
                data.unresolved_edges = std::move(local_edges);
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
        worker_pools[wid].pool.clear();
    }

    for (auto& val : file_data_map | std::views::values) {
        auto& data = val;
        file_tasks.emplace_back(&data, current_id);

        if (data.is_parsed_this_run) {
            const auto& off_map = offset_maps[data.worker_id];
            for (auto& n : data.nodes) {
                n.name_offset = off_map.at(n.name_offset);
                n.path_offset = off_map.at(n.path_offset);
            }
            for (auto& e : data.unresolved_edges) {
                e.target_symbol_offset = off_map.at(e.target_symbol_offset);
            }
            data.is_parsed_this_run = false;
        }

        for (auto& n : data.nodes) {
            TempNodeRecord updated_node = n;
            updated_node.node_id = current_id;
            
            if (updated_node.type == NodeType::FUNCTION || updated_node.type == NodeType::CLASS ||
                updated_node.type == NodeType::METHOD) {
                flat_symbol_map.insert(ScopeLookupKey{updated_node.scope_id, updated_node.name_offset, updated_node.type}, current_id);
            }
            global_nodes.push_back(std::move(updated_node));
            current_id++;
        }
    }
}

void ParallelParsingEngine::resolve_cross_references(
    const std::vector<std::pair<FileData*, uint32_t>>& file_tasks, class FlatSymbolMultiMap& flat_symbol_map
) {
    auto resolve_worker = [this, &flat_symbol_map](FileData* data) {
        if (!data->cached_edges.empty()) return; // Skip if already resolved
        
        for (const auto& edge : data->unresolved_edges) {
            uint32_t current_scope = edge.source_scope_id;
            bool resolved = false;

            while (true) {
                ScopeLookupKey key { current_scope, edge.target_symbol_offset, edge.expected_target_kind };
                auto matches = flat_symbol_map.find_all(key);
                
                if (!matches.empty()) {
                    CachedResolvedEdge cached_edge{};
                    cached_edge.source_local_index = edge.source_local_index;
                    cached_edge.resolved_key = key;
                    cached_edge.is_external = false;
                    cached_edge.type = edge.type;
                    data->cached_edges.push_back(cached_edge);
                    resolved = true;
                    break;
                }
                
                if (current_scope == 0) break;
                current_scope = global_scope_interner.get_parent(current_scope);
            }

            if (!resolved) {
                CachedResolvedEdge cached_edge{};
                cached_edge.source_local_index = edge.source_local_index;
                cached_edge.resolved_key = ScopeLookupKey{0, edge.target_symbol_offset, NodeType::EXTERNAL};
                cached_edge.is_external = true;
                cached_edge.type = edge.type;
                data->cached_edges.push_back(cached_edge);
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
                resolve_worker(file_tasks[idx].first);
            }
        });
    }

    for (auto& t : resolve_threads) {
        t.join();
    }
}

void ParallelParsingEngine::finalize_global_structures(
    std::vector<std::pair<FileData*, uint32_t>>& file_tasks, uint32_t& current_id, class FlatSymbolMultiMap& flat_symbol_map
) {
    uint32_t external_path_offset = global_pool.get_or_add("external");

    for (const auto& [data, local_offset] : file_tasks) {
        for (const auto& cached_edge : data->cached_edges) {
            uint32_t global_source_id = local_offset + cached_edge.source_local_index;
            
            if (cached_edge.is_external) {
                uint32_t target_offset = cached_edge.resolved_key.symbol_offset;
                uint32_t target_global_id = 0;
                
                auto ext_it = data->local_external_nodes.find(target_offset);
                if (ext_it != data->local_external_nodes.end()) {
                    target_global_id = ext_it->second;
                } else {
                    target_global_id = current_id++;
                    data->local_external_nodes[target_offset] = target_global_id;
                    
                    TempNodeRecord ext_node{};
                    ext_node.node_id = target_global_id;
                    ext_node.name_offset = target_offset;
                    ext_node.path_offset = external_path_offset;
                    ext_node.start_line = 0;
                    ext_node.end_line = 0;
                    ext_node.type = NodeType::EXTERNAL;
                    ext_node.start_column = 0;
                    ext_node.flags = 0;
                    ext_node.scope_id = 0;
                    global_nodes.push_back(ext_node);
                }
                global_edges.push_back({global_source_id, target_global_id, cached_edge.type});
            } else {
                auto matches = flat_symbol_map.find_all(cached_edge.resolved_key);
                if (!matches.empty()) {
                    for (uint32_t match_id : matches) {
                        EdgeType final_type = (matches.size() > 1 && cached_edge.type == EdgeType::CALLS) ? EdgeType::AMBIGUOUS_CALL : cached_edge.type;
                        global_edges.push_back({global_source_id, match_id, final_type});
                    }
                } else {
                    // Symbol was deleted! Relink to external.
                    uint32_t target_offset = cached_edge.resolved_key.symbol_offset;
                    uint32_t target_global_id = 0;
                    
                    auto ext_it = data->local_external_nodes.find(target_offset);
                    if (ext_it != data->local_external_nodes.end()) {
                        target_global_id = ext_it->second;
                    } else {
                        target_global_id = current_id++;
                        data->local_external_nodes[target_offset] = target_global_id;
                        
                        TempNodeRecord ext_node{};
                        ext_node.node_id = target_global_id;
                        ext_node.name_offset = target_offset;
                        ext_node.path_offset = external_path_offset;
                        ext_node.start_line = 0;
                        ext_node.end_line = 0;
                        ext_node.type = NodeType::EXTERNAL;
                        ext_node.start_column = 0;
                        ext_node.flags = 0;
                        ext_node.scope_id = 0;
                        global_nodes.push_back(ext_node);
                    }
                    global_edges.push_back({global_source_id, target_global_id, cached_edge.type});
                }
            }
        }
    }
    
    std::ranges::sort(global_edges, [](const RawEdge& a, const RawEdge& b) {
        if (a.source_node_id != b.source_node_id) return a.source_node_id < b.source_node_id;
        if (a.target_node_id != b.target_node_id) return a.target_node_id < b.target_node_id;
        return a.type < b.type;
    });
    auto last = std::ranges::unique(global_edges, [](const RawEdge& a, const RawEdge& b) {
        return a.source_node_id == b.source_node_id && a.target_node_id == b.target_node_id && a.type == b.type;
    }).begin();
    global_edges.erase(last, global_edges.end());
}

void ParallelParsingEngine::build_flat_graph() {
    std::lock_guard<std::mutex> lock(map_mutex);
    global_nodes.clear();
    global_edges.clear();

    size_t total_nodes = 0;
    for (const auto& val : file_data_map | std::views::values) total_nodes += val.nodes.size();
    FlatSymbolMultiMap flat_symbol_map(total_nodes);
    global_nodes.reserve(total_nodes + 1000); 

    uint32_t current_id = 0;
    std::vector<std::pair<FileData*, uint32_t>> file_tasks;

    merge_local_graphs(file_tasks, current_id, flat_symbol_map);
    resolve_cross_references(file_tasks, flat_symbol_map);
    finalize_global_structures(file_tasks, current_id, flat_symbol_map);
}
