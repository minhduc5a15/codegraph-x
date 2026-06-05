#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include "InMemoryGraphEngine.hpp"

struct TSTree;

class ParallelParsingEngine {
public:
    ParallelParsingEngine() = default;
    ~ParallelParsingEngine() = default;

    void execute(const std::vector<std::string>& files_to_parse);

    std::vector<NodeRecord> take_nodes() { return std::move(global_nodes); }
    std::vector<RawEdge> take_edges() { return std::move(global_edges); }

private:
    void worker_thread_func();
    void process_syntax_tree(TSTree* tree, const std::string& file_path, 
                            std::vector<NodeRecord>& local_nodes, 
                            std::vector<RawEdge>& local_edges);

    std::vector<NodeRecord> global_nodes;
    std::vector<RawEdge> global_edges;
    std::mutex results_mutex;

    std::queue<std::string> task_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    bool stop_workers = false;
};
