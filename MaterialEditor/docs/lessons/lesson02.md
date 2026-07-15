# 课2：Core 基础设施

## 目标

实现 UUID、Hash、Logger、RefCounted、MathTypes 五个基础工具。后续所有模块都依赖它们。

---

## 背景知识

这些是整个项目的"基础设施层"，等同于 UE5 的 `Core` 模块中最基础的部分。

| 我们的类 | UE5 对应 | 作用 |
|---------|---------|------|
| `UUID` | `FGuid` | 唯一标识每个节点/引脚/连接 |
| `Hash` | `CityHash64` 等 | 编译器用哈希做代码块去重 |
| `Logger` | `UE_LOG` 宏 | 分级日志输出 |
| `Ref<T>` | `TRefCountPtr<T>` | 引用计数智能指针 |
| `MathTypes` | `FVector`, `FMatrix` 等 | 数学类型封装 |

---

## 操作步骤

### 1. 创建文件

```
src/Core/Public/UUID.h
src/Core/Public/Hash.h
src/Core/Public/Logger.h
src/Core/Public/RefCounted.h
src/Core/Public/MathTypes.h
```

### 2. UUID.h

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <random>
#include <functional>

struct UUID {
    uint64_t high = 0;
    uint64_t low = 0;

    static UUID Generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dist;
        return { dist(gen), dist(gen) };
    }

    std::string ToString() const {
        // 格式: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx（标准 UUID 格式 8-4-4-4-12）
        char buf[37];
        snprintf(buf, sizeof(buf),
            "%08x-%04x-%04x-%04x-%08x%04x",
            (uint32_t)(high >> 32),
            (uint32_t)(high >> 16) & 0xFFFF,
            (uint32_t)(high) & 0xFFFF,
            (uint16_t)(low >> 48) & 0xFFFF,
            (uint32_t)(low >> 16) & 0xFFFFFFFF,
            (uint16_t)(low) & 0xFFFF);
        return buf;
    }

    bool operator==(const UUID& o) const { return high == o.high && low == o.low; }
    bool operator!=(const UUID& o) const { return !(*this == o); }
    bool operator<(const UUID& o) const { return high < o.high || (high == o.high && low < o.low); }

    bool IsValid() const { return high != 0 || low != 0; }

    static UUID Invalid() { return { 0, 0 }; }
};

// 让 UUID 可以作为 std::map / std::unordered_map 的 key
namespace std {
template<>
struct hash<UUID> {
    size_t operator()(const UUID& id) const {
        // 复用 HashCombine，与项目其他哈希保持一致（见 HashUtil.h）
        size_t h = hash<uint64_t>()(id.high);
        h ^= hash<uint64_t>()(id.low) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
}
```

> **关于哈希写法**：UUID 的 hash 特化里写的是 `h ^= ... + 0x9e3779b9 + (h << 6) + (h >> 2)`，HashCombine 工具函数写的是 `a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2))`。两者数学结果完全相同（一个先 `^=` 后 `+`，另一个整体 `^`），只是写法风格不同。这里保留两种写法是为了让你看到 C++ 里"同一逻辑可以有多种表达"。实际项目里推荐统一用 `HashCombine`，方便维护。

**讲解**：
- 用 `mt19937_64` 随机数生成器产生两个 `uint64`，组成 128 位唯一标识
- `operator<` 使其可用于 `std::map`
- `std::hash` 特化使其可用于 `std::unordered_map`
- `IsValid()` 检查是否为全零（无效值）

**UE5 参考**：`Engine/Source/Runtime/Core/Public/Misc/Guid.h`

### 3. Hash.h

```cpp
#pragma once
#include <cstdint>
#include <string>

inline uint64_t HashCombine(uint64_t a, uint64_t b) {
    // boost::hash_combine 算法
    return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2));
}

