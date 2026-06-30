#pragma once
#include "Reflection/Public/Reflection.h"
#include <nlohmann/json.hpp>
#include <type_traits>
#include <cstddef>

// 辅助函数：根据C++类型选择FieldType
// 用 if constexpr 在编译器决定，不满足的分支不参与编译

template<typename T>

constexpr reflection::FieldType GetFieldType(){
    using DT = std::decay_t<T>;
    if constexpr (std::is_same_v<DT, float>) return reflection::FieldType::Float;
    else if constexpr(std::is_same_v<DT,int>) return reflection::FieldType::Int;
    else if constexpr(std::is_same_v<DT,bool>) return reflection::FieldType::Bool;
    else if constexpr(std::is_same_v<DT,std::string>) return reflection::FieldType::String;
    else if constexpr(std::is_same_v<DT,Vec2>) return reflection::FieldType::Float2;
    else if constexpr(std::is_same_v<DT,Vec3>) return reflection::FieldType::Float3;
    else if constexpr(std::is_same_v<DT,Vec4>) return reflection::FieldType::Float4;
    else static_assert(sizeof(T)==0, "Unsupported field type");

}

// 开启类的反射注册(函数体未闭合，ME_END_CLASS负责闭合)
#define ME_BEGIN_CLASS(ClassName)\
    static const reflection::ClassDesc& GetClassDesc_Static(){\
        static reflection::ClassDesc desc;\
        static bool initialized = false;\
        if (!initialized){\
            initialized = true;\
            desc.type_name = #ClassName;\
            desc.display_name = #ClassName;\
            desc.category="Misc";

#define ME_DISPLAY_NAME(name) desc.display_name = (name);
#define ME_CATEGORY(name) desc.category = (name);
#define ME_CATEGORY_COLOR(hex) desc.category_color = (hex);

// 注册一个字段
// 直接赋值静态函数指针（不走 lambda，避免冗余间接调用）
// DefaultValue 走 Accessor 序列化（Vec3 等用户类型 nlohmann::json 不认识）
#define ME_FIELD(ClassName, FieldName, DefaultValue)\
    {\
        using FieldType_t = std::decay_t<decltype(ClassName::FieldName)>;\
        reflection::FieldDesc field;\
        field.name = #FieldName;\
        field.type = GetFieldType<FieldType_t>();\
        field.offset = offsetof(ClassName, FieldName);\
        FieldType_t _tmp_default = (DefaultValue);\
        reflection::Accessor<FieldType_t>::toJson(&_tmp_default, 0, field.default_value);\
        field.toJson   = &reflection::Accessor<FieldType_t>::toJson;\
        field.fromJson = &reflection::Accessor<FieldType_t>::fromJson;\
        desc.fields.push_back(std::move(field));\
    }\

// 结束注册
#define ME_END_CLASS(ClassName)\
    }\
    return desc;\
}\
    const reflection::ClassDesc* GetClassDesc()const override{\
        return &GetClassDesc_Static();\
}\
        
    