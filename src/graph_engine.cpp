#include "graph_engine.hpp"
#include <iostream>

GraphEngine::GraphEngine() {
    // Constructor
}

void GraphEngine::reserve(size_t num_nodes, size_t num_edges, size_t string_capacity) {
    nodes.reserve(num_nodes);
    offsets.reserve(num_nodes + 1);
    edges.reserve(num_edges);
    string_pool.reserve(string_capacity);
}

uint32_t GraphEngine::register_string(std::string_view str) {
    uint32_t offset = static_cast<uint32_t>(string_pool.size());
    string_pool.insert(string_pool.end(), str.begin(), str.end());
    string_pool.push_back('\0'); // Kết thúc chuỗi chuẩn C
    return offset;
}

std::string_view GraphEngine::resolve_string(uint32_t offset) const {
    if (offset >= string_pool.size()) {
        return "";
    }
    return std::string_view(&string_pool[offset]);
}

void GraphEngine::add_node(NodeRecord&& node) {
    nodes.push_back(node);
    if (offsets.empty()) {
        offsets.push_back(0);
    }
}

void GraphEngine::finalize_node_edges(const std::vector<EdgeRecord>& node_edges) {
    edges.insert(edges.end(), node_edges.begin(), node_edges.end());
    offsets.push_back(static_cast<uint32_t>(edges.size()));
}

