# 课5：表达式基类 + 类型系统 + 反射系统

## 目标

1. 实现 **迷你反射系统**（类似 UE5 的 UPROPERTY） —— 表达式的元数据由反射集中管理
2. 定义 **Expression 抽象基类** —— 节点的"行为层"，只剩编译 + 引脚布局，参数全部走反射
3. 定义 **TypeSystem 工具类** —— HLSL 类型推导规则

三者关系：

```
Node（数据层）      Expression（行为层）          反射系统（元数据层）
├── id, position    ├── Compile() — 编译逻辑      ├── ClassDesc — 类元信息
├── inputPins       ├── GetInputPins/             ├── FieldDesc — 字段元信息
└── parameters      │   GetOutputPins — 引脚布局  └── Registry — 全局注册表
                    └── GetClassDesc() — 反射入口
       │                    │                              │
       └────── 三个层面共同描述一个材质表达式节点 ──────────┘
```

**核心设计原则（这一课贯彻始终）：**

- **元数据全部走反射**（typeName/displayName/category/fields 都存在 ClassDesc 里）
- **行为用虚函数**（Compile、引脚布局）
- **没有手写参数虚函数**（没有 GetParameters/SetParameter/GetParameter 这种手写 if-else 路径）
- **没有 GetCategoryColor 这种 hardcoded if-else**（颜色属于 UI 层，不属于 Expression）

---

## 背景知识

### Expression 和 Node 的关系

Node 只存数据（位置、引脚、参数 JSON）。但每种表达式有不同的**编译行为**——Add 节点编译成 HLSL 的 `+`，TextureSample 编译成 `tex2D()`。

**Expression** 是这个"行为"的描述。这和 UE5 的 `UMaterialExpression` 一样。

### TypeSystem 的作用

Float1 + Float3 → Float3。类型推导规则由 TypeSystem 集中管理。

### 为什么反射是必需的？

设想一个颜色常量 `ExprConstant3Vector`，暴露 R/G/B 给属性面板。**没有反射**时（伪代码，展示"假设要手写"会有多糟）：

```cpp
// ❌ 反例：假设没有反射时的手写参数虚函数
class ExprConstant3Vector : public Expression {
    float R, G, B;
public:
    // 假设基类要求子类手写这三个函数（我们的设计里已经不需要）
    std::vector<ParamDesc> GetParameters() const override {
        return {
            {"R", ParamType::Float, R},
            {"G", ParamType::Float, G},
            {"B", ParamType::Float, B},
        };
    }
    void SetParameter(const std::string& name, const nlohmann::json& val) override {
        if (name == "R" && val.is_number()) R = val.get<float>();
        else if (name == "G" && val.is_number()) G = val.get<float>();
        else if (name == "B" && val.is_number()) B = val.get<float>();
    }
    nlohmann::json GetParameter(const std::string& name) const override {
        if (name == "R") return R;
        if (name == "G") return G;
        if (name == "B") return B;
        return {};
    }
};
```

字段名 `"R"`/`"G"`/`"B"` 各出现 3 次（共 9 次硬编码字符串），加一个参数改 4 处，拼写错误编译器不报错。

**有反射后**：

```cpp
// ✅ 反射版：声明字段 + 三行宏
class ExprConstant3Vector : public Expression {
public:
    float R = 0.f, G = 0.f, B = 0.f;

    ME_BEGIN_CLASS(ExprConstant3Vector)
        ME_DISPLAY_NAME("Constant 3Vector")
        ME_CATEGORY("Constants")
        ME_FIELD(ExprConstant3Vector, R, 0.0f)
        ME_FIELD(ExprConstant3Vector, G, 0.0f)
        ME_FIELD(ExprConstant3Vector, B, 0.0f)
    ME_END_CLASS(ExprConstant3Vector)

    // ... 只需要写 Compile 和 GetInputPins/GetOutputPins
};
```

加一个字段就是加一行 `ME_FIELD`。序列化、类型安全、UI 生成全自动。这就是 UE5 UPROPERTY 的简化版。

---

## 第一部分：反射系统（元数据层，先讲因为后面要用）

### 1.1 文件结构

```
src/Reflection/Public/Reflection.h          — 核心数据结构 + Accessor 模板
src/Reflection/Public/ReflectionMacros.h    — 注册宏（ME_BEGIN_CLASS/ME_FIELD/ME_END_CLASS）
```

### 1.2 为什么需要"类型擦除"？—— 必须先理解的核心设计动机

