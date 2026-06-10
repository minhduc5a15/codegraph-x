#pragma once

#include <fstream>
#include <string>
#include <vector>

class FileBuffer {
public:
    explicit FileBuffer(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return;

        const std::streamsize size = file.tellg();
        if (size <= 0) return;

        file.seekg(0, std::ios::beg);
        buffer.resize(static_cast<size_t>(size));
        if (file.read(buffer.data(), size)) {
            valid = true;
        }
    }

    bool is_valid() const { return valid; }
    const char* data() const { return buffer.data(); }
    size_t size() const { return buffer.size(); }

private:
    std::vector<char> buffer;
    bool valid = false;
};
