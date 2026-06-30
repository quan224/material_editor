# 课1：项目骨架

## 目标

创建项目目录、CMakeLists.txt、空 main.cpp，编译运行弹出一个 Qt 空窗口。

---

## 背景知识

在开始写代码之前，理解几个关键概念：

### CMake 是什么？
CMake 是 C++ 的构建配置工具。你写一个 `CMakeLists.txt` 描述"源文件在哪、依赖什么库"，CMake 就帮你生成 VS 项目或 Makefile。

在 UE5 中，这个角色由 **UnrealBuildTool**（UBT）承担。每个 UE5 模块有一个 `*.Build.cs` 文件，等同于我们的 `CMakeLists.txt`。例如：
- UE5：`E:\UE5\Engine\Source\Editor\MaterialEditor\MaterialEditor.Build.cs`
- 我们：`MaterialEditor/CMakeLists.txt`

### Qt 的程序入口
Qt 程序的入口和普通 C++ 程序一样是 `main()`，但需要创建一个 `QApplication` 对象来管理事件循环（窗口消息、鼠标键盘事件等）。最后调用 `app.exec()` 进入事件循环，程序就不会退出了。

在 UE5 中，这个角色是 `FEngineLoop`：
- UE5：`E:\UE5\Engine\Source\Runtime\Launch\Private\Launch.cpp` → `ENGINE_MAIN()`
- 我们：`src/main.cpp` → `main()`

### CMAKE_AUTOMOC 是什么？
Qt 有一个叫 **MOC（Meta-Object Compiler）** 的工具。当你写一个类包含 `Q_OBJECT` 宏时（信号槽机制需要），MOC 会自动生成额外的 C++ 代码。设置 `CMAKE_AUTOMOC ON` 让 CMake 自动处理这个步骤，否则你得手动跑 MOC 工具。

---

## 操作步骤

### 1. 创建目录结构

在 `E:\UE5_mirror\MaterialEditor\` 下手动创建以下结构：

```
MaterialEditor/
├── CMakeLists.txt
├── src/
│   └── main.cpp          ← 本课只需要这一个文件
└── resources/
    └── shaders/           ← 先建空目录，后续课程用
```

### 2. 创建 CMakeLists.txt

在项目根目录创建 `CMakeLists.txt`，内容如下：

```cmake
cmake_minimum_required(VERSION 3.20)
project(MaterialEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Widgets REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS src/*.h)

add_executable(MaterialEditor ${SOURCES} ${HEADERS})
target_link_libraries(MaterialEditor PRIVATE
    Qt6::Core Qt6::Widgets
    glm::glm nlohmann_json::nlohmann_json
)
target_include_directories(MaterialEditor PRIVATE src/)

if(WIN32)
    target_compile_definitions(MaterialEditor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(MaterialEditor PRIVATE
        d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib
    )
endif()

file(COPY resources/ DESTINATION ${CMAKE_BINARY_DIR}/resources)
```

**逐行讲解**：

| 行 | 含义 |
|----|------|
| `cmake_minimum_required(VERSION 3.20)` | 要求 CMake 版本 ≥ 3.20 |
| `project(MaterialEditor VERSION 0.1.0)` | 项目名叫 MaterialEditor，版本 0.1.0 |
| `set(CMAKE_CXX_STANDARD 17)` | 使用 C++17 标准 |
| `CMAKE_AUTOMOC ON` | 自动处理 Qt 的 MOC（见上方背景知识） |
| `find_package(Qt6 ... REQUIRED)` | 查找 Qt6 库，`REQUIRED` 表示找不到就报错停止（不需要 OpenGL 组件，渲染使用 DirectX 12） |
| `find_package(glm ...)` | 查找 glm 数学库（UE5 FVector 等的替代品） |
| `find_package(nlohmann_json ...)` | 查找 JSON 库（后续保存/加载材质图用） |
| `file(GLOB_RECURSE SOURCES src/*.cpp)` | 自动收集 src/ 下所有 .cpp 文件 |
| `add_executable(MaterialEditor ...)` | 定义可执行目标 |
| `target_link_libraries(...)` | 链接 Qt、glm、json 库；Windows 下额外链接 DX12 相关库（d3d12、dxgi、d3dcompiler） |
| `target_include_directories(... src/)` | 让 `#include "Core/Public/UUID.h"` 这样的路径能工作 |
| `NOMINMAX` | Windows 特殊处理，防止 min/max 被宏覆盖 |
| `file(COPY resources/ ...)` | 把着色器等资源复制到构建目录 |

### 3. 创建 src/main.cpp

```cpp
#include <QApplication>
#include <QWidget>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("Material Editor v0.1");
    window.resize(1280, 720);
    window.show();

    return app.exec();
}
```

**逐行讲解**：
- `QApplication app(argc, argv)` — 创建 Qt 应用实例，处理命令行参数和事件循环
- `QWidget window` — 创建一个最基础的空白窗口控件
- `window.setWindowTitle(...)` — 设置窗口标题
- `window.resize(1280, 720)` — 设置窗口大小
- `window.show()` — 显示窗口（默认隐藏的）
- `return app.exec()` — 进入事件循环，程序在这里持续运行直到窗口关闭

### 4. 安装依赖

你需要系统上安装好 Qt6、glm、nlohmann_json。最方便的方式是用 **vcpkg**：

```bash
# 安装 vcpkg（如果还没装的话）
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg && bootstrap-vcpkg.bat

# 安装依赖
vcpkg install glm nlohmann-json qtbase --triplet x64-windows
```

或者单独安装 Qt6（推荐用 Qt 官方安装器）：
- 下载地址：https://www.qt.io/download
- 安装时选择 Qt 6.x + MSVC 2019/2022 64-bit

### 5. 配置和编译

```bash
cd E:\UE5_mirror\MaterialEditor

# 配置（指定 Qt 路径和生成器）
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_PREFIX_PATH="C:/Qt/6.5.0/msvc2019_64"

# 编译
cmake --build build --config Debug

# 运行
build\Debug\MaterialEditor.exe
```

> **注意**：`CMAKE_PREFIX_PATH` 要改成你实际的 Qt 安装路径。如果用 vcpkg，还需要加 `-DCMAKE_TOOLCHAIN_FILE=<vcpkg路径>/scripts/buildsystems/vcpkg.cmake`。

---

## 验证

运行程序后应该看到：
- 一个 1280×720 的空白窗口
- 标题栏显示 "Material Editor v0.1"
- 窗口可以正常关闭

如果编译失败，检查：
1. Qt6 是否正确安装（路径是否正确）
2. CMake 版本是否 ≥ 3.20（`cmake --version` 查看）
3. VS 2022 是否安装了 C++ 桌面开发工作负载

---

## UE5 参考

| 我们的代码 | UE5 中对应的部分 |
|-----------|-----------------|
| `CMakeLists.txt` | `MaterialEditor.Build.cs` — 模块依赖声明 |
| `main()` + `QApplication` | `Launch.cpp` 中的 `ENGINE_MAIN()` + `FEngineLoop` |
| `QWidget window` | UE5 中 `SWindow` — Slate 窗口控件 |

想看 UE5 的最小程序结构可以参考：
`E:\UE5\Engine\Source\Programs\BlankProgram\` — 这是 UE5 源码中最简单的独立程序，只有几百行。

---

## 本课完成标志

- [ ] 项目目录结构创建完成
- [ ] CMakeLists.txt 创建并理解每行含义
- [ ] main.cpp 创建并理解 QApplication 的作用
- [ ] 编译成功，弹出空白窗口

完成后回到 `docs/progress.md` 把课1状态改为 ✅ 已完成，然后继续课2。