在写任何代码之前，必须先想清楚一个**根本问题**：

> 反射系统要让 `ClassDesc` 存"一个类的所有字段"。但不同字段类型不同（float、int32_t、std::string、Vec3...），怎么用**一个统一的结构体**装下它们？

#### 第一次尝试：直接用模板（失败）

```cpp
// ❌ 假设 FieldDesc 是模板
template <typename T>
struct FieldDesc {
    std::string name;
    T defaultValue;
    T* ptr;
};
```

现在 `ClassDesc` 要存这个类的所有字段：

```cpp
class ExprConstant {
    float value_;          // 字段 1：float
    std::string name_;     // 字段 2：std::string
};

struct ClassDesc {
    std::vector<FieldDesc<???>> fields;  // ← 这里写什么？
};
```

- 写 `FieldDesc<float>` → 装不下 `string` 字段
- 写 `FieldDesc<int32_t>` → 装不下 `float` 字段
- **C++ 是静态类型语言，一个 `vector` 里所有元素类型必须一致**

走不通。

#### 第二次尝试：用 RTTI（被否决）

C++ 有运行时类型信息 RTTI：

```cpp
struct FieldDesc {
    std::string name;
    const std::type_info& type;  // ← typeid(float)、typeid(int) 都能存
};
```

但项目不用 RTTI，原因：

| 维度 | 自定义枚举 | RTTI (`type_info`) |
|---|---|---|
| 开销 | 4 字节整数 | 比较/哈希较慢 |
| 可序列化 | ✅ 能写进 JSON | ❌ `name()` 是实现相关的乱码 |
| 跨编译器 | ✅ 完全一致 | ⚠️ GCC/MSVC/Clang 输出不同 |
| 可扩展 | ✅ 加新类型加一个枚举 | 难以扩展 |

UE5 也不用 RTTI，自己搞了 `UClass` 系统（更复杂的枚举 + 元数据）。

#### 正确方案：类型擦除（type erasure）

**核心思想：把"编译期才知道的类型 T"压扁成"运行期可存储的标识"**。

类型信息被拆成**三个层次**：

| 层次 | 形式 | 何时确定 | 用途 |
|---|---|---|---|
| **1. C++ 真实类型 T** | 模板参数 | 编译期 | 生成专属代码（Accessor<T>） |
| **2. FieldType 枚举** | 运行期整数 | 编译期映射 | 运行期查询"这是什么类型"（UI 渲染决策） |
| **3. lambda 闭包** | 编译期烧录 | 编译期生成 | 运行期执行类型相关操作（toJson/fromJson） |

```
编译期                          运行期
──────                          ──────
T = float（模板参数）             FieldDesc {
   ↓                               name: "value_",
GetFieldType<float>() ←─┐         type: FieldType::Float,  ← 层 2：枚举标识
   ↓                     │         offset: 4,
if constexpr →           │         toJson: lambda ─────────── 层 3：lambda 闭包
  FieldType::Float ──────┘                       ↑
                                            内部烧录了 Accessor<float>
   ↓                                 }
FieldType_t 确定                    调用 toJson：
（宏展开时）                            lambda 内部 reinterpret_cast<float*>
```

- **层 1（T）**：编译期就消失，但生成代码时被"烧录"进 lambda
- **层 2（FieldType 枚举）**：运行期能查到的"类型 ID"，让 UI 知道"这是 Float 还是 Vec3"
- **层 3（lambda）**：每个字段的 lambda 是**专属生成**的，内部硬编码了正确的 `Accessor<T>`，运行期通过函数指针调用

**关键洞察：FieldType 枚举不是"真实类型的简化"，而是"运行期可存储的类型 ID"。真实类型操作能力由 lambda 保留。**

#### 类比理解

想象一个快递分拣系统：

- **C++ 类型 T** = 真实的包裹内容（书、衣服、电子设备）
- **FieldType 枚举** = 快递单上的"类别"标签（"图书"、"服装"、"电子"）
- **lambda 闭包** = 每个类别的专属处理流水线（图书按重量计费、服装按体积计费）

分拣中心（`vector<FieldDesc>`）只能按"类别标签"统一存档，不能直接存包裹本身。但处理时（调用 lambda），又调用了对应类型的专属流水线。

**理解了这三个层次，再往下看具体实现就一目了然**：FieldType 是层 2，FieldDesc 里那两个函数指针是层 3，Accessor<T> 是层 1 的实现。

---

