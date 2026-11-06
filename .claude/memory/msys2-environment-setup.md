---
name: msys2-environment-setup
description: 本机已装好 MSYS2/MinGW 工具链，如何编译本项目的命令
metadata: 
  node_type: memory
  type: project
  originSessionId: c8808508-b25d-4346-b4a1-1364a712d732
  modified: 2026-07-29T11:04:53.623Z
---

2026-07-29 在 `C:\msys64` 全新安装了 MSYS2 + MinGW-w64 工具链，用于编译本项目（用户无法装 VS/MSVC，走 BUILD.md 的 MinGW 路线 A）。

已装组件（均来自清华镜像 `mirrors.tuna.tsinghua.edu.cn/msys2`）：
- g++ 16.1.0、cmake 4.4.0、ninja 1.13.2
- Qt6 6.11.1（Widgets）
- glm、nlohmann-json 3.12.0
- `C:\msys64\mingw64\bin` 已加入用户级 PATH

**编译命令**（在 `material_editor_project/` 下）：
```bash
cmake --preset mingw
cmake --build build-mingw
./build-mingw/MaterialEditor.exe
```
注意：在 Git Bash 里直接调 cmake/g++ 可能因 DLL 冲突报错，需用 MSYS2 的工具，或前置 `PATH=/c/msys64/mingw64/bin:$PATH`。

cmake mingw preset 硬编码了 `C:/msys64/mingw64/bin/g++.exe`，所以 MSYS2 必须装在 `C:\msys64`。当前配置已通过（find Qt6/glm/json 成功，13 个源文件全部编译过）。链接还没通是代码问题，见 [[build-link-failures-lesson6]]。
