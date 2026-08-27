#pragma once
#include "MaterialGraph/Public/Types.h"
#include <cstdint>

// 编译器专属的类型算法（依赖 MaterialGraph 的基础类型定义）
// - GetArithmeticResultType：两个类型如何运算出第三个（对齐 UE .cpp:4221 的推导规则）
// - ToLWCType：float → LWC 的类型级提升（跨族算术用）
// - ToHLSLType：类型 → HLSL 关键字（HLSL 是编译器概念，不应出现在 Types.h）
// - TypeName：类型可读名（EmitError 报错信息用；对照 UE DescribeType .cpp:3141）
//
// 注：GetComponentCount 属于"类型本身的属性查询"，留在 Types.h 里作为 free function。
//     本类不再重复定义，调用方请直接使用 ::GetComponentCount。
class TypeSystem{
public:
    // 算术结果类型。对照 UE .cpp:4221：
    // ①非 primitive（矩阵/纹理/打包值）→ 错误；②同类型 → 原类型；
    // ③任一方是标量 → 另一方（float 标量和 LWC 标量各自提升同族）；
    // ④float×LWC 跨族 → LWC 侧结果（UE 语义：混合时先把 float 提升为 LWC）；
    // ⑤其余（Float2 vs Float3 等）→ 错误 + Unknown
    static EValueType GetArithmeticResultType(EValueType a, EValueType b){
        // ① 对象类型/矩阵/未知不能算术
        //   纹理全族靠 MCT_Texture 掩码一网打尽；打包三件套在此拦截
        if (a & MCT_Texture || b & MCT_Texture){
            return MCT_Unknown;
        }
        if (a == MCT_MaterialAttributes || b == MCT_MaterialAttributes
            || a == MCT_ShadingModel || b == MCT_ShadingModel
            || a == MCT_Substrate || b == MCT_Substrate){
            return MCT_Unknown;
        }
        if (a == MCT_Unknown || b == MCT_Unknown){
            return MCT_Unknown;
        }
        if (GetComponentCount(a) == 0 || GetComponentCount(b) == 0){
            return MCT_Unknown;  // 矩阵/LWCMatrix
        }

        if (a == b) return a;                              // ② 同类型
        bool aScalar = (a == MCT_Float || a == MCT_Float1);
        bool bScalar = (b == MCT_Float || b == MCT_Float1);
        if (aScalar && !bScalar) return b;                 // ③ float 标量提升
        if (bScalar && !aScalar) return a;
        if (a == MCT_LWCScalar && (b & MCT_LWCType)) return b;  // LWC 标量提升同族
        if (b == MCT_LWCScalar && (a & MCT_LWCType)) return a;
        // ④ 跨族 float × LWC：结果取 LWC 侧（混合运算先把 float 提升为 LWC）
        if ((a & MCT_LWCType) && (b & MCT_Float)) return ToLWCType(b);
        if ((b & MCT_LWCType) && (a & MCT_Float)) return ToLWCType(a);
        return MCT_Unknown;                                // ⑤ 不兼容
        // 注意：返回 Unknown 时调用方负责 EmitError（要带 node/pin 定位），
        // TypeSystem 是无状态工具类，不该持有错误上下文
    }

    // float → LWC 的类型级提升（算术跨族时用）
    static EValueType ToLWCType(EValueType t){
        switch (t)
        {
        case MCT_Float: case MCT_Float1: return MCT_LWCScalar;
        case MCT_Float2: return MCT_LWCVector2;
        case MCT_Float3: return MCT_LWCVector3;
        case MCT_Float4: return MCT_LWCVector4;
        default: return t;
        }
    }

    // 类型到 HLSL 类型名（对照 UE .cpp:3141-3223 的类型名表）
    static const char* ToHLSLType(EValueType type){
        switch (type)
        {
        case MCT_Float: case MCT_Float1: return "float";
        case MCT_Float2:   return "float2";
        case MCT_Float3:   return "float3";
        case MCT_Float4:   return "float4";
        // LWC：HLSL 里是 double 族
        case MCT_LWCScalar:  return "double";
        case MCT_LWCVector2: return "double2";
        case MCT_LWCVector3: return "double3";
        case MCT_LWCVector4: return "double4";
        case MCT_LWCMatrix:  return "double4x4";
        case MCT_Float3x3: return "float3x3";
        case MCT_Float4x4: return "float4x4";
        // 无符号整数
        case MCT_UInt1: return "uint";
        case MCT_UInt2: return "uint2";
        case MCT_UInt3: return "uint3";
        case MCT_UInt4: return "uint4";
        case MCT_Bool: return "bool";
        // 纹理声明类型
        case MCT_Texture2D:      return "Texture2D";
        case MCT_TextureCube:    return "TextureCube";
        case MCT_Texture2DArray: return "Texture2DArray";
        case MCT_TextureCubeArray: return "TextureCubeArray";
        case MCT_VolumeTexture:  return "Texture3D";
        case MCT_TextureExternal: return "TextureExternal";
        case MCT_TextureVirtual:  return "TextureVirtual";
        case MCT_SparseVolumeTexture: return "SparseVolumeTexture";
        // 打包/模型类型
        case MCT_MaterialAttributes: return "FMaterialAttributes";
        case MCT_ShadingModel:       return "uint";
        case MCT_Substrate:          return "FSubstrateData";
        default: return "float";  // 兜底，避免生成非法 HLSL
        }
    }

    // 类型可读名（报错信息用；对照 UE DescribeType）
    static const char* TypeName(EValueType type){
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
};
