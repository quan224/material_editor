// ExprSkin.h
// 皮肤材质 Expression：标准 PBR（GGX）+ 次表面散射参数。
// 参数通过反射 ME_FIELD 暴露到属性面板；Compile 出标准 GGX 光照色。
// SSS 的实际散射由渲染器在延迟光照后跑 SSSS pass（屏幕空间，见 SSSS_SeparablePass.hlsl），
// 参数（scatterRadius/Color/intensity）由渲染器经反射 GetParameter 读出绑进 SSSS cbuffer。
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Core/Public/MathTypes.h"
#include <vector>
#include <cstdint>

class ExprSkin : public Expression
{
public:
    // —— SSS 参数（反射暴露到属性面板，渲染器读出绑 SSSS cbuffer）——
    Vec3  scatterRadius = Vec3(1.0f, 0.45f, 0.25f); // 散射半径：R 散得最远→偏暖（mm 量级，调参）
    Vec3  scatterColor  = Vec3(1.0f, 0.75f, 0.65f); // 散射颜色（肤色）
    float intensity     = 1.0f;                     // SSS 强度

    // —— Expression 接口 ——
    std::vector<ExpressionPinDesc> GetInputPins()  const override;
    std::vector<ExpressionPinDesc> GetOutputPins() const override;
    std::vector<int32_t> Compile(MaterialCompiler* compiler, Node* ownerNode) const override;

    // —— 反射注册（GetClassDesc 由 ME_END_CLASS 自动实现）——
    ME_BEGIN_CLASS(ExprSkin)
        ME_DISPLAY_NAME("Skin")
        ME_CATEGORY("Material")
        ME_FIELD(ExprSkin, scatterRadius, Vec3(1.0f, 0.45f, 0.25f))
        ME_FIELD(ExprSkin, scatterColor,  Vec3(1.0f, 0.75f, 0.65f))
        ME_FIELD(ExprSkin, intensity, 1.0f)
    ME_END_CLASS(ExprSkin)
};
