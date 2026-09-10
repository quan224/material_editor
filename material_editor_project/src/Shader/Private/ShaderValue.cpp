#include "Shader/Public/ShaderTypes.h"

namespace shader{

const FValueTypeDescription& GetValueTypeDescription(EValueType t){
    return GValueTypeDescriptions[(int32_t)t];
}

EValueType MakeDerivativeType(EValueType t){
    const FValueTypeDescription& type_desc = GetValueTypeDescription(t);

}











const char* FType::GetName() const{
    if(IsStruct()){
        return struct_type->name;
    }
    FValueTypeDescription description = GetValueTypeDescription(value_type);
    return description.name;
}

FType FType::GetDerivativeType() const{
    if (IsStruct()){
        return struct_type->derivative_type;
    }
    else if(IsObject()){
        return *this;
    }

    return MakeDerivativeType(value_type);
}

}
