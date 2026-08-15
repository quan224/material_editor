# 课6：编译器核心（完整版，对照 UE5）

## 目标

实现 `MaterialCompiler` 主类 + `CodeChunk` 数据结构 + 常数折叠 + 编译期错误收集，把材质图（Graph）的算子调用翻译成 HLSL 代码片段。对标 UE5 的 `FHLSLMaterialTranslator`——这是整个项目**最核心最复杂**的部分。

**本课范围（自给自足，不挖坑）**：
- 类型系统扩展（Float/Int/Matrix/Texture/Sampler）+ 推导规则
- `CodeChunk` 数据结构，常量值用 `std::variant<float, Vec2, Vec3, Vec4>` 支持向量折叠
- 常数折叠（标量 + 向量）+ 代数化简（`x*1`→`x` 等）
- `MaterialCompiler` 算子 API：算术 / 三角 / 向量 / 标量常量 / 类型转换（共 ~20 个算子）
- `chunks_` 数组管理 + hash 去重 + 查询函数（带边界保护）
- **错误诊断编译器层**：`CompileError` 结构 + `EmitError` 收集器 + 类型不匹配/除零两类检查（贯穿算子）

**本课不做、留给后续课第一次实现的部分**（明确边界，不在正文挖坑）：
- **端到端编译** `Compile(Graph*)` + `CompileInputPin` + `CompileExpression` → **课 7** 接 `ExpressionRegistry` 后第一次实现
- **图层级错误检查**：循环依赖 / 必需引脚未连 → **课 7**（依赖 `Compile(Graph*)` 主流程）
- **HLSL 完整生成** `GenerateCode` → **课 8**（接 cbuffer / VS / PS 完整着色器模板）
- **纹理 / 控制流算子** `TextureCoordinate` / `TextureSample` / `If` → **课 14/15**（接 DX12 上下文）
- **错误诊断 UI 层**（节点高亮 / 引脚标红 / 错误面板 / 点击跳转）→ **课 19**

每个设计点都对照 UE5 真实实现（相对 `Engine/` 路径），讲清楚"UE 怎么做、教学版为什么这样简化"。

---

## 背景知识

### 编译器做什么

接收一个 `Graph`，从输出节点反向遍历，对每个节点调用其 `Expression::Compile()`，生成一系列"代码块"（`CodeChunk`），最后组装成 HLSL 着色器。

### 完整编译流程（标出本课实现的部分）

```
MaterialCompiler::Compile(Graph*)               ← 课7 实现
  │
  ├─ 0. 环检测（DFS 三色标记）                    ← 课7 实现
  │     有环 → EmitError("Cycle: A → B → C → A")
  │
  ├─ 1. 获取输出节点（材质根）
  ├─ 2. 必需引脚检查                              ← 课7 实现
  │     MaterialOutput.BaseColor 未连 → EmitError
  │
  ├─ 3. 对每个输入引脚 CompileInputPin             ← 课7 实现
  │     CompileInputPin(outputNode, "BaseColor")
  │       │
  │       ├─ 找到上游节点
  │       ├─ CompileExpression(upstreamNode)      ← 课7 实现（接 ExpressionRegistry）
  │       │     │
  │       │     ├─ 上游 Expression::Compile(this)
  │       │     │    ├─ CompileInputPin() ← 递归
  │       │     │    ├─ compiler->Add(a, b) ← ★ 本课实现
  │       │     │    └─ 返回输出引脚的代码块索引
  │       │     │
  │       │     └─ 缓存结果
  │
  └─ 4. GenerateCode() 组装 HLSL                 ← 课8 实现
```

**本课（课6）只实现★标注的算子 API 层**——`compiler->Add/Sub/Mul/Div/Constant/...` + 支撑它们的数据结构（`chunks_`、`CompileError`、`EmitError`）。课 6 的测试 `compiler_test.cpp` **直接调算子 API**，不走 `Compile(Graph*)`。

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
    return compiler->Add(a, b);                      // 组合（本课实现的算子）
