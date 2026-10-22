#pragma once
#include "Compiler/Public/HLSLMaterialTranslator.h"
#include "Core/Public/StringBuilder.h"

#define HLSL_LINE_TERMINATOR "\n"

// =================== 工具 ===================

static int32_t GAnalyticDerivEnabled = 1;

// static FAutoConsoleVariableRef CVarAnalyticDerivEnabled(
// 	TEXT("r.MaterialEditor.AnalyticDeriv"),
// 	GAnalyticDerivEnabled,
// 	TEXT("Enable analytic derivative code generation.")
// 	);

static inline bool IsAnalyticDerivEnabled(){
	return GAnalyticDerivEnabled != 0;
}

EMaterialCastFlags GetForceCastFlags(uint32_t force_cast_flags){
	EMaterialCastFlags cast_flags = EMaterialCastFlags::AllowTruncate|EMaterialCastFlags::AllowAppendZeroes;
	if ((force_cast_flags & MFCF_ReplicateValue) || !(force_cast_flags & MFCF_ExactMatch))
	{
		// Replicate scalar if requested, or if we don't require an exact match (this can happen when force-casting to/from LWC
		// The only way we *don't* replicate scalar is requesting an exact match without the ReplicateValue flag
		// TODO - My guess is that case probably isn't relevant, and we should just always replicate scalar on cast, but trying to preserve behavior for now
		cast_flags |= EMaterialCastFlags::ReplicateScalar;
	}
	return cast_flags;
}

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

const FShaderCodeChunk& FHLSLMaterialTranslator::AtParameterCodeChunk(int32_t index) const{
	ME_CHECK(index != INDEX_NONE);
	ME_CHECK(index>=0 && index<current_scope_chunks.size());
	const FShaderCodeChunk& code_chunk = *(current_scope_chunks[index]);
	return code_chunk;
}

FDerivInfo FHLSLMaterialTranslator::GetDerivInfo(int32_t index, bool b_allow_non_float){
	const FShaderCodeChunk& code_chunk = AtParameterCodeChunk(index);
	if(!b_allow_non_float && GetDerivType(code_chunk.type_, true) == EDerivativeType::None){
		ME_LOG_ERROR("Internal error: unexpected non-numeric derivative argument type found (%s).", DescribeType(code_chunk.type_));
		return FDerivInfo{code_chunk.type_, EDerivativeType::None, EDerivativeStatus::NotValid};
	}
	return FDerivInfo{code_chunk.type_, GetDerivType(code_chunk.type_, b_allow_non_float), code_chunk.derivative_status};
}

// =================== 查询（基类接口 override）====================

EMaterialValueType FHLSLMaterialTranslator::GetType(int32_t code){
	if(code != INDEX_NONE){
		return GetParameterType(code);
	}
	else{
		return MCT_Unknown;
	}
}

EMaterialValueType FHLSLMaterialTranslator::GetParameterType(int32_t index) const{
	ME_CHECK(index>=0 && index<current_scope_chunks.size());
	return current_scope_chunks[index]->type_;
}

FMaterialUniformExpression* FHLSLMaterialTranslator::GetParameterUniformExpression(int32_t index) const{
	ME_CHECK(index >= 0 && index<current_scope_chunks.size());
	return current_scope_chunks[index]->uniform_expression.get();
}

// =================== 类型转换 ===================

// LWC 函数使用计数。对照 UE .cpp:3293 原文：previous frame 编译不重复计数，按频率分 VS/PS/CS 三组
void FHLSLMaterialTranslator::AddLWCFuncUsage(ELWCFunctionKind kind, uint32_t count){
	if(IsCurrentlyCompilingForPreviousFrame()){
		// Ignore non-actionable cases
		return;
	}
	if(shader_frequency == SF_Pixel){
		compile_result.estimated_lwc_func_usages_ps[(int)kind] += count;
	}
	else if(shader_frequency == SF_Compute){
		compile_result.estimated_lwc_func_usages_cs[(int)kind] += count;
	}
	else{
		compile_result.estimated_lwc_func_usages_vs[(int)kind] += count;
	}
}

