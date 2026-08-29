#pragma once
#include "MaterialGraph/Public/Types.h"
#include <cstdint>


class TypeSystem{
public:

    static EValueType GetArithmeticResultType(EValueType a, EValueType b){
        if(!IsPrimitiveType(a) || !IsPrimitiveType(b)){
            ME_LOG_ERROR("Attempting to perform arithmetic on non-primitive types: %s %s",DescribeType(a), DescribeType(b));
            return MCT_Unknown;
        }
        if (a == b){
            return a;
        }

        if (a&MCT_LWCType || b&MCT_LWCType){
            EValueType al = ToLWCType(a);
            EValueType bl = ToLWCType(b);
            if (al == bl){
                return al;
            }
            if(al==MCT_LWCScalar && IsFloatNumericType(bl)){
                return bl;
            }
            if(bl==MCT_LWCScalar && IsFloatNumericType(al)){
                return al;
            }

        }
        if(a==MCT_Float || a==MCT_Float1){
            return b;
        }
        if(b==MCT_Float || b==MCT_Float1){
            return a;
        }

        ME_LOG_ERROR("Arithmetic between types %s and %s are undefined", DescribeType(a), DescribeType(b));
        return MCT_Unknown; 
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

};
