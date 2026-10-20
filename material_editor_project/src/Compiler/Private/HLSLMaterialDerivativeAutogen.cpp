#include "Compiler/Public/HLSLMaterialDerivativeAutogen.h"


EDerivativeType GetDerivType(EMaterialValueType value_type, bool b_allow_non_float){
    switch(value_type){
        case MCT_StaticBool:
        case MCT_Float:
        case MCT_Float1:
        case MCT_UInt1:
            return EDerivativeType::Float1;
        case MCT_Float2:
            return EDerivativeType::Float2;
        case MCT_Float3:
            return EDerivativeType::Float3;
        case MCT_Float4:
            return EDerivativeType::Float4;
        case MCT_LWCScalar:
            return EDerivativeType::LWCScalar;
        case MCT_LWCVector2:
            return EDerivativeType::LWCVector2;
        case MCT_LWCVector3:
            return EDerivativeType::LWCVector3;
        case MCT_LWCVector4:
            return EDerivativeType::LWCVector4;
        default:
            ME_CHECK()
    }
}