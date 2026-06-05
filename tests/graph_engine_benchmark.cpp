#include <benchmark/benchmark.h>
#include "InMemoryGraphEngine.hpp"
#include <vector>
#include <random>

static void BM_BuildFromRaw(benchmark::State& state) {
    const size_t num_nodes = 10000;
    const size_t num_edges = 100000;
    
    std::vector<NodeRecord> raw_nodes;
    raw_nodes.reserve(num_nodes);
    for (uint32_t i = 0; i < num_nodes; ++i) {
        raw_nodes.push_back({i, 0, 0, 0, 0, NodeType::FUNCTION, {0,0,0}});
    }

    std::vector<RawEdge> raw_edges;
    raw_edges.reserve(num_edges);
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint32_t> dist(0, num_nodes - 1);
    for (size_t i = 0; i < num_edges; ++i) {
        raw_edges.push_back({dist(rng), dist(rng), EdgeType::CALLS});
    }

    for (auto _ : state) {
        state.PauseTiming();
        InMemoryGraphEngine engine;
        auto nodes_copy = raw_nodes;
        state.ResumeTiming();
        
        engine.build_from_raw(std::move(nodes_copy), raw_edges);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_BuildFromRaw);

static void BM_GetAdjacentEdges(benchmark::State& state) {
    const size_t num_nodes = 10000;
    const size_t num_edges = 100000;
    
    InMemoryGraphEngine engine;
    std::vector<NodeRecord> raw_nodes;
    for (uint32_t i = 0; i < num_nodes; ++i) {
        raw_nodes.push_back({i, 0, 0, 0, 0, NodeType::FUNCTION, {0,0,0}});
    }

    std::vector<RawEdge> raw_edges;
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint32_t> dist(0, num_nodes - 1);
    for (size_t i = 0; i < num_edges; ++i) {
        raw_edges.push_back({dist(rng), dist(rng), EdgeType::CALLS});
    }
    engine.build_from_raw(std::move(raw_nodes), raw_edges);

    std::uniform_int_distribution<uint32_t> query_dist(0, num_nodes - 1);
    
    for (auto _ : state) {
        uint32_t node_id = query_dist(rng);
        auto result = engine.get_adjacent_edges(node_id);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_GetAdjacentEdges);

BENCHMARK_MAIN();
