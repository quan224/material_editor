#include "Expression/Public/Expression.h"

// 参数读写的默认实现 —— 全部走反射，子类不需要也不允许重写。
// 反射路径：GetClassDesc() → FindItem 找 Property → 调 Property::ToJson/FromJson

std::vector<const Property*> Expression::GetParameters()const{
    const ClassDesc* class_desc = GetClassDesc();
    if (!class_desc) return {};
    std::vector<const Property*> result;
    for (const auto& p : class_desc->fields){
        result.emplace_back(p.get());
    }
    return result;
}

void Expression::SetParameter(const std::string& name, const nlohmann::json& value){
    const ClassDesc* class_desc = GetClassDesc();
    if (!class_desc) return;
    const Property* field_desc = class_desc->FindItem(name);
    if (!field_desc) return;
    field_desc->FromJson(this, field_desc->offset, value);
}

nlohmann::json Expression::GetParameter(const std::string& name)const{
    const ClassDesc* class_desc = GetClassDesc();
    if (!class_desc) return {};                              // 返回 null json
    const Property* field_desc = class_desc->FindItem(name);
    if (!field_desc)   return {};
    nlohmann::json out;
    field_desc->ToJson(static_cast<void*>(const_cast<Expression*>(this)), field_desc->offset, out);
    return out;
}
