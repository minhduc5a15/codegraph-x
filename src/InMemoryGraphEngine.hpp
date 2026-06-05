#pragma once
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

enum class NodeType : uint8_t { FILE = 0, CLASS = 1, FUNCTION = 2, METHOD = 3 };

enum class EdgeType : uint8_t { CALLS = 0, INHERITS = 1, IMPORTS = 2 };

struct NodeRecord {
    uint32_t node_id;           // offset 0
    uint32_t name_pool_offset;  // offset 4
    uint32_t path_pool_offset;  // offset 8
    uint32_t start_line;        // offset 12
    uint32_t end_line;          // offset 16
    NodeType type;              // offset 20
    uint8_t padding[3];         // offset 21 (Natural alignment for 24-byte total size)
};

struct EdgeRecord {
    uint32_t target_node_id;
    EdgeType type;
};

struct RawEdge {
    uint32_t source_node_id;
    uint32_t target_node_id;
    EdgeType type;
};

class InMemoryGraphEngine {
private:
    std::vector<NodeRecord> nodes;
    std::vector<uint32_t> offsets;
    std::vector<EdgeRecord> edges;
    std::vector<char> string_pool;
    bool is_frozen = false;

public:
    InMemoryGraphEngine() = default;

    InMemoryGraphEngine(const InMemoryGraphEngine&) = delete;
    InMemoryGraphEngine& operator=(const InMemoryGraphEngine&) = delete;
    InMemoryGraphEngine(InMemoryGraphEngine&&) noexcept = default;
    InMemoryGraphEngine& operator=(InMemoryGraphEngine&&) noexcept = default;

    void reserve(size_t num_nodes, size_t num_edges, size_t string_capacity);
    uint32_t register_string(std::string_view str);
    std::string_view resolve_string(uint32_t offset) const;

    void build_from_raw(std::vector<NodeRecord>&& raw_nodes, const std::vector<RawEdge>& raw_edges);

    inline std::pair<const EdgeRecord*, size_t> get_adjacent_edges(uint32_t node_id) const {
        if (!is_frozen || node_id >= nodes.size()) {
            return {nullptr, 0};
        }
        uint32_t start_idx = offsets[node_id];
        uint32_t end_idx = offsets[node_id + 1];
        size_t count = end_idx - start_idx;
        if (count == 0) return {nullptr, 0};
        return {&edges[start_idx], count};
    }

    inline size_t get_node_count() const { return nodes.size(); }

    inline const NodeRecord& get_node(uint32_t node_id) const {
        if (node_id >= nodes.size()) {
            throw std::out_of_range("Node ID out of range");
        }
        return nodes[node_id];
    }

    inline bool frozen() const { return is_frozen; }

    // Raw data access for Node-API Zero-copy
    void* get_nodes_data() { return nodes.data(); }
    size_t get_nodes_bytes() const { return nodes.size() * sizeof(NodeRecord); }

    void* get_offsets_data() { return offsets.data(); }
    size_t get_offsets_bytes() const { return offsets.size() * sizeof(uint32_t); }

    void* get_edges_data() { return edges.data(); }
    size_t get_edges_bytes() const { return edges.size() * sizeof(EdgeRecord); }

    void* get_string_pool_data() { return string_pool.data(); }
    size_t get_string_pool_bytes() const { return string_pool.size() * sizeof(char); }
};
