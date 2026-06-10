#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "in_memory_graph_engine.hpp"

class StringPool {
public:
    std::vector<char> pool;
    std::unordered_map<std::string, uint32_t, StringHash, std::equal_to<>> lookup;

    uint32_t get_or_add(const std::string_view s) {
        const auto it = lookup.find(s);
        if (it != lookup.end()) return it->second;

        const auto offset = static_cast<uint32_t>(pool.size());
        pool.insert(pool.end(), s.begin(), s.end());
        pool.push_back('\0');
        lookup.emplace(std::string(s), offset);
        return offset;
    }

    std::string_view resolve(const uint32_t offset) const { return {&pool[offset]}; }
};
