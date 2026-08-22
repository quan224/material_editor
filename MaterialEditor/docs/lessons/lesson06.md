# 课6：编译器核心（完整版，对照 UE5.8 结构）

## 目标

实现编译器的**全部核心数据结构**——类型系统、代码块、常量/参数表达式树、双层编译器接口——再把算术算子架在这些结构上。对标 UE 的四个真实结构（本版教案逐字段、逐行对照源码，UE 版本 5.8.1，行号均为实测）：

| # | 教学版 | UE 对应 | 对齐点 |
|---|--------|---------|--------|
| ① | `EValueType : uint64_t` bitmask + 类别掩码 | `EMaterialValueType`（`MaterialValueType.h`）| 位定义、掩码语义、"MCT_Float1 不自动提升"规则全部照搬 |
| ② | `CodeChunk` 全字段 | `FShaderCodeChunk`（`HLSLMaterialTranslator.h:83-174`）| 每个字段逐一裁决采用/不采用并说明理由 |
| ③ | `UniformExpression` 表达式树 | `FMaterialUniformExpression` 家族（`MaterialUniformExpressions.h`）| 常量与含参数表达式统一为一棵树，支持递归 IsConstant / IsIdentical 去重 / CPU 求值 |
| ④ | `MaterialCompiler` 抽象基类 + `HLSLTranslator` 实现 | `FMaterialCompiler`（`MaterialCompiler.h:145`）+ `FHLSLMaterialTranslator` | 编译器拆两层，表达式只依赖抽象接口 |

**本课范围（自给自足，不挖坑）**：
- 类型系统 bitmask 改造（Float1-4 / 矩阵 / 纹理 + 类别掩码）+ 算术推导规则
- `UniformExpression` 树：基类 + `UniformConstant` + `UniformFoldedMath` + `UniformFoldedUnary`
- `CodeChunk` 全字段版 + 常量/参数表达式 chunk 的构建与**双层去重**（代码 hash + `IsIdentical`）
- 双层编译器：`MaterialCompiler`（抽象接口）+ `HLSLTranslator`（实现）
- 算子 API：算术 / 三角 / 向量 / 标量常量 / 类型转换（共 ~20 个），按 UE 真实的**三轨判定**实现（错误检查 → 表达式树/立即折叠 → HLSL 发射）
- 错误诊断编译器层：`CompileError` + `EmitError` 收集器 + 类型不匹配/除零检查

**本课不做、由后续课第一次实现的部分**（明确边界）：
- 端到端编译 `Compile(Graph*)` + `CompileInputPin` + `CompileExpression` → **课 7**（接 `ExpressionRegistry`）
- 图层级错误检查：循环依赖 / 必需引脚未连 → **课 7**（依赖 `Compile(Graph*)` 主流程）
- HLSL 完整生成 `GenerateCode` → **课 8**（cbuffer / VS / PS 模板）
- 纹理 / 控制流算子 `TextureCoordinate` / `TextureSample` / `If` → **课 14/15**
- 错误诊断 UI 层（节点高亮 / 引脚标红 / 错误面板）→ **课 19**
- 含参数的表达式节点（如 `UniformScalarParameter`）→ **课 20**（在树上追加子类，本课的树 API 已为它留好位置——`GetNumberValue` 带 `MaterialRenderContext` 参数）

---

## 背景知识

### 编译器做什么

接收一个 `Graph`，从输出节点反向遍历，对每个节点调用其 `Expression::Compile()`，生成一系列"代码块"（`CodeChunk`），最后组装成 HLSL 着色器。

### 完整编译流程（标出本课实现的部分）

```
MaterialCompiler::Compile(Graph*)               ← 课7 实现
  │
  ├─ 0. 环检测（DFS 三色标记）                    ← 课7 实现
  ├─ 1. 获取输出节点（材质根）
  ├─ 2. 必需引脚检查                              ← 课7 实现
  ├─ 3. 对每个输入引脚 CompileInputPin             ← 课7 实现
  │     CompileExpression(upstreamNode)           ← 课7 实现
  │       └─ 上游 Expression::Compile(compiler, node)
  │            ├─ CompileInputPin() ← 递归
  │            ├─ compiler->Add(a, b) ← ★ 本课实现
  │            └─ 返回输出引脚的代码块索引
  └─ 4. GenerateCode() 组装 HLSL                 ← 课8 实现
```

**本课只实现★标注的算子 API 层**及其全部支撑结构。课 6 的测试 `compiler_test.cpp` **直接调算子 API**，不走 `Compile(Graph*)`。

### 关键设计：双向递归（对照 UE）

| 角色 | 职责 |
|------|------|
| **编译器**（`MaterialCompiler` 接口）| 只暴露"算子 API"（`Add`/`Constant`/...），**不知道图结构** |
| **图节点**（`Expression` 子类）| `Compile()` 里先编译自己的输入引脚（递归），再调编译器算子组合结果 |

> **UE 对照**：`FHLSLMaterialTranslator` 只提供算子，`UMaterialExpression` 子类的 `Compile()` 驱动递归。见 `Material.cpp` 的 `CompilePropertyAndSetMaterialProperty`。

### 为什么是表达式树，不是 variant 存常量

编译期一个 chunk 的"值"有**三种形态**，这是理解本课的钥匙：

| 形态 | 例子 | UE 怎么存 | 教学版怎么存 |
|------|------|----------|-------------|
| 纯常量 | `Constant3(1,0,0)` | `FMaterialUniformExpressionConstant`（表达式树的叶子）| `UniformConstant`（树的叶子）|
| 含参数的可折叠表达式 | `ScalarParameter("R") * 2` | `FMaterialUniformExpressionFoldedMath` 子树（preshader 运行时 CPU 求值）| `UniformFoldedMath` 子树（`GetNumberValue` CPU 求值）|
| 纯 GPU 表达式 | `TextureSample` 的结果 | 无表达式，只有 HLSL 代码串 | 无表达式，只有 HLSL 代码串 |

如果 chunk 里只存一个 `std::variant<float, Vec2, Vec3, Vec4>` 常量值，第二种形态（含参数）完全表达不了——参数的值编译期不知道，只有"一棵保留参数依赖的树"才能在运行时求值。UE 的 `FShaderCodeChunk` 里挂的就是 `TRefCountPtr<FMaterialUniformExpression>` 指针（`.h:111`），教学版照搬：**chunk 挂表达式树指针，常量只是树的一种叶子**。

---

## 第一部分：类型系统——`EValueType` 改为 uint64_t bitmask

### 文件：`src/MaterialGraph/Public/Types.h`

课 1-4 定义的 `enum class EValueType`（普通枚举）在本课替换为 bitmask。对照 UE `Engine/Source/Runtime/Engine/Public/MaterialValueType.h`（全文 93 行，建议通读）：

```cpp
// 值类型 bitmask。对照 UE EMaterialValueType（MaterialValueType.h）。
// 单类型 = 单个位；类别掩码 = 多个位的或。判断类别用 &，判断单类型用 ==。
enum EValueType : uint64_t {
    MCT_Unknown  = 0,

    // 标量/向量（float）——位值与 UE 完全一致（bit 0-3）
    MCT_Float1   = 1u << 0,
    MCT_Float2   = 1u << 1,
    MCT_Float3   = 1u << 2,
    MCT_Float4   = 1u << 3,

    // 纹理对象类型——"对象类型"，不能参与算术，只供 TextureSample 用
    // （位值对齐 UE：MCT_Texture2D 在 bit 4）
    MCT_Texture2D   = 1u << 4,
    MCT_TextureCube = 1u << 5,

    // 纹理变体——位值对齐 UE（散布在 bit 6/8/12/13，UE 的纹理位本来就不连续：
    // bit 7=TextureCubeArray、bit 9=StaticBool、bit 10=Unknown（UE 的 Unknown 位，
    // 教学版 MCT_Unknown 用 0）教学版均未引入，bit 11=MaterialAttributes 在下面引入）
    MCT_Texture2DArray  = 1u << 6,   // 贴图数组：uv + layer 索引采样
    MCT_VolumeTexture   = 1u << 8,   // 3D 纹理：uvw 采样（体积雾等）——UE 原名即 VolumeTexture
    MCT_TextureExternal = 1u << 12,  // 外部纹理（视频帧直采）
    MCT_TextureVirtual  = 1u << 13,  // 虚拟纹理（分页流式，页表间接采样）

    // 打包/模型类型——位值对齐 UE（bit 11/16/17）
    MCT_MaterialAttributes = 1u << 11,  // 材质属性打包值（一个 struct 流动）
    MCT_ShadingModel       = 1u << 16,  // shading model 选择值
    MCT_Substrate          = 1u << 17,  // Substrate BSDF 值（新材质模型统一输出）

    // 矩阵——位值对齐 UE（bit 32/33，UE 用 1ull 因为超过 32 位）
    MCT_Float3x3 = 1ull << 32,
    MCT_Float4x4 = 1ull << 33,

    // ===== 类别掩码（多个位的或，用于"是不是某类"的集合判断）=====
    MCT_Float   = MCT_Float1 | MCT_Float2 | MCT_Float3 | MCT_Float4,   // 任意 float 向量
    MCT_Texture = MCT_Texture2D | MCT_TextureCube | MCT_Texture2DArray
                | MCT_VolumeTexture | MCT_TextureExternal | MCT_TextureVirtual,  // 任意纹理
    // 打包值三件套（MaterialAttributes/ShadingModel/Substrate）不能算术、只走专门节点，
    // UE 也没有给它们统一掩码——用 == 单值判断，同款
};
```

### 每个位/掩码的作用（全表）

| 位/掩码 | 作用 | 使用场景 |
|---------|------|---------|
| `MCT_Float1` | 标量 float | 单分量常量、Dot/Length 的结果 |
| `MCT_Float2` | 二维向量 | UV 坐标 |
| `MCT_Float3` | 三维向量 | 颜色 RGB、世界坐标 |
| `MCT_Float4` | 四维向量 | RGBA 颜色、齐次坐标 |
| `MCT_Float3x3` | 3x3 矩阵 | 切线空间变换（Transform 节点用）|
| `MCT_Float4x4` | 4x4 矩阵 | 投影/世界变换 |
| `MCT_Texture2D` | 2D 纹理对象 | TextureSample 的输入，**不能算术** |
| `MCT_TextureCube` | 立方体纹理 | 环境贴图反射采样 |
| `MCT_Texture2DArray` | 贴图数组 | uv + layer 索引采样；DX12 SRV 用 `TEXTURE2DARRAY` 维度 |
| `MCT_VolumeTexture` | 3D 纹理 | uvw 三维采样（体积雾/3D 数据）；SRV `TEXTURE3D` |
| `MCT_TextureExternal` | 外部纹理 | 视频帧采样；教学版按普通 2D 采样路径，类型位对齐 UE |
| `MCT_TextureVirtual` | 虚拟纹理 | 分页流式；教学版按普通 2D 采样 + 注释说明页表机制，类型位对齐 UE |
| `MCT_MaterialAttributes` | 材质属性打包值 | Make/Break 节点的输入输出；编译时展开成多个属性 chunk |
| `MCT_ShadingModel` | shading model 值 | ShadingModel 节点输出（0=Unlit, 1=Lit）；决定 PS 光照分支 |
| `MCT_Substrate` | Substrate BSDF 值 | Substrate BSDF 节点族输出；教学版桩化（类型/编译路径全真，BSDF 求值函数桩返回默认值）|
| `MCT_Float`（掩码）| "任意 float 向量"集合 | `Type & MCT_Float` 判断能否声明局部变量、能否算术 |
| `MCT_Texture`（掩码）| "任意纹理"集合 | `Type & MCT_Texture` 判断是否对象类型 |

### 消费方索引——每个新类型位的"谁在读它"（类型活着 = 有代码读它做分支）

