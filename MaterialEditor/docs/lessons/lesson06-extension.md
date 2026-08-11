# 课6 编译器扩展路线图：向 UE5 核心能力对齐

## 概述

课6 的 `MaterialCompiler` 是教学版，覆盖材质编译的**核心机制**：算术运算、常数折叠（含向量）、hash 去重、递归编译、Int/Matrix/Texture 类型、Property 反射体系、简化的 HLSL 生成。

本文档原先是"向 UE5 工业级演进的 5 个方向"的规划路线图。**这 5 个方向现已全部写成深度教案，合并进核心课程的对应课里**（见下表）。本文档保留作**总览索引**，方便从一处看到全貌。

## 5 个扩展方向 → 已并入哪节课

| 方向 | 合并进 | 核心内容 |
|------|--------|---------|
| **完整 HLSL 代码生成** | [课8](lesson08.md) | 完整着色器模板（cbuffer/VS/PS）、UniformCollector、拓扑排序声明顺序、占位符 vs 拼接取舍、DX12 衔接 |
| **参数系统** | [课20](lesson20.md) | Scalar/Vector/TextureParameter 节点、UniformTable 编译期收集、StaticSwitch 变体（key/缓存/爆炸）、反射集成 |
| **错误诊断** | [课6](lesson06.md) 编译器层 + [课19](lesson19.md) UI 层 | 课6：CompileError{nodeId,pin,severity} + EmitError 收集器 + 算子内检查（类型不匹配/除零）。课19：错误面板 Dock + 节点染色 + 引脚标红 + 点击跳转 |
| **材质域 / 混合模式** | [课15](lesson15.md) | EMaterialDomain/EBlendMode 枚举、5 种 blend 的 D3D12_BLEND_DESC 配置表、domain×blend 合法组合矩阵 |
| **多进程 Shader 编译** | [课16](lesson16.md) | 为什么多进程、ShaderCompileWorker 架构、IPC 协议、任务调度、shader 缓存、错误回传+SourceMap |

每节课里的对应章节都有：**原理 + 数据结构设计 + 参考代码 + UE5 对照（带 Engine 路径）+ 已踩坑 + 集成步骤**，深度对标课6。

## 定位

这 5 块**都不在核心课程（课1-21）的最小实现路径上**——核心课程先教骨架，这 5 块是骨架长成准工业级编译器的**进阶深度内容**，按各自的前置依赖嵌入对应的课：
- **完整 HLSL 生成 / 材质域 / 多进程编译**：依赖课14-17 的 DX12 上下文，在那些课里教。
- **参数系统**：编译器内部 + 反射，在课20（扩展功能）里教。
- **错误诊断**：编译器内部（CompileError/EmitError/算子内检查）在课6（编译器核心）里教；UI 层（错误面板 + 节点染色 + 点击跳转）在课19 里教。

## UE5 参考

- `FHLSLMaterialTranslator`（`.h` / `.cpp`）：UE5 编译器主类，本路线图的工业级对标
- `MaterialTemplate.ush`：着色器模板（课8 完整生成的参考）
- `ShaderCompileWorker` + `FShaderCompileManager`：课16 多进程并发的直接对标
- `Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression.h`：表达式基类（块外的扩展）
- 项目内 `.claude/UE5_Material_System_Analysis.md`（1029 行）：UE5 材质系统全景分析