```

> **UE 对照**：UE 也是这套——`FHLSLMaterialTranslator` 只提供算子，`UMaterialExpression` 子类的 `Compile()` 驱动递归。见 `Engine/Source/Runtime/Engine/Private/Materials/Material.cpp` 的 `CompilePropertyAndSetMaterialProperty`，它触发具体节点的 `Compile`，节点又调 `Compiler->Add/Sub/...`。

### CodeChunk 是什么

每一行 HLSL 代码 = 一个 CodeChunk，编译器用 `int32_t` 索引引用：

```hlsl
float Local0 = 1.0;              // CodeChunk 0: 常量
float Local1 = 0.5;              // CodeChunk 1: 常量
float Local2 = Local0 + Local1;  // CodeChunk 2: Add 的结果，引用 0 和 1
```

`Add(0, 1)` 返回 `2`（索引），下游用 `2` 引用结果。**`int32_t` 是 `chunks_` 数组的下标，不是数值**。负数段（`-1` 等）作为 **`INDEX_NONE` 哨兵**——上游算子出错（类型不匹配等）时返回 -1，下游算子的 `if (a < 0 || b < 0) return -1;` 会**沿依赖链传播错误**，但**不重复 EmitError**（同一个错误只报一次，靠 `SameAs` 去重）。

---

## 第一部分：类型系统（对照 UE `EMaterialValueType`）

### 文件：`src/MaterialGraph/Public/Types.h`（扩展 `EValueType`）

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

### 文件：`src/Compiler/Public/CodeChunk.h`

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

    // ⑦ 常量值——核心：variant 支持向量
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
| ⑦ `constant_value` | **常量值（variant）** | 折叠时存实际数值，**能装 Vec2/3/4，向量也能折叠** |
| ⑧ `references` | 依赖追踪 | 记录依赖哪些 chunk，生成代码时按依赖顺序声明 |

### 为什么 `constant_value` 用 `std::variant`

如果 `constant_value` 是单个 `float`，**存不下向量**——所以 `Constant3(1,0,0)` 只能走 `AddCodeChunk` 存成 `"float3(1,0,0)"` 字符串、`is_constant=false`、**不参与折叠**。两个颜色常量相加 `Add(Constant3(1,0,0), Constant3(0,1,0))` 无法编译期算。

改成 `std::variant<float, Vec2, Vec3, Vec4>`，向量也能存、能折叠：

```cpp
// 能这样折叠：
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

## 第三部分：错误诊断基础设施（编译器层）

> 错误诊断分两层：**编译器层**（错误生成 + 收集）留本课；**UI 层**（节点高亮 + 错误面板 + 点击跳转）→ 课 19。本课只做编译器层。
>
> 错误检查分两类：**算子内检查**（类型不匹配 / 除零）留本课，**图层级检查**（循环依赖 / 必需引脚未连）→ 课 7（依赖 `Compile(Graph*)` 主流程）。

### 为什么错误要带节点/pin 定位

**问题**：如果 `CompileResult` 只有一个 `error_message`（一句话），用户看到 "Type mismatch: Float3 vs Float1" 不知道是图里**哪个 Add 节点**的**哪个引脚**出错——只能眼睛挨个扫图。一个稍大的材质图可能有十几个 Add，这种错误信息几乎没有可操作性。

**正确做法**：每个错误携带 `{nodeId, pinName}`，编辑器（课 19）据此：
- 把对应节点的标题栏按严重级别着色（Error=红 / Warning=黄 / Info=蓝）
- 把具体出错的引脚单独标红（不是整个节点一片红——多个引脚时只标出错的那一个）
- 错误列表里点击一条 → 跳到对应节点 + 选中引脚

**为什么必须支持多错误**：一个图可能同时有多个错误（`MaterialOutput.BaseColor` 没连 + 某个 `Add` 类型不匹配 + 另一个 `Divide` 除零）。一次编译把所有错误都报出来，用户一轮修复，不用"改一个 → 重编译 → 发现下一个"反复横跳。

**对照 UE**：UE 编译错误带 line + node 信息，`HandleMaterialCompilationErrors`（`Engine/Source/Runtime/Engine/Private/Materials/Material.cpp`）把编译器收集的错误回填到 `UMaterialExpression::LastErrorText`，再触发节点 redraw。

### 文件：`src/Compiler/Public/CompileError.h`

```cpp
#pragma once
#include "Core/Public/UUID.h"
#include <string>

// 错误严重级别（决定 UI 着色 + 是否中断编译）
enum class EErrorSeverity {
    Error,    // 编译失败：类型不匹配、循环依赖、必需引脚未连接
    Warning,  // 编译继续：除零保护、隐式窄化（Float3→Float1）
    Info,     // 提示：未使用节点、冗余算子
};

struct CompileError {
    UUID            nodeId   = UUID::Invalid();  // 出错节点（必需——UI 按它高亮、点击跳节点）
    std::string     pinName;                     // 出错引脚名（可空——节点级错误如环检测）
    std::string     message;                     // 用户可读描述（要带上下文）
    EErrorSeverity  severity = EErrorSeverity::Error;

    // 去重 key：同 node + 同 pin + 同 message 视为同一个错误。
    // 递归编译可能多次触发同一检查（同一个节点经多条路径被访问），需去重。
    // 用方法而不是 operator==，避免被误用到别处的相等比较
    bool SameAs(const CompileError& other) const {
        return nodeId == other.nodeId
            && pinName == other.pinName
            && message == other.message;
    }
};
```

**字段说明**：

| 字段 | 为什么需要 |
|------|----------|
| `nodeId` | 编辑器按节点高亮（标题栏染色）、错误列表点击跳节点，都要 nodeId |
| `pinName` | 区分节点级 vs 引脚级错误。环检测是节点级（pinName 空）；类型不匹配是引脚级（pinName="A"/"B"）。引脚级高亮只标出错的那个 pin，不是整节点一片红 |
| `message` | 用户可读描述。要带足够上下文，如 `"Type mismatch on pin 'A': expects Float1 but upstream provides Float3"` |
| `severity` | UI 着色（红/黄/蓝）+ 决定是否中断编译。Error 中断当前分支，Warning/Info 继续 |

