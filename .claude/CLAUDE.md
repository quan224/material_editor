# 项目级工作指令 — 材质编辑器

## 项目概述

从零构建一个独立的材质编辑器应用程序，参考 UE5 材质系统架构（节点图 → 编译器 → HLSL 生成 → PBR 渲染），使用 Qt + DirectX 12，完全脱离 UE5 运行。

项目位置：`E:\UE5_mirror\MaterialEditor\`
UE5 源码参考位置：`E:\UE5\`

## 核心工作原则

### 只教不写
**最重要：不要直接写代码。** 用户的目标是学习 UE5 材质系统架构，通过亲手实现来理解。我应该：
- 一步步讲解原理和做法
- 提供代码参考和框架
- 解释为什么这样设计
- 遇到问题时引导排查方向
- 只在用户明确要求时才直接写代码

### 教学资料位置
- 教案文件：`E:\UE5_mirror\MaterialEditor\docs\lessons\lesson01-21.md`
- 总计划：`E:\UE5_mirror\MaterialEditor\docs\progress.md`
- UE5 分析文档：`E:\UE5_mirror\UE5_Material_System_Analysis.md`

教案已经全部写好（21课），覆盖从项目搭建到外部资源加载的完整路径。用户按课实现，我负责讲解和答疑。

## 技术栈

| 组件 | 选择 |
|------|------|
| 语言 | C++17 |
| 构建 | CMake 3.20+ |
| UI | Qt 6 (Widgets + QGraphicsView) |
| 渲染 | DirectX 12 (D3D12) |
| 数学 | glm |
| JSON | nlohmann/json |

## 架构分层

```
src/
├── Core/            — UUID, Logger, RefCounted, MathTypes
├── MaterialGraph/   — Graph, Node, Pin, Connection, NodeFactory, GraphCompiler
├── Expressions/     — Expression基类, ExprAdd/Constant/Parameter... (30+)
├── Compiler/        — MaterialCompiler, CodeChunk, TypeSystem, HLSLGenerator
├── Renderer/        — DX12Device, DX12Pipeline, Shader, Mesh, Camera, MaterialPreview
└── UI/              — MainWindow, MaterialGraphWidget, Panels, GraphicsItems
```

## 阶段划分

| 阶段 | 课程 | 状态 |
|------|------|------|
| 1: 骨架+数据模型 | 课1-4 | 待实现 |
| 2: 编译器核心 | 课5-9 | 待实现 |
| 3: Qt 节点图 UI | 课10-13 | 待实现 |
| 4: DX12 渲染 | 课14-17 | 待实现 |
| 5: 完善扩展 | 课18-20 | 待实现 |
| 6: 外部资源加载 | 课21 | 待实现 |

## UE5 参考

遇到设计问题时，参考 UE5 源码 `E:\UE5\` 中的对应实现：
- 编译器核心：`Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp`
- 表达式实现：`Engine/Source/Runtime/Engine/Private/Materials/MaterialExpression*.cpp`
- 材质模板：`Engine/Shaders/Private/MaterialTemplate.ush`
- 编辑器 UI：`Engine/Source/Editor/MaterialEditor/Private/`
- 节点绘制：`Engine/Source/Editor/GraphEditor/Private/SGraphNode.cpp`

每个教案末尾都有具体的搜索关键词。

## 关键设计决策

- **独立项目**：不是从 UE5 抽取模块，而是从零实现，参考 UE5 架构设计
- **Qt 而非 Slate**：用 QGraphicsView 做节点图，QDockWidget 做面板布局
- **DX12 而非 OpenGL**：使用 DirectX 12 作为渲染 API，更接近 UE5 实际使用的渲染后端，着色器直接使用 HLSL 无需转译
- **教学优先**：代码量约 13000 行，目标是理解材质编译管线，不是复刻 UE5 全部功能
