# 课 6 · FShaderCodeChunk 全字段版 —— 实现流程稿

> 操作向流程稿（做什么 / 怎么做 / 怎么验证），代码自己写。
> UE 原文均在本机镜像实测：`Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.h:83-174`（FShaderCodeChunk 全文）
> 教案理论底稿：`MaterialEditor/docs/lessons/lesson06.md` 第三部分

---

## 0. 这一步在全局的位置

| | 内容 |
|---|---|
| **输入** | 表达式树六类刚完成（`UniformExpression.h`）——本步把树接到 chunk 上 |
| **产出** | ① `EDerivativeStatus` 枚举 ② `FShaderCodeChunk` 7 字段简化版 → 全字段版（17 字段 + AtDefinition）③ 旧代码（MaterialCompiler.cpp）最小适配编译过 |
| **下游消费者** | ⑤代码块管理（AddCodeChunk/AddUniformExpression 装配它、双层去重）、⑥算子、课 8 生成器（code/code_analytic 双轨、is_intermediate 剔除）、课 18（material_attribute_mask）、课 20（scoped_chunks、作用域三件套、DerivativeAutogen 写 derivative_status） |
| **本步不做** | chunk 的构造/去重逻辑（⑤）、算子三轨（⑥）、旧编译器整体拆两层（④） |

## 1. 命名裁决（2026-10-08 终裁）

**类名/方法名对齐 UE**：`AtDefinition(ECompiledPartialDerivativeVariation)`（枚举已建在 HLSLMaterialDerivativeAutogen.h，对照 UE `MaterialShared.h:2043`）。
**字段保持 snake_case**（`symbol_name`/`is_inline`...）——用户自己对照 UE PascalCase 原文，对照表见文末附录。

## 2. EDerivativeStatus → 独立文件 `src/Compiler/Public/HLSLMaterialDerivativeAutogen.h`

与 UE 同名文件（UE 在 `Private/Materials/`），课 20 的 DerivativeAutogen 生成器本体也住这个文件——一步到位不再搬迁。骨架已建，内容照抄：

```cpp
#pragma once
#include <cstdint>

// 解析导数自动生成系统。对照 UE HLSLMaterialDerivativeAutogen.h（课 20 实现生成器本体）
// 偏导数状态。对照 UE EDerivativeStatus（同文件 :20，四值原样）
enum class EDerivativeStatus : uint8_t {
    NotAware,   // 表达式不感知导数（普通路径）
    NotValid,   // 导数无效（分支/采样后）
    Zero,       // 导数恒零（常量）
    Valid,      // 解析导数有效（code_analytic 可用）
};

// 对照 UE IsDerivativeValid（同文件 :30：Valid || Zero）
inline bool IsDerivativeValid(EDerivativeStatus status) {
    return status == EDerivativeStatus::Valid || status == EDerivativeStatus::Zero;
}
```

CodeChunk.h 顶部 `#include "Compiler/Public/HLSLMaterialDerivativeAutogen.h"`（derivative_status 字段用）。

## 3. FShaderCodeChunk 全字段版底稿

对照 UE 逐字段（本机行号）：

