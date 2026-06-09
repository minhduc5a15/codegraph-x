# Kế hoạch Sửa đổi: Phân giải Dependency chéo (Cross-file Resolution)

## 1. Đánh giá hiện trạng
Cơ chế hiện tại trong `build_flat_graph()` của `src/parallel_parser.cpp` là một kỹ thuật 2-pass cực kỳ ngây thơ, thiếu thực tế và **sẽ thất bại** trên bất kỳ codebase C++ thực tế nào.

**Các điểm nghẽn và lỗ hổng chết người:**
1. **Lỗi đè Symbol (Overriding):** Dùng `std::unordered_map<std::string, uint32_t>` làm Symbol Map toàn cục. Trong C++, Function Overloading là tính năng cơ bản. Hai hàm cùng tên nhưng khác tham số sẽ đè lên nhau, làm sai lệch hoàn toàn đồ thị.
2. **Logic Phân giải Scope sai lệch:** Chỉ kiểm tra Scope trực tiếp (`source_name.rfind("::")`). Bỏ qua hoàn toàn quy tắc tìm kiếm giật lùi (Lexical Scope Climbing) của C++.
3. **Trích xuất AST bằng DFS:** `find_first_identifier` chui xuống lá đầu tiên của AST. Với `obj.method()`, nó sẽ lấy `obj` làm target symbol thay vì `method`. Lỗi logic nghiêm trọng.
4. **Xoá âm thầm External Edges:** Nếu cạnh không giải quyết được (ví dụ gọi stdlib hoặc các package ngoài), nó bị âm thầm xoá bỏ. Graph sẽ bị thiếu nét đứt gãy.
5. **Cổ chai Đơn luồng:** `build_flat_graph()` gom mọi thứ về luồng chính để ghép chuỗi (`std::string`) và look-up map, tốn kém cả CPU lẫn RAM.

---

## 2. Kế hoạch thiết kế lại (Redesign Plan)

Mục tiêu: Đảm bảo tính đúng đắn (Correctness), Mở rộng được (Scalability) và Thực tế (Practicality).

### Giai đoạn 1: Sửa Symbol Map & Hỗ trợ Đa hình/Overload
**Vấn đề:** 1 Chuỗi tên hàm có thể trỏ tới nhiều ID.
**Giải pháp:**
- Thay đổi cấu trúc Symbol Map thành `std::unordered_map<std::string, std::vector<uint32_t>> global_symbols;`.
- Khi resolve, nếu một symbol trỏ tới nhiều ID (do overloading), tạo các cạnh với cờ đánh dấu `EdgeType::AMBIGUOUS_CALL`. Việc xử lý ambiguity (type inference) tạm thời đẩy xuống client hoặc module phân tích sau.

### Giai đoạn 2: Trích xuất AST bằng Tree-sitter Queries
**Vấn đề:** DFS `find_first_identifier` lấy sai định danh khi có OOP call (e.g., `obj.method()`).
**Giải pháp:**
- Vứt bỏ hàm `find_first_identifier`.
- Sử dụng **Tree-sitter Query (TSQ)** chuẩn xác để bắt node. 
  - Khởi tạo TS Query một lần: `(call_expression function: (field_expression field: (field_identifier) @target))`, `(call_expression function: (identifier) @target)`.
  - Dùng API `ts_query_cursor_exec` để bốc chính xác cái tên hàm cần gọi ra khỏi AST. Tốc độ cao hơn, chính xác tuyệt đối.

### Giai đoạn 3: Phân giải Scope theo chuẩn C++ (Lexical Climbing)
**Vấn đề:** Lỗi tìm kiếm Scope khi gọi hàm ở Scope cha hoặc Global.
**Giải pháp:**
- Trong quá trình parse, mỗi Node (`TempNodeRecord`) cần lưu cấu trúc phân tầng, ví dụ: mảng các namespace cha `std::vector<std::string> enclosing_scopes`.
- Khi resolve cạnh chưa rõ, áp dụng thuật toán:
  ```cpp
  // Mã giả
  string target = e.target_symbol;
  for (int i = enclosing_scopes.size(); i >= 0; --i) {
      string try_scope = join(enclosing_scopes, 0, i) + "::" + target;
      if (try_scope == "::" + target) try_scope = target; // Global
      if (global_symbols.contains(try_scope)) {
          // Nối cạnh và break
      }
  }
  ```

### Giai đoạn 4: Quản lý External Dependencies Explicitly
**Vấn đề:** Ẩn lỗi khi missing edge.
**Giải pháp:**
- Thêm `NodeType::EXTERNAL` vào enum `NodeType`.
- Nếu thuật toán Scope Climbing ở Giai đoạn 3 không tìm thấy đích đến, **KHÔNG ĐƯỢC XOÁ**.
- Tạo một Node ảo mang kiểu `EXTERNAL` chứa tên `e.target_symbol` (nếu chưa có).
- Trỏ cạnh unresolved về Node ảo này. Hệ thống sẽ thấy được mã nguồn phụ thuộc vào thư viện ngoài nào.

### Giai đoạn 5: Tối ưu Bộ nhớ và Cổ chai (Memory & Bottleneck Optimization)
**Vấn đề:** Tốn kém mem/cpu khi cấp phát `std::string` ở luồng chính.
**Giải pháp:**
- Xây dựng một `StringPool` toàn cục, băm mọi `std::string` thành `uint32_t` ngay từ lúc các worker parse.
- Symbol Map chuyển thành `std::unordered_map<uint32_t, std::vector<uint32_t>>`.
- Thuật toán `build_flat_graph` giờ đây chỉ làm việc với số nguyên, tốc độ look-up tăng gấp hàng chục lần.
- Đưa thuật toán phân giải Scope Climbing vào các luồng song song (sau khi bảng Symbol toàn cục đã được tổng hợp ở dạng read-only).

---

## 3. Lịch trình triển khai (Execution Schedule)
- **Bước 1:** Cập nhật TS Queries và dỡ bỏ `find_first_identifier` (1 ngày).
- **Bước 2:** Cập nhật Enum `NodeType` và `EdgeType` (thêm EXTERNAL, AMBIGUOUS). Điều chỉnh C++ struct (`InMemoryGraphEngine` / `parallel_parser`) (1 ngày).
- **Bước 3:** Thay đổi logic resolve cạnh thành Lexical Climbing và gắn External Nodes (2 ngày).
- **Bước 4:** Áp dụng thuật toán băm chuỗi (String Pool) và xử lý đa luồng cho resolve phase (2 ngày).
- **Bước 5:** Viết các Test cases C++ cụ thể: Overloading, Deep namespace fallback, External library calls.

Đánh giá mức độ khả thi sau sửa đổi: 8/10. Hệ thống sẽ chính xác hơn rất nhiều về mặt kỹ thuật thay vì là đồ chơi như hiện tại.
