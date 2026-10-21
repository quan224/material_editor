#pragma once
#include <cstdint>
#include <type_traits>

// enum class 位运算符全家桶。对照 UE ENUM_CLASS_FLAGS（Misc/EnumClassFlags.h:8）
// enum class 默认禁止位运算（强类型隔离），此宏为指定的枚举补齐 |&^~! 五组运算符，
// 使其可当位标志使用。用法：enum class EFlags : uint32_t {...};  之后 ENUM_CLASS_FLAGS(EFlags);
#define ENUM_CLASS_FLAGS(Enum) \
    constexpr Enum& operator|=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((std::underlying_type_t<Enum>)Lhs | (std::underlying_type_t<Enum>)Rhs); } \
    constexpr Enum& operator&=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((std::underlying_type_t<Enum>)Lhs & (std::underlying_type_t<Enum>)Rhs); } \
    constexpr Enum& operator^=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((std::underlying_type_t<Enum>)Lhs ^ (std::underlying_type_t<Enum>)Rhs); } \
    constexpr Enum  operator| (Enum  Lhs, Enum Rhs) { return (Enum)((std::underlying_type_t<Enum>)Lhs | (std::underlying_type_t<Enum>)Rhs); } \
    constexpr Enum  operator& (Enum  Lhs, Enum Rhs) { return (Enum)((std::underlying_type_t<Enum>)Lhs & (std::underlying_type_t<Enum>)Rhs); } \
    constexpr Enum  operator^ (Enum  Lhs, Enum Rhs) { return (Enum)((std::underlying_type_t<Enum>)Lhs ^ (std::underlying_type_t<Enum>)Rhs); } \
    constexpr bool  operator! (Enum  E)             { return !(std::underlying_type_t<Enum>)E; } \
    constexpr Enum  operator~ (Enum  E)             { return (Enum)~(std::underlying_type_t<Enum>)E; }

// 含全部/任一标志判定。对照 UE EnumHasAllFlags/EnumHasAnyFlags（同文件 :30/:37）
template<typename Enum>
constexpr bool EnumHasAllFlags(Enum Flags, Enum Contains) {
    using UnderlyingType = std::underlying_type_t<Enum>;
    return ((UnderlyingType)Flags & (UnderlyingType)Contains) == (UnderlyingType)Contains;
}

template<typename Enum>
constexpr bool EnumHasAnyFlags(Enum Flags, Enum Contains) {
    using UnderlyingType = std::underlying_type_t<Enum>;
    return (((UnderlyingType)Flags & (UnderlyingType)Contains) != 0);
}
