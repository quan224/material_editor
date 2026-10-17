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

class FHLSLMaterialTranslator : public FMaterialCompiler {

public:
    // =================== 构造 ===================
    // MemStack 由外部传入，生命周期须罩住整个编译过程（编译完 Mark/Pop 批量回收 chunk）
    explicit FHLSLMaterialTranslator(MemStack& in_allocator) : allocator(&in_allocator) {}

    // =================== 工具 ===================
    // 变参类型白名单（printf 风格函数的编译期安检）
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

    // 类型 → HLSL 类型名（"MaterialFloat3" / "Texture2D" / ...）
    const char* HLSLTypeString(EMaterialValueType Type) const;

    // 导数信息查询（双轨用）
    FDerivInfo GetDerivInfo(int32_t index, bool b_allow_non_float = false);

    // =================== 查询（基类接口 override）====================
    EMaterialValueType GetType(int32_t code) override;
    EMaterialValueType GetParameterType(int32_t index) const override;
    FMaterialUniformExpression* GetParameterUniformExpression(int32_t index) const override;

    // =================== 类型转换 ===================
    // 对照 UE ForceCast（MaterialCompiler.h:238，接口属于基类——基类重整时上移）
    int32_t ForceCast(int32_t code, EMaterialValueType dest_type, uint32_t force_cast_flags = 0);
    // LWC 结果与当前类型不符时截断/提升（对照 UE LWCCastIfNeccessary）
    int32_t LWCCastIfNeccessary(EMaterialValueType result_type, int32_t result_code);

    // =================== 表达式树（表达式块路径）====================
    // 把挂着表达式树的 chunk 兑换成能写进 HLSL 的代码
    int32_t AccessUniformExpression(int32_t index);
    // 挂树入库（双层去重）
    int32_t AddUniformExpression(FAddUniformExpressionScope& scope, FMaterialUniformExpression* uniform_expression, EMaterialValueType t, const char* format, ...);
    int32_t AddUniformExpressionInner(int64_t hash, FMaterialUniformExpression* uniform_expression, EMaterialValueType t, const char* formatted_code);
    // 折叠结果 → 常量树入库（折叠算子的共同出口）
    int32_t ConstResultValue(EMaterialValueType t, Vec4 constant_value);
    int32_t ConstResultValue(EMaterialValueType t, float constant_value);

    // =================== 代码块管理（纯代码块路径）====================
    // 变量名生成（Local0/Local1/...）
    std::string CreateSymbolName(const char* symbol_name_hint);
    // 已格式化好的串直接入库（四段分支：Unknown/内联/白名单建变量/报错）
    int32_t AddCodeChunkInner(uint64_t hash, const char* formatted_type, const char* formatted_code, EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined);
    int32_t AddCodeChunkInner(uint64_t Hash, const char* formatted_code, EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined);
    // printf 版：拼串+算hash后转调上面
    int32_t AddCodeChunkInner(EMaterialValueType t, const char* formated_type, EDerivativeStatus derivative_status, bool in_lined, const char* format, ...);
    int32_t AddCodeChunkInner(EMaterialValueType t, EDerivativeStatus derivative_status, bool in_lined, const char* format, ...);

    // 作用域登记（声明位置管理，LCA 爬升在内）
    void AddCodeChunkToCurrentScope(int32_t chunk_index);
    void AddCodeChunkToScope(int32_t chunk_index, int32_t scope_index);

    // =================== 模板外壳（类型安检 + 预设参数转发）====================
    template<typename... types>
    int32_t AddCodeChunk(EMaterialValueType t, const char* format, types... args) {
        ME_CHECK(TIsValidVariadicFunctionArg<types>::Value&&...);
        return AddCodeChunkInner(t, EDerivativeStatus::NotAware, false, format, args...);
    }

    template<typename... types>
    int32_t AddInlinedCodeChunkZeroDeriv(EMaterialValueType t, const char* format, types... args) {
        ME_CHECK(TIsValidVariadicFunctionArg<types>::Value&&...);
        return AddCodeChunkInner(t, EDerivativeStatus::Zero, true, format, args...);
    }

    template<typename... types>
    int32_t AddInlinedCodeChunk(EMaterialValueType t, const char* format, types... args) {
        ME_CHECK(TIsValidVariadicFunctionArg<types>::Value&&...);
        return AddCodeChunkInner(t, EDerivativeStatus::NotAware, true, format, args...);
    }

    // =================== 算子（⑥，基类接口 override）====================
    // 常量
    int32_t Constant(float x) override;
    int32_t Constant2(float x, float y) override;
    int32_t Constant3(float x, float y, float z) override;
    int32_t Constant4(float x, float y, float z, float w) override;

    // =================== 状态 ===================
    MemStack* allocator = nullptr;                          // chunk 对象住在 MemStack 上（对照 UE FMemStackBase* Allocator）
    std::vector<FShaderCodeChunk*> current_scope_chunks;    // 当前作用域的 chunk 地址清单（对象本体在 MemStack）
    std::vector<FShaderCodeChunk*> uniform_expressions;     // 材质级表达式登记表（跨属性共享去重用）
    std::vector<int32_t> scope_stack;                       // 作用域索引栈（栈顶=当前作用域）
    int32_t next_symbol_index = 0;                          // 变量名计数器
    int32_t adding_uniform_expression = 0;                  // 表达式树构建深度（Scope RAII 维护）
    uint32_t b_cull_intermediate_uniform_expression : 1;    // 是否剔除中间表达式（构造时定格）

    friend class FAddUniformExpressionScope;
};

// =================== RAII：表达式树构建范围标记 ===================
// 参数求值发生在进入函数体之前，所以标记必须由调用语句处的本对象预先打上（对照 UE .h:1459 注释）
class FAddUniformExpressionScope {
public:
    FAddUniformExpressionScope(FHLSLMaterialTranslator* in_translator) : translator(in_translator) {
        translator->adding_uniform_expression++;
    }
    ~FAddUniformExpressionScope() {
        translator->adding_uniform_expression--;
        ME_CHECK(translator->adding_uniform_expression >= 0);
    }

private:
    FHLSLMaterialTranslator* translator;
};
