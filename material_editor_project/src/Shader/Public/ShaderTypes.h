#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace shader{

struct FType{

    FType():value_type(EValueType::Void){}
    FType(EValueType in_value_type): value_type(in_value_type){
        // check( value_type != EValueType::Struct && value_type != EValuetype::Object);
    }
    FType(const FStructType* in_struct_type): struct_type(in_struct_type), value_type(struct_type? EValueType::Struct : EValueType::Void){}
    FType(const std::string& in_object_type): object_name(in_object_type), value_type(object_name.empty()? EValueType::Void : EValueType::Object){}

    const char* GetName() const;
    FType GetDerivativeType() const;

    bool IsVoid() const {return value_type == EValueType::Void;}
    bool IsStruct() const {return value_type == EValueType::Struct;}
    bool IsObject() const {return value_type == EValueType::Object;}
    bool IsAny() const {return value_type == EValueType::Any;}

    

    const FStructType* struct_type = nullptr;
    std::string object_name;
    EValueType value_type;
};

struct FStructType{
    uint64_t hash;
    const char* name;
    const FStructType* derivative_type = nullptr;

    std::vector<const FStructField> fields;
    std::vector<const EValueComponentType> component_types;
    std::vector<const EValueType> flat_field_types;

    bool IsExternal() const {return fields.empty();}
    int32_t GetNumComponents() const {return component_types.size();}
    const FStructField* FindFieldByName(const char* in_name) const;
};

struct FStructField{
    const char* name;
    FType type;
    int32_t component_index;
    int32_t flat_field_index;
    int32_t GetNumComponents() const {return type.GetNumComponents();}
};


enum class EValueComponentType : uint8
{
	Void,
	Float,
	Double,
	Int,
	Bool,

	// May be any numeric type, stored internally as 'double' within FValue
	Numeric,

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

	Num,
};
}