类型位和消费方成对演进（UE 的 MCT_TextureExternal 和 MaterialExpressionExternalTexture 就是同一个 PR 进的）。本课先立类型系统和编译器层的消费，节点级消费在各课落地：

| 类型位 | 课 6 内的消费（本课就写）| 后续课的消费节点 |
|-------|------------------------|-----------------|
| `MCT_Texture*` 全族 | `GetArithmeticResultType` 返回 Unknown + `AddCodeChunk` 纹理报错分支 + `ToHLSLType` 映射（教学版纹理全族同走这三处，靠 `& MCT_Texture` 掩码自动涵盖，**加位不改消费代码**——这正是掩码设计的收益）| 课 14 TextureSample 按纹理类型分派采样签名；课 15 DX12 SRV 按 ViewDimension 建视图；课 21 各格式加载 |
| `MCT_MaterialAttributes` | 同上三处 + `GetComponentCount` 返回 0（打包值无分量概念）| 课 18：Make/Break MaterialAttributes 节点 + 编译时展开 |
| `MCT_ShadingModel` | 同上三处 | 课 15/17：材质根节点读 ShadingModel chunk 决定 PS 分支（Unlit 跳过光照）|
| `MCT_Substrate` | 同上三处 + `ToHLSLType` 返回 `"FSubstrateData"`（对齐 UE `.cpp:3182`）| 课 20：Substrate BSDF 节点族，输出 `GetInitialisedSubstrateData()` 桩调用（对齐 UE `.cpp:12927`）|

### UE 有、教学版不引入的位（对照表，避免"为什么少了"的疑问）

| UE 位 | UE 用途 | 不引入的原因 |
|-------|---------|-------------|
| `MCT_UInt1-4`（bit 25-28）| 无符号整数运算（位操作、VT 页表）| 教学版无整数节点；UE 也没有有符号 Int 类型（反射层 IntProperty 是参数面板类型，过节点边界时 `(float)` 转换）|
| `MCT_LWCScalar/Vector2/3/4`（bit 18-21）| Large World Coordinates 双精度坐标 | 教学版不做双精度坐标管线（LWC 是全管线工程：类型提升/截断/WSAdd 函数族）|
| `MCT_StaticBool` / `MCT_Bool` | 静态开关 / 动态 bool | 静态开关需要 permutation 变体管理系统（每个开关组合单独编译一份 shader + 缓存）；bool 语义用 Float 0/1 表达 |
| `MCT_LicenseeReserved`（bit 48-63）| 授权方自定义保留段 | 不适用 |

> **注**：`MCT_MaterialAttributes` / `MCT_Substrate` / `MCT_ShadingModel` 和纹理变体四组**已引入**（见上方枚举和消费方索引）——策略：**类型系统完整对齐 UE，编译器层消费本课落地，节点级消费分课落地，Substrate 的 BSDF 求值桩化**（UE 的 Substrate.ush 着色器运行时 16k 行，教学版用桩函数返回默认 BSDF，类型通路全真）。

### 两条关键语义（从 UE 注释原样搬来，最容易踩的坑）

UE 源文件头两个类型的注释里藏着两条规则：

1. **`MCT_Float1` 不会自动提升**（"Note that MCT_Float1 will not auto promote to any other float types"）——算术推导里 `Float1 + Float3 → Float3` 的提升，是推导函数的特判逻辑，**不是类型系统自带的**。
2. **标量表达式的返回类型用 `MCT_Float` 掩码，不用 `MCT_Float1`**（"use MCT_Float instead for scalar expression return types"）——`MCT_Float` 语义是"标量，可复制提升到任意 float 向量"。UE 的 `GetArithmeticResultType`（`.cpp:4250`）显式写了 `TypeA == MCT_Float || TypeA == MCT_Float1` 两者都当标量处理，就是为这条规则服务。

### 判断方式的迁移（普通枚举 → bitmask）

| 旧写法（enum）| 新写法（bitmask）| 说明 |
|--------------|-----------------|------|
| `type == EValueType::Float3` | `type == MCT_Float3` | 单类型判断不变（只能对单值位用 `==`）|
| （做不到）| `type & MCT_Float` | **新增能力**：类别集合判断 |
| （做不到）| `type & MCT_Texture` | 对象类型判断 |
| ❌ `type == MCT_Float` 当"是 float3"用 | 掩码永远不能 `==` 单值 | `MCT_Float & MCT_Float3` 为真，但 `MCT_Float == MCT_Float3` 为假 |

### 类型查询（free function，留在 Types.h）

```cpp
// 分量数：单值位有效；掩码/矩阵/纹理返回 0（掩码问"分量数"没有意义）
// 注意 MCT_Float 返回 1——UE GetNumComponents 同样把 MCT_Float 当标量处理
inline int GetComponentCount(EValueType t) {
    switch (t) {
        case MCT_Float: case MCT_Float1: return 1;
        case MCT_Float2: return 2;
        case MCT_Float3: return 3;
        case MCT_Float4: return 4;
        default: return 0;   // Matrix/Texture/MaterialAttributes/ShadingModel/Substrate/Unknown
                            // 及掩码组合不走分量逻辑（打包值无分量概念，UE 同款）
    }
}
```

分层原则不变：**纯类型属性查询留 Types.h，HLSL 字符串/推导规则进编译器层 TypeSystem**。

### 文件：`src/Compiler/Public/TypeSystem.h`（推导规则）

对照 UE `FHLSLMaterialTranslator::GetArithmeticResultType`（`.cpp:4221-4261`，逐行对齐）：

```cpp
class TypeSystem {
public:
    // 算术结果类型。对照 UE .cpp:4221：
    // ①非 primitive（矩阵/纹理/打包值）→ 错误；②同类型 → 原类型；
    // ③任一方是 MCT_Float / MCT_Float1（标量）→ 另一方；
    // ④其余（Float2 vs Float3 等）→ 错误 + Unknown
    static EValueType GetArithmeticResultType(EValueType a, EValueType b) {
        // ① 对象类型/矩阵/未知不能算术（对照 UE IsPrimitiveType 检查）
        //   纹理全族靠 MCT_Texture 掩码一网打尽；打包三件套也在此拦截
        if (a & MCT_Texture || b & MCT_Texture) return MCT_Unknown;
        if (a == MCT_MaterialAttributes || b == MCT_MaterialAttributes
            || a == MCT_ShadingModel || b == MCT_ShadingModel
            || a == MCT_Substrate || b == MCT_Substrate) return MCT_Unknown;
        if (a == MCT_Unknown || b == MCT_Unknown) return MCT_Unknown;
        if (GetComponentCount(a) == 0 || GetComponentCount(b) == 0) return MCT_Unknown;  // 矩阵

        if (a == b) return a;                        // ② 同类型
        if (a == MCT_Float || a == MCT_Float1) return b;   // ③ 标量提升
        if (b == MCT_Float || b == MCT_Float1) return a;
        return MCT_Unknown;                          // ④ 不兼容
        // 注意：返回 Unknown 时调用方负责 EmitError——UE 在这里 Errorf，
        // 教学版把错误上报挪到算子里（带上 node/pin 定位，见第三部分）
    }

    static const char* ToHLSLType(EValueType t) {
        switch (t) {
            case MCT_Float: case MCT_Float1: return "float";
            case MCT_Float2:   return "float2";
            case MCT_Float3:   return "float3";
            case MCT_Float4:   return "float4";
            case MCT_Float3x3: return "float3x3";
            case MCT_Float4x4: return "float4x4";
            // 纹理声明类型（对照 UE .cpp:3141-3223 的类型名表）
            case MCT_Texture2D:      return "Texture2D";
            case MCT_TextureCube:    return "TextureCube";
            case MCT_Texture2DArray: return "Texture2DArray";
            case MCT_VolumeTexture:  return "Texture3D";
            case MCT_TextureExternal:case MCT_TextureVirtual:
                                      return "Texture2D";   // 教学版按 2D 采样路径（见消费方索引）
            // 打包/模型类型（对照 UE：FSubstrateData 见 .cpp:3182）
            case MCT_MaterialAttributes: return "FMaterialAttributes";
            case MCT_ShadingModel:       return "uint";      // 枚举当整数流动（UE 同款语义）
            case MCT_Substrate:          return "FSubstrateData";
            default: return "float";   // 兜底，避免生成非法 HLSL
        }
    }

    // 类型可读名（EmitError 的报错信息用；对照 UE DescribeType .cpp:3141）
    static const char* TypeName(EValueType t) {
        switch (t) {
            case MCT_Float: case MCT_Float1: return "Float";
            case MCT_Float2:   return "Float2";
            case MCT_Float3:   return "Float3";
            case MCT_Float4:   return "Float4";
            case MCT_Float3x3: return "Float3x3";
            case MCT_Float4x4: return "Float4x4";
            case MCT_Texture2D:      return "Texture2D";
            case MCT_TextureCube:    return "TextureCube";
            case MCT_Texture2DArray: return "Texture2DArray";
            case MCT_VolumeTexture:  return "VolumeTexture";
            case MCT_TextureExternal:return "TextureExternal";
            case MCT_TextureVirtual: return "TextureVirtual";
            case MCT_MaterialAttributes: return "MaterialAttributes";
            case MCT_ShadingModel:       return "ShadingModel";
            case MCT_Substrate:          return "Substrate";
            default: return "Unknown";
        }
    }
};
```

> UE 版里错误上报（`Errorf`）在 `GetArithmeticResultType` 内部；教学版挪到算子里发，因为 EmitError 需要 node/pin 上下文（第三部分），而 TypeSystem 是无状态的静态工具类。

---

## 第二部分：UniformExpression——常量/参数表达式树

### 文件：`src/Expression/Public/UniformExpression.h`（L4）

> 放 Expression 层不是 Compiler 层：①依赖上它只需要 L1（RefCounted/MathTypes）和 L2（Types.h），放得下；②`MaterialCompiler` 接口（本层，第五部分）的签名要返回 `UniformExpression*`，接口在 L4 就不能向上 include L5；③概念上它是「CPU 侧的表达式树」，与图侧表达式节点（Expression）同族，同层合理。

对照 `Engine/Source/Runtime/Engine/Private/Materials/MaterialUniformExpressions.h`。这棵树表达"只随 uniform 输入变化的表达式"——纯常量是它的特例（叶子），含参数的表达式是它的内部节点。

### 基类（对照 `FMaterialUniformExpression`，`.h:56-81`）

```cpp
#pragma once
#include "Core/Public/RefCounted.h"     // Ref<> 引用计数智能指针（对标 TRefCountPtr）
#include "Core/Public/MathTypes.h"      // Vec4
#include "MaterialGraph/Public/Types.h"
#include <vector>

// 树求值常按分量遍历 Vec4（(&v.x)[i]），编译期锁死"四成员连续"这个前提
static_assert(sizeof(Vec4) == 4 * sizeof(float), "Vec4 must be tightly packed float[4]");

// 渲染期上下文：表达式树 CPU 求值时提供外部数据。
// 当前无参数源（空壳）；参数系统的求值入口就是它。
struct MaterialRenderContext {};

// 表达式树基类。对照 FMaterialUniformExpression（MaterialUniformExpressions.h:56）
class UniformExpression : public RefCounted {
public:
    // 这棵子树是否编译期常量（整棵子树无参数节点才为真）。
    // 基类默认 false——只有 Constant 叶子/Folded 递归判定才返回 true。
    // 对照 UE：virtual bool IsConstant() const { return false; }
    virtual bool IsConstant() const { return false; }

    // 语义相等判定（去重用）：两棵树结构、值、类型完全一致才为真。
    // 对照 UE：virtual bool IsIdentical(const FMaterialUniformExpression*) const { return false; }
    virtual bool IsIdentical(const UniformExpression* other) const { return false; }

    // CPU 求值：把这棵子树算成一个 4 分量值（对标 UE GetNumberValue(Context, FLinearColor&)）。
    // 只对 IsConstant()==true 的树保证结果确定；含参数的树由参数表决定。
    virtual void GetNumberValue(const MaterialRenderContext& ctx, Vec4& out) const { out = Vec4(0,0,0,0); }

    virtual ~UniformExpression() = default;
};
```

