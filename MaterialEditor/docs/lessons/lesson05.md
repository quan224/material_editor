# 课5：表达式基类 + 反射系统（UE 风格：Property 继承 + 注册表）

## 目标

1. 实现 **反射系统**（Property 继承体系 + 注册表，对标 UE5 的 `FProperty`/`UClass`/`IPropertyTypeCustomization`）
2. 定义 **Expression 抽象基类** —— 节点的"行为层"，参数全部走反射
3. 定义 **TypeSystem 工具类** —— HLSL 类型推导规则（课6 扩展版用）

三者关系：

```
Node（数据层）      Expression（行为层）          反射系统（元数据层）
├── id, position    ├── Compile() — 编译逻辑      ├── ClassDesc — 类元信息
├── inputPins       ├── GetInputPins/             ├── Property — 字段元信息（继承体系）
└── parameters      │   GetOutputPins             └── PropertyCustomizer — UI 注册表
                    └── GetClassDesc() — 反射入口
```

**核心设计原则（对标 UE5）：**
- **Property 继承体系**实现类型擦除（虚函数 `toJson`/`fromJson`，对标 `FProperty::ImportText`/`ExportText`）
- **不使用枚举标识类型**——子类身份就是类型（`FloatProperty` 就是"float 类型"）
- **注册表**替代 `switch(枚举)`——UI 层为每种类型注册定制器（对标 `IPropertyTypeCustomization`）
- **ME_FIELD 宏**对标 `UPROPERTY` + UHT 代码生成

---

## 背景知识

### 类型擦除：用继承 + 虚函数（对标 UE）

反射系统要让 `ClassDesc` 存"一个类的所有字段"。不同字段类型不同（float/int/string/Vec3...），怎么用**统一的方式**存？

**方案：Property 继承体系**——每种类型一个子类，基类定义统一虚函数接口，运行期通过基类指针多态调用。

```
Property（基类，虚函数接口）
  ├── FloatProperty     → 重写 toJson/fromJson（处理 float）
  ├── IntProperty       → 重写（处理 int32_t）
  ├── BoolProperty      → 重写（处理 bool）
  ├── StringProperty    → 重写（处理 std::string）
  ├── Vec2/3/4Property  → 重写（处理 Vec2/3/4）

ClassDesc 持有：std::vector<std::unique_ptr<Property>>   ← 基类指针数组
```

**对标 UE**：UE 的 `FProperty` 就是这个设计——基类 `FProperty` + 子类 `FFloatProperty`/`FBoolProperty`/`FObjectProperty` 等，`UClass` 持有 `TArray<FProperty*>`。

### 对照：枚举方式 vs 继承方式

| | 枚举 + 函数指针（旧版）| 继承 + 虚函数（本版，对标 UE）|
|---|---|---|
| 类型标识 | `FieldType` 枚举 | **子类身份**（FloatProperty 就是 float）|
| 类型操作 | 函数指针（FieldDesc::toJson）| **虚函数**（Property::toJson）|
| 统一存储 | FieldDesc 结构体（值）| `unique_ptr<Property>`（指针）|
| UI 判断类型 | `switch(FieldType)` | **注册表**（类型→定制器）|
| 对照 UE | 无（教学简化）| `FProperty` 继承体系 |

---

## 第一部分：反射系统（Property 继承体系）

### 1.1 文件结构

```
src/Reflection/Public/Property.h         — Property 基类 + 所有子类
src/Reflection/Public/ClassDesc.h        — 类描述符（持有 Property 数组）
src/Reflection/Public/ReflectionMacros.h — 注册宏（ME_BEGIN_CLASS / ME_FIELD / ME_END_CLASS）
src/Reflection/Public/PropertyCustomizer.h — UI 注册表（对标 IPropertyTypeCustomization）
```

### 1.2 Property 基类（对标 `FProperty`）

**文件：`src/Reflection/Public/Property.h`**

