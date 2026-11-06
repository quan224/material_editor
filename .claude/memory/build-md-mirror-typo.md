---
name: build-md-mirror-typo
description: BUILD.md 里清华 mingw 镜像路径用错了变量，照抄会 404
metadata: 
  node_type: memory
  type: reference
  originSessionId: c8808508-b25d-4346-b4a1-1364a712d732
  modified: 2026-07-29T11:05:03.386Z
---

`BUILD.md` 第 29 行配置清华镜像时，mingw 的 Server 写的是 `$arch`：
```
Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/$arch/
```
这是**错的**。清华镜像 mingw 目录下是 `mingw64/`（即 `$repo`），不是 `x86_64/`（`$arch`）。实测：
- `mingw/$repo/` → `mingw/mingw64/` → HTTP 200 ✅
- `mingw/$arch/` → `mingw/x86_64/` → **404** ❌

正确的两条（我已写进 `C:\msys64\etc\pacman.d\`）：
```
mingw: Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/$repo/
msys:  Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/msys/$arch/
```
（mingw 包用 `$repo`，msys 包才用 `$arch`——和上游 mirrorlist 一致。）

**How to apply**：重装 MSYS2 或在新机器配置时，mingw 镜像用 `$repo`，别照抄 BUILD.md。msys 那条 `$arch` 是对的。