### 基类每个虚函数的作用

| 虚函数 | UE 对应 | 作用 | 谁依赖它 |
|--------|---------|------|---------|
| `IsConstant()` | 同名（`.h:65`）| 递归判定子树是否纯常量 | 算子的"立即折叠"判定：两边都常量 → 编译期算成字面量，不生成运算 |
| `IsIdentical(other)` | 同名（`.h:66`）| 语义去重 | `AddUniformExpression` 的第二层去重：相同表达式只建一个 chunk（多个材质属性共享）|
| `GetNumberValue(ctx, out)` | 同名（`.h:70`）| CPU 端求值整棵树 | 立即折叠时算值；参数变化时运行时重求值（不重编译 shader，即 preshader 语义）|

**UE 有、教学版不采用的基类成员**（逐个说明）：

| UE 成员 | 作用 | 不采用原因 |
|---------|------|-----------|
| `GetType()`（`FMaterialUniformExpressionType` RTTI）| 序列化/类型分派 | 教学版用 C++ 自带的 `dynamic_cast`/`IsIdentical` 内部 `static_cast` 前的类别判断即可，不需要手写 RTTI 注册表 |
| `WriteNumberOpcodes(FPreshaderData&)` | 把树编译成 preshader **字节码**（后序遍历写指令流）| 教学版不做字节码虚拟机——树的求值直接用 C++ 虚函数递归（`GetNumberValue`），语义等价、实现省一个 VM |
| `GetChildren()` | 暴露子节点数组（供遍历/序列化/剔除）| 没有 bytecode 序列化和剔除 pass 就没有遍历需求 |
| `UniformOffset` / `UniformIndex` / `ShaderFrequencyMask` | preshader 结果在 uniform buffer 里的布局 | 课 8 的 cbuffer 布局不需要按 UE 的对齐规则压缩 |

### 常量叶子：`UniformConstant`（对照 `FMaterialUniformExpressionConstant`，`.h:257-306`）

```cpp
// 常量叶子。对照 FMaterialUniformExpressionConstant：
// UE 用 FLinearColor Value + uint8 ValueType——4 分量颜色 + 类型标签，
// 不是 variant！float1-4 统一按 4 分量存，类型标签决定取几个分量。
class UniformConstant : public UniformExpression {
public:
    UniformConstant(const Vec4& value, EValueType type) : value_(value), type_(type) {}

    bool IsConstant() const override { return true; }

    bool IsIdentical(const UniformExpression* other) const override {
        auto* o = dynamic_cast<const UniformConstant*>(other);
        return o && o->type_ == type_ && o->value_ == value_;   // 类型 + 值都相等
    }

    void GetNumberValue(const MaterialRenderContext&, Vec4& out) const override {
        out = value_;   // 叶子直接返回存储值
    }

private:
    Vec4      value_;   // 4 分量统一存储（float1 存 (x,x,x,x)，float2 存 (x,y,0,0)，UE 同款）
    EValueType type_;   // 类型标签：决定有效分量数
};
```

**为什么 4 分量 + 标签而不是 `variant<float, Vec2, Vec3, Vec4>`**：UE 的选择。求值永远在 4 分量上进行（`FLinearColor` 逐分量乘除就是 4 分量全算），类型标签只在**生成 HLSL 字面量**和**语义比较**时区分维度。好处：`FoldedMath` 求值不用分发 variant 类型——统一 4 分量算完，由 chunk 的 `Type` 决定输出格式。

### 二元运算节点：`UniformFoldedMath`（对照 `FMaterialUniformExpressionFoldedMath`，`.h:1093-1158`）

```cpp
// 折叠运算种类。对照 UE EFoldedMathOperation（.h:1093）
enum class EFoldedMathOp : uint8_t {
    Add, Sub, Mul, Div, Dot, Cross,   // UE FMO_Add/Sub/Mul/Div/Dot/Cross 一一对应
};

// 二元运算节点。对照 FMaterialUniformExpressionFoldedMath（.h:1103）：
//   IsConstant  = A && B 递归（.h:1134-1137）
//   IsIdentical = A/B/Op/ValueType 四者递归相等（.h:1138-1146）
class UniformFoldedMath : public UniformExpression {
public:
    UniformFoldedMath(UniformExpression* a, UniformExpression* b,
                      EFoldedMathOp op, EValueType type)
        : a_(a), b_(b), op_(op), type_(type) {}

    bool IsConstant() const override {
        return a_->IsConstant() && b_->IsConstant();   // 递归：子树全常量才算常量
    }

    bool IsIdentical(const UniformExpression* other) const override {
        auto* o = dynamic_cast<const UniformFoldedMath*>(other);
        return o && op_ == o->op_ && type_ == o->type_
                && a_->IsIdentical(o->a_.get()) && b_->IsIdentical(o->b_.get());
    }

    void GetNumberValue(const MaterialRenderContext& ctx, Vec4& out) const override {
        Vec4 va, vb;
        a_->GetNumberValue(ctx, va);        // 先递归求两个孩子（后序遍历）
        b_->GetNumberValue(ctx, vb);
        EvalComponents(va, vb, out);        // 逐分量/按运算语义组合
        if (op_ == EFoldedMathOp::Dot)   out = Vec4(DotComponents(va, vb), 0, 0, 0);
        if (op_ == EFoldedMathOp::Cross) out = CrossComponents(va, vb);
    }

private:
    // 逐分量四则：Add/Sub/Mul/Div 对 4 个分量全算（维度裁剪交给类型标签，
    // 对照 UE FLinearColor 的 operator* / operator/ 就是 4 分量全算）
    static void EvalComponents(const Vec4& a, const Vec4& b, Vec4& out);
    static float DotComponents(const Vec4& a, const Vec4& b);   // 前 N 分量点积（N 由 type_ 决定）
    static Vec4  CrossComponents(const Vec4& a, const Vec4& b); // 前 3 分量叉积

    Ref<UniformExpression> a_, b_;   // 子树（引用计数，对标 TRefCountPtr）
    EFoldedMathOp op_;
    EValueType    type_;             // 运算的"操作数维度"标签（Dot/Cross 用，对标 UE ValueType 成员）
};
```

### 一元运算节点：`UniformFoldedUnary`

UE 为每个一元运算**单独建类**（如 `FMaterialUniformExpressionRcp`——Div 的倒数优化用，见 `.cpp:9696`；`FMaterialUniformExpressionPeriodic` 等）。教学版合并成一个类 + op 枚举，**语义完全一致**（IsConstant 递归 / IsIdentical 递归 / GetNumberValue 逐分量）：

```cpp
enum class EFoldedUnaryOp : uint8_t { Neg, Abs, Sine, Cosine, Rcp };

class UniformFoldedUnary : public UniformExpression {
public:
    UniformFoldedUnary(UniformExpression* x, EFoldedUnaryOp op) : x_(x), op_(op) {}

    bool IsConstant() const override { return x_->IsConstant(); }   // 递归

    bool IsIdentical(const UniformExpression* other) const override {
        auto* o = dynamic_cast<const UniformFoldedUnary*>(other);
        return o && op_ == o->op_ && x_->IsIdentical(o->x_.get());
    }

    void GetNumberValue(const MaterialRenderContext& ctx, Vec4& out) const override {
        Vec4 v; x_->GetNumberValue(ctx, v);
        switch (op_) {
            case EFoldedUnaryOp::Neg:    out = Vec4(-v.x, -v.y, -v.z, -v.w); break;
            case EFoldedUnaryOp::Abs:    out = Vec4(std::abs(v.x), std::abs(v.y),
                                                    std::abs(v.z), std::abs(v.w)); break;
            case EFoldedUnaryOp::Sine:   out = Vec4(std::sin(v.x), std::sin(v.y),
                                                    std::sin(v.z), std::sin(v.w)); break;
            case EFoldedUnaryOp::Cosine: out = Vec4(std::cos(v.x), std::cos(v.y),
                                                    std::cos(v.z), std::cos(v.w)); break;
            case EFoldedUnaryOp::Rcp:    out = Vec4(1.f/v.x, 1.f/v.y, 1.f/v.z, 1.f/v.w); break;
        }
    }

private:
    Ref<UniformExpression> x_;
    EFoldedUnaryOp op_;
};
```

### 三种形态在这棵树上怎么统一（回看背景知识的表）

- `Constant3(1,0,0)` → 建 `UniformConstant(Vec4(1,0,0,0), MCT_Float3)` 叶子
- `Add(Constant(1), Constant(2))` → 两边都 `IsConstant()` → **不建树，立即求值**成 `UniformConstant(3)`（见第七部分"立即折叠 vs 建树"）
- `Add(param, Constant(2))`（参数，课 20）→ 建子树，运行时 `GetNumberValue` 求值
- `Add(texSample结果, x)` → 无表达式 → 纯 HLSL 代码串 chunk

---

## 第三部分：CodeChunk——对齐 FShaderCodeChunk 全字段

### 文件：`src/Compiler/Public/CodeChunk.h`

### UE `FShaderCodeChunk` 全字段表（`.h:83-174`，每个字段的作用）

| UE 字段 | 作用 | 教学版 |
|---------|------|--------|
| `uint64 Hash` | 代码块哈希——"不同表达式生成了等价代码"时去重（默认就是代码串的 CityHash64）| ✅ `hash` |
| `uint64 MaterialAttributeMask` | MaterialAttributes 引脚的属性位掩码——一个引脚打包多个材质属性（如 BSD 节点），记录这个 chunk 覆盖了哪些属性 | ❌ 无 MaterialAttributes 节点 |
| `FString DefinitionFinite` | 代码定义串（**硬件有限差分**版本）——非内联时是 `float3 Local0 = ...;` 局部变量声明，内联时是直接嵌入的表达式 | ✅ `code`（教学版只有一版定义串）|
| `FString DefinitionAnalytic` | 代码定义串（**解析偏导**版本）——导数感知材质（如曲面细分位移）需要解析导数时用这版 | ❌ 不做导数双轨（需要整套 DerivativeAutogen）|
| `FString SymbolName` | 局部变量名（`Local0`）。**有 UniformExpression 或内联时为空**，直接用 Definition | ✅ `symbol_name` |
| `TRefCountPtr<FMaterialUniformExpression> UniformExpression` | 挂的表达式树（常量/含参数）。**为空 = 纯 GPU 代码块** | ✅ `uniform_expression`（`Ref<UniformExpression>`）|
| `TArray<int32> ScopedChunks` | 作用域在此 chunk 下的子块（Custom 节点函数体的块）——翻译完成后统一填充 | ❌ 无 Custom 节点作用域 |
| `TArray<int32> ReferencedCodeChunks` | 依赖哪些其他 chunk——生成代码时按依赖顺序输出 | ✅ `references` |
| `EMaterialValueType Type` | 值类型（bitmask）| ✅ `type` |
| `int32 DeclaredScopeIndex / UsedScopeIndex / ScopeLevel` | 作用域三件套：声明在哪个作用域/使用在哪个作用域/嵌套层级——Custom 函数体内声明的变量不能泄漏到外层 | ❌ 同上，无函数作用域 |
| `bool bInline` | 内联块：无变量名，Definition 直接嵌入使用处 | ✅ `is_inline` |
| `bool bIntermediate` | 中间块标记——只为辅助其他 chunk 存在（如 LWC 转换辅助块），编译收尾可剔除 | ❌ 无剔除 pass |
| `EDerivativeStatus DerivativeStatus` | 此表达式的偏导数状态（有限差分/解析/无效）| ❌ 不做导数 |

### 教学版 CodeChunk

