#pragma once

enum ECompiledPartialDerivativeVariation{
    CompiledPDV_FiniteDifferences,
    CompiledPDV_Analytic,
    CompiledPDV_MAX
};

// LWC 函数使用种类统计。对照 UE ELWCFunctionKind（MaterialShared.h:292，值原样）
enum class ELWCFunctionKind
{
	Constructor,
	Promote,
	Demote,
	Add,
	Subtract,
	Divide,
	MultiplyVectorVector,
	MultiplyVectorMatrix,
	MultiplyMatrixMatrix,
	Other,
	Max
};