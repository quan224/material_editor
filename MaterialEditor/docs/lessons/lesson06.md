# 课6：编译器核心（扩展版，对照 UE5）

## 目标

实现 `MaterialCompiler` 主类 + `CodeChunk` 数据结构，把材质图（Graph）翻译成 HLSL 代码。对标 UE5 的 `FHLSLMaterialTranslator`——这是整个项目**最核心最复杂**的部分。

**扩展版相比初版的关键升级**（一步到位，不做简化中间态）：
- **类型系统**：从只有 `Float1-4` 扩展到 `Int1-4` / `Matrix3x3` / `Matrix4x4` / `Texture2D` / `SamplerState`
- **CodeChunk**：常量值从单个 `float` 改为 `std::variant<float, Vec2, Vec3, Vec4>`，**向量也能编译期折叠**
- **常数折叠**：从标量扩展到向量/矩阵；`Constant2/3/4` 走常量路径（不再只是 code 字符串）
- **MaterialCompiler**：区分 `AddCodeChunk` / `AddInlinedCodeChunk`；`Add/Sub/Mul/Div` 采用 UE 风格的**三段判定**

每个设计点都对照 UE5 真实实现（相对 `Engine/` 路径），讲清楚"UE 怎么做、教学版为什么这样简化"。

---

## 背景知识

### 编译器做什么

接收一个 `Graph`，从输出节点反向遍历，对每个节点调用其 `Expression::Compile()`，生成一系列"代码块"（`CodeChunk`），最后组装成 HLSL 着色器。

### 编译流程

```
MaterialCompiler::Compile(Graph*)
  │
  ├─ 1. 获取输出节点（材质根）
  ├─ 2. 对输出节点的每个输入引脚（BaseColor, Metallic, ...）
  │     调用 CompileInputPin(outputNode, "BaseColor")
  │       │
  │       ├─ 找到该引脚连接的上游节点
  │       ├─ 递归编译上游 → CompileExpression(upstreamNode)
  │       │     │
  │       │     ├─ 上游 Expression::Compile(this)
  │       │     │    ├─ 又调 CompileInputPin() 编译自己的输入 ← 递归
  │       │     │    ├─ 调 compiler->Add(a, b) 等生成代码块
  │       │     │    └─ 返回输出引脚的代码块索引
  │       │     │
  │       │     └─ 缓存结果（同一节点不重复编译）
  │       │
  │       └─ 返回代码块索引
  │
  ├─ 3. 收集所有材质属性的代码块索引
  └─ 4. GenerateCode() 组装最终 HLSL
```

### 关键设计：双向递归（对照 UE）

这是理解整个编译器的钥匙——**递归是双向的**，编译器和图节点各管一半：

| 角色 | 职责 |
|------|------|
| **编译器**（`MaterialCompiler`）| 只暴露"算子 API"（`Add`/`Constant`/`TextureSample`…），**不知道图结构** |
| **图节点**（`Expression` 子类）| `Compile()` 里**先编译自己的输入引脚（递归），再调编译器算子组合结果** |

```
Expression::Compile(compiler, node):
    int a = compiler->CompileInputPin(node, "A");   // ← 递归编译 A 引脚（上游）
    int b = compiler->CompileInputPin(node, "B");   // ← 递归编译 B 引脚（上游）
    return compiler->Add(a, b);                      // 组合
```

> **UE 对照**：UE 也是这套——`FHLSLMaterialTranslator` 只提供算子，`UMaterialExpression` 子类的 `Compile()` 驱动递归。见 `Engine/Source/Runtime/Engine/Private/Materials/Material.cpp` 的 `CompilePropertyAndSetMaterialProperty`，它触发具体节点的 `Compile`，节点又调 `Compiler->Add/Sub/...`。

### CodeChunk 是什么

每一行 HLSL 代码 = 一个 CodeChunk，编译器用 `int32_t` 索引引用：

```hlsl
float Local0 = 1.0;              // CodeChunk 0: 常量
float Local1 = 0.5;              // CodeChunk 1: 常量
float Local2 = Local0 + Local1;  // CodeChunk 2: Add 的结果，引用 0 和 1
```

`Add(0, 1)` 返回 `2`（索引），下游用 `2` 引用结果。**`int32_t` 是 `chunks_` 数组的下标，不是数值**（负数段 `-1` 等留给"错误/无效"哨兵）。

---

## 第一部分：类型系统（对照 UE `EMaterialValueType`）

### 文件：`src/MaterialGraph/Public/Types.h`（扩展 `EValueType`）

初版只有 `Float1-4`。扩展版加 int / matrix / texture / sampler：

```cpp
enum class EValueType {
    Unknown,
    // 标量/向量（float）
    Float1, Float2, Float3, Float4,
    // 整数（材质里偶有整数运算：循环计数、位掩码等）
    Int1, Int2, Int3, Int4,
    // 矩阵（变换、投影）
    Matrix3x3, Matrix4x4,
    // 资源对象类型（不能参与算术，只能给 TextureSample 用）
    Texture2D,
    SamplerState,
};
```

**每个类型的用途**：
- `Float1-4`：颜色、UV、向量、标量——材质主体
- `Int1-4`：整数运算（少数节点用，如位操作）
- `Matrix3x3/4x4`：变换矩阵（如 `Transform` 节点）
- `Texture2D / SamplerState`：纹理对象 + 采样器状态，是"对象类型"，**不能算术运算**，只供 `TextureSample` 使用

**类型查询（free function，留在这层）**：

```cpp
// 分量数：纯类型属性查询，零编译器知识 → 放 Types.h（不放 TypeSystem）
inline int GetComponentCount(EValueType t) {
    switch (t) {
        case EValueType::Float1: case EValueType::Int1: return 1;
        case EValueType::Float2: case EValueType::Int2: return 2;
        case EValueType::Float3: case EValueType::Int3: return 3;
        case EValueType::Float4: case EValueType::Int4: return 4;
        default: return 0;   // Matrix/Texture/Unknown 不走分量逻辑
    }
}
```
> `GetComponentCount` 是"类型本身的属性"，留 Types.h；TypeSystem（编译器层）**不重复定义**，需要时调 `::GetComponentCount(t)`。分层原则：HLSL/组合规则才进 TypeSystem。

