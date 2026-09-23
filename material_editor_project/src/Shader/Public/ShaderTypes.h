#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <cassert>
#include "Core/Public/StringBuilder.h"
#include "Core/Public/MemStack.h"
#include "Core/Public/Hash.h"
#include "Core/Public/Logger.h"

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


const FValueTypeDescription  GValueTypeDescriptions[]=
{
	{"void",			EValueType::Void,		EValueComponentType::Void,		0, 0 },
	{"float",		EValueType::Float1,		EValueComponentType::Float,		1, sizeof(float) },
	{"float2",		EValueType::Float2,		EValueComponentType::Float,		2, sizeof(float) },
	{"float3",		EValueType::Float3,		EValueComponentType::Float,		3, sizeof(float) },
	{"float4",		EValueType::Float4,		EValueComponentType::Float,		4, sizeof(float) },
	{"FWSScalar",	EValueType::Double1,	EValueComponentType::Double,	1, sizeof(double) },
	{"FWSVector2",	EValueType::Double2,	EValueComponentType::Double,	2, sizeof(double) },
	{"FWSVector3",	EValueType::Double3,	EValueComponentType::Double,	3, sizeof(double) },
	{"FWSVector4",	EValueType::Double4,	EValueComponentType::Double,	4, sizeof(double) },
	{"int",			EValueType::Int1,		EValueComponentType::Int,		1, sizeof(int32_t) },
	{"int2",			EValueType::Int2,		EValueComponentType::Int,		2, sizeof(int32_t) },
	{"int3",			EValueType::Int3,		EValueComponentType::Int,		3, sizeof(int32_t) },
	{"int4",			EValueType::Int4,		EValueComponentType::Int,		4, sizeof(int32_t) },
	{"bool",			EValueType::Bool1,		EValueComponentType::Bool,		1, 1 },
	{"bool2",		EValueType::Bool2,		EValueComponentType::Bool,		2, 1 },
	{"bool3",		EValueType::Bool3,		EValueComponentType::Bool,		3, 1 },
	{"bool4",		EValueType::Bool4,		EValueComponentType::Bool,		4, 1 },
	{"Numeric1",		EValueType::Numeric1,	EValueComponentType::Numeric,	1, sizeof(double) },
	{"Numeric2",		EValueType::Numeric2,	EValueComponentType::Numeric,	2, sizeof(double) },
	{"Numeric3",		EValueType::Numeric3,	EValueComponentType::Numeric,	3, sizeof(double) },
	{"Numeric4",		EValueType::Numeric4,	EValueComponentType::Numeric,	4, sizeof(double) },
	{"float4x4",		EValueType::Float4x4,	EValueComponentType::Float,		16, sizeof(float) },
	{"FWSMatrix",	EValueType::Double4x4,	EValueComponentType::Double,	16, sizeof(double) },
	{"FWSInverseMatrix", EValueType::DoubleInverse4x4, EValueComponentType::Double, 16, sizeof(double) },
	{"Numeric4x4",	EValueType::Numeric4x4, EValueComponentType::Numeric,	16, sizeof(double) },
	{"struct",		EValueType::Struct,		EValueComponentType::Void,		0, 0 },
	{"object",		EValueType::Object,		EValueComponentType::Void,		0, 0 },
	{"Any",			EValueType::Any,		EValueComponentType::Void,		0, 0 },
	{"<INVALID>",	EValueType::Num,		EValueComponentType::Void,		0, 0 },
};

static_assert(sizeof(GValueTypeDescriptions)/sizeof(GValueTypeDescriptions[0]) == (NumValueTypes+1), "Missing entry from shader value description table");

