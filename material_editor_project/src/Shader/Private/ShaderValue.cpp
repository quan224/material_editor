#include "Shader/Public/ShaderTypes.h"

namespace shader{

const char* FType::GetName() const{
    if(IsStruct()){
        return struct_type->name;
    }
    FValueTypeDescription description = GValueTypeDescriptions[value_type];
    return description.name;
}

}