```cpp
#pragma once
#include <string>
#include <cstddef>
#include <memory>
#include <nlohmann/json.hpp>
#include "Core/Public/MathTypes.h"

// ============================================================================
// Property 基类 —— 所有字段类型的统一接口（对标 UE 的 FProperty）
// ============================================================================
// 类型擦除的核心：ClassDesc 存 unique_ptr<Property>，不知具体类型。
// 调 toJson/fromJson 时虚函数分发到正确的子类。
//
// 对标 UE：
//   FProperty（基类）
//     ├── ImportText / ExportText（序列化，对应我们的 toJson/fromJson）
//     ├── Serialize（二进制序列化，我们用 JSON 代替）
//     └── 各种子类（FFloatProperty/FBoolProperty/...）
//
// const-correctness 要点：
//   - Property 自身是不可变元数据（name/offset/defaultValue 注册后不再变），
//     所以 toJson/fromJson 都是 const 方法。
//   - 它们读写的是【传入的 obj】，不是 Property 自己：
//       toJson   读 obj  → obj 用 const void*
//       fromJson 写 obj  → obj 用 void*
//   - 这样调用方（Expression::Get/SetParameter）一处 const_cast 都不用。
class Property {
public:
    virtual ~Property() = default;

    // === 类型擦除的读写接口（对标 FProperty::ExportText/ImportText）===
    // obj = 对象基址，offset = 字段在对象里的偏移（offsetof 的结果）
    virtual void toJson(const void* obj, nlohmann::json& out) const = 0;       // 读
    virtual void fromJson(void* obj, const nlohmann::json& in) const = 0;      // 写

    // === 公共属性（注册期由宏设置，之后只读）===
    std::string       name;              // 字段名，如 "scale"
    std::size_t       offset = 0;        // 字段在对象内存中的偏移
    nlohmann::json    defaultValue;      // 默认值（JSON 统一存，任何类型都能装）

    // === 设置属性（宏里用，链式调用）===
    Property& SetName(const std::string& n)    { name = n;         return *this; }
    Property& SetOffset(std::size_t o)         { offset = o;       return *this; }
    Property& SetDefault(const nlohmann::json& d) { defaultValue = d; return *this; }
};
```

**讲解**：
- **虚函数 `toJson`/`fromJson`**：类型擦除的读写接口。子类内部用 `reinterpret_cast` 把 `obj + offset` 转成正确类型读写。
- **`toJson` 用 `const void*`，`fromJson` 用 `void*`**：读用 const、写用非 const——这样调用方一处 `const_cast` 都不用（见第三部分 Expression）。
- **`unique_ptr<Property>`**：ClassDesc 持有这个，堆分配。对标 UE 的 `TArray<FProperty*>`。
- **不设 `GetType()`**：不需要枚举标识类型——子类身份就是类型。UI 层用注册表（见 1.5）。

### 1.3 Property 子类（对标 `FFloatProperty` 等）

**文件：`src/Reflection/Public/Property.h`（继续）**

每种类型一个子类，重写 `toJson`/`fromJson`。内部用 `static_cast<const char*>(obj) + offset`（读）/ `static_cast<char*>(obj) + offset`（写）定位字段。

