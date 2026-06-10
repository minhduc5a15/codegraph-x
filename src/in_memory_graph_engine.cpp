#include "in_memory_graph_engine.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>

inline std::string_view extract_short_name(const std::string_view fqn) {
    const size_t pos = fqn.find_last_of(".:");
    return (pos == std::string_view::npos) ? fqn : fqn.substr(pos + 1);
}

void InMemoryGraphEngine::reserve(const size_t num_nodes, const size_t num_edges, const size_t string_capacity) {
    if (is_frozen) return;
    nodes.reserve(num_nodes);
    offsets.reserve(num_nodes + 1);
    edges.reserve(num_edges);
    string_pool.reserve(string_capacity);
}

uint32_t InMemoryGraphEngine::register_string(const std::string_view str) {
    if (is_frozen) return 0;
    const auto it = string_lookup.find(str);
    if (it != string_lookup.end()) {
        return it->second;
    }
    std::string key(str);
    auto offset = static_cast<uint32_t>(string_pool.size());
    string_pool.insert(string_pool.end(), str.begin(), str.end());
    string_pool.push_back('\0');
    string_lookup.emplace(std::move(key), offset);
    return offset;
}

std::string_view InMemoryGraphEngine::resolve_string(const uint32_t offset) const {
    if (offset >= string_pool.size()) {
        return "";
    }
    return {&string_pool[offset]};
}

void InMemoryGraphEngine::build_from_raw(std::vector<NodeRecord>&& raw_nodes, const std::vector<RawEdge>& raw_edges) {
    if (is_frozen) return;

    size_t dropped_edges = 0;
    nodes = std::move(raw_nodes);
    const auto num_nodes = static_cast<uint32_t>(nodes.size());

    // 1. Calculate Out-degree with Sanity Check and Error Tracking
    build_out_degree_and_edges(raw_edges, num_nodes, dropped_edges);

    if (dropped_edges > 0) {
        std::cerr << "[WARNING] InMemoryGraphEngine dropped " << dropped_edges << " invalid edges out of bounds."
                  << std::endl;
    }

    // 2. Build Sorted Indices
    build_sorted_indices(num_nodes);

    // 3. Build Incoming Edges (CSR)
    build_incoming_edges(raw_edges, num_nodes);

    is_frozen = true;
    nodes.shrink_to_fit();
    offsets.shrink_to_fit();
    edges.shrink_to_fit();
    string_pool.shrink_to_fit();
    name_index.shrink_to_fit();
    short_name_index.shrink_to_fit();
    path_index.shrink_to_fit();
    incoming_offsets.shrink_to_fit();
    incoming_edges.shrink_to_fit();
    std::unordered_map<std::string, uint32_t, StringHash, std::equal_to<>>().swap(string_lookup);
}

std::vector<uint32_t> InMemoryGraphEngine::search_substring(const std::string_view query, const size_t limit) const {
    std::vector<uint32_t> results;
    if (query.empty()) return results;
    for (const auto& node : nodes) {
        if (resolve_string(node.name_pool_offset).find(query) != std::string_view::npos) {
            results.push_back(node.node_id);
            if (results.size() >= limit) break;
        }
    }
    return results;
}

std::vector<uint32_t> InMemoryGraphEngine::search_path_substring(const std::string_view query, const size_t limit)
    const {
    std::vector<uint32_t> results;
    if (query.empty()) return results;
    for (const auto& node : nodes) {
        if (resolve_string(node.path_pool_offset).find(query) != std::string_view::npos) {
            results.push_back(node.node_id);
            if (results.size() >= limit) break;
        }
    }
    return results;
}

void InMemoryGraphEngine::build_out_degree_and_edges(
    const std::vector<RawEdge>& raw_edges, const uint32_t num_nodes, size_t& dropped_edges
) {
    offsets.assign(num_nodes + 1, 0);
    for (const auto& edge : raw_edges) {
        if (edge.source_node_id < num_nodes && edge.target_node_id < num_nodes) {
            offsets[edge.source_node_id + 1]++;
        } else {
            dropped_edges++;
        }
    }

    for (uint32_t i = 1; i <= num_nodes; ++i) {
        offsets[i] += offsets[i - 1];
    }

    edges.resize(offsets[num_nodes]);
    std::vector<uint32_t> write_cursors = offsets;

    for (const auto& [source_node_id, target_node_id, type] : raw_edges) {
        if (source_node_id < num_nodes && target_node_id < num_nodes) {
            uint32_t& pos = write_cursors[source_node_id];
            edges[pos] = {target_node_id, type};
            pos++;
        }
    }
}

void InMemoryGraphEngine::build_sorted_indices(const uint32_t num_nodes) {
    name_index.resize(num_nodes);
    short_name_index.resize(num_nodes);
    path_index.resize(num_nodes);
    std::iota(name_index.begin(), name_index.end(), 0);
    std::iota(short_name_index.begin(), short_name_index.end(), 0);
    std::iota(path_index.begin(), path_index.end(), 0);

    std::ranges::sort(name_index, [&](const uint32_t a, const uint32_t b) {
        return resolve_string(nodes[a].name_pool_offset) < resolve_string(nodes[b].name_pool_offset);
    });

    std::ranges::sort(short_name_index, [&](const uint32_t a, const uint32_t b) {
        const std::string_view sa = extract_short_name(resolve_string(nodes[a].name_pool_offset));
        const std::string_view sb = extract_short_name(resolve_string(nodes[b].name_pool_offset));
        if (sa != sb) return sa < sb;
        return resolve_string(nodes[a].name_pool_offset) < resolve_string(nodes[b].name_pool_offset);
    });

    std::ranges::sort(path_index, [&](const uint32_t a, const uint32_t b) {
        return resolve_string(nodes[a].path_pool_offset) < resolve_string(nodes[b].path_pool_offset);
    });
}

void InMemoryGraphEngine::build_incoming_edges(const std::vector<RawEdge>& raw_edges, const uint32_t num_nodes) {
    incoming_offsets.assign(num_nodes + 1, 0);
    for (const auto& edge : raw_edges) {
        if (edge.source_node_id < num_nodes && edge.target_node_id < num_nodes) {
            incoming_offsets[edge.target_node_id + 1]++;
        }
    }
    for (uint32_t i = 1; i <= num_nodes; ++i) {
        incoming_offsets[i] += incoming_offsets[i - 1];
    }
    incoming_edges.resize(incoming_offsets[num_nodes]);
    std::vector<uint32_t> incoming_write_cursors = incoming_offsets;
    for (const auto& edge : raw_edges) {
        if (edge.source_node_id < num_nodes && edge.target_node_id < num_nodes) {
            uint32_t& pos = incoming_write_cursors[edge.target_node_id];
            incoming_edges[pos] = edge.source_node_id;
            pos++;
        }
    }
}