### 1.3 字段类型枚举（层 2：运行期类型 ID）

```cpp
// Reflection.h —— 文件开头
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "Core/Public/MathTypes.h"   // Vec2/Vec3/Vec4
#include "Core/Public/Singleton.h"   // Singleton<T> 模板

namespace reflection {

enum class FieldType {
    Float,      // float
    Int,        // int32_t
    Bool,       // bool
    String,     // std::string
    Float2,     // Vec2
    Float3,     // Vec3
    Float4      // Vec4
};

} // namespace reflection
```

每种类型对应一个 Accessor 模板偏特化。需要支持新类型时，这里加枚举值 + 写偏特化。

### 1.4 字段描述符 FieldDesc（层 3：函数指针存什么）

```cpp
namespace reflection {

struct FieldDesc {
    std::string name;           // 字段名，如 "value_"
    FieldType   type;           // 类型枚举
    std::size_t offset;         // 字段在对象内存中的偏移量（offsetof 的结果）

    // 类型擦除的读写函数（下面详细讲）
    void (*toJson)(void* obj, std::size_t offset, nlohmann::json& out);
    void (*fromJson)(void* obj, std::size_t offset, const nlohmann::json& in);

    nlohmann::json defaultValue;  // 默认值
};

} // namespace reflection
```

`offset` 是最关键的域。对象在内存地址 `0x1000`，字段 offset 是 8，字段地址就是 `0x1008`。通过偏移量，我们可以在**不知道具体类类型**的情况下访问任意字段。

`toJson` 和 `fromJson` 是**函数指针**，用来做类型擦除。

### 1.5 函数指针类型擦除的回顾（动机已在 1.2 解释，这里看具体代码）

C++ 是静态类型语言，编译期就要确定类型。要在一个统一结构体里存"可能是 float、可能是 int、可能是 string"的字段信息，需要**类型擦除**——把类型信息藏起来。

```cpp
// toJson 签名：接收 void*（不知道是什么类型），输出 json
void (*toJson)(void* obj, std::size_t offset, nlohmann::json& out);
```

`void*` 是"指向任意类型的指针"。`toJson` 这个函数知道实际类型——因为我们会为每种类型生成一个专属实现。调用时传入对象指针和偏移量，函数内部做正确的类型转换。

### 1.6 Accessor 模板偏特化（层 1：C++ 真实类型 T 的专属实现）

