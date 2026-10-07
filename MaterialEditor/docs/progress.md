# 材质编辑器 - 实现进度

## 总计划文件
`C:\Users\quan\.claude\plans\expressive-marinating-horizon.md`

## 参考文档
- UE5 材质系统分析：`E:\UE5_mirror\UE5_Material_System_Analysis.md`
- UE5 源码：`E:\UE5\`

## 课程列表

| 课 | 标题 | 文件 | 状态 |
|----|------|------|------|
| 1 | 项目骨架 | [lesson01.md](lessons/lesson01.md) | 📝 教案已写 |
| 2 | Core 基础设施 | [lesson02.md](lessons/lesson02.md) | 📝 教案已写 |
| 3 | 图数据模型 | [lesson03.md](lessons/lesson03.md) | 📝 教案已写 |
| 4 | 节点工厂 + 图遍历 | [lesson04.md](lessons/lesson04.md) | 📝 教案已写 |
| 5 | 表达式基类 + 类型系统 + 反射系统 | [lesson05.md](lessons/lesson05.md) | 📝 教案已写 |
| 5c | 迷你垂直切片：可视化反射（教学跳章） | [lesson05c.md](lessons/lesson05c.md) | ✅ 已完成 |
| 6 | 编译器核心 | [lesson06.md](lessons/lesson06.md) | ⏳ 进行中（类型系统/shader 值体系/底层工具已落地，见下方细分）|
| 7 | 第一批表达式 | [lesson07.md](lessons/lesson07.md) | 📝 教案已写 |
| 8 | HLSL 代码生成 | [lesson08.md](lessons/lesson08.md) | 📝 教案已写 |
| 9 | 命令行验证编译管线 | [lesson09.md](lessons/lesson09.md) | 📝 教案已写 |
| 10 | Qt 主窗口布局 | [lesson10.md](lessons/lesson10.md) | 📝 教案已写 |
| 11 | 节点图形项 | [lesson11.md](lessons/lesson11.md) | 📝 教案已写 |
| 12 | 连线 + 画布交互 | [lesson12.md](lessons/lesson12.md) | 📝 教案已写 |
| 13 | 面板（调色板/属性/代码） | [lesson13.md](lessons/lesson13.md) | 📝 教案已写 |
| 14 | DX12 基础：设备与渲染目标 | [lesson14.md](lessons/lesson14.md) | 📝 教案已写 |
| 15 | DX12 渲染管线：根签名与着色器 | [lesson15.md](lessons/lesson15.md) | 📝 教案已写 |
| 16 | DX12 渲染器封装 | [lesson16.md](lessons/lesson16.md) | 📝 教案已写 |
| 17 | 3D 预览集成 | [lesson17.md](lessons/lesson17.md) | 📝 教案已写 |
| 18 | 保存/加载 + 导出 | [lesson18.md](lessons/lesson18.md) | 📝 教案已写 |
| 19 | 错误处理 + 撤销重做 | [lesson19.md](lessons/lesson19.md) | 📝 教案已写 |
| 20 | 扩展功能 | [lesson20.md](lessons/lesson20.md) | 📝 教案已写 |
| 21 | 外部模型与贴图加载 | [lesson21.md](lessons/lesson21.md) | 📝 教案已写 |

## 阶段划分

### 课 6 细分进度（2026-09-27 更新）

已完成（对照 lesson06.md 各部分）：
- ✅ 第一部分 类型系统：`MaterialTypes/Public/ValueType.h`（EMaterialValueType 全 31+ 位 bitmask + 谓词族 + CanImplicitConvert）
- ✅ 底层工具：`Core/Public/MemStack.h`、`Core/Public/StringBuilder.h`
- ✅ shader:: 值体系（课 20 preshader 的底层，提前铺）：`Shader/Public/ShaderTypes.h` + `Shader/Private/ShaderValue.cpp`（EValueComponentType/FType/FValue/FStructTypeRegistry，UE ShaderValueType.h 移植）
- ✅ `Expression/Public/UniformExpression.h` 骨架（类型注册表 + 树基类）

待做（lesson06 第二~九部分）：
- ⏳ UniformConstant / UniformFoldedMath / 一元子类族（第二部分）
- ⏳ CodeChunk 全字段版（第三部分，现为简化版 7 字段）
- ⏳ CompileError / CompileResult（第四部分，已有雏形）
- ⏳ MaterialCompiler 拆两层：接口迁 `Expression/Public/`，HLSLTranslator 实现（第五部分）
- ⏳ 代码块管理 + 双层去重（第六部分）
- ⏳ 算子三轨判定 ~20 个（第七部分）+ compiler_test.cpp（第九部分）
- ⏳ 旧代码处置：ConstantFolding.h 退役、GraphCompiler 废弃（第八部分）

### 阶段 1：项目骨架 + 数据模型（课1-4）
CMake 项目搭建、Core 基础设施、图数据模型、节点工厂和图遍历。

### 阶段 2：编译器核心（课5-9）
表达式基类、（可选）反射系统、编译器、第一批表达式、HLSL 代码生成、端到端验证。

### 阶段 3：Qt 节点图 UI（课10-13）
主窗口布局、节点图形项、连线画布交互、功能面板。

### 阶段 4：DX12 渲染（课14-17）
DX12 设备与渲染目标、渲染管线与根签名、渲染器封装、PBR 预览。

### 阶段 5：完善和扩展（课18-20）
保存加载、导出、错误处理、撤销重做、更多表达式、多平台输出。

### 阶段 6：外部资源加载（课21）
模型加载（Assimp）、贴图加载（stb_image）、DX12 纹理上传、拖拽导入、网络下载。