**为什么用 enum 而不是 `bool is_warning`**：将来要加 Info（未使用节点提示）、Fatal（编译器内部错误），enum 扩展性更好；debug 时打印枚举名也比看 bool 直观。

### `CompileResult`（直接定义最终版）

```cpp
struct CompileResult {
    bool                        success = false;       // = !HasErrors()
    std::string                 hlsl_code;
    std::string                 error_message;         // 兼容字段：第一个 Error 级错误的 message
    std::vector<CompileError>   errors;                // 所有错误（含 Warning/Info）

    // 便捷查询：是否有 Error 级错误（Warning/Info 不算）
    bool HasErrors() const {
        for (const auto& e : errors)
            if (e.severity == EErrorSeverity::Error) return true;
        return false;
    }
};
```

**`error_message` 保留作为兼容字段**：`Compile()`（课 7 实现）末尾把第一个 Error 的 message 复制到 `error_message`，简化只想知道"出错没、错是什么"的调用方代码。

> **`hlsl_code` 在失败时也填**：即使有 Error，编译器仍可能生成部分 HLSL（错误分支被短路，其他分支正常）。课 8 实现的代码面板可以继续显示这部分代码（红色标错），帮用户对照定位——不要清空。

### 错误收集策略：贯穿编译路径一次加全

**原则**：错误检查**贯穿所有编译路径**，在出错点立即 `EmitError(...)`——不要"先标记、编译结束后回扫补错误"。

**为什么**：

1. **递归编译天然带上下文**：`CompileInputPin(node, "A")`（课 7）入口知道当前在编译哪个节点的哪个引脚。失败时直接拿 `node`/`"A"` emit，**事后回扫反而要在两个地方维护同一套上下文**（编译时一份 + 回扫时一份），极易不一致。

2. **避免遗漏路径**：编译器有十几个算子，每个都有类型检查、除零检查……如果在算子里发错误，每加一个算子自动获得错误报告；如果"事后扫"，每加一个算子都要记得在回扫函数里加对应检查。

3. **多错误一次报全**：每个算子的错误检查独立运行，编译完一轮 `errors_` 自然收集了所有错误。

### 错误短路 vs 继续的策略

| 严重度 | 行为 | 例子 |
|--------|------|------|
| Error（当前算子）| 当前算子**不生成新 chunk**（返回 `INDEX_NONE=-1`），但**不立即停止整个编译** | 类型不匹配：`Add` 返回 -1，下游算子的 `if (a < 0) return -1;` 哨兵会传播 |
| Error（致命）| 立即 return，不继续编译 | 循环依赖（课 7）：无法继续拓扑遍历，整个 `Compile()` 提前结束 |
| Warning | 编译继续（生成兜底 chunk），同时进 `errors_` | 除零：返回 `Constant(0)`，但发一条 Warning |
| Info | 编译继续，无副作用 | 未使用节点提示 |

这套"短路当前算子但继续其他分支"的策略对照 UE 的 `INDEX_NONE` 哨兵传播（`HLSLMaterialTranslator.cpp` 所有算子开头都检查 `if (A < 0 || B < 0) return INDEX_NONE;`）。**关键点**：上游 Error 让下游也"染上"`INDEX_NONE`，但下游**不要重复 EmitError**同一个错误（去重靠 `SameAs`）。

### `EmitError` 收集器 + 上下文成员（MaterialCompiler 私有成员）

```cpp
// MaterialCompiler.h private 段（在第四部分完整定义类时整合进去）
std::vector<CompileError> errors_;        // 所有错误
Node*         current_node_ = nullptr;    // 当前正在编译的节点（EmitError 默认 nodeId 用它）
std::string   current_pin_;               // 当前正在编译的引脚（EmitError 默认 pinName 用它）

void EmitError(const std::string& msg,
               EErrorSeverity sev = EErrorSeverity::Error,
               const UUID& overrideNodeId = UUID::Invalid(),
               const std::string& overridePinName = "");
```

```cpp
// MaterialCompiler.cpp
void MaterialCompiler::EmitError(const std::string& msg, EErrorSeverity sev,
                                  const UUID& overrideNodeId,
                                  const std::string& overridePinName) {
    CompileError err;
    err.message  = msg;
    err.severity = sev;
    // 优先用 override（检查项显式指定），否则用 current_* 上下文
    err.nodeId   = overrideNodeId.IsValid() ? overrideNodeId
                  : (current_node_ ? current_node_->id : UUID::Invalid());
    err.pinName  = !overridePinName.empty() ? overridePinName : current_pin_;

    // 去重：同 node + 同 pin + 同 message 只存一份
    // （递归编译可能多次触发同一检查，如环路上的节点被多条路径访问）
    for (const auto& existing : errors_)
        if (existing.SameAs(err)) return;
    errors_.push_back(err);
}
```

