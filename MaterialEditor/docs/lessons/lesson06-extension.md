# 课6 编译器扩展路线图：向 UE5 核心能力对齐

## 概述

课6 的 `MaterialCompiler` 是教学版（~500 行），覆盖材质编译的**核心机制**：算术运算、标量常数折叠、hash 去重、递归编译、简化的 HLSL 生成。

本文档规划**在课6 基础上向 UE5 工业级能力靠拢**的扩展路线，目标是把教学版升级成一个"相当完整的单平台（DirectX / HLSL）材质编译器"。

**范围说明**——本路线图**排除**两块（体量过大、且属于其他课程范畴）：
- **表达式节点**：UE5 有 100+ 个 `MaterialExpression`（Fresnel / Desaturation / DDXY / BumpOffset / CameraVector…），每个 50-200 行。属课7 范畴，按需逐个加，不在本路线图估算内。
- **多平台后端**：GLSL（Vulkan）/ Metal（Apple）/ PS5 / Switch 等非 HLSL 后端。属渲染层多平台支持，本项目专注 DX12。

## 当前教学版状态（课6-9 完成后）

- `CodeChunk`：hash 去重、`isInline`、`isConstant` + `constantValue`（单 `float`，仅标量）
- 类型：仅 `Float1 / Float2 / Float3 / Float4`
- 常数折叠：仅标量，少量代数化简（`*1` / `*0`）
- 算术 / 三角 / 向量运算 API
- 简化的 `GenerateCode`（变量声明 + 注释，**非完整着色器**）
- 累计代码量：~2000 行

## 6 个扩展方向

### 1. 类型系统扩展（+300-500 行）

**目标**：支持 int/uint、矩阵、纹理对象、采样器类型。

**要改的文件**：
- `Types.h`：`EValueType` 加 `Int1/2/3/4`、`Matrix3x3`、`Matrix4x4`、`Texture2D`、`SamplerState`
- `TypeSystem.h`：算术推导规则扩展（int+float 升级、matrix×vector、matrix×matrix）
- `Reflection.h`：`FieldType` 同步加（如要让参数也支持 int/matrix）
- `Accessor`：加 int / matrix 的读写特化

**关键设计点**：
- int 与 float 混合运算的隐式转换规则（通常 int → float 升级）
- 矩阵乘法的维度检查（`3x3 × float3 = float3`）
- 纹理 / 采样器是"对象类型"，不能参与算术，只能给 `TextureSample` 用

### 2. 常数折叠扩展（+300-500 行）

**目标**：向量 / 矩阵也能编译期折叠 + 更多代数化简。

**要改的文件**：
- `CodeChunk.h`：`constantValue` 改成 `std::variant<float, Vec2, Vec3, Vec4, Mat3x3, Mat4x4>` 或拆成多字段
- `MaterialCompiler.cpp`：`AddConstantChunk` 支持向量/矩阵；`Constant2/3/4` 改走常量路径（不再只是 code 字符串）
- `ConstantFolding.h`：加向量 / 矩阵运算版

**关键设计点**：
- 向量折叠：`Add(Constant3(1,0,0), Constant3(0,1,0))` → `Constant3(1,1,0)`，编译期算出
- 代数化简扩展：`x-0→x`、`x-x→0`、`pow(x,1)→x`、`pow(x,0)→1`、`max/min` 常量化、`clamp` 范围折叠、`abs(abs(x))→abs(x)`

### 3. 完整 HLSL 代码生成（+2000-5000 行）—— 最重的一块

**目标**：`GenerateCode` 输出**完整可编译的着色器**，而非变量声明片段。

**要改 / 加的文件**：
- `MaterialCompiler.cpp` 的 `GenerateCode`：重写
- 新增 `MaterialTemplate.h/.cpp`（或 `ShaderTemplate.ush`）：着色器骨架模板
- 新增 `UniformCollector`：收集参数（与第 4 块耦合）

**生成内容**：
- `cbuffer MaterialParams { float4 BaseColor; ... }`（uniform 声明）
- `struct VSInput { float3 Pos; float3 Normal; float2 UV; };`
- `struct PSInput { float4 Pos : SV_Position; ... };`
- VertexShader / PixelShader 函数骨架
- 材质属性 chunk → PS 输出的赋值

**关键设计点**：
- 模板化：占位符（`{{BASE_COLOR_CODE}}`）+ 替换，或字符串拼接
- **建议放到课14（DX12 渲染）一起做**，那时有渲染管线（cbuffer / layout / VS-PS）的上下文

### 4. 参数系统（+800-1500 行）

**目标**：支持带名字的 uniform 参数 + 编译期静态分支。

**要改 / 加的文件**：
- 新增 `Parameter` 相关类（`ScalarParameter` / `VectorParameter` / `TextureParameter`）
- `MaterialCompiler`：编译时收集 uniform 列表
- `StaticSwitch` 处理：编译期决定分支，一个材质编出多个变体

