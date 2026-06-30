#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "Core/Public/MathTypes.h"
#include "Core/Public/Singleton.h"

namespace reflection{
    // 支持的字段类型
    enum class FieldType{
        Float,
        Int,
        Bool,
        String,
        Float2,
        Float3,
        Float4
    };

    // 字段元信息 — 描述"一个属性叫什么、是什么类型、在内存哪里、怎么读写"
    struct FieldDesc
    {
        std::string name;
        FieldType type;
        std::size_t offset;
        // 类型擦除读写函数
        void (*toJson)(void *obj, std::size_t offset, nlohmann::json& out);
        void (*fromJson)(void *obj, std::size_t offset, const nlohmann::json& in);
        nlohmann::json default_value;
    };

    template<typename T>
    struct Accessor{};

    template<>
    struct Accessor<float>{
        static void toJson(void* obj, std::size_t offset, nlohmann::json& out){
            float* ptr = reinterpret_cast<float*>(static_cast<char*>(obj)+offset);
            out = *ptr;
        }
        static void fromJson(void* obj, std::size_t offset, const nlohmann::json& in){
            if(in.is_number()){
                float* ptr = reinterpret_cast<float*>(static_cast<char*>(obj)+offset);
                *ptr = in.get<float>();
            }
        }
    };

    template<>
    struct Accessor<int32_t>{
      static void  toJson(void* obj, std::size_t offset, nlohmann::json& out){
        int32_t* ptr = reinterpret_cast<int32_t*>((static_cast<char*>(obj)+offset) ) ;
        out = *ptr;
      }

      static void fromJson(void* obj, std::size_t offset, const nlohmann::json& in){
        if (in.is_number_integer()){
            int32_t* ptr = reinterpret_cast<int32_t*>((static_cast<char*>(obj)+offset) ) ;
            *ptr = in.get<int32_t>();
        }
      }
    };

    template<>
    struct Accessor<bool>{
      static void  toJson(void* obj, std::size_t offset, nlohmann::json& out){
        bool* ptr = reinterpret_cast<bool*>((static_cast<char*>(obj)+offset) ) ;
        out = *ptr;
      }

      static void fromJson(void* obj, std::size_t offset, const nlohmann::json& in){
        if (in.is_boolean()){
            bool* ptr = reinterpret_cast<bool*>((static_cast<char*>(obj)+offset) ) ;
            *ptr = in.get<bool>();
        }
      }
    };

    template<>
    struct Accessor<std::string>{
      static void  toJson(void* obj, std::size_t offset, nlohmann::json& out){
        std::string* ptr = reinterpret_cast<std::string*>((static_cast<char*>(obj)+offset) ) ;
        out = *ptr;
      }

      static void fromJson(void* obj, std::size_t offset, const nlohmann::json& in){
        if (in.is_string()){
            std::string* ptr = reinterpret_cast<std::string*>((static_cast<char*>(obj)+offset) ) ;
            *ptr = in.get<std::string>();
        }
      }
    };


    template<>
    struct Accessor<Vec2>{
        static void toJson(void* obj, size_t offset, nlohmann::json& out){
            Vec2* ptr = reinterpret_cast<Vec2*>(static_cast<char*>(obj)+offset);
            out = nlohmann::json::array({ ptr->x, ptr->y});
        }

        static void fromJson(void* obj, size_t offset, const nlohmann::json& in){
            if(in.is_array()&&in.size()>=2){
               Vec2* ptr = reinterpret_cast<Vec2*>(static_cast<char*>(obj)+offset);
               ptr->x = in[0].get<float>();
               ptr->y = in[1].get<float>();
            }
        }
    };

    template<>
    struct Accessor<Vec3>{
        static void toJson(void* obj, size_t offset, nlohmann::json& out){
            Vec3* ptr = reinterpret_cast<Vec3*>(static_cast<char*>(obj)+offset);
            out = nlohmann::json::array({ ptr->x, ptr->y, ptr->z});
        }

        static void fromJson(void* obj, size_t offset, const nlohmann::json& in){
            if(in.is_array()&&in.size()>=3){
               Vec3* ptr = reinterpret_cast<Vec3*>(static_cast<char*>(obj)+offset);
               ptr->x = in[0].get<float>();
               ptr->y = in[1].get<float>();
               ptr->z = in[2].get<float>();
            }
        }
    };

    template<>
    struct Accessor<Vec4>{
        static void toJson(void* obj, size_t offset, nlohmann::json& out){
            Vec4* ptr = reinterpret_cast<Vec4*>(static_cast<char*>(obj)+offset);
            out = nlohmann::json::array({ ptr->x, ptr->y, ptr->z, ptr->w});
        }

        static void fromJson(void* obj, size_t offset, const nlohmann::json& in){
            if(in.is_array()&&in.size()>=4){
               Vec4* ptr = reinterpret_cast<Vec4*>(static_cast<char*>(obj)+offset);
               ptr->x = in[0].get<float>();
               ptr->y = in[1].get<float>();
               ptr->z = in[2].get<float>();
               ptr->w = in[3].get<float>();
            }
        }
    };


    // 类元信息-一个类的所有字段描述符
    // 承载所有元数据：身份信息（类型/显示名/分类）+ UI 信息（颜色）+ 字段表
    struct ClassDesc{
        std::string type_name;        // 类名标识，如 "ExprAdd"
        std::string display_name;     // UI 显示名，如 "Add"
        std::string category;         // 分类，如 "Math"
        std::string category_color;   // UI 颜色，如 "#4CA7E8"
        std::vector<FieldDesc> fields;

        // 按名字查找字段
        const FieldDesc* find(const std::string& field_name) const{
            for(const auto& field:fields){
                if(field.name==field_name) return &field;
            }
            return nullptr;
        }

    };

    class Registry:public Singleton<Registry>{

        friend Singleton<Registry>;
    public:
        void RegisterClass(ClassDesc desc){
            classes_[desc.type_name] = desc;
        }

        const ClassDesc* Find(const std::string& type_name){
            auto it = classes_.find(type_name);
            return  it != classes_.end()? &(it->second) : nullptr;
        }

        std::vector<std::string> GetAllTypeNames() const{
            std::vector<std::string> all_names;
            for (const auto& [name, class_desc]:classes_){
                all_names.push_back(name);
            }
            return all_names;
        }



    private:
        Registry()=default;
        std::unordered_map<std::string, ClassDesc> classes_;
    };


}