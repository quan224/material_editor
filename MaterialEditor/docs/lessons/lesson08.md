# 课8：HLSL 代码生成

## 目标

将编译器生成的代码块组装成完整的 HLSL 着色器，可以直接用于 DX12 预览渲染。

---

## 背景知识

课 6 的编译器产物是 `chunks_` 数组（`CodeChunk` 中间表示），课 7 打通了图遍历。本课补上最后一环：`GenerateCode()`——把 `chunks_` 组装成完整的 HLSL 着色器。

一个完整的着色器除了局部变量声明，还需要：

1. cbuffer 常量缓冲声明（场景常量 + 材质参数）
2. 顶点着色器传来的插值变量（`PS_INPUT`）
3. PBR 光照计算代码
4. VS/PS 两个入口函数（`VSMain` / `PSMain`，带语义）

UE5 用 `MaterialTemplate.ush` 作为骨架，编译器生成的代码注入到特定位置。我们用同样的方式。

由于我们使用 DX12 作为渲染后端，着色器直接使用 HLSL。DX12 原生支持 HLSL，不需要 GLSL 转换。

---

## 操作步骤

### 1. 创建文件

```
src/Compiler/Public/HLSLTemplate.h
src/Compiler/Public/UniformCollector.h
src/Compiler/Public/HLSLGenerator.h
```

### 2. HLSLTemplate.h —— VS+PS 一体完整模板

把编译器输出想象成填一张**表格**：表头/表尾/章节标题都是固定的（cbuffer 声明、struct 定义、PBR 辅助函数、main 入口签名），只有表格中间几格是材质图编译产物（局部变量声明、材质属性赋值）。固定部分是「着色器骨架」，可变部分是「编译器注入」。

UE5 `MaterialTemplate.ush` 就是这张表格（几千行，含 Substrate/虚拟纹理/光追/各材质域分支），编译器把生成代码塞进 `%XXX%` 标记。我们的教学版缩到 ~120 行，但**思路完全一致**：模板定义结构，编译器填空。

模板统一承担三件事：① VS 也注入材质代码（`WorldPositionOffset` 是材质驱动的——材质图能改顶点位置，所以 VS 不能是完全手写的独立文件）；② 材质参数预留 `cbuffer MaterialParams`（b1 寄存器，b0 留给 `SceneConstants`）；③ 局部变量声明区（编译器按依赖顺序注入）。

```cpp
// HLSLTemplate.h
#pragma once
#include <string>

class HLSLTemplate {
public:
    // 完整模板：VS + PS + cbuffer，统一生成
    // 对标 UE5 Engine/Shaders/Private/MaterialTemplate.ush（精简到 ~120 行）
    static std::string GetFullShaderTemplate() {
        return R"hlsl(
// === 由 MaterialCompiler 生成，不要手改 ===

// ────── cbuffer：场景常量（DX12 root signature b0 槽）──────
cbuffer SceneConstants : register(b0) {
    float4x4 viewProj;
    float4x4 model;
    float4   cameraPos;
    float4   lightDir;
    float4   lightColor;
    float    time;
    float3   _pad0;
};

// ────── cbuffer：材质参数（b1 槽，由 UniformCollector 填）──────
{{UNIFORMS}}

// ────── 纹理 + 采样器（材质图里的 TextureSample 节点引用）──────
{{TEXTURES}}

// ────── 输入/输出结构（DX12 input layout 必须匹配 VS_INPUT）──────
struct VS_INPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct PS_INPUT {
    float4 pos       : SV_POSITION;   // 光栅化必需，由 VS 写
    float3 worldPos  : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 uv        : TEXCOORD2;
};

// ────── 材质局部变量（拓扑排序后注入，见 §4）──────
{{MATERIAL_FUNCTIONS}}

static const float PI = 3.14159265359;

// PBR 辅助函数必须定义在 main 之前——HLSL 不允许前向调用未声明函数
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom  = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ────── Vertex Shader：MVP 变换 + WorldPositionOffset 注入 ──────
PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    float3 worldPositionOffset = float3(0, 0, 0);
    {{MATERIAL_BODY_VS}}                 // 材质图驱动的顶点偏移
    float4 worldPos = mul(model, float4(input.position + worldPositionOffset, 1.0));
    output.worldPos = worldPos.xyz;
    output.normal   = mul((float3x3)model, input.normal);
    output.uv       = input.uv;
    output.pos      = mul(viewProj, worldPos);
    return output;
}

// ────── Pixel Shader：把材质属性 chunk 赋给输出，跑 PBR ──────
float4 PSMain(PS_INPUT input) : SV_Target {
    // 材质属性默认值（必须和 UE5 GetMaterialOutputInfo 的 default 一致）
    float3 BaseColor         = float3(0, 0, 0);
    float  Metallic          = 0.0;
    float  Specular          = 0.5;
    float  Roughness         = 0.5;
    float3 Normal            = normalize(input.normal);
    float3 EmissiveColor     = float3(0, 0, 0);
    float  Opacity           = 1.0;       // Masked/Translucent 才用，Opaque 忽略
    float  AmbientOcclusion  = 1.0;

    {{MATERIAL_BODY_PS}}                 // 材质图编译产物注入这里

    // PBR 直接光照（教学版只做一个方向光，UE5 在 BasePassPixelShader.ush 做多光源 + IBL）
    float3 N = normalize(Normal);
    float3 V = normalize(cameraPos.xyz - input.worldPos);
    float3 L = normalize(-lightDir.xyz);
    float3 H = normalize(V + L);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), BaseColor, Metallic);

    float  NDF = DistributionGGX(N, H, Roughness);
    float  G   = GeometrySmith(N, V, L, Roughness);
    float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - Metallic);
    float  NdotL = max(dot(N, L), 0.0);
    float3 numerator   = NDF * G * F;
    float  denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.001;
    float3 specular = numerator / denominator;
    float3 Lo = (kD * BaseColor / PI + specular) * lightColor.xyz * NdotL;

    float3 ambient = float3(0.03, 0.03, 0.03) * BaseColor * AmbientOcclusion;
    float3 color = ambient + Lo + EmissiveColor;
    color = color / (color + 1.0);                 // Reinhard tone map
    color = pow(color, 1.0 / 2.2);                 // gamma
    return float4(color, Opacity);
}
)hlsl";
    }
};
```

