#pragma once
#include "Reflection/Public/Property.h"
#include <vector>
#include <string>
#include <memory>

// ============================================================================
// ClassDesc —— 类描述符，持有该类所有 Property（对标 UE 的 UClass）
// ============================================================================

struct ClassDesc{

    std::string type_name;
    std::string display_name;
    std::string category;
    std::string category_color;  // 如: "#FF8800"

    std::vector<std::unique_ptr<Property>> fields;

    // 按名字查找字段
    const Property* FindItem(const std::string& field_name)const{
        for(const auto& f:fields){
            if(f->name == field_name) return f.get();
        }
        return nullptr;
    }
};