**`current_node_` / `current_pin_` 谁来设**：课 7 实现的 `CompileInputPin` 入口会设 `current_node_ = node; current_pin_ = pin_name;`——这是"贯穿编译路径一次加全"策略的核心机制，让所有下游算子的 EmitError 都能拿到正确的定位上下文，不需要每个算子自己手动传 nodeId。**课 6 阶段**还没接 `CompileInputPin`，所以 `current_node_` 默认为 `nullptr`，算子里 EmitError 时 `nodeId` 会落到 `UUID::Invalid()`——这是正常的，等课 7 接上 `CompileInputPin` 后自动有上下文。`compiler_test.cpp` 测试错误收集时，可以**手动 `compiler.current_node_ = &someNode`**（友元测试）或**显式 overrideNodeId 参数**绕过。

> **调试用 helper**：错误信息里要展示类型名，建议加个 `TypeName(EValueType)` 函数（返回 `"Float1"`/`"Float3"`/...）。可以基于 `TypeSystem::ToHLSLType` 包一层去 `"float"` 前缀，或单独写个 switch。错误信息里的类型名要让人看得懂。

---

## 第四部分：常数折叠 + 算术算子（含 EmitError 集成）

### 文件：`src/Compiler/Public/ConstantFolding.h`

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

        // 除零检查：任意分量 b==0 则不能折叠（交由 Divide 算子处理 EmitError）
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

**关键**：折叠函数接收/返回 `ConstValue`（variant），能处理向量。例如 `FoldBinary("+", Vec3(1,0,0), Vec3(0,1,0))` 逐分量相加得 `Vec3(1,1,0)`。

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

### Add/Sub/Mul/Div 的三段判定（对照 UE，含 EmitError）

UE 的 `FHLSLMaterialTranslator::Add`（`.cpp:9849-9883`）有一套清晰的判定顺序，教学版照搬这个骨架，**段 1 直接带 EmitError**（不是返回 -1 让用户看不到原因）：

```cpp
// === Add：三段判定的模板（带 EmitError）===
int32_t MaterialCompiler::Add(int32_t a, int32_t b) {
    // 段 1：哨兵传播 + 类型检查（带 EmitError）
    if (a < 0 || b < 0) return -1;   // 上游 Error 已发过错误，不重复
    auto resultType = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    if (resultType == EValueType::Unknown) {
        EmitError("Add inputs incompatible: A=" + TypeName(GetType(a))
                  + ", B=" + TypeName(GetType(b)),
                  EErrorSeverity::Error,
                  /*overrideNodeId=*/current_node_ ? current_node_->id : UUID::Invalid(),
                  /*overridePinName=*/"A/B");   // ← 显式 override，算子不知是 A 还是 B 出错
        return -1;
    }

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
    if (resultType == EValueType::Unknown) {
        EmitError("Subtract inputs incompatible: A=" + TypeName(GetType(a))
                  + ", B=" + TypeName(GetType(b)),
                  EErrorSeverity::Error,
                  current_node_ ? current_node_->id : UUID::Invalid(), "A/B");
        return -1;
    }

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
    if (resultType == EValueType::Unknown) {
        EmitError("Multiply inputs incompatible: A=" + TypeName(GetType(a))
                  + ", B=" + TypeName(GetType(b)),
                  EErrorSeverity::Error,
                  current_node_ ? current_node_->id : UUID::Invalid(), "A/B");
        return -1;
    }

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

// === Divide：含除零保护（Warning）+ x/1→x 化简 ===
int32_t MaterialCompiler::Divide(int32_t a, int32_t b) {
    if (a < 0 || b < 0) return -1;
    auto resultType = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    if (resultType == EValueType::Unknown) {
        EmitError("Divide inputs incompatible: A=" + TypeName(GetType(a))
                  + ", B=" + TypeName(GetType(b)),
                  EErrorSeverity::Error,
                  current_node_ ? current_node_->id : UUID::Invalid(), "A/B");
        return -1;
    }

    // 除零保护：b 是常量 0 → 编译继续，但发 Warning（UI 才能高亮除数引脚）
    if (IsConstant(b) && ConstantFolding::IsScalarZero(GetConstantValue(b))) {
        EmitError("Division by zero: divisor (pin 'B') is constant 0",
                  EErrorSeverity::Warning,
                  current_node_ ? current_node_->id : UUID::Invalid(),
                  /*overridePinName=*/"B");   // ← 精确到除数引脚
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
1. **错误检查**：`INDEX_NONE`（负数）传播 + 类型推导失败 → `EmitError`——上游失败不继续算，类型不匹配发错误带定位
2. **常数折叠**：两边都常量 → 编译期算，生成扁平常量 chunk
3. **HLSL 发射**：非常量 → 生成代码串 chunk

**为什么除零是 Warning 不是 Error**：除零在 HLSL 里行为未定义，但我们用 `Constant(0)` 兜底，shader 仍能编译运行——用户应该修但不应阻塞编译。这是"软错误"策略，对照 UE 也有很多 Warning 级编译问题不阻塞（如未使用的输入、隐式转换）。

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

## 第五部分：MaterialCompiler 主类（完整定义）

### 文件：`src/Compiler/Public/MaterialCompiler.h`

完整 API（对照 UE `FHLSLMaterialTranslator` 的算子集）：
- `AddCodeChunk` vs `AddInlinedCodeChunk` 分离（对照 UE）
- `Constant2/3/4` 走 `AddConstantChunk`（向量也折叠）
- 常量查询返回 `ConstValue`（variant）
- **`CompileResult` 直接定义带 errors 数组的最终版**
- **`errors_` / `current_node_` / `current_pin_` / `EmitError` 直接定义**（错误收集是算子的标配基础设施，不是后插补丁）

> **本课 API 列表不含** `Compile(Graph*)` / `CompileInputPin` / `CompileExpression` / `GenerateCode` / `TextureCoordinate` / `TextureSample` / `If`——这些分别在课 7/8/14 实现，本课完全不做（不在正文挖"占位"）。

```cpp
#pragma once
#include <map>
#include <vector>
#include <string>
#include "MaterialGraph/Public/Graph.h"
#include "Compiler/Public/CodeChunk.h"
#include "Compiler/Public/ConstantFolding.h"
#include "Compiler/Public/CompileError.h"
#include "Compiler/Public/TypeSystem.h"
#include "MaterialGraph/Public/Types.h"
#include "Core/Public/Hash.h"

