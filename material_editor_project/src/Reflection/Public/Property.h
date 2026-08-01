#pragma once
#include <string>
#include <cstddef>
#include <memory>
#include <nlohmann/json.hpp>
#include "Core/Public/MathTypes.h"

// Property 基类 -- 所有字段类型的统一接口（对标UE的FProperty）
// 对标 UE：
//   FProperty（基类）
//     ├── ImportText / ExportText（序列化，对应我们的 toJson/fromJson）
//     ├── Serialize（二进制序列化，我们用 JSON 代替）
//     └── 各种子类（FFloatProperty/FBoolProperty/...）

class Property{
public:
    virtual ~Property() = default;

    virtual void ToJson(const void* obj, size_t offset, nlohmann::json& out) const = 0;
    virtual void FromJson(void* obj,size_t offset, const nlohmann::json& in) const = 0;

    std::string name;
    std::size_t offset = 0;
    nlohmann::json default_value;

    Property& SetName(const std::string& n){name=n; return *this;}
    Property& SetOffset(const std::size_t o){offset=o; return *this;}
    Property& SetDefaultValue(const nlohmann::json& d){default_value=d; return *this;}
};

// ============================================================================
// FloatProperty —— float 字段（对标 FFloatProperty）
// ============================================================================

class FloatProperty:public Property{
public:
    void ToJson(const void* obj,size_t offset, nlohmann::json& out)const override{
        const float* ptr = reinterpret_cast<const float*>(static_cast<const char*>(obj)+offset);
        out = *ptr;
    }

    void FromJson(void* obj,size_t offset, const nlohmann::json& in)const override{
        if(in.is_number()){
            float* ptr = reinterpret_cast<float*>(static_cast<char*>(obj)+offset);
            *ptr = in.get<float>();
        }
    }
};

// ============================================================================
// IntProperty —— int 字段（对标 FIntProperty）
// ============================================================================

class IntProperty:public Property{
public:
    void ToJson(const void* obj,size_t offset, nlohmann::json& out)const override{
        const int32_t* ptr = reinterpret_cast<const int32_t*>(static_cast<const char*>(obj)+offset);
        out = *ptr;
    }

    void FromJson(void* obj,size_t offset, const nlohmann::json& in)const override{
        if(in.is_number_integer()){
            int32_t* ptr = reinterpret_cast<int32_t*>(static_cast<char*>(obj)+offset);
            *ptr = in.get<int32_t>();
        }
    }
};

// ============================================================================
// BoolProperty —— bool 字段（对标 FBoolProperty）
// ============================================================================

class BoolProperty:public Property{
public:
    void ToJson(const void* obj,size_t offset, nlohmann::json& out)const override{
        const bool* ptr = reinterpret_cast<const bool*>(static_cast<const char*>(obj)+offset);
        out = *ptr;
    }

    void FromJson(void* obj,size_t offset, const nlohmann::json& in)const override{
        if(in.is_boolean()){
            bool* ptr = reinterpret_cast<bool*>(static_cast<char*>(obj)+offset);
            *ptr = in.get<bool>();
        }
    }
};

// ============================================================================
// StringProperty —— string 字段（对标 FStringProperty）
// ============================================================================

class StringProperty:public Property{
public:
    void ToJson(const void* obj,size_t offset, nlohmann::json& out)const override{
        const std::string* ptr = reinterpret_cast<const std::string*>(static_cast<const char*>(obj)+offset);
        out = *ptr;
    }

    void FromJson(void* obj,size_t offset, const nlohmann::json& in)const override{
        if(in.is_string()){
            std::string* ptr = reinterpret_cast<std::string*>(static_cast<char*>(obj)+offset);
            *ptr = in.get<std::string>();
        }
    }
};

// ============================================================================
// Vec2Property —— Vec2 字段（对标 FVec2Property）
// ============================================================================

class Vec2Property:public Property{
public:
    void ToJson(const void* obj,size_t offset, nlohmann::json& out)const override{
        const Vec2* ptr = reinterpret_cast<const Vec2*>(static_cast<const char*>(obj)+offset);
        out = nlohmann::json::array({ptr->x, ptr->y});
    }

    void FromJson(void* obj,size_t offset, const nlohmann::json& in)const override{
        if(in.is_array()&&in.size()>=2){
            Vec2* ptr = reinterpret_cast<Vec2*>(static_cast<char*>(obj)+offset);
            ptr->x = in[0].get<float>();
            ptr->y = in[1].get<float>();
        }
    }
};

// ============================================================================
// Vec3Property —— Vec3 字段（对标 FVec3Property）
// ============================================================================

class Vec3Property:public Property{
public:
    void ToJson(const void* obj,size_t offset, nlohmann::json& out)const override{
        const Vec3* ptr = reinterpret_cast<const Vec3*>(static_cast<const char*>(obj)+offset);
        out = nlohmann::json::array({ptr->x, ptr->y, ptr->z});
    }

    void FromJson(void* obj,size_t offset, const nlohmann::json& in)const override{
        if(in.is_array()&&in.size()>=3){
            Vec3* ptr = reinterpret_cast<Vec3*>(static_cast<char*>(obj)+offset);
            ptr->x = in[0].get<float>();
            ptr->y = in[1].get<float>();
            ptr->z = in[2].get<float>();
        }
    }
};

// ============================================================================
// Vec4Property —— Vec4 字段（对标 FVec4Property）
// ============================================================================

class Vec4Property:public Property{
public:
    void ToJson(const void* obj,size_t offset, nlohmann::json& out)const override{
        const Vec4* ptr = reinterpret_cast<const Vec4*>(static_cast<const char*>(obj)+offset);
        out = nlohmann::json::array({ptr->x, ptr->y, ptr->z, ptr->w});
    }

    void FromJson(void* obj,size_t offset, const nlohmann::json& in)const override{
        if(in.is_array()&&in.size()>=4){
            Vec4* ptr = reinterpret_cast<Vec4*>(static_cast<char*>(obj)+offset);
            ptr->x = in[0].get<float>();
            ptr->y = in[1].get<float>();
            ptr->z = in[2].get<float>();
            ptr->w = in[3].get<float>();
        }
    }
};