```cpp
namespace reflection {

// 模板主声明（无定义，不支持的类型会编译报错）
template <typename T>
struct Accessor {
    // 如果到这里说明类型未被支持，编译期报错
};

// === float 的偏特化 ===
template <>
struct Accessor<float> {
    static void toJson(void* obj, std::size_t offset, nlohmann::json& out) {
        float* ptr = reinterpret_cast<float*>(
            static_cast<char*>(obj) + offset);
        out = *ptr;
    }
    static void fromJson(void* obj, std::size_t offset,
                          const nlohmann::json& in) {
        if (in.is_number()) {
            float* ptr = reinterpret_cast<float*>(
                static_cast<char*>(obj) + offset);
            *ptr = in.get<float>();
        }
    }
};

// === int32_t 的偏特化 ===
template <>
struct Accessor<int32_t> {
    static void toJson(void* obj, std::size_t offset, nlohmann::json& out) {
        int32_t* ptr = reinterpret_cast<int32_t*>(
            static_cast<char*>(obj) + offset);
        out = *ptr;
    }
    static void fromJson(void* obj, std::size_t offset,
                          const nlohmann::json& in) {
        if (in.is_number_integer()) {
            int32_t* ptr = reinterpret_cast<int32_t*>(
                static_cast<char*>(obj) + offset);
            *ptr = in.get<int32_t>();
        }
    }
};

// === bool 的偏特化 ===
template <>
struct Accessor<bool> {
    static void toJson(void* obj, std::size_t offset, nlohmann::json& out) {
        bool* ptr = reinterpret_cast<bool*>(
            static_cast<char*>(obj) + offset);
        out = *ptr;
    }
    static void fromJson(void* obj, std::size_t offset,
                          const nlohmann::json& in) {
        if (in.is_boolean()) {
            bool* ptr = reinterpret_cast<bool*>(
                static_cast<char*>(obj) + offset);
            *ptr = in.get<bool>();
        }
    }
};

// === std::string 的偏特化 ===
template <>
struct Accessor<std::string> {
    static void toJson(void* obj, std::size_t offset, nlohmann::json& out) {
        std::string* ptr = reinterpret_cast<std::string*>(
            static_cast<char*>(obj) + offset);
        out = *ptr;
    }
    static void fromJson(void* obj, std::size_t offset,
                          const nlohmann::json& in) {
        if (in.is_string()) {
            std::string* ptr = reinterpret_cast<std::string*>(
                static_cast<char*>(obj) + offset);
            *ptr = in.get<std::string>();
        }
    }
};

// === Vec2 的偏特化（json 数组 [x,y]）===
template <>
struct Accessor<Vec2> {
    static void toJson(void* obj, std::size_t offset, nlohmann::json& out) {
        Vec2* ptr = reinterpret_cast<Vec2*>(
            static_cast<char*>(obj) + offset);
        out = nlohmann::json::array({ptr->x, ptr->y});
    }
    static void fromJson(void* obj, std::size_t offset,
                          const nlohmann::json& in) {
        if (in.is_array() && in.size() >= 2) {
            Vec2* ptr = reinterpret_cast<Vec2*>(
                static_cast<char*>(obj) + offset);
            ptr->x = in[0].get<float>();
            ptr->y = in[1].get<float>();
        }
    }
};

// === Vec3 的偏特化（json 数组 [x,y,z]）===
template <>
struct Accessor<Vec3> {
    static void toJson(void* obj, std::size_t offset, nlohmann::json& out) {
        Vec3* ptr = reinterpret_cast<Vec3*>(
            static_cast<char*>(obj) + offset);
        out = nlohmann::json::array({ptr->x, ptr->y, ptr->z});
    }
    static void fromJson(void* obj, std::size_t offset,
                          const nlohmann::json& in) {
        if (in.is_array() && in.size() >= 3) {
            Vec3* ptr = reinterpret_cast<Vec3*>(
                static_cast<char*>(obj) + offset);
            ptr->x = in[0].get<float>();
            ptr->y = in[1].get<float>();
            ptr->z = in[2].get<float>();
        }
    }
};

// === Vec4 的偏特化（json 数组 [x,y,z,w]）===
template <>
struct Accessor<Vec4> {
    static void toJson(void* obj, std::size_t offset, nlohmann::json& out) {
        Vec4* ptr = reinterpret_cast<Vec4*>(
            static_cast<char*>(obj) + offset);
        out = nlohmann::json::array({ptr->x, ptr->y, ptr->z, ptr->w});
    }
    static void fromJson(void* obj, std::size_t offset,
                          const nlohmann::json& in) {
        if (in.is_array() && in.size() >= 4) {
            Vec4* ptr = reinterpret_cast<Vec4*>(
                static_cast<char*>(obj) + offset);
            ptr->x = in[0].get<float>();
            ptr->y = in[1].get<float>();
            ptr->z = in[2].get<float>();
            ptr->w = in[3].get<float>();
        }
    }
};

} // namespace reflection
```

**什么是模板偏特化？**

为特定类型提供专门的实现：

```cpp
template <typename T>          // 通用模板
struct Accessor { /* 无定义 */ };

template <>                    // 偏特化：T = float 时用这个
struct Accessor<float> { /* float 专属实现 */ };
```

主模板没有定义，意味着 `Accessor<double>`（未支持的类型）会编译报错——**编译期类型安全**。

**reinterpret_cast 为什么要先转 char\*？**

```cpp
float* ptr = reinterpret_cast<float*>(
    static_cast<char*>(obj) + offset);  // 先 void* → char*，再加偏移
```

`char*` 每个元素 1 字节，`char* + offset` 就是"向前 offset 字节"。直接 `void* + offset` 不允许——void* 不知道每次加多少字节。

- `static_cast<char*>(obj)` —— `void* → char*`，C++ 标准合法路径
- `reinterpret_cast<float*>(charPtr)` —— `char* → float*`，强行重新解释（必须 reinterpret，因为这两类型无合法转换路径）

### 1.7 类描述符 ClassDesc 和注册表 Registry