```cpp
// ============================================================================
// FloatProperty —— float 字段（对标 FFloatProperty）
// ============================================================================
class FloatProperty : public Property {
public:
    void toJson(const void* obj, nlohmann::json& out) const override {
        const float* ptr = reinterpret_cast<const float*>(static_cast<const char*>(obj) + offset);
        out = *ptr;
    }
    void fromJson(void* obj, const nlohmann::json& in) const override {
        if (in.is_number()) {
            float* ptr = reinterpret_cast<float*>(static_cast<char*>(obj) + offset);
            *ptr = in.get<float>();
        }
    }
};

// ============================================================================
// IntProperty —— int32_t 字段（对标 FIntProperty）
// ============================================================================
class IntProperty : public Property {
public:
    void toJson(const void* obj, nlohmann::json& out) const override {
        const int32_t* ptr = reinterpret_cast<const int32_t*>(static_cast<const char*>(obj) + offset);
        out = *ptr;
    }
    void fromJson(void* obj, const nlohmann::json& in) const override {
        if (in.is_number_integer()) {
            int32_t* ptr = reinterpret_cast<int32_t*>(static_cast<char*>(obj) + offset);
            *ptr = in.get<int32_t>();
        }
    }
};

// ============================================================================
// BoolProperty —— bool 字段（对标 FBoolProperty）
// ============================================================================
class BoolProperty : public Property {
public:
    void toJson(const void* obj, nlohmann::json& out) const override {
        const bool* ptr = reinterpret_cast<const bool*>(static_cast<const char*>(obj) + offset);
        out = *ptr;
    }
    void fromJson(void* obj, const nlohmann::json& in) const override {
        if (in.is_boolean()) {
            bool* ptr = reinterpret_cast<bool*>(static_cast<char*>(obj) + offset);
            *ptr = in.get<bool>();
        }
    }
};

// ============================================================================
// StringProperty —— std::string 字段（对标 FStrProperty）
// ============================================================================
class StringProperty : public Property {
public:
    void toJson(const void* obj, nlohmann::json& out) const override {
        const std::string* ptr = reinterpret_cast<const std::string*>(static_cast<const char*>(obj) + offset);
        out = *ptr;
    }
    void fromJson(void* obj, const nlohmann::json& in) const override {
        if (in.is_string()) {
            std::string* ptr = reinterpret_cast<std::string*>(static_cast<char*>(obj) + offset);
            *ptr = in.get<std::string>();
        }
    }
};

// ============================================================================
// Vec2Property / Vec3Property / Vec4Property —— 向量字段（对标 FStructProperty）
// ============================================================================
class Vec2Property : public Property {
public:
    void toJson(const void* obj, nlohmann::json& out) const override {
        const Vec2* ptr = reinterpret_cast<const Vec2*>(static_cast<const char*>(obj) + offset);
        out = nlohmann::json::array({ptr->x, ptr->y});
    }
    void fromJson(void* obj, const nlohmann::json& in) const override {
        if (in.is_array() && in.size() >= 2) {
            Vec2* ptr = reinterpret_cast<Vec2*>(static_cast<char*>(obj) + offset);
            ptr->x = in[0].get<float>();
            ptr->y = in[1].get<float>();
        }
    }
};

class Vec3Property : public Property {
public:
    void toJson(const void* obj, nlohmann::json& out) const override {
        const Vec3* ptr = reinterpret_cast<const Vec3*>(static_cast<const char*>(obj) + offset);
        out = nlohmann::json::array({ptr->x, ptr->y, ptr->z});
    }
    void fromJson(void* obj, const nlohmann::json& in) const override {
        if (in.is_array() && in.size() >= 3) {
            Vec3* ptr = reinterpret_cast<Vec3*>(static_cast<char*>(obj) + offset);
            ptr->x = in[0].get<float>();
            ptr->y = in[1].get<float>();
            ptr->z = in[2].get<float>();
        }
    }
};

class Vec4Property : public Property {
public:
    void toJson(const void* obj, nlohmann::json& out) const override {
        const Vec4* ptr = reinterpret_cast<const Vec4*>(static_cast<const char*>(obj) + offset);
        out = nlohmann::json::array({ptr->x, ptr->y, ptr->z, ptr->w});
    }
    void fromJson(void* obj, const nlohmann::json& in) const override {
        if (in.is_array() && in.size() >= 4) {
            Vec4* ptr = reinterpret_cast<Vec4*>(static_cast<char*>(obj) + offset);
            ptr->x = in[0].get<float>();
            ptr->y = in[1].get<float>();
            ptr->z = in[2].get<float>();
            ptr->w = in[3].get<float>();
        }
    }
};
```

**讲解**：
- 每个子类的 `toJson`/`fromJson` 用 `reinterpret_cast<T*>` 把 `obj + offset` 转成正确类型的指针读写。读用 `const char*`，写用 `char*`——和基类签名匹配。
- 这和旧版 `Accessor<T>` 内部逻辑完全一样，只是从"模板特化 + 函数指针"变成了"虚函数重写"。
- **对标 UE**：UE 的 `FFloatProperty::ExportText` 内部也是 `*(float*)((char*)obj + offset)` 的逻辑。

### 1.4 MakeProperty 工厂 + ME_FIELD 宏（对标 `UPROPERTY` + UHT）

**文件：`src/Reflection/Public/ReflectionMacros.h`**

