#include "InMemoryGraphEngine.hpp"

#include <iostream>

void InMemoryGraphEngine::reserve(size_t num_nodes, size_t num_edges, size_t string_capacity) {
    if (is_frozen) return;
    nodes.reserve(num_nodes);
    offsets.reserve(num_nodes + 1);
    edges.reserve(num_edges);
    string_pool.reserve(string_capacity);
}

uint32_t InMemoryGraphEngine::register_string(std::string_view str) {
    if (is_frozen) return 0;
    std::string key(str);
    auto it = string_lookup.find(key);
    if (it != string_lookup.end()) {
        return it->second;
    }
    uint32_t offset = static_cast<uint32_t>(string_pool.size());
    string_pool.insert(string_pool.end(), str.begin(), str.end());
    string_pool.push_back('\0');
    string_lookup.emplace(std::move(key), offset);
    return offset;
}

std::string_view InMemoryGraphEngine::resolve_string(uint32_t offset) const {
    if (offset >= string_pool.size()) {
        return "";
    }
    return std::string_view(&string_pool[offset]);
}

void InMemoryGraphEngine::build_from_raw(std::vector<NodeRecord>&& raw_nodes, const std::vector<RawEdge>& raw_edges) {
    if (is_frozen) return;

    size_t dropped_edges = 0;
    nodes = std::move(raw_nodes);
    uint32_t num_nodes = static_cast<uint32_t>(nodes.size());

    // 1. Calculate Out-degree with Sanity Check and Error Tracking
    offsets.assign(num_nodes + 1, 0);
    for (const auto& edge : raw_edges) {
        if (edge.source_node_id < num_nodes && edge.target_node_id < num_nodes) {
            offsets[edge.source_node_id + 1]++;
        } else {
            dropped_edges++;
        }
    }

    // 2. Prefix Sum
    for (uint32_t i = 1; i <= num_nodes; ++i) {
        offsets[i] += offsets[i - 1];
    }

    // 3. Fill edges with Sanity Check and Error Tracking
    edges.resize(offsets[num_nodes]);
    std::vector<uint32_t> write_cursors = offsets;

    for (const auto& edge : raw_edges) {
        if (edge.source_node_id < num_nodes && edge.target_node_id < num_nodes) {
            uint32_t& pos = write_cursors[edge.source_node_id];
            edges[pos] = {edge.target_node_id, edge.type};
            pos++;
        }
    }

    if (dropped_edges > 0) {
        std::cerr << "[WARNING] InMemoryGraphEngine dropped " << dropped_edges << " invalid edges out of bounds." << std::endl;
    }

    is_frozen = true;
    nodes.shrink_to_fit();
    offsets.shrink_to_fit();
    edges.shrink_to_fit();
    string_pool.shrink_to_fit();
}