模板里 6 个占位标记的语义（和 UE5 `MaterialTemplate.ush` 一一对应）：

| 占位标记 | 内容 | UE5 对应 |
|--------|------|---------|
| `{{UNIFORMS}}` | `cbuffer MaterialParams { ... }` | `%PARAMETER_DECLARATION%` |
| `{{TEXTURES}}` | `Texture2D T0; SamplerState S0; ...` | `%TEXTURE_DECLARATION%` |
| `{{MATERIAL_FUNCTIONS}}` | 局部变量声明（拓扑排序后）`float3 Local0 = ...;` | `%MATERIAL_BODY%` |
| `{{MATERIAL_BODY_VS}}` | VS 里的 WorldPositionOffset 赋值 | `%WORLDPOSITIONOFFSET%` |
| `{{MATERIAL_BODY_PS}}` | PS 里把材质属性 chunk 赋给 BaseColor/Metallic/... | 各 `%PIXELMATERIALINPUTS_*%` |

### 3. UniformCollector（uniform 收集 + cbuffer 生成）

材质图里的「参数节点」（`ScalarParameter`/`VectorParameter`/`TextureParameter`）编译时变成**带名字的 uniform**——它们不是局部变量（每次材质实例化值不同），要汇总成 `cbuffer MaterialParams { ... }`，由 CPU 端每实例上传。

`UniformCollector` 只关心生成侧：给定一组 uniform 描述，输出符合 HLSL packing 规则的 `cbuffer`（描述从哪来——参数节点的注册——属课 20 参数系统的范围；本课 collector 接收描述并生成）。

```cpp
// UniformCollector.h
#pragma once
#include <Core/Public/MathTypes.h>   // Vec2/Vec3/Vec4
#include <string>
#include <vector>

struct UniformEntry {
    std::string name;        // "BaseColorValue" / "Roughness" / 等
    enum class Kind { Float, Float2, Float3, Float4, Texture2D } kind;
    int  registerSlot;       // cbuffer 内偏移 / 纹理寄存器槽（t0/t1/...）
};

class UniformCollector {
public:
    void AddScalar(const std::string& name) {
        uniforms_.push_back({name, UniformEntry::Kind::Float, /*slot=*/-1});
    }
    void AddVector(const std::string& name) {     // Vec3 实际存成 Float4（对齐）
        uniforms_.push_back({name, UniformEntry::Kind::Float4, -1});
    }
    void AddTexture(const std::string& name) {
        textures_.push_back({name, UniformEntry::Kind::Texture2D, -1});
    }

    // 生成 cbuffer + 纹理声明。slot 在这里分配（cbuffer packing 规则见下）
    std::string Generate() const {
        std::string out;
        out += "cbuffer MaterialParams : register(b1) {\n";

        // cbuffer packing（HLSL packing 规则，对标 D3D11 constant buffer layout）：
        // - 每个变量从 16 字节边界开始，不能跨 16 字节边界
        // - float3 占 12 字节，下一个 float 会被挤到下一个 vec4 槽（除非显式 packoffset）
        // - 安全做法：所有标量集中放，所有 vec3/vec4 单独占槽，避免 padding 浪费
        int slot = 0;
        for (const auto& u : uniforms_) {
            switch (u.kind) {
                case UniformEntry::Kind::Float:
                    out += "    float " + u.name + ";\n";
                    break;
                case UniformEntry::Kind::Float2:
                    out += "    float2 " + u.name + ";\n";
                    break;
                case UniformEntry::Kind::Float3:
                    out += "    float3 " + u.name + ";\n";
                    break;
                case UniformEntry::Kind::Float4:
                    out += "    float4 " + u.name + ";\n";
                    break;
            }
            (void)slot;
        }
        out += "}\n\n";

        // 纹理 + 采样器（t0/t1/... 寄存器槽自动分配）
        int texSlot = 0;
        for (const auto& t : textures_) {
            out += "Texture2D    " + t.name + " : register(t" + std::to_string(texSlot) + ");\n";
            out += "SamplerState " + t.name + "_S : register(s" + std::to_string(texSlot) + ");\n";
            ++texSlot;
        }
        return out;
    }

private:
    std::vector<UniformEntry> uniforms_;
    std::vector<UniformEntry> textures_;
};
```