```cpp
#pragma once
#include "Reflection/Public/Property.h"
#include "Reflection/Public/ClassDesc.h"
#include <type_traits>
#include <cstddef>

// ============================================================================
// MakeProperty<T> —— 工厂函数：按 C++ 类型 T 创建对应的 Property 子类
// ============================================================================
// 对标 UE 的 UHT：UHT 扫描 UPROPERTY 宏时，根据 C++ 类型生成对应的
// FProperty 子类实例（FFloatProperty/FBoolProperty/...）。
// 我们用模板 + if constexpr 在 C++ 编译期做同样的"类型→子类"映射。
//
// 第三个参数按 C++ 类型 T 传默认值（不是 json）——因为 Vec3 等用户类型
// 不能隐式转 nlohmann::json。下面用 Property 自己的 toJson 把它序列化进
// prop->defaultValue，和旧版 Accessor::toJson 转换默认值同理（现在走虚函数）。
template <typename T>
Property* MakeProperty(const std::string& name,
                       std::size_t offset,
                       const T& defaultVal) {
    using DT = std::decay_t<T>;
    Property* prop = nullptr;

    if      constexpr (std::is_same_v<DT, float>)        prop = new FloatProperty();
    else if constexpr (std::is_same_v<DT, int32_t>)      prop = new IntProperty();
    else if constexpr (std::is_same_v<DT, bool>)         prop = new BoolProperty();
    else if constexpr (std::is_same_v<DT, std::string>)  prop = new StringProperty();
    else if constexpr (std::is_same_v<DT, Vec2>)         prop = new Vec2Property();
    else if constexpr (std::is_same_v<DT, Vec3>)         prop = new Vec3Property();
    else if constexpr (std::is_same_v<DT, Vec4>)         prop = new Vec4Property();
    else static_assert(sizeof(DT) == 0, "Unsupported property type — add a Property subclass + MakeProperty branch");

    prop->SetName(name).SetOffset(offset);
    prop->toJson(&defaultVal, 0, prop->defaultValue);   // 序列化默认值（&defaultVal → const void*）
    return prop;   // 返回裸指针，由 ClassDesc 用 unique_ptr 接管
}

// ============================================================================
// 反射注册宏（对标 UE 的 UCLASS / UPROPERTY）
// ============================================================================

// ME_BEGIN_CLASS：开启类的反射注册（函数体未闭合，ME_END_CLASS 负责闭合）
#define ME_BEGIN_CLASS(ClassName)                                                       \
    static const ClassDesc& GetClassDesc_Static() {                                     \
        static ClassDesc desc;                                                          \
        static bool initialized = false;                                                \
        if (!initialized) {                                                             \
            initialized = true;                                                         \
            desc.type_name = #ClassName;                                                \
            desc.display_name = #ClassName;                                              \
            desc.category = "Misc";

#define ME_DISPLAY_NAME(name) desc.display_name = (name);
#define ME_CATEGORY(name)     desc.category = (name);
#define ME_CATEGORY_COLOR(hex) desc.category_color = (hex);

// ME_FIELD：注册一个字段（对标 UPROPERTY 宏 + UHT 生成 FProperty 子类）
#define ME_FIELD(ClassName, FieldName, DefaultVal)                                      \
    {                                                                                   \
        desc.fields.emplace_back(                                                       \
            MakeProperty<std::decay_t<decltype(ClassName::FieldName)>>(                \
                #FieldName,                                                             \
                offsetof(ClassName, FieldName),                                         \
                (DefaultVal)));                                                         \
    }

// ME_END_CLASS：结束注册，重写 GetClassDesc 虚函数
#define ME_END_CLASS(ClassName)                                                         \
        }                                                                               \
        return desc;                                                                    \
    }                                                                                   \
    const ClassDesc* GetClassDesc() const override {                                    \
        return &GetClassDesc_Static();                                                  \
    }
```

**讲解**：
- **`MakeProperty<T>`**：编译期（`if constexpr`）把 C++ 类型 T 映射到对应的 Property 子类。**对标 UE 的 UHT**——UHT 扫描 UPROPERTY 宏时根据 C++ 类型生成 `new FFloatProperty()` 等代码，我们用 `if constexpr` 在 C++ 编译期做同样的事。返回裸指针，由 `ClassDesc` 的 `unique_ptr` 接管生命周期。
- **默认值走 `prop->toJson` 序列化**：`Vec3(1,0,0)` 这类用户类型不能直接赋给 `nlohmann::json`，所以 `MakeProperty` 收下 C++ 类型的默认值后，用 Property 自己的 `toJson` 把它转成 json 存进 `prop->defaultValue`——和旧版 `Accessor::toJson` 转换默认值是同一个思路，现在走虚函数。
- **`ME_FIELD` 宏**：调用 `MakeProperty<T>`，传入字段名（`#FieldName` 字符串化）、偏移（`offsetof`）、默认值。**用户接口和旧版完全一样**（`ME_FIELD(TestExpr, scale, 1.0f)`），内部从"赋函数指针"变成了"工厂创建子类"。
- **`static_assert`**：不支持的类型编译期报错（和旧版一样）。

