# 课8：HLSL 代码生成

## 目标

将编译器生成的代码块组装成完整的 HLSL 着色器，可以直接用于 DX12 预览渲染。

---

## 背景知识

课6中 `GenerateCode()` 只生成了变量声明。一个完整的像素着色器还需要：
1. cbuffer 常量缓冲声明
2. 顶点着色器传来的插值变量
3. PBR 光照计算代码
4. main() 函数入口（带 SV_Target 语义）

UE5 用 `MaterialTemplate.ush` 作为骨架，编译器生成的代码注入到特定位置。我们用同样的方式。

由于我们使用 DX12 作为渲染后端，着色器直接使用 HLSL。DX12 原生支持 HLSL，不需要 GLSL 转换。

---

## 操作步骤

### 1. 创建文件

```
src/Compiler/Public/HLSLTemplate.h
src/Compiler/Private/HLSLTemplate.cpp
src/Compiler/Public/HLSLGenerator.h
src/Compiler/Private/HLSLGenerator.cpp
resources/shaders/preview.vs.hlsl
resources/shaders/preview.ps.hlsl
```

### 2. HLSLTemplate.h/cpp

提供着色器模板字符串：

```cpp
#pragma once
#include <string>

class HLSLTemplate {
public:
    static std::string GetPixelShaderTemplate() {
        return R"hlsl(
// === 像素着色器模板 ===

struct PS_INPUT {
    float4 pos       : SV_POSITION;
    float3 worldPos  : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 uv        : TEXCOORD2;
};

cbuffer SceneConstants : register(b0) {
    float4x4 viewProj;
    float4x4 model;
    float4 cameraPos;
    float4 lightDir;
    float4 lightColor;
    float  time;
    float3 _pad;
};

// === 材质参数 ===
{{UNIFORMS}}

static const float PI = 3.14159265359;

// === 材质计算函数（编译器注入的 Local0/Local1...）===
{{MATERIAL_FUNCTIONS}}

// === PBR 辅助函数（必须定义在 main 之前，HLSL 不允许前向调用未声明函数）===
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float4 main(PS_INPUT input) : SV_Target {
    // 计算材质属性
    float3 BaseColor = float3(0.0, 0.0, 0.0);
    float  Metallic = 0.0;
    float  Specular = 0.5;
    float  Roughness = 0.5;
    float3 Normal = normalize(input.normal);
    float3 EmissiveColor = float3(0.0, 0.0, 0.0);
    float  AO = 1.0;
    float3 WorldPositionOffset = float3(0.0, 0.0, 0.0);

    {{MATERIAL_BODY}}

    // PBR 光照计算
    float3 N = normalize(Normal);
    float3 V = normalize(cameraPos.xyz - input.worldPos);
    float3 L = normalize(-lightDir.xyz);
    float3 H = normalize(V + L);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), BaseColor, Metallic);

    float  NDF = DistributionGGX(N, H, Roughness);
    float  G = GeometrySmith(N, V, L, Roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - Metallic);

    float  NdotL = max(dot(N, L), 0.0);
    float3 numerator = NDF * G * F;
    float  denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    float3 specular = numerator / denominator;

    float3 Lo = (kD * BaseColor / PI + specular) * lightColor.xyz * NdotL;

    float3 ambient = float3(0.03, 0.03, 0.03) * BaseColor * AO;
    float3 color = ambient + Lo + EmissiveColor;

    // HDR -> LDR tone mapping
    color = color / (color + float3(1.0, 1.0, 1.0));
    // Gamma correction
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));

    return float4(color, 1.0);
}
)hlsl";
    }
};
```

### 3. HLSLGenerator.h/cpp

组装编译器输出和模板：

```cpp
#pragma once
#include "Compiler/Public/CodeChunk.h"
#include <map>
#include <string>
#include <vector>

class HLSLGenerator {
public:
    struct Params {
        std::map<std::string, int32_t> materialOutputs; // "BaseColor" → chunk index
        // 注意：这里用指针而非引用，因为含引用成员的结构体无法默认构造，
        // 后面的 `HLSLGenerator::Params params;` 写法会编译失败。
        // 指针可以默认初始化为 nullptr，再赋值
        const std::vector<CodeChunk>* chunks = nullptr;
    };

    static std::string Generate(const Params& params) {
        if (!params.chunks) return "";

        // 1. 生成变量声明
        std::string declarations;
        for (size_t i = 0; i < params.chunks->size(); i++) {
            const auto& chunk = (*params.chunks)[i];
            if (chunk.isInline) continue;

            // 用 TypeSystem::ToHLSLType（课6 扩展版，支持 Float/Int/Matrix 全类型，
            // 不用手写 switch——加新类型时 TypeSystem 自动覆盖）。
            // 注意：HLSLGenerator.h 要 #include "Compiler/Public/TypeSystem.h"
            declarations += std::string("    ") + TypeSystem::ToHLSLType(chunk.type)
                          + " " + chunk.symbolName + " = " + chunk.code + ";\n";
        }

        // 2. 生成材质属性赋值
        std::string body;
        for (const auto& [name, idx] : params.materialOutputs) {
            std::string code = GetChunkCode(*params.chunks, idx);
            if (name == "BaseColor") body += "    BaseColor = " + code + ";\n";
            else if (name == "Metallic") body += "    Metallic = " + code + ";\n";
            else if (name == "Roughness") body += "    Roughness = " + code + ";\n";
            else if (name == "Normal") body += "    Normal = " + code + ";\n";
            else if (name == "EmissiveColor") body += "    EmissiveColor = " + code + ";\n";
            else if (name == "AmbientOcclusion") body += "    AO = " + code + ";\n";
            else if (name == "Opacity") {}  // 暂不处理
            else if (name == "Specular") body += "    Specular = " + code + ";\n";
        }

        // 3. 填充模板
        std::string template_ = HLSLTemplate::GetPixelShaderTemplate();
        ReplaceAll(template_, "{{UNIFORMS}}", "");
        ReplaceAll(template_, "{{MATERIAL_FUNCTIONS}}", declarations);
        ReplaceAll(template_, "{{MATERIAL_BODY}}", body);

        return template_;
    }

private:
    static std::string GetChunkCode(const std::vector<CodeChunk>& chunks, int32_t idx) {
        if (idx < 0 || idx >= (int32_t)chunks.size()) return "0.0";
        const auto& chunk = chunks[idx];
        return chunk.isInline ? chunk.code : chunk.symbolName;
    }

    static void ReplaceAll(std::string& str, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
    }
};
```