> **cbuffer packing 警告（已踩坑预警）**：HLSL 的 cbuffer 不是 C struct 那样紧密排列。`float3 a; float b;` 占 32 字节（`a` 占一个 vec4 槽，`b` 因为不能挤进 `a` 的剩余 4 字节而开新槽——准确说 `b` **能**挤进去，但 `float3 a; float3 b;` 占 32 字节，因为 `b` 不能跨槽）。CPU 端上传时**必须按同样的 packing 规则布局**，否则数据错位。教学版建议：① 全部用 `float4`（最简单，浪费点显存无所谓）；② 或加 `packoffset` 显式控制。课14 接 DX12 时会再讲一次。

### 4. 声明顺序：拓扑排序（按 `CodeChunk::references`）

这是代码生成**最容易被忽略但必踩的坑**。

**问题**：`chunks_` 的 push_back 顺序 = 算子调用顺序，不等于依赖顺序。看个反例：

```cpp
// 用户图：BaseColor = (Constant3(1,0,0) + Constant3(0,1,0)) * 0.5
int a = compiler->Constant3(1,0,0);   // chunk 0（内联，不入声明）
int b = compiler->Constant3(0,1,0);   // chunk 1（内联，不入声明）
int sum = compiler->Add(a, b);        // chunk 2（内联，因为是简单算术）
                                       // 注：Add 走 AddInlinedCodeChunk，不生成 Local
int half_ = compiler->Constant(0.5);  // chunk 3（内联）
int result = compiler->Multiply(sum, half_);  // chunk 4（如果非常量、复杂，可能非内联）
```

简单情况靠内联规避了声明顺序问题。但**复杂表达式**（`TextureSample`、`Power`、`Cross` 这些走 `AddCodeChunk(is_inline=false)`）必须声明局部变量：

```cpp
int n1 = compiler->TextureSample(tex, uv);  // chunk 5：非内联，symbolName = "Local0"
int n2 = compiler->Power(n1, exp);          // chunk 6：非内联，symbolName = "Local1"，references=[5]
```

如果之后有 chunk 7 引用 chunk 6，而 chunks_ 顺序是 `[5, 6, 7]`，简单 for 循环按顺序输出没问题。但**图遍历顺序不保证**——比如先编译了下游再编译上游（缓存命中或递归回溯），push_back 顺序可能是 `[6, 5, 7]` 或更乱。

**生成的 HLSL 就会出错**：

```hlsl
float3 Local1 = pow(Local0, exp);   // Local0 还没声明 → fxc 报 undeclared identifier
float3 Local0 = tex2D(...);
```

**解决方案**：按 `CodeChunk::references` 做拓扑排序。

#### 算法对比

| 算法 | 思路 | 适合 |
|------|------|------|
| **Kahn**（入度法）| 计算每个 chunk 入度（被多少 chunk 依赖的反向计数），从入度 0 开始摘 | 想要层次顺序（拓扑层） |
| **DFS 后序** | 对每个 chunk DFS，先访问其 references，再访问自己；后序输出 | 实现 5 行，自然处理森林 |

教学版选 **DFS 后序**——简单、递归直观、循环依赖检测天然融入（用「灰度」标记）。

#### 实现（含循环依赖检测）

```cpp
// HLSLGenerator.cpp（或 TopologicalSort.h 单独抽出来）
namespace {
    enum class DFSColor { White, Gray, Black };
}

// 返回拓扑顺序（先被依赖的在前，HLSL 声明顺序要求）
// 检测到循环依赖时返回空 vector + 写 errorMessage
std::vector<int32_t> TopoSortChunks(
    const std::vector<CodeChunk>& chunks,
    std::string& errorMessage)
{
    std::vector<int32_t> order;
    order.reserve(chunks.size());
    std::vector<DFSColor> color(chunks.size(), DFSColor::White);

    // 用 std::function 包装递归 lambda（C++17 lambda 不能直接递归）
    std::function<bool(int32_t)> dfs = [&](int32_t i) -> bool {
        if (i < 0 || i >= (int32_t)chunks.size()) return true;  // 哨兵，无视
        if (color[i] == DFSColor::Black) return true;           // 已访问，跳过
        if (color[i] == DFSColor::Gray) {
            // 回到「灰度」节点 = 找到环
            errorMessage = "Circular dependency detected at chunk "
                         + chunks[i].symbolName + " (index " + std::to_string(i) + "). "
                         + "材质图里有环——检查连线。";
            return false;
        }
        color[i] = DFSColor::Gray;   // 进入栈
        for (int32_t ref : chunks[i].references) {
            if (!dfs(ref)) return false;
        }
        color[i] = DFSColor::Black;  // 离开栈，标记完成
        order.push_back(i);          // 后序：所有依赖先 push 了，最后 push 自己
        return true;
    };

    // 对所有 chunk 都跑一遍（森林——多个无依赖子图也能覆盖）
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (!dfs((int32_t)i)) return {};
    }
    return order;
}
```

