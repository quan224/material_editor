#pragma once
#include "MaterialTypes/Public/ValueType.h"
#include "Core/Public/MathTypes.h"   // Vec2/3/4（constant_value variant 用）
#include "Compiler/Public/HLSLMaterialDerivativeAutogen.h"
#include "Expression/Public/UniformExpression.h"
#include <string>
#include <vector>
#include <cstdint>
#include <variant>

struct FShaderCodeChunk{
    uint64_t hash = 0;
    uint64_t material_attribute_mask = 0;  // MaterialAttributes 引脚的属性位掩码
    std::string code;  // :97  定义串·有限差分版（UE DefinitionFinite）
    std::string code_analytic;  // :101 定义串·解析导数版（UE DefinitionAnalytic
    std::string symbol_name;  // 局部变量名
    Ref<FMaterialUniformExpression> uniform_expression;  // 表达式树指针
    std::vector<int32_t> scoped_chunks;  // 作用域挂在本块下的子块
    std::vector<int32_t> reference;  // 以来的chunk索引
    EMaterialValueType type_ = MCT_Unknown;
    int32_t declared_scope = -1;  // 声明所在的作用域
    int32_t used_scope = -1;  // 使用所在作用域
    int32_t scope_level = 0;  // 作用域嵌套层级
    bool is_inline = false;  // 内联
    bool is_intermediate = false;  // 中间块
    EDerivativeStatus derivative_status = EDerivativeStatus::NotAware;

    // 按变体取定义串（对照 UE AtDefinition, HLSLMaterialTranslator.h:126）
    const std::string& AtDefinition(ECompiledPartialDerivativeVariation variation) const{
        return variation == ECompiledPartialDerivativeVariation::Analytic && !code_analytic.empty()
               ? code_analytic : code;
    }
    FShaderCodeChunk()=default;

        // 纯代码块（UE :140）：可能有 symbol_name（非内联时）
    FShaderCodeChunk(uint64_t in_hash, const char* in_code_finite, const char* in_code_analytic,
              const std::string& in_symbol_name, EMaterialValueType in_type,
              EDerivativeStatus in_derivative_status, bool in_inline)
        : hash(in_hash), material_attribute_mask(0),
          code(in_code_finite), code_analytic(in_code_analytic),
          symbol_name(in_symbol_name), uniform_expression(nullptr), type_(in_type),
          declared_scope(-1), used_scope(-1), scope_level(0),
          is_inline(in_inline), is_intermediate(false),
          derivative_status(in_derivative_status) {}

        // 带表达式树的块（UE :160）：无 symbol_name、is_inline 恒 false——Definition 直接用
    FShaderCodeChunk(uint64_t in_hash, FMaterialUniformExpression* in_expression,
              const char* in_code_finite, const char* in_code_analytic,
              EMaterialValueType in_type, EDerivativeStatus in_derivative_status)
        : hash(in_hash), material_attribute_mask(0),
          code(in_code_finite), code_analytic(in_code_analytic),
          symbol_name(), uniform_expression(in_expression), type_(in_type),
          declared_scope(-1), used_scope(-1), scope_level(0),
          is_inline(false), is_intermediate(false),
          derivative_status(in_derivative_status) {}

};