**关键设计点**：
- uniform 收集：编译时遇到 Parameter 节点，记到 uniform 表，生成时输出 cbuffer
- StaticSwitch 变体：静态参数组合作为 key，每种组合编译一个 shader 变体
- 与第 3 块**强耦合**（uniform 声明是生成的一部分）

### 5. 错误诊断（+300-500 行）

**目标**：编译错误定位到具体节点 / 引脚，供编辑器高亮。

**要改的文件**：
- `MaterialCompiler.cpp`：编译各处加错误检查
- `CompileResult`：加错误列表（node id + pin name + 错误描述）

**检查项**：
- 类型不匹配（Float3 接到 Float1 引脚）
- 循环依赖（图里有环）
- 未连接的必需引脚
- 除零（已有 warning，可升级为错误或保留 warning）

**关键设计点**：
- 错误带节点定位（`Error{nodeId, pinName, message}`），编辑器能高亮对应节点
- **贯穿所有编译路径**，建议边做其他块边加，而非单独做一遍

### 6. 材质域 / 混合模式（+500-1000 行）

**目标**：支持不同材质类型和混合行为。

**要加的枚举**：
- `EMaterialDomain`：Surface、Unlit、PostProcess、Decal、UserInterface
- `EBlendMode`：Opaque、Masked、Translucent、Additive、Modulate
- 开关：双面、投射阴影、tessellation

**影响**：主要在第 3 块（代码生成）里加分支——不同域 / 模式生成不同 shader 结构和 blend state。

### 7.（可选）Shader 编译集成，DX 单平台（+1000-2000 行）

**目标**：把生成的 HLSL 编译成 DXBC / DXIL 字节码。

**要加**：
- 调 `D3DCompile`（fxc）或 DXC 编译 HLSL
- 编译错误回显（HLSL 编译错误 → 回显到材质编辑器）
- Shader 缓存（相同 HLSL 不重复编译）

**说明**：这块属于**课14-17（DX12 渲染）**范畴，不是编译器核心。但想要"端到端能跑"（材质图 → shader → 渲染）就得接。留到 DX12 阶段。

## 总估算

| 块 | 代码量 |
|----|--------|
| 当前课6-9（基础）| ~2000（已有）|
| 1. 类型系统 | +300-500 |
| 2. 常数折叠扩展 | +300-500 |
| 3. 完整 HLSL 生成 | +2000-5000 |
| 4. 参数系统 | +800-1500 |
| 5. 错误诊断 | +300-500 |
| 6. 材质域 / 混合模式 | +500-1000 |
| 7. Shader 编译集成（可选）| +1000-2000 |
| **合计（不含 7）** | **+4200-9000** |
| **合计（含 7）** | **+5200-11000** |

累计可达 **~6000-13000 行**——一个相当完整的单平台（DX）材质编译器。比教学版大一数量级，比 UE5 全功能小一个数量级。

## 实现顺序（依赖图）

```
1. 类型系统          ← 基础，其他都依赖
   ↓
2. 常数折叠扩展      ← 依赖新类型（向量折叠）
   ↓
4. 参数系统          ← 收集 uniform（生成需要）
   ↓
3. 完整 HLSL 生成    ← 最重，依赖 1+4
   ↓
6. 材质域 / 混合模式 ← 在 3 的生成里加分支
   ↓
5. 错误诊断          ← 贯穿，边做边加
   ↓
7. Shader 编译集成   ← 最后接 DX12（课14+）
```

**关键耦合**：3（完整生成）和 4（参数系统）强耦合，建议一起做。

## 分阶段建议

- **阶段 A（编译器内部增强，无外部依赖）**：块 1 类型系统 + 块 2 折叠扩展。纯编译器内部，课6 测试就能验证，收益直接（向量也能折叠了）。**最先做。**
- **阶段 B（参数 + 生成）**：块 4 参数系统 + 块 3 完整 HLSL 生成。建议放到**课14（DX12 渲染）**一起做，那时有渲染管线上下文（cbuffer / input layout / VS-PS）。
- **阶段 C（工程化）**：块 5 错误诊断 + 块 6 材质域 / 混合。在 B 基础上加。
- **阶段 D（集成）**：块 7 Shader 编译集成。课14-17 DX12 阶段。

## UE5 参考

- `FHLSLMaterialTranslator`（`.h` / `.cpp`）：UE5 编译器主类，本路线图的工业级对标
- `MaterialTemplate.ush`：着色器模板（块 3 的参考）
- `Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression.h`：表达式基类（块外的扩展）
- 项目内 `.claude/UE5_Material_System_Analysis.md`（1029 行）：UE5 材质系统全景分析

---

## 核心原则

教学版课6-9 抓的是 UE5 材质编译的**"骨架"**（~2000 行 ≈ 核心 10% 代码 / 90% 设计思想）。本路线图的扩展是在骨架上**堆功能**——理解骨架后，每加一块都有明确归属，不会迷失在 UE5 的几万行里。

**不必追求行数对齐**。先把课6-9 的骨架吃透，再按本路线图分阶段扩展，每加一块都配测试验证。