**关键点**：

1. **后序输出**（`order.push_back(i)` 在递归回程）——保证 `i` 的所有 `references` 先进 `order`，HLSL 里被引用的先声明。
2. **三色标记**（白/灰/黑）——灰度节点在当前 DFS 栈中，再遇到它就是环。**循环依赖必须在编译期发现**，否则 fxc 报错信息完全不可读（"undeclared identifier"，但其实是环导致根本无法声明）。
3. **森林遍历**（外层 for + 内层 DFS）——chunk 之间可能有多个无依赖子图（BaseColor 和 Normal 各自独立），单次 DFS 起点覆盖不全。
4. **报错带符号名**（`Local7`）——用户在材质图里能定位到具体节点。

> **循环依赖怎么产生**：材质图正常情况下是 DAG（输出节点单向回流到根），不会有环。但**自定义表达式节点**（`Custom` 节点，UE5 也支持）允许用户写 HLSL 引用其他 chunk，理论上能制造环。教学版如果将来加 `Custom` 节点，必须依赖这个拓扑检测兜底。

### 5. HLSLGenerator.h

把 UniformCollector + TopoSort + 模板替换整合到一起：

```cpp
// HLSLGenerator.h
#pragma once
#include "Compiler/Public/CodeChunk.h"
#include "Compiler/Public/TypeSystem.h"
#include "Compiler/Public/UniformCollector.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

class HLSLGenerator {
public:
    struct Params {
        std::map<std::string, int32_t> materialOutputs;  // "BaseColor" → chunk index
        const std::vector<CodeChunk>* chunks = nullptr;  // 指针（结构体要默认构造）
        // uniform / texture 描述（参数系统提供，见课 20；无参数图时为空）
        UniformCollector uniforms;
        // 导数变体选择（对照 UE ECompiledPartialDerivativeVariation）：
        // false=有限差分（普通材质，用 chunk.code）；true=解析导数（位移材质，用 chunk.code_analytic）
        bool bAnalyticDerivatives = false;
    };

    struct Result {
        std::string hlsl;
        std::string error;   // 非空表示生成失败（如循环依赖）
    };

    static Result Generate(const Params& params) {
        Result result;
        if (!params.chunks) { result.error = "no chunks"; return result; }

        // 1. 拓扑排序（检测循环依赖）
        std::string topoError;
        auto order = TopoSortChunks(*params.chunks, topoError);
        if (!topoError.empty()) {
            result.error = topoError;
            return result;
        }

        // 2. 按拓扑顺序生成非内联变量声明
        //    内联 chunk（is_inline=true）不入声明——它们在使用处直接展开
        //    中间块（is_intermediate=true）且无引用者 → 剔除（对照 UE bIntermediate 语义：
        //    LWC 转换辅助块等"只服务生成过程"的块不进最终代码）
        //    导数双轨：按 params.bAnalyticDerivatives 选 code / code_analytic
        //    （对照 UE GetDefinitions 的 Variation 参数——普通材质走有限差分版，
        //     位移/曲面细分材质走解析导数版，两版定义串课 6 已备好字段）
        std::string declarations;
        for (int32_t idx : order) {
            const auto& chunk = (*params.chunks)[idx];
            if (chunk.is_inline) continue;
            if (chunk.is_intermediate && CountReferencers(idx, *params.chunks) == 0) continue;
            declarations += "    " + std::string(TypeSystem::ToHLSLType(chunk.type))
                          + " " + chunk.symbol_name + " = "
                          + chunk.AtCode(params.bAnalyticDerivatives) + ";\n";
        }

        // 3. 材质属性 → PS/VS 赋值代码
        std::string bodyPS = GenerateMaterialBodyPS(params.materialOutputs, *params.chunks);
        std::string bodyVS = GenerateMaterialBodyVS(params.materialOutputs, *params.chunks);

        // 4. uniform / texture 声明
        std::string uniforms = params.uniforms.Generate();

        // 5. 模板替换
        std::string tpl = HLSLTemplate::GetFullShaderTemplate();
        ReplaceAll(tpl, "{{UNIFORMS}}",            uniforms);
        ReplaceAll(tpl, "{{TEXTURES}}",            "");              // 本课不接纹理参数
        ReplaceAll(tpl, "{{MATERIAL_FUNCTIONS}}",  declarations);
        ReplaceAll(tpl, "{{MATERIAL_BODY_VS}}",    bodyVS);
        ReplaceAll(tpl, "{{MATERIAL_BODY_PS}}",    bodyPS);

        result.hlsl = tpl;
        return result;
    }

private:
    static std::string GenerateMaterialBodyPS(
        const std::map<std::string, int32_t>& outputs,
        const std::vector<CodeChunk>& chunks)
    {
        std::string body;
        for (const auto& [name, idx] : outputs) {
            std::string code = GetChunkCode(chunks, idx);
            // 注意左值名字必须和模板里 PSMain 的局部变量名一致
            if      (name == "BaseColor")         body += "    BaseColor = " + code + ";\n";
            else if (name == "Metallic")          body += "    Metallic = " + code + ";\n";
            else if (name == "Specular")          body += "    Specular = " + code + ";\n";
            else if (name == "Roughness")         body += "    Roughness = " + code + ";\n";
            else if (name == "Normal")            body += "    Normal = " + code + ";\n";
            else if (name == "EmissiveColor")     body += "    EmissiveColor = " + code + ";\n";
            else if (name == "Opacity")           body += "    Opacity = " + code + ";\n";
            else if (name == "AmbientOcclusion")  body += "    AmbientOcclusion = " + code + ";\n";
            // WorldPositionOffset 不在 PS 处理，留空
        }
        return body;
    }

    static std::string GenerateMaterialBodyVS(
        const std::map<std::string, int32_t>& outputs,
        const std::vector<CodeChunk>& chunks)
    {
        std::string body;
        auto it = outputs.find("WorldPositionOffset");
        if (it != outputs.end()) {
            body += "    worldPositionOffset = " + GetChunkCode(chunks, it->second) + ";\n";
        }
        return body;
    }

    static std::string GetChunkCode(const std::vector<CodeChunk>& chunks, int32_t idx) {
        if (idx < 0 || idx >= (int32_t)chunks.size()) return "0.0";
        const auto& chunk = chunks[idx];
        return chunk.isInline ? chunk.code : chunk.symbolName;
    }

    static void ReplaceAll(std::string& str, const std::string& from, const std::string& to) {
        if (from.empty()) return;
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();   // 关键：跳过替换内容，避免递归替换
        }
    }
};
```