```cpp
#pragma once
#include "MaterialGraph/Public/Types.h"
#include "Expression/Public/UniformExpression.h"
#include "Core/Public/RefCounted.h"
#include <string>
#include <vector>
#include <cstdint>

// 代码块：一行（或一段）HLSL 代码 + 元数据。对照 FShaderCodeChunk（HLSLMaterialTranslator.h:83）
struct CodeChunk {
    uint64_t hash = 0;                        // 代码哈希：纯代码块去重（UE 同）
    std::string code;                         // 定义串（= UE DefinitionFinite）
    std::string symbol_name;                  // 局部变量名；有 uniform_expression 时为空（UE 同）
    EValueType type = MCT_Unknown;            // 值类型（bitmask）
    bool is_inline = false;                   // 内联块：无变量名，code 直接嵌入（UE bInline）
    Ref<UniformExpression> uniform_expression;// 表达式树；空 = 纯 GPU 代码块（UE 同名成员）
    std::vector<int32_t> references;          // 依赖的 chunk 索引（UE ReferencedCodeChunks）
};
```

### 两个构造函数的语义（UE 的设计，教学版遵守）

UE 给了两个构造函数（`.h:143` / `.h:160`），对应 chunk 的两种来源：

1. **纯代码块**（`Hash, Definition, SymbolName, Type, bInline`）——`AddCodeChunkInner` 路径用：可能有 SymbolName（非内联时分配 `Local0`）。
2. **带表达式树的块**（`Hash, UniformExpression, Definition, Type`）——`AddUniformExpressionInner` 路径用：**没有 SymbolName 参数、bInline 恒 false**，但字段注释说明了真正语义：*有 UniformExpression 时 Definition 直接用，不声明局部变量*。即**表达式块永远"内联"使用**——常量短串直接嵌，含参数块在课 8 生成代码时替换为 preshader 访问串。

`GetParameterCode` 因此是三段判断（顺序重要）：

```cpp
// 有表达式树 → 直接用定义串（常量字面量 / preshader 访问）
// 内联块    → 定义串
// 非内联块  → 变量名（引用 float3 Local0 = ...; 的声明）
if (c.uniform_expression) return c.code;
if (c.is_inline)          return c.code;
return c.symbol_name;
```

---

## 第四部分：错误诊断基础设施（编译器层）

> 错误诊断分两层：**编译器层**（错误生成 + 收集）留本课；**UI 层**（节点高亮 + 错误面板）→ 课 19。

### 文件：`src/Compiler/Public/CompileError.h`

```cpp
#pragma once
#include "Core/Public/UUID.h"
#include <string>

// 错误严重级别（决定 UI 着色 + 是否中断编译）
enum class EErrorSeverity {
    Error,    // 编译失败：类型不匹配、循环依赖、必需引脚未连接
    Warning,  // 编译继续：除零保护、隐式窄化
    Info,     // 提示：未使用节点、冗余算子
};

struct CompileError {
    UUID            nodeId   = UUID::Invalid();  // 出错节点（UI 按它高亮、点击跳转）
    std::string     pinName;                     // 出错引脚名（空 = 节点级错误如环检测）
    std::string     message;                     // 用户可读描述（要带上下文）
    EErrorSeverity  severity = EErrorSeverity::Error;

    // 去重 key：同 node + 同 pin + 同 message 视为同一个错误
    // （递归编译可能多次触发同一检查）。用方法不用 operator==，避免误用
    bool SameAs(const CompileError& other) const {
        return nodeId == other.nodeId
            && pinName == other.pinName
            && message == other.message;
    }
};
```

### `CompileResult`（最终版，课 7 的 `Compile()` 直接填它）

```cpp
struct CompileResult {
    bool                        success = false;       // = !HasErrors()
    std::string                 hlsl_code;
    std::string                 error_message;         // 兼容字段：第一个 Error 级错误的 message
    std::vector<CompileError>   errors;                // 所有错误（含 Warning/Info）

    bool HasErrors() const {
        for (const auto& e : errors)
            if (e.severity == EErrorSeverity::Error) return true;
        return false;
    }
};
```

> `hlsl_code` 在失败时也填——错误分支被短路，其他分支正常，部分 HLSL 帮用户对照定位，不清空。

### 错误短路 vs 继续的策略

| 严重度 | 行为 | 例子 |
|--------|------|------|
| Error（当前算子）| 不生成新 chunk（返回 `INDEX_NONE=-1`），**不停止整个编译** | 类型不匹配：`Add` 返回 -1，下游 `if (a < 0) return -1;` 哨兵传播 |
| Error（致命）| 立即 return，不继续编译 | 循环依赖（课 7）|
| Warning | 编译继续（生成兜底 chunk），同时进 `errors_` | 除零：折叠为 `Constant(0)` + Warning |
| Info | 编译继续，无副作用 | 未使用节点提示 |

对照 UE 的 `INDEX_NONE` 哨兵传播（所有算子开头 `if (A == INDEX_NONE || B == INDEX_NONE) return INDEX_NONE;`）。**关键**：上游 Error 让下游"染上"哨兵，但下游**不重复 EmitError**（`SameAs` 去重）。

### `EmitError` 收集器（HLSLTranslator 私有段，第五部分整合）

```cpp
// HLSLTranslator.h private 段
std::vector<CompileError> errors_;
Node*       current_node_ = nullptr;   // 当前正在编译的节点（课7 的 CompileInputPin 设置）
std::string current_pin_;              // 当前正在编译的引脚（同上）

void EmitError(const std::string& msg,
               EErrorSeverity sev = EErrorSeverity::Error,
               const UUID& overrideNodeId = UUID::Invalid(),
               const std::string& overridePinName = "");
```

```cpp
// HLSLTranslator.cpp
void HLSLTranslator::EmitError(const std::string& msg, EErrorSeverity sev,
                               const UUID& overrideNodeId,
                               const std::string& overridePinName) {
    CompileError err;
    err.message  = msg;
    err.severity = sev;
    // 优先 override（算子显式指定），否则 current_* 上下文（课7 接上后自动有值）
    err.nodeId   = overrideNodeId.IsValid() ? overrideNodeId
                  : (current_node_ ? current_node_->id : UUID::Invalid());
    err.pinName  = !overridePinName.empty() ? overridePinName : current_pin_;

    for (const auto& existing : errors_)      // SameAs 去重
        if (existing.SameAs(err)) return;
    errors_.push_back(err);
}
```

**课 6 阶段** `current_node_` 是 nullptr，EmitError 的 nodeId 落到 `UUID::Invalid()`——正常，课 7 接 `CompileInputPin` 后自动有上下文。测试用显式 `overrideNodeId` 参数或友元访问。

> **调试 helper**：错误信息用的 `TypeName(EValueType)` 已定义在 TypeSystem（第一部分），单值位查表。

---

## 第五部分：双层编译器——抽象接口 + HLSL 实现

### UE 为什么拆两层（真实理由，`MaterialCompiler.h:141-144`）

UE 基类注释原话：*"The interface used to translate material expressions into executable code. Note: Most member functions should be pure virtual to force a FProxyMaterialCompiler override!"*

三个真实理由：

1. **代理编译器**（`FProxyMaterialCompiler`）：预览材质、Lightmass 烘烤等场景需要一个"替身编译器"——拦截特定调用（如把某些表达式替换成常量），其余转发给真编译器。没有抽象接口就没有代理。
2. **多种编译器类型**：`EMaterialCompilerType { Standard, Lightmass, MaterialProxy }`（`MaterialCompiler.h:86-91`）——表达式节点可以根据编译器类型选择不同行为。
3. **依赖倒置**：`UMaterialExpression`（表达式）只 include 抽象接口头，**不知道** HLSL 实现的存在。没有这层接口，"表达式调编译器、编译器调表达式"会形成循环依赖。教学版同理：`Expression`（L4）只依赖 `MaterialCompiler` 接口，`HLSLTranslator`（L5）实现接口并通过 `ExpressionRegistry`（L4）调用表达式——环被接口剪开。

```
Expression 子类 ──只认识──▶ MaterialCompiler（抽象接口，纯虚算子）
                                   ▲ 实现
HLSLTranslator ──创建/求值──▶ ExpressionRegistry ──▶ Expression 子类
```

> **接口文件放哪一层？** UE 没有这个烦恼（FMaterialCompiler 和 UMaterialExpression 同在 Engine 模块平铺）。教学版有分层铁律（每层只能引用下方层）：接口若放 `Compiler/`（L5），`Expression::Compile(MaterialCompiler*)`（L4）就向上依赖了——违规。**依赖倒置要求接口在"双方的公共下层"**：接口放 `src/Expression/Public/MaterialCompiler.h`（L4，与 Expression 同层，Expression 向同层引用没问题），`HLSLTranslator`（L5）include 并实现它。两个依赖方向都向下，铁律保住。

### 文件：`src/Expression/Public/MaterialCompiler.h`（抽象基类，L4）

对照 `FMaterialCompiler`（`MaterialCompiler.h:145`）——纯虚算子接口，本课只声明课 6 实现的算子集。**注意放在 Expression 层不是 Compiler 层**（分层铁律，见上面"接口文件放哪一层"）：

```cpp
#pragma once
#include "MaterialGraph/Public/Types.h"
#include "Expression/Public/UniformExpression.h"
#include <string>
#include <cstdint>

// 编译器抽象接口。对照 FMaterialCompiler（Engine/Public/MaterialCompiler.h:145）：
// 纯虚算子集，表达式节点的 Compile() 只看到这个接口。
// 放 L4（Expression 层）：Expression 向同层引用 OK，HLSLTranslator（L5）向下实现它 OK。
class MaterialCompiler {
public:
    virtual ~MaterialCompiler() = default;

    // 常量（全部走表达式树路径）
    virtual int32_t Constant(float v) = 0;
    virtual int32_t Constant2(float x, float y) = 0;
    virtual int32_t Constant3(float x, float y, float z) = 0;
    virtual int32_t Constant4(float x, float y, float z, float w) = 0;

    // 算术
    virtual int32_t Add(int32_t a, int32_t b) = 0;
    virtual int32_t Subtract(int32_t a, int32_t b) = 0;
    virtual int32_t Multiply(int32_t a, int32_t b) = 0;
    virtual int32_t Divide(int32_t a, int32_t b) = 0;
    virtual int32_t Power(int32_t base, int32_t exp) = 0;
    virtual int32_t Lerp(int32_t a, int32_t b, int32_t alpha) = 0;
    virtual int32_t Clamp(int32_t x, int32_t minVal, int32_t maxVal) = 0;

    // 一元
    virtual int32_t Abs(int32_t x) = 0;
    virtual int32_t Negate(int32_t x) = 0;
    virtual int32_t Sine(int32_t x) = 0;
    virtual int32_t Cosine(int32_t x) = 0;

    // 向量
    virtual int32_t Dot(int32_t a, int32_t b) = 0;
    virtual int32_t Cross(int32_t a, int32_t b) = 0;
    virtual int32_t Normalize(int32_t x) = 0;
    virtual int32_t Length(int32_t x) = 0;

    // 向量操作 / 类型转换
    virtual int32_t ComponentMask(int32_t input, bool r, bool g, bool b, bool a) = 0;
    virtual int32_t AppendVector(int32_t a, int32_t b) = 0;
    virtual int32_t ValidCast(int32_t code, EValueType dest_type) = 0;

    // 查询（对照 UE 基类的 GetType/GetParameterType/GetParameterUniformExpression）
    virtual EValueType           GetType(int32_t index) const = 0;
    virtual std::string          GetParameterCode(int32_t index) const = 0;
    virtual EValueType           GetParameterType(int32_t index) const = 0;
    virtual UniformExpression*   GetParameterUniformExpression(int32_t index) const = 0;
};
```

> UE 基类还有 `Error`/`ShouldStopTranslating`/`SetMaterialProperty` 等——教学版的错误收集是带定位的 `EmitError`（实现层内部），编译上下文（材质属性）到课 7 的 `Compile(Graph*)` 才出现，届时在基类**追加**声明。

