#pragma once
#include <vector>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <utility>

enum class NodeType : uint8_t {
    FILE = 0,
    CLASS = 1,
    FUNCTION = 2,
    METHOD = 3
};

enum class EdgeType : uint8_t {
    CALLS = 0,
    INHERITS = 1,
    IMPORTS = 2
};

#pragma pack(push, 1)
struct NodeRecord {
    uint32_t node_id;
    NodeType type;
    uint32_t name_pool_offset; // Trỏ vào vùng nhớ String Pool
    uint32_t path_pool_offset;
    uint32_t start_line;
    uint32_t end_line;
};

struct EdgeRecord {
    uint32_t target_node_id;
    EdgeType type;
};
#pragma pack(pop)

class GraphEngine {
private:
    std::vector<NodeRecord> nodes;
    std::vector<uint32_t> offsets;
    std::vector<EdgeRecord> edges;
    std::vector<char> string_pool;

public:
    GraphEngine();

    void reserve(size_t num_nodes, size_t num_edges, size_t string_capacity);
    uint32_t register_string(std::string_view str);
    std::string_view resolve_string(uint32_t offset) const;
    void add_node(NodeRecord&& node);
    void finalize_node_edges(const std::vector<EdgeRecord>& node_edges);

    // Truy vấn các đỉnh lân cận với độ phức tạp thời gian cực tiểu O(k)
    inline std::pair<const EdgeRecord*, size_t> get_adjacent_edges(uint32_t node_id) const {
        if (offsets.empty() || node_id >= offsets.size() - 1) {
            return { nullptr, 0 };
        }
        uint32_t start_idx = offsets[node_id];
        uint32_t end_idx = offsets[node_id + 1];
        return { &edges[start_idx], static_cast<size_t>(end_idx - start_idx) };
    }

    inline size_t get_node_count() const {
        return nodes.size();
    }

    inline const NodeRecord& get_node(uint32_t node_id) const {
        return nodes[node_id];
    }
};