const FValueTypeDescription& GetValueTypeDescription(EValueType t);
inline bool IsLWCType(EValueComponentType c_t) {return c_t == EValueComponentType::Double;}
inline bool IsLWCType(EValueType t) {return IsLWCType(GetValueTypeDescription(t).comp_type);}
inline bool IsNumericType(EValueComponentType t){return t!= EValueComponentType::Void;}
inline bool IsNumericType(EValueType t){return IsNumericType(GetValueTypeDescription(t).comp_type);}
inline bool IsGenericType(EValueComponentType c_t){return c_t == EValueComponentType::Numeric;}
inline bool IsGenericType(EValueType t){return t == EValueType::Any || IsGenericType(GetValueTypeDescription(t).comp_type);}
inline bool IsNumericScalarType(EValueType t){
	FValueTypeDescription type_desc = GetValueTypeDescription(t);
	return IsNumericType(type_desc.comp_type) && type_desc.num_components == 1;
}
inline bool IsNumericVectorType(EValueType t){
	FValueTypeDescription type_desc = GetValueTypeDescription(t);
	return IsNumericType(type_desc.comp_type) && type_desc.num_components <= 4;
}
inline bool IsNumericMatrixType(EValueType t){
	FValueTypeDescription type_desc = GetValueTypeDescription(t);
	return IsNumericType(type_desc.comp_type) && type_desc.num_components == 16;
}

inline EValueComponentType MakeNoneLWCType(EValueComponentType c_t){return c_t == EValueComponentType::Double? EValueComponentType::Float:c_t;}
EValueType MakeNoneLWCType(EValueType t);
inline EValueComponentType MakeConcreteType(EValueComponentType c_t){return c_t == EValueComponentType::Numeric? EValueComponentType::Float:c_t;}
EValueType MakeConcreteType(EValueType t);




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
	// 双精度变单精度
	FType GetNonLWCType() const {return IsNumericLWC()? FType(MakeNoneLWCType(value_type)):*this;}
	// 类型定性
	FType GetConcreteType() const {return IsNumeric()? FType(MakeConcreteType(value_type)):*this;}
    bool IsVoid() const {return value_type == EValueType::Void;}
    bool IsStruct() const {return value_type == EValueType::Struct;}
    bool IsObject() const {return value_type == EValueType::Object;}
    bool IsAny() const {return value_type == EValueType::Any;}
	// 是否是还没定型的数值
	bool IsGeneric() const {return !IsStruct()&& !IsObject() && IsGenericType(value_type);}
	// 是否是可运算类型
	bool IsNumeric() const {return !IsStruct()&& !IsObject() && IsNumericType(value_type);}
	// 是否是单值可运算
	bool IsNumericScalar() const {return !IsStruct()&& !IsObject() && IsNumericScalarType(value_type);}
	// 是否是向量可运算
	bool IsNumericVector() const {return !IsStruct()&& !IsObject() && IsNumericVectorType(value_type);}
	// 是否是矩阵可运算
	bool IsNumericMatrix() const {return !IsStruct()&& !IsObject() && IsNumericMatrixType(value_type);}
	// 是否是大世界精度可运算
	bool IsNumericLWC() const {return IsNumeric()&& IsLWCType(value_type);}

	// struct S { float3 A; Inner B; }    Inner { float X; float Y; }
	// 返回 A.x, A.y, A.z, X, Y
	int32_t GetNumComponents() const;
	// 返回 A，X，Y
	int32_t GetNumFlatFields() const;
	EValueComponentType GetComponentType(int32_t index) const;
	EValueType GetFlatFieldType(int32_t index) const;

	inline operator EValueType()const {return value_type;}
	inline operator bool() const {return !IsVoid();}
	inline bool operator!() const {return IsVoid();}

    // value_type == EValueType::Struct 时生效，指向真正的结构类型
    const FStructType* struct_type = nullptr;
    // value_type == EValueType::Object 时生效, Texture 这种类型没有对应的引擎测对象，
    std::string object_name;
    // 一直会用的值，指向具体类型
    EValueType value_type;
};

inline bool operator==(const FType& l, const FType& r){
	if (l.value_type != r.value_type) return false;
	if (l.value_type==EValueType::Struct && r.value_type!=EValueType::Struct) return false;
	if (l.value_type==EValueType::Object && r.value_type!=EValueType::Object) return false;
	return true;
}
inline bool operator!=(const FType& l, const FType& r){
	return !(l==r);
}

inline bool operator==(const FType& l, const EValueType& r)
{
	return !l.IsStruct() && l.value_type == r;
}
inline bool operator!=(const FType& l, const EValueType& r)
{
	return !operator==(l, r);
}

