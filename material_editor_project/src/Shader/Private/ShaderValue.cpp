#include "Shader/Public/ShaderTypes.h"

namespace shader{

const FValueTypeDescription& GetValueTypeDescription(EValueType t){
    return GValueTypeDescriptions[(int32_t)t];
}

EValueType MakeValueType(EValueComponentType c_t, int8_t nums){
    if (c_t == EValueComponentType::Void || nums == 0){
        return EValueType::Void;
    }
    switch(c_t){
    case EValueComponentType::Float:
        switch(nums){
            case 1: return EValueType::Float1;
            case 2: return EValueType::Float2;
            case 3: return EValueType::Float3;
            case 4: return EValueType::Float4;
            case 16: return EValueType::Float4x4;
            default: break;
        } 
        break;
    case EValueComponentType::Double:
        switch(nums){
            case 1: return EValueType::Double1;
            case 2: return EValueType::Double2;
            case 3: return EValueType::Double3;
            case 4: return EValueType::Double4;
            case 16: return EValueType::Double4x4;
            default: break;
        } 
        break;
    case EValueComponentType::Numeric:
        switch(nums){
            case 1: return EValueType::Numeric1;
            case 2: return EValueType::Numeric2;
            case 3: return EValueType::Numeric3;
            case 4: return EValueType::Numeric4;
            case 16: return EValueType::Numeric4x4;
            default: break;
        } 
        break;
    case EValueComponentType::Int:
        switch(nums){
            case 1: return EValueType::Int1;
            case 2: return EValueType::Int2;
            case 3: return EValueType::Int3;
            case 4: return EValueType::Int4;
            case 16: return EValueType::Float4x4;
            default: break;
        } 
        break;
    case EValueComponentType::Bool:
        switch(nums){
            case 1: return EValueType::Bool1;
            case 2: return EValueType::Bool2;
            case 3: return EValueType::Bool3;
            case 4: return EValueType::Bool4;
            case 16: return EValueType::Float4x4;
            default: break;
        } 
        break;
    }


    return EValueType::Void;
}

EValueType MakeValueType(EValueType t, int8_t nums){
    return MakeValueType(GetValueTypeDescription(t).comp_type, nums); 
}

EValueType MakeDerivativeType(EValueType t){
    const FValueTypeDescription& type_desc = GetValueTypeDescription(t);
    if (IsNumericType(type_desc.comp_type)){
        return MakeValueType(EValueComponentType::Float, type_desc.num_components);
    }
    return EValueType::Void;

}

EValueType MakeNoneLWCType(EValueType t){
    FValueTypeDescription type_desc = GetValueTypeDescription(t);
    if (type_desc.comp_type == EValueComponentType::Double){
        return MakeValueType( MakeNoneLWCType(type_desc.comp_type), type_desc.num_components);
    }
    return t;
}

EValueType MakeConcreteType(EValueType t){
    FValueTypeDescription type_desc = GetValueTypeDescription(t);
    if (type_desc.comp_type == EValueComponentType::Numeric){
        return MakeValueType( MakeConcreteType(type_desc.comp_type), type_desc.num_components);
    }
    return t; 
}





// ===================↑ 都是工具函数===================





const char* FType::GetName() const{
    if(IsStruct()){
        return struct_type->name;
    }
    FValueTypeDescription description = GetValueTypeDescription(value_type);
    return description.name;
}

// derivative 求导
FType FType::GetDerivativeType() const{
    if (IsStruct()){
        return struct_type->derivative_type;
    }
    else if(IsObject()){
        return *this;
    }

    return MakeDerivativeType(value_type);
}

int32_t FType::GetNumComponents() const{
    if(IsStruct()){
        return struct_type->component_types.size();
    }
    if(IsObject()){
        return 1;
    }
    return GetValueTypeDescription(value_type).num_components;
}

int32_t FType::GetNumFlatFields() const{
    if(IsStruct()){
        return struct_type->flat_field_types.size();
    }
    return 1;
}

EValueComponentType FType::GetComponentType(int32_t index) const{
    if (index<0){
        return EValueComponentType::Void;
    }

    if(IsStruct()){
        if(index<struct_type->component_types.size()){
            return struct_type->component_types[index];
        }
    }
    else if(IsNumeric()){
        FValueTypeDescription type_desc = GetValueTypeDescription(value_type);
        // 标量的特权， 标量在获取其2，3，4号位的类型时依旧返回自己类型，防止类型广播时出错
        if((type_desc.num_components == 1 && index<=3 )|| index<type_desc.num_components){
            return type_desc.comp_type;
        }
    }
    return EValueComponentType::Void;

}

EValueType FType::GetFlatFieldType(int32_t index) const{
    if(index<0){
        return EValueType::Void;
    }

    if(IsStruct()){
        return index<struct_type->flat_field_types.size()? struct_type->flat_field_types[index]:EValueType::Void;
    }
    return index==0 ? value_type:EValueType::Void;
}
	// 

}