std::string FHLSLMaterialTranslator::CastValue(const std::string& code, EMaterialValueType source_type, EMaterialValueType dest_type, EMaterialCastFlags flags){
	if(source_type == dest_type){
		return code;
	}
	const bool b_allow_truncate = EnumHasAnyFlags(flags, EMaterialCastFlags::AllowTruncate);
	const bool b_allow_append_zeroes = EnumHasAnyFlags(flags, EMaterialCastFlags::AllowAppendZeroes);
	const bool b_allow_replicate_scalar = EnumHasAnyFlags(flags, EMaterialCastFlags::ReplicateScalar);
	const bool b_allow_integer = EnumHasAnyFlags(flags, EMaterialCastFlags::AllowInteger);

	const EMaterialValueType allowed_types = EMaterialValueType(MCT_Float | MCT_LWCType | (b_allow_integer ? MCT_UInt : 0));
	if(IsMaterialValueType(source_type, allowed_types)&&IsMaterialValueType(dest_type, allowed_types)){
		const uint32_t num_source_components = GetNumComponents(source_type);
		const uint32_t num_dest_components = GetNumComponents(dest_type);
		const bool b_replicate_scalar = b_allow_replicate_scalar && (num_source_components==1);
		if(!b_replicate_scalar&&!b_allow_append_zeroes&&num_dest_components>num_source_components){
			ME_LOG_ERROR("Cannot cast from smaller type %s to larger type %s.", DescribeType(source_type), DescribeType(dest_type));
			return "";
		}
		if(!b_replicate_scalar&&!b_allow_truncate&&num_dest_components<num_source_components){
			ME_LOG_ERROR("Cannot cast from larger type %s to smaller type %s.", DescribeType(source_type), DescribeType(dest_type));
			return "";
		}
		const bool b_is_lwc = IsLWCType(dest_type);
		if(b_is_lwc!=IsLWCType(source_type)){
			if(b_is_lwc){
				// float->LWC
				AddLWCFuncUsage(ELWCFunctionKind::Promote);
				return FStringBuilderBase().Appendf("WSPromote(%s)", CastValue(code, source_type, MakeNonLWCType(dest_type), flags)).GetData();
			}
			else{
				// LWC->float
				AddLWCFuncUsage(ELWCFunctionKind::Demote);
				return CastValue(FStringBuilderBase().Appendf("WSDemote(%s)", code).GetData(), MakeNonLWCType(source_type), dest_type, flags);
			}
		}

		std::string result;
		uint32_t num_components = 0u;
		bool b_need_closing_paren = false;
		if(b_is_lwc){
			AddLWCFuncUsage(ELWCFunctionKind::Constructor);
			result = "MakeWSVector(";
			b_need_closing_paren = true;
		}
		else{
			if(num_source_components == num_dest_components){
				num_components = num_dest_components;
				result+=code;
			}
			else if(b_replicate_scalar){
				num_components = num_dest_components;
				result+=FStringBuilderBase().Appendf("((%s)%s)", HLSLTypeString(dest_type), code ).GetData();
			}
			else{
				num_components = std::min(num_source_components, num_dest_components);
				if(num_components < num_dest_components){
					result = (std::string)HLSLTypeString(dest_type)+"(";
					b_need_closing_paren = true;
				}
				if(num_components == num_source_components){
					result+=code;
				}
				else{
					static const std::string mask[]={"<ERROR>", "x", "xy", "xyz", "xyzw"};
					ME_CHECK(num_components<=4);
					result+=FStringBuilderBase().Appendf("%s.%s", code, mask[num_components]).GetData();
				}
			}
		}
		if(b_need_closing_paren){
			for(uint32_t component_index=num_components; component_index<num_dest_components;++component_index){
				if(component_index>0u){
					result+=",";
				}
				if(b_is_lwc){
					if(!b_replicate_scalar&&component_index>=num_source_components){
						ME_CHECK(b_allow_append_zeroes);
						AddLWCFuncUsage(ELWCFunctionKind::Promote);
						result+="WSPromote(0.0f)";
					}
					else{
						result+=FStringBuilderBase().Appendf("WSGetComponent(%s, %d)", code, b_replicate_scalar?0:component_index).GetData();
					}
				}
				else{
					ME_CHECK(b_allow_append_zeroes);
					ME_CHECK(!b_replicate_scalar);
					ME_CHECK(component_index>=num_source_components);
					result+="0.0f";
				}
			}
			num_components = num_dest_components;
			result += ")";
		}
		ME_CHECK(num_components==num_dest_components);
		return result;
	}
	if (source_type & MCT_TextureCollection && source_type != MCT_TextureCollection)
	{
		return CastValue(code, EMaterialValueType(source_type & ~MCT_TextureCollection), dest_type, flags);
	}
	ME_LOG_ERROR("Cannot cast between non-numeric types %s to %s.", DescribeType(source_type), DescribeType(dest_type));
	return "";
}