### 文件：`src/Compiler/Public/HLSLTranslator.h`（实现类）

```cpp
#pragma once
#include "Expression/Public/MaterialCompiler.h"   // 抽象接口在 L4（见第五部分"接口文件放哪一层"）
#include "Compiler/Public/CodeChunk.h"
#include "Compiler/Public/CompileError.h"
#include "Compiler/Public/TypeSystem.h"
#include "MaterialGraph/Public/Graph.h"     // Node（错误定位 current_node_ 用）
#include "Core/Public/Hash.h"
#include <map>
#include <vector>
#include <string>

// HLSL 翻译器。对照 FHLSLMaterialTranslator（继承 FMaterialCompiler）。
class HLSLTranslator : public MaterialCompiler {
public:
    // === 算子实现（第七部分逐一展开，此处声明）===
    int32_t Constant(float v) override;
    int32_t Constant2(float x, float y) override;
    int32_t Constant3(float x, float y, float z) override;
    int32_t Constant4(float x, float y, float z, float w) override;
    int32_t Add(int32_t a, int32_t b) override;
    int32_t Subtract(int32_t a, int32_t b) override;
    int32_t Multiply(int32_t a, int32_t b) override;
    int32_t Divide(int32_t a, int32_t b) override;
    int32_t Power(int32_t base, int32_t exp) override;
    int32_t Lerp(int32_t a, int32_t b, int32_t alpha) override;
    int32_t Clamp(int32_t x, int32_t minVal, int32_t maxVal) override;
    int32_t Abs(int32_t x) override;
    int32_t Negate(int32_t x) override;
    int32_t Sine(int32_t x) override;
    int32_t Cosine(int32_t x) override;
    int32_t Dot(int32_t a, int32_t b) override;
    int32_t Cross(int32_t a, int32_t b) override;
    int32_t Normalize(int32_t x) override;
    int32_t Length(int32_t x) override;
    int32_t ComponentMask(int32_t input, bool r, bool g, bool b, bool a) override;
    int32_t AppendVector(int32_t a, int32_t b) override;
    int32_t ValidCast(int32_t code, EValueType dest_type) override;

    EValueType         GetType(int32_t index) const override;
    std::string        GetParameterCode(int32_t index) const override;
    EValueType         GetParameterType(int32_t index) const override;
    UniformExpression* GetParameterUniformExpression(int32_t index) const override;

    // === 常量/表达式快捷判定（算子和测试用，对照 UE IsExpressionConstantValue）===
    bool IsExpressionConstantValue(int32_t index, float compare) const;
    bool IsConstant(int32_t index) const;   // chunk 有表达式且树 IsConstant()

    // === 错误访问（测试 + 课19 UI 层读取）===
    const std::vector<CompileError>& GetErrors() const { return errors_; }
    void ClearErrors() { errors_.clear(); }   // 测试隔离用（GetErrors 返回 const 引用不能 clear）

    // 测试注入通道：课6 没有 TextureSample 等非表达式来源，测试用友元
    // 直接注入"纯 GPU 代码块"，验证算子的非表达式路径
    friend struct CompilerTestAccess;

private:
    // === 代码块管理（第六部分）===
    int32_t AddCodeChunk(EValueType type, const std::string& code, bool is_inline = false);
    int32_t AddInlinedCodeChunk(EValueType type, const std::string& code);
    int32_t AddUniformExpression(UniformExpression* expr, EValueType type,
                                 const std::string& code);
    int32_t ConstResultValue(EValueType type, const Vec4& value);   // 折叠结果 → 常量块
    std::string MakeSymbolName();
    std::string FormatConstantCode(EValueType type, const Vec4& value);

    // === 错误收集（第四部分）===
    std::vector<CompileError> errors_;
    Node*       current_node_ = nullptr;
    std::string current_pin_;
    void EmitError(const std::string& msg, EErrorSeverity sev = EErrorSeverity::Error,
                   const UUID& overrideNodeId = UUID::Invalid(),
                   const std::string& overridePinName = "");

    // === 状态 ===
    std::vector<CodeChunk> chunks_;                 // 全部代码块（UE CurrentScopeChunks，教学版无作用域只有一个数组）
    std::map<uint64_t, int32_t> hash_to_chunk_;     // 纯代码块的去重索引（UE 用线性扫描，教学版用 map 等价）
    std::vector<Ref<UniformExpression>> uniform_expressions_;  // 材质级唯一表达式表（UE UniformExpressions，跨属性共享）
    int32_t next_symbol_index_ = 0;
    Graph*  current_graph_ = nullptr;
    // node_cache_ → 课7 的 CompileExpression 用
};
```

---

## 第六部分：代码块管理与双层去重

UE 有**两条添加路径、两层去重**，教学版全部照搬：

| 路径 | UE 函数 | 去重方式 | 装什么 |
|------|---------|---------|--------|
| 纯代码块 | `AddCodeChunkInner`（`.cpp:3337`）| 代码串 hash（非内联块线性扫 `Hash` 比对，`.cpp:3365-3372`）| `(a + b)`、`pow(...)` 等纯 GPU 表达式 |
| 表达式块 | `AddUniformExpressionInner`（`.cpp:3610`）| **`IsIdentical` 语义去重**（材质级 + chunk 级两层，`.cpp:3644-3673`）| 常量字面量、FoldedMath/Unary 子树 |

### 纯代码块路径（对照 `AddCodeChunkInner` `.cpp:3337-3405`）

```cpp
// 生成局部变量名：Local0, Local1, ...（UE CreateSymbolName(TEXT("Local"))）
std::string HLSLTranslator::MakeSymbolName() {
    return "Local" + std::to_string(next_symbol_index_++);
}

// 纯代码块。对照 UE AddCodeChunkInner：
//  ① Unknown → INDEX_NONE（.cpp:3341）
//  ② 内联块不去重直接追加（.cpp:3355-3360，UE 也只对非内联块查重）
//  ③ 只有数值类型能声明局部变量（.cpp:3362 的 Type & (MCT_Float|MCT_LWCType|MCT_UInt|...)）
//  ④ 纹理等对象类型试图建块 → Errorf（.cpp:3389-3399）
int32_t HLSLTranslator::AddCodeChunk(EValueType type, const std::string& code, bool is_inline) {
    if (type == MCT_Unknown) return -1;                       // ①

    int32_t index = -1;
    if (is_inline) {
        index = (int32_t)chunks_.size();                      // ② 内联不去重
    } else if (type & MCT_Float || type == MCT_Float3x3 || type == MCT_Float4x4) {
        // ③ 数值类型才能建局部变量块 + hash 去重
        uint64_t hash = HashString(code);
        auto it = hash_to_chunk_.find(hash);
        if (it != hash_to_chunk_.end()) return it->second;    // hash 命中 → 复用

        CodeChunk chunk;
        std::string symbol = MakeSymbolName();   // 先生成一次，code 和 symbol_name 共用
        chunk.hash = hash;
        chunk.code = "\t" + std::string(TypeSystem::ToHLSLType(type)) + " "
                   + symbol + " = " + code + ";";  // UE .cpp:3380：定义串含声明+换行
        chunk.symbol_name = symbol;
        chunk.type = type;
        chunk.is_inline = false;
        chunks_.push_back(chunk);
        index = (int32_t)chunks_.size() - 1;
        hash_to_chunk_[hash] = index;
    } else if (type & MCT_Texture) {
        EmitError("Operation not supported on a Texture");
        return -1;                                            // ④ 对照 UE .cpp:3390
    } else {
        EmitError("Operation not supported for type " + TypeName(type));
        return -1;
    }
    chunks_[index].references = {};   // 调用方按需填
    return index;
}

// 内联简写（UE AddInlinedCodeChunk：零成本操作专用——mask/append/简单算术）
int32_t HLSLTranslator::AddInlinedCodeChunk(EValueType type, const std::string& code) {
    return AddCodeChunk(type, code, /*is_inline=*/true);
}
```

> UE 注释原话（`.cpp:3530-3535`）："Creating local variables instead of inlining simplifies the generated code and reduces redundant expression chains, Making compiles faster and enabling the shader optimizer to do a better job."——**会产生真实指令**的表达式（函数调用、纹理采样）用非内联块，让生成代码清晰、shader 编译快、优化器效果好；零成本操作（mask、简单算术）内联。

### 表达式块路径（对照 `AddUniformExpressionInner` `.cpp:3610-3723`）

```cpp
// 表达式块。对照 UE AddUniformExpressionInner 的两层 IsIdentical 去重：
//  第一层（.cpp:3646-3673）：扫材质级 UniformExpressions 表，IsIdentical 命中 →
//    若当前 chunks 里已有同表达式的块 → 直接复用那个块（delete 新表达式）
//  第二层（.cpp:3709-3719）：没命中 → 新块 + 表达式登记进材质级表
//
// ★ 所有权契约：调用方 new 出裸指针传入 = 所有权转移给本函数。
//   三条出口：①第一层命中 → delete（丢弃新树）；②第二层命中 → delete（复用登记树）；
//   ③没命中 → 被新 chunk 的 Ref 接管。任何路径都不泄漏、不双删。
int32_t HLSLTranslator::AddUniformExpression(UniformExpression* expr,
                                             EValueType type,
                                             const std::string& code) {
    if (type == MCT_Unknown) { delete expr; return -1; }

    // 第一层：材质级表达式表 + chunk 级复用
    for (int32_t i = 0; i < (int32_t)chunks_.size(); ++i) {
        UniformExpression* existing = chunks_[i].uniform_expression.get();
        if (existing && existing->IsIdentical(expr)) {
            delete expr;              // UE .cpp:3662：语义相同 → 丢弃新树，复用旧块
            return i;
        }
    }
    for (const auto& registered : uniform_expressions_) {
        if (registered->IsIdentical(expr)) {
            // 材质级已有此表达式（chunk 在别的属性下）——复用登记树，仍建新块。
            // 原 new 的树作废，必须 delete（否则泄漏——裸指针没人接管了）
            delete expr;
            expr = registered.get();
            break;
        }
    }

    CodeChunk chunk;
    chunk.hash = HashString("expr_" + code);
    chunk.code = code;                          // 表达式块：code 直接嵌（无 SymbolName）
    chunk.type = type;
    chunk.is_inline = false;                    // UE 带表达式构造 bInline=false
    chunk.uniform_expression = expr;            // 挂树（chunk 持引用；若是登记树，两处 Ref 共享，计数保命）
    chunks_.push_back(chunk);
    int32_t index = (int32_t)chunks_.size() - 1;

    // 第二层：登记材质级唯一表达式表
    bool registered_before = false;
    for (const auto& r : uniform_expressions_)
        if (r.get() == expr) { registered_before = true; break; }
    if (!registered_before)
        uniform_expressions_.push_back(chunk.uniform_expression);
    return index;
}
```

**为什么要两层去重**：代码 hash 只能发现"字符串相同"，发现不了"字符串不同但语义相同"——`Constant(1) + Constant(2)` 和 `Constant(3)` 的代码串分别是 `(1.0 + 2.0)` 和 `3.0`，但 `IsIdentical`（递归比较树）在立即折叠后能判定它们相等（折叠让两个都变成 `UniformConstant(3)`，第二层材质级表保证整个材质只保留一份）。UE 靠这层让**多个材质属性共享同一表达式**（`.cpp:3654` 注释原话："This allows multiple material properties to share uniform expressions"）。

### 折叠结果快捷路径（对照 UE `ConstResultValue`）

