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
    MCT_TextureCubeArray = 1u<<7,  // 立方体贴图数组
    MCT_VolumeTexture = 1u<<8,  // 3D纹理
    MCT_StaticBool = 1u<<9,  // 静态开关
    MCT_TextureExternal = 1u<<12,  // 外部纹理
    MCT_TextureVirtual = 1u<<13,  // 虚拟纹理
    MCT_SparseVolumeTexture = 1u<<14,  // 稀疏体积纹理
    MCT_VTPageTableResult = 1u<<15,  // VT页表查询结果(内部类型)

    // 打包/模型类型
    MCT_MaterialAttributes = 1u<<11,  // 材质属性打包值
    MCT_ShadingModel = 1u<<16,  // shading model 选择值
    MCT_Substrate = 1u<<17,  // Substrate  BSDF值

    // LWC 双精度坐标 float->double 用于大世界地图
    MCT_LWCScalar = 1u<<18,  // 双精度标量
    MCT_LWCVector2 = 1u<<19,  // 双精度二维
    MCT_LWCVector3 = 1u<<20,  // 双精度三维
    MCT_LWCVector4 = 1u<<21,  // 双精度四维
    MCT_Execution = 1u<<22,  // 执行流引脚(静态开关分支)
    MCT_Bool = 1u<<24,  // 动态bool
    MCT_LWCMatrix = 1ull<<34,  // 双精度矩阵(大世界变换)

    // 无符号整数
    MCT_UInt1 = 1u<<25,
    MCT_UInt2 = 1u<<26,
    MCT_UInt3 = 1u<<27,
    MCT_UInt4 = 1u<<28,

    // 矩阵
    MCT_Float3x3 = 1ull<<32,
    MCT_Float4x4 = 1ull<<33,
    MCT_Unexposed = 1ull<<36,  // 不暴露给用户的内部类型

    // 类别掩码
    MCT_Float = MCT_Float1|MCT_Float2|MCT_Float3|MCT_Float4, // 任意float
    MCT_LWCType = MCT_LWCScalar|MCT_LWCVector2|MCT_LWCVector3|MCT_LWCVector4,  // 任意LWC值
    MCT_UInt = MCT_UInt1|MCT_UInt2|MCT_UInt3|MCT_UInt4,  // 任意无符号整数
    MCT_Numeric = MCT_Float|MCT_LWCType|MCT_UInt,  // 任意数值(对齐UE含UInt; UE还含Bool,教学版Bool单独判)
    MCT_Texture = MCT_Texture2D|MCT_TextureCube|MCT_Texture2DArray|MCT_TextureCubeArray|MCT_VolumeTexture|MCT_TextureExternal|MCT_TextureVirtual|MCT_SparseVolumeTexture,  // 任意纹理(对齐UE掩码成员,不含VTPageTableResult内部类型)

};

// 类型是否可以隐式转换
inline bool CanImplicitConvert(EValueType from, EValueType to)
{
    // 流只能和流
    if(from&MCT_Execution && to&MCT_Execution){
        return from == to;
    }
    // 未知放行（未知更多是还没连线）
    if(from&MCT_Unknown){
        return true;
    }
    if(to&MCT_Unknown){
        return true;
    }
    // 同族放行
    if(from&to){
        return true;
    }
    // 数值放宽(float2 -> float3)
    if(from&MCT_Numeric && to&MCT_Numeric){
        return true;
    }
    if(from == MCT_Bool && to == MCT_StaticBool){
        return true;
    }
    return false;
}

// 能否算术运算
inline bool IsNumericType(EValueType t){
    return t & (MCT_Float|MCT_LWCType|MCT_UInt|MCT_ShadingModel);
}

// 能否运算
inline bool IsPrimitiveType(EValueType t){
    return t & (MCT_Float|MCT_LWCType|MCT_UInt|MCT_ShadingModel|MCT_Bool|MCT_StaticBool);
}

// float/lwc 族
inline bool IsFloatNumericType(EValueType t){
    return t & (MCT_Float|MCT_LWCType);
}

// float->LWC
inline EValueType ToLWCType(EValueType f){
    switch(f){
        case MCT_Float:case MCT_Float1: return MCT_LWCScalar;
        case MCT_Float2: return MCT_LWCVector2;
        case MCT_Float3: return MCT_LWCVector3;
        case MCT_Float4: return MCT_LWCVector4;
        default: return f;
    }
}


// 获取类型的分量数
inline int GetComponentCount(const EValueType& t){
    switch(t){
        case MCT_Float: case MCT_Float1: case MCT_LWCScalar:
        case MCT_UInt1: case MCT_Bool: case MCT_StaticBool: return 1;
        case MCT_Float2: case MCT_LWCVector2: case MCT_UInt2: return 2;
        case MCT_Float3: case MCT_LWCVector3: case MCT_UInt3: return 3;
        case MCT_Float4: case MCT_LWCVector4: case MCT_UInt4: return 4;
        default: return 0;
    }
}


// 类型可读名（报错信息用；对照 UE DescribeType）
inline const char* DescribeType(EValueType type){
    switch (type)
    {
    case MCT_Float: case MCT_Float1: return "Float";
    case MCT_Float2:   return "Float2";
    case MCT_Float3:   return "Float3";
    case MCT_Float4:   return "Float4";
    case MCT_LWCScalar:  return "LWCScalar";
    case MCT_LWCVector2: return "LWCVector2";
    case MCT_LWCVector3: return "LWCVector3";
    case MCT_LWCVector4: return "LWCVector4";
    case MCT_LWCMatrix:  return "LWCMatrix";
    case MCT_Float3x3: return "Float3x3";
    case MCT_Float4x4: return "Float4x4";
    case MCT_UInt1: return "UInt1";
    case MCT_UInt2: return "UInt2";
    case MCT_UInt3: return "UInt3";
    case MCT_UInt4: return "UInt4";
    case MCT_Bool: return "Bool";
    case MCT_StaticBool: return "StaticBool";
    case MCT_Execution: return "Execution";
    case MCT_Texture2D:      return "Texture2D";
    case MCT_TextureCube:    return "TextureCube";
    case MCT_Texture2DArray: return "Texture2DArray";
    case MCT_TextureCubeArray: return "TextureCubeArray";
    case MCT_VolumeTexture:  return "VolumeTexture";
    case MCT_TextureExternal:return "TextureExternal";
    case MCT_TextureVirtual: return "TextureVirtual";
    case MCT_SparseVolumeTexture: return "SparseVolumeTexture";
    case MCT_VTPageTableResult: return "VTPageTableResult";
    case MCT_Unexposed: return "Unexposed";
    case MCT_MaterialAttributes: return "MaterialAttributes";
    case MCT_ShadingModel:       return "ShadingModel";
    case MCT_Substrate:          return "Substrate";
    default: return "Unknown";
    }
}