### 文件：`src/Compiler/Public/TypeSystem.h`（推导规则）

算术运算的结果类型推导（对照 UE `GetArithmeticResultType`）：

```cpp
class TypeSystem {
public:
    // 算术结果类型（Add/Sub/Mul/Div 用）
    static EValueType GetArithmeticResultType(EValueType a, EValueType b) {
        if (a == Unknown || b == Unknown) return Unknown;
        if (a == b) return a;
        // 标量 × 向量 → 向量（标量复制提升）：Float1 + Float3 → Float3
        if (a == Float1) return b;
        if (b == Float1) return a;
        // 同理 Int 标量提升
        if (a == Int1) return b;
        if (b == Int1) return a;
        return Unknown;  // Float2 + Float3 这种不兼容
    }
    // GetComponentCount 不在这里——它是纯类型属性查询，留在 Types.h（free function），
    // 本类不重复定义，需要时调 ::GetComponentCount(t)。分层：HLSL/组合规则才进 TypeSystem。

    static const char* ToHLSLType(EValueType t) {
        switch (t) {
            case EValueType::Float1:   return "float";
            case EValueType::Float2:   return "float2";
            case EValueType::Float3:   return "float3";
            case EValueType::Float4:   return "float4";
            case EValueType::Int1:     return "int";
            case EValueType::Int2:     return "int2";
            case EValueType::Int3:     return "int3";
            case EValueType::Int4:     return "int4";
            case EValueType::Matrix3x3: return "float3x3";
            case EValueType::Matrix4x4: return "float4x4";
            default: return "float";   // 兜底，避免生成非法 HLSL
        }
    }
};
```

### 对照 UE：为什么 UE 用 bitmask，我们用 enum

UE 的 `EMaterialValueType` 是 **64 位 bitmask**（`Engine/Source/Runtime/Engine/Public/MaterialValueType.h`）：

```cpp
enum EMaterialValueType : uint64 {
    MCT_Float1 = 1u<<0, MCT_Float2 = 1u<<1, ...,
    MCT_Float = MCT_Float1|MCT_Float2|MCT_Float3|MCT_Float4,  // "任意 float"
    MCT_UInt1..4, MCT_StaticBool, MCT_Float3x3, MCT_Float4x4,
    MCT_Texture2D, ...
};
```

**bitmask 的好处**：能做"类别集合"判断，如 `Type & MCT_Float`（是不是任意 float）、`Type & MCT_Texture`（是不是任意纹理）。UE 的 `AddCodeChunkInner` 就用 `(Type & (MCT_Float|MCT_LWCType|MCT_UInt|...))` 判断能否声明局部变量。

**教学版为什么用普通 enum**：
- 简单直观，调试看枚举名就懂（bitmask 看到的是数字）
- 教学版的类型少（不需要"任意 float"这种集合判断）
- 代价：不能做 `IsFloat()` 这种集合查询——但教学版用 `switch` 逐个判断够用

> 这是一个有意的简化取舍：**UE 用 bitmask 换表达力（集合运算），教学版用 enum 换可读性**。

---

## 第二部分：CodeChunk（对照 UE `FShaderCodeChunk`）

### 文件：`src/Compiler/Public/CodeChunk.h`（扩展版）

```cpp
#pragma once
#include "MaterialGraph/Public/Types.h"
#include "Core/Public/MathTypes.h"   // Vec2/Vec3/Vec4
#include <string>
#include <vector>
#include <cstdint>
#include <variant>

struct CodeChunk {
    uint64_t hash = 0;              // ① 哈希：去重用（相同代码只存一份）
    std::string code;               // ② HLSL 代码片段，如 "Local0 + Local1"
    std::string symbol_name;        // ③ 变量名，如 "Local2"（非内联时）
    EValueType type = EValueType::Unknown;  // ④ 类型（决定生成 float/float3/...）
    bool is_inline = false;         // ⑤ 短表达式直接嵌入，不声明变量
    bool is_constant = false;       // ⑥ 是否编译时常量（折叠标记）

    // ⑦ 常量值——扩展版核心升级：variant 支持向量
    std::variant<float, Vec2, Vec3, Vec4> constant_value;

    std::vector<int32_t> references; // ⑧ 依赖哪些其他 chunk（确定声明顺序）
};
```

### 每个字段的作用 + 为什么

| 字段 | 作用 | 为什么需要 |
|------|------|-----------|
| ① `hash` | 代码块去重 | 相同的 `Local0 + Local1` 只存一份，UE 也这么做（CityHash64）|
| ② `code` | HLSL 片段 | 最终拼进着色器，如 `"float3(1,0,0) + float3(0,1,0)"` |
| ③ `symbol_name` | 局部变量名 | 非内联时生成 `float3 Local2 = ...;`，下游用 `Local2` 引用 |
| ④ `type` | 值类型 | 决定生成 `float` / `float3` / `int` / 等声明；算术推导用 |
| ⑤ `is_inline` | 是否内联 | 短表达式（如 `1.0`）直接嵌入使用处，不生成变量声明——减少无谓局部变量 |
| ⑥ `is_constant` | 是否常量 | 折叠判定根：`Add` 里 `if (IsConstant(a) && IsConstant(b))` 走折叠 |
| ⑦ `constant_value` | **常量值（variant）** | 折叠时存实际数值，**升级点：能装 Vec2/3/4，向量也能折叠** |
| ⑧ `references` | 依赖追踪 | 记录依赖哪些 chunk，生成代码时按依赖顺序声明 |

### 为什么 `constant_value` 用 `std::variant`（扩展版核心）

**初版问题**：`constant_value` 是单个 `float`，**存不下向量**——所以 `Constant3(1,0,0)` 只能走 `AddCodeChunk` 存成 `"float3(1,0,0)"` 字符串、`is_constant=false`、**不参与折叠**。两个颜色常量相加 `Add(Constant3(1,0,0), Constant3(0,1,0))` 无法编译期算。

**扩展版方案**：改成 `std::variant<float, Vec2, Vec3, Vec4>`，向量也能存、能折叠：

```cpp
// 现在能这样折叠（初版做不到）：
int a = c.Constant3(1,0,0);          // constant_value = Vec3(1,0,0)
int b = c.Constant3(0,1,0);          // constant_value = Vec3(0,1,0)
int s = c.Add(a, b);                 // 折叠！constant_value = Vec3(1,1,0)
```