```cpp
#pragma once
#include "MaterialTypes/Public/ValueType.h"
#include "Expression/Public/UniformExpression.h"   // Ref<> + 树（刚写的）
#include "Compiler/Public/HLSLMaterialDerivativeAutogen.h"   // EDerivativeStatus
#include <string>
#include <vector>
#include <cstdint>

// 代码块：一行/一段 HLSL + 元数据。对照 FShaderCodeChunk（HLSLMaterialTranslator.h:83）
struct FShaderCodeChunk {
    // === 字段（UE :85-124 逐个对应）===
    uint64_t hash = 0;                  // :85  代码哈希——不同表达式产出等价代码时去重
    uint64_t material_attribute_mask = 0;   // :89  MaterialAttributes 引脚的属性位掩码（课 18 消费）
    std::string code;                  // :97  定义串·有限差分版（UE DefinitionFinite）
    std::string code_analytic;         // :101 定义串·解析导数版（UE DefinitionAnalytic；空=回落 code）
    std::string symbol_name;           // :105 局部变量名；is_inline 或有 uniform_expression 时为空
    Ref<FMaterialUniformExpression> uniform_expression;  // :108 表达式树指针；空=纯 GPU 代码块
    std::vector<int32_t> scoped_chunks;   // :110 作用域挂在本块下的子块（翻译完成后填，课 20）
    std::vector<int32_t> references;      // :113 依赖的 chunk 索引（UE ReferencedCodeChunks）
    EMaterialValueType type = MCT_Unknown;   // :115
    int32_t declared_scope = -1;        // :117 声明所在作用域（UE DeclaredScopeIndex，课 20）
    int32_t used_scope = -1;            // :118 使用所在作用域（UE UsedScopeIndex，课 20）
    int32_t scope_level = 0;            // :119 作用域嵌套层级（UE ScopeLevel，课 20）
    bool is_inline = false;             // :121 内联：无变量名，code 直接嵌入（UE bInline）
    bool is_intermediate = false;       // :122 中间块：无引用者时课 8 生成可剔除（UE bIntermediate）
    EDerivativeStatus derivative_status = EDerivativeStatus::NotAware;   // :124

    // 按变体取定义串（对照 UE AtDefinition :126）
    const std::string& AtDefinition(ECompiledPartialDerivativeVariation variation) const {
        return variation == ECompiledPartialDerivativeVariation::Analytic
               && !code_analytic.empty() ? code_analytic : code;
    }

    // === 两个构造（UE :140-174 照抄，语义见 §4）===
    FShaderCodeChunk() = default;

    // 纯代码块（UE :140）：可能有 symbol_name（非内联时）
    FShaderCodeChunk(uint64_t in_hash, const char* in_code_finite, const char* in_code_analytic,
              const std::string& in_symbol_name, EMaterialValueType in_type,
              EDerivativeStatus in_derivative_status, bool in_inline)
        : hash(in_hash), material_attribute_mask(0),
          code(in_code_finite), code_analytic(in_code_analytic),
          symbol_name(in_symbol_name), uniform_expression(nullptr), type(in_type),
          declared_scope(-1), used_scope(-1), scope_level(0),
          is_inline(in_inline), is_intermediate(false),
          derivative_status(in_derivative_status) {}

    // 带表达式树的块（UE :160）：无 symbol_name、is_inline 恒 false——Definition 直接用
    FShaderCodeChunk(uint64_t in_hash, FMaterialUniformExpression* in_expression,
              const char* in_code_finite, const char* in_code_analytic,
              EMaterialValueType in_type, EDerivativeStatus in_derivative_status)
        : hash(in_hash), material_attribute_mask(0),
          code(in_code_finite), code_analytic(in_code_analytic),
          symbol_name(), uniform_expression(in_expression), type(in_type),
          declared_scope(-1), used_scope(-1), scope_level(0),
          is_inline(false), is_intermediate(false),
          derivative_status(in_derivative_status) {}
};
```

**相对旧版（7 字段）的变化**：
- `is_constant` + `constant_value(variant)` **退役**——由 `uniform_expression` 树指针替代（树能表达 variant 表达不了的"含参数"形态）
- 新增 10 个字段 + AtDefinition + 两个构造

## 4. 两个构造函数的语义（UE :140/:160 注释原文的翻译）

| | 纯代码块构造（:140） | 带表达式构造（:160） |
|---|---|---|
| 谁用它 | ⑤的 `AddCodeChunk`（函数调用、纹理采样这类真 GPU 代码） | ⑤的 `AddUniformExpression`（常量/FoldedMath 树） |
| SymbolName | 非内联时有（`Local0`） | **无参数**——永远空串 |
| bInline | 调用方传 | **恒 false**（但语义上"表达式块永远直接用 code"） |
| code 的意义 | 内联时=嵌入表达式；非内联时=变量声明的右值 | 定义串直接用（字面量/表达式串） |

由此，⑤的 `GetParameterCode` 是三段判断（顺序重要）：

```cpp
if (c.uniform_expression) return c.code;   // 有树 → 定义串直接用
if (c.is_inline)           return c.code;   // 内联块 → 定义串
return c.symbol_name;                       // 非内联 → 变量名（引用声明）
```

（本步只记语义，实现归⑤。）

## 5. 旧代码适配（MaterialCompiler.cpp 实测引用点）

升级字段后旧代码立刻编译不过，按下面逐处改（都是"让它编过"级的最小改动，④⑤⑥ 会重写这些函数）：

