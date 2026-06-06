#include "parallel_parser.hpp"
#include <tree_sitter/api.h>

#if defined(_WIN32)
    #define NOMINMAX
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

extern "C" const TSLanguage *tree_sitter_cpp();

class MmapGuard {
public:
    explicit MmapGuard(const std::string& file_path) {
#if defined(_WIN32)
        hFile = CreateFileA(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return;

        LARGE_INTEGER size;
        if (!GetFileSizeEx(hFile, &size) || size.QuadPart == 0) return;
        mapped_size = static_cast<size_t>(size.QuadPart);

        hMapping = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMapping) return;

        mapped_data = static_cast<const char*>(MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0));
#else
        fd = open(file_path.c_str(), O_RDONLY);
        if (fd == -1) return;

        struct stat st;
        if (fstat(fd, &st) == -1 || st.st_size == 0) return;
        mapped_size = static_cast<size_t>(st.st_size);

        void* addr = mmap(nullptr, mapped_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED) return;
        mapped_data = static_cast<const char*>(addr);
#endif
    }

    ~MmapGuard() {
#if defined(_WIN32)
        if (mapped_data) UnmapViewOfFile(mapped_data);
        if (hMapping) CloseHandle(hMapping);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
#else
        if (mapped_data) munmap(const_cast<char*>(mapped_data), mapped_size);
        if (fd != -1) close(fd);
#endif
    }

    bool is_valid() const { return mapped_data != nullptr; }
    const char* data() const { return mapped_data; }
    size_t size() const { return mapped_size; }

private:
    const char* mapped_data = nullptr;
    size_t mapped_size = 0;
#if defined(_WIN32)
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMapping = NULL;
#else
    int fd = -1;
#endif
};

void ParallelParsingEngine::execute(const std::vector<std::string>& files_to_parse) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        while(!task_queue.empty()) task_queue.pop();
        for (const auto& file : files_to_parse) {
            task_queue.push(file);
        }
        stop_workers = false;
    }

    unsigned int thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) thread_count = 2;

    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < thread_count; ++i) {
        workers.emplace_back(&ParallelParsingEngine::worker_thread_func, this);
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        stop_workers = true;
    }
    cv.notify_all();

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ParallelParsingEngine::worker_thread_func() {
    TSParser* local_parser = ts_parser_new();
    ts_parser_set_language(local_parser, tree_sitter_cpp());

    std::vector<NodeRecord> local_nodes;
    std::vector<RawEdge> local_edges;

    while (true) {
        std::string file_path;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            cv.wait(lock, [this] { return !task_queue.empty() || stop_workers; });

            if (task_queue.empty() && stop_workers) {
                break;
            }

            if (!task_queue.empty()) {
                file_path = std::move(task_queue.front());
                task_queue.pop();
            } else {
                continue;
            }
        }

        MmapGuard mmap_file(file_path);
        if (!mmap_file.is_valid()) continue;

        TSTree* syntax_tree = ts_parser_parse_string(local_parser, nullptr, mmap_file.data(), mmap_file.size());
        if (syntax_tree) {
            process_syntax_tree(syntax_tree, file_path, local_nodes, local_edges);
            ts_tree_delete(syntax_tree);
        }
    }

    // Merge results
    {
        std::lock_guard<std::mutex> lock(results_mutex);
        uint32_t offset = static_cast<uint32_t>(global_nodes.size());

        for (auto& n : local_nodes) n.node_id += offset;
        for (auto& e : local_edges) {
            e.source_node_id += offset;
            e.target_node_id += offset;
        }

        global_nodes.insert(global_nodes.end(), 
                           std::make_move_iterator(local_nodes.begin()), 
                           std::make_move_iterator(local_nodes.end()));
        global_edges.insert(global_edges.end(), 
                           std::make_move_iterator(local_edges.begin()), 
                           std::make_move_iterator(local_edges.end()));
    }

    ts_parser_delete(local_parser);
}

void ParallelParsingEngine::process_syntax_tree(TSTree* tree, const std::string& file_path, 
                                               std::vector<NodeRecord>& local_nodes, 
                                               std::vector<RawEdge>& local_edges) {
    (void)file_path; (void)local_edges;

    TSNode root = ts_tree_root_node(tree);
    
    NodeRecord file_node{};
    file_node.node_id = static_cast<uint32_t>(local_nodes.size());
    file_node.start_line = ts_node_start_point(root).row;
    file_node.end_line = ts_node_end_point(root).row;
    file_node.type = NodeType::FILE;
    
    local_nodes.push_back(file_node);
}