### 1.5 PropertyCustomizer 注册表（对标 `IPropertyTypeCustomization`）

**文件：`src/Reflection/Public/PropertyCustomizer.h`**

这是**替代旧版 `switch(FieldType)` 的方案**——UI 层为每种 Property 类型注册一个"属性编辑器创建器"，不用枚举判断。

```cpp
#pragma once
#include "Reflection/Public/Property.h"
#include <functional>
#include <map>
#include <typeindex>

// 前向声明：属性编辑器接口（课13 PropertyPanel 实现）
class QWidget;

// ============================================================================
// PropertyCustomizerRegistry —— UI 层的类型→编辑器注册表
// ============================================================================
// 对标 UE 的 IPropertyTypeCustomization + FDetailPropertyNode：
// UE 的属性面板（Details Panel）为每种 FProperty 类型注册一个 Customization，
// 查表创建对应的 UI 控件（不用 switch 枚举）。
//
// 我们用 std::type_index（RTTI 类型标识）作为 key——
// 对应 Property 子类的 typeid，运行期查表。
class PropertyCustomizerRegistry {
public:
    using EditorCreatorFn = std::function<QWidget*(Property*)>;

    // 注册某种 Property 类型对应的编辑器创建器
    template <typename PropType>
    void Register(EditorCreatorFn creator) {
        creators_[std::type_index(typeid(PropType))] = std::move(creator);
    }

    // 为某个 Property 创建对应的编辑器（查表，不用 switch）
    QWidget* CreateEditor(Property* prop) const {
        auto it = creators_.find(std::type_index(typeid(*prop)));
        if (it != creators_.end()) {
            return it->second(prop);
        }
        return nullptr;  // 未注册的类型返回 nullptr
    }

private:
    std::map<std::type_index, EditorCreatorFn> creators_;
};
```

**UI 层（课13 PropertyPanel）怎么用**：

```cpp
// 课13：初始化时注册每种类型的编辑器
PropertyCustomizerRegistry customizerRegistry;

customizerRegistry.Register<FloatProperty>([](Property* prop) -> QWidget* {
    auto* spin = new QDoubleSpinBox;
    spin->setRange(-10000.0, 10000.0);
    spin->setDecimals(3);
    // prop->defaultValue 初始值...
    return spin;
});

customizerRegistry.Register<BoolProperty>([](Property* prop) -> QWidget* {
    auto* check = new QCheckBox;
    return check;
});

customizerRegistry.Register<StringProperty>([](Property* prop) -> QWidget* {
    auto* edit = new QLineEdit;
    return edit;
});

// customizerRegistry.Register<Vec3Property>(...);  // 向量编辑器
// ... 每种类型注册一次

// 使用：不用 switch(FieldType)，直接查表
for (auto& prop : classDesc.fields) {
    QWidget* editor = customizerRegistry.CreateEditor(prop.get());
    if (editor) formLayout->addRow(prop->name, editor);
}
```

**讲解**：
- **`std::type_index(typeid(*prop))`**：运行期获取 Property 子类的类型标识（RTTI），作为注册表的 key 查找。**不用枚举，不用 dynamic_cast**。
- **注册一次，到处用**：初始化时注册每种类型，之后 `CreateEditor(prop)` 自动查表分发。**对标 UE 的属性面板注册 Customization**。
- **分层干净**：`PropertyCustomizer.h` 在反射层定义接口，UI 层（课13）注册具体实现（lambda 里 `new QDoubleSpinBox`）。反射层不依赖 Qt（只用 `QWidget*` 前向声明）。

---

## 第二部分：ClassDesc（对标 `UClass`）

**文件：`src/Reflection/Public/ClassDesc.h`**

```cpp
#pragma once
#include "Reflection/Public/Property.h"
#include <vector>
#include <string>
#include <memory>

// ============================================================================
// ClassDesc —— 类描述符，持有该类所有 Property（对标 UE 的 UClass）
// ============================================================================
struct ClassDesc {
    std::string type_name;        // 类名标识，如 "TestExpr"
    std::string display_name;     // UI 显示名，如 "Test Expr"
    std::string category;         // 分类，如 "Demo"
    std::string category_color;   // UI 颜色，如 "#FF8800"

    // 字段表——持有 unique_ptr<Property>（基类指针，多态）
    // 对标 UClass 的 TArray<FProperty*>
    std::vector<std::unique_ptr<Property>> fields;

    // 按名字查找字段
    const Property* find(const std::string& field_name) const {
        for (const auto& f : fields) {
            if (f->name == field_name) return f.get();
        }
        return nullptr;
    }
};
```

