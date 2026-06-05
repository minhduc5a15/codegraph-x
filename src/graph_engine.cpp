#include "graph_engine.hpp"
#include <cstring>

void GraphEngine::reserve(size_t num_nodes, size_t num_edges, size_t string_capacity) {
    if (is_frozen) return;
    nodes.reserve(num_nodes);
    offsets.reserve(num_nodes + 1);
    edges.reserve(num_edges);
    string_pool.reserve(string_capacity);
}

uint32_t GraphEngine::register_string(std::string_view str) {
    if (is_frozen) return 0;
    
    uint32_t offset = static_cast<uint32_t>(string_pool.size());
    string_pool.insert(string_pool.end(), str.begin(), str.end());
    string_pool.push_back('\0'); // Kết thúc chuỗi chuẩn C cho khả năng tương thích
    return offset;
}

std::string_view GraphEngine::resolve_string(uint32_t offset) const {
    if (offset >= string_pool.size()) {
        return "";
    }
    // Trả về string_view trỏ trực tiếp vào pool. 
    // Lưu ý: Chỉ an toàn sau khi pool ngừng mở rộng (is_frozen = true)
    return std::string_view(&string_pool[offset]);
}

void GraphEngine::build_from_raw(std::vector<NodeRecord>&& raw_nodes, std::vector<RawEdge>& raw_edges) {
    if (is_frozen) return;

    // 1. Tiếp nhận và sắp xếp lại nodes (nếu cần thiết theo ID, ở đây giả định raw_nodes đã khớp ID)
    nodes = std::move(raw_nodes);
    uint32_t num_nodes = static_cast<uint32_t>(nodes.size());

    // 2. Tính toán Out-degree (bậc ra) của từng Node
    // offsets có kích thước |V| + 1, khởi tạo bằng 0
    offsets.assign(num_nodes + 1, 0);
    for (const auto& edge : raw_edges) {
        if (edge.source_node_id < num_nodes) {
            // Tạm thời lưu bậc tại offsets[id + 1]
            offsets[edge.source_node_id + 1]++;
        }
    }

    // 3. Biến đổi mảng đếm thành mảng dịch chuyển tích lũy (Prefix Sum)
    // offsets[i] sẽ là vị trí bắt đầu của node i trong mảng edges phẳng
    for (uint32_t i = 1; i <= num_nodes; ++i) {
        offsets[i] += offsets[i - 1];
    }

    // 4. Điền dữ liệu cạnh vào mảng phẳng edges liên tục
    edges.resize(offsets[num_nodes]);
    
    // Mảng tạm để theo dõi vị trí ghi hiện tại cho từng node
    std::vector<uint32_t> write_cursors = offsets;

    for (const auto& edge : raw_edges) {
        if (edge.source_node_id < num_nodes) {
            uint32_t& pos = write_cursors[edge.source_node_id];
            edges[pos] = { edge.target_node_id, edge.type };
            pos++;
        }
    }

    // 5. Khóa đồ thị (Freeze) và giải phóng bộ nhớ thừa (Compact)
    is_frozen = true;
    
    // Tối ưu hóa Cache Locality bằng cách giải phóng vùng nhớ dự phòng không sử dụng
    nodes.shrink_to_fit();
    offsets.shrink_to_fit();
    edges.shrink_to_fit();
    string_pool.shrink_to_fit();
}
