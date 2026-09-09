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
inline constexpr int32_t NumValueTypes = (int32_t)EValueType::Num;


struct FValueTypeDescription{
    const char* name;  // 类型名
    EValueType value_type;  // 类型枚举值
    EValueComponentType comp_type;  // 由什么分量构成 
    int8_t num_components;  // 几个分量
    int8_t component_size_in_bytes;  // 每个分量几字节
};


const FValueTypeDescription  GValueTypeDescriptions[]={
	{ {"void"),			EValueType::Void,		EValueComponentType::Void,		0, 0 },
	{ {"float"),		EValueType::Float1,		EValueComponentType::Float,		1, sizeof(float) },
	{ {"float2"),		EValueType::Float2,		EValueComponentType::Float,		2, sizeof(float) },
	{ {"float3"),		EValueType::Float3,		EValueComponentType::Float,		3, sizeof(float) },
	{ {"float4"),		EValueType::Float4,		EValueComponentType::Float,		4, sizeof(float) },
	{ {"FWSScalar"),	EValueType::Double1,	EValueComponentType::Double,	1, sizeof(double) },
	{ {"FWSVector2"),	EValueType::Double2,	EValueComponentType::Double,	2, sizeof(double) },
	{ {"FWSVector3"),	EValueType::Double3,	EValueComponentType::Double,	3, sizeof(double) },
	{ {"FWSVector4"),	EValueType::Double4,	EValueComponentType::Double,	4, sizeof(double) },
	{ {"int"),			EValueType::Int1,		EValueComponentType::Int,		1, sizeof(int32) },
	{ {"int2"),			EValueType::Int2,		EValueComponentType::Int,		2, sizeof(int32) },
	{ {"int3"),			EValueType::Int3,		EValueComponentType::Int,		3, sizeof(int32) },
	{ {"int4"),			EValueType::Int4,		EValueComponentType::Int,		4, sizeof(int32) },
	{ {"bool"),			EValueType::Bool1,		EValueComponentType::Bool,		1, 1 },
	{ {"bool2"),		EValueType::Bool2,		EValueComponentType::Bool,		2, 1 },
	{ {"bool3"),		EValueType::Bool3,		EValueComponentType::Bool,		3, 1 },
	{ {"bool4"),		EValueType::Bool4,		EValueComponentType::Bool,		4, 1 },
	{ {"Numeric1"),		EValueType::Numeric1,	EValueComponentType::Numeric,	1, sizeof(double) },
	{ {"Numeric2"),		EValueType::Numeric2,	EValueComponentType::Numeric,	2, sizeof(double) },
	{ {"Numeric3"),		EValueType::Numeric3,	EValueComponentType::Numeric,	3, sizeof(double) },
	{ {"Numeric4"),		EValueType::Numeric4,	EValueComponentType::Numeric,	4, sizeof(double) },
	{ {"float4x4"),		EValueType::Float4x4,	EValueComponentType::Float,		16, sizeof(float) },
	{ {"FWSMatrix"),	EValueType::Double4x4,	EValueComponentType::Double,	16, sizeof(double) },
	{ {"FWSInverseMatrix"), EValueType::DoubleInverse4x4, EValueComponentType::Double, 16, sizeof(double) },
	{ {"Numeric4x4"),	EValueType::Numeric4x4, EValueComponentType::Numeric,	16, sizeof(double) },
	{ {"struct"),		EValueType::Struct,		EValueComponentType::Void,		0, 0 },
	{ {"object"),		EValueType::Object,		EValueComponentType::Void,		0, 0 },
	{ {"Any"),			EValueType::Any,		EValueComponentType::Void,		0, 0 },
	{ {"<INVALID>"),	EValueType::Num,		EValueComponentType::Void,		0, 0 },
};

static_assert(sizeof(GValueTypeDescriptions)/sizeof(GValueTypeDescriptions[0]) == (NumValueTypes+1), "Missing entry from shader value description table");



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