**讲解**：
- **`std::vector<std::unique_ptr<Property>>`**：字段表。`unique_ptr` 管理生命周期（自动 delete），`Property*` 基类指针实现多态。对标 UE `UClass` 的 `TArray<FProperty*>`。
- **`find`**：按名字查找字段（返回 `const Property*`，调用方通过虚函数读写——虚函数都是 const，所以 const 指针够用）。

---

## 第三部分：Expression 基类（行为层）

**文件：`src/Expression/Public/Expression.h`**

```cpp
#pragma once
#include "Reflection/Public/ClassDesc.h"
#include "MaterialGraph/Public/Types.h"
#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

class MaterialCompiler;
class Node;

struct ExpressionPinDesc {
    std::string name;
    EValueType type;
    std::string default_value;
};

class Expression {
public:
    virtual ~Expression() = default;

    // === 反射入口（纯虚：子类必须通过 ME_BEGIN_CLASS 宏重写）===
    virtual const ClassDesc* GetClassDesc() const = 0;

    // === 引脚布局 ===
    virtual std::vector<ExpressionPinDesc> GetInputPins() const = 0;
    virtual std::vector<ExpressionPinDesc> GetOutputPins() const = 0;

    // === 编译（课6+ 实现）===
    virtual std::vector<int32_t> Compile(MaterialCompiler* compiler, Node* ownerNode) const = 0;

    // === 参数读写（非虚，纯反射实现，子类不碰）===
    // 遍历 ClassDesc::fields，调 Property::toJson/fromJson
    std::vector<const Property*> GetParameters() const;
    void SetParameter(const std::string& name, const nlohmann::json& value);
    nlohmann::json GetParameter(const std::string& name) const;
};
```

**文件：`src/Expression/Private/Expression.cpp`**

```cpp
#include "Expression/Public/Expression.h"

// GetParameters：返回字段列表（Property 指针数组）
std::vector<const Property*> Expression::GetParameters() const {
    const ClassDesc* desc = GetClassDesc();
    if (!desc) return {};
    std::vector<const Property*> result;
    for (const auto& field : desc->fields) {
        result.push_back(field.get());
    }
    return result;
}

// SetParameter：按名字找到 Property，调 fromJson 写入字段值
//   this 是 Expression*（非 const 方法）→ 隐式转 void* 给 fromJson
//   field 是 const Property*，fromJson 是 const 方法 → 直接调，无需 const_cast
void Expression::SetParameter(const std::string& name, const nlohmann::json& value) {
    const ClassDesc* desc = GetClassDesc();
    if (!desc) return;
    const Property* field = desc->find(name);
    if (!field) return;
    field->fromJson(this, field->offset, value);
}

// GetParameter：按名字找到 Property，调 toJson 读出字段值
//   this 是 const Expression*（const 方法）→ 隐式转 const void* 给 toJson
//   无需任何 const_cast
nlohmann::json Expression::GetParameter(const std::string& name) const {
    const ClassDesc* desc = GetClassDesc();
    if (!desc) return {};
    const Property* field = desc->find(name);
    if (!field) return {};
    nlohmann::json out;
    field->toJson(this, field->offset, out);
    return out;
}
```

**讲解**：
- `GetParameters`/`SetParameter`/`GetParameter` 和旧版**外部接口完全一样**——调用方（UI/编译器/序列化）的代码不用改。
- 内部从"走 `FieldDesc` 函数指针 + `Accessor`"变成"走 `Property` 虚函数"——但调用方看不到差异。
- **因为 `toJson` 用 `const void*`、`fromJson` 用 `void*` 且都是 const 方法**，这里一处 `const_cast` 都不用，const-correctness 自然成立。

---

## 第四部分：完整示例 — TestExpr

