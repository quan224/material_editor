#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cassert>
#include "Core/Public/StringBuilder.h"

namespace shader{

enum class EValueComponentType : uint8_t
{
	Void,
	Float,
	Double,
	Int,
	Bool,
	// May be any numeric type, stored internally as 'double' within FValue
    // 可能是 float, double, bool, int， double兜底 
	Numeric,

    // 当作length来用 EValueComponentType::Num == 6
	Num,
};


enum class EValueType : uint8_t{
	Void,

	Float1,
	Float2,
	Float3,
	Float4,

	Double1,
	Double2,
	Double3,
	Double4,

	Int1,
	Int2,
	Int3,
	Int4,

	Bool1,
	Bool2,
	Bool3,
	Bool4,

	// Any scalar/vector type
	Numeric1,
	Numeric2,
	Numeric3,
	Numeric4,

	// float4x4
	Float4x4,

	// Both of these are double4x4 on CPU
	// On GPU, they map to FDFMatrix and FDFInverseMatrix
	Double4x4,
	DoubleInverse4x4,

	// Any matrix type
	Numeric4x4,

	Struct,
	Object,
	Any,
    // 当作length来用 EValueComponentType::Num == 28
	Num,
};

struct FStructType;   // 前置声明，FType 里用指针引用它

struct FType{

    FType():value_type(EValueType::Void){}
    FType(EValueType in_value_type):value_type(in_value_type){
        assert(value_type != EValueType::Struct && value_type != EValueType::Object);
    }
    FType(const FStructType* in_struct_type): struct_type(in_struct_type), value_type(in_struct_type ? EValueType::Struct:EValueType::Void){}
    FType(std::string in_object_name): object_name(in_object_name), value_type(!in_object_name.empty()? EValueType::Object:EValueType::Void){}


    const char* GetName() const;
    FType GetDerivativeType() const;
    int32_t GetNumComponents() const;

    bool IsVoid() const {return value_type == EValueType::Void;}
    bool IsStruct() const {return value_type == EValueType::Struct;}
    bool IsObject() const {return value_type == EValueType::Object;}
    bool IsAny() const {return value_type == EValueType::Any;}


    // value_type == EValueType::Struct 时生效，指向真正的结构类型
    const FStructType* struct_type = nullptr;
    // value_type == EValueType::Object 时生效, Texture 这种类型没有对应的引擎测对象，
    std::string object_name;
    // 一直会用的值，指向具体类型
    EValueType value_type;
};

struct FStructField{
    const char* name;
    FType type;
    int32_t component_index;
    int32_t flat_field_index;
    int32_t GetNumComponents() const {return type.GetNumComponents();}
};

struct FStructType{
    uint64_t hash;
    const char* name;
    const FStructType* derivative_type = nullptr;

    std::vector<FStructField> fields;
    std::vector<EValueComponentType> component_types;
    std::vector<EValueType> flat_field_types;

    bool IsExternal() const {return fields.empty();}
    int32_t GetNumComponents() const {return component_types.size();}
    const FStructField* FindFieldByName(const char* in_name) const;
};


union FValueComponent{
    FValueComponent():packed(0u){}
    FValueComponent(double in_double):packed(0u){_double=in_double;}
    FValueComponent(float in_float):packed(0u){_float=in_float;}
    FValueComponent(int32_t in_int):packed(0u){_int=in_int;}
    FValueComponent(bool in_bool):packed(0u){ in_bool?_bool=1u:_bool=0u; }

    // 转回bool用
    bool AsBool(){return _bool != 0u; }

    const char* ToString(EValueComponentType type, FStringBuilderBase& out_string) const;

    uint64_t packed;
    double _double;
    float _float;
    int32_t _int;
    uint8_t _bool;
};
static_assert(sizeof(FValueComponent) == sizeof(uint64_t), "bad packing");


struct FValue{
    FValue(){};

    FType type;
    std::vector<FValueComponent> componet;

};






}



//   1. EValueComponentType ──MakeValueType(分量类型,个数)──▶EValueType
//                                                       │
//   2. FType(EValueType)                          ◀──────┘
//      （或 FType(FStructType*) / FType(FName) 走结构/对象分支）
//           │
//   3. FValue(FType)        ←「按图纸铸实物」，开出分量数组
//           │                  塞值时经 FValueComponent(union) 存进数组
//           ▼
//   4. 值运算函数（Add/F_Schlick 那类）读 FType.Type 决定算法，
//      产出新的 FValue          ←纯函数：编辑器解释执行
//      或 InPlace 版直接改 Component 数组 ←preshader VM 执行字节码
//           │
//           ├─▶operator== / GetTypeHash   →当 map key（去重/缓存键）
//           ├─▶AsLinearColor/AsFloat...   →转给渲染用的类型
//           ├─▶ToString(Description/HLSL) →打日志 / 生成 HLSL 字面量
//           └─▶AsMemoryImage/FromMemoryImage →DDC 磁盘往返


//   枚举层（EValueType / EValueComponentType）   ←最底，谁都不依赖
//        ↑
//   描述层（两个 Description + 值域 Bounds）      ←查枚举的属性
//        ↑
//   包装层（FType）                              ←持有枚举 + 结构/对象分支
//        ↑
//   值层（FValueComponent →FValue）              ←持有 FType + 分量数组
//        ↑
//   运算层（纯函数 + InPlace 族）                  ←消费 FValue，产出 FValue
//        ↑
//   调用方（VM / 编译器 / DDC）                   ←全在文件外

//   单向向上，无环——每一层只依赖下面的层，这就是它能被整个shader 子系统共用的原因。你抄进 Shader/ShaderTypes.h
//   时保持同构即可：已搬的 FValueComponent 在值层、FType 在包装层，位置正确；运算层（InPlace 族）等课 20 的 VM 来调用。