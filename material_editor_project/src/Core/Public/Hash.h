#pragma once
#include <cstdint>
#include <string>


inline uint64_t HashString(const std::string& s)
{   // 字符串转hash 用于加速
    // FNV-1a 哈希
    uint64_t hash = 14695981039346656037ULL;
    for (char c : s)
    {
        hash ^= (uint64_t)c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline uint64_t HashRaw(const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline uint64_t HashCombine(uint64_t a, uint64_t b) {
    // boost::hash_combine 算法
    return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2));
}