int32_t FHLSLMaterialTranslator::ForceCast(int32_t code, EMaterialValueType dest_type, uint32_t force_cast_flags){

	if(code == INDEX_NONE){
		return INDEX_NONE;
	}
	if(GetParameterUniformExpression(code) && !GetParameterUniformExpression(code)->IsConstant()){
		return ForceCast(AccessUniformExpression(code), dest_type, force_cast_flags);
	}

	const EMaterialValueType source_type = GetParameterType(code);
	if((force_cast_flags&MFCF_ExactMatch)?(source_type==dest_type):(source_type&dest_type)){
		return code;
	}
	else if(IsFloatNumericType(source_type)&&IsFloatNumericType(dest_type)){
		const FDerivInfo code_deriv_info = GetDerivInfo(code);
		EMaterialCastFlags cast_flags = GetForceCastFlags(force_cast_flags);
		std::string finite_code = CastValue(GetParameterCode(code), source_type, dest_type, cast_flags);
		if(IsAnalyticDerivEnabled() && IsDerivativeValid(code_deriv_info.derivative_status)){
			if(code_deriv_info.derivative_status == EDerivativeStatus::Valid){
				std::string deriv_string = *GetParameterCodeDeriv(code, CompiledPDV_Analytic);
				std::string ddx_code = CastValue(deriv_string+".Ddx", MakeNonLWCType(source_type), MakeNonLWCType(dest_type), cast_flags);
				std::string ddy_code = CastValue(deriv_string+".Ddy", MakeNonLWCType(source_type), MakeNonLWCType(dest_type), cast_flags);
				std::string analytic_code = derivative_autogen.ConstructDeriv(finite_code, ddx_code, ddy_code, GetDerivType(dest_type));
				return AddCodeChunkInnerDeriv(finite_code, analytic_code, dest_type, false, EDerivativeStatus::Valid);
			}
			else{
				return AddInlinedCodeChunkZeroDeriv(dest_type, finite_code.c_str());
			}
		}
		else{
			return AddInlinedCodeChunk(dest_type, finite_code.c_str());
		}
	}
	else if (IsNumericType(source_type)&&IsNumericType(dest_type)){
		EMaterialCastFlags cast_flags = GetForceCastFlags(force_cast_flags)|EMaterialCastFlags::AllowInteger;
		std::string finite_code = CastValue(GetParameterCode(code), source_type, dest_type, cast_flags);
		return AddInlinedCodeChunk(dest_type, finite_code.c_str());
	}
	else if((source_type & MCT_StaticBool)&&IsFloatNumericType(dest_type)){
		std::string static_bool_to_float = GetParameterCode(code) =="true"? "1.0f":"0.0f";
		static_bool_to_float = CastValue(static_bool_to_float, MCT_Float, dest_type, GetForceCastFlags(force_cast_flags));
		return AddInlinedCodeChunk(dest_type, static_bool_to_float.c_str());
	}
	else if ((source_type & (MCT_TextureVirtual|MCT_TextureMeshPaint|MCT_TextureMaterialCache)) && (dest_type & MCT_Texture2D))
	{
		return code;
	}
	else{
		ME_LOG_ERROR("Cannot force a cast between non-numeric types.");
		return INDEX_NONE;
	}


}

