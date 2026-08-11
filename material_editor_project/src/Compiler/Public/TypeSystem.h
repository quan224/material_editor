#pragma once
#include "MaterialGraph/Public/Types.h"
#include <cstdint>

// 编译器专属的类型算法（依赖 MaterialGraph 的基础类型定义）
// - GetArithmeticResultType：两个类型如何运算出第三个（语义级，编译器内部规则）
// - ToHLSLType：类型 → HLSL 关键字（HLSL 是编译器概念，不应出现在 Types.h）
//
// 注：GetComponentCount 属于"类型本身的属性查询"，留在 Types.h 里作为 free function。
//     本类不再重复定义，调用方请直接使用 ::GetComponentCount。
class TypeSystem{
public:
    // 两个类型做算数运算后的结果类型
    static EValueType GetArithmeticResultType(EValueType a, EValueType b){
        if (a==EValueType::Unknown || b==EValueType::Unknown){
            return EValueType::Unknown;
        }
        if (a==b) return a;
        if (a==EValueType::Float1) return b;
        if (b==EValueType::Float1) return a;
        if (a == EValueType::Int1) return b;
        if (b == EValueType::Int1) return a;
        return EValueType::Unknown;
    }

    // 类型到 HLSL 类型名
    static const char* ToHLSLType(EValueType type){
        switch (type)
        {
        case EValueType::Float1: return "float";
        case EValueType::Float2: return "float2";
        case EValueType::Float3: return "float3";
        case EValueType::Float4: return "float4";
        case EValueType::Int1: return "int";
        case EValueType::Int2: return "int2";
        case EValueType::Int3: return "int3";
        case EValueType::Int4: return "int4";
        case EValueType::Matrix3x3: return "float3x3";
        case EValueType::Matrix4x4: return "float4x4";
        default: return "float";
        }
    }

};