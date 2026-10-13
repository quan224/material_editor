#pragma once
#include "Compiler/Public/CodeChunk.h"

#define HLSL_LINE_TERMINATOR "\n"

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
std::string FHLSLMaterialTranslator::CreateSymbolName(const char* symbol_name_hint) {
	return symbol_name_hint + std::to_string(next_symbol_index++);
}

void FHLSLMaterialTranslator::AddCodeChunkToCurrentScope(int32_t chunk_index) {
	if (chunk_index != INDEX_NONE && scope_stack.size() > 0) {
		const int32_t current_scope_index = scope_stack.size();
		AddCodeChunkToScope(chunk_index, current_scope_index);  // 需要先记录下这里的流程以及AddCodeChunkToScope实在做什么

	}
}

int32_t FHLSLMaterialTranslator::AddCodeChunkInner(uint64_t hash, const char* formatted_type, const char* formatted_code, EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined) {
	if (t == MCT_Unknown) {
		return INDEX_NONE;
	}
	const bool b_from_texture_collection = (t & MCT_TextureCollection) != 0;
	int32_t code_index = INDEX_NONE;
	if (t == MCT_VoidStatement) {
		code_index = current_scope_chunks.size();
		const std::string statement = std::string(formatted_code) + HLSL_LINE_TERMINATOR;
		current_scope_chunks.emplace_back(hash, statement.c_str(), statement.c_str(), "", t, derivative_status, true);
	}
	else if (in_lined) {
		code_index = current_scope_chunks.size();
		current_scope_chunks.emplace_back(hash, formatted_code, formatted_code, "", t, derivative_status, true);
	}
	else if ((t & (MCT_Float | MCT_LWCType | MCT_VTPageTableResult | MCT_Unexposed | MCT_UInt)) || t == MCT_ShadingModel || t == MCT_MaterialAttributes || t == MCT_Substrate || t == MCT_UInt || b_from_texture_collection || t == MCT_MaterialCacheABuffer) {

		for (int32_t i = 0; i < current_scope_chunks.size(); i++) {
			if (current_scope_chunks[i]->hash == hash) {
				code_index = i;
				break;
			}
			if (code_index == INDEX_NONE) {
				code_index = current_scope_chunks.size();
				const std::string symbol_name = CreateSymbolName("Local");
				const std::string local_variable_definition_finite = std::string(" ") + formatted_type + " " + symbol_name + " = " + formatted_code + ";" + HLSL_LINE_TERMINATOR;
				const std::string local_variable_definition_analytic = std::string("	") + formatted_type + " " + symbol_name + " = " + formatted_code + ";" + HLSL_LINE_TERMINATOR;
				current_scope_chunks.emplace_back(hash, local_variable_definition_finite.c_str(), local_variable_definition_analytic.c_str(), symbol_name.c_str(), t, derivative_status, false);
			}
		}
	}
	else {
		if (t & MCT_Texture) {
			ME_LOG_ERROR("Operation not supported on a Texture");
			return INDEX_NONE;
		}
		else if (t == MCT_StaticBool)
		{
			ME_LOG_ERROR("Operation not supported on a Static Bool");
			return INDEX_NONE;
		}
		else
		{
			ME_LOG_ERROR("Operation not supported for type %s", DescribeType(t));
			return INDEX_NONE;
		}
	}
	AddCodeChunkToCurrentScope(code_index);
	return code_index;
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