#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "InMemoryGraphEngine.hpp"

struct TSTree;

class ParallelParsingEngine {
public:
    ParallelParsingEngine() = default;
    ~ParallelParsingEngine();

    void execute(const std::vector<std::string>& files_to_parse);
    void build_flat_graph();

    struct TempNodeRecord {
        uint32_t node_id;
        std::string name;
        std::string path;
        uint32_t start_line;
        uint32_t end_line;
        NodeType type;
    };

    std::vector<TempNodeRecord> take_nodes() { return std::move(global_nodes); }
    std::vector<RawEdge> take_edges() { return std::move(global_edges); }

    struct UnresolvedEdge {
        uint32_t source_node_id;
        std::string target_symbol;
        EdgeType type;
    };

private:
    void initialize_workers();
    void worker_thread_func();
    void process_syntax_tree(TSTree* tree, const std::string& file_path, const char* source_code, std::vector<TempNodeRecord>& local_nodes,
                             std::vector<UnresolvedEdge>& local_edges);

    struct FileData {
        std::vector<TempNodeRecord> nodes;
        std::vector<UnresolvedEdge> edges;
    };
    std::unordered_map<std::string, FileData> file_data_map;
    std::mutex map_mutex;

    std::vector<TempNodeRecord> global_nodes;
    std::vector<RawEdge> global_edges;

    std::queue<std::string> task_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::condition_variable cv_main;
    std::vector<std::thread> workers;
    int active_tasks = 0;
    bool stop_workers = false;
    bool workers_initialized = false;
};