`variant` 的 `index()` 告诉你存的是哪种类型，`std::get<Vec3>()` 取值。配合 `GetType(idx)`（EValueType），编译器知道这个常量是 float 还是 Vec3。

### 对照 UE：`FShaderCodeChunk` 怎么存常量

UE **不在 chunk 里直接存数值**，而是挂一棵表达式树（`Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.h:83-174`）：

```cpp
struct FShaderCodeChunk {
    uint64 Hash;
    FString DefinitionFinite;       // HLSL 串
    FString SymbolName;
    EMaterialValueType Type;
    bool bInline;
    TRefCountPtr<FMaterialUniformExpression> UniformExpression;  // ← 常量/可折叠表达式树
    ...
};
```

常量值存在 `UniformExpression` 指向的 `FMaterialUniformExpressionConstant` 节点里的 `FLinearColor Value`（4 分量，能装 float1-4）+ `ValueType` 标签。

**UE 为什么用表达式树而不是 variant**：
- UE 要支持 **preshader**——含运行时参数（如 `ScalarParameter`）的表达式也能"折叠"成 CPU 端预求值，而非编译期算死。`FoldedMath(A, B, Op)` 子树保留参数依赖，运行时求值。
- 表达式树能 `IsConstant()` **递归判定**（整棵子树都是常量才算常量）、`IsIdentical()` 去重。
- 代价：复杂（要设计表达式类层次 + 序列化 preshader 字节码 + 求值器）。

**教学版为什么用 variant**：
- 不做 preshader（含参数的运行时折叠）——材质参数直接当 uniform 传给 shader，不在编译器里折叠
- 只做**编译期常量折叠**（纯常量算成字面量）——variant 直接存值、直接算，够用
- 代价：丢失了"含参数表达式预求值"能力，但教学版不需要

> 这是有意的简化：**UE 用表达式树换 preshader 能力（含参数预求值），教学版用 variant 换简单（只编译期折叠纯常量）**。核心语义保留——"是否常量"决定能否折叠。

---

## 第三部分：常数折叠（对照 UE `Add/Sub/Mul/Div` 三段判定）

### 文件：`src/Compiler/Public/ConstantFolding.h`（扩展版：支持向量）

```cpp
#pragma once
#include "Core/Public/MathTypes.h"
#include <cmath>
#include <variant>
#include <optional>
#include <string>

// 常量值类型（和 CodeChunk::constant_value 一致）
using ConstValue = std::variant<float, Vec2, Vec3, Vec4>;

class ConstantFolding {
public:
    // 二元运算折叠。返回空 optional 表示不能折叠（除零、类型不兼容、未知 op）
    static std::optional<ConstValue> FoldBinary(const std::string& op,
                                                 const ConstValue& a,
                                                 const ConstValue& b) {
        // 都是向量但维度不同（Vec2+Vec3）→ 不折叠，让调用方发 HLSL 或报错
        if (DimOf(a) > 1 && DimOf(b) > 1 && DimOf(a) != DimOf(b))
            return std::nullopt;

        const int dim = std::max(DimOf(a), DimOf(b));   // 结果维度
        float pa[4], pb[4];
        FillArray(a, dim, pa);   // 标量(float)广播到 dim 个分量；向量按自身分量填
        FillArray(b, dim, pb);

        // 除零检查：任意分量 b==0 则不能折叠
        if (op == "/") {
            for (int i = 0; i < dim; ++i)
                if (pb[i] == 0.0f) return std::nullopt;
        }

        float r[4];
        for (int i = 0; i < dim; ++i) {
            if      (op == "+") r[i] = pa[i] + pb[i];
            else if (op == "-") r[i] = pa[i] - pb[i];
            else if (op == "*") r[i] = pa[i] * pb[i];
            else if (op == "/") r[i] = pa[i] / pb[i];
            else return std::nullopt;   // 未知 op
        }
        return MakeValue(r, dim);
    }

    // 一元运算折叠
    static std::optional<ConstValue> FoldUnary(const std::string& op, const ConstValue& a) {
        const int dim = DimOf(a);
        float pa[4];
        FillArray(a, dim, pa);
        float r[4];
        for (int i = 0; i < dim; ++i) {
            if      (op == "abs") r[i] = std::abs(pa[i]);
            else if (op == "neg") r[i] = -pa[i];
            else if (op == "sin") r[i] = std::sin(pa[i]);
            else if (op == "cos") r[i] = std::cos(pa[i]);
            else return std::nullopt;
        }
        return MakeValue(r, dim);
    }

    // 标量常量判定（代数化简用：x*1, x*0, x/1, x-0 的 1/0 检查）
    static bool IsScalarZero(const ConstValue& v) {
        return std::holds_alternative<float>(v) && std::get<float>(v) == 0.0f;
    }
    static bool IsScalarOne(const ConstValue& v) {
        return std::holds_alternative<float>(v) && std::get<float>(v) == 1.0f;
    }

private:
    // 维度：float=1, Vec2=2, Vec3=3, Vec4=4
    static int DimOf(const ConstValue& v) {
        switch (v.index()) {
            case 0: return 1;   // float
            case 1: return 2;   // Vec2
            case 2: return 3;   // Vec3
            case 3: return 4;   // Vec4
        }
        return 0;
    }

    // 把 ConstValue 按目标维度 dim 填到 out[]。float 标量会广播到所有分量
    // （实现 Float1 + Float3 这种标量提升）；向量按自身分量填（不足的分量不动）
    static void FillArray(const ConstValue& v, int dim, float out[4]) {
        if (std::holds_alternative<float>(v)) {
            float f = std::get<float>(v);
            for (int i = 0; i < dim; ++i) out[i] = f;       // 标量广播
        } else if (std::holds_alternative<Vec2>(v)) {
            Vec2 vec = std::get<Vec2>(v); out[0] = vec.x; out[1] = vec.y;
        } else if (std::holds_alternative<Vec3>(v)) {
            Vec3 vec = std::get<Vec3>(v); out[0] = vec.x; out[1] = vec.y; out[2] = vec.z;
        } else if (std::holds_alternative<Vec4>(v)) {
            Vec4 vec = std::get<Vec4>(v); out[0] = vec.x; out[1] = vec.y; out[2] = vec.z; out[3] = vec.w;
        }
    }

    // 按维度组装回 ConstValue
    static ConstValue MakeValue(const float r[4], int dim) {
        switch (dim) {
            case 1: return r[0];
            case 2: return Vec2(r[0], r[1]);
            case 3: return Vec3(r[0], r[1], r[2]);
            case 4: return Vec4(r[0], r[1], r[2], r[3]);
        }
        return r[0];
    }
};
```