### 6. 修改 MaterialCompiler —— 接入 HLSLGenerator

`GenerateCode` 把 chunks_ 交给生成器，错误（如循环依赖）冒泡到 `error_message_`：

```cpp
#include "Compiler/Public/HLSLGenerator.h"

std::string MaterialCompiler::GenerateCode(const std::map<std::string, int32_t>& outputs) {
    HLSLGenerator::Params params;
    params.materialOutputs = outputs;
    params.chunks = &chunks_;
    auto result = HLSLGenerator::Generate(params);
    if (!result.error.empty()) {
        error_message_ = result.error;   // 拓扑失败等错误冒泡
        return "";
    }
    return result.hlsl;
}
```

---

## 深度：设计取舍与 UE 对照

### 模板化方案取舍：占位标记替换 vs 纯字符串拼接

实现「模板 + 注入」有两条路：

| 维度 | 占位标记替换（`{{X}}` / `%X%`） | 纯字符串拼接（`out += "..."`） |
|------|------------------------------|------------------------------|
| 模板可读性 | 模板文件本身就是个**完整 HLSL**（标记当注释也能看），用 fxc 单独编一遍能验证骨架正确 | 拼出来的 HLSL 只能跑一次看结果，没拼之前看不见 |
| 维护成本 | 改模板不动 C++（把 PBR 函数从 Schlick 改成 GGX，只动模板文件） | 加一行 HLSL 要改 C++ 重新编译 |
| 转义坑 | 模板里出现标记字符串字面量要小心（HLSL 里基本不会，但注释里 `// {{XXX}}` 会被误替换） | 无（但你拼的时候要小心引号、换行） |
| 工具链 | 模板可以提取成 `.ush` 文件，UE5 用的就是 `#include` + 字符串替换 | 没法独立验证 |
| 编译期检查 | 无（运行时替换） | 无 |
| UE5 选择 | `%XXX%` 占位标记 + `ReplaceParameter` | translator 内部局部拼 |
| **教学版选择** | **`{{XXX}}` 占位标记 + `ReplaceAll`** | — |

**结论：占位标记替换是正确选择**，和 UE 一致，模板可独立维护/验证。本课坚持这条路。

> **关键决策**：占位标记用 `{{NAME}}`（双花括号）而不是 `%NAME%`。理由：① 双花括号在 HLSL 里**不会自然出现**（HLSL 没有花括号插值语法），单 `%` 在 HLSL 里是合法标识符字符（如 `n%2`），有误伤风险；② 双花括号和 Mustache/Jinja 等模板引擎一致，学习者一看就懂。

