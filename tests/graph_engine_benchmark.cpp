#include "graph_engine.hpp"
#include <iostream>
#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <numeric>

// Giả lập đồ thị quy mô lớn: 100,000 đỉnh và 1,000,000 cạnh
const size_t NUM_NODES = 100000;
const size_t NUM_EDGES = 1000000;
const size_t STRING_POOL_CAPACITY = 2000000;

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "Codegraph-X: In-Memory Graph Engine (CSR) Benchmark" << std::endl;
    std::cout << "==================================================" << std::endl;

    GraphEngine engine;
    
    // 1. Phân bổ bộ nhớ trước (Reserve)
    auto start_reserve = std::chrono::high_resolution_clock::now();
    engine.reserve(NUM_NODES, NUM_EDGES, STRING_POOL_CAPACITY);
    auto end_reserve = std::chrono::high_resolution_clock::now();
    std::cout << "Memory reserved in: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end_reserve - start_reserve).count()
              << " ms." << std::endl;

    // Thiết lập bộ tạo số ngẫu nhiên
    std::mt19937 rng(42); // Seed cố định để kết quả benchmark ổn định
    std::uniform_int_distribution<int> type_dist(0, 3);
    std::uniform_int_distribution<int> edge_type_dist(0, 2);
    std::uniform_int_distribution<uint32_t> node_id_dist(0, NUM_NODES - 1);

    // 2. Tạo đỉnh và ghi vào String Pool
    auto start_nodes = std::chrono::high_resolution_clock::now();
    for (uint32_t i = 0; i < NUM_NODES; ++i) {
        std::string name = "entity_name_" + std::to_string(i);
        std::string path = "/src/project/file_" + std::to_string(i % 1000) + ".cpp";
        
        uint32_t name_offset = engine.register_string(name);
        uint32_t path_offset = engine.register_string(path);

        NodeRecord node{
            i,
            static_cast<NodeType>(type_dist(rng)),
            name_offset,
            path_offset,
            static_cast<uint32_t>(i * 10),
            static_cast<uint32_t>(i * 10 + 8)
        };
        engine.add_node(std::move(node));
    }
    auto end_nodes = std::chrono::high_resolution_clock::now();
    std::cout << "Generated " << NUM_NODES << " nodes in: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end_nodes - start_nodes).count()
              << " ms." << std::endl;

    // 3. Phân bổ đều các cạnh ngẫu nhiên cho các đỉnh
    auto start_edges = std::chrono::high_resolution_clock::now();
    size_t edges_per_node = NUM_EDGES / NUM_NODES;
    for (uint32_t i = 0; i < NUM_NODES; ++i) {
        std::vector<EdgeRecord> node_edges;
        for (size_t e = 0; e < edges_per_node; ++e) {
            uint32_t target = node_id_dist(rng);
            // Tránh tự nối với chính mình
            if (target == i) {
                target = (i + 1) % NUM_NODES;
            }
            node_edges.push_back(EdgeRecord{
                target,
                static_cast<EdgeType>(edge_type_dist(rng))
            });
        }
        engine.finalize_node_edges(node_edges);
    }
    auto end_edges = std::chrono::high_resolution_clock::now();
    std::cout << "Generated " << NUM_EDGES << " edges (CSR finalized) in: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end_edges - start_edges).count()
              << " ms." << std::endl;

    // 4. Đo lường hiệu năng duyệt đồ thị (3-step Neighbor Traversal / BFS)
    std::cout << "\nRunning benchmark: 10,000 traversals (depth = 3)..." << std::endl;
    
    const size_t NUM_TRIALS = 10000;
    std::vector<double> query_times_us;
    query_times_us.reserve(NUM_TRIALS);

    size_t total_visited = 0;

    for (size_t trial = 0; trial < NUM_TRIALS; ++trial) {
        uint32_t start_node = node_id_dist(rng);
        
        auto q_start = std::chrono::high_resolution_clock::now();
        
        // Duyệt lân cận 3 bước (DFS/BFS quy mô nhỏ)
        std::vector<uint32_t> current_level;
        std::vector<uint32_t> next_level;
        std::vector<bool> visited(NUM_NODES, false);

        current_level.push_back(start_node);
        visited[start_node] = true;
        size_t visited_in_trial = 1;

        for (int step = 0; step < 3; ++step) {
            next_level.clear();
            for (uint32_t curr : current_level) {
                auto [adj_edges, count] = engine.get_adjacent_edges(curr);
                if (adj_edges != nullptr) {
                    for (size_t idx = 0; idx < count; ++idx) {
                        uint32_t target = adj_edges[idx].target_node_id;
                        if (!visited[target]) {
                            visited[target] = true;
                            next_level.push_back(target);
                            visited_in_trial++;
                        }
                    }
                }
            }
            if (next_level.empty()) break;
            current_level = next_level;
        }

        auto q_end = std::chrono::high_resolution_clock::now();
        double elapsed_us = std::chrono::duration<double, std::micro>(q_end - q_start).count();
        query_times_us.push_back(elapsed_us);
        total_visited += visited_in_trial;
    }

    // 5. Tính toán các chỉ số thống kê
    double sum = std::accumulate(query_times_us.begin(), query_times_us.end(), 0.0);
    double avg_us = sum / NUM_TRIALS;
    
    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "Traversal Benchmark Results:" << std::endl;
    std::cout << "  - Total trials: " << NUM_TRIALS << std::endl;
    std::cout << "  - Avg query latency: " << avg_us << " us (micro-seconds)" << std::endl;
    std::cout << "  - Total nodes visited: " << total_visited << std::endl;
    std::cout << "  - Avg nodes visited per query: " << static_cast<double>(total_visited) / NUM_TRIALS << std::endl;
    std::cout << "==================================================" << std::endl;

    return 0;
}