int32_t FHLSLMaterialTranslator::LWCCastIfNeccessary(EMaterialValueType result_type, int32_t result_code){
	if(!IsNumericType(result_type)){
		ME_LOG_ERROR("LWCCastIfNeccessary Cannot cast non numeric type");
		return INDEX_NONE;
	}
	EMaterialValueType current_type = GetType(result_code);
	if(current_type == result_type){
		return result_code;
	}

	EMaterialValueType result_non_lwc_t = MakeNonLWCType(result_type);
	EMaterialValueType current_non_lwc_t = MakeNonLWCType(current_type);
	if (result_non_lwc_t == current_non_lwc_t || result_non_lwc_t == MCT_Float || current_non_lwc_t == MCT_Float)
	{
		return ForceCast(result_code, result_type);
	}
	ME_LOG_ERROR("LWCCastIfNeccessary can only cast between LWC and Non LWC types.  Current result has a different number of components than requested type");
	return INDEX_NONE;
}

// =================== 表达式树（表达式块路径）====================

int32_t FHLSLMaterialTranslator::AccessUniformExpression(int32_t index){
	ME_CHECK(index>=0&&index<current_scope_chunks.size());
	const FShaderCodeChunk& code_chunk = *(current_scope_chunks[index]);
	ME_CHECK(code_chunk.uniform_expression && !code_chunk.uniform_expression->IsConstant());
	const bool b_is_lwc = IsLWCType(code_chunk.type_);
	return AddInlinedCodeChunkZeroDeriv(code_chunk.type_, "%s", code_chunk.definition_finite.c_str());
}

int32_t FHLSLMaterialTranslator::AddUniformExpression(FAddUniformExpressionScope& scope, FMaterialUniformExpression* uniform_expression, EMaterialValueType t, const char* format, ...) {
	FStringBuilderBase builder(256);
	va_list args;
	va_start(args, format);
	const char* formatted_code = builder.AppendV(format, args).GetData();
	va_end(args);
	int64_t hash = HashString(formatted_code);
	return AddUniformExpressionInner(hash, uniform_expression, t, formatted_code);

}

int32_t FHLSLMaterialTranslator::AddUniformExpressionInner(int64_t hash, FMaterialUniformExpression* uniform_expression, EMaterialValueType t, const char* formatted_code) {
	if (t == MCT_Unknown) {
		return INDEX_NONE;
	}

	ME_CHECK(uniform_expression);
	// Only a texture uniform expression can have MCT_Texture type
	//if ((t & MCT_Texture) && !uniform_expression->GetTextureUniformExpression()
	//	&& !uniform_expression->GetExternalTextureUniformExpression())
	//{
	//	ME_LOG_ERROR("Operation not supported on a Texture");
	//	return INDEX_NONE;
	//}

	//// External textures must have an external texture uniform expression
	//if ((t & MCT_TextureExternal) && !uniform_expression->GetExternalTextureUniformExpression())
	//{
	//	ME_LOG_ERROR("Operation not supported on an external texture");
	//	return INDEX_NONE;
	//}

	if (t == MCT_StaticBool)
	{
		ME_LOG_ERROR("Operation not supported on a Static Bool");
		return INDEX_NONE;
	}
	if (t == MCT_MaterialAttributes)
	{
		ME_LOG_ERROR("Operation not supported on a MaterialAttributes");
		return INDEX_NONE;
	}

	bool b_found_existing_expression = false;
	for (int32_t expression_index = 0; expression_index < uniform_expressions.size() && !b_found_existing_expression; expression_index++) {
		FMaterialUniformExpression* test_expression = (uniform_expressions[expression_index]->uniform_expression).get();
		ME_CHECK(test_expression);
		if (test_expression->IsIdentical(uniform_expression)) {
			b_found_existing_expression = true;
			ME_CHECK(t == uniform_expressions[expression_index]->type_);
			for (int32_t chunk_index = 0; chunk_index < current_scope_chunks.size(); chunk_index++) {
				FMaterialUniformExpression* other_expression = current_scope_chunks[chunk_index]->uniform_expression.get();
				if (other_expression && other_expression->IsIdentical(uniform_expression)) {
					delete uniform_expression;
					AddCodeChunkToCurrentScope(chunk_index);
					return chunk_index;
				}
			}
			delete uniform_expression;
			uniform_expression = test_expression;
			break;
		}
	}
	const int32_t return_index = current_scope_chunks.size();
	current_scope_chunks.push_back(allocator->New<FShaderCodeChunk>(hash, uniform_expression, formatted_code, formatted_code, t, EDerivativeStatus::Zero));
	if (!b_found_existing_expression) {
		uniform_expressions.push_back(current_scope_chunks[return_index]);
	}
	AddCodeChunkToCurrentScope(return_index);
	return return_index;
}