```cpp
namespace reflection {

// 类元信息 —— 一个类的所有元数据
// 承载 typeName/displayName/category/categoryColor + 字段表
struct ClassDesc {
    std::string type_name;        // 类名标识，如 "ExprAdd"
    std::string display_name;     // UI 显示名，如 "Add"
    std::string category;         // 分类，如 "Math"
    std::string category_color;   // UI 颜色，如 "#4CA7E8"
    std::vector<FieldDesc> fields;

    const FieldDesc* find(const std::string& field_name) const {
        for (const auto& f : fields) {
            if (f.name == field_name) return &f;
        }
        return nullptr;
    }
};

// 全局类型注册表 —— 继承 Singleton<T>
class Registry : public Singleton<Registry> {
    friend Singleton<Registry>;
public:
    void RegisterClass(ClassDesc desc) {
        classes_[desc.type_name] = std::move(desc);
    }
    const ClassDesc* Find(const std::string& type_name) const {
        auto it = classes_.find(type_name);
        return it != classes_.end() ? &(it->second) : nullptr;
    }
    std::vector<std::string> GetAllTypeNames() const {
        std::vector<std::string> names;
        for (const auto& [name, desc] : classes_) names.push_back(name);
        return names;
    }
private:
    Registry() = default;
    std::unordered_map<std::string, ClassDesc> classes_;
};

} // namespace reflection
```

**Singleton<T> 模板**（Core/Public/Singleton.h，CRTP 模式）：

```cpp
template <typename T>
class Singleton {
public:
    static T& GetInstance() {
        static T instance;  // Meyers Singleton：C++11 起线程安全
        return instance;
    }
protected:
    Singleton() = default;
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
};
```

**CRTP（Curiously Recurring Template Pattern）**：

```cpp
class Registry : public Singleton<Registry> { ... };
//                          ▲
//                          └── 把自己作为模板参数传给基类
```

基类知道"我服务的子类是谁"，可以返回子类引用 `T&`。NodeFactory 也是这样：

```cpp
class NodeFactory : public Singleton<NodeFactory> { ... };
```

整个项目里所有需要单例的类都用同一个模板，风格统一。

---

### 1.8 注册宏 ReflectionMacros.h（把三层串起来）

反射系统的"用户接口"。三个核心宏：`ME_BEGIN_CLASS` / `ME_FIELD` / `ME_END_CLASS`，加两个可选元数据宏：`ME_DISPLAY_NAME` / `ME_CATEGORY`。

```cpp
// ReflectionMacros.h —— 完整内容，直接复制到这个文件即可
#pragma once
#include "Reflection/Public/Reflection.h"
#include <nlohmann/json.hpp>
#include <type_traits>
#include <cstddef>

// 辅助函数：根据 C++ 类型选择 FieldType
// 用 if constexpr 在编译期决定，不满足的分支不参与编译
template <typename T>
constexpr reflection::FieldType GetFieldType() {
    using DT = std::decay_t<T>;
    if constexpr (std::is_same_v<DT, float>)        return reflection::FieldType::Float;
    else if constexpr (std::is_same_v<DT, int32_t>) return reflection::FieldType::Int;
    else if constexpr (std::is_same_v<DT, bool>)    return reflection::FieldType::Bool;
    else if constexpr (std::is_same_v<DT, std::string>) return reflection::FieldType::String;
    else if constexpr (std::is_same_v<DT, Vec2>)    return reflection::FieldType::Float2;
    else if constexpr (std::is_same_v<DT, Vec3>)    return reflection::FieldType::Float3;
    else if constexpr (std::is_same_v<DT, Vec4>)    return reflection::FieldType::Float4;
    else static_assert(sizeof(T) == 0, "Unsupported field type");
}

// 开启类的反射注册（函数体未闭合，ME_END_CLASS 负责闭合）
#define ME_BEGIN_CLASS(ClassName)                                                       \
    static const reflection::ClassDesc &GetClassDesc_Static() {                        \
        static reflection::ClassDesc desc;                                              \
        static bool initialized = false;                                                \
        if (!initialized) {                                                             \
            initialized = true;                                                         \
            desc.type_name = #ClassName;                                                \
            desc.display_name = #ClassName; /* 默认显示名 = 类名 */                       \
            desc.category = "Misc";                  /* 默认分类 */

#define ME_DISPLAY_NAME(name) desc.display_name = (name);
#define ME_CATEGORY(name)     desc.category = (name);
#define ME_CATEGORY_COLOR(hex) desc.category_color = (hex);

// 注册一个字段
#define ME_FIELD(ClassName, FieldName, DefaultVal)                                      \
    {                                                                                   \
        using FieldType_t = std::decay_t<decltype(ClassName::FieldName)>;               \
        reflection::FieldDesc field;                                                    \
        field.name = #FieldName;                                                        \
        field.type = GetFieldType<FieldType_t>();                                       \
        field.offset = offsetof(ClassName, FieldName);                                  \
        field.defaultValue = (DefaultVal);                                              \
        field.toJson = [](void* obj, std::size_t off, nlohmann::json& out) {            \
            reflection::Accessor<FieldType_t>::toJson(obj, off, out);                   \
        };                                                                              \
        field.fromJson = [](void* obj, std::size_t off, const nlohmann::json& in) {     \
            reflection::Accessor<FieldType_t>::fromJson(obj, off, in);                  \
        };                                                                              \
        desc.fields.push_back(std::move(field));                                        \
    }

// 结束注册，重写 GetClassDesc 虚函数
#define ME_END_CLASS(ClassName)                                                         \
        }                                                                               \
        return desc;                                                                    \
    }                                                                                   \
    const reflection::ClassDesc *GetClassDesc() const override {                       \
        return &GetClassDesc_Static();                                                  \
    }
```

