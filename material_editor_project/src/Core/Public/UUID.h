#pragma once
#include <cstdint>
#include <string>
#include <random>
#include <functional>

struct UUID{

    uint64_t high = 0;
    uint64_t low = 0;

    static UUID Generate(){
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dist;
        return {dist(gen), dist(gen)};
    }

    static UUID Invalid()
    {
        return {0, 0};
    }

    std::string ToString() const
    {
        // 格式: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
        char buf[37];
        snprintf(buf, sizeof(buf),
                 "%08x-%04x-%04x-%04x-%08x%04x",
                 (uint32_t)(high >> 32),
                 (uint32_t)(high >> 16) & 0xFFFF,
                 (uint32_t)(high) & 0xFFFF,
                 (uint32_t)(low >> 48),
                 (uint32_t)(low >> 16) & 0xFFFFFFFF,
                 (uint32_t)(low) & 0xFFFF);
        return buf;
    }

    bool operator==(const UUID &o) const{return high == o.high && low == o.low;}
    bool operator!=(const UUID &o) const { return !(*this == o); }
    bool operator<(const UUID &o) const { return high < o.high || (high == o.high && low < o.low); }
    bool IsValid() const { return high != 0 || low != 0; }
};

// 让 UUID 可以作为 std::map / std::unordered_map 的 key

namespace std
{
    template <>
    struct hash<UUID>
    {
        size_t operator()(const UUID &id) const
        {
            size_t h = hash<uint64_t>()(id.high);
            h ^= hash<uint64_t>()(id.low) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
}