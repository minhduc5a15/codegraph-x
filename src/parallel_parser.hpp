#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <filesystem>

#include "in_memory_graph_engine.hpp"
#include "string_pool.hpp"
#include "scope_interner.hpp"
#include "flat_symbol_map.hpp"

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
        uint16_t start_column;
        uint8_t flags;
        uint32_t scope_id;
    };

    std::vector<TempNodeRecord> take_nodes() { return std::move(global_nodes); }
    std::vector<RawEdge> take_edges() { return std::move(global_edges); }
    StringPool take_string_pool() { return std::move(global_pool); }

    struct UnresolvedEdge {
        uint32_t source_local_index;
        uint32_t source_scope_id;
        uint32_t target_symbol_offset;
        NodeType expected_target_kind;
        EdgeType type;
    };

    struct CachedResolvedEdge {
        uint32_t source_local_index;
        ScopeLookupKey resolved_key;
        bool is_external;
        EdgeType type;
    };

private:
    void initialize_workers();
    void worker_thread_func(int worker_id);

    struct FileData {
        uint32_t worker_id;
        std::filesystem::file_time_type last_write_time;
        bool is_parsed_this_run;
        std::vector<TempNodeRecord> nodes;
        std::vector<UnresolvedEdge> unresolved_edges;
        std::vector<CachedResolvedEdge> cached_edges;
        std::unordered_map<uint32_t, uint32_t> local_external_nodes;
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
    void finalize_global_structures(std::vector<std::pair<FileData*, uint32_t>>& file_tasks, uint32_t& current_id, class FlatSymbolMultiMap& flat_symbol_map);

    StringPool global_pool;
    GlobalScopeInterner global_scope_interner;
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
