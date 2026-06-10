#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

static constexpr uint32_t GOLDEN_RATIO_HASH = 2654435769U;

class FlatSymbolMultiMap {
private:
    struct Slot {
        uint32_t key;
        uint32_t value;
    };
    std::vector<Slot> table;
    size_t capacity;
    static constexpr uint32_t EMPTY_KEY = 0xFFFFFFFF;
    inline size_t hash_int(uint32_t x) const { return (x * GOLDEN_RATIO_HASH) % capacity; }

public:
    explicit FlatSymbolMultiMap(const size_t expected_size) {
        capacity = std::max<size_t>(expected_size * 2, 16);
        table.resize(capacity, {EMPTY_KEY, 0});
    }
    void insert(const uint32_t key, const uint32_t value) {
        size_t idx = hash_int(key);
        while (table[idx].key != EMPTY_KEY) {
            idx = (idx + 1) % capacity;
        }
        table[idx] = {key, value};
    }
    std::vector<uint32_t> find_all(const uint32_t key) const {
        std::vector<uint32_t> results;
        size_t idx = hash_int(key);
        while (table[idx].key != EMPTY_KEY) {
            if (table[idx].key == key) results.push_back(table[idx].value);
            idx = (idx + 1) % capacity;
        }
        return results;
    }
};