class MaterialCompiler {
public:
    // === 编译结果（带 errors 数组的最终版）===
    struct CompileResult {
        bool                        success = false;       // = !HasErrors()
        std::string                 hlsl_code;
        std::string                 error_message;         // 兼容字段：第一个 Error 的 message
        std::vector<CompileError>   errors;                // 所有错误（含 Warning/Info）

        bool HasErrors() const {
            for (const auto& e : errors)
                if (e.severity == EErrorSeverity::Error) return true;
            return false;
        }
    };

    // Compile(Graph*) / CompileInputPin / CompileExpression → 课7 实现
    // GenerateCode → 课8 实现

    // === 算子 API（图节点 Compile 里调用；本课测试直接调）===

    // 算术（三段判定 + 向量折叠 + EmitError）
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

    // 常量——全部走常量路径（向量也折叠）
    int32_t Constant(float v);                    // Float1 常量
    int32_t Constant2(float x, float y);          // Float2 常量（走 AddConstantChunk）
    int32_t Constant3(float x, float y, float z); // Float3 常量（走 AddConstantChunk）
    int32_t Constant4(float x, float y, float z, float w);

    // 向量操作 / 类型转换
    int32_t ComponentMask(int32_t input, bool r, bool g, bool b, bool a);
    int32_t AppendVector(int32_t a, int32_t b);
    int32_t Cast(int32_t code, EValueType target_type);

    // TextureCoordinate / TextureSample / If → 课14/15 实现

    // === 查询（常量值返回 variant；带边界保护）===
    std::string GetParameterCode(int32_t index) const;
    EValueType  GetType(int32_t index) const;
    bool        IsConstant(int32_t index) const;
    ConstValue  GetConstantValue(int32_t index) const;   // ← 返回 variant

    // === 错误访问（供测试 + 课19 UI 层读取）===
    const std::vector<CompileError>& GetErrors() const { return errors_; }

private:
    // === 代码块管理 ===
    int32_t AddCodeChunk(EValueType type, const std::string& code, bool is_inline = false);
    int32_t AddInlinedCodeChunk(EValueType type, const std::string& code);  // 零成本操作专用
    int32_t AddConstantChunk(EValueType type, const ConstValue& value);     // ← 接收 variant
    std::string FormatConstantCode(const ConstValue& value);
    std::string MakeSymbolName();

    // ParseDefaultValue → 课7 在 CompileInputPin 里实现（解析未连接引脚的默认值）

    // === 错误收集 ===
    std::vector<CompileError> errors_;
    Node*         current_node_ = nullptr;    // 当前正在编译的节点（课7 的 CompileInputPin 设置）
    std::string   current_pin_;               // 当前正在编译的引脚（课7 的 CompileInputPin 设置）

    void EmitError(const std::string& msg,
                   EErrorSeverity sev = EErrorSeverity::Error,
                   const UUID& overrideNodeId = UUID::Invalid(),
                   const std::string& overridePinName = "");