**关键**：折叠函数现在接收/返回 `ConstValue`（variant），能处理向量。例如 `FoldBinary("+", Vec3(1,0,0), Vec3(0,1,0))` 逐分量相加得 `Vec3(1,1,0)`。

实现要点：用 `std::visit` 或检查 `variant.index()` 分发——两个 float 直接算、两个 Vec3 逐分量算、float × Vec3 标量广播（`Float1 + Float3` 情况）。

### 代数化简（在 `Add/Sub/Mul/Div` 里特判，不只靠 FoldBinary）

对照 UE 的 `Mul`（`HLSLMaterialTranslator.cpp:9939`）：

| 模式 | 化简结果 | 例子 |
|------|---------|------|
| `x * 1` | `x`（直通，不生成新 chunk）| `Multiply(expr, Constant(1.0))` → 直接返回 expr 的索引 |
| `x * 0` | `0` | `Multiply(expr, Constant(0.0))` → 返回 `Constant(0)` |
| `x / 1` | `x` | |
| `0 / x` | `0` | |
| `x - 0` | `x` | |
| `x - x` | `0` | |
| `pow(x, 1)` | `x` | |
| `pow(x, 0)` | `1` | |

### Add/Sub/Mul/Div 的三段判定（对照 UE，黄金模板）

UE 的 `FHLSLMaterialTranslator::Add`（`.cpp:9849-9883`）有一套清晰的判定顺序，教学版照搬这个骨架：

```cpp
// === Add：三段判定的模板 ===
int32_t MaterialCompiler::Add(int32_t a, int32_t b) {
    // 段 1：错误检查（INDEX_NONE 哨兵传播）
    if (a < 0 || b < 0) return -1;
    auto resultType = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    if (resultType == EValueType::Unknown) return -1;   // 类型不兼容

    // 段 2：常数折叠（两边都是常量 → 编译期算出 variant）
    if (IsConstant(a) && IsConstant(b)) {
        auto folded = ConstantFolding::FoldBinary("+", GetConstantValue(a), GetConstantValue(b));
        if (folded) return AddConstantChunk(resultType, *folded);
    }

    // 段 3：普通 HLSL 发射（简单算术走内联）
    return AddInlinedCodeChunk(resultType,
        GetParameterCode(a) + " + " + GetParameterCode(b));
}

// === Subtract：同 Add 模式，加一个 x-0→x 化简 ===
int32_t MaterialCompiler::Subtract(int32_t a, int32_t b) {
    if (a < 0 || b < 0) return -1;
    auto resultType = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    if (resultType == EValueType::Unknown) return -1;

    if (IsConstant(a) && IsConstant(b)) {
        auto folded = ConstantFolding::FoldBinary("-", GetConstantValue(a), GetConstantValue(b));
        if (folded) return AddConstantChunk(resultType, *folded);
    }
    // 代数化简：x - 0 → x（b 是标量常量 0）
    if (IsConstant(b) && ConstantFolding::IsScalarZero(GetConstantValue(b))) return a;

    return AddInlinedCodeChunk(resultType,
        GetParameterCode(a) + " - " + GetParameterCode(b));
}

// === Multiply：含 1*x→x、x*1→x、0*x→0、x*0→0 化简 ===
int32_t MaterialCompiler::Multiply(int32_t a, int32_t b) {
    if (a < 0 || b < 0) return -1;
    auto resultType = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    if (resultType == EValueType::Unknown) return -1;

    if (IsConstant(a) && IsConstant(b)) {
        auto folded = ConstantFolding::FoldBinary("*", GetConstantValue(a), GetConstantValue(b));
        if (folded) return AddConstantChunk(resultType, *folded);
    }
    // 代数化简
    if (IsConstant(a) && ConstantFolding::IsScalarOne (GetConstantValue(a))) return b;               // 1 * x → x
    if (IsConstant(b) && ConstantFolding::IsScalarOne (GetConstantValue(b))) return a;               // x * 1 → x
    if (IsConstant(a) && ConstantFolding::IsScalarZero(GetConstantValue(a))) return Constant(0.0f);  // 0 * x → 0
    if (IsConstant(b) && ConstantFolding::IsScalarZero(GetConstantValue(b))) return Constant(0.0f);  // x * 0 → 0

    return AddInlinedCodeChunk(resultType,
        GetParameterCode(a) + " * " + GetParameterCode(b));
}

// === Divide：含除零保护、x/1→x 化简 ===
int32_t MaterialCompiler::Divide(int32_t a, int32_t b) {
    if (a < 0 || b < 0) return -1;
    auto resultType = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    if (resultType == EValueType::Unknown) return -1;

    // 除零保护：b 是标量常量 0 → 返回 0，不崩
    if (IsConstant(b) && ConstantFolding::IsScalarZero(GetConstantValue(b))) {
        ME_LOG_WARNING("Division by zero in material compile");
        return Constant(0.0f);
    }
    if (IsConstant(a) && IsConstant(b)) {
        auto folded = ConstantFolding::FoldBinary("/", GetConstantValue(a), GetConstantValue(b));
        if (folded) return AddConstantChunk(resultType, *folded);
    }
    // 代数化简：x / 1 → x
    if (IsConstant(b) && ConstantFolding::IsScalarOne(GetConstantValue(b))) return a;

    return AddInlinedCodeChunk(resultType,
        GetParameterCode(a) + " / " + GetParameterCode(b));
}
```

三段的职责：
1. **错误检查**：`INDEX_NONE`（负数）传播——上游失败不继续算
2. **常数折叠**：两边都常量 → 编译期算，生成扁平常量 chunk
3. **HLSL 发射**：非常量 → 生成代码串 chunk

### 对照 UE：preshader 双轨