**关键 C++ 技巧速查（宏里用到的）：**

| 技巧 | 作用 |
|---|---|
| `#FieldName` | 预处理器的字符串化：`#value_` → `"value_"` |
| `offsetof(Class, Member)` | 返回字段在对象中的字节偏移量 |
| `decltype(ClassName::FieldName)` | 编译期推导字段类型 |
| `std::decay_t<T>` | 去掉 const/引用修饰，还原裸类型 |
| `std::is_same_v<A, B>` | 编译期比较两个类型是否相同 |
| `if constexpr` | 编译期 if，不匹配的分支被丢弃 |
| 无捕获 lambda → 函数指针 | 类型擦除：把类型信息"烧录"在 lambda 里，对外暴露统一签名 |

**offsetof 的近似实现原理：**

```cpp
#define offsetof(Class, Member) \
    (std::size_t)(&((Class*)0)->Member)  // 假设对象在地址 0，成员地址就是偏移量
```

**限制**：`offsetof` 只对 **standard-layout 类型**有定义。我们的 Expression 子类有虚表指针但单继承，仍安全。多重继承下用 offsetof 是 UB。

---

## 第二部分：Expression 基类（行为层）

```cpp
// Expression.h
#pragma once
#include "MaterialGraph/Public/Types.h"
#include "Reflection/Public/Reflection.h"
#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

class MaterialCompiler;
class Node;

// 引脚描述（用于 GetInputPins/GetOutputPins）
struct ExpressionPinDesc {
    std::string name;
    EValueType type;
    std::string default_value;
};

class Expression {
public:
    virtual ~Expression() = default;

    // === 反射入口（纯虚：子类必须通过 ME_BEGIN_CLASS 宏重写）===
    virtual const reflection::ClassDesc* GetClassDesc() const = 0;

    // === 引脚布局 ===
    virtual std::vector<ExpressionPinDesc> GetInputPins() const = 0;
    virtual std::vector<ExpressionPinDesc> GetOutputPins() const = 0;

    // === 编译 ===
    virtual std::vector<int32_t> Compile(MaterialCompiler* compiler, Node* ownerNode) const = 0;

    // === 参数读写（非虚，纯反射实现）===
    // 子类不需要也不允许重写
    std::vector<reflection::FieldDesc> GetParameters() const;
    void SetParameter(const std::string& name, const nlohmann::json& value);
    nlohmann::json GetParameter(const std::string& name) const;
};
```

```cpp
// Expression.cpp
#include "Expression/Public/Expression.h"

std::vector<reflection::FieldDesc> Expression::GetParameters() const {
    const reflection::ClassDesc* desc = GetClassDesc();
    if (!desc) return {};
    return desc->fields;
}

void Expression::SetParameter(const std::string& name, const nlohmann::json& value) {
    const reflection::ClassDesc* desc = GetClassDesc();
    if (!desc) return;
    const reflection::FieldDesc* field = desc->find(name);
    if (!field) return;
    field->fromJson(static_cast<void*>(const_cast<Expression*>(this)), field->offset, value);
}

nlohmann::json Expression::GetParameter(const std::string& name) const {
    const reflection::ClassDesc* desc = GetClassDesc();
    if (!desc) return {};
    const reflection::FieldDesc* field = desc->find(name);
    if (!field) return {};
    nlohmann::json out;
    field->toJson(static_cast<void*>(const_cast<Expression*>(this)), field->offset, out);
    return out;
}
```

**设计要点：**