int32_t FHLSLMaterialTranslator::ConstResultValue(EMaterialValueType t, Vec4 constant_value){
	EMaterialValueType non_lwc_t = MakeNonLWCType(t);
	int32_t result;
	switch(non_lwc_t){
		case MCT_Float:
		case MCT_Float1:
			result = Constant(constant_value.r);
			break;
		case MCT_Float2:
			result = Constant2(constant_value.r, constant_value.g);
			break;
		case MCT_Float3:
			result = Constant3(constant_value.r, constant_value.g, constant_value.b);
			break;
		case MCT_Float4:
			result = Constant4(constant_value.r, constant_value.g, constant_value.b, constant_value.a);
			break;
		default:
			return INDEX_NONE;
	}
	return LWCCastIfNeccessary(t, result);
}

int32_t FHLSLMaterialTranslator::ConstResultValue(EMaterialValueType t, float constant_value){
	return ConstResultValue(t, Vec4(constant_value, constant_value, constant_value, constant_value));
}

// =================== 代码块管理（纯代码块路径）====================

std::string FHLSLMaterialTranslator::CreateSymbolName(const char* symbol_name_hint) {
	return symbol_name_hint + std::to_string(next_symbol_index++);
}

void FHLSLMaterialTranslator::AddCodeChunkToCurrentScope(int32_t chunk_index) {
	if (chunk_index != INDEX_NONE && scope_stack.size() > 0) {
		const int32_t current_scope_index = scope_stack.back();
		AddCodeChunkToScope(chunk_index, current_scope_index);
	}
}