UE 实际是**三轨**（`HLSLMaterialTranslator.cpp` 的 `Div` 10011-10048 看得最清）：
1. **错误检查**（`INDEX_NONE`）
2. **编译期折叠**：两边都是 `IsConstant()` → `ConstResultValue` 直接算字面量
3. **preshader 折叠**：两边都有 `UniformExpression`（但不是纯常量，含参数）→ 建 `FoldedMath(A,B,Op)` 子树，运行时 CPU 求值
4. **普通 HLSL 发射**：至少一边不是 uniform → 发代码串

**教学版只做 1+2+4，跳过 3（preshader）**——因为 variant 只存纯常量，不支持含参数表达式树。这是 variant 方案的代价，但教学版不需要 preshader。

### 其他算子（Power 为代表，其余照 Add 模式）

Add/Sub/Mul/Div 是最复杂的（三段判定 + 化简）。其他算子结构类似但更简单。给一个 `Power` 完整实现作模板：

```cpp
// Power：三段判定 + pow(x,0)=1 / pow(x,1)=x 化简
int32_t MaterialCompiler::Power(int32_t base, int32_t exp) {
    if (base < 0 || exp < 0) return -1;

    if (IsConstant(base) && IsConstant(exp)) {
        const auto& bv = GetConstantValue(base);
        const auto& ev = GetConstantValue(exp);
        if (ConstantFolding::IsScalarZero(ev)) return Constant(1.0f);   // pow(x,0) → 1
        if (ConstantFolding::IsScalarOne(ev))  return base;             // pow(x,1) → x
        // 两个标量常量 → 编译期算 pow
        if (std::holds_alternative<float>(bv) && std::holds_alternative<float>(ev)) {
            return AddConstantChunk(GetType(base),
                std::pow(std::get<float>(bv), std::get<float>(ev)));
        }
    }
    // 非常量 → 发 HLSL（pow 是函数调用，非内联，声明局部变量）
    return AddCodeChunk(GetType(base),
        "pow(" + GetParameterCode(base) + ", " + GetParameterCode(exp) + ")", /*is_inline=*/false);
}
```

其余算子照这个模式实现（每个 ~5-8 行）：

| 算子 | 模式 | 代码骨架 |
|------|------|---------|
| `Abs/Negate/Sine/Cosine` | 一元 + 常量折叠 | 常量时 `ConstantFolding::FoldUnary(...)` → `AddConstantChunk`；否则 `AddInlinedCodeChunk(GetType(x), "abs(" + GetParameterCode(x) + ")")`（一元短，内联）|
| `Lerp(a,b,alpha)` | 三元，无折叠 | `AddCodeChunk(GetType(a), "lerp(" + a + ", " + b + ", " + alpha + ")", false)` |
| `Clamp(x,min,max)` | 三元，无折叠 | `AddCodeChunk(GetType(x), "clamp(" + x + ", " + min + ", " + max + ")", false)` |
| `Dot(a,b)` | 二元，结果固定 Float1 | `AddInlinedCodeChunk(EValueType::Float1, "dot(" + a + ", " + b + ")")` |
| `Cross(a,b)` | 二元，结果固定 Float3 | `AddCodeChunk(EValueType::Float3, "cross(" + a + ", " + b + ")", false)` |
| `Normalize(x)` | 一元，类型同输入 | `AddInlinedCodeChunk(GetType(x), "normalize(" + x + ")")` |
| `Length(x)` | 一元，结果 Float1 | `AddInlinedCodeChunk(EValueType::Float1, "length(" + x + ")")` |

（表里 `a`/`x` 等代表 `GetParameterCode(对应索引)`，写代码时展开）

---

## 第四部分：MaterialCompiler（对照 UE `FHLSLMaterialTranslator`）

### 文件：`src/Compiler/Public/MaterialCompiler.h`（扩展版 API）

完整 API（对照 UE `FHLSLMaterialTranslator` 的算子集）。关键扩展点：
- `AddCodeChunk` vs `AddInlinedCodeChunk` 分离（对照 UE）
- `Constant2/3/4` 改走 `AddConstantChunk`（向量也折叠）
- 常量查询返回 `ConstValue`（variant）

```cpp
class MaterialCompiler {
public:
    struct CompileResult {
        bool success = false;
        std::string hlsl_code;
        std::string error_message;
    };

    CompileResult Compile(Graph* graph);

    // === 算子 API（图节点 Compile 里调用）===
    int32_t CompileInputPin(Node* node, const std::string& pin_name);

    // 算术（三段判定 + 向量折叠）
    int32_t Add(int32_t a, int32_t b);
    int32_t Subtract(int32_t a, int32_t b);
    int32_t Multiply(int32_t a, int32_t b);
    int32_t Divide(int32_t a, int32_t b);
    int32_t Power(int32_t base, int32_t exp);
    int32_t Lerp(int32_t a, int32_t b, int32_t alpha);
    int32_t Clamp(int32_t x, int32_t minVal, int32_t maxVal);
    int32_t Abs(int32_t x);
    int32_t Negate(int32_t x);

    // 三角函数
    int32_t Sine(int32_t x);
    int32_t Cosine(int32_t x);

    // 向量运算
    int32_t Dot(int32_t a, int32_t b);
    int32_t Cross(int32_t a, int32_t b);
    int32_t Normalize(int32_t x);
    int32_t Length(int32_t x);

    // 常量——全部走常量路径（扩展版：Constant3 也折叠）
    int32_t Constant(float v);                    // Float1 常量
    int32_t Constant2(float x, float y);          // Float2 常量（走 AddConstantChunk）
    int32_t Constant3(float x, float y, float z); // Float3 常量（走 AddConstantChunk）
    int32_t Constant4(float x, float y, float z, float w);

    // 向量操作 / 纹理 / 控制 / 类型转换（同初版）
    int32_t ComponentMask(int32_t input, bool r, bool g, bool b, bool a);
    int32_t AppendVector(int32_t a, int32_t b);
    int32_t TextureCoordinate();
    int32_t TextureSample(int32_t texture, int32_t coordinate);
    int32_t If(int32_t condition, int32_t trueVal, int32_t falseVal);
    int32_t Cast(int32_t code, EValueType target_type);

    // === 查询（常量值返回 variant）===
    std::string GetParameterCode(int32_t index) const;
    EValueType  GetType(int32_t index) const;
    bool        IsConstant(int32_t index) const;
    ConstValue  GetConstantValue(int32_t index) const;   // ← 返回 variant（不再是 float）

private:
    // 代码块管理——区分内联 vs 非内联（对照 UE AddCodeChunk/AddInlinedCodeChunk）
    int32_t AddCodeChunk(EValueType type, const std::string& code, bool is_inline = false);
    int32_t AddInlinedCodeChunk(EValueType type, const std::string& code);  // 零成本操作专用
    int32_t AddConstantChunk(EValueType type, const ConstValue& value);     // ← 接收 variant
    std::string MakeSymbolName();

    int32_t ParseDefaultValue(const nlohmann::json& val, EValueType type);
    std::vector<int32_t> CompileExpression(Node* node);
    std::string GenerateCode(const std::map<std::string, int32_t>& outputs);

    // 状态
    std::vector<CodeChunk> chunks_;
    std::map<uint64_t, int32_t> hash_to_chunk_;     // hash 去重
    std::map<std::string, std::vector<int32_t>> node_cache_;
    int32_t next_symbol_index_ = 0;
    Graph* current_graph_ = nullptr;
    std::string error_message_;
};
```

