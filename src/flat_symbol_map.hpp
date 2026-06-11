#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

#include "in_memory_graph_engine.hpp"

struct ScopeLookupKey {
    uint32_t scope_id;
    uint32_t symbol_offset;
    NodeType kind;

    bool operator==(const ScopeLookupKey& other) const {
        return scope_id == other.scope_id && symbol_offset == other.symbol_offset && kind == other.kind;
    }
};

struct ScopeLookupHash {
    size_t operator()(const ScopeLookupKey& key) const {
        const size_t h1 = std::hash<uint32_t>{}(key.scope_id);
        const size_t h2 = std::hash<uint32_t>{}(key.symbol_offset);
        const size_t h3 = std::hash<uint8_t>{}(static_cast<uint8_t>(key.kind));
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

class FlatSymbolMultiMap {
private:
    struct Slot {
        ScopeLookupKey key;
        uint32_t value;
        bool is_empty;
    };
    std::vector<Slot> table;
    size_t capacity;
    ScopeLookupHash hasher;

    inline size_t get_hash(const ScopeLookupKey& key) const { return hasher(key) % capacity; }

public:
    explicit FlatSymbolMultiMap(const size_t expected_size) {
        capacity = std::max<size_t>(expected_size * 2, 16);
        table.resize(capacity, {ScopeLookupKey{0, 0, NodeType::FILE}, 0, true});
    }

    void insert(const ScopeLookupKey& key, const uint32_t value) {
        size_t idx = get_hash(key);
        while (!table[idx].is_empty) {
            idx = (idx + 1) % capacity;
        }
        table[idx] = {key, value, false};
    }

    std::vector<uint32_t> find_all(const ScopeLookupKey& key) const {
        std::vector<uint32_t> results;
        size_t idx = get_hash(key);
        while (!table[idx].is_empty) {
            if (table[idx].key == key) {
                results.push_back(table[idx].value);
            }
            idx = (idx + 1) % capacity;
        }
        return results;
    }
};
