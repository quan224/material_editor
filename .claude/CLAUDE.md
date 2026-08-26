# 项目级工作指令 — 材质编辑器

> 本文件随 git 走。所有路径相对于仓库根目录（即包含 `.claude/` 的目录），
> 用正斜杠。涉及外部依赖（UE5 源码）的路径会标注「外部」。

## 项目概述

从零构建一个独立的材质编辑器应用程序，参考 UE5 材质系统架构（节点图 → 编译器 → HLSL 生成 → PBR 渲染），使用 Qt + DirectX 12，完全脱离 UE5 运行。

**项目目标**：通过亲手实现编辑器来理解 UE5 材质编辑器是如何实现的——关键数据结构和设计决策尽量与 UE5 对齐（如 CodeChunk ↔ FShaderCodeChunk、UniformExpression ↔ FMaterialUniformExpression），教学简化只用于非核心环节。

- 仓库根目录：当前目录（含 `.git/`、`.claude/`、`MaterialEditor/`、`material_editor_project/`）
- 教案与文档：`MaterialEditor/`
- 代码实现：`material_editor_project/`
- UE5 分析文档：`.claude/UE5_Material_System_Analysis.md` —— 1029 行 UE5 材质系统全景分析，写课 6/7/11 时参考
- UE5 源码参考（外部，可选）：`E:\UE5\` —— 仅在新机器装了 UE5 时可用

## 核心工作原则

### 只教不写
**最重要：不要直接写代码。** 用户的目标是学习 UE5 材质系统架构，通过亲手实现来理解。应该：
- 一步步讲解原理和做法
- 提供代码参考和框架
- 解释为什么这样设计
- 遇到问题时引导排查方向
- 只在用户明确要求时才直接写代码（如「帮我修复 X」「写一个 Y」）

### 沟通约定
- 中文沟通，代码注释中文，提交信息英文（约定式提交）
- 状态提示用中文（"正在检查" 不用 "Checking"）
- 解释前先说**为什么**，再说**怎么做**
- 用户容易踩的坑要主动提醒

### 教学资料位置
- 教案：`MaterialEditor/docs/lessons/lesson01-21.md` + `lesson05c.md`
- 进度表：`MaterialEditor/docs/progress.md`
- 架构心智模型：`material_editor_project/docs/mental_model.html`
- 类图：`material_editor_project/docs/architecture.html`

### 教案不挖坑不回退（铁律）
- **后续课可以为前面课"补充"，但绝对不允许"推翻重来"**——不能在前一课写简化版，让后续课整段替换。这是已经被反复折磨过的痛点。
- **判断标准**：后续课的代码会不会"覆盖"前面课已经写好的函数/类？
  - 会覆盖 → ❌ 违规，必须改方案（要么前面完全不做，要么前面给最终版）
  - 只追加新函数/新成员 → ✅ 允许
- **完成标志必须本课可勾**——不允许"依赖后续课才能勾"的 checkbox
- **每节课相对完整、自给自足**，正文里不挖坑（不留 TODO、不写"课 X 再做"），合理的"前置依赖引用"（指向已完成的前课）允许
- 检查时 grep 关键词：`简化版|TODO|占位|留到|课 \d+ 再|课 \d+ 完善|升级为|补丁` —— 这些都不应该出现

## 技术栈

| 组件 | 选择 |
|------|------|
| 语言 | C++17 |
| 构建 | CMake 3.20+ |
| UI | Qt 6 (Widgets + QGraphicsView) |
| 渲染 | DirectX 12 (D3D12) |
| 数学 | glm |
| JSON | nlohmann/json |
| 包管理 | vcpkg（Qt6 / nlohmann_json / glm 都通过 vcpkg） |

## 仓库目录结构

```
.
├── .claude/CLAUDE.md            ← 你正在读的文件
├── .vscode/                     ← VSCode 配置（含 Qt include 路径，机器特定）
├── MaterialEditor/              ← 教案与文档
│   └── docs/
│       ├── lessons/lessonXX.md  ← 21+1 节课的教案
│       └── progress.md          ← 进度表
├── material_editor_project/     ← C++ 代码实现
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── Core/Public/         ← UUID / Logger / Singleton / MathTypes / RefCounted
│   │   ├── MaterialGraph/       ← Node / Pin / Graph / NodeFactory / GraphCompiler / Types
│   │   ├── Reflection/Public/   ← Reflection.h / ReflectionMacros.h
│   │   ├── Expression/          ← Expression 基类 + 反射驱动参数 API
│   │   ├── Compiler/Public/     ← TypeSystem.h（HLSL 类型映射）
│   │   └── main.cpp             ← 当前是课5 反射系统的测试 main
│   └── docs/                    ← 架构图（HTML）
└── UE5_Material_System_Analysis.md
```

## 架构分层（自下而上依赖）

```
L1 Core              ← 零依赖工具（UUID/Logger/Singleton/Vec数学）
L2 MaterialGraph     ← 数据模型（Node/Pin/Graph + Types.h 引脚类型）
L3 Reflection        ← 类型擦除（FieldDesc/ClassDesc/Registry/Accessor）
L4 Expression        ← 表达式抽象基类 + 子类
L5 应用层            ← UI / Compiler / Renderer
```

**铁律**：每层只能引用下方层，不能反向。

## 当前进度（2026-07-01 更新）

| 阶段 | 课程 | 状态 |
|------|------|------|
| 1: 骨架 + 数据模型 | 课 1-4 | ✅ 已完成 |
| 2: 编译器核心 | 课 5 | ✅ 已完成（含反射系统）|
| 2.5: 垂直切片 | 课 5c | 📝 教案已写、⏳ 待实现 |
| 2: 编译器核心 | 课 6-9 | ⏳ 待实现 |
| 3: Qt 节点图 UI | 课 10-13 | ⏳ 待实现 |
| 4: DX12 渲染 | 课 14-17 | ⏳ 待实现 |
| 5: 完善扩展 | 课 18-20 | ⏳ 待实现 |
| 6: 外部资源加载 | 课 21 | ⏳ 待实现 |

**下一步**：用户实现课 5c（迷你 Qt 反射演示窗口），完成后回到课 6（编译器核心）。

## 已固化的设计决策（不要推翻）

### 反射系统（课 5）— UE 风格：Property 继承体系
- **类型擦除用继承 + 虚函数**（对标 UE `FProperty`）：`Property` 基类（虚函数 `toJson`/`fromJson`）+ 子类（`Float/Int/Bool/StringProperty` + `Vec2/3/4Property`），`ClassDesc` 持有 `vector<unique_ptr<Property>>`。旧三层（FieldType 枚举 / FieldDesc 函数指针 / `Accessor<T>` 模板特化）**已废弃**。
- **const-correctness（关键，对标 `FProperty::ExportText/ImportText`）**：
  - `toJson(const void* obj, nlohmann::json&) const` —— 读用 `const void*`
  - `fromJson(void* obj, const nlohmann::json&) const` —— 写用 `void*`
  - 两者都是 const 方法（Property 是不可变元数据，只读写传入的 obj）
  - 这样 `Expression::Get/SetParameter` **全程无 const_cast**
- **类型→子类映射**：`MakeProperty<T>` 工厂用 `if constexpr`（对标 UE UHT），返回裸指针由 `ClassDesc` 的 `unique_ptr` 接管。
- **ME_FIELD 默认值序列化**：用户传 `Vec3(1,0,0)` 不能直接赋给 json，必须在 `MakeProperty` 里**用 Property 自己的 toJson** 把默认值序列化进 `prop->defaultValue`（`prop->toJson(&defaultVal, 0, prop->defaultValue)`）。
- **GetClassDesc_Static 返回引用**：返回 `const ClassDesc&` 而非值，避免 `&GetClassDesc_Static()` 取到临时值地址。
- **ClassDesc::find 用 const auto&**：按值拷贝会返回悬空指针。
- **Expression 三套 API 分工**：
  - 4 个**纯虚** = 子类必须实现（GetClassDesc 通过 ME_END_CLASS 宏自动实现、GetInputPins/GetOutputPins/Compile 手写）
  - 3 个**非虚** = 基类用反射统一实现（GetParameters/SetParameter/GetParameter），子类不碰
- **UI 不 switch 枚举**：`PropertyCustomizerRegistry` 按 `std::type_index` 注册编辑器（对标 `IPropertyTypeCustomization`）。

### 类型系统去重（课 5 期间的重构）
- `Types.h`（L2 数据模型层）：`EValueType` / `EPinDataDirection` / `GetComponentCount` / `CanImplicitConvert` —— **零编译器知识**
- `TypeSystem.h`（L5 编译器层）：`GetArithmeticResultType` / `ToHLSLType` —— HLSL 字符串映射属于编译器
- 原则：HLSL 字符串不能出现在 Types.h，破坏分层

### 编译器结构对齐 UE（课 6 教案，已定稿，不要推翻）
设计细节（EValueType 改 bitmask 含 LWC/纹理变体/打包三件套 / UniformExpression 表达式树替代 variant / CodeChunk 与 FShaderCodeChunk 逐字段一一对应（含导数双轨/作用域三件套） / MaterialCompiler 抽象基类 + HLSLTranslator 实现的两层结构 / 三轨判定 / 双层去重 / PromoteToType 规则）**以 lesson06.md 为唯一权威来源**，此处不复述。
**完整度约定（2026-08-26 定）**：除「多平台编译」和「130+ 重复算子的批量复制」外不省略——LWC/导数双轨/材质函数/Custom 作用域/Substrate 完整实现（组合树+课17 GGX 求值，非桩）/多属性编译/DDC 缓存全部进教案（课 6/7/8/20）。

### 宏的命名规范
- `ME_BEGIN_CLASS` / `ME_FIELD` / `ME_END_CLASS` / `ME_DISPLAY_NAME` / `ME_CATEGORY` / `ME_CATEGORY_COLOR`
- 宏参数加括号防御运算符优先级（`field.default_value = (DefaultValue)`）
- `#Name` 字符串化时不加括号（保留原始标识符）