### 关键实现要点

#### 1. `AddCodeChunk` vs `AddInlinedCodeChunk`（对照 UE）

UE 严格区分这两个（`HLSLMaterialTranslator.h:806-811`）：

| 方法 | 用途 | 生成 | 例子 |
|------|------|------|------|
| `AddCodeChunk`（非内联）| 会产生**真实指令**的表达式 | `float3 Local0 = ...;` 局部变量声明 | `tex2D(...)`、复杂函数调用 |
| `AddInlinedCodeChunk` | **零成本操作** | 直接嵌入使用处，无变量声明 | `mask`（`v.xy`）、`append`、简单算术 |

> UE 注释原话（`.h:795-800`）："Creating local variables instead of inlining simplifies the generated code... Making compiles faster and enabling the shader optimizer to do a better job."（复杂表达式声明局部变量，让生成代码清晰、shader 编译快、优化器效果好）

教学版照搬这个区分——`Add`/`Sub` 等简单算术用 `AddInlinedCodeChunk`（生成 `a + b` 嵌入），`TextureSample`/`Power` 等用 `AddCodeChunk`（生成局部变量）。

#### 2. `Constant2/3/4` 走常量路径（扩展版核心）

初版 `Constant3` 用 `AddCodeChunk(... "float3(x,y,z)" ...)`——只是 code 字符串，`is_constant=false`，不能折叠。

扩展版改走 `AddConstantChunk`，存 variant 常量值：

```cpp
int32_t Constant3(float x, float y, float z) {
    return AddConstantChunk(EValueType::Float3, Vec3(x, y, z));  // ← 存 Vec3，is_constant=true
}
```

这样 `Add(Constant3(...), Constant3(...))` 能折叠（两边都 `is_constant`，variant 逐分量算）。

#### 3. 代码块管理（完整实现）

```cpp
// 生成局部变量名：Local0, Local1, ...
std::string MaterialCompiler::MakeSymbolName() {
    return "Local" + std::to_string(next_symbol_index_++);
}

// 通用代码块：产生真实指令的表达式用（函数调用、纹理采样、复杂算术）。
// 非内联时生成 "float3 Local0 = ...;" 声明；内联时不声明，直接嵌入使用处
int32_t MaterialCompiler::AddCodeChunk(EValueType type, const std::string& code, bool is_inline) {
    uint64_t hash = HashString(code);
    auto it = hash_to_chunk_.find(hash);
    if (it != hash_to_chunk_.end()) return it->second;   // hash 命中 → 复用（去重）

    CodeChunk chunk;
    chunk.hash = hash;
    chunk.code = code;
    chunk.type = type;
    chunk.is_inline = is_inline;
    if (!is_inline) chunk.symbol_name = MakeSymbolName();   // 非内联才需要变量名
    chunks_.push_back(chunk);
    int32_t index = (int32_t)chunks_.size() - 1;
    hash_to_chunk_[hash] = index;
    return index;
}

// 内联代码块（零成本操作专用：mask/append/简单算术）—— AddCodeChunk 的 inline=true 简写
int32_t MaterialCompiler::AddInlinedCodeChunk(EValueType type, const std::string& code) {
    return AddCodeChunk(type, code, /*is_inline=*/true);
}

// 常量代码块：存 variant 值，is_constant=true，参与常数折叠
int32_t MaterialCompiler::AddConstantChunk(EValueType type, const ConstValue& value) {
    std::string code = FormatConstantCode(value);          // 生成 "0.0"/"1.0"/"float3(...)" 等
    uint64_t hash = HashString("const_" + code);           // "const_" 前缀：防和普通 chunk 碰撞
    auto it = hash_to_chunk_.find(hash);
    if (it != hash_to_chunk_.end()) return it->second;

    CodeChunk chunk;
    chunk.hash = hash;
    chunk.code = code;
    chunk.type = type;
    chunk.is_inline = true;          // 常量总是内联（短）
    chunk.is_constant = true;        // 标记为常量（折叠判定用）
    chunk.constant_value = value;    // ← variant 存储
    chunks_.push_back(chunk);
    int32_t index = (int32_t)chunks_.size() - 1;
    hash_to_chunk_[hash] = index;
    return index;
}

// 把 variant 常量格式化成 HLSL 串。标量 0/1 特判（生成的 HLSL 干净，不是 "0.000000"）
std::string MaterialCompiler::FormatConstantCode(const ConstValue& value) {
    if (std::holds_alternative<float>(value)) {
        float f = std::get<float>(value);
        if (f == 0.0f) return "0.0";
        if (f == 1.0f) return "1.0";
        return std::to_string(f);
    }
    if (std::holds_alternative<Vec2>(value)) {
        Vec2 v = std::get<Vec2>(value);
        return "float2(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
    }
    if (std::holds_alternative<Vec3>(value)) {
        Vec3 v = std::get<Vec3>(value);
        return "float3(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
    }
    if (std::holds_alternative<Vec4>(value)) {
        Vec4 v = std::get<Vec4>(value);
        return "float4(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ", " + std::to_string(v.w) + ")";
    }
    return "0.0";   // 兜底
}
```

