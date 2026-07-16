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

## 7 个扩展方向

### 1. 类型系统扩展（+300-500 行）

**目标**：支持 int/uint、矩阵、纹理对象、采样器类型。

**要改的文件**：
- `Types.h`：`EValueType` 加 `Int1/2/3/4`、`Matrix3x3`、`Matrix4x4`、`Texture2D`、`SamplerState`
- `TypeSystem.h`：算术推导规则扩展（int+float 升级、matrix×vector、matrix×matrix）
- `Reflection.h`：`Property` 继承体系加子类（如 IntProperty 等，同课5 的 Property 继承体系）
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
- 这块和 DX12 渲染管线强相关（cbuffer / input layout / VS-PS），**在课14（DX12 渲染）阶段做**，那时有完整上下文，一次做对

### 4. 参数系统（+800-1500 行）

**目标**：支持带名字的 uniform 参数 + 编译期静态分支。

**要改 / 加的文件**：
- 新增 `Parameter` 相关类（`ScalarParameter` / `VectorParameter` / `TextureParameter`）
- `MaterialCompiler`：编译时收集 uniform 列表
- `StaticSwitch` 处理：编译期决定分支，一个材质编出多个变体

**关键设计点**：
- uniform 收集：编译时遇到 Parameter 节点，记到 uniform 表，生成时输出 cbuffer
- StaticSwitch 变体：静态参数组合作为 key，每种组合编译一个 shader 变体
- 与第 3 块**强耦合**（uniform 声明是生成的一部分），一起做

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
- **贯穿所有编译路径**，在做每个块时就**一次把对应的错误检查加全**，不留"先标记后补"的中间态

### 6. 材质域 / 混合模式（+500-1000 行）

**目标**：支持不同材质类型和混合行为。

**要加的枚举**：
- `EMaterialDomain`：Surface、Unlit、PostProcess、Decal、UserInterface
- `EBlendMode`：Opaque、Masked、Translucent、Additive、Modulate
- 开关：双面、投射阴影、tessellation

**影响**：主要在第 3 块（代码生成）里加分支——不同域 / 模式生成不同 shader 结构和 blend state。

### 7. Shader 编译集成（多进程并发，+2000-4000 行）

**目标**：把生成的 HLSL 编译成 DXBC / DXIL 字节码，**直接做多进程并发版**（一步到位，不做"先单线程再升级"的中间态）。

#### 为什么 shader 编译必须并发
- 一个项目几百个材质 → **上万个 shader 变体**（StaticSwitch × 材质域 × 混合模式 × 质量）
- 每个编译（调 fxc / DXC）几十~几百 ms
- 串行：启动等几分钟~几十分钟；并发利用多核：几秒~几十秒
- 不并发根本不可用

#### 多线程 vs 多进程（核心选择，选多进程）

| 维度 | 多线程 | 多进程 |
|------|--------|--------|
| 实现 | 线程池，共享内存 | 独立进程（worker.exe）|
| 通信 | 共享变量（快）| IPC：管道 / 文件 / socket |
| 隔离 | 一线程崩→全进程崩 | 进程隔离，崩了不影响主 |
| 第三方库 | **fxc.dll / DXC 不完全线程安全** → 多线程有风险 | 进程隔离，无视线程安全 |
| 平台隔离 | 难 | 每个平台一个 worker 进程 |

**选多进程的理由**（同 UE5 `ShaderCompileWorker`）：
1. fxc / DXC 线程不安全，进程隔离规避
2. 非法 HLSL 导致编译崩，不拖垮主进程
3. 各平台 worker 独立，版本依赖不冲突
4. worker 卡死可杀掉重启

#### 架构（ShaderCompileWorker）

```
主进程（引擎/编辑器）                Worker 进程 × N（N = CPU 核数）
   │                                   │
   ├─ 收集 shader 编译任务              ├─ 收任务（IPC：管道/文件）
   ├─ 线程池分发任务给空闲 worker ─────→│ 编译（调 D3DCompile/DXC）
   │                                   │←────────── 返回字节码 + 错误（IPC）
   ├─ 收结果、拼 pipeline               │
   └─ shader 缓存命中则跳过             └─ N 个 worker 并行
```