## 已踩过的坑（避免重蹈）

| Bug | 现象 | 教训 |
|-----|------|------|
| `Registry::Find` 三元逻辑反 | 找不到时返回垃圾指针，找到时返回 nullptr | `it != end() ? &it->second : nullptr` 别写反 |
| `ClassDesc::find` 按值迭代 | 返回局部变量地址，悬空指针 | `for(const auto& x : v)` |
| `ME_FIELD` 默认值直接赋 json | `Vec3` 不能隐式转 `nlohmann::json`，编译失败 | 在 `MakeProperty` 里用 `Property::toJson` 序列化进 `prop->defaultValue` |
| `desc.fields` 漏写 | ClassDesc 本身没有 push_back/emplace_back | 用 `desc.fields.emplace_back(std::unique_ptr<Property>)` |
| `elif constexpr` | C++ 没这个关键字 | `else if constexpr` |
| `static const ClassDesc GetClassDesc_Static()` | 返回值带 `&` 是悬空指针 | 返回 `const ClassDesc&` |
| Vec4 fromJson 检查 `>=3` | 越界访问 `in[3]` | Vec4 要 `>=4` |
| CMake GLOB_RECURSE 新文件不进编译 | Expression.cpp 链接不到 | 加新 .cpp 后必须 `cmake -B build -S .` 重跑 configure |
| GetParameter 空 return | `return ;` 但函数返回 json | `return {};` |
| bitmask 用 `==` 判类别 | `type == MCT_Float` 对 Float3 永远为假 | 类别判断必须 `&`，`==` 只对单值位合法 |
| ×1 直通直接返回索引 | `Float1 * Float3` 直通后下游分量不足 | 走 `PromoteToType` 的 while-AppendVector（UE .cpp:9608 同款） |