| 位置 | 现状 | 改法 |
|---|---|---|
| `AddConstantChunk`（:58-75） | `is_constant = true; constant_value = value;`（variant 存值） | **挂树**：把 variant 值转 `Vec4`（float→(v,v,v,v)，Vec2/3/4 直接转），`chunk.uniform_expression = MakeRef<FMaterialUniformExpressionConstant>(vec, type);`——你的树第一次正式上岗 |
| `FormatConstantCode`（:79） | 调 `ConstFolding::IsScalarZero/One` | 内联替换：`std::holds_alternative<float>(value) && std::get<float>(value) == 0.0f`（ConstFolding 整体退役是⑦，这里先脱钩） |
| `IsConstant(index)`（:120） | `return chunk.is_constant;` | 新语义：`return chunk.uniform_expression && chunk.uniform_expression->IsConstant();` |
| `GetConstantValue(index)`（:128） | 读 `constant_value` variant | **删除**（声明+实现）——它的职责由树的 `GetNumbervalue` 接管（⑥算子折叠时用） |
| Add 算子里的立即折叠（:11-12） | `ConstFolding::FoldBinary("+", GetConstantValue(a), ...)` | **删除该分支**——⑥重写时按教案三轨判定建 FoldedMath 树（UE 原版 Add 就是建树），现在只留纯 HLSL 路径 |

## 6. 验证

结构升级 + 适配完成后：

1. `cmake --build build --config Debug` 零错误（CodeChunk.h 现在被 MaterialCompiler.cpp 真实编译着，不是假阴性）
2. 字段自查（对照 §3 表逐个 grep）：`grep -c "material_attribute_mask\|code_analytic\|derivative_status\|is_intermediate\|AtDefinition" Compiler/Public/CodeChunk.h` → 5 类全命中
3. 行为小测（写进 ExprTreeTest 尾部或临时 main 三行）：
   ```cpp
   FShaderCodeChunk c1(0, "1.0+2.0", "", "", MCT_Float, EDerivativeStatus::NotAware, true);
   FShaderCodeChunk c2(0, nullptr_expr_ptr, "3.0", "", MCT_Float, EDerivativeStatus::Zero);
   // AtDefinition 双轨：analytic 为空时回落 finite
   assert(c1.AtDefinition(ECompiledPartialDerivativeVariation::Analytic) == "1.0+2.0");   // code_analytic 空 → 回落
   assert(c2.AtDefinition(ECompiledPartialDerivativeVariation::FiniteDifferences) == "3.0");
   assert(c2.symbol_name.empty());          // 表达式构造无符号名
   assert(!c2.is_inline);                   // 表达式构造恒非内联
   ```

## 7. 完成标志（本步可勾）

- [ ] `EDerivativeStatus` 四值枚举（照抄 UE :20）
- [ ] FShaderCodeChunk 17 字段 + `AtDefinition(variation)` + 两个构造（参数与 UE :140/:160 一一对应）
- [ ] `is_constant`/`constant_value` 退役，无残留引用
- [ ] §5 五处旧代码适配完成，`MaterialCompiler.cpp` 编译零错误
- [ ] §6 验证全过

## 附：UE ↔ 教学版对照总表（本机实测行号）

| 教学版 | UE | UE 行号 | 消费时机 |
|---|---|---|---|
| hash | Hash | :85 | 现在（⑤去重） |
| material_attribute_mask | MaterialAttributeMask | :89 | 课 18 Make/Break |
| code | DefinitionFinite | :97 | 现在 |
| code_analytic | DefinitionAnalytic | :101 | 课 8 读 / 课 20 写 |
| symbol_name | SymbolName | :105 | 现在 |
| uniform_expression | UniformExpression | :108 | 现在（本步接上树） |
| scoped_chunks | ScopedChunks | :110 | 课 20 Custom |
| references | ReferencedCodeChunks | :113 | 现在（课 8 拓扑排序） |
| type | Type | :115 | 现在 |
| declared_scope/used_scope/scope_level | DeclaredScopeIndex/UsedScopeIndex/ScopeLevel | :117-119 | 课 20 |
| is_inline | bInline | :121 | 现在 |
| is_intermediate | bIntermediate | :122 | 课 8 剔除 |
| derivative_status | DerivativeStatus | :124 | 课 20 写 |
| AtDefinition(ECompiledPartialDerivativeVariation) | AtDefinition(ECompiledPartialDerivativeVariation) | :126 | 课 8 |
| 构造① | 纯代码块构造 | :140 | ⑤ |
| 构造② | 带表达式构造 | :160 | ⑤ |
| IsDerivativeValid（已随枚举搬入独立文件） | IsDerivativeValid | DerivativeAutogen.h:30 | 课 20 |
