# Codegraph-X

Codegraph-X là một dự án máy chủ MCP (Model Context Protocol) hỗ trợ phân tích mã nguồn và xây dựng biểu đồ quan hệ (dependency graph) trong bộ nhớ (in-memory) để hỗ trợ các AI Agent (như Cursor, Cline, Gemini, v.v.) trong việc đọc hiểu mã nguồn.

> ⚠️ **CẢNH BÁO QUAN TRỌNG / IMPORTANT WARNING**
>
> - **Chỉ hỗ trợ C++**: Hiện tại, dự án **chỉ mới hỗ trợ phân tích các dự án viết bằng C++**. Trình phân tích cú pháp cho các ngôn ngữ khác (như TypeScript, Python, Rust, Go) chưa được hiện thực hóa.
> - **Trạng thái dự án**: Dự án đang ở giai đoạn **thử nghiệm ban đầu (early prototype)**. Các tính năng và API có thể thay đổi hoặc hoạt động chưa thực sự ổn định với các dự án có cấu trúc C++ quá phức tạp hoặc phi tiêu chuẩn.

---

## Cách thức hoạt động

Dự án sử dụng bộ thư viện phân tích cú pháp Tree-sitter (thông qua C++ và Node-API bindings) để:

1. Duyệt qua các file mã nguồn C++ trong thư mục làm việc.
2. Xây dựng một biểu đồ quan hệ các ký hiệu (symbol map, caller/callee relationships) trực tiếp trong RAM.
3. Cung cấp các công cụ MCP để AI Agent truy vấn thông tin cấu trúc thay vì phải thực hiện tìm kiếm văn bản thông thường (grep) chậm chạp.

---

## Các công cụ MCP cung cấp

Mở rộng qua giao thức MCP, dự án cung cấp 2 công cụ chính cho AI:

- `explore_codebase`: Nhận đầu vào là từ khóa hoặc câu hỏi tự nhiên để tìm kiếm các thực thể, luồng thực thi và cấu trúc liên quan trong biểu đồ mã nguồn.
- `read_node`: Lấy toàn bộ nội dung mã nguồn của một nút cụ thể thông qua `node_id` được trả về từ `explore_codebase`.

---

## Yêu cầu hệ thống

Để build và chạy dự án cục bộ, bạn cần chuẩn bị:

- **Node.js** (đã test với phiên bản 18+)
- **CMake** (phiên bản 3.26 trở lên) và một trình biên dịch hỗ trợ chuẩn **C++20** (GCC, Clang hoặc MSVC) để biên dịch các module C++ nguyên bản (native addon).

---

## Hướng dẫn cài đặt và sử dụng

### 1. Build dự án cục bộ

Tải các dependencies và biên dịch cả TypeScript lẫn phần mã nguồn C++:

```bash
npm install
npm run build
```

### 2. Tích hợp tự động vào AI Agent

Chạy lệnh cài đặt tương tác để đăng ký máy chủ MCP với các ứng dụng AI được hỗ trợ (như Cursor, Cline):

```bash
node dist/cli.js install
```

### 3. Chạy máy chủ MCP thủ công

Chạy máy chủ thông qua giao tiếp stdio:

```bash
node dist/cli.js mcp [đường_dẫn_đến_dự_án]
```

---

## Lộ trình phát triển dự kiến

- [ ] Hoàn thiện phân giải ký hiệu liên file (cross-file symbol resolution).
- [ ] Bổ sung hỗ trợ phân tích ngôn ngữ JavaScript/TypeScript.
- [ ] Bổ sung hỗ trợ ngôn ngữ Python.
- [ ] Cơ chế cập nhật biểu đồ tăng trưởng (incremental updates / watch mode).
