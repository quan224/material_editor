# 材质编辑器 — 从零搭建指南

在全新 Windows 电脑上从零编译运行本项目的完整步骤。

## 项目状态

- **进度**：课5（反射系统）已完成，能编译运行反射测试（33/33 通过）
- **技术栈**：C++17 / CMake 3.20+ / Qt6 / MinGW（或 MSVC）/ DirectX 12（待实现）
- **配置**：仓库已含双 CMake preset（MinGW + MSVC），`git clone` 后开箱即用

---

## 路线 A：MinGW + MSYS2（推荐）

pacman 装的是预编译二进制，几分钟搞定，无 MSVC 授权问题。**本项目在这条路线上验证通过。**

### 1. 装 MSYS2

1. 从清华镜像下载 installer（国外源极慢）：
   `https://mirrors.tuna.tsinghua.edu.cn/msys2/distrib/x86_64/`
   选最新的 `msys2-x86_64-*.exe`
2. 双击安装，**目录保持默认 `C:\msys64`**（CMake preset 里写死了这个路径）

### 2. 配清华镜像（必做，否则 pacman 下载极慢）

开始菜单打开 **MSYS2** 终端，粘贴：

```bash
sed -i '1i Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/$arch/' /etc/pacman.d/mirrorlist.mingw
sed -i '1i Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/msys/$arch/' /etc/pacman.d/mirrorlist.msys
```

### 3. 更新 + 装齐所有依赖（含 git）

仍在 MSYS2 终端：

```bash
pacman -Syu --noconfirm
pacman -S --noconfirm --needed \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-qt6-base \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-glm \
  mingw-w64-x86_64-nlohmann-json \
  mingw-w64-x86_64-git
```

### 4. 加 PATH（让 cmd / VSCode 能用 g++ / cmake / qmake）

把 `C:\msys64\mingw64\bin` 加到**用户级** PATH。PowerShell：

```powershell
[Environment]::SetEnvironmentVariable('PATH', [Environment]::GetEnvironmentVariable('PATH','User') + ';C:\msys64\mingw64\bin', 'User')
```

然后**重开终端**让 PATH 生效。

### 5. clone + 编译 + 运行

cmd 或 MSYS2 终端：

```bash
git clone https://github.com/quan224/material_editor.git
cd material_editor/material_editor_project
cmake --preset mingw
cmake --build build-mingw
./build-mingw/MaterialEditor.exe
```

看到 `33/33 通过` 即成功。

### 6. 打包（可选，做成可分发）

```bash
windeployqt build-mingw/MaterialEditor.exe
```

打包后 `build-mingw\` 整个文件夹拷到别的 Windows 电脑即可直接运行，**无需安装任何东西**（MinGW 运行时已通过 `-static` 静态链接进 exe，windeployqt 负责收集 Qt dll）。

---

## 路线 B：MSVC（备选）

仅当装了 Visual Studio、且不介意 vcpkg 从源码编译 Qt6（约 1~2 小时）。

| 步骤 | 操作 |
|------|------|
| 装 VS 2022 | 勾选「使用 C++ 的桌面开发」+ Windows SDK（或只装 Build Tools）|
| 装 vcpkg | `git clone https://github.com/microsoft/vcpkg` → `vcpkg\bootstrap-vcpkg.bat` |
| 装依赖 | `vcpkg install qt6-base glm nlohmann-json` |
| 编译 | `cmake --preset vcpkg-vs2022` → `cmake --build build` |

> 这条路线较慢（Qt6 源码编译），且 MSVC 有企业授权门槛（>250 PC 或 >100 万美元营收的企业需付费订阅）。

---

## 注意事项

1. **CMake 配置无需修改**：`CMakePresets.json`（双 preset）、`CMakeLists.txt`（MinGW 分支 + `-static` 静态链接）都已在仓库里。
2. **mingw preset 显式指定编译器路径**（`C:/msys64/mingw64/bin/g++.exe`），即使 PATH 没配好也能编译，绕过 PATH 冲突。
3. **MSYS2 必须装在 `C:\msys64`**，否则要改 `CMakePresets.json` 里的编译器路径。
4. **VSCode**：项目 `.vscode` 已配 `cmake.useCMakePresets: always`，底部状态栏选 preset 即可，IntelliSense 自动跟随 preset 注入 include 路径。
5. **日志**：程序运行会在工作目录生成 `debug_log.txt`（相对路径），每次启动覆盖前次，互不干扰。
