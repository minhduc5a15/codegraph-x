#pragma once
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

enum class NodeType : uint8_t { FILE = 0, CLASS = 1, FUNCTION = 2, METHOD = 3, EXTERNAL = 4 };

enum class EdgeType : uint8_t { CALLS = 0, INHERITS = 1, IMPORTS = 2, AMBIGUOUS_CALL = 3 };

struct NodeRecord {
    uint32_t node_id;           // offset 0
    uint32_t name_pool_offset;  // offset 4
    uint32_t path_pool_offset;  // offset 8
    uint32_t start_line;        // offset 12
    uint32_t end_line;          // offset 16
    NodeType type;              // offset 20
    uint8_t flags;              // offset 21 (1 byte)
    uint16_t start_column;      // offset 22 (2 bytes)
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

struct StringHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(const std::string_view txt) const { return std::hash<std::string_view>{}(txt); }
    [[nodiscard]] size_t operator()(const std::string& txt) const { return std::hash<std::string>{}(txt); }
};

class InMemoryGraphEngine {
private:
    std::vector<NodeRecord> nodes;
    std::vector<uint32_t> offsets;
    std::vector<EdgeRecord> edges;
    std::vector<char> string_pool;
    std::unordered_map<std::string, uint32_t, StringHash, std::equal_to<>> string_lookup;
    std::vector<uint32_t> name_index;
    std::vector<uint32_t> short_name_index;
    std::vector<uint32_t> path_index;
    std::vector<uint32_t> incoming_offsets;
    std::vector<uint32_t> incoming_edges;
    bool is_frozen = false;

    void build_out_degree_and_edges(const std::vector<RawEdge>& raw_edges, uint32_t num_nodes, size_t& dropped_edges);
    void build_sorted_indices(uint32_t num_nodes);
    void build_incoming_edges(const std::vector<RawEdge>& raw_edges, uint32_t num_nodes);

public:
    InMemoryGraphEngine() = default;

    InMemoryGraphEngine(const InMemoryGraphEngine&) = delete;
    InMemoryGraphEngine& operator=(const InMemoryGraphEngine&) = delete;
    InMemoryGraphEngine(InMemoryGraphEngine&&) noexcept = default;
    InMemoryGraphEngine& operator=(InMemoryGraphEngine&&) noexcept = default;

    void reserve(size_t num_nodes, size_t num_edges, size_t string_capacity);

    void take_string_pool(
        std::vector<char>&& pool, std::unordered_map<std::string, uint32_t, StringHash, std::equal_to<>>&& lookup
    ) {
        string_pool = std::move(pool);
        string_lookup = std::move(lookup);
    }

    uint32_t register_string(std::string_view str);
    std::string_view resolve_string(uint32_t offset) const;

    void build_from_raw(std::vector<NodeRecord>&& raw_nodes, const std::vector<RawEdge>& raw_edges);

    std::vector<uint32_t> search_substring(std::string_view query, size_t limit = 100) const;
    std::vector<uint32_t> search_path_substring(std::string_view query, size_t limit = 100) const;
    std::vector<uint32_t> search_fuzzy(std::string_view query, size_t limit = 50) const;

    inline std::pair<const EdgeRecord*, size_t> get_adjacent_edges(const uint32_t node_id) const {
        if (!is_frozen || node_id >= nodes.size()) {
            return {nullptr, 0};
        }
        const uint32_t start_idx = offsets[node_id];
        const uint32_t end_idx = offsets[node_id + 1];
        const size_t count = end_idx - start_idx;
        if (count == 0) return {nullptr, 0};
        return {&edges[start_idx], count};
    }

    inline size_t get_node_count() const { return nodes.size(); }

    inline const NodeRecord& get_node(const uint32_t node_id) const {
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

    void* get_name_index_data() { return name_index.data(); }
    size_t get_name_index_bytes() const { return name_index.size() * sizeof(uint32_t); }

    void* get_short_name_index_data() { return short_name_index.data(); }
    size_t get_short_name_index_bytes() const { return short_name_index.size() * sizeof(uint32_t); }

    void* get_path_index_data() { return path_index.data(); }
    size_t get_path_index_bytes() const { return path_index.size() * sizeof(uint32_t); }

    void* get_incoming_offsets_data() { return incoming_offsets.data(); }
    size_t get_incoming_offsets_bytes() const { return incoming_offsets.size() * sizeof(uint32_t); }

    void* get_incoming_edges_data() { return incoming_edges.data(); }
    size_t get_incoming_edges_bytes() const { return incoming_edges.size() * sizeof(uint32_t); }
};