```cpp
// 折叠出的常量 → 常量块（对照 UE ConstResultValue：new FMaterialUniformExpressionConstant + AddUniformExpression）
int32_t HLSLTranslator::ConstResultValue(EValueType type, const Vec4& value) {
    return AddUniformExpression(new UniformConstant(value, type), type,
                                FormatConstantCode(type, value));
}

// 常量字面量格式化（干净输出：0/1 特判，向量 float3(x,y,z)）
std::string HLSLTranslator::FormatConstantCode(EValueType type, const Vec4& v) {
    int n = GetComponentCount(type);
    if (n <= 1) {
        if (v.x == 0.0f) return "0.0";
        if (v.x == 1.0f) return "1.0";
        return std::to_string(v.x);
    }
    std::string s = std::string(TypeSystem::ToHLSLType(type)) + "(" + std::to_string(v.x);
    if (n >= 2) s += ", " + std::to_string(v.y);
    if (n >= 3) s += ", " + std::to_string(v.z);
    if (n >= 4) s += ", " + std::to_string(v.w);
    return s + ")";
}
```

### 查询方法（带边界保护，assert 开发期断言 + if 兜底）

```cpp
std::string HLSLTranslator::GetParameterCode(int32_t index) const {
    assert(index >= 0 && index < (int32_t)chunks_.size());
    if (index < 0 || index >= (int32_t)chunks_.size()) return "0.0";
    const auto& c = chunks_[index];
    if (c.uniform_expression) return c.code;   // 表达式块：定义串直接用（UE 字段注释语义）
    return c.is_inline ? c.code : c.symbol_name;
}
EValueType HLSLTranslator::GetType(int32_t index) const {
    assert(index >= 0 && index < (int32_t)chunks_.size());
    if (index < 0 || index >= (int32_t)chunks_.size()) return MCT_Unknown;
    return chunks_[index].type;
}
EValueType HLSLTranslator::GetParameterType(int32_t index) const { return GetType(index); }

UniformExpression* HLSLTranslator::GetParameterUniformExpression(int32_t index) const {
    assert(index >= 0 && index < (int32_t)chunks_.size());
    if (index < 0 || index >= (int32_t)chunks_.size()) return nullptr;
    return chunks_[index].uniform_expression.get();
}

// chunk 是否常量 = 有表达式 && 树 IsConstant()（递归）
bool HLSLTranslator::IsConstant(int32_t index) const {
    UniformExpression* e = GetParameterUniformExpression(index);
    return e && e->IsConstant();
}

// 对照 UE IsExpressionConstantValue：表达式是常量且所有有效分量 == compare
bool HLSLTranslator::IsExpressionConstantValue(int32_t index, float compare) const {
    // (&v.x)[i] 按分量遍历依赖 Vec4 四成员内存连续——编译期锁死这个前提
    static_assert(sizeof(Vec4) == 4 * sizeof(float), "Vec4 must be tightly packed float[4]");
    if (!IsConstant(index)) return false;
    MaterialRenderContext ctx;
    Vec4 v; GetParameterUniformExpression(index)->GetNumberValue(ctx, v);
    int n = std::max(1, GetComponentCount(GetType(index)));
    for (int i = 0; i < n; ++i)
        if ((&v.x)[i] != compare) return false;
    return true;
}
```

---

## 第七部分：算子实现——三轨判定（对照 UE 真实结构）

### Constant/2/3/4：常量也走表达式路径（对照 `.cpp:5211-5233`）

UE 的 `Constant` 一行建 `FMaterialUniformExpressionConstant`（`FLinearColor(X,X,X,X)` + 类型标签）走 `AddUniformExpression`——**常量从来不是特殊 chunk，就是表达式块的叶子**：

```cpp
int32_t HLSLTranslator::Constant(float v) {
    // UE .cpp:5214：FLinearColor(X,X,X,X)——标量也填满 4 分量，类型标签 MCT_Float
    return AddUniformExpression(new UniformConstant(Vec4(v, v, v, v), MCT_Float),
                                MCT_Float, FormatConstantCode(MCT_Float, Vec4(v,v,v,v)));
}
int32_t HLSLTranslator::Constant2(float x, float y) {
    return AddUniformExpression(new UniformConstant(Vec4(x, y, 0, 0), MCT_Float2),
                                MCT_Float2, FormatConstantCode(MCT_Float2, Vec4(x,y,0,0)));
}
int32_t HLSLTranslator::Constant3(float x, float y, float z) {
    return AddUniformExpression(new UniformConstant(Vec4(x, y, z, 0), MCT_Float3),
                                MCT_Float3, FormatConstantCode(MCT_Float3, Vec4(x,y,z,0)));
}
int32_t HLSLTranslator::Constant4(float x, float y, float z, float w) {
    return AddUniformExpression(new UniformConstant(Vec4(x, y, z, w), MCT_Float4),
                                MCT_Float4, FormatConstantCode(MCT_Float4, Vec4(x,y,z,w)));
}
```

### 立即折叠 vs 建树：UE 的真实分工

读 UE 源码（`.cpp:9484-9711`）会发现一个重要事实——**不是所有算子都立即折叠纯常量**：

| 算子 | 两边纯常量时 UE 的行为 |
|------|----------------------|
| `Add`/`Sub`（`.cpp:9494-9498`）| **直接建 FoldedMath 树**（不立即算）——值的求值交给 preshader 运行时 |
| `Mul`（`.cpp:9620-9628`）| **立即折叠**：`GetConstParameterValue` 拿值 → `ConstResultValue` |
| `Div`（`.cpp:9675-9683`）| **立即折叠**，且除零时不折（`!IsExpressionConstantValue(B, 0.0f)`，注释说"HLSL 编译器不喜欢 inf 输出"）|

为什么 Mul/Div 特殊？因为它们的**代数化简**（x*0、x*1、x/1）需要先知道"是不是常量 0/1"，顺手就把纯常量算了；Add/Sub 没有化简需求，UE 就把求值推迟给 preshader。

**教学版规则**：没有 preshader 字节码运行时，统一采用 Mul/Div 的模式——**两边 `IsConstant()` → 立即求值折叠；两边有表达式（至少一边含参数）→ 建 FoldedMath 树**（树的 `GetNumberValue` 是 C++ 直接求值，语义与 preshader 等价）。三个机制（`IsConstant` 递归判定、`ConstResultValue`、`FoldedMath` 树）与 UE 一一对应。

### Add——黄金模板（对照 `.cpp:9484-9518`，删去 LWC/导数分支）

```cpp
int32_t HLSLTranslator::Add(int32_t a, int32_t b) {
    // 段 1：哨兵传播 + 类型检查（UE .cpp:9486 INDEX_NONE 检查在最前）
    if (a < 0 || b < 0) return -1;
    EValueType resultType = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    if (resultType == MCT_Unknown) {
        EmitError("Add inputs incompatible: A=" + TypeName(GetType(a))
                  + ", B=" + TypeName(GetType(b)), EErrorSeverity::Error,
                  current_node_ ? current_node_->id : UUID::Invalid(), "A/B");
        return -1;
    }

    // 段 2：表达式三轨（UE .cpp:9491-9498 的 ExpressionA && ExpressionB 分支）
    UniformExpression* ea = GetParameterUniformExpression(a);
    UniformExpression* eb = GetParameterUniformExpression(b);
    if (ea && eb) {
        if (ea->IsConstant() && eb->IsConstant()) {
            // 纯常量 → 立即求值折叠（Mul/Div 模式）
            MaterialRenderContext ctx;
            Vec4 va, vb;
            ea->GetNumberValue(ctx, va);
            eb->GetNumberValue(ctx, vb);
            // 维度对齐：标量广播到较宽的分量（Float1+Float3 → 3 分量）
            AlignComponents(va, GetType(a), vb, GetType(b), resultType);
            return ConstResultValue(resultType, va + vb);
        }
        // 含参数 → 建树（UE .cpp:9497：new FoldedMath(A, B, FMO_Add)）
        return AddUniformExpression(
            new UniformFoldedMath(ea, eb, EFoldedMathOp::Add, resultType),
            resultType,
            "(" + GetParameterCode(a) + " + " + GetParameterCode(b) + ")");
    }

    // 段 3：纯 HLSL 发射（UE .cpp:9514：AddCodeChunk "(%s + %s)"——简单算术内联）
    return AddInlinedCodeChunk(resultType,
        "(" + GetParameterCode(a) + " + " + GetParameterCode(b) + ")");
}
```

（`AlignComponents` 是私有 helper：按 `GetComponentCount` 把窄分量值广播到结果宽度，配合 `UniformConstant` 的 4 分量存储。）

### Subtract（同模板，`-` / `FMO_Sub`）

同 Add，把 `+` 换 `-`、`EFoldedMathOp::Sub`。UE 的 Sub 尾部有 LWC 截断逻辑（`.cpp:9556-9568`），教学版无 LWC 不需要。

### Multiply（对照 `.cpp:9574-9644`，代数化简 + 标量提升）

```cpp
int32_t HLSLTranslator::Multiply(int32_t a, int32_t b) {
    if (a < 0 || b < 0) return -1;
    EValueType resultType = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    if (resultType == MCT_Unknown) { /* 同 Add 的 EmitError */ return -1; }

    // === 代数化简（UE .cpp:9591-9613 逐条对照）===
    // x * 0 / 0 * x → 0（UE .cpp:9591：ConstArithmeticResultValue(A, B, 0.0)）
    if (IsExpressionConstantValue(a, 0.0f) || IsExpressionConstantValue(b, 0.0f))
        return ConstResultValue(resultType, Vec4(0,0,0,0));

    // x * 1 → x（UE .cpp:9595-9613：不是简单返回索引！标量×向量要补分量）
    if (IsExpressionConstantValue(b, 1.0f)) return PromoteToType(a, resultType);
    if (IsExpressionConstantValue(a, 1.0f)) return PromoteToType(b, resultType);

    // === 表达式三轨（UE .cpp:9616-9632）===
    UniformExpression* ea = GetParameterUniformExpression(a);
    UniformExpression* eb = GetParameterUniformExpression(b);
    if (ea && eb) {
        if (ea->IsConstant() && eb->IsConstant()) {
            MaterialRenderContext ctx;
            Vec4 va, vb; ea->GetNumberValue(ctx, va); eb->GetNumberValue(ctx, vb);
            AlignComponents(va, GetType(a), vb, GetType(b), resultType);
            return ConstResultValue(resultType, va * vb);   // UE .cpp:9626：ValueA * ValueB
        }
        return AddUniformExpression(
            new UniformFoldedMath(ea, eb, EFoldedMathOp::Mul, resultType), resultType,
            "(" + GetParameterCode(a) + " * " + GetParameterCode(b) + ")");
    }
    return AddInlinedCodeChunk(resultType,
        "(" + GetParameterCode(a) + " * " + GetParameterCode(b) + ")");
}

// 标量×向量的 ×1 直通（UE .cpp:9604-9613 的 while AppendVector 循环）：
// Float1 * Float3(1,1,1) 时 a 是 Float1，直接返回 a 类型不够，要 Append 补分量
int32_t HLSLTranslator::PromoteToType(int32_t code, EValueType resultType) {
    int32_t base = code;        // 始终 Append 原始操作数（UE .cpp:9610 传 ConstOneReturnValue，不是 Return！）
    int32_t result = code;
    while (GetComponentCount(GetType(result)) < GetComponentCount(resultType)) {
        result = AppendVector(result, base);
        if (result < 0) return -1;   // AppendVector 失败防死循环（GetType(-1)=Unknown → 分量 0 → 恒 < 目标）
    }
    return result;
}
```

> **为什么 ×1 直通不能无脑返回索引**：`Float1 * Float3` 结果类型是 Float3，但被直通的操作数是 Float1——下游按 Float3 用它会分量不足。UE 用 while-AppendVector 循环把标量复制成向量（`.cpp:9608-9611` 注释原话："This should only be possible with scalar x vector arithmatic"）。这是教科书级坑，UE 用循环处理了。
>
> **循环体第二个参数必须是原始标量**（`base`），不能写 `AppendVector(result, result)`——那样分量翻倍：Float1→Float2→Float4，目标 Float3 时会得到 Float4 而且循环多跑一轮。UE 原文循环外保存 `ConstOneReturnValue`，循环内始终传它，教学版同构。