### 与 DX12 的衔接（课14–17 落地）

> **时机建议**：完整 HLSL 生成本身不依赖 DX12，本课就能写出。但**真正验证它能编译、能渲染**最好在课14–17 DX12 上下文就绪后。课8 只能 `fxc` 离线编一遍验证语法。本节讲清楚生成的 HLSL 怎么被 DX12 用，方便课14 直接接。

| HLSL 端 | DX12 端 | 衔接说明 |
|---------|---------|---------|
| `cbuffer SceneConstants : register(b0)` | `D3D12_ROOT_PARAMETER` type `CBV`，`DescriptorTable` 或 `RootDescriptor` | 根签名里 b0 槽绑定一个 CBV 描述符，指向 256B 对齐的常量缓冲上传区 |
| `cbuffer MaterialParams : register(b1)` | 同上，b1 槽 | 每材质实例独立 CBV，参数系统（课20）填充内容 |
| `Texture2D T0 : register(t0)` + `SamplerState S0 : register(s0)` | `D3D12_ROOT_PARAMETER` type `SRV` / `Sampler` | 描述符表里给 t 槽和 s 槽各一个 |
| `struct VS_INPUT { float3 position : POSITION; ... }` | `D3D12_INPUT_ELEMENT_DESC[]`（input layout） | 语义名和输入槽位一一对应，DX12 在 IA 阶段把顶点缓冲解析成 VS_INPUT |
| `PS_INPUT` 的 `SV_POSITION` | 光栅器自动插值（DX12 内置） | 不需要外部绑定，DX12 看 `SV_POSITION` 知道是裁剪空间坐标 |
| `float4 PSMain(...) : SV_Target` | 当前 render target 的 RTV | SV_Target 表示写到 RTV[0]，MRT 用 `SV_Target0/1/2...` |
| `VSMain` / `PSMain` 入口名 | `D3D12_SHADER_BYTECODE` 的 `EntryPoint` 字段 | 编译时 `fxc /E VSMain /T vs_5_0`、`/E PSMain /T ps_5_0` |

**关键耦合点**：

1. **input layout 必须和 `VS_INPUT` 字段顺序、语义完全一致**——DX12 不会"自动匹配"，错一个字节顶点数据就乱。
2. **cbuffer packing 必须和 CPU 端 struct 布局一致**——见 UniformCollector 的 packing 警告。课14 会定义 `struct SceneConstantsCPU { ... }`，用 `static_assert(offsetof(...) == ...)` 校验。
3. **根签名 slot 编号必须和 HLSL `register(b0/b1/t0/...)` 一致**——这是教学版最常见的"屏幕全黑"原因（数据没绑定上去，shader 读到默认 0）。

### 已踩坑 / 注意

1. **替换的递归替换**：`ReplaceAll` 里 `pos += to.length()` 而不是 `pos += from.length()`，避免替换内容里又含 `from` 时无限循环。例：`{{A}}` 替换成的代码里包含另一个 `{{A}}`（不太可能但 `uniforms` 生成时要注意）。
2. **标记出现在模板注释里**：模板里写 `// 这里填 {{UNIFORMS}}` 当注释，会被 `ReplaceAll` 误伤替换。**模板里不要在注释里写标记字面量**，或换成 `// UNIFORMS go here`（不带花括号）。
3. **循环依赖必须在编译期报错**，不能让 fxc 报。fxc 看到 `Local1 = Local0; Local0 = Local1;` 报的是 `undeclared identifier 'Local0'`，完全无法定位环。`TopoSortChunks` 的灰度检测在编译期就能给出 "Circular dependency at Local7"。
4. **HLSL 不允许前向调用未声明函数**：模板里 PBR 辅助函数（`DistributionGGX` 等）必须定义在 `PSMain` **之前**。如果想抽到独立 `.ush` 文件 `#include`，也要在 main 之前 include（main 之后定义 fxc 直接报错）。
5. **cbuffer packing 跨槽**：`float3 a; float b;` 在 HLSL 里 `b` 会挤进 `a` 的第 4 分量（同一 vec4 槽），但 `float3 a; float3 b;` 的 `b` 会**开新槽**（不能跨槽）。CPU 端 struct 必须按相同规则——加 `pad` 字段对齐，或全用 `float4`。课14 接 DX12 时这个坑会重踩一次。
6. **VS 输出和 PS 输入语义必须匹配**：`TEXCOORD0/1/2` 在 VS 写、PS 读，错一个 PS 那个字段就是未定义值（教学版表现为法线或 UV 全错）。
7. **生成失败要冒泡**：`HLSLGenerator::Generate` 返回 `Result{error}`，`MaterialCompiler::GenerateCode` 要把 error 写进 `error_message_` 并返回空串；`Compile` 入口检查空串设 `CompileResult.success = false`。否则下游 fxc 编译时才发现 HLSL 是空的，错误信息丢失。
8. **模板字符串的换行**：`R"hlsl(...)hlsl"` 原始字符串里换行就是 `\n`，没有转义问题。但注意模板**开头/结尾的换行**会出现在生成 HLSL 里——开头空一行无所谓，结尾空行也无所谓，**不要在原始字符串里塞 `\n` 字面量**（会被当两个字符）。
9. **入口名要和 fxc 命令行匹配**：模板用 `VSMain`/`PSMain`，fxc 调用就是 `/E VSMain` 和 `/E PSMain`。UE5 用 `MainVS`/`MainPS`——选哪套都行，**只要 shader 编译器和 C++ 端约定一致**。