inline bool operator==(const EValueType& l, const FType& r)
{
	return !r.IsStruct() && l == r.value_type;
}
inline bool operator!=(const EValueType& l, const FType& r)
{
	return !operator==(l, r);
}

FType CombineTypes(const FType& l, const FType& r, bool b_merge_matrix_types=false);

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

struct FStructFieldInitializer{

	FStructFieldInitializer()=default;
	FStructFieldInitializer(const std::string& n, const FType& t):name(n), type(t){}
	std::string name;
	FType type;
};

struct FStructTypeInitializer{
	std::string name;
	std::vector<FStructFieldInitializer> fields;
	bool b_is_derivative_type = false;
};


class FStructTypeRegistry{
public:
	explicit FStructTypeRegistry(MemStack& in_allocator):allocator(&in_allocator){}
	void EmitDeclarationsCode(FStringBuilderBase& out_code) const;
	const FStructType* NewType(const FStructTypeInitializer& initializer);
	// 外部结构的注册方法，一般来自hlsl侧，本系统只知其名字，不清楚细节
	const FStructType* NewExternalType(std::string name);
	const FStructType* FindType(uint64_t hash) const;


private:
	MemStack* allocator;
	std::map<uint64_t, const FStructType*> types;
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

// 4分量定长容器（AsFloat 等转出函数的返回类型）
template<typename T>
struct TValue{
    T component[4];
    inline T& operator[](int32_t i){ME_CHECK(i>=0 && i<4); return component[i];}
    inline const T& operator[](int32_t i)const{ME_CHECK(i>=0 && i<4); return component[i];}
};
using FFloatValue = TValue<float>;
using FDoubleValue = TValue<double>;
using FIntValue = TValue<int32_t>;
using FBoolValue = TValue<bool>;

// ToString 的输出格式
enum class EValueStringFormat{
    Description,
    HLSL,
};

// DDC 序列化的字节镜像（课20 用）
struct FMemoryImageValue{
    static const uint32_t max_size = sizeof(double)*16;
    uint8_t bytes[max_size];
    uint32_t size;
};

// 可以存储一个任意类型的算术/结构值，值被表达为平铺的分量列表
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



//   ┌─────┬──────────┬────────────┬───────────────────────────────────────┬─────────┐
//   │  #  │   函数   │    数学    │              选它的理由               │ UE 位置 │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 1   │ Neg      │ -x         │ Negate 算子                           │ :1389   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 2   │ Abs      │ |x|        │ Abs 算子                              │ :1394   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 3   │ Saturate │ clamp(0,1) │ 材质最常用                            │ :1399   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 4   │ Floor    │ 向下取整   │ Round 族代表                          │ :1404   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 5   │ Ceil     │ 向上取整   │ 同上                                  │ :1409   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 6   │ Frac     │ 小数部分   │ Panner/动画常用                       │ :1429   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 7   │ Sign     │ 符号       │ 简单                                  │ :1424   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 8   │ Sqrt     │ √         │ 材质常用                              │ :1439   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 9   │ Rcp      │ 1/x        │ Div 的折叠搭档                        │ :1444   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 10  │ Sin      │ sin        │ 三角代表                              │ :1474   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 11  │ Cos      │ cos        │ 同上                                  │ :1479   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 12  │ Add      │ +          │ 四则之首                              │ :1504   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 13  │ Sub      │ −          │ 四则                                  │ :1509   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 14  │ Mul      │ ×         │ 四则                                  │ :1514   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 15  │ Div      │ ÷         │ 四则                                  │ :1519   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 16  │ Min      │ 取小       │ 向量常用                              │ :1544   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 17  │ Max      │ 取大       │ 向量常用                              │ :1549   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 18  │ Clamp    │ 夹取       │ 三元代表                              │ :1569   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 19  │ Dot      │ 点积       │ 向量核心                              │ :1574   │
//   ├─────┼──────────┼────────────┼───────────────────────────────────────┼─────────┤
//   │ 20  │ Append   │ 拼接       │ ComponentMask/AppendVector 的求值需要 │ :1667