- `GetClassDesc()` 是**纯虚**——子类必须用宏重写，没有"不启用反射"的逃逸路径
- `GetParameters/SetParameter/GetParameter` 是**非虚**——子类不能重写，行为完全由反射决定
- 没有 `GetTypeName/GetDisplayName/GetCategory/GetCategoryColor` 虚函数——这些元数据在 ClassDesc 里，调用方通过 `GetClassDesc()->display_name` 等访问
- 没有 `ExpressionParamDesc` 这种重复枚举——参数类型就是 `reflection::FieldType`，不重复定义

**调用方怎么读 typeName/category？**

```cpp
const reflection::ClassDesc* desc = expr->GetClassDesc();
std::cout << desc->type_name;     // "ExprAdd"
std::cout << desc->display_name;  // "Add"
std::cout << desc->category;      // "Math"
```

---

## 第三部分：TypeSystem

```cpp
// TypeSystem.h
#pragma once
#include "MaterialGraph/Public/Types.h"
#include <cstdint>

class TypeSystem {
public:
    // 算术运算结果类型推导
    static EValueType GetArithmeticResultType(EValueType a, EValueType b) {
        if (a == EValueType::Unknown || b == EValueType::Unknown)
            return EValueType::Unknown;
        if (a == b) return a;
        if (a == EValueType::Float1) return b;
        if (b == EValueType::Float1) return a;
        return EValueType::Unknown;
    }

    static int GetComponentCount(EValueType type) {
        switch (type) {
            case EValueType::Float1: return 1;
            case EValueType::Float2: return 2;
            case EValueType::Float3: return 3;
            case EValueType::Float4: return 4;
            default: return 0;
        }
    }

    // Unknown 返回 nullptr，让调用方知道这是错误
    static const char* ToHLSLType(EValueType type) {
        switch (type) {
            case EValueType::Float1: return "float";
            case EValueType::Float2: return "float2";
            case EValueType::Float3: return "float3";
            case EValueType::Float4: return "float4";
            default: return nullptr;
        }
    }
};
```

**规则：**

- 任一为 Unknown → Unknown（错误传播）
- 同类型 → 该类型
- Float1 + FloatN → FloatN（标量扩展）
- Float2 + Float3 → Unknown（编译期错误，**不要写"取较大维度"的隐式升级**）

**UE5 参考**：`HLSLMaterialTranslator.cpp` 搜索 `GetArithmeticResultType`

---

## 第四部分：完整示例 — ExprConstant

```cpp
// ExprConstant.h
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"

class ExprConstant : public Expression {
public:
    float value_ = 0.0f;

    // 反射注册：声明字段 + 元数据
    ME_BEGIN_CLASS(ExprConstant)
        ME_DISPLAY_NAME("Constant")
        ME_CATEGORY("Constants")
        ME_FIELD(ExprConstant, value_, 0.0f)
    ME_END_CLASS(ExprConstant)

    // 引脚布局
    std::vector<ExpressionPinDesc> GetInputPins() const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {{"Output", EValueType::Float1, ""}};
    }

    // 编译
    std::vector<int32_t> Compile(MaterialCompiler* compiler, Node* ownerNode) const override {
        return {compiler->Constant(value_)};
    }
};
```

**这个类只写了 3 件事**：声明字段（反射）、声明引脚、实现编译。没有任何手写参数代码。

---

## 工作原理总结

完整的数据流：

```
1. 编译期
   ME_BEGIN_CLASS(ExprConstant)
       ME_FIELD(ExprConstant, value_, 0.0f)
   ME_END_CLASS(ExprConstant)
       ↓ 宏展开
   生成 GetClassDesc_Static()，构造 ClassDesc {
       type_name: "ExprConstant",
       display_name: "Constant",
       category: "Constants",
       fields: [{ name:"value_", offset:4, toJson:lambda... }]
   }
       ↓
   重写 GetClassDesc() 虚函数

2. 运行时 — 读取参数
   expr->GetParameter("value_")
       ↓ 非虚实现
   GetClassDesc() → ClassDesc
   ClassDesc->find("value_") → FieldDesc
   FieldDesc->toJson(this, offset, out)
       ↓ lambda 内部
   Accessor<float>::toJson(this, 4, out)
       ↓
   float* ptr = (float*)((char*)this + 4);
   out = *ptr;
```

---

## 验证

### 测试1：TypeSystem 类型推导

```cpp
auto r1 = TypeSystem::GetArithmeticResultType(EValueType::Float1, EValueType::Float3);
assert(r1 == EValueType::Float3);

auto r2 = TypeSystem::GetArithmeticResultType(EValueType::Float2, EValueType::Float3);
assert(r2 == EValueType::Unknown);
```