### UE5 深度对照（带 Engine 相对路径）

| 我们的组件 | UE5 对应 | 路径 |
|------------|----------|------|
| `HLSLTemplate::GetFullShaderTemplate` | `MaterialTemplate.ush`（占位标记 `%XXX%`） | `Engine/Shaders/Private/MaterialTemplate.ush` |
| `HLSLGenerator::Generate` | `FHLSLMaterialTranslator::GenerateHLSLFunction` | `Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp`（搜 `GenerateHLSLFunction`） |
| 材质属性 → 标记填充 | `GetMaterialOutputInfo` + 各 `%PIXELMATERIALINPUTS_*%` | `Engine/Source/Runtime/Engine/Private/Materials/MaterialShader.cpp`（搜 `GetMaterialOutputInfo`） |
| `UniformCollector::Generate` | `GenerateParameterUniformExpressions` + `FMaterialUniformBufferLayout` | `Engine/Source/Runtime/Engine/Private/Materials/MaterialUniformExpressions.cpp` |
| `TopoSortChunks`（DFS 后序） | UE 不显式拓扑——靠 `SharedLookupId` 入队顺序保证 | `Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp`（搜 `SharedLookupId`） |
| `{{MATERIAL_BODY_VS}}`（WorldPositionOffset） | `%WORLDPOSITIONOFFSET%` | `MaterialTemplate.ush`（搜 `WorldPositionOffset`） |
| `cbuffer SceneConstants : register(b0)` | `ViewUniformBuffer` / `FViewUniformShaderParameters` | `Engine/Source/Runtime/Renderer/Private/ViewUniformBuffer.cpp` |
| `VSMain` / `PSMain` 入口 | `MainVS` / `MainPS` | `MaterialTemplate.ush` 末尾 |

**`FHLSLMaterialTranslator::GenerateHLSLFunction` 做什么**（核心对照）：
1. 调用各材质属性的 `GetMaterialOutputInfo`，确定该属性是否被材质图覆盖、对应的 chunk 是哪个
2. 把 `MaterialTemplate.ush` 里 `%PIXELMATERIALINPUTS_*%` 替换成"chunk 的 LocalX 赋值"
3. `%PARAMETER_DECLARATION%` 替换成 uniform 列表（参数系统收集）
4. `%MATERIAL_BODY%` 替换成拓扑排序后的局部变量声明
5. 输出最终 HLSL 文本

我们的 `HLSLGenerator::Generate` 是这个流程的精简版——5 步对应 5 个标记替换。

**`MaterialTemplate.ush` 为什么那么大**（几千行 vs 我们 ~120 行）：
- 材质域分支：Surface / Unlit / Decal / PostProcess / UserInterface / Volume（6 种 PS 结构）
- 混合模式分支：Opaque / Masked / Translucent / Additive / Modulate（影响 blend state 和 PS 输出）
- 高级特性：Substrate 完整运行时（教学版在课 20 以桩函数体验节点通路，见 lesson20 的 Substrate BSDF 节点族）、虚拟纹理输出、光线追踪、自动导数（`ddx`/`ddy`）
- 多 shader model 分支：SM5 / SM6 / ES3.1 等

教学版只保留 Surface + Opaque + 简单 PBR，所以 120 行够。要扩，每个分支加一个新标记 + 模板里 `#if` 分支即可——这是模板化方案的可扩展性红利。

> **搜索关键词**（UE 源码）：`GenerateHLSLFunction`、`GetMaterialOutputInfo`、`%PIXELMATERIALINPUTS_BASECOLOR%`、`MaterialTemplate.ush`、`ReplaceParameter`、`SharedLookupId`、`FMaterialUniformBufferLayout`。

### 何时落地

| 阶段 | 做什么 |
|------|--------|
| **课8（本课）** | 写出 `HLSLTemplate` + `UniformCollector` + `TopoSortChunks` + `HLSLGenerator`，用 `fxc` 离线验证生成的 HLSL 能编 |
| **课9** | 端到端：图 → 编译 → GenerateCode → 把 HLSL 写文件，肉眼/工具验证 |
| **课14–15** | DX12 接入：根签名绑定 b0/b1/t0/s0、input layout 匹配 `VS_INPUT`、CBV 上传 |
| **课17（3D 预览）** | 真正用生成的 shader 渲染一个 PBR 球/模型，验证视觉效果 |
| **课20（参数系统）** | `UniformCollector` 接材质参数节点，cbuffer 真正被填充 |

