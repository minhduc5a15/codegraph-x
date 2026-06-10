#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "in_memory_graph_engine.hpp"
#include "string_pool.hpp"

struct TSTree;

class ParallelParsingEngine {
public:
    ParallelParsingEngine() = default;
    ~ParallelParsingEngine();

    void execute(const std::vector<std::string>& files_to_parse);
    void build_flat_graph();

    struct TempNodeRecord {
        uint32_t node_id;
        uint32_t name_offset;
        uint32_t path_offset;
        uint32_t start_line;
        uint32_t end_line;
        NodeType type;
        std::vector<uint32_t> enclosing_scopes;
    };

    std::vector<TempNodeRecord> take_nodes() { return std::move(global_nodes); }
    std::vector<RawEdge> take_edges() { return std::move(global_edges); }
    StringPool take_string_pool() { return std::move(global_pool); }

    struct UnresolvedEdge {
        uint32_t source_node_id;
        uint32_t target_symbol_offset;
        EdgeType type;
    };

private:
    void initialize_workers();
    void worker_thread_func(int worker_id);

    struct UnresolvedExternal {
        size_t edge_index{};
        TempNodeRecord node;
    };

    struct FileData {
        uint32_t worker_id;
        std::vector<TempNodeRecord> nodes;
        std::vector<UnresolvedEdge> edges;
        std::vector<RawEdge> resolved_edges;
        std::vector<UnresolvedExternal> resolved_external_nodes;
    };
    std::unordered_map<std::string, FileData> file_data_map;
    std::mutex map_mutex;

    void merge_local_graphs(
        std::vector<std::pair<FileData*, uint32_t>>& file_tasks,
        uint32_t& current_id,
        class FlatSymbolMultiMap& flat_symbol_map
    );
    void resolve_cross_references(
        const std::vector<std::pair<FileData*, uint32_t>>& file_tasks, class FlatSymbolMultiMap& flat_symbol_map
    );
    void finalize_global_structures(std::vector<std::pair<FileData*, uint32_t>>& file_tasks, uint32_t& current_id);

    StringPool global_pool;
    std::vector<TempNodeRecord> global_nodes;
    std::vector<RawEdge> global_edges;

    std::queue<std::string> task_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::condition_variable cv_main;
    std::vector<std::thread> workers;
    std::vector<StringPool> worker_pools;
    int active_tasks = 0;
    bool stop_workers = false;
    bool workers_initialized = false;
};
