# 课20：扩展功能（更多表达式、多平台输出、着色器统计）

## 目标

扩展编辑器功能：添加更多表达式节点、多平台着色器输出、着色器统计面板。

---

## 背景知识

经过课1-19，我们已经有了一个功能完整的材质编辑器。本课添加锦上添花的功能，让编辑器更接近 UE5 的能力。

### 更多表达式

UE5 有 200+ 种表达式，我们目前只有 10 个基础数学表达式。按优先级添加：

**第二优先级**（参数和纹理）：
- ScalarParameter — 可命名的标量参数
- VectorParameter — 可命名的向量参数
- TextureSample — 纹理采样
- TextureCoordinate — UV 坐标

**第三优先级**（向量操作）：
- ComponentMask — 分量掩码
- AppendVector — 向量拼接
- OneMinus — 1 - x
- Saturate — clamp(x, 0, 1)

**第四优先级**（工具）：
- Time — 时间
- Fresnel — 菲涅尔效应
- If — 条件分支
- Noise — 噪声函数

### 多平台输出

UE5 支持多平台着色器编译（PC=D3D12/HLSL, Mobile=GLSL/Vulkan, Mac=Metal）。我们的编辑器以 HLSL 为主格式（DX12 预览直接使用），同时提供可选的其他格式输出：
- HLSL（默认，DX12 预览和导出用）
- GLSL（可选，用于 Vulkan 后续扩展）
- SPIR-V（可选，通过 glslang 编译）

### 着色器统计

UE5 材质编辑器底部有统计面板，显示：
- 指令数（Shader Instructions）
- 纹理采样次数
- 插值器数量
- 是否使用特定特性

---

## 操作步骤

### 第一部分：更多表达式

所有新增表达式都遵循课5 / 课7 的**反射驱动模式**：
- 用 `ME_BEGIN_CLASS / ME_DISPLAY_NAME / ME_CATEGORY / ME_FIELD / ME_END_CLASS` 宏声明元数据
- 字段全部用 `ME_FIELD` 注册（无需手写 Get/Set/GetParameter）
- 只实现引脚布局和 Compile

#### 1. ScalarParameter

```
src/Expression/Public/Parameters/ExprScalarParameter.h
```

```cpp
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Compiler/Public/MaterialCompiler.h"

class ExprScalarParameter : public Expression {
public:
    float value_ = 0.0f;
    std::string paramName_ = "Param";

    ME_BEGIN_CLASS(ExprScalarParameter)
        ME_DISPLAY_NAME("ScalarParameter")
        ME_CATEGORY("Parameters")
        ME_FIELD(ExprScalarParameter, value_, 0.0f)
        ME_FIELD(ExprScalarParameter, paramName_, "Param")
    ME_END_CLASS(ExprScalarParameter)

    std::vector<ExpressionPinDesc> GetInputPins() const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {{"Value", EValueType::Float1, ""}};
    }

    std::vector<int32_t> Compile(MaterialCompiler* c, Node*) const override {
        return {c->Parameter(paramName_, value_)};
    }
};
```

> **注意**：需要在 `MaterialCompiler` 中添加 `Parameter()` 方法，生成 `uniform float ParamName;` 声明并返回引用该 uniform 的代码块。

#### 2. VectorParameter

```
src/Expression/Public/Parameters/ExprVectorParameter.h
```

```cpp
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Compiler/Public/MaterialCompiler.h"

class ExprVectorParameter : public Expression {
public:
    float r_ = 1.0f, g_ = 1.0f, b_ = 1.0f;
    std::string paramName_ = "Color";

    ME_BEGIN_CLASS(ExprVectorParameter)
        ME_DISPLAY_NAME("VectorParameter")
        ME_CATEGORY("Parameters")
        ME_FIELD(ExprVectorParameter, paramName_, "Color")
        ME_FIELD(ExprVectorParameter, r_, 1.0f)
        ME_FIELD(ExprVectorParameter, g_, 1.0f)
        ME_FIELD(ExprVectorParameter, b_, 1.0f)
    ME_END_CLASS(ExprVectorParameter)

    std::vector<ExpressionPinDesc> GetInputPins() const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {{"Output", EValueType::Float3, ""}};
    }

    std::vector<int32_t> Compile(MaterialCompiler* c, Node*) const override {
        return {c->Parameter3(paramName_, r_, g_, b_)};
    }
};
```

#### 3. TextureSample

```
src/Expression/Public/Texture/ExprTextureSample.h
```

