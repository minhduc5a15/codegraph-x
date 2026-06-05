#pragma once
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>
#include <algorithm>
#include <stdexcept>

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
    uint32_t name_pool_offset;
    uint32_t path_pool_offset;
    uint32_t start_line;
    uint32_t end_line;
};

struct EdgeRecord {
    uint32_t target_node_id;
    EdgeType type;
};
#pragma pack(pop)

struct RawEdge {
    uint32_t source_node_id;
    uint32_t target_node_id;
    EdgeType type;
};

class GraphEngine {
private:
    std::vector<NodeRecord> nodes;
    std::vector<uint32_t> offsets; // CSR Offsets: size |V| + 1
    std::vector<EdgeRecord> edges;  // CSR Edges: flat array
    std::vector<char> string_pool;  // String Pool: flat char array
    bool is_frozen = false;

public:
    GraphEngine() = default;

    // Ngăn chặn copy để tránh rò rỉ hoặc vô hiệu hóa pointer
    GraphEngine(const GraphEngine&) = delete;
    GraphEngine& operator=(const GraphEngine&) = delete;
    GraphEngine(GraphEngine&&) noexcept = default;
    GraphEngine& operator=(GraphEngine&&) noexcept = default;

    void reserve(size_t num_nodes, size_t num_edges, size_t string_capacity);
    
    // Đăng ký chuỗi vào pool, trả về offset
    uint32_t register_string(std::string_view str);
    
    // Giải mã chuỗi từ offset (Chỉ an toàn tuyệt đối sau khi frozen)
    std::string_view resolve_string(uint32_t offset) const;

    /**
     * @brief Xây dựng đồ thị CSR từ dữ liệu thô bằng thuật toán Prefix Sum.
     * Tách biệt quá trình thu thập (Write) và truy vấn (Read).
     */
    void build_from_raw(std::vector<NodeRecord>&& raw_nodes, std::vector<RawEdge>& raw_edges);

    // Truy vấn các cạnh lân cận O(1)
    inline std::pair<const EdgeRecord*, size_t> get_adjacent_edges(uint32_t node_id) const {
        if (!is_frozen || node_id >= nodes.size()) {
            return { nullptr, 0 };
        }
        uint32_t start_idx = offsets[node_id];
        uint32_t end_idx = offsets[node_id + 1];
        size_t count = static_cast<size_t>(end_idx - start_idx);
        if (count == 0) return { nullptr, 0 };
        return { &edges[start_idx], count };
    }

    inline size_t get_node_count() const {
        return nodes.size();
    }

    inline const NodeRecord& get_node(uint32_t node_id) const {
        if (node_id >= nodes.size()) {
            throw std::out_of_range("Node ID out of range");
        }
        return nodes[node_id];
    }

    inline bool frozen() const { return is_frozen; }
};
