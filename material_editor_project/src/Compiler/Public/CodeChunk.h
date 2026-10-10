#pragma once
#include "MaterialTypes/Public/ValueType.h"
#include "Core/Public/MathTypes.h"   // Vec2/3/4（constant_value variant 用）
#include "Compiler/Public/HLSLMaterialDerivativeAutogen.h"
#include "Compiler/Public/MaterialShared.h"
#include "Expression/Public/UniformExpression.h"
#include "MaterialTypes/Public/MiscDefines.h"
#include "Core/Public/Logger.h"
#include <string>
#include <vector>
#include <cstdint>
#include <variant>

struct FShaderCodeChunk{
    uint64_t hash = 0;
    uint64_t material_attribute_mask = 0;  // MaterialAttributes 引脚的属性位掩码
    std::string definition_finite;  // :97  定义串·有限差分版（UE DefinitionFinite）
    std::string definition_analytic;  // :101 定义串·解析导数版（UE DefinitionAnalytic
    std::string symbol_name;  // 局部变量名
    Ref<FMaterialUniformExpression> uniform_expression;  // 表达式树指针
    std::vector<int32_t> scoped_chunks;  // 作用域挂在本块下的子块
    std::vector<int32_t> referenced_code_chunks;  // 以来的chunk索引
    EMaterialValueType type_ = MCT_Unknown;
    int32_t declared_scope_index = -1;  // 声明所在的作用域
    int32_t used_scope_index = -1;  // 使用所在作用域
    int32_t scope_level = 0;  // 作用域嵌套层级
    bool b_inline = false;  // 内联
    bool b_intermediate = false;  // 中间块
    EDerivativeStatus derivative_status = EDerivativeStatus::NotAware;

    std::string& AtDefinition(ECompiledPartialDerivativeVariation variation){
        ME_CHECK(variation == CompiledPDV_FiniteDifferences || variation==CompiledPDV_Analytic);
        return variation == CompiledPDV_FiniteDifferences? definition_finite:definition_analytic;
    }

    const std::string& AtDefinition(ECompiledPartialDerivativeVariation variation) const{
        ME_CHECK(variation == CompiledPDV_FiniteDifferences || variation==CompiledPDV_Analytic);
        return variation == CompiledPDV_FiniteDifferences? definition_finite:definition_analytic;
    }

    FShaderCodeChunk()=default;

    // 纯代码块构造（无表达式树）。对照 UE :140
    FShaderCodeChunk(uint64_t in_hash, const char* in_definition_finite, const char* in_definition_analytic,
                     const std::string& in_symbol_name, EMaterialValueType in_type,
                     EDerivativeStatus in_derivative_status, bool b_in_inline)
        : hash(in_hash),
          material_attribute_mask(0u),
          definition_finite(in_definition_finite),
          definition_analytic(in_definition_analytic),
          symbol_name(in_symbol_name),
          uniform_expression(nullptr),
          type_(in_type),
          declared_scope_index(INDEX_NONE),
          used_scope_index(INDEX_NONE),
          scope_level(0),
          b_inline(b_in_inline),
          b_intermediate(false),
          derivative_status(in_derivative_status)
    {}

    // 带表达式树的构造。对照 UE :160
    FShaderCodeChunk(uint64_t in_hash, FMaterialUniformExpression* in_uniform_expression,
                     const char* in_definition_finite, const char* in_definition_analytic,
                     EMaterialValueType in_type, EDerivativeStatus in_derivative_status)
        : hash(in_hash),
          material_attribute_mask(0u),
          definition_finite(in_definition_finite),
          definition_analytic(in_definition_analytic),
          uniform_expression(in_uniform_expression),
          type_(in_type),
          declared_scope_index(INDEX_NONE),
          used_scope_index(INDEX_NONE),
          scope_level(0),
          b_inline(false),
          b_intermediate(false),
          derivative_status(in_derivative_status)
    {}

};

class FHLSLMaterialTranslator: public FMaterialCompiler{

public:
    /**
     * Tests if a type is a valid argument to a variadic function, e.g. printf.
     */
    template <typename T>
    struct TIsValidVariadicFunctionArg
    {
    private:
        using DecayedT = std::decay_t<T>;

    public:
        static constexpr bool Value =
            std::is_enum_v      <DecayedT> ||
            std::is_arithmetic_v<DecayedT> ||
            std::is_pointer_v   <DecayedT> ||
            std::is_same_v      <DecayedT, TYPE_OF_NULLPTR>;
    };

protected:
    /** Adds an already formatted inline or referenced code chunk */
	int32_t AddCodeChunkInner(uint64_t hash, const char* formatted_type, const char* formatted_code, EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined);
	int32_t AddCodeChunkInner(uint64_t Hash, const char* formatted_code, EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined);

protected:
    /** 
	* Constructs the formatted code chunk and creates a new local variable definition from it. 
	* This should be used over AddInlinedCodeChunk when the code chunk adds actual instructions, and especially when calling a function.
	* Creating local variables instead of inlining simplifies the generated code and reduces redundant expression chains,
	* Making compiles faster and enabling the shader optimizer to do a better job.
	*/
    int32_t AddCodeChunkInner(EMaterialValueType t, const char* formated_type, EDerivativeStatus derivative_status, bool in_lined, const char* format, ...);
    int32_t AddCodeChunkInner(EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined, const char* format, ...);

    template<typename... types>
    int32_t AddCodeChunk(EMaterialValueType t, const char* format, types... args){
        ME_CHECK(TIsValidVariadicFunctionArg<types>::Value&&...);
        return AddCodeChunkInner(t, EDerivativeStatus::NotAware, false, format, args...);
    }

};