课8 阶段的核心成果：**编译器吐出的 HLSL 文本本身是合法、可编译、结构完整**——视觉验证留给 DX12 上下文就绪后。

---

## 验证

编译复杂一点的图：
```
Constant3(1,0,0) → Multiply(0.5) → Add(0.1) → BaseColor
```

验证输出的 HLSL 包含：
- VS + PS 双入口（`VSMain` / `PSMain`，各自带语义）
- 按拓扑顺序排列的 float3 变量声明
- PBR 光照计算代码
- 没有语法错误——用 fxc 分别编两个入口：`fxc /T vs_5_0 /E VSMain out.hlsl` 和 `fxc /T ps_5_0 /E PSMain out.hlsl`

---

## UE5 参考（相对 `Engine/` 路径）

- `Engine/Shaders/Private/MaterialTemplate.ush` — UE5 的材质模板（本课 HLSLTemplate 的对标）
- `Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp` 搜索 `GetMaterialShaderCode` — 最终代码拼装

### 对照 UE `MaterialTemplate.ush`（标记注入机制）

UE 的材质模板和我们用**同样的"标记注入"思路**——模板里有占位标记，编译器把生成的代码填进去：

| 我们的标记 | UE 的标记（`MaterialTemplate.ush`）| 填什么 |
|------------|--------------------------------------|--------|
| `{{UNIFORMS}}` | `%PARAMETER_DECLARATION%` | 材质 uniform 参数声明（cbuffer）|
| `{{MATERIAL_FUNCTIONS}}` | `%MATERIAL_BODY%`（局部变量声明 Local0/Local1...）| 编译器生成的中间变量 |
| `{{MATERIAL_BODY_VS}}` / `{{MATERIAL_BODY_PS}}` | `%WORLDPOSITIONOFFSET%` / 各 `%PIXELMATERIAL_...%` | 材质属性赋值 |

**关键差异**：
1. **UE 的 `MaterialTemplate.ush` 是 `.ush`（header）**，用 `#include` 进 translator 生成的 .usf，标记是 `%NAME%` 格式，靠字符串替换填充。我们是 `R"hlsl(...)hlsl"` 原始字符串 + `{{NAME}}` 替换，本质一样。
2. **UE 模板远比我们的复杂**：含 Substrate、虚拟纹理、光线追踪、各材质域（Surface/Unlit/PostProcess）的分支、自动导数等几千行。我们的只保留核心 PBR + 几个标记（教学够用）。
3. **PBR 函数**（DistributionGGX/GeometrySmith/FresnelSchlick）我们直接写进模板；UE 的在 `Engine/Shaders/Private/BRDF.ush` 等单独文件 `#include`。

> **搜索关键词**（UE 源码）：`MaterialTemplate.ush`、`GetMaterialShaderCode`、`%MATERIAL_BODY%`、`ReplaceParameter`。

注意：UE5 内部用 HLSL 作为主要着色语言，DX12 后端直接编译 HLSL 字节码（fxc/DXC），不需要 GLSL 转换——这也是我们选 DX12 + HLSL 的原因。

---

## 完成标志

- [ ] `HLSLTemplate` 提供 VS+PS 一体模板（含双 cbuffer / `VS_INPUT`·`PS_INPUT` / PBR 辅助函数 / `VSMain`·`PSMain`）
- [ ] `TopoSortChunks` 按 `CodeChunk::references` DFS 后序排序，生成的局部变量声明顺序合法（无 `undeclared identifier`）
- [ ] 拓扑排序检测循环依赖，错误信息带 chunk 符号名（`Circular dependency at Local7`）
- [ ] 导数双轨发射：`Params.bAnalyticDerivatives` 切换 `chunk.AtCode()` 的 finite/analytic 版本（字段课 6 已备好；DerivativeAutogen 课 20 接通前 analytic 为空自动回落 finite）
- [ ] 中间块剔除：`is_intermediate` 且无引用者的 chunk 不进声明段（对照 UE bIntermediate 语义）
- [ ] `UniformCollector` 能输出 `cbuffer MaterialParams : register(b1)` + 纹理声明（描述由参数系统提供，本课验证生成侧）
- [ ] 所有材质属性（BaseColor/Metallic/Roughness/Normal/Emissive/Opacity/AO/WorldPositionOffset）都能正确注入对应入口
- [ ] 生成失败（如循环依赖）通过 `Result.error` 冒泡到 `CompileResult.error_message`，不丢失
- [ ] 生成的 HLSL 用 `fxc /T vs_5_0 /E VSMain` 和 `fxc /T ps_5_0 /E PSMain` 都能离线编过（无语法错误）
