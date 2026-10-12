#pragma once
#include "Compiler/Public/CodeChunk.h"

const char* FHLSLMaterialTranslator::HLSLTypeString(EMaterialValueType Type) const
{
	switch (Type)
	{
	case MCT_Float1:				return "MaterialFloat";
	case MCT_Float2:				return "MaterialFloat2";
	case MCT_Float3:				return "MaterialFloat3";
	case MCT_Float4:				return "MaterialFloat4";
	case MCT_Float:					return "MaterialFloat";
	case MCT_Texture2D:				return "Texture2D";
	case MCT_TextureCube:			return "TextureCube";
	case MCT_Texture2DArray:		return "Texture2DArray";
	case MCT_TextureCubeArray:		return "TextureCubeArray";
	case MCT_VolumeTexture:			return "Texture3D";
	case MCT_StaticBool:			return "static bool";
	case MCT_Bool:					return "bool";
	case MCT_MaterialAttributes:	return "FMaterialAttributes";
	case MCT_TextureExternal:		return "TextureExternal";
	case MCT_TextureVirtual:		return "TextureVirtual";
	case MCT_SparseVolumeTexture:	return "FSparseVolumeTexture";
	case MCT_VTPageTableResult:		return "VTPageTableResult";
	case MCT_ShadingModel:			return "uint";
	case MCT_UInt:					return "uint";
	case MCT_UInt1:					return "uint";
	case MCT_UInt2:					return "uint2";
	case MCT_UInt3:					return "uint3";
	case MCT_UInt4:					return "uint4";
	case MCT_Substrate:				return "FSubstrateData";
	case MCT_LWCScalar:				return "FWSScalar";
	case MCT_LWCVector2:			return "FWSVector2";
	case MCT_LWCVector3:			return "FWSVector3";
	case MCT_LWCVector4:			return "FWSVector4";
	case MCT_TextureCollection:		return "FResourceCollection";
	case MCT_TextureMeshPaint:		return "TextureMeshPaint";
	case MCT_TextureMaterialCache:	return "TextureMaterialCache";
	case MCT_MaterialCacheABuffer:	return "MaterialCacheABufferUntyped";
	default:						return "unknown";
	};
}


int32_t FHLSLMaterialTranslator::AddCodeChunkInner(uint64_t hash, const char* formatted_type, const char* formatted_code, EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined) {

}

int32_t FHLSLMaterialTranslator::AddCodeChunkInner(EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined, const char* format, ...) {
	FStringBuilderBase builder(256);
	va_list args;
	va_start(args, format);
	const char* formatted_code = builder.AppendV(format, args).GetData();
	EMaterialValueType effective_type = EMaterialValueType(t & ~MCT_TextureCollection);
	const uint64_t hash = HashString(formatted_code);

	return AddCodeChunkInner(hash, HLSLTypeString(effective_type), formatted_code, t, derivative_status, in_lined);
}