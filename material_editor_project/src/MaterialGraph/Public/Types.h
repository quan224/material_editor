#pragma once
#include <cstdint>
#include <utility>

// 引脚方向
enum class EPinDataDirection
{
    Input,
    Output
};

// 注意：故意用弱类型 enum（不带 class）——对齐 UE EMaterialValueType 的用法，
// bitmask 成员（MCT_Float3 等）可以裸名直接用，写起来短；
// 代价是无隐式 int 转换保护，但底层类型已显式锁定 uint64_t，位运算是安全的
enum EValueType : uint64_t
{
    MCT_Unknown = 0,

    // 标量/向量
    MCT_Float1 = 1u << 0,
    MCT_Float2 = 1u << 1,
    MCT_Float3 = 1u << 2,
    MCT_Float4 = 1u << 3,

    // 纹理对象
    MCT_Texture2D = 1u<<4,
    MCT_TextureCube = 1u<<5,

    // 纹理变体
    MCT_Texture2DArray = 1u<<6,  // 贴图数组
    MCT_VolumeTexture = 1u<<8,  // 3D纹理
    MCT_TextureExternal = 1u<<12,  // 外部纹理
    MCT_TextureVirtual = 1u<<13,  // 虚拟纹理

    // 打包/模型类型
    MCT_MaterialAttributes = 1u<<11,  // 材质属性打包值
    MCT_ShadingModel = 1u<<16,  // shading model 选择值
    MCT_Substrate = 1u<<17,  // Substrate  BSDF值

    // LWC 双精度坐标 float->double 用于大世界地图
    MCT_LWCScalar = 1u<<18,  // 双精度标量
    MCT_LWCVector2 = 1u<<19,  // 双精度二维
    MCT_LWCVector3 = 1u<<20,  // 双精度三维
    MCT_LWCVector4 = 1u<<21,  // 双精度四维
    MCT_LWCMatrix = 1ull<<34,  // 双精度矩阵(大世界变换)

    // 矩阵
    MCT_Float3x3 = 1ull<<32,
    MCT_Float4x4 = 1ull<<33,

    // 类别掩码
    MCT_Float = MCT_Float1|MCT_Float2|MCT_Float3|MCT_Float4, // 任意float
    MCT_LWCType = MCT_LWCScalar|MCT_LWCVector2|MCT_LWCVector3|MCT_LWCVector4,  // 任意LWC值
    MCT_Numeric = MCT_Float|MCT_LWCType,  // 任意数值
    MCT_Texture = MCT_Texture2D|MCT_TextureCube|MCT_Texture2DArray|MCT_VolumeTexture|MCT_TextureExternal|MCT_TextureVirtual,  // 任意纹理

};

// 类型是否可以隐式转换（如从float1->floatN）
inline bool CanImplicitConvert(EValueType from, EValueType to)
{
    if (from == to)
        return true;

    static const std::pair<EValueType, EValueType> rules[] = {
        {MCT_Float1, MCT_Float2},
        {MCT_Float1, MCT_Float3},
        {MCT_Float1, MCT_Float4},
    };
    for (auto &[f, t] : rules)
    {
        if (from == f && to == t)
            return true;
    }
    return false;
}

// 获取类型的分量数（纯查询，与编译器/HLSL 无关）
// 注意 MCT_Float 返回 1——UE GetNumComponents 同样把 MCT_Float 当标量处理
// LWC 族与 float 族同分量数；掩码/矩阵/纹理/打包值返回 0（掩码问"分量数"没有意义）
inline int GetComponentCount(const EValueType& t){
    switch(t){
        case MCT_Float: case MCT_Float1: case MCT_LWCScalar: return 1;
        case MCT_Float2: case MCT_LWCVector2: return 2;
        case MCT_Float3: case MCT_LWCVector3: return 3;
        case MCT_Float4: case MCT_LWCVector4: return 4;
        default: return 0;
    }
}

// 注：类型 → HLSL 字符串的映射原本也在这里（ValueTypeToString），
// 但 HLSL 是编译器概念，违反"Types.h 零编译器知识"原则。
// 已移到 Compiler/Public/TypeSystem.h::ToHLSLType()，由编译器独占。