### 测试2：反射参数读写

```cpp
ExprConstant e;
e.SetParameter("value_", 42.0f);
float val = e.GetParameter("value_").get<float>();
assert(val == 42.0f);

auto params = e.GetParameters();
assert(params.size() == 1);
assert(params[0].name == "value_");
assert(params[0].type == reflection::FieldType::Float);
```

### 测试3：序列化往返

```cpp
ExprConstant e;
e.value_ = 3.14f;

nlohmann::json j;
for (const auto& field : e.GetParameters()) {
    j[field.name] = e.GetParameter(field.name);
}

ExprConstant e2;
for (auto it = j.begin(); it != j.end(); ++it) {
    e2.SetParameter(it.key(), it.value());
}
assert(e2.value_ == e.value_);
```

### 测试4：元数据查询

```cpp
ExprConstant e;
const reflection::ClassDesc* desc = e.GetClassDesc();
assert(desc->type_name == "ExprConstant");
assert(desc->display_name == "Constant");
assert(desc->category == "Constants");
```

---

## C++ 知识点速查表

| 特性 | 用在哪 | 作用 |
|------|--------|------|
| `#` 字符串化 | ME_FIELD | `#value_` → `"value_"` |
| `offsetof` | ME_FIELD | 字段在对象中的字节偏移 |
| 模板偏特化 | Accessor<float/int/bool/string/Vec2/Vec3/Vec4> | 为每种类型提供专属读写 |
| CRTP 单例 | Singleton<T> / Registry / NodeFactory | 编译期类型安全的单例 |
| `if constexpr` | GetFieldType | 编译期分支 |
| `std::is_same_v` | GetFieldType | 编译期类型比较 |
| `std::decay_t` | GetFieldType | 去 const/引用修饰 |
| `decltype` | ME_FIELD | 自动推导字段类型 |
| 无捕获 lambda → 函数指针 | ME_FIELD | 类型擦除实现 |
| `reinterpret_cast` | Accessor | char* + offset → T* |
| `static_cast` | Accessor | void* → char* |

---

## 常见问题

### Q1：offsetof 编译警告 C4204

某些编译器对含虚函数的类用 `offsetof` 会警告。可以 `#pragma warning(suppress: 4204)` 抑制。我们的 Expression 子类满足 standard-layout（单继承 + 虚函数，无虚基类），安全。

### Q2：宏展开后编译错误，看不出哪里错了

VS Code 中右键 → "预处理输出"（或 `cl /P`），看宏展开后的实际代码。或手动展开宏对照。

### Q3：SetParameter 设置了但 GetParameter 读不到

检查 `ME_FIELD` 中的字段名和调用时传入的名字是否**完全一致**（包括下划线）。`#FieldName` 字符串化是精确的，`value_` 不会变成 `value`。

### Q4：lambda 转函数指针报错

只有**无捕获 lambda**能转函数指针。我们的 Accessor lambda 都是无捕获的。

---

## UE5 参考

| 功能 | UE5 源码位置 |
|------|-------------|
| MaterialExpression 基类 | `Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression.h` |
| `Compile()` 虚函数 | 同上，搜索 `virtual int32 Compile(FMaterialCompiler*` |
| 类型推导规则 | `HLSLMaterialTranslator.cpp` 搜索 `GetArithmeticResultType` |
| UPROPERTY 宏定义 | `Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h` |
| 属性反射系统 | `Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h` |
| UClass 注册 | `Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h` |

---

## 完成标志

- [ ] Reflection.h 编译通过：FieldType、FieldDesc、Accessor（6 个偏特化）、ClassDesc（含 display_name/category/category_color）、Registry（继承 Singleton）
- [ ] ReflectionMacros.h 编译通过：GetFieldType、ME_BEGIN_CLASS、ME_DISPLAY_NAME、ME_CATEGORY、ME_FIELD、ME_END_CLASS
- [ ] Expression.h 编译通过：纯虚 GetClassDesc、纯虚 Compile/GetInputPins/GetOutputPins、非虚 Get/Set/GetParameter
- [ ] TypeSystem 类型推导规则正确
- [ ] ExprConstant 反射版的 Get/Set/Serialize/Metadata 查询全部正确
- [ ] 理解每个 C++ 技巧（offsetof / 偏特化 / if constexpr / 函数指针类型擦除 / CRTP）
