#pragma once
#include "Reflection/Public/Property.h"
#include "Reflection/Public/ClassDesc.h"
#include <type_traits>
#include <cstddef>

// ============================================================================
// MakeProperty<T> —— 工厂函数：按 C++ 类型 T 创建对应的 Property 子类
// ============================================================================
// 对标 UE 的 UHT：根据 C++ 类型生成对应的 FProperty 子类。
// 用 if constexpr 在编译期做"类型→子类"映射，返回裸指针由 ClassDesc 的 unique_ptr 接管。
template<typename T>
Property* MakeProperty(const std::string& name, const size_t offset, const T& default_value){
    using DT = std::decay_t<T>;

    Property* p = nullptr;

    if constexpr (std::is_same_v<DT, float>)             p = new FloatProperty();
    else if constexpr (std::is_same_v<DT, int32_t>)      p = new IntProperty();
    else if constexpr (std::is_same_v<DT, bool>)         p = new BoolProperty();
    else if constexpr (std::is_same_v<DT, std::string>)  p = new StringProperty();
    else if constexpr (std::is_same_v<DT, Vec2>)         p = new Vec2Property();
    else if constexpr (std::is_same_v<DT, Vec3>)         p = new Vec3Property();
    else if constexpr (std::is_same_v<DT, Vec4>)         p = new Vec4Property();
    else static_assert(sizeof(T)==0, "Unsupported property type — add a Property subclass + MakeProperty branch");

    p->SetName(name);

    // 默认值序列化：default_value 是独立值，ToJson 的 offset 参数传 0（从其起始地址读）。
    nlohmann::json j_default;
    p->ToJson(&default_value, 0, j_default);
    p->SetDefaultValue(j_default);

    p->SetOffset(offset);   // 记录字段真实偏移，供运行期 Expression 读写用

    return p;
}

// ============================================================================
// 反射注册宏（对标 UE 的 UCLASS / UPROPERTY）
// ============================================================================

// ME_BEGIN_CLASS：开启类的反射注册（函数体未闭合，ME_END_CLASS 负责闭合）
#define ME_BEGIN_CLASS(ClassName)\
    static const ClassDesc& GetClassDesc_static(){\
        static ClassDesc desc;\
        static bool initialized = false;\
        if (!initialized){\
            initialized = true;\
            desc.type_name = #ClassName;\
            desc.display_name = #ClassName;\
            desc.category = "Misc";\

#define ME_DISPLAY_NAME(name) desc.display_name = (name);
#define ME_CATEGORY(name)     desc.category = (name);
#define ME_CATEGORY_COLOR(hex) desc.category_color = (hex);

// ME_FIELD：注册一个字段（对标 UPROPERTY + UHT 生成 FProperty 子类）
#define ME_FIELD(ClassName, FieldName, DefaultValue)\
            {\
                desc.fields.emplace_back(MakeProperty<std::decay_t<decltype(ClassName::FieldName)>>(\
                    #FieldName, offsetof(ClassName, FieldName), DefaultValue\
                ));\
            }\

// ME_END_CLASS：结束注册，重写 GetClassDesc 虚函数
#define ME_END_CLASS(ClassName)\
        }\
        return desc;\
    }\
    const ClassDesc* GetClassDesc() const override{\
        return &GetClassDesc_static();\
    }\