### Divide（对照 `.cpp:9646-9711`，四条化简 + rcp 技巧 + 除零 Warning）

```cpp
int32_t HLSLTranslator::Divide(int32_t a, int32_t b) {
    if (a < 0 || b < 0) return -1;
    EValueType resultType = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    if (resultType == MCT_Unknown) { /* 同 Add 的 EmitError */ return -1; }

    // 0 / x → 0（UE .cpp:9655）
    if (IsExpressionConstantValue(a, 0.0f))
        return ConstResultValue(resultType, Vec4(0,0,0,0));
    // x / 1 → x（含标量提升，UE .cpp:9659-9668）
    if (IsExpressionConstantValue(b, 1.0f))
        return PromoteToType(a, resultType);

    UniformExpression* ea = GetParameterUniformExpression(a);
    UniformExpression* eb = GetParameterUniformExpression(b);
    if (ea && eb) {
        // 除零：UE 不折（.cpp:9675 注释"hlsl compiler does not like inf"）——
        // 跳过立即折叠、落到建树分支，但先记 Warning（错误进 errors_ 列表，课 19 UI 才能高亮）。
        // 注意 UE 原文把 !IsExpressionConstantValue(B, 0.0f) 并进折叠条件（.cpp:9675），
        // 教学版拆成先 Warning 再判断，行为等价
        if (IsExpressionConstantValue(b, 0.0f)) {
            EmitError("Division by zero: divisor (pin 'B') is constant 0",
                      EErrorSeverity::Warning,
                      current_node_ ? current_node_->id : UUID::Invalid(), "B");
        }
        if (ea->IsConstant() && eb->IsConstant()
            && !IsExpressionConstantValue(b, 0.0f)) {         // 纯常量立即折，除零除外（UE .cpp:9675）
            MaterialRenderContext ctx;
            Vec4 va, vb; ea->GetNumberValue(ctx, va); eb->GetNumberValue(ctx, vb);
            AlignComponents(va, GetType(a), vb, GetType(b), resultType);
            return ConstResultValue(resultType, va / vb);
        }
        return AddUniformExpression(
            new UniformFoldedMath(ea, eb, EFoldedMathOp::Div, resultType), resultType,
            "(" + GetParameterCode(a) + " / " + GetParameterCode(b) + ")");
    }

    // rcp 技巧（UE .cpp:9688-9699）：除数是【非纯常量】表达式时，
    // 把倒数折进表达式树（1/b 在 CPU 端求值），GPU 端只剩乘法——
    // "Division is often optimized as multiplication by reciprocal"（UE 注释原话）
    if (eb && !eb->IsConstant()) {
        int32_t rcpB = AddUniformExpression(
            new UniformFoldedUnary(eb, EFoldedUnaryOp::Rcp), GetType(b),
            "rcp(" + GetParameterCode(b) + ")");
        return Multiply(a, rcpB);
    }

    return AddInlinedCodeChunk(resultType,
        "(" + GetParameterCode(a) + " / " + GetParameterCode(b) + ")");
}
```

### 其他算子（每个按 Add 模板，5-15 行）

| 算子 | 表达式树轨道 | HLSL 轨道 | 备注 |
|------|-------------|-----------|------|
| `Power` | 两边常量 → 求值 `pow`；`pow(x,0)→1`、`pow(x,1)→x` 化简 | `AddCodeChunk`（函数调用非内联）：`pow(a, b)` | UE `Power`（`.cpp:9817`）|
| `Abs`/`Negate`/`Sine`/`Cosine` | 常量 → `UniformFoldedUnary` 求值 | 一元短表达式内联：`abs(x)`/`-x`/`sin(x)`/`cos(x)` | 一元树节点用 |
| `Lerp(a,b,alpha)` | 三元无化简，HLSL 直接发 | `AddCodeChunk`：`lerp(a, b, alpha)` | 结果类型 = GetArithmeticResultType(a,b) |
| `Clamp(x,min,max)` | 同上 | `AddCodeChunk`：`clamp(x, min, max)` | 类型同 x |
| `Dot(a,b)` | 两边有表达式 → `FoldedMath(FMO_Dot, 维度标签)`（UE `.cpp:9742` 连 ValueType 都传了）| 内联 `dot(a, b)`，结果 `MCT_Float` | UE `.cpp:9713`：标量×标量退化为 Mul |
| `Cross(a,b)` | `FoldedMath(FMO_Cross)` | `AddCodeChunk`：`cross(a, b)`，结果 `MCT_Float3` | 要求两边 Float3 |
| `Normalize(x)` | 常量 → 求值 | 内联 `normalize(x)` | 类型同输入 |
| `Length(x)` | 常量 → 求值 | 内联 `length(x)`，结果 `MCT_Float` | |
| `ComponentMask` | 常量 → 按掩码取分量重组 | 内联 `v.xz` 形式 | 零成本操作，内联 |
| `AppendVector` | 常量 → 拼接两树分量 | 内联 `float4(a, b)` | 类型 = 两边分量数之和 |
| `ValidCast` | 常量 → 求值后重打类型标签 | 截断 `float4→float3` / 复制提升 `float1→float3` | 对照 UE `ValidCast`（`MaterialCompiler.h:237` 注释：truncate 不加分量、float1 复制提升任意）|

`ValidCast` 规则（UE 注释原意）：`float4 → float3` 截断 ✅；`float2 → float3` 加分量 ❌（报错）；`float1 → 任意 float` 复制提升 ✅。

---

## 第八部分：实现步骤（按依赖顺序）

### 旧代码迁移对照（开工前先看，避免新旧混杂）

课 6 是编译器结构的**最终形态**：旧 `src/Compiler/` 里的过渡代码按此表处置。这不是"推翻课 5"——课 5 的反射系统（Reflection/Expression 层）不受影响，被替换的只是课 6 自己的半成品。

| 旧代码（`src/Compiler/`）| 处置 | 去向 |
|---|---|---|
| `MaterialCompiler.h/.cpp`（旧单层版，含 `TextureSample/If/Cast/Compile(Graph*)` 声明）| **拆掉重写** | 纯虚算子接口 → `src/Expression/Public/MaterialCompiler.h`（删去 `TextureSample/If`（课14/15）、`Cast` → `ValidCast`、`Compile(Graph*)`（课7）、`Negative` → 改名 `Negate` 对齐 UE）|
| `CodeChunk.h`（variant 版）| **字段升级** | 换全字段版：`is_constant`+`constant_value` → `uniform_expression` 树指针，加 `references` |
| `ConstantFolding.h`（variant 折叠）| **退役** | 折叠逻辑成为 `UniformFoldedMath/Unary::GetNumberValue` 的一部分（树求值即折叠），不再需要独立文件 |
| `TypeSystem.h/.cpp` | **保留扩展** | `GetArithmeticResultType` 重写为 UE 4 步逻辑 + 加 `TypeName`；`ToHLSLType` 不变 |
| `CompileError.h` | **保留扩展** | `EmitError` 挪进 HLSLTranslator；`CompileResult` 移到本文件成为最终版 |
| `MaterialGraph/Public/GraphCompiler.h/.cpp`（课 3 遗留）| **废弃** | 它是数据模型层的旧遍历器，职责被课 7 的 `HLSLTranslator::Compile(Graph*)` 完全取代；课 6 起不再引用，文件可删（若课 4 的测试还引用它，把测试一并退役）|

1. **Types.h 改造**：`EValueType` 换 bitmask（MCT_* 单值位 + 类别掩码）；`GetComponentCount` 适配；全项目 `EValueType::Float3` 等旧引用改 `MCT_Float3`（编译器会逐个报错，正好逐个改）。
   > 注意：这是类型系统的**最终形态升级**（课 3 的普通 enum 是它课 6 之前的形态，UE 对应物从一开始就是 bitmask）——`EValueType` 这个概念、它在 Types.h 的位置、`GetComponentCount`/`CanImplicitConvert` 的职责全部不变，变的是表示方式。
2. **TypeSystem**：`GetArithmeticResultType`（对齐 UE 4 步逻辑）+ `ToHLSLType` + `TypeName`
3. **UniformExpression.h**（`src/Expression/Public/`，L4）：基类 → `UniformConstant` → `EFoldedMathOp` + `UniformFoldedMath` → `EFoldedUnaryOp` + `UniformFoldedUnary`（`MaterialRenderContext` 空壳结构体）
4. **CodeChunk.h**：全字段版（含 `uniform_expression`）
5. **CompileError.h**：`EErrorSeverity` / `CompileError` / `CompileResult`（纯数据，无实现文件）
6. **MaterialCompiler.h**（`src/Expression/Public/`，L4）：抽象基类（纯虚接口，无 .cpp）
7. **HLSLTranslator 骨架**：成员 + `EmitError`（SameAs 去重）
8. **代码块管理**：`MakeSymbolName` / `AddCodeChunk` / `AddInlinedCodeChunk` / `AddUniformExpression`（双层去重）/ `ConstResultValue` / `FormatConstantCode` / 查询四件套 + `IsConstant` / `IsExpressionConstantValue` / `AlignComponents`
9. **常量**：`Constant/2/3/4`
10. **算术**：`Add` → `Subtract` → `Multiply`（含 `PromoteToType`）→ `Divide`（含 rcp）
11. **其余算子**：`Power/Lerp/Clamp/Abs/Negate/Sine/Cosine/Dot/Cross/Normalize/Length/ComponentMask/AppendVector/ValidCast`

每实现一块放开 `compiler_test.cpp` 对应测试。

---

## 第九部分：测试（`compiler_test.cpp`）