```cpp
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "MaterialGraph/Public/Types.h"
#include "Core/Public/MathTypes.h"
#include "Core/Public/Logger.h"
#include <cstdio>

// ============================================================
// 测试用 Expression 子类
// ============================================================
class TestExpr : public Expression {
public:
    float       scale   = 1.0f;
    Vec3        color   = Vec3(1.0f, 0.0f, 0.0f);
    bool        enabled = true;
    std::string label   = "default";

    ME_BEGIN_CLASS(TestExpr)
        ME_DISPLAY_NAME("Test Expr")
        ME_CATEGORY("Test")
        ME_CATEGORY_COLOR("#FF8800")
        ME_FIELD(TestExpr, scale,   1.0f)
        ME_FIELD(TestExpr, color,   Vec3(1.0f, 0.0f, 0.0f))
        ME_FIELD(TestExpr, enabled, true)
        ME_FIELD(TestExpr, label,   std::string("default"))
    ME_END_CLASS(TestExpr)

    std::vector<ExpressionPinDesc> GetInputPins()  const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override { return {}; }
    std::vector<int32_t> Compile(MaterialCompiler*, Node*) const override { return {}; }
};

// ============================================================
// 测试代码
// ============================================================
int main() {
    std::printf("============================================================\n");
    std::printf("  课5 反射系统测试（UE 风格：Property 继承）\n");
    std::printf("============================================================\n");

    TestExpr obj;

    // 1. 元数据查询
    const ClassDesc* desc = obj.GetClassDesc();
    std::printf("type_name: %s\n", desc->type_name.c_str());
    std::printf("display_name: %s\n", desc->display_name.c_str());
    std::printf("category: %s\n", desc->category.c_str());

    // 2. 字段枚举
    auto params = obj.GetParameters();
    std::printf("字段数量: %zu\n", params.size());
    for (const auto* p : params) {
        std::printf("  - %s (offset=%zu)\n", p->name.c_str(), p->offset);
    }

    // 3. 读默认值
    auto scale_json = obj.GetParameter("scale");
    std::printf("scale 默认: %f\n", scale_json.get<float>());

    // 4. 写入 → 回读
    obj.SetParameter("scale", 3.14f);
    std::printf("scale 改为: %f\n", obj.scale);
    std::printf("GetParameter 回读: %f\n", obj.GetParameter("scale").get<float>());

    // 5. 未知字段安全
    auto val = obj.GetParameter("nonexistent");
    std::printf("未知字段: %s\n", val.is_null() ? "null (安全)" : "有值?");

    std::printf("============================================================\n");
    return 0;
}
```

---

## UE5 参考（相对 `Engine/` 路径）

| 我们的概念 | UE 对应 | 位置 |
|-----------|---------|------|
| `Property` 基类 | `FProperty`（旧名 `UProperty`）| `Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h` |
| `FloatProperty` 等子类 | `FFloatProperty` / `FBoolProperty` 等 | 同上 |
| `ClassDesc`（持有 `vector<unique_ptr<Property>>`）| `UClass`（持有 `TArray<FProperty*>`）| `Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h` |
| `MakeProperty<T>` 工厂 | UHT 代码生成（扫描 UPROPERTY → 生成 FProperty 子类）| UHT 是独立工具 |
| `ME_FIELD` 宏 | `UPROPERTY()` 宏 | `Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h` |
| `ME_BEGIN_CLASS/END_CLASS` | `UCLASS()` + UHT 生成 `.generated.h` | 同上 |
| `PropertyCustomizerRegistry` | `IPropertyTypeCustomization` + Details Panel 注册 | `Engine/Source/Editor/PropertyEditor/Public/IPropertyTypeCustomization.h` |
| `Property::toJson/fromJson` | `FProperty::ExportText/ImportText` | `UnrealType.h` |

---

## 完成标志

- [ ] Property 基类 + 7 个子类（Float/Int/Bool/String/Vec2/3/4）编译通过
- [ ] MakeProperty<T> 工厂 + ME_FIELD 宏工作
- [ ] ClassDesc 持有 `vector<unique_ptr<Property>>`，find 按名查找
- [ ] Expression::Get/Set/GetParameter 走 Property 虚函数（**全程无 const_cast**）
- [ ] PropertyCustomizerRegistry 注册表工作（课13 验证）
- [ ] TestExpr 反射测试通过（元数据查询/字段枚举/读写往返/未知字段安全）
- [ ] 理解对照 UE：Property↔FProperty、ClassDesc↔UClass、MakeProperty↔UHT、注册表↔IPropertyTypeCustomization