#### 要加的组件
- **HLSL → DXBC**：`D3DCompile`（fxc）或 DXC 调用，在 worker 进程内
- **Worker 进程管理**：`CreateProcess` 启动 N 个 worker（N = CPU 核数）
- **IPC 协议**：主进程↔worker，传 HLSL 源码、回字节码 / 错误（命名管道或文件）
- **任务调度**：主进程用线程池分发任务、收结果（生产者-消费者队列）
- **Shader 缓存**：HLSL 的 hash → 字节码，命中则跳过编译
- **错误回显**：worker 编译失败 → 回传错误信息 → 显示在材质编辑器

#### 学习点（C++ 并发编程，通用技能）
这块是项目里**唯一系统学并发编程**的地方，能学到：
- **多线程**：`std::thread`、线程池、`std::mutex` / `std::condition_variable`、`std::future` / `std::promise`
- **多进程**：`CreateProcess`（Windows）/ `fork`+`exec`（POSIX）
- **进程间通信 IPC**：命名管道、共享内存、文件序列化、socket
- **任务调度**：生产者-消费者队列、work-stealing、优先级
- **同步原语**：原子操作、无锁队列、信号量

这些是通用技能，以后任何"大量独立任务并行"场景都用得到。

#### 依赖与时机
依赖**块 3（完整 HLSL 生成）**+ **课14（DX12 pipeline 用字节码）**。放**阶段 D**（课14+）做。

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
| 7. Shader 编译集成（多进程并发）| +2000-4000 |
| **合计（7 块全做）** | **+6200-13000** |

累计可达 **~8000-15000 行**——一个相当完整的单平台（DX）材质编译器，含多进程 shader 编译。比教学版大一数量级，比 UE5 全功能小一个数量级。

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
5. 错误诊断          ← 贯穿各块，每块做时一次加全
   ↓
7. Shader 编译集成   ← 多进程并发，最后接 DX12（课14+）
```

**关键耦合**：3（完整生成）和 4（参数系统）强耦合，一起做。

## 分阶段建议

- **阶段 A（编译器内部增强，无外部依赖）**：块 1 类型系统 + 块 2 折叠扩展。纯编译器内部，课6 测试就能验证，收益直接（向量也能折叠了）。**最先做。**
- **阶段 B（参数 + 生成）**：块 4 参数系统 + 块 3 完整 HLSL 生成。在**课14（DX12 渲染）**阶段做，那时有渲染管线上下文（cbuffer / input layout / VS-PS），一次做对。
- **阶段 C（工程化）**：块 5 错误诊断 + 块 6 材质域 / 混合。在做 B 的过程中就把错误诊断一并加全。
- **阶段 D（集成 + 并发）**：块 7 Shader 编译集成，**直接做多进程版**。课14-17 DX12 阶段，同时系统学并发编程。

## UE5 参考

- `FHLSLMaterialTranslator`（`.h` / `.cpp`）：UE5 编译器主类，本路线图的工业级对标
- `MaterialTemplate.ush`：着色器模板（块 3 的参考）
- `ShaderCompileWorker` + `FShaderCompileManager`：块 7 多进程并发的直接对标
- `Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression.h`：表达式基类（块外的扩展）
- 项目内 `.claude/UE5_Material_System_Analysis.md`（1029 行）：UE5 材质系统全景分析

---

## 核心原则

1. **一步到位，不做"先简化再升级"的中间态**。每块直接做目标版本——shader 编译直接多进程（不先单线程）、错误诊断一次加全（不先标记后补）、HLSL 生成一次做完整（不先片段后完整）。简化版会被推翻重来，反而浪费功夫、制造混乱。

2. **理解骨架再堆功能**。教学版课6-9 抓的是 UE5 材质编译的**"骨架"**（~2000 行 ≈ 核心 10% 代码 / 90% 设计思想）。本路线图的扩展是在骨架上堆功能——理解骨架后，每加一块都有明确归属，不会迷失在 UE5 的几万行里。

3. **每块配测试**。加一块扩一块 `compiler_test.cpp`，保证不破坏前面。