    // === 状态 ===
    std::vector<CodeChunk> chunks_;
    std::map<uint64_t, int32_t> hash_to_chunk_;     // hash 去重
    // node_cache_ → 课7 在 CompileExpression 里用
    int32_t next_symbol_index_ = 0;
    Graph* current_graph_ = nullptr;
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

教学版照搬这个区分——`Add`/`Sub` 等简单算术用 `AddInlinedCodeChunk`（生成 `a + b` 嵌入），`Power` 等用 `AddCodeChunk`（生成局部变量）。`TextureSample` 同样会用 `AddCodeChunk`（课 14 实现）。

#### 2. `Constant2/3/4` 走常量路径（核心）

如果 `Constant3` 用 `AddCodeChunk(... "float3(x,y,z)" ...)`——只是 code 字符串，`is_constant=false`，不能折叠。

走 `AddConstantChunk` 存 variant 常量值：

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

// 通用代码块：产生真实指令的表达式用（函数调用、复杂算术）。
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

#### 4. hash 去重（对照 UE 双层去重）

教学版用**单层 hash 去重**（`hash_to_chunk_` map）：相同 code 的 chunk 复用索引。

> **UE 是双层**（`HLSLMaterialTranslator.cpp:4187-4265`）：① HLSL 串 hash 去重（减少局部变量）；② `UniformExpression::IsIdentical()` 去重（让多个材质属性共享同一 preshader）。教学版只做①，因为没 preshader。

#### 5. 查询方法（完整实现，带边界保护）

算子和折叠都依赖这四个查询。**`assert` 在 Debug 下捕获调用方越界 bug**，越界返回安全的兜底值（不让一条坏索引导致连续崩溃）：

```cpp
std::string MaterialCompiler::GetParameterCode(int32_t index) const {
    assert(index >= 0 && index < (int32_t)chunks_.size());
    if (index < 0 || index >= (int32_t)chunks_.size()) return "0.0";
    const auto& c = chunks_[index];
    return c.is_inline ? c.code : c.symbol_name;   // 内联返回代码串，非内联返回变量名
}
EValueType MaterialCompiler::GetType(int32_t index) const {
    assert(index >= 0 && index < (int32_t)chunks_.size());
    if (index < 0 || index >= (int32_t)chunks_.size()) return EValueType::Unknown;
    return chunks_[index].type;
}
bool MaterialCompiler::IsConstant(int32_t index) const {
    assert(index >= 0 && index < (int32_t)chunks_.size());
    if (index < 0 || index >= (int32_t)chunks_.size()) return false;
    return chunks_[index].is_constant;
}
ConstValue MaterialCompiler::GetConstantValue(int32_t index) const {
    assert(index >= 0 && index < (int32_t)chunks_.size());
    if (index < 0 || index >= (int32_t)chunks_.size()) return 0.0f;   // 越界返回 float 0
    return chunks_[index].constant_value;   // ← 返回 variant
}
```

> 为什么 `assert` + `if` 双保险：`assert` 是开发期断言（捕获调用方 bug，触发调试器中断）；`if` 是 Release 期的兜底（避免一条坏索引让整个编译崩溃。下游算子拿到兜底值还能继续编译，错误集中报告而不是连环崩溃）。

---

## 第六部分：实现步骤（按依赖顺序）

边写边用 `compiler_test.cpp` 验证，每实现一块放开对应测试：

1. **类型系统**：`Types.h` 扩展 `EValueType`；`TypeSystem.h` 推导规则
2. **CompileError**：`CompileError.h` 定义 `CompileError` / `EErrorSeverity` 结构（无实现，纯数据）
3. **CodeChunk**：variant `constant_value` + 各字段
4. **MaterialCompiler 类骨架**：状态成员 + `errors_` / `current_node_` / `current_pin_` + `EmitError`（带 `SameAs` 去重）+ `CompileResult`（带 errors 数组 + `HasErrors`）
5. **代码块管理**：`MakeSymbolName` / `AddCodeChunk` / `AddInlinedCodeChunk` / `AddConstantChunk`（variant 版）/ hash 去重 / `FormatConstantCode`
6. **查询**：`GetType` / `IsConstant` / `GetConstantValue`（返回 variant）/ `GetParameterCode`（带 assert + 边界保护）
7. **常量**：`Constant/2/3/4` 全走 `AddConstantChunk`
8. **ConstantFolding**：`FoldBinary` / `FoldUnary` / `IsScalarZero` / `IsScalarOne`（variant 版）
9. **算术（带 EmitError）**：`Add/Sub/Mul/Div` 三段判定 + 向量折叠 + 代数化简 + 类型不匹配 EmitError + 除零 Warning
10. **其他算子**：`Power/Lerp/Clamp/Abs/Negate/Sine/Cosine/Dot/Cross/Normalize/Length/ComponentMask/AppendVector/Cast`

---

## 第七部分：测试（`compiler_test.cpp`）

测试覆盖完整版的关键能力（特别是向量折叠 + 错误收集）：

```cpp
// 1. 标量折叠
int s = c.Add(c.Constant(1.0f), c.Constant(2.0f));
assert(c.IsConstant(s));
assert(std::get<float>(c.GetConstantValue(s)) == 3.0f);

// 2. 向量折叠（完整版核心）
int a = c.Constant3(1,0,0);
int b = c.Constant3(0,1,0);
int v = c.Add(a, b);
assert(std::get<Vec3>(c.GetConstantValue(v)) == Vec3(1,1,0));

// 3. Multiply 优化（乘1直通、乘0归零）
int x = c.Constant3(1,2,3);    // 非常量
assert(c.Multiply(x, c.Constant(1.0f)) == x);   // 直通，返回原索引
assert(std::get<float>(c.GetConstantValue(c.Multiply(c.Constant(2.0f), c.Constant(0.0f)))) == 0.0f);

// 4. 除零保护（不崩 + Warning 进 errors）
c.GetErrors().clear();   // 测试前清空
int dz = c.Divide(c.Constant(1.0f), c.Constant(0.0f));
assert(std::get<float>(c.GetConstantValue(dz)) == 0.0f);   // 兜底返回 0
assert(!c.GetErrors().empty());                            // 有错误
assert(c.GetErrors().back().severity == EErrorSeverity::Warning);
assert(c.GetErrors().back().pinName == "B");               // 精确到除数引脚

// 5. 嵌套运算（2*3+4 → 10）
int n = c.Add(c.Multiply(c.Constant(2.0f), c.Constant(3.0f)), c.Constant(4.0f));
assert(std::get<float>(c.GetConstantValue(n)) == 10.0f);

// 6. 类型不匹配 EmitError（Float2 + Float3 不兼容）
c.GetErrors().clear();
int bad = c.Add(c.Constant2(1,2), c.Constant3(1,2,3));
assert(bad < 0);   // 哨兵传播
assert(!c.GetErrors().empty());
assert(c.GetErrors().back().severity == EErrorSeverity::Error);

// 7. 错误去重（同一个错误多次触发只存一份）
c.GetErrors().clear();
for (int i = 0; i < 5; ++i) {
    c.Add(c.Constant2(1,2), c.Constant3(1,2,3));   // 同一个类型不匹配
}
// 因为 Add 返回 -1，第二次循环时 a<0 短路，不会重复 emit
// 如果是不同路径触发同一检查，靠 SameAs 去重
assert(c.GetErrors().size() <= 2);   // 一次 emit + 可能的传播

// 8. 查询边界保护
assert(c.GetType(9999) == EValueType::Unknown);   // 越界不崩，返回 Unknown
assert(c.GetParameterCode(-1) == "0.0");          // 负索引返回 "0.0"
```

---

## 第八部分：UE 5.7 参考（相对 `Engine/` 路径）

> 注：UE 源码根目录因工作环境而异（如 `E:\UE5\` 或仓库内 `UE5/UnrealEngine-release/`），下表路径均为相对 `Engine/` 的路径，在自己的 UE 源码根目录下对应查找即可。

| 本课概念 | UE 对应 | 位置 |
|---------|---------|------|
| `CodeChunk` | `FShaderCodeChunk` | `Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.h:83-174` |
| `EValueType` | `EMaterialValueType`（64 位 bitmask）| `Source/Runtime/Engine/Public/MaterialValueType.h` |
| `MaterialCompiler` 算子 | `FHLSLMaterialTranslator::Add/Sub/Mul/Div` | `Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp:9849-10060` |
| `Constant/2/3/4` | `FHLSLMaterialTranslator::Constant/2/3/4` | 同上 `.cpp:5598-5630` |
| `AddCodeChunk`/`AddInlinedCodeChunk` | 同名（UE 也是这两个）| 同上 `.cpp:3880-4280` |
| 常量折叠 variant（教学版简化）| `FMaterialUniformExpression` 表达式树 + preshader | `Source/Runtime/Engine/Private/Materials/MaterialUniformExpressions.h` |
| 类型推导 | `GetArithmeticResultType` | `HLSLMaterialTranslator.cpp:4612-4663` |
| `CompileError{nodeId, pinName, message, severity}` | `FMaterialCompilerError` + `UMaterialExpression::LastErrorText` | `Engine/Source/Runtime/Engine/Public/MaterialShared.h` |
| `EmitError`（贯穿编译路径一次加全）| `Compiler->Errorf` + 各 expression `Compile` 失败时记录 | `HLSLMaterialTranslator.cpp` 各算子开头 |
| 算子类型检查 + INDEX_NONE 哨兵传播 | 同（UE 也是 `if (A < 0) return INDEX_NONE;`）| `HLSLMaterialTranslator.cpp` 全部分发器 |

**搜索关键词**（在 UE 源码里）：`FShaderCodeChunk`、`FHLSLMaterialTranslator::Add`、`AddCodeChunkInner`、`IsExpressionConstantValue`、`EMaterialValueType`、`HandleMaterialCompilationErrors`、`FMaterialCompilerError`、`LastErrorText`。

---

## 第九部分：错误诊断（已踩坑 / 注意）

| 坑 | 现象 | 教训 |
|----|------|------|
| 递归编译中途出错不短路 | 类型错误后仍生成残缺 HLSL，错上加错 | Error 级哨兵传播（算子返 -1），**当前分支**不展开，但**其他分支继续**（让多错误一轮报全）|
| 错误不去重 | 同一类型不匹配因递归多次访问被报 N 次，错误列表刷屏 | `EmitError` 里 `SameAs` 判定，相同 node+pin+message 只存一份 |
| 除零只用 `ME_LOG_WARNING` | 用户在 UI 看不到（日志只到控制台）| 改为 `EmitError(Warning)` 进 errors 列表，UI 才能高亮（课 19）|
| 算子内 pinName 不准 | `Add` 里不知道是 A 还是 B 出错（`current_pin_` 是上游设的）| 算子内 EmitError 时**显式 override pinName**（如 `"A/B"`），不要依赖 `current_pin_` |
| `current_node_` 未设置 | 课 6 阶段没接 CompileInputPin，`current_node_` 是 nullptr，EmitError 拿到 nodeId 为 Invalid | 课 6 测试时用 overrideNodeId 显式指定，或友元测试设置 `current_node_`；课 7 接 CompileInputPin 后自动有上下文 |
| `hlsl_code` 在失败时被清空 | 用户改不到代码面板对照 | 即使编译失败也保留部分 HLSL（错误分支被短路，其他分支正常）；课 8 UI 显示残缺代码 + 错误信息 |

> **核心设计取舍**：错误检查**贯穿编译路径一次加全**（不要"先标记后补"），因为递归编译天然带 node/pin 上下文，事后回扫反而要在两处维护同一检查。

**对照 UE 错误诊断的三个差异**：

1. **UE 的错误回填是后处理**：编译完后 `HandleMaterialCompilationErrors` 遍历编译器收集的错误，回填到 `UMaterialExpression::LastErrorText`，再触发节点 redraw。**我们是编译时直接带 nodeId**——课 7 实现的 `CompileInputPin` 入口设置 `current_node_`，EmitError 直接定位，省了"编译器错误 → 表达式对象"的回填层。代价：我们的错误检查和编译耦合在 `MaterialCompiler` 里，UE 是分离的（编译器只生成错误，回填由 `UMaterial` 做）。

2. **UE 的环检测在编辑时**：用户连线时就调 `IsMaterialInputLooping` 拒绝循环连接，编译时不会有环——我们是编辑时允许（`Pin::CanConnectTo` 不查环），编译时查（课 7）。教学版简化（编辑时每条连线都要 DFS 对教学过重），代价是用户能连出环（编译才发现）。

3. **UE 错误没 pin 概念**：UE 的 `UMaterialExpression` 是类，输入是命名成员（`FExpressionInput A`），错误格式是 "line N: expression ClassName: error text"——没明确"哪个 pin"。**我们 pin 一等公民**（`Pin::name`），错误能精确到引脚，UI 引脚级高亮更直接（课 19）。

---

## 完成标志

本课真正实现的（每一条都能本课勾选）：

- [ ] `EValueType` 扩展（Float/Int/Matrix/Texture/Sampler）+ `TypeSystem::GetArithmeticResultType` / `ToHLSLType` 推导
- [ ] `GetComponentCount` 留 Types.h（free function），`TypeSystem` 不重复定义
- [ ] `CodeChunk` 用 variant `constant_value`（支持 Vec2/3/4 折叠）
- [ ] `CompileError` 结构（`nodeId` / `pinName` / `message` / `severity`）+ `EErrorSeverity` 三级 + `SameAs` 去重
- [ ] `CompileResult` 直接定义带 `errors` 数组的最终版 + `HasErrors()` 查询 + `error_message` 兼容字段
- [ ] `MaterialCompiler::EmitError` 收集器 + `errors_` / `current_node_` / `current_pin_` 上下文成员
- [ ] 4 个查询函数（`GetType` / `IsConstant` / `GetConstantValue` / `GetParameterCode`）+ assert + 边界保护
- [ ] `AddCodeChunk` / `AddInlinedCodeChunk` / `AddConstantChunk`（接收 variant）+ hash 去重 + `FormatConstantCode`
- [ ] `Constant` / `Constant2` / `Constant3` / `Constant4` 全走常量路径（`is_constant=true`）
- [ ] `ConstantFolding::FoldBinary` / `FoldUnary` / `IsScalarZero` / `IsScalarOne`（variant 版，含标量广播）
- [ ] `Add` / `Subtract` / `Multiply` / `Divide` 三段判定 + 向量折叠 + 代数化简 + 类型不匹配 `EmitError(Error)` + 除零 `EmitError(Warning)`
- [ ] `Abs` / `Negate` / `Sine` / `Cosine`（一元 + 折叠）
- [ ] `Lerp` / `Clamp` / `Power`（多元，含 `pow(x,0)→1` / `pow(x,1)→x` 化简）
- [ ] `Dot` / `Cross` / `Normalize` / `Length`（向量运算）
- [ ] `ComponentMask` / `AppendVector` / `Cast`
- [ ] `compiler_test.cpp` 全部断言通过：标量折叠、向量折叠、乘 1/0 化简、除零 Warning + errors 收集、类型不匹配 Error + 哨兵传播、嵌套运算、查询边界保护、错误去重

---

## 核心原则回顾

1. **一步到位**：variant CodeChunk + 完整类型 + 向量折叠 + 错误收集，直接做完整版，不做"先标量后向量"、"先无错误后有错误"的中间态
2. **对照 UE**：每个设计点知道 UE 怎么做、教学版为什么简化（variant vs 表达式树、enum vs bitmask、单层 vs 双层去重、错误编译时直接带 nodeId vs UE 后处理回填）
3. **保留核心语义**：hash 去重、"是否常量"判定、双向递归——这些和 UE 一致，简化的是"实现手段"不是"设计思想"
4. **本课边界清晰**：算子 API + chunks 数据结构 + 编译器层错误收集做完；`Compile(Graph*)` 留课 7、`GenerateCode` 留课 8、纹理算子留课 14/15、错误 UI 留课 19。**不挖坑、不留 TODO 占位、不写"课 X 再做"**