```cpp
// 0. 测试通道：注入"纯 GPU 代码块"（课6 没有纹理等非表达式来源，
//    用友元 CompilerTestAccess 造一个，验证算子的非表达式路径）
struct CompilerTestAccess {
    static int32_t MakeRawChunk(HLSLTranslator& c, EValueType type, const std::string& code) {
        CodeChunk chunk;
        chunk.hash = HashString(code);
        chunk.code = code;
        chunk.type = type;
        chunk.is_inline = true;   // 简单起见内联块（无 uniform_expression）
        c.chunks_.push_back(chunk);
        return (int32_t)c.chunks_.size() - 1;
    }
};
MaterialRenderContext ctx;   // 求值上下文

// 1. 标量折叠：Constant(1) + Constant(2) → 折叠成常量 3（无加法代码）
HLSLTranslator c;
int s = c.Add(c.Constant(1.0f), c.Constant(2.0f));
assert(c.IsConstant(s));
UniformExpression* se = c.GetParameterUniformExpression(s);
Vec4 v; se->GetNumberValue(ctx, v);
assert(v.x == 3.0f);
// 折叠后是"纯常量叶子"而非 FoldedMath 树：和 UniformConstant(3) 语义相等
assert(se->IsIdentical(UniformConstant(Vec4(3,3,3,3), MCT_Float)));

// 2. 向量折叠：Constant3(1,0,0) + Constant3(0,1,0) → (1,1,0)
int a = c.Constant3(1,0,0);
int b = c.Constant3(0,1,0);
int vec = c.Add(a, b);
assert(c.IsConstant(vec));
c.GetParameterUniformExpression(vec)->GetNumberValue(ctx, v);
assert(v.x == 1.0f && v.y == 1.0f && v.z == 0.0f);
assert(c.GetParameterCode(vec) == "float3(1.000000, 1.000000, 0.000000)");  // 字面量直接可用

// 3. x*1 直通 + 标量提升（UE 的 while-AppendVector 路径）
int raw = CompilerTestAccess::MakeRawChunk(c, MCT_Float, "View.GameTime");  // 非表达式块
int m1 = c.Multiply(raw, c.Constant(1.0f));       // Float1 * Float1 → 直通
assert(m1 == raw);
int vec3c = c.Constant3(1,1,1);
int m2 = c.Multiply(raw, vec3c);                  // Float1 * Float3(1,1,1)
assert(c.GetType(m2) == MCT_Float3);              // 类型提升到位（AppendVector 补分量）

// 4. x*0 → 0；嵌套 2*3+4 → 10
assert(c.IsExpressionConstantValue(c.Multiply(c.Constant(2.0f), c.Constant(0.0f)), 0.0f));
int n = c.Add(c.Multiply(c.Constant(2.0f), c.Constant(3.0f)), c.Constant(4.0f));
assert(c.IsExpressionConstantValue(n, 10.0f));

// 5. 除零：Warning + 不折叠（建树）+ 错误进列表
//    UE 行为（.cpp:9675）：除零跳过立即折叠，落到建树分支——不产 inf 常量
c.ClearErrors();
int dz = c.Divide(c.Constant(1.0f), c.Constant(0.0f));
assert(!c.IsExpressionConstantValue(dz, 0.0f));   // 不是常量 0——根本没折叠
assert(c.GetParameterUniformExpression(dz));      // 是表达式树（FoldedMath Div）
assert(c.IsConstant(dz));                          // 树本身全常量（两个叶子都是常量）
assert(!c.GetErrors().empty());
assert(c.GetErrors().back().severity == EErrorSeverity::Warning);
assert(c.GetErrors().back().pinName == "B");      // 精确到除数引脚

// 6. 表达式树构建 + 双层去重（除零路径：本课没有参数节点，
//    纯常量一律立即折叠——树只在"除零跳过折叠"时出现，UE 同款行为）
int t1 = c.Divide(c.Constant(1.0f), c.Constant(0.0f));   // 建 FoldedMath(Div) 树（跳过折叠）
int t2 = c.Divide(c.Constant(1.0f), c.Constant(0.0f));   // 再来一次同样的
assert(t1 == t2);   // IsIdentical 去重：同一棵树只建一个 chunk

// 7. 类型不匹配 EmitError（Float2 + Float3）
c.ClearErrors();
int bad = c.Add(c.Constant2(1,2), c.Constant3(1,2,3));
assert(bad < 0);                                     // 哨兵
assert(!c.GetErrors().empty());
assert(c.GetErrors().back().severity == EErrorSeverity::Error);
// 错误去重：同错误重复触发只存一份
size_t errCount = c.GetErrors().size();
c.Add(c.Constant2(1,2), c.Constant3(1,2,3));
assert(c.GetErrors().size() == errCount);

// 8. 非表达式路径：两个纯 GPU 块相加 → 内联 HLSL，无树
int r1 = CompilerTestAccess::MakeRawChunk(c, MCT_Float, "View.GameTime");
int r2 = CompilerTestAccess::MakeRawChunk(c, MCT_Float, "View.RealTime");
int r3 = c.Add(r1, r2);
assert(c.GetParameterUniformExpression(r3) == nullptr);          // 无表达式
assert(c.GetParameterCode(r3) == "(View.GameTime + View.RealTime)");

// 9. 查询边界保护
assert(c.GetType(9999) == MCT_Unknown);
assert(c.GetParameterCode(-1) == "0.0");
```

> 测试 6 说明树的验证策略：`FoldedMath` 树的**大量出现**要等参数表达式（课 20 的 `ScalarParameter` 不是 `IsConstant`，两边有它就走建树轨道）——本课用除零路径（UE 明确跳过折叠建树）验证树构建、递归 `IsConstant`、`IsIdentical` 去重三个机制都已工作。

---

## 第十部分：UE 5.8.1 参考（相对 `Engine/` 路径，行号实测）

| 本课概念 | UE 对应 | 位置 |
|---------|---------|------|
| `EValueType` bitmask | `EMaterialValueType` | `Source/Runtime/Engine/Public/MaterialValueType.h`（全文 93 行）|
| 类别判断 `Type & MCT_Float` | 同（`AddCodeChunkInner` 用它判断能否建局部变量）| `HLSLMaterialTranslator.cpp:3362` |
| `CodeChunk` | `FShaderCodeChunk` | `Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.h:83-174` |
| 表达式树基类 | `FMaterialUniformExpression` | `Source/Runtime/Engine/Private/Materials/MaterialUniformExpressions.h:56-81` |
| 常量叶子（4 分量 + 类型标签）| `FMaterialUniformExpressionConstant` | 同上 `.h:257-306` |
| 二元折叠节点 + FMO 枚举 | `FMaterialUniformExpressionFoldedMath` + `EFoldedMathOperation` | 同上 `.h:1093-1158` |
| 抽象编译器基类 | `FMaterialCompiler` | `Source/Runtime/Engine/Public/MaterialCompiler.h:145` |
| 编译器类型枚举（Standard/Lightmass/MaterialProxy）| `EMaterialCompilerType` | 同上 `:86-91` |
| `GetArithmeticResultType` | 同名（MCT_Float/MCT_Float1 标量特判）| `HLSLMaterialTranslator.cpp:4221-4261` |
| `Constant/2/3/4` 走表达式路径 | 同名 | 同上 `.cpp:5211-5233` |
| 纯代码块管理 + 类型限制 | `AddCodeChunkInner` | 同上 `.cpp:3337-3405` |
| 表达式块 + IsIdentical 双层去重 | `AddUniformExpressionInner` | 同上 `.cpp:3610-3723` |
| `Add`/`Sub`/`Mul`/`Div`/`Dot` | 同名（三轨判定原貌）| 同上 `.cpp:9484 / 9520 / 9574 / 9646 / 9713` |
| rcp 除法优化 | `FMaterialUniformExpressionRcp` + Div 内使用 | 同上 `.cpp:9696` |
| `ValidCast` 截断/提升规则 | 基类注释 | `MaterialCompiler.h:233-238` |

**搜索关键词**（在 UE 源码里）：`FShaderCodeChunk`、`AddUniformExpressionInner`、`IsIdentical`、`FMaterialUniformExpressionFoldedMath`、`IsExpressionConstantValue`、`ConstResultValue`、`EMaterialCompilerType`。

---

## 第十一部分：已踩坑 / 注意

| 坑 | 现象 | 教训 |
|----|------|------|
| 掩码用 `==` 判断 | `type == MCT_Float` 对 Float3 永远为假 | 类别判断必须 `&`，`==` 只对单值位合法 |
| `MCT_Float` 和 `MCT_Float1` 混用 | 标量表达式返回 `MCT_Float`（UE 规则），只判 `Float1` 会漏 | 推导/分量数两处都同时接受两者（UE `.cpp:4250`、`GetNumComponents` 同款）|
| ×1 直通直接返回索引 | `Float1 * Float3` 直通后下游分量不足 | 走 `PromoteToType` 的 while-AppendVector（UE `.cpp:9608`）|
| 表达式块给 SymbolName | UE 带表达式的构造函数根本没有 SymbolName 参数 | 有表达式 → 定义串直接用，`GetParameterCode` 第一判 |
| 纯常量 Add/Sub 建树不折叠 | 输出 `(1.0 + 2.0)` 而非 `3.0` | 教学版统一"纯常量立即折"（Mul/Div 模式），见第七部分分工说明 |
| `dynamic_cast` 没开 RTTI | `IsIdentical` 全返回假 | CMake 确认编译选项含 RTTI（MSVC 默认开）|
| 除零返回 inf/nan 常量 | HLSL 编译器拒绝 inf 字面量（UE 注释原话）| 除零不折 + Warning + 建树（对齐 UE `.cpp:9675` 原版行为）|
| 递归错误重复报告 | 错误列表刷屏 | `EmitError` 里 `SameAs` 去重 + 下游不重复 emit |
| 算子内 pinName 不准 | 算子不知道 A 还是 B 出错 | 算子内 EmitError 显式 override pinName（`"A/B"` 或 `"B"`）|

---

## 完成标志

本课真正实现的（每一条都能本课勾选）：

- [ ] `EValueType` bitmask 改造（MCT_* 单值位 + `MCT_Float`/`MCT_Texture` 掩码，位值对齐 UE；含纹理变体四类 + MaterialAttributes/ShadingModel/Substrate 三类，共 17 个单值位）+ 全项目旧枚举引用迁移
- [ ] 新类型位的编译器层消费：`GetArithmeticResultType` 拦截（纹理掩码 + 打包三件套 ==）+ `ToHLSLType` 映射（FSubstrateData 等）+ `TypeName` 可读名 + `GetComponentCount` 打包值返回 0
- [ ] `GetComponentCount` 适配（`MCT_Float` 与 `MCT_Float1` 都返回 1）
- [ ] `TypeSystem::GetArithmeticResultType`（4 步逻辑对齐 UE）+ `ToHLSLType` + `TypeName`
- [ ] `UniformExpression` 基类（`IsConstant` / `IsIdentical` / `GetNumberValue(ctx, out)` 三个虚函数，语义对齐 UE）
- [ ] `UniformConstant`（Vec4 4 分量 + 类型标签，对齐 `FMaterialUniformExpressionConstant`）
- [ ] `UniformFoldedMath`（`EFoldedMathOp` 6 运算 + 递归 IsConstant/IsIdentical）+ `UniformFoldedUnary`（5 运算）
- [ ] `CodeChunk` 全字段版：逐字段对照表 + `uniform_expression` 树指针 + 两种 chunk 来源的语义（表达式块无 SymbolName）
- [ ] `MaterialCompiler` 抽象基类（纯虚算子接口）+ `HLSLTranslator` 实现（两层结构，依赖倒置）
- [ ] `CompileError` / `EErrorSeverity` / `CompileResult`（errors 数组 + `HasErrors`）+ `EmitError`（SameAs 去重）+ `current_node_`/`current_pin_` 上下文
- [ ] `AddCodeChunk`（Unknown→-1 / 内联不去重 / 数值类型限制 / 纹理报错）+ `AddInlinedCodeChunk` + `AddUniformExpression`（IsIdentical 双层去重 + 材质级表达式表）
- [ ] `Constant/2/3/4` 全走表达式路径（`UniformConstant` 叶子）
- [ ] 查询四件套 + `IsConstant`（树递归）+ `IsExpressionConstantValue` + assert + 边界保护
- [ ] `Add`/`Subtract`/`Multiply`/`Divide` 三轨判定 + 立即折叠 + 代数化简（x*0/x*1/0/x/x/1）+ `PromoteToType` 标量提升 + 除零 Warning + rcp 技巧
- [ ] `Power`/`Lerp`/`Clamp`/`Abs`/`Negate`/`Sine`/`Cosine`/`Dot`/`Cross`/`Normalize`/`Length`/`ComponentMask`/`AppendVector`/`ValidCast`
- [ ] `compiler_test.cpp` 全部断言通过：标量/向量折叠、×1 直通+标量提升、×0 归零、嵌套折叠、除零 Warning、树构建+IsIdentical 去重、类型不匹配 Error+哨兵、非表达式 HLSL 路径、边界保护

---

## 核心原则回顾

1. **结构对齐优先**：bitmask / 全字段 chunk / 表达式树 / 双层编译器四个结构原样照搬 UE，简化只发生在"UE 为工业规模付出的工程"（preshader 字节码 VM、LWC、导数双轨、作用域系统）——每个不采用的字段都在对照表里说明了它**在 UE 里的作用**和不采用的理由。
2. **树是三种形态的统一**：纯常量（叶子）、含参数（子树，运行时 CPU 求值）、纯 GPU（无树）——理解了这个，算子的三轨判定就是自然的。
3. **保留核心语义**：递归 IsConstant、IsIdentical 双层去重、INDEX_NONE 哨兵、立即折叠 vs 建树的分工、"局部变量 vs 内联"的取舍——全部与 UE 同源。
4. **本课边界清晰**：算子 API + 数据结构 + 编译器层错误收集；`Compile(Graph*)` 课 7、`GenerateCode` 课 8、纹理算子课 14/15、错误 UI 课 19、参数表达式节点课 20（树上追加子类 + `MaterialRenderContext` 填充参数表）。不挖坑、不留占位。
