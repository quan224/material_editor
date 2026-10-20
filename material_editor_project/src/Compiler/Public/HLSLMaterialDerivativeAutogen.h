#pragma once
#include <cstdint>
#include "MaterialTypes/Public/ValueType.h"
#include "Core/Public/Logger.h"

enum class EDerivativeType
{
	None = -1,
	Float1 = 0,
	Float2,
	Float3,
	Float4,
	LWCScalar,
	LWCVector2,
	LWCVector3,
	LWCVector4,
	Num,
};

enum class EDerivativeStatus:uint8_t{
    NotAware,  // 表达式不感知导数(普通路径)
    NotValid,  // 导数无效(分支/采样后)
    Zero,  // 导数恒零(常量)
    Valid,  // 解析导数有效(code_analytic 可用)
};

inline bool IsDerivativeValid(EDerivativeStatus status){
    return status == EDerivativeStatus::Zero || status == EDerivativeStatus::Valid;
}

struct FDerivInfo{
    EMaterialValueType type;
    EDerivativeType deriv_type;
    EDerivativeStatus derivative_status;
};

EDerivativeType GetDerivType(EMaterialValueType value_type, bool b_allow_non_float=false);