> `FormatConstantCode` 是 `MaterialCompiler` 的私有 helper，要在 `MaterialCompiler.h` 的 private 段声明：`std::string FormatConstantCode(const ConstValue& value);`

#### 4. hash 去重（对照 UE 双层去重）

教学版用**单层 hash 去重**（`hash_to_chunk_` map）：相同 code 的 chunk 复用索引。

> **UE 是双层**（`HLSLMaterialTranslator.cpp:4187-4265`）：① HLSL 串 hash 去重（减少局部变量）；② `UniformExpression::IsIdentical()` 去重（让多个材质属性共享同一 preshader）。教学版只做①，因为没 preshader。

#### 5. `GenerateCode`（简化版，课8 完善为完整着色器）

把 `chunks_` 拼成 HLSL 文本。简化版只输出**变量声明 + 材质属性赋值注释**；课8 扩展为完整着色器（cbuffer / VS / PS）。

```cpp
std::string MaterialCompiler::GenerateCode(const std::map<std::string, int32_t>& outputs) {
    std::string out;

    // 1. 非内联 chunk 生成局部变量声明：float3 Local0 = ...;
    for (const auto& chunk : chunks_) {
        if (chunk.is_inline) continue;   // 内联的不声明
        out += "    " + std::string(TypeSystem::ToHLSLType(chunk.type)) + " " + chunk.symbol_name
             + " = " + chunk.code + ";\n";
    }

    // 2. 材质属性赋值（注释形式，课8 接真实 PS 输出）
    out += "\n    // Material outputs\n";
    for (const auto& [name, idx] : outputs) {
        out += "    // " + name + " = " + GetParameterCode(idx) + "\n";
    }
    return out;
}
```

> **注意**：`GenerateCode` 在 `MaterialCompiler.h` 里是 **private**（它是 `Compile` 的内部步骤）。课6 测试要直接调它看 HLSL，需**临时把它挪到 public 段**（或加一个 public 测试入口），等课9 用 `Compile(Graph)` 端到端后改回 private。

#### 6. 查询方法（完整实现）

算子和折叠都依赖这四个查询：

```cpp
std::string MaterialCompiler::GetParameterCode(int32_t index) const {
    if (index < 0 || index >= (int32_t)chunks_.size()) return "0.0";
    const auto& c = chunks_[index];
    return c.is_inline ? c.code : c.symbol_name;   // 内联返回代码串，非内联返回变量名
}
EValueType MaterialCompiler::GetType(int32_t index) const {
    if (index < 0 || index >= (int32_t)chunks_.size()) return EValueType::Unknown;
    return chunks_[index].type;
}
bool MaterialCompiler::IsConstant(int32_t index) const {
    if (index < 0 || index >= (int32_t)chunks_.size()) return false;
    return chunks_[index].is_constant;
}
ConstValue MaterialCompiler::GetConstantValue(int32_t index) const {
    if (index < 0 || index >= (int32_t)chunks_.size()) return 0.0f;   // 越界返回 float 0
    return chunks_[index].constant_value;   // ← 返回 variant
}
```

#### 7. 递归编译（`Compile` / `CompileInputPin` / `CompileExpression`）

这是把 Graph 翻译成 HLSL 的主流程。`Compile` 入口 → 遍历输出节点的引脚 → `CompileInputPin` 递归编译上游 → `CompileExpression` 调用节点的 `Expression::Compile`。

```cpp
MaterialCompiler::CompileResult MaterialCompiler::Compile(Graph* graph) {
    CompileResult result;
    current_graph_ = graph;
    chunks_.clear();
    hash_to_chunk_.clear();
    node_cache_.clear();
    next_symbol_index_ = 0;
    error_message_.clear();

    Node* output = graph->GetOutputNode();
    if (!output) { result.success = false; result.error_message = "No output node"; return result; }

    std::map<std::string, int32_t> outputs;
    for (const auto& pin : output->inputPins) {
        if (pin.IsConnected()) {
            int32_t idx = CompileInputPin(output, pin.name);
            if (idx < 0) { result.success = false; result.error_message = error_message_; return result; }
            outputs[pin.name] = idx;
        }
    }
    result.hlsl_code = GenerateCode(outputs);
    result.success = true;
    return result;
}

int32_t MaterialCompiler::CompileInputPin(Node* node, const std::string& pin_name) {
    const Pin* pin = node->FindInputPin(pin_name);
    if (!pin) return Constant(0.0f);                          // 引脚不存在 → 默认 0
    if (!pin->IsConnected()) return ParseDefaultValue(pin->defaultValue, pin->type);  // 未连接 → 用默认值

    const auto& conn = pin->connections[0];                   // 输入引脚最多一个连接
    Node* upstream = current_graph_->FindNode(conn.otherNodeId);
    if (!upstream) return Constant(0.0f);

    auto outputs = CompileExpression(upstream);               // 递归编译上游
    const Pin* src = upstream->FindPin(conn.otherPinId);      // 找上游输出引脚
    if (!src) return Constant(0.0f);
    int pinIdx = 0;
    for (int i = 0; i < (int)upstream->outputPins.size(); ++i)
        if (upstream->outputPins[i].id == src->id) { pinIdx = i; break; }
    if (pinIdx < (int)outputs.size()) return outputs[pinIdx];
    return Constant(0.0f);
}

std::vector<int32_t> MaterialCompiler::CompileExpression(Node* node) {
    // 缓存：同一节点不重复编译
    std::string key = node->id.ToString();
    auto it = node_cache_.find(key);
    if (it != node_cache_.end()) return it->second;

    std::vector<int32_t> outputs;
    // TODO(课7)：通过 ExpressionRegistry 按 node->typeName 获取 Expression 实例，
    //            调用 expr->Compile(this, node)，把返回的索引填进 outputs。
    // 课6 阶段还没接表达式注册，先报错：
    error_message_ = "Expression not registered: " + node->typeName;

    node_cache_[key] = outputs;
    return outputs;
}
```

