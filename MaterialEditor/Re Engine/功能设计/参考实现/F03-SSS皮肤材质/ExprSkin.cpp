// ExprSkin.cpp
#include "ExprSkin.h"
#include "MaterialGraph/Public/Node.h"

std::vector<ExpressionPinDesc> ExprSkin::GetInputPins() const
{
    return {
        { "BaseColor", EValueType::Float3, "0.9,0.7,0.6" }, // 默认肤色
        { "Roughness", EValueType::Float1, "0.5" },
        { "Metallic",  EValueType::Float1, "0.0" },         // 皮肤通常非金属
        { "Normal",    EValueType::Float3, "0,0,1" },
        { "AO",        EValueType::Float1, "1.0" },
    };
}

std::vector<ExpressionPinDesc> ExprSkin::GetOutputPins() const
{
    // 材质模型节点：单个"Material"输出交给输出节点/渲染器（实际写入 G-Buffer）。
    return { { "Material", EValueType::Float3, "" } };
}

std::vector<int32_t> ExprSkin::Compile(MaterialCompiler* compiler, Node* ownerNode) const
{
    // 1) 递归编译上游输入，拿到各输入的 chunk 索引
    int32_t baseColor = compiler->CompileInputPin(ownerNode, "BaseColor");
    int32_t roughness = compiler->CompileInputPin(ownerNode, "Roughness");
    int32_t metallic  = compiler->CompileInputPin(ownerNode, "Metallic");
    int32_t normal    = compiler->CompileInputPin(ownerNode, "Normal");
    int32_t ao        = compiler->CompileInputPin(ownerNode, "AO");

    // 2) 取各输入的 HLSL 片段（符号名或字面量）
    std::string cb = compiler->GetParameterCode(baseColor);
    std::string cr = compiler->GetParameterCode(roughness);
    std::string cm = compiler->GetParameterCode(metallic);
    std::string cn = compiler->GetParameterCode(normal);
    std::string ca = compiler->GetParameterCode(ao);

    // 3) 生成本材质的标准 GGX 光照色 chunk。
    //    SkinShading() 是材质头里的 GGX BRDF（见 SSSS_IntegrationNotes.md 的完整实现）。
    //    注意：SSS 的"散射"由延迟光照后的 SSSS pass 处理，这里只产出"标准光照色"。
    //    SSS mask = 1 由渲染器在 G-Buffer 写入（见 MaterialCompiler_SkinBranch.cpp）。
    std::string code = "SkinShading(" + cb + ", " + cr + ", " + cm + ", " + cn + ", " + ca + ")";

    int32_t out = compiler->AddCodeChunk(EValueType::Float3, code, /*is_inline=*/false);
    return { out };
}
