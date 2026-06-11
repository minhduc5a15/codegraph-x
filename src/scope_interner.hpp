#pragma once

#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "string_pool.hpp"

struct ScopeNode {
    uint32_t parent_scope_id;
    uint32_t name_offset;
};

class GlobalScopeInterner {
private:
    std::vector<ScopeNode> scopes;
    std::unordered_map<uint64_t, uint32_t> scope_lookup;
    mutable std::mutex mtx;

public:
    GlobalScopeInterner() {
        scopes.push_back({0, 0});  // Scope 0 (Global Scope)
    }

    uint32_t get_or_create_scope(const uint32_t parent_scope_id, const std::string_view name, StringPool& global_pool) {
        const uint64_t h_name = std::hash<std::string_view>{}(name);
        const uint64_t key = (static_cast<uint64_t>(parent_scope_id) << 32) ^ h_name;

        // Fast path: Thread-local cache
        thread_local std::unordered_map<uint64_t, uint32_t> local_cache;
        const auto local_it = local_cache.find(key);
        if (local_it != local_cache.end()) return local_it->second;

        // Slow path: Global lock
        std::lock_guard<std::mutex> lock(mtx);
        const auto it = scope_lookup.find(key);
        if (it != scope_lookup.end()) {
            local_cache[key] = it->second;
            return it->second;
        }

        const uint32_t name_offset = global_pool.get_or_add(name);
        const auto new_id = static_cast<uint32_t>(scopes.size());
        scopes.push_back({parent_scope_id, name_offset});
        scope_lookup[key] = new_id;
        local_cache[key] = new_id;
        return new_id;
    }

    uint32_t get_parent(const uint32_t scope_id) const {
        if (scope_id == 0 || scope_id >= scopes.size()) return 0;
        return scopes[scope_id].parent_scope_id;
    }
};
