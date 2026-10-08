#pragma once
#include <cstdint>

enum class EDerivativeStatus:uint8_t{
    NotAware,  // 表达式不感知导数(普通路径)
    NotValid,  // 导数无效(分支/采样后)
    Zero,  // 导数横零(常量)
    Valid,  // 解析导数有效(code_analytic 可用)
};

inline bool IsDerivateValid(EDerivativeStatus status){
    return status == EDerivativeStatus::Zero || status == EDerivativeStatus::Valid; 
}
// 导数变体（对照 UE ECompiledPartialDerivativeVariation, MaterialShared.h:2043）
enum class ECompiledPartialDerivativeVariation : uint8_t {
    FiniteDifferences,   // 硬件有限差分版（CompiledPDV_FiniteDifferences）
    Analytic,            // 解析导数版（CompiledPDV_Analytic）
};