## 新会话起手清单

新 Claude 会话开始时，**先读这几个文件**了解上下文：

1. `.claude/CLAUDE.md` —— 本文件，整体规则和进度
2. `MaterialEditor/docs/progress.md` —— 详细课程列表
3. `material_editor_project/docs/mental_model.html` —— 五层架构心智模型（浏览器打开）
4. 最近改动的文件：`material_editor_project/src/main.cpp`（课5 反射测试）

如果用户问「我们现在做到哪了」：看 progress.md 的状态列。

如果用户问「反射怎么用」：让他读 `MaterialEditor/docs/lessons/lesson05.md` + `lesson05c.md`。

## UE5 参考（外部）

如果新机器装了 UE5（`E:\UE5\` 或其他路径），遇到设计问题时参考：
- 编译器核心：`Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp`
- 表达式实现：`Engine/Source/Runtime/Engine/Private/Materials/MaterialExpression*.cpp`
- 材质模板：`Engine/Shaders/Private/MaterialTemplate.ush`
- 编辑器 UI：`Engine/Source/Editor/MaterialEditor/Private/`
- 节点绘制：`Engine/Source/Editor/GraphEditor/Private/SGraphNode.cpp`

每个教案末尾都有具体的搜索关键词。**没装 UE5 也能继续**，教案本身已自洽。

## 其他重要决策

- **独立项目**：不是从 UE5 抽取模块，而是从零实现，参考 UE5 架构设计
- **Qt 而非 Slate**：用 QGraphicsView 做节点图，QDockWidget 做面板布局
- **DX12 而非 OpenGL**：更接近 UE5 实际使用的渲染后端，着色器直接用 HLSL 无需转译
- **教学优先**：代码量约 13000 行，目标是理解材质编译管线，不是复刻 UE5 全部功能

## 工作流提醒

- **写完代码立刻编译**：CMake `cmake --build build --config Debug`，让编译器当地面真相，不要靠"看着像对的"
- **加新文件后重跑 cmake configure**：`cmake -B build -S .` 让 GLOB_RECURSE 重新扫
- **危险操作前确认**：删除、强制推送、改公共代码前先问用户
- **commit message 英文**，遵循约定式提交（feat / fix / refactor / docs / chore）

---

**最后更新**：2026-08-17
**优先级**：本文件 > 全局 `~/.claude/CLAUDE.md` > 默认行为
