#include "Expression/Public/Expression.h"

// 参数读写的默认实现 —— 全部走反射，子类不需要也不允许重写。
// 反射路径：GetClassDesc() → 找 FieldDesc → 调 Accessor::toJson/fromJson

std::vector<reflection::FieldDesc> Expression::GetParameters()const{
    const reflection::ClassDesc* class_desc = GetClassDesc();
    if (!class_desc) return {};
    return class_desc->fields;
}

void Expression::SetParameter(const std::string& name, const nlohmann::json& value){
    const reflection::ClassDesc* class_desc = GetClassDesc();
    if (!class_desc) return;
    const reflection::FieldDesc* field_desc = class_desc->find(name);
    if (!field_desc) return;
    field_desc->fromJson(static_cast<void*>(const_cast<Expression*>(this)), field_desc->offset, value);
}

nlohmann::json Expression::GetParameter(const std::string& name)const{
    const reflection::ClassDesc* class_desc = GetClassDesc();
    if (!class_desc) return {};                              // 返回 null json
    const reflection::FieldDesc* field_desc = class_desc->find(name);
    if (!field_desc)   return {};
    nlohmann::json out;
    field_desc->toJson(static_cast<void*>(const_cast<Expression*>(this)), field_desc->offset, out);
    return out;
}