```cpp
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Compiler/Public/MaterialCompiler.h"

class ExprTextureSample : public Expression {
public:
    std::string textureName_ = "Texture";

    ME_BEGIN_CLASS(ExprTextureSample)
        ME_DISPLAY_NAME("TextureSample")
        ME_CATEGORY("Texture")
        ME_FIELD(ExprTextureSample, textureName_, "Texture")
    ME_END_CLASS(ExprTextureSample)

    std::vector<ExpressionPinDesc> GetInputPins() const override {
        return {{"UVs", EValueType::Float2, "0.0"}};  // 默认使用自动 UV
    }
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {
            {"RGB", EValueType::Float3, ""},
            {"R",   EValueType::Float1, ""},
            {"G",   EValueType::Float1, ""},
            {"B",   EValueType::Float1, ""},
            {"A",   EValueType::Float1, ""}
        };
    }

    std::vector<int32_t> Compile(MaterialCompiler* c, Node* node) const override {
        int32_t uv = c->CompileInputPin(node, "UVs");
        if (uv < 0) uv = c->TextureCoordinate();
        return c->TextureSample(textureName_, uv);
    }
};
```

> **注意**：`MaterialCompiler::TextureSample()` 需要修改为接受纹理名称参数，并在 `HLSLGenerator` 中生成 `uniform sampler2D TextureName;` 声明。

#### 4. TextureCoordinate

```cpp
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Compiler/Public/MaterialCompiler.h"

class ExprTextureCoordinate : public Expression {
public:
    int32_t uvIndex_ = 0;

    ME_BEGIN_CLASS(ExprTextureCoordinate)
        ME_DISPLAY_NAME("TexCoord")
        ME_CATEGORY("Texture")
        ME_FIELD(ExprTextureCoordinate, uvIndex_, 0)
    ME_END_CLASS(ExprTextureCoordinate)

    std::vector<ExpressionPinDesc> GetInputPins() const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {{"UVs", EValueType::Float2, ""}};
    }

    std::vector<int32_t> Compile(MaterialCompiler* c, Node*) const override {
        return {c->TextureCoordinate()};
    }
};
```

#### 5. 其他简单表达式（模式相同）

```cpp
// ExprOneMinus
std::vector<int32_t> Compile(MaterialCompiler* c, Node* node) const override {
    int32_t x = c->CompileInputPin(node, "X");
    return {c->Subtract(c->Constant(1.0f), x)};
}

// ExprSaturate
std::vector<int32_t> Compile(MaterialCompiler* c, Node* node) const override {
    int32_t x = c->CompileInputPin(node, "X");
    return {c->Clamp(x, c->Constant(0.0f), c->Constant(1.0f))};
}

// ExprComponentMask
std::vector<int32_t> Compile(MaterialCompiler* c, Node* node) const override {
    int32_t input = c->CompileInputPin(node, "Input");
    return {c->ComponentMask(input, maskR_, maskG_, maskB_, maskA_)};
}

// ExprTime
std::vector<int32_t> Compile(MaterialCompiler* c, Node*) const override {
    return {c->Time()};
}

// ExprFresnel
std::vector<int32_t> Compile(MaterialCompiler* c, Node* node) const override {
    int32_t exp = c->CompileInputPin(node, "Exponent");
    return {c->Fresnel(exp)};
}
```

#### 6. 在 MaterialCompiler 中添加新方法

```cpp
// MaterialCompiler.h 添加

// 参数 uniform
int32_t Parameter(const std::string& name, float defaultValue);
int32_t Parameter3(const std::string& name, float r, float g, float b);

// 时间
int32_t Time();

// 菲涅尔
int32_t Fresnel(int32_t exponent);

// 纹理采样（修改版，支持纹理名）
std::vector<int32_t> TextureSample(const std::string& textureName, int32_t uv);
```

```cpp
// MaterialCompiler.cpp 实现

int32_t MaterialCompiler::Parameter(const std::string& name, float defaultValue) {
    // 记录 uniform 声明（HLSL 语法，放在 cbuffer 中）
    uniforms_.push_back("    float " + name + " = " +
                         std::to_string(defaultValue) + ";");
    // 返回引用该 uniform 的代码块
    return AddCodeChunk(EValueType::Float1, name, true);
}

int32_t MaterialCompiler::Parameter3(const std::string& name,
                                      float r, float g, float b) {
    uniforms_.push_back("    float3 " + name + " = float3(" +
                         std::to_string(r) + ", " +
                         std::to_string(g) + ", " +
                         std::to_string(b) + ");");
    return AddCodeChunk(EValueType::Float3, name, true);
}

int32_t MaterialCompiler::Time() {
    // 使用内置 uTime uniform
    return AddCodeChunk(EValueType::Float1, "uTime", true);
}

int32_t MaterialCompiler::Fresnel(int32_t exponent) {
    std::string code = "pow(1.0 - max(dot(normalize(input.normal), "
                       "normalize(cameraPos.xyz - input.worldPos)), 0.0), " +
                       GetParameterCode(exponent) + ")";
    return AddCodeChunk(GetType(exponent), code, false);
}
```

