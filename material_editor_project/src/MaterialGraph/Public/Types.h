#pragma once
#include <cstdint>
#include <utility>

// 引脚方向
enum class EPinDataDirection
{
    Input,
    Output
};

enum class EValueType : uint32_t
{
    Unknown = 0,
    Float1 = 1 << 0, // 单浮点（标量）
    Float2 = 1 << 1, // 二维向量
    Float3 = 1 << 2, // 三维向量 / 颜色
    Float4 = 1 << 3, // 四维向量
    // 后续可扩展: Int1, Bool, Texture2D 等
};

// 类型是否可以隐式转换（如从float1->floatN）
inline bool CanImplicitConvert(EValueType from, EValueType to)
{
    if (from == to)
        return true;

    static const std::pair<EValueType, EValueType> rules[] = {
        {EValueType::Float1, EValueType::Float2},
        {EValueType::Float1, EValueType::Float3},
        {EValueType::Float1, EValueType::Float4},
    };
    for (auto &[f, t] : rules)
    {
        if (from == f && to == t)
            return true;
    }
    return false;
}

// 获取类型的分量数（纯查询，与编译器/HLSL 无关）
inline int GetComponentCount(EValueType type) {
    switch (type) {
        case EValueType::Float1: return 1;
        case EValueType::Float2: return 2;
        case EValueType::Float3: return 3;
        case EValueType::Float4: return 4;
        default: return 0;
    }
}

// 注：类型 → HLSL 字符串的映射原本也在这里（ValueTypeToString），
// 但 HLSL 是编译器概念，违反"Types.h 零编译器知识"原则。
// 已移到 Compiler/Public/TypeSystem.h::ToHLSLType()，由编译器独占。