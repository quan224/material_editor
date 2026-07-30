// MaterialCompiler_SkinBranch.cpp
// —— 补丁示例：给 MaterialCompiler 加 Skin 材质模型分支 ——
// 这不是独立编译单元，而是展示"编译器如何处理 ExprSkin"的代码片段。
// 真正实现时把这段并入 src/Compiler/Private/MaterialCompiler.cpp，并 include ExprSkin.h。
//
// 职责：
//   1) 检测节点表达式是否为 ExprSkin（材质模型判定）；
//   2) 写 G-Buffer：baseColor/normal/roughness/metallic + SSS mask = 1；
//   3) 声明 SSS 参数 cbuffer（scatterRadius/Color/intensity），值由渲染器从反射填。
#include "Compiler/Public/MaterialCompiler.h"
#include "Expression/Public/Expression.h"
#include "ExprSkin.h"
#include <sstream>

// 假设在 MaterialCompiler 里新增一个成员：记录当前材质是否为 Skin，供渲染器查询
//   bool currentMaterialIsSkin_ = false;
// 以及一个方法：编译材质节点时调用
bool MaterialCompiler::CompileSkinIfSkin(Node* node)
{
    Expression* expr = /*从 node 取表达式*/ nullptr; // 实际：node->GetExpression()
    // 这里假设有 Node::GetExpression()；项目里节点持有一个 Expression*
    // 若无，可通过 typeName == "Skin" 判定

    ExprSkin* skin = dynamic_cast<ExprSkin*>(expr);
    if (!skin) return false;

    currentMaterialIsSkin_ = true;

    // 1) 走标准 PBR 编译（ExprSkin::Compile 已做 GGX 光照色）
    std::vector<int32_t> out = skin->Compile(this, node);

    // 2) 写 G-Buffer：在生成的 HLSL 里，把光照色写进 G-Buffer 的 color 槽，
    //    并把 SSS mask 写进 G-Buffer 的 SSS 通道（RE Engine 用 VelocityXYAoSss.A）。
    std::ostringstream hlsl;
    hlsl << "// —— Skin material: 写 G-Buffer + SSS mask —— \n"
         << "GBufferOut.Color     = " << GetParameterCode(out[0]) << "; // 光照色\n"
         << "GBufferOut.SSSMask   = 1.0;                            // 标记为皮肤\n"
         << "GBufferOut.ScatterR  = Material.ScatterRadius;        // 透传给 SSSS pass\n"
         << "GBufferOut.ScatterC  = Material.ScatterColor;\n"
         << "GBufferOut.SSSStreng = Material.SSSIntensity;\n";
    // 把这段并入当前材质 HLSL 输出（具体并入点取决于编译器的 HLSL 拼装方式）

    // 3) 声明材质参数 cbuffer（ScatterRadius/Color/Intensity）
    //    值由渲染器从 ExprSkin 反射读出填进 cbuffer（见 SSSS_IntegrationNotes.md）
    hlsl << "\ncbuffer cbSkin : register(bSLOT_SKIN) {\n"
         << "    float3 Material_ScatterRadius; // 反射 GetParameter(\"scatterRadius\")\n"
         << "    float3 Material_ScatterColor;  // 反射 GetParameter(\"scatterColor\")\n"
         << "    float  Material_SSSIntensity;  // 反射 GetParameter(\"intensity\")\n"
         << "};\n";

    // （把 hlsl.str() 并入 GenerateCode 的材质段；具体接口依编译器实现）
    (void)hlsl;
    return true;
}

// 渲染器侧（伪代码，放在 Renderer 里，不是这里）——绑定 SSSS 参数：
//   ExprSkin* skin = ...;
//   auto jr = skin->GetParameter("scatterRadius"); // nlohmann::json
//   cbSSS.scatterRadius = Vec3(jr[0],jr[1],jr[2]);  // 走 Accessor 反序列化
//   ... 同理 scatterColor / intensity
//   然后 SSSS 双 pass 用这份 cbuffer。
