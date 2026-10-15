#pragma once
#include "MaterialTypes/Public/ValueType.h"
#include "Core/Public/MathTypes.h"
#include "Compiler/Public/HLSLMaterialDerivativeAutogen.h"
#include "Core/Public/MemStack.h"
#include "Compiler/Public/MaterialCompiler.h"
#include "Compiler/Public/CodeChunk.h"
#include "Expression/Public/UniformExpression.h"
#include "MaterialTypes/Public/MiscDefines.h"
#include "Core/Public/Logger.h"
#include <string>
#include <vector>
#include <cstdint>
#include <type_traits>


class FAddUniformExpressionScope;

class FHLSLMaterialTranslator: public FMaterialCompiler{

public:
    // MemStack 由外部传入，生命周期须罩住整个编译过程（编译完 Mark/Pop 批量回收 chunk）
    explicit FHLSLMaterialTranslator(MemStack& in_allocator) : allocator(&in_allocator) {}

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
            std::is_same_v      <DecayedT, std::nullptr_t>;
    };

    const char* HLSLTypeString(EMaterialValueType Type) const;

protected:
    EMaterialValueType GetType(int32_t code) override;
    EMaterialValueType GetParameterType(int32_t index) const override;

    int32_t LWCCastIfNeccessary(EMaterialValueType result_type, int32_t result_code);
    std::string CreateSymbolName(const char* symbol_name_hint);
    void AddCodeChunkToCurrentScope(int32_t chunk_index);
    void AddCodeChunkToScope(int32_t chunk_index, int32_t scope_index);

    /** Adds an already formatted inline or referenced code chunk */
	int32_t AddCodeChunkInner(uint64_t hash, const char* formatted_type, const char* formatted_code, EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined);
	int32_t AddCodeChunkInner(uint64_t Hash, const char* formatted_code, EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined);

	// 类型转换。对照 UE ForceCast（MaterialCompiler.h:238，接口属于基类——基类重整时上移）
	int32_t ForceCast(int32_t code, EMaterialValueType dest_type, uint32_t force_cast_flags = 0);

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

    template<typename... types>
    int32_t AddInlinedCodeChunk(EMaterialValueType t, const char* format, types... args) {
        ME_CHECK(TIsValidVariadicFunctionArg<types>::Value&&...);
        return AddCodeChunkInner(t, EDerivativeStatus::NotAware, true, format, args...);
    }

    int32_t AddUniformExpression(FAddUniformExpressionScope& scope, FMaterialUniformExpression* uniform_expression, EMaterialValueType t, const char* format, ...);
    int32_t AddUniformExpressionInner(int64_t hash, FMaterialUniformExpression* uniform_expression, EMaterialValueType t, const char* formatted_code);

    int32_t ConstResultValue(EMaterialValueType t, Vec4 constant_value);
    int32_t ConstResultValue(EMaterialValueType t, float constant_value);

    



    // 表达式
    int32_t Constant(float x) override;
    int32_t Constant2(float x, float y) override;
    int32_t Constant3(float x, float y, float z) override;
    int32_t Constant4(float x, float y, float z, float w) override;

    MemStack* allocator = nullptr;                      // chunk 对象住在 MemStack 上（对照 UE FMemStackBase* Allocator）
    std::vector<FShaderCodeChunk*> current_scope_chunks;  // 地址清单（对象本体在 MemStack）
    std::vector<FShaderCodeChunk*> uniform_expressions;  // 表达式
    std::vector<int32_t> scope_stack;  // 作用域索引记录
    int32_t next_symbol_index;  // 变量名
    int32_t adding_uniform_expression;

    friend class FAddUniformExpressionScope;
};

class FAddUniformExpressionScope {
public:
    FAddUniformExpressionScope(FHLSLMaterialTranslator* in_translator) :translator(in_translator) {
        translator->adding_uniform_expression++;
    }
    ~FAddUniformExpressionScope() {
        translator->adding_uniform_expression--;
        ME_CHECK(translator->adding_uniform_expression >= 0);
    }


private:
    FHLSLMaterialTranslator* translator;
};