#### 7. 注册新表达式

在 `RegisterAllExpressions()` 中添加：

```cpp
#include "Expression/Public/Parameters/ExprScalarParameter.h"
#include "Expression/Public/Parameters/ExprVectorParameter.h"
#include "Expression/Public/Texture/ExprTextureSample.h"
#include "Expression/Public/Texture/ExprTextureCoordinate.h"
#include "Expression/Public/Vector/ExprComponentMask.h"
#include "Expression/Public/Math/ExprOneMinus.h"
#include "Expression/Public/Math/ExprSaturate.h"
#include "Expression/Public/Utility/ExprTime.h"
#include "Expression/Public/Utility/ExprFresnel.h"

void RegisterAllExpressions() {
    // ... 原有 10 个 ...
    reg.Register("ExprScalarParameter", ...);
    reg.Register("ExprVectorParameter", ...);
    reg.Register("ExprTextureSample", ...);
    reg.Register("ExprTextureCoordinate", ...);
    reg.Register("ExprComponentMask", ...);
    reg.Register("ExprOneMinus", ...);
    reg.Register("ExprSaturate", ...);
    reg.Register("ExprTime", ...);
    reg.Register("ExprFresnel", ...);
}
```

---

### 第二部分：多平台输出

#### HLSL 输出（默认格式）

修改 `HLSLGenerator` 添加输出格式选项：

```cpp
class HLSLGenerator {
public:
    enum class OutputFormat {
        HLSL,   // 默认 DX12 预览
        GLSL,   // 可选，用于 Vulkan 后续扩展
        SPIRV   // 可选，通过 glslang 编译
    };

    static std::string Generate(const Params& params,
                                 OutputFormat format = OutputFormat::HLSL);
};
```

由于我们的编辑器内部编译管线（课8的 HLSLGenerator）已经直接生成 HLSL，DX12 预览可以直接使用。如果需要 GLSL 输出（例如 Vulkan 后端），可以实现一个 `ConvertHLSLToGLSL()` 函数：

```cpp
static std::string ConvertHLSLToGLSL(const std::string& hlsl) {
    std::string glsl = hlsl;

    // 添加版本声明
    glsl = "#version 450 core\n" + glsl;

    // 类型替换（HLSL → GLSL）
    ReplaceAll(glsl, "float2", "vec2");
    ReplaceAll(glsl, "float3", "vec3");
    ReplaceAll(glsl, "float4", "vec4");
    ReplaceAll(glsl, "float3x3", "mat3");
    ReplaceAll(glsl, "float4x4", "mat4");
    ReplaceAll(glsl, "Texture2D", "sampler2D");
    ReplaceAll(glsl, "lerp", "mix");
    ReplaceAll(glsl, "frac", "fract");
    ReplaceAll(glsl, "fmod", "mod");

    // 修改 cbuffer → uniform 声明
    // 修改入口函数签名
    // float4 main(PS_INPUT input) : SV_Target → void main()

    return glsl;
}
```

HLSL 和 GLSL 的主要差异（供参考）：

| 特性 | HLSL | GLSL |
|------|------|------|
| 向量类型 | float2/float3/float4 | vec2/vec3/vec4 |
| 矩阵类型 | float4x4 | mat4 |
| 纹理采样 | tex2D(sampler, uv) | texture(sampler, uv) |
| 缓冲声明 | cbuffer { ... } | uniform float3 name; |
| 入口函数 | float4 main(...) : SV_Target | void main() |
| 输入修饰 | TEXCOORD0/SV_Position | in/out |

---

### 第三部分：着色器统计

#### StatsPanel.h

```cpp
#pragma once
#include <QWidget>
#include <QTreeWidget>
#include "Compiler/Public/ShaderStats.h"

class StatsPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatsPanel(QWidget* parent = nullptr);

    void SetStats(const ShaderStats& stats);
    void Clear();

private:
    QTreeWidget* tree_;
};
```

#### ShaderStats.h