> **`ParseDefaultValue`**（未连接引脚的默认值解析，如 `0.5` → `Constant(0.5)`、`[1,0,0]` → `Constant3`）实现按 `EValueType` 用 `nlohmann::json` 的 `is_number()` / `is_array()` 分发到对应的 `Constant/2/3/4`。Pin 的 `defaultValue` 字段本身是 json 类型，直接传进来即可。

> **课6 端到端的限制**：`CompileExpression` 里的 TODO 意味着**课6 还不能从 Graph 端到端编译**（缺表达式注册，那是课7）。课6 用 `compiler_test.cpp` 直接调算子 API（`Constant`/`Add`/...）+ `GenerateCode` 验证编译器核心。等课7 接上表达式注册，才能 `Compile(graph)` 跑通整条管线。

---

## 第五部分：实现步骤（按依赖顺序）

边写边用 `compiler_test.cpp` 验证，每实现一块放开对应测试：

1. **类型系统**：`Types.h` 扩展 `EValueType`；`TypeSystem.h` 推导规则
2. **CodeChunk**：variant `constant_value` + 各字段
3. **代码块管理**：`MakeSymbolName` / `AddCodeChunk` / `AddInlinedCodeChunk` / `AddConstantChunk`（variant 版）/ hash 去重
4. **常量**：`Constant/2/3/4` 全走 `AddConstantChunk`
5. **查询**：`GetType` / `IsConstant` / `GetConstantValue`（返回 variant）/ `GetParameterCode`
6. **算术**：`Add/Sub/Mul/Div` 三段判定 + 向量折叠 + 代数化简
7. **其他算子**：`Power/Lerp/Clamp/Abs/Sine/Cosine/Dot/Cross/Normalize/Length/ComponentMask/AppendVector/Cast`
8. **纹理/控制**：`TextureCoordinate/TextureSample/If`（占位，课14 接 DX12）
9. **递归编译**：`Compile` / `CompileInputPin` / `CompileExpression`（`CompileExpression` 的 TODO 课7 接表达式注册后才能端到端）
10. **代码生成**：`GenerateCode`（简化版，课8 完善为完整着色器）

---

## 第六部分：测试（`compiler_test.cpp`）

测试覆盖扩展版的关键能力（特别是向量折叠，这是初版做不到的）：

```cpp
// 1. 标量折叠（初版就有）
int s = c.Add(c.Constant(1.0f), c.Constant(2.0f));
assert(c.GetConstantValue(s) == ConstValue(3.0f));

// 2. 向量折叠（扩展版新增，初版做不到）
int a = c.Constant3(1,0,0);
int b = c.Constant3(0,1,0);
int v = c.Add(a, b);
assert(std::get<Vec3>(c.GetConstantValue(v)) == Vec3(1,1,0));

// 3. Multiply 优化（乘1直通、乘0归零）
assert(c.Multiply(x, c.Constant(1.0f)) == x);   // 直通
assert(std::get<float>(c.GetConstantValue(c.Multiply(x, c.Constant(0.0f)))) == 0.0f);

// 4. 除零保护
assert(/* Divide(x, Constant(0)) 安全返回 0，不崩 */);

// 5. 嵌套运算（2*3+4 → 10）
int n = c.Add(c.Multiply(c.Constant(2.0f), c.Constant(3.0f)), c.Constant(4.0f));
assert(std::get<float>(c.GetConstantValue(n)) == 10.0f);

// 6. HLSL 生成（GenerateCode 输出，需临时改 public）
std::map<std::string,int32_t> outputs = {{"BaseColor", c.Constant3(1,0.5,0)}};
std::cout << c.GenerateCode(outputs);
```

---

## 第七部分：UE 5.7 参考（相对 `Engine/` 路径）

| 本课概念 | UE 对应 | 位置 |
|---------|---------|------|
| `CodeChunk` | `FShaderCodeChunk` | `Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.h:83-174` |
| `EValueType` | `EMaterialValueType`（64 位 bitmask）| `Source/Runtime/Engine/Public/MaterialValueType.h` |
| `MaterialCompiler` 算子 | `FHLSLMaterialTranslator::Add/Sub/Mul/Div` | `Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp:9849-10060` |
| `Constant/2/3/4` | `FHLSLMaterialTranslator::Constant/2/3/4` | 同上 `.cpp:5598-5630` |
| `AddCodeChunk`/`AddInlinedCodeChunk` | 同名（UE 也是这两个）| 同上 `.cpp:3880-4280` |
| 常量折叠 variant（教学版简化）| `FMaterialUniformExpression` 表达式树 + preshader | `Source/Runtime/Engine/Private/Materials/MaterialUniformExpressions.h` |
| 双向递归 | `CompilePropertyAndSetMaterialProperty` + `UMaterialExpression::Compile` | `Source/Runtime/Engine/Private/Materials/Material.cpp:186` |
| 类型推导 | `GetArithmeticResultType` | `HLSLMaterialTranslator.cpp:4612-4663` |

**搜索关键词**（在 UE 源码里）：`FShaderCodeChunk`、`FHLSLMaterialTranslator::Add`、`AddCodeChunkInner`、`IsExpressionConstantValue`、`EMaterialValueType`。

---

## 完成标志

- [ ] `EValueType` 扩展（Float/Int/Matrix/Texture/Sampler）+ `TypeSystem` 推导
- [ ] `CodeChunk` 用 variant `constant_value`
- [ ] `AddConstantChunk` 接收 variant，`Constant2/3/4` 走常量路径
- [ ] `Add/Sub/Mul/Div` 三段判定 + 向量折叠 + 代数化简
- [ ] `AddCodeChunk` vs `AddInlinedCodeChunk` 区分
- [ ] `compiler_test.cpp` 向量折叠断言通过（`Add(Constant3(...), Constant3(...))` 编译期算）
- [ ] 编译通过（`CompileExpression` 的 TODO 课7 接表达式注册后端到端）

---

## 核心原则回顾

1. **一步到位**：variant CodeChunk + 完整类型 + 向量折叠，直接做扩展版，不做"先标量后向量"的中间态
2. **对照 UE**：每个设计点知道 UE 怎么做、教学版为什么简化（variant vs 表达式树、enum vs bitmask、单层 vs 双层去重）
3. **保留核心语义**：hash 去重、"是否常量"判定、双向递归——这些和 UE 一致，简化的是"实现手段"不是"设计思想"