void FHLSLMaterialTranslator::AddCodeChunkToScope(int32_t chunk_index, int32_t scope_index){
	if(chunk_index != INDEX_NONE){
		FShaderCodeChunk& current_scope = *current_scope_chunks[scope_index];
		FShaderCodeChunk& chunk = *current_scope_chunks[chunk_index];
		if(chunk.declared_scope_index == INDEX_NONE){
			// 如果没被声明过，那也不应该被使用
			ME_CHECK(chunk.used_scope_index==INDEX_NONE);
			chunk.declared_scope_index = scope_index;
			chunk.used_scope_index = scope_index;
			chunk.scope_level = current_scope.scope_level + 1;
		}
		// 满足全部使用者的“最浅可见层”结论
		else if(chunk.used_scope_index != scope_index){
			// Find the most derived scope that's shared by the current scope, and the scope this code was previously referenced from
			int32_t scope_index0 = scope_index;
			int32_t scope_index1 = chunk.used_scope_index;
			while(scope_index0 != scope_index1){
				const FShaderCodeChunk& scope0 = *current_scope_chunks[scope_index0];
				const FShaderCodeChunk& scope1 = *current_scope_chunks[scope_index1];
				if(scope0.scope_level > scope1.scope_level){
					ME_CHECK(scope0.used_scope_index != INDEX_NONE);
					scope_index0 = scope0.used_scope_index;
				}
				else{
					ME_CHECK(scope1.used_scope_index != INDEX_NONE);
					scope_index1 = scope1.used_scope_index;
				}
			}
			const FShaderCodeChunk& scope = *current_scope_chunks[scope_index0];
			chunk.used_scope_index = scope_index0;
			chunk.scope_level = scope.scope_level + 1;
		}
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
		current_scope_chunks.push_back(allocator->New<FShaderCodeChunk>(hash, statement.c_str(), statement.c_str(), "", t, derivative_status, true));
	}
	else if (in_lined) {
		code_index = current_scope_chunks.size();
		current_scope_chunks.push_back(allocator->New<FShaderCodeChunk>(hash, formatted_code, formatted_code, "", t, derivative_status, true));
	}
	else if ((t & (MCT_Float | MCT_LWCType | MCT_VTPageTableResult | MCT_Unexposed | MCT_UInt)) || t == MCT_ShadingModel || t == MCT_MaterialAttributes || t == MCT_Substrate || t == MCT_UInt || b_from_texture_collection || t == MCT_MaterialCacheABuffer) {

		// Check for existing
		for (int32_t i = 0; i < current_scope_chunks.size(); i++) {
			if (current_scope_chunks[i]->hash == hash) {
				code_index = i;
				break;
			}
		}

		if (code_index == INDEX_NONE) {
			code_index = current_scope_chunks.size();
			const std::string symbol_name = CreateSymbolName("Local");
			const std::string local_variable_definition_finite = std::string(" ") + formatted_type + " " + symbol_name + " = " + formatted_code + ";" + HLSL_LINE_TERMINATOR;
			const std::string local_variable_definition_analytic = std::string("	") + formatted_type + " " + symbol_name + " = " + formatted_code + ";" + HLSL_LINE_TERMINATOR;
			current_scope_chunks.push_back(allocator->New<FShaderCodeChunk>(hash, local_variable_definition_finite.c_str(), local_variable_definition_analytic.c_str(), symbol_name.c_str(), t, derivative_status, false));
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

int32_t FHLSLMaterialTranslator::AddCodeChunkInner(uint64_t Hash, const char* formatted_code, EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined) {
	const EMaterialValueType effective_type = EMaterialValueType(t & ~MCT_TextureCollection);
	return AddCodeChunkInner(Hash, HLSLTypeString(effective_type), formatted_code, t, derivative_status, in_lined);
}

int32_t FHLSLMaterialTranslator::AddCodeChunkInner(EMaterialValueType t, const char* formated_type, EDerivativeStatus derivative_status, bool in_lined, const char* format, ...) {
	FStringBuilderBase builder(256);
	va_list args;
	va_start(args, format);
	const char* formatted_code = builder.AppendV(format, args).GetData();
	EMaterialValueType effective_type = EMaterialValueType(t & ~MCT_TextureCollection);
	const uint64_t hash = HashString(formatted_code);

	return AddCodeChunkInner(hash, HLSLTypeString(effective_type), formatted_code, t, derivative_status, in_lined);
}

// =================== 算子（⑥，基类接口 override）====================

int32_t FHLSLMaterialTranslator::Constant(float x){
	FAddUniformExpressionScope scope(this);
	return AddUniformExpression(scope, new FMaterialUniformExpressionConstant(Vec4(x,x,x,x), MCT_Float), MCT_Float, "%0.8ff", x);
}

int32_t FHLSLMaterialTranslator::Constant2(float x, float y){
	FAddUniformExpressionScope scope(this);
	return AddUniformExpression(scope, new FMaterialUniformExpressionConstant(Vec4(x,y,0,0), MCT_Float2), MCT_Float2, "MaterialFloat2(%0.8ff, %0.8ff)", x, y);
}

int32_t FHLSLMaterialTranslator::Constant3(float x, float y, float z){
	FAddUniformExpressionScope scope(this);
	return AddUniformExpression(scope, new FMaterialUniformExpressionConstant(Vec4(x,y,z,0), MCT_Float3), MCT_Float3, "MaterialFloat3(%0.8ff, %0.8ff, %0.8ff)", x, y, z);
}

int32_t FHLSLMaterialTranslator::Constant4(float x, float y, float z, float w){
	FAddUniformExpressionScope scope(this);
	return AddUniformExpression(scope, new FMaterialUniformExpressionConstant(Vec4(x,y,z,w), MCT_Float4), MCT_Float4, "MaterialFloat4(%0.8ff, %0.8ff, %0.8ff, %0.8ff)", x, y, z, w);
}