```cpp
#pragma once
#include <string>
#include <vector>

struct ShaderStats {
    int instructionCount = 0;      // 指令数
    int textureSampleCount = 0;    // 纹理采样次数
    int uniformCount = 0;          // uniform 数量
    int interpolatorCount = 0;     // 插值器数量
    bool usesWorldPosition = false;
    bool usesCameraVector = false;
    bool usesTime = false;
    bool usesNormal = true;

    // 从生成的代码中统计
    static ShaderStats Analyze(const std::string& code);

    // 基准值（和 UE5 对比）
    struct Baseline {
        int baseInstructionCount = 50;  // PBR 光照基础指令数
        int maxInstructionCount = 500;  // 建议上限
    };
};
```

#### ShaderStats.cpp

```cpp
#include "Compiler/Public/ShaderStats.h"
#include <algorithm>

ShaderStats ShaderStats::Analyze(const std::string& code) {
    ShaderStats stats;

    // 统计指令数（近似：非空行数 - 声明行 - 空行）
    int lines = 0;
    bool inMain = false;
    for (size_t i = 0; i < code.size(); ) {
        // 找到行尾
        size_t eol = code.find('\n', i);
        if (eol == std::string::npos) eol = code.size();

        std::string line = code.substr(i, eol - i);
        // 去除空格
        size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos) {
            line = line.substr(start);
            if (line.find("void main") != std::string::npos) inMain = true;
            if (inMain && !line.empty() && line[0] != '/' && line[0] != '#') {
                if (line.find("=") != std::string::npos ||
                    line.find("(") != std::string::npos) {
                    lines++;
                }
            }
        }

        i = eol + 1;
    }
    stats.instructionCount = lines;

    // 统计纹理采样（HLSL 和 GLSL 格式都检测）
    size_t pos = 0;
    while ((pos = code.find("tex2D(", pos)) != std::string::npos) {
        stats.textureSampleCount++;
        pos++;
    }
    pos = 0;
    while ((pos = code.find("texture(", pos)) != std::string::npos) {
        stats.textureSampleCount++;
        pos++;
    }

    // 统计 uniform（支持 HLSL cbuffer 风格）
    pos = 0;
    while ((pos = code.find("uniform ", pos)) != std::string::npos) {
        std::string rest = code.substr(pos + 8, 20);
        if (rest.find("uCameraPos") == std::string::npos &&
            rest.find("uLightDir") == std::string::npos &&
            rest.find("uLightColor") == std::string::npos &&
            rest.find("uTime") == std::string::npos &&
            rest.find("uViewProj") == std::string::npos &&
            rest.find("uModel") == std::string::npos &&
            rest.find("cameraPos") == std::string::npos &&
            rest.find("lightDir") == std::string::npos &&
            rest.find("lightColor") == std::string::npos &&
            rest.find("viewProj") == std::string::npos &&
            rest.find("model") == std::string::npos) {
            stats.uniformCount++;
        }
        pos++;
    }

    // 特性检测（兼容 HLSL 和 GLSL 变量名）
    stats.usesWorldPosition = code.find("worldPos") != std::string::npos ||
                              code.find("vWorldPos") != std::string::npos;
    stats.usesCameraVector = code.find("cameraPos") != std::string::npos ||
                             code.find("uCameraPos") != std::string::npos;
    stats.usesTime = code.find("time") != std::string::npos ||
                     code.find("uTime") != std::string::npos;

    return stats;
}
```

#### StatsPanel.cpp

```cpp
#include "UI/Private/Panels/StatsPanel.h"
#include <QHeaderView>
#include <QLabel>

StatsPanel::StatsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({"Property", "Value"});
    tree_->header()->setStretchLastSection(true);
    tree_->setIndentation(0);
    layout->addWidget(tree_);
}

void StatsPanel::SetStats(const ShaderStats& stats) {
    tree_->clear();

    auto addItem = [&](const QString& name, const QString& value,
                       const QColor& color = Qt::white) {
        auto* item = new QTreeWidgetItem(tree_, {name, value});
        item->setForeground(1, color);
    };

    addItem("Instructions",
            QString::number(stats.instructionCount),
            stats.instructionCount > 500 ? QColor(255, 100, 100) :
            stats.instructionCount > 300 ? QColor(255, 200, 100) :
            QColor(100, 255, 100));

    addItem("Texture Samples",
            QString::number(stats.textureSampleCount));
    addItem("Uniforms",
            QString::number(stats.uniformCount));
    addItem("Interpolators",
            QString::number(stats.interpolatorCount));

    // 特性
    auto* featureItem = new QTreeWidgetItem(tree_, {"Features", ""});
    if (stats.usesWorldPosition)
        new QTreeWidgetItem(featureItem, {"", "World Position"});
    if (stats.usesCameraVector)
        new QTreeWidgetItem(featureItem, {"", "Camera Vector"});
    if (stats.usesTime)
        new QTreeWidgetItem(featureItem, {"", "Time"});
    featureItem->setExpanded(true);
}

void StatsPanel::Clear() {
    tree_->clear();
}
```