### 4. 修改 MaterialCompiler — 使用 HLSLGenerator

替换 `MaterialCompiler::GenerateCode()`：

```cpp
#include "Compiler/Public/HLSLGenerator.h"

std::string MaterialCompiler::GenerateCode(const std::map<std::string, int32_t>& outputs) {
    HLSLGenerator::Params params;
    params.materialOutputs = outputs;
    params.chunks = &chunks_;  // 取地址（Params 中是指针）
    return HLSLGenerator::Generate(params);
}
```

### 5. 顶点着色器 resources/shaders/preview.vs.hlsl

```hlsl
struct VS_INPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

struct VS_OUTPUT {
    float4 pos       : SV_POSITION;
    float3 worldPos  : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 uv        : TEXCOORD2;
};

cbuffer SceneConstants : register(b0) {
    float4x4 viewProj;
    float4x4 model;
    float4 cameraPos;
    float4 lightDir;
    float4 lightColor;
    float  time;
    float3 _pad;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.worldPos = worldPos.xyz;
    output.normal = mul((float3x3)model, input.normal);
    output.uv = input.uv;
    output.pos = mul(viewProj, worldPos);
    return output;
}
```

---

## 验证

编译复杂一点的图：
```
Constant3(1,0,0) → Multiply(0.5) → Add(0.1) → BaseColor
```

验证输出的 HLSL 包含：
- 正确的 float3 变量声明
- PBR 光照计算代码
- main() 函数完整（带 SV_Target 语义）
- 没有语法错误（可以用 fxc /T ps_5_0 编译验证）

---

## UE5 参考（相对 `Engine/` 路径）

- `Engine/Shaders/Private/MaterialTemplate.ush` — UE5 的材质模板（本课 HLSLTemplate 的对标）
- `Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp` 搜索 `GetMaterialShaderCode` — 最终代码拼装

### 对照 UE `MaterialTemplate.ush`（占位符注入机制）

UE 的材质模板和我们用**同样的"占位符注入"思路**——模板里有占位标记，编译器把生成的代码填进去：

| 我们的占位符 | UE 的占位符（`MaterialTemplate.ush`）| 填什么 |
|------------|--------------------------------------|--------|
| `{{UNIFORMS}}` | `%PARAMETER_DECLARATION%` | 材质 uniform 参数声明（cbuffer）|
| `{{MATERIAL_FUNCTIONS}}` | `%MATERIAL_BODY%`（局部变量声明 Local0/Local1...）| 编译器生成的中间变量 |
| `{{MATERIAL_BODY}}` | 各 `%PIXELMATERIAL_...%`（BaseColor/Metallic/...）| 材质属性赋值 |

**关键差异**：
1. **UE 的 `MaterialTemplate.ush` 是 `.ush`（header）**，用 `#include` 进 translator 生成的 .usf，占位符是 `%NAME%` 格式，靠字符串替换填充。我们是 `R"hlsl(...)hlsl"` 原始字符串 + `{{NAME}}` 替换，本质一样。
2. **UE 模板远比我们的复杂**：含 Substrate、虚拟纹理、光线追踪、各材质域（Surface/Unlit/PostProcess）的分支、自动导数等几千行。我们的只保留核心 PBR + 几个占位符（教学够用）。
3. **PBR 函数**（DistributionGGX/GeometrySmith/FresnelSchlick）我们直接写进模板；UE 的在 `Engine/Shaders/Private/BRDF.ush` 等单独文件 `#include`。

> **搜索关键词**（UE 源码）：`MaterialTemplate.ush`、`GetMaterialShaderCode`、`%MATERIAL_BODY%`、`ReplaceParameter`。

注意：UE5 内部用 HLSL 作为主要着色语言，DX12 后端直接编译 HLSL 字节码（fxc/DXC），不需要 GLSL 转换——这也是我们选 DX12 + HLSL 的原因。

---

## 完成标志

- [ ] 生成的 HLSL 是完整的、可编译的像素着色器
- [ ] PBR 光照代码正确（metallic/non-metallic 区分明显）
- [ ] 多节点图的编译结果正确
- [ ] DX12 直接使用 HLSL，不需要 GLSL 转换
