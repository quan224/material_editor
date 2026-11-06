---
name: build-link-failures-lesson6
description: 当前代码链接不过的两个原因（课6 进行中），非环境问题
metadata: 
  node_type: memory
  type: project
  originSessionId: c8808508-b25d-4346-b4a1-1364a712d732
  modified: 2026-07-29T11:04:59.665Z
---

2026-07-29 验证：环境完全正常（cmake 配置通过、13 个源文件全部编译通过），但 `cmake --build` 在**链接阶段**失败，两个都是项目代码自身原因：

1. **`RunCompilerTest()` 重复定义（multiple definition）** —— `src/Compiler/compiler_test.cpp` 既被 `CMakeLists.txt` 的 `GLOB_RECURSE src/*.cpp` 当独立源文件编译，又被 `main.cpp` 第 16 行 `#include "Compiler/compiler_test.cpp"` 直接包含，导致 `RunCompilerTest()` 被定义两次。test 文件顶部的 `#pragma once` 只能防单 TU 内重复包含，防不了「同一 .cpp 被编成两个 TU」。
   - 修法二选一：把 compiler_test.cpp 移出 `src/`（如移到 `tests/` 不被 GLOB 到），或从 main.cpp 去掉 `#include` 改用 `extern void RunCompilerTest();` 声明。

2. **课6 方法未实现（undefined reference）** —— `MaterialCompiler::Add / IsConstant / GetConstantValue` 在头文件声明了但 `MaterialCompiler.cpp` 还没写实现（项目处于课6 编译器核心阶段，只实现了 Constant/Constant2 + chunk 基础设施）。这正是课6 待写内容，按 CLAUDE.md「只教不写」原则不应代写。

**Why**：排查构建时先区分「环境问题」和「代码状态问题」——本项目当前是后者。
**How to apply**：用户报「编译不过」时，先看是不是这两个已知原因，别误判成环境没装好。环境状态见 [[msys2-environment-setup]]。