---

### 集成到 MainWindow

```cpp
// 在 OnCompile() 中更新统计
void MainWindow::OnCompile() {
    auto result = compiler_->Compile(graph_);

    if (result.success) {
        // ... 现有逻辑 ...

        // 统计
        auto stats = ShaderStats::Analyze(result.hlslCode);
        if (statsPanel_) statsPanel_->SetStats(stats);
    }
}
```

---

## 验证

### 新表达式

1. 添加 ScalarParameter → 属性面板可编辑名称和值
2. 添加 VectorParameter → 属性面板可编辑颜色
3. 添加 TextureSample → 有 5 个输出引脚（RGB/R/G/B/A）
4. 添加 Time → 输出时间值
5. Fresnel → 视角边缘高亮效果

### 多平台输出

1. File → Export → 选择 HLSL 格式（默认）
2. 导出的 HLSL 代码使用 `float3` 语法
3. 导出的 HLSL 可被 fxc/dxc 编译器编译
4. 如果选择 GLSL 格式导出，代码使用 `vec3` 语法

### 着色器统计

1. 编译后统计面板显示指令数
2. 指令数超过 500 时变红
3. 纹理采样次数正确
4. 特性列表正确（使用 Time 时显示）

---

## UE5 参考（相对 `Engine/` 路径）

- `Engine/Source/Runtime/Engine/Private/Materials/MaterialExpressionFresnel.cpp` — 复杂表达式
- `Engine/Source/Runtime/Engine/Private/Materials/MaterialExpressionScalarParameter.cpp` — 参数表达式
- 着色器统计：搜索 `GetShaderInstructionCount` / `MaterialStats`
- 多平台：搜索 `EShaderPlatform` / `CompileShader`

### 对照 UE 扩展功能

| 我们的 | UE | 作用 |
|--------|-----|------|
| 更多 `ExprXxx`（Fresnel/Desaturation/...）| 100+ `UMaterialExpression*` | 节点库 |
| 着色器指令数统计 | `GetShaderInstructionCount` / `MaterialStats` | 性能分析 |
| 单平台（DX12）| `EShaderPlatform`（SM5/SM6/Vulkan/Metal）| 多平台编译 |

**三个关键差异**：

1. **表达式数量**：UE 有 **100+** `UMaterialExpression`（Fresnel、Desaturation、DDXY、CameraVector、Time、ViewProperty...）。我们的课7 只实现 ~10 个基础表达式——按需加，每个照课7 的反射模式（`ME_BEGIN_CLASS` + `Compile`），50-200 行/个。

2. **着色器统计**：UE 显示指令数、纹理采样数、寄存器压力、shader 复杂度警告。我们的可以加（用 DX12 shader 反射查指令数）。

3. **多平台**：UE 编译到 SM5/SM6/Vulkan/Metal/各主机后端。我们单平台（DX12/HLSL）——多平台是大工程（`lesson06-extension.md` 已排除，专注 DX12）。

### 扩展预告：错误诊断（块5）+ 材质域/混合模式（块6）

`lesson06-extension.md` 的块5/6 规划：
- **块5 错误诊断**：编译错误（类型不匹配 / 循环依赖 / 未连接必需引脚 / 除零）带**节点 + pin 定位**，编辑器据此高亮出错节点（对照 UE 的 `HandleMaterialCompilationErrors`）。
- **块6 材质域/混合模式**：`EMaterialDomain`（Surface/Unlit/PostProcess/Decal）+ `EBlendMode`（Opaque/Masked/Translucent/Additive）——决定 shader 结构（课8 的模板分支）+ blend state。

这两块在课20 阶段加（扩展功能），`lesson06-extension.md` 有详细规划。

> **搜索关键词**（UE 源码）：`MaterialExpressionFresnel`、`GetShaderInstructionCount`、`EShaderPlatform`、`EMaterialDomain`、`EBlendMode`、`HandleMaterialCompilationErrors`。

---

## 完成标志

- [ ] ScalarParameter 和 VectorParameter 可编辑和预览
- [ ] TextureSample 可采样纹理（至少代码生成正确）
- [ ] Time/Fresnel/OneMinus/Saturate 工作正确
- [ ] HLSL 导出格式正确（默认格式）
- [ ] 着色器统计面板显示指令数、纹理采样数
- [ ] 所有新表达式注册到调色板和工厂