inline uint64_t HashString(const std::string& s) {
    // FNV-1a 哈希
    uint64_t hash = 14695981039346656037ULL;
    for (char c : s) {
        hash ^= (uint64_t)c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline uint64_t HashRaw(const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}
```

**讲解**：
- `HashCombine` — 将两个哈希值合并为一个，用于组合键哈希
- `HashString` — FNV-1a 算法，快速且分布均匀
- `HashRaw` — 对任意内存块做哈希
- UE5 用 CityHash64（Google 的高性能哈希），我们用 FNV-1a 够用且简单

**UE5 参考**：搜索 `Engine/Source/Runtime/Core/Public/` 中的 `CityHash`

### 4. Logger.h

```cpp
#pragma once
#include <QDebug>
#include <cstdio>
#include <fstream>

// 日志写入文件（Windows 下 qDebug 输出走 OutputDebugString，普通终端看不到）
inline void LogToFile(const char* msg) {
    static std::ofstream logFile("debug_log.txt", std::ios::app);
    if (logFile.is_open()) logFile << msg << std::endl;
}

#define ME_LOG_INFO(msg, ...) \
    do { \
        qDebug("[INFO] " msg, ##__VA_ARGS__); \
        char _logBuf[512]; \
        snprintf(_logBuf, sizeof(_logBuf), "[INFO] " msg "\n", ##__VA_ARGS__); \
        LogToFile(_logBuf); \
    } while(0)

#define ME_LOG_WARNING(msg, ...) \
    do { \
        qWarning("[WARN] " msg, ##__VA_ARGS__); \
        char _logBuf[512]; \
        snprintf(_logBuf, sizeof(_logBuf), "[WARN] " msg "\n", ##__VA_ARGS__); \
        LogToFile(_logBuf); \
    } while(0)

#define ME_LOG_ERROR(msg, ...) \
    do { \
        qCritical("[ERROR] " msg, ##__VA_ARGS__); \
        char _logBuf[512]; \
        snprintf(_logBuf, sizeof(_logBuf), "[ERROR] " msg "\n", ##__VA_ARGS__); \
        LogToFile(_logBuf); \
    } while(0)
```

**讲解**：
- 同时输出到 qDebug 和文件 `debug_log.txt`（Windows 下 qDebug 走 `OutputDebugString`，普通终端看不到）
- `do { ... } while(0)` 是宏的安全写法，防止宏在 if/else 里展开时出错
- `char _logBuf[512]` 固定缓冲区——超过 512 字节的日志会被截断（`snprintf` 保证 `\0` 终止，所以不会越界，但内容会丢）。**教学简化**：实际生产代码可以用 `vsnprintf(NULL, 0, ...)` 先算出实际长度再动态分配，避免截断
- 用下划线前缀避免和外部变量名冲突
- `std::ios::app` 以追加模式打开文件，不会覆盖之前的日志
- `##__VA_ARGS__` 是可变参数宏（MSVC 和 GCC 都支持），允许零个额外参数

**UE5 参考**：`Engine/Source/Runtime/Core/Public/Logging/LoggingMacros.h` — `UE_LOG` 宏

### 5. RefCounted.h

```cpp
#pragma once
#include <memory>

template<typename T>
using Ref = std::shared_ptr<T>;

template<typename T, typename... Args>
Ref<T> MakeRef(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template<typename T>
using WeakRef = std::weak_ptr<T>;
```

**讲解**：
- **简化版**：直接用 `std::shared_ptr` 做引用计数。注意语义和 UE5 的 `TRefCountPtr` 有差异：`TRefCountPtr` 是**侵入式引用计数**（类继承 `FRefCountedObject`，计数直接存在对象里，无控制块开销），`shared_ptr` 是**非侵入式**（堆上额外分配控制块）。我们项目规模小，`shared_ptr` 够用，写起来也简单
- `Ref<T>` 比 `std::shared_ptr<T>` 短很多，代码更清晰
- `MakeRef` 封装 `std::make_shared`，一次分配内存
- `WeakRef` 用于打破循环引用（后续有需要时用）

**UE5 参考**：`Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h`

### 6. MathTypes.h

```cpp
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// 基础类型别名（等同 UE5 的 FVector, FMatrix 等）
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;

// 常量
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;
constexpr float HALF_PI = PI * 0.5f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;
```

**讲解**：
- `glm` 名字带 GL 是历史原因（早期定位是 OpenGL Mathematics），但**它本质是个纯数学库，不依赖 OpenGL**。命名风格参考了 GLSL 的 `vec3`/`mat4` 等，但概念和 HLSL 的 `float3`/`float4x4` 完全对齐。**本项目用 DX12 + HLSL，使用 glm 没有任何冲突**——它就是个数学运算库，不涉及图形 API
- 用 `using` 起别名而不是 `typedef`，C++11 推荐方式
- 后续渲染器中的矩阵运算（投影、变换）都用 glm

**UE5 参考**：`Engine/Source/Runtime/Core/Public/Math/Vector.h` 等

---

## 验证

修改 `src/main.cpp` 临时测试：

```cpp
#include <QApplication>
#include <QWidget>
#include "Core/Public/UUID.h"
#include "Core/Public/Hash.h"
#include "Core/Public/Logger.h"
#include "Core/Public/RefCounted.h"
#include "Core/Public/MathTypes.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // 测试 UUID
    auto id1 = UUID::Generate();
    auto id2 = UUID::Generate();
    ME_LOG_INFO("UUID 1: %s", id1.ToString().c_str());
    ME_LOG_INFO("UUID 2: %s", id2.ToString().c_str());
    ME_LOG_INFO("UUID equal: %s", id1 == id2 ? "yes" : "no");

    // 测试 Hash
    ME_LOG_INFO("Hash 'hello': %llu", HashString("hello"));

    // 测试 RefCounted
    auto v = MakeRef<Vec3>(1.0f, 2.0f, 3.0f);
    ME_LOG_INFO("Vec3: (%.1f, %.1f, %.1f), ref_count: %ld", (*v)[0], (*v)[1], (*v)[2], v.use_count());

    QWidget window;
    window.setWindowTitle("Material Editor v0.1");
    window.resize(1280, 720);
    window.show();
    return app.exec();
}
```

**预期输出**（在 Qt Creator 的"应用程序输出"或命令行中看到）：
```
[INFO] UUID 1: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
[INFO] UUID 2: yyyyyyyy-yyyy-yyyy-yyyy-yyyyyyyyyyyy
[INFO] UUID equal: no
[INFO] Hash 'hello': 1234567890
[INFO] Vec3: (1.0, 2.0, 3.0), ref_count: 1
```

---

## UE5 参考

| 我们的文件 | UE5 源码位置 |
|-----------|-------------|
| UUID.h | `Engine/Source/Runtime/Core/Public/Misc/Guid.h` |
| Hash.h | `Engine/Source/Runtime/Core/Public/Math/UnrealMathUtility.h` |
| Logger.h | `Engine/Source/Runtime/Core/Public/Logging/LoggingMacros.h` |
| RefCounted.h | `Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h` |
| MathTypes.h | `Engine/Source/Runtime/Core/Public/Math/Vector.h` |

---

## 完成标志

- [ ] UUID 可以生成并转字符串，两次生成不重复
- [ ] HashString 对同一字符串返回相同值
- [ ] Logger 在控制台输出带前缀的日志
- [ ] Ref<T> 可以创建和拷贝，引用计数正确
- [ ] MathTypes 的 Vec3 可以做基本运算
