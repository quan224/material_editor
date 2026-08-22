# 课20：扩展功能（更多表达式、多平台输出、着色器统计）

## 目标

扩展编辑器功能：添加更多表达式节点、多平台着色器输出、着色器统计面板。

---

## 背景知识

经过课1-19，我们已经有了一个功能完整的材质编辑器。本课添加锦上添花的功能，让编辑器更接近 UE5 的能力。

### 更多表达式

UE5 有 200+ 种表达式，我们目前只有 10 个基础数学表达式。按优先级添加：

**第二优先级**（纹理）：
- TextureSample — 纹理采样
- TextureCoordinate — UV 坐标

> 参数节点（ScalarParameter / VectorParameter / StaticSwitch）不在"更多表达式"里做——它们牵扯 uniform 收集、同名合并、材质变体，统一放**第四部分（参数系统深度）**第一次实现。

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

#### 1. TextureSample

```
src/Expression/Public/Texture/ExprTextureSample.h
```

```cpp
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Expression/Public/MaterialCompiler.h"  // 抽象接口在 L4（课6 分层裁决）

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

#### 2. TextureCoordinate

```cpp
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Expression/Public/MaterialCompiler.h"  // 抽象接口在 L4（课6 分层裁决）

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

#### 3. 其他简单表达式（模式相同）

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

#### 4. 在 MaterialCompiler 中添加新方法

```cpp
// MaterialCompiler.h 添加

// 时间
int32_t Time();

// 菲涅尔
int32_t Fresnel(int32_t exponent);

// 纹理采样（修改版，支持纹理名）
std::vector<int32_t> TextureSample(const std::string& textureName, int32_t uv);
```

```cpp
// MaterialCompiler.cpp 实现

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

#### 5. 注册新表达式

在 `RegisterAllExpressions()` 中添加：

```cpp
#include "Expression/Public/Texture/ExprTextureSample.h"
#include "Expression/Public/Texture/ExprTextureCoordinate.h"
#include "Expression/Public/Vector/ExprComponentMask.h"
#include "Expression/Public/Math/ExprOneMinus.h"
#include "Expression/Public/Math/ExprSaturate.h"
#include "Expression/Public/Utility/ExprTime.h"
#include "Expression/Public/Utility/ExprFresnel.h"

void RegisterAllExpressions() {
    // ... 原有 10 个 ...
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

### 第四部分：参数系统（深度）

参数节点（`ScalarParameter` / `VectorParameter` / `TextureParameter` / `StaticSwitch`）**在本部分第一次实现**。它们和普通算子节点有本质区别：参数不是局部变量（每次材质实例化值不同），编译期必须收集成结构化数据——同名参数合并、纹理参数自动配 SamplerState、StaticSwitch 编出多个 shader 变体、UI 分组编辑、序列化。支撑这一切的是编译器的**一等数据结构** `UniformTable`，以及**材质变体**机制。

本部分对照 UE5 的 `FMaterialCompilationOutput` / `FMaterialResource` / `StaticSwitchParameter`，把参数系统讲透。深度对标课6 的编译器核心——参数系统是编译器第二大复杂块（仅次于常数折叠/类型推导）。

#### 为什么参数系统需要单独设计

**核心区别：参数节点 vs 普通节点**。

| 维度 | 普通节点（Add/Multiply/TextureSample…） | 参数节点（ScalarParameter/VectorParameter/StaticSwitch） |
|------|------|------|
| 输出确定性 | 编译期完全确定（输入连什么就编什么） | 编译期只决定"洞的位置和形状"，**值在运行期填** |
| 进 shader 的方式 | 局部变量 `float3 Local0 = ...;` | 引用一个 `cbuffer` 里的 uniform `MaterialParams.BaseColor` |
| 是否参与折叠 | 是（`Add(Constant(1), Constant(2))` → `3`） | **否**（参数是运行期值，编译期不知道具体值，不能折叠） |
| 同名合并 | 不合并（每个节点独立） | **必须合并**（同名 `BaseColor` 在图里出现 3 次，cbuffer 里只声明 1 次） |
| 影响 shader 数量 | 否 | StaticSwitch **是**——每个静态参数组合产生一个变体 |

**uniform 怎么进 cbuffer**：shader 里所有"运行期可变值"必须从某个全局缓冲读。HLSL 的 `cbuffer` 是一段按布局对齐的 GPU 内存，shader 用符号名访问它的字段。参数系统的任务就是：

1. **编译期**：扫图，收集所有参数节点 → 生成 `cbuffer MaterialParams { float BaseColor_R; float3 Tint; ... };` 声明
2. **运行期**：CPU 端把用户填的参数值写进这个 cbuffer 的对应偏移，绑定到 GPU

**静态分支为何要变体**：HLSL 的 `if (bool)` 是**动态分支**——运行期判断，shader 里两条分支都在。但材质图里的"开关"（如"是否开启菲涅尔"）用户希望是**编译期决定**——开了就编菲涅尔代码、没开就完全没这段代码，省指令、省 uniform。这就是 StaticSwitch：编译期选一条分支，另一条**根本不进 shader**。

代价：N 个静态开关有 2^N 种组合，每种组合是一个独立 shader——这就是**变体爆炸**问题。参数系统必须管理这些变体。

> **UE 对照**：UE 的 `StaticSwitchParameter` / `StaticComponentMaskParameter` 都生成材质变体（`FMaterialResource` 按 `FMaterialCompilationOutput::StaticParams` 组织）。UE 的"材质实例"（`UMaterialInstance`）允许每个实例有不同静态参数组合，引擎按需编译对应变体并缓存。位置：`Engine/Source/Runtime/Engine/Private/Materials/MaterialInstance.cpp` 的 `FMaterialResource::IsSameWithId` / `FindStaticParameterDerived`。

#### UniformTable：编译期的参数收集

**文件**：`src/Compiler/Public/UniformTable.h`（**编译器层 L5**——它生成 HLSL cbuffer，不能放 Types.h）

```cpp
#pragma once
#include "MaterialGraph/Public/Types.h"     // EValueType
#include "Core/Public/MathTypes.h"          // Vec2/3/4
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>

// 单条 uniform 项的描述（编译期收集，运行期传给 shader）
struct UniformEntry {
    std::string name;            // 参数名（用户可见，如 "BaseColor"）
    EValueType  type;            // Float1/Float3/Texture2D/SamplerState...

    // 默认值——和 CodeChunk::constant_value 同款 variant
    // （只装 float 和 Vec2-4；纹理参数的默认值用 defaultTexturePath 单独存）
    std::variant<float, Vec2, Vec3, Vec4> defaultValue{0.0f};

    std::string defaultTexturePath;   // 仅 Texture2D 参数用（TextureParameter 的默认纹理）

    // 反射/UI 元信息（和 UE 的 ParameterGroup 对应）
    std::string group      = "Default";   // UI 分组（"Color"/"Texture"/"Switch"...）
    bool        isExposed  = true;        // 是否暴露给材质实例（不暴露 = 仅图内部用）
    bool        isStatic   = false;       // 是否静态参数（StaticSwitch——不进 cbuffer，进变体 key）

    // 运行期偏移（GenerateHLSL 之后由 Compile 阶段填，给 CPU 端写 cbuffer 用）
    uint32_t    cbufferOffset = 0;
};

// UniformTable——编译期收集所有参数，生成 cbuffer 声明 + 运行期绑定元数据
class UniformTable {
public:
    // 注册一个参数。三种情况：
    //   ① 新名字 → 加入 entries_
    //   ② 同名同类型已存在 → 合并（保留第一次的默认值，参考 UE 行为）
    //   ③ 同名不同类型 → 返回 false（调用方报编译错误："参数 X 类型冲突"）
    bool Register(const UniformEntry& entry);

    // 查询：参数是否已注册
    const UniformEntry* Find(const std::string& name) const;

    // 生成 HLSL——分两段返回（cbuffer 段 + 纹理/sampler 段，前者进 cbuffer 块，后者是独立声明）
    struct HLSLDeclaration {
        std::string cbufferBody;     // cbuffer MaterialParams { ... } 的内部字段
        std::string textureDecls;    // Texture2D xxx; SamplerState xxxSampler;
    };
    HLSLDeclaration GenerateHLSL() const;

    const std::vector<UniformEntry>& Entries() const { return entries_; }
    size_t Size() const { return entries_.size(); }

private:
    std::vector<UniformEntry>                  entries_;
    std::unordered_map<std::string, size_t>    nameIndex_;   // name → entries_ 下标
};
```

**实现要点**：

```cpp
// UniformTable.cpp
bool UniformTable::Register(const UniformEntry& entry) {
    auto it = nameIndex_.find(entry.name);
    if (it != nameIndex_.end()) {
        const UniformEntry& existing = entries_[it->second];
        // 情况 ③：同名不同类型 → 冲突
        if (existing.type != entry.type) return false;
        // 情况 ②：同名同类型 → 合并（保留第一次的默认值，丢弃后来的）
        // 注意：不更新 defaultValue，参考 UE 的行为——第一个出现的节点"拥有"默认值
        return true;
    }
    // 情况 ①：新参数
    nameIndex_[entry.name] = entries_.size();
    entries_.push_back(entry);
    return true;
}

const UniformEntry* UniformTable::Find(const std::string& name) const {
    auto it = nameIndex_.find(name);
    return it != nameIndex_.end() ? &entries_[it->second] : nullptr;
}

UniformTable::HLSLDeclaration UniformTable::GenerateHLSL() const {
    HLSLDeclaration out;
    uint32_t offset = 0;

    for (const auto& e : entries_) {
        // 静态参数（StaticSwitch）不进 cbuffer——它影响变体选择，不在 shader 里有字段
        if (e.isStatic) continue;

        if (e.type == EValueType::Texture2D) {
            // 纹理和 sampler 独立声明（DX12 风格，不放进 cbuffer）
            out.textureDecls += "Texture2D    " + e.name + ";\n";
            out.textureDecls += "SamplerState " + e.name + "Sampler;\n";
            continue;   // 不占 cbuffer 偏移
        }
        if (e.type == EValueType::SamplerState) {
            // 已经在配对的 Texture2D 里一起声明了——跳过避免重复
            continue;
        }

        // cbuffer 字段：float / float3 / float4...
        const char* hlslType = TypeSystem::ToHLSLType(e.type);
        out.cbufferBody += std::string("    ") + hlslType + " " + e.name + ";\n";
        // 注意：这里没做 std140 / packoffset 对齐——教学版用 HLSL 的默认 cbuffer layout
        // （HLSL 会自动按 16 字节对齐 vec4、vec3 单独占 16 字节）。UE 用 ConstantBufferLayout
        // 显式算偏移，教学版交给 FXC/DXC 编译器处理。
    }
    return out;
}
```

**cbuffer 最终长这样**（由课8 的 HLSLGenerator 把 `cbufferBody` 嵌入模板）：

```hlsl
cbuffer MaterialParams : register(b1)   // b0 留给引擎全局的 ViewProj/Camera
{
    float  ScalarParam;
    float3 ColorParam;       // 注意：HLSL cbuffer 里 float3 实际占 16 字节
    float4 AnotherVec;
};
Texture2D    AlbedoTex;
SamplerState AlbedoTexSampler;
```

#### 参数节点详解（Expression 子类骨架）

每个参数节点都是 `Expression` 子类，遵循课5/课7 的反射驱动模式：`ME_BEGIN_CLASS` + `ME_FIELD` 注册字段、`GetInputPins/GetOutputPins/Compile` 手写。**关键区别**：参数节点的 `Compile` 要做两件事——(1) 把自己注册进 `UniformTable`，(2) 输出一个引用该 uniform 的 chunk。

##### ScalarParameter（float uniform）

```cpp
// src/Expression/Public/Parameters/ExprScalarParameter.h
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Expression/Public/MaterialCompiler.h"  // 抽象接口在 L4（课6 分层裁决）

class ExprScalarParameter : public Expression {
public:
    std::string paramName_     = "Scalar";   // 参数名（用户可改，决定 cbuffer 字段名）
    float       defaultValue_  = 0.0f;       // 默认值（cbuffer 初始化、UI 显示）

    ME_BEGIN_CLASS(ExprScalarParameter)
        ME_DISPLAY_NAME("ScalarParameter")
        ME_CATEGORY("Parameters")
        ME_FIELD(ExprScalarParameter, paramName_,    "Scalar")    // 字符串字段 → StringProperty
        ME_FIELD(ExprScalarParameter, defaultValue_, 0.0f)        // float 字段 → FloatProperty
    ME_END_CLASS(ExprScalarParameter)

    std::vector<ExpressionPinDesc> GetInputPins() const override { return {}; }   // 纯输出节点
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {{"Value", EValueType::Float1, ""}};
    }

    std::vector<int32_t> Compile(MaterialCompiler* c, Node*) const override {
        // 1. 注册进 UniformTable（同名同类型自动合并，不会重复声明）
        UniformEntry entry;
        entry.name         = paramName_;
        entry.type         = EValueType::Float1;
        entry.defaultValue = defaultValue_;
        entry.group        = "Scalar";
        if (!c->GetUniformTable().Register(entry)) {
            c->ReportError("Parameter '" + paramName_ + "' type conflict (Float1 vs other)");
            return {-1};
        }

        // 2. 输出一个引用该 uniform 的 chunk——code 就是参数名本身（直接读 cbuffer 字段）
        //    用 AddInlinedCodeChunk：参数名是短表达式，不需要 "float Local0 = ParamName;"
        //    这种额外声明，直接在使用处嵌入（如 "ParamName * 2.0"）
        return { c->AddInlinedCodeChunk(EValueType::Float1, paramName_) };
    }
};
```

> **命名说明**：字段名用 `defaultValue_` 而不是 `value_`——这个值是 cbuffer 的**默认初始值**，运行期 UI 改了就覆盖，名字要体现这一点。

##### VectorParameter（float3/float4 uniform）

```cpp
// src/Expression/Public/Parameters/ExprVectorParameter.h
class ExprVectorParameter : public Expression {
public:
    std::string paramName_   = "Color";
    Vec3        defaultValue_ = Vec3(1.0f, 1.0f, 1.0f);   // 用 Vec3 字段（项目已有 Vec3Property）

    ME_BEGIN_CLASS(ExprVectorParameter)
        ME_DISPLAY_NAME("VectorParameter")
        ME_CATEGORY("Parameters")
        ME_FIELD(ExprVectorParameter, paramName_,    "Color")
        ME_FIELD(ExprVectorParameter, defaultValue_, Vec3(1,1,1))   // Vec3 字段 → Vec3Property
    ME_END_CLASS(ExprVectorParameter)

    std::vector<ExpressionPinDesc> GetInputPins() const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {{"XYZ", EValueType::Float3, ""}};
    }

    std::vector<int32_t> Compile(MaterialCompiler* c, Node*) const override {
        UniformEntry entry;
        entry.name         = paramName_;
        entry.type         = EValueType::Float3;
        entry.defaultValue = defaultValue_;     // variant 能装 Vec3
        entry.group        = "Vector";
        if (!c->GetUniformTable().Register(entry)) {
            c->ReportError("Parameter '" + paramName_ + "' type conflict");
            return {-1};
        }
        return { c->AddInlinedCodeChunk(EValueType::Float3, paramName_) };
    }
};
```

> **Vec4 参数**：如果需要 float4（带 alpha 的颜色），加一个 `ExprVector4Parameter`，把 `EValueType::Float4` + `Vec4` 默认值。Vec4Property 项目里已有。用合并的 `Vec3 defaultValue_` 而不是三个分离的 `r_/g_/b_` 字段更干净（一个颜色字段在 UI 里显示成颜色选择器，三个 float 字段是三个滑块）。

##### TextureParameter（Texture2D + SamplerState 配对）

纹理参数最坑的地方：**Texture2D 必须配一个 SamplerState 才能采样**，且配对关系要稳定（同一个纹理参数每次编译产生的 sampler 名必须一致，否则 CPU 端绑定会错乱）。命名约定：纹理参数名 + `"Sampler"`。

```cpp
// src/Expression/Public/Parameters/ExprTextureParameter.h
class ExprTextureParameter : public Expression {
public:
    std::string paramName_           = "Texture";
    std::string defaultTexturePath_  = "";      // 默认纹理资源路径（运行期可换）

    ME_BEGIN_CLASS(ExprTextureParameter)
        ME_DISPLAY_NAME("TextureParameter")
        ME_CATEGORY("Parameters")
        ME_FIELD(ExprTextureParameter, paramName_,          "Texture")
        ME_FIELD(ExprTextureParameter, defaultTexturePath_, "")
    ME_END_CLASS(ExprTextureParameter)

    std::vector<ExpressionPinDesc> GetInputPins() const override {
        return {{"UVs", EValueType::Float2, ""}};   // 可选 UV 输入（不连 → 用默认 UV）
    }
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {
            {"RGB", EValueType::Float3, ""},
            {"R",   EValueType::Float1, ""},
            {"G",   EValueType::Float1, ""},
            {"B",   EValueType::Float1, ""},
            {"A",   EValueType::Float1, ""},
        };
    }

    std::vector<int32_t> Compile(MaterialCompiler* c, Node* node) const override {
        // 1. 注册纹理参数
        UniformEntry texEntry;
        texEntry.name              = paramName_;
        texEntry.type              = EValueType::Texture2D;
        texEntry.defaultTexturePath = defaultTexturePath_;
        texEntry.group             = "Texture";
        if (!c->GetUniformTable().Register(texEntry)) {
            c->ReportError("Parameter '" + paramName_ + "' type conflict");
            return {-1};
        }

        // 2. 注册配对的 sampler（命名约定：纹理名 + "Sampler"）
        //    UE 不强制命名约定（用户可选 sampler），教学版用约定减少 UI 负担
        UniformEntry sampEntry;
        sampEntry.name  = paramName_ + "Sampler";
        sampEntry.type  = EValueType::SamplerState;
        sampEntry.group = "Texture";
        c->GetUniformTable().Register(sampEntry);   // sampler 通常没冲突风险

        // 3. 编译 UV 输入（没连 → 用默认 UV0）
        int32_t uv = c->CompileInputPin(node, "UVs");
        if (uv < 0) uv = c->TextureCoordinate();

        // 4. 发射采样调用——返回 5 个 chunk（RGB / R / G / B / A）
        //    GenerateHLSL 后形如：
        //    float4 Local0 = AlbedoTex.Sample(AlbedoTexSampler, input.uv);
        //    float3 Local1 = Local0.rgb;
        //    float  Local2 = Local0.r; ...
        std::string sampleExpr = paramName_ + ".Sample(" + paramName_ + "Sampler, " +
                                 c->GetParameterCode(uv) + ")";
        int32_t sampleChunk = c->AddCodeChunk(EValueType::Float4, sampleExpr, /*is_inline=*/false);

        // 拆分 RGBA → 5 个输出（用 ComponentMask 从 float4 提取）
        return {
            c->ComponentMask(sampleChunk, true, true, true,  false),  // RGB
            c->ComponentMask(sampleChunk, true, false, false, false), // R
            c->ComponentMask(sampleChunk, false, true, false, false), // G
            c->ComponentMask(sampleChunk, false, false, true, false), // B
            c->ComponentMask(sampleChunk, false, false, false, true)  // A
        };
    }
};
```

> **注意 TextureSample（非参数版）vs TextureParameter**：第一部分的 `TextureSample` 是"硬编码纹理"——纹理资源直接挂在节点上，不进 cbuffer，shader 里是 `Texture2D AlbedoTex;`（静态资源）。`TextureParameter` 是"可换纹理"——纹理名进 cbuffer 的 uniform 表，运行期由材质实例决定用哪张。两者生成代码几乎一样，区别在"是否参数化"。UE 也是这个区分（`UMaterialExpressionTextureSample` vs `UMaterialExpressionTextureSampleParameter`）。

#### StaticSwitch 与材质变体

StaticSwitch 是参数系统最复杂的部分——它**不进 cbuffer**，而是影响**编译哪段代码**。

##### 静态参数 vs 动态参数

| 维度 | 动态参数（ScalarParameter 等） | 静态参数（StaticSwitch） |
|------|------|------|
| 进 shader 的方式 | cbuffer 字段，运行期读 | **不进 shader**——编译期决定分支 |
| shader 数量 | 1 个（参数值变了 shader 不变） | N 个（每种开关组合 1 个变体） |
| 分支代价 | 两条分支都在 shader 里（动态分支 or 都执行） | 只编选中那条，另一条**不存在** |
| 修改后的代价 | 改 cbuffer 值，**不重编** | **必须重编**新变体 |
| 适用场景 | 颜色、强度、阈值等连续值 | "开关某功能"、"选 mask 通道"等离散选择 |

##### StaticSwitch 节点的 Compile（编译期分支）

```cpp
// src/Expression/Public/Parameters/ExprStaticSwitch.h
class ExprStaticSwitch : public Expression {
public:
    std::string paramName_    = "Switch";
    bool        defaultValue_ = false;   // 编译期选哪个分支（UI 可改、材质实例可覆盖）

    ME_BEGIN_CLASS(ExprStaticSwitch)
        ME_DISPLAY_NAME("StaticSwitch")
        ME_CATEGORY("Parameters")
        ME_FIELD(ExprStaticSwitch, paramName_,    "Switch")
        ME_FIELD(ExprStaticSwitch, defaultValue_, false)
    ME_END_CLASS(ExprStaticSwitch)

    std::vector<ExpressionPinDesc> GetInputPins() const override {
        return {
            {"True",  EValueType::Float1, ""},   // 类型可任（编译期决定走哪个，不要求两边同类型）
            {"False", EValueType::Float1, ""},
        };
    }
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {{"Output", EValueType::Float1, ""}};
    }

    std::vector<int32_t> Compile(MaterialCompiler* c, Node* node) const override {
        // 关键：编译期根据 switch 当前值，**只编译**对应分支
        // （另一分支根本不调 CompileInputPin，它的子图不会出现在 shader 里）
        bool value = c->GetStaticSwitchValue(paramName_, defaultValue_);
        const char* pin = value ? "True" : "False";
        return { c->CompileInputPin(node, pin) };
    }
};
```

##### MaterialCompiler 改造（持有静态参数上下文）

```cpp
// MaterialCompiler.h 扩展
class MaterialCompiler {
public:
    // 访问 uniform 表（参数节点 Compile 时调用）
    UniformTable& GetUniformTable() { return uniformTable_; }

    // 静态开关查询——优先用材质实例覆盖值，否则用节点默认值
    bool GetStaticSwitchValue(const std::string& name, bool defaultValue) const {
        auto it = staticOverrides_.find(name);
        return it != staticOverrides_.end() ? it->second : defaultValue;
    }

    // 编译前设置静态参数覆盖（材质实例用——决定编哪个变体）
    void SetStaticOverrides(const std::map<std::string, bool>& overrides) {
        staticOverrides_ = overrides;
    }

    // 错误报告（统一入口，带节点定位，对照 UE 的 HandleMaterialCompilationErrors）
    void ReportError(const std::string& msg);

    // ...原有 API（Constant/Add/Mul/...）...

private:
    UniformTable                   uniformTable_;
    std::map<std::string, bool>    staticOverrides_;   // 静态参数当前值（决定变体）
};
```

##### 变体 key：静态参数组合的 hash

每种静态参数组合对应一个 shader 变体。变体的唯一标识是"所有静态开关名 + 当前值"的有序 hash：

```cpp
// StaticSwitchContext.h（编译器层）
struct StaticSwitchContext {
    // std::map 按 key 排序——保证 (A=true,B=false) 和 (B=false,A=true) 得到同一 key
    std::map<std::string, bool> switches;

    // FNV-1a hash（64 位）——相同 switches 组合得相同 key
    uint64_t Key() const {
        uint64_t h = 0xcbf29ce484222325ULL;   // FNV offset basis
        for (const auto& [k, v] : switches) {
            for (char c : k) { h ^= (uint8_t)c; h *= 0x100000001b3ULL; }
            h ^= v ? 1 : 0; h *= 0x100000001b3ULL;
            h ^= 0x7E;     // 分隔符（防 "AB" + true 和 "A" + Btrue 碰撞）
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    bool operator==(const StaticSwitchContext& o) const { return switches == o.switches; }
};
```

**为什么用 `std::map` 而不是 `std::unordered_map`**：map 保证 key 有序遍历，hash 结果只依赖"开关集合"不依赖插入顺序。unordered_map 遍历顺序不固定，同一组开关可能算出不同 hash。

##### 材质变体管理

一个材质资源持有多个变体，按变体 key 索引。第一次请求某组合 → 编译；后续请求同一组合 → 直接取缓存：

```cpp
// MaterialResource.h（材质资源——一个材质图 + 一组编译好的变体）
class MaterialResource {
public:
    // 编译指定变体（按静态参数组合）。已编译则返回缓存
    Shader* GetOrCompileVariant(const StaticSwitchContext& ctx,
                                 Graph* graph,
                                 MaterialCompiler& compiler);

    // 已编译变体数（监控变体爆炸）
    size_t VariantCount() const { return variants_.size(); }

private:
    struct VariantEntry {
        StaticSwitchContext       ctx;     // 用于 IsSameWithId 比较（对照 UE）
        std::unique_ptr<Shader>   shader;
    };
    std::vector<VariantEntry> variants_;   // 用 vector + 线性查找（变体数通常 < 64）
};
```

```cpp
// MaterialResource.cpp
Shader* MaterialResource::GetOrCompileVariant(const StaticSwitchContext& ctx,
                                               Graph* graph,
                                               MaterialCompiler& compiler) {
    uint64_t key = ctx.Key();

    // 1. 查缓存
    for (const auto& v : variants_) {
        if (v.ctx.Key() == key) return v.shader.get();
    }

    // 2. 缓存未命中 → 编译新变体
    compiler.SetStaticOverrides(ctx.switches);   // 关键：让 StaticSwitch 选对分支
    auto result = compiler.Compile(graph);       // 课6 的 Compile，但这次会走 StaticSwitch 分支
    if (!result.success) return nullptr;

    auto shader = std::make_unique<Shader>(result.hlslCode);
    Shader* raw = shader.get();
    variants_.push_back({ctx, std::move(shader)});
    return raw;
}
```

##### 变体爆炸（最严重的坑）

N 个独立 StaticSwitch 有 **2^N** 种组合。10 个开关 = 1024 个 shader = 编译时间 + 显存爆炸。

| 措施 | 说明 |
|------|------|
| **按需编译**（教学版采用） | 不预编所有组合，运行期遇到某组合才编。大多数组合用户根本不会同时出现 |
| **变体上限**（教学版建议加） | `if (variants_.size() > 32) ReportError("变体数超限，请减少 StaticSwitch")` |
| **静态开关合并**（UE5 也做） | 编译时扫描，把"必然同时为 true"的开关合并成一个 |
| **用动态分支替代**（设计建议） | 如果某开关在帧内会变（不是材质级别），用 `If` 节点（动态分支）而不是 StaticSwitch |

> **UE 对照**：UE 的 `FMaterialResource::CompileStaticPermutationParameters` 处理变体。UE 也有变体爆炸问题——`MaterialInstance.cpp` 里的 `bHasStaticPermutationWarning` 标志就是用来警告用户的。引擎还提供"UsedStaticParameters"列表让用户看到"哪些开关实际产生了变体"。

#### 反射集成细节

参数节点的字段（`paramName_` / `defaultValue_`）走项目的反射系统（课5 的 `ME_FIELD`），自动获得：

1. **序列化**：`Expression::GetParameters()` 返回所有字段的 json，`SetParameter(name, value)` 改单个字段。材质存盘 → 加载后参数节点恢复原名和默认值。
2. **UI 编辑**：`PropertyCustomizerRegistry` 按 `std::type_index` 注册编辑器——字符串字段显示成文本框、float 字段显示成滑块、Vec3 字段显示成颜色选择器、bool 字段（StaticSwitch 的 `defaultValue_`）显示成复选框。
3. **分组**：`ME_CATEGORY` 决定节点在调色板的分组，`UniformEntry::group` 决定参数在材质实例编辑器的分组（两者不同——前者是节点库分组，后者是参数分组）。

**参数名重命名的影响**：用户改 `paramName_` 会破坏序列化兼容（已存的材质实例按旧名引用 cbuffer 字段）。UE 的做法是给参数一个 `ParameterName` 字段 + 一个 GUID，重命名时只改显示名不改 GUID。教学版简化：参数名改了就视为新参数，旧材质实例的值丢失（在 UI 上提示用户）。

**参数名校验**（建议加在 `Register` 里）：

```cpp
// 名字必须合法 HLSL 标识符（字母/数字/下划线，不能数字开头）
// 防止用户输入 "My Param" 或 "1Color" 导致生成的 HLSL 编译失败
static bool IsValidParameterName(const std::string& name) {
    if (name.empty()) return false;
    if (!std::isalpha((unsigned char)name[0]) && name[0] != '_') return false;
    for (char c : name)
        if (!std::isalnum((unsigned char)c) && c != '_') return false;
    return true;
}
```

#### 对照 UE5

| 本课概念 | UE 对应 | 位置（相对 `Engine/`） |
|---------|---------|------|
| `UniformTable` | `FMaterialCompilationOutput`（持有 uniform 描述表）| `Source/Runtime/Engine/Public/Materials/MaterialCompilationOutput.h` |
| `UniformEntry` | `FMaterialUniformInfo` / `FScalarParameter` / `FVectorParameter` | 同上 |
| `ScalarParameter` 节点 | `UMaterialExpressionScalarParameter` | `Source/Runtime/Engine/Private/Materials/MaterialExpressionParameter.cpp` |
| `VectorParameter` 节点 | `UMaterialExpressionVectorParameter` | 同上 |
| `TextureParameter` 节点 | `UMaterialExpressionTextureSampleParameter2D` | 同上 |
| `StaticSwitch` 节点 | `UMaterialExpressionStaticSwitchParameter` | 同上 `.cpp` |
| StaticSwitch 上下文 | `FStaticParameterSet` / `FStaticSwitchParameter` | `MaterialCompilationOutput.h` |
| 材质变体 | `FMaterialResource`（每个变体一个实例）| `Source/Runtime/Engine/Private/Materials/MaterialRenderProxy.cpp` |
| 变体比较 | `FMaterialResource::IsSameWithId` | 同上 |
| cbuffer 偏移计算 | `TConstantBufferLayout` | `Source/Runtime/RenderCore/Public/Shader.h` |
| 同名参数合并 | `MatchTextLookupCollapse` / Parameter override | `MaterialInstance.cpp` |
| 参数默认值序列化 | `FMaterialInstanceBasePropertyOverrides` | `Source/Runtime/Engine/Public/Materials/MaterialInstance.h` |

**UE 和教学版的三个关键差异**：

1. **preshader**：UE 把含参数的表达式（如 `ScalarParameter * 2`）做成 `FMaterialUniformExpression` 子树，运行期 CPU 端预求值（preshader），结果再传给 shader。教学版**不做 preshader**——参数直接当 uniform 传，`ScalarParameter * 2` 整段在 shader 里算。代价：UE 能把"参数 × 常量"算成单一 uniform（省 shader 指令），教学版每帧都算。
2. **参数编辑器**：UE 的材质实例编辑器（`SMaterialInstanceEditor`）是一套专门的 UI，分组、排序、动画曲线、颜色拾取都内置。教学版用 `PropertyCustomizerRegistry` 通用反射 UI——够用但功能弱。
3. **变体缓存全局化**：UE 的变体缓存是**全局的**（`FMaterialResourceManager`），多个材质实例共享同一变体。教学版的 `MaterialResource::variants_` 是**每个材质独享**的——两个材质即使静态参数组合相同也各编一份。代价：教学版编一遍材质，UE 编一遍全局。

#### 已踩坑/注意

| 坑 | 现象 | 解决 |
|----|------|------|
| **同名参数不合并** | 图里 3 个 `BaseColor` 节点 → cbuffer 里声明 3 次 `float3 BaseColor` → HLSL 编译失败（重复定义） | `UniformTable::Register` 用 `nameIndex_` 去重，第二次注册同名的直接 return true（合并） |
| **同名不同类型冲突** | 一个节点叫 `Color`（float3），另一个也叫 `Color`（float1）→ cbuffer 类型混乱 | `Register` 检测到同名但 `type` 不同 → 返回 false → 编译器报错 |
| **纹理参数漏配 sampler** | 只注册 `Texture2D AlbedoTex`，没 sampler → shader 里 `AlbedoTex.Sample(?, uv)` 缺参数 → HLSL 编译失败 | `TextureParameter::Compile` 里**总是**注册配对的 `NameSampler`（命名约定） |
| **变体 key 用 unordered_map** | `(A=true,B=false)` 和 `(B=false,A=true)` 算出不同 key → 同一组合编两次 | 用 `std::map`（有序遍历），key 只依赖开关集合 |
| **StaticSwitch 改值不重编** | 用户改了开关值，shader 没变（仍然走旧分支） | StaticSwitch 改值必须触发 `MaterialResource::GetOrCompileVariant` 重编（或返回缓存） |
| **变体爆炸** | 10 个 StaticSwitch → 1024 个 shader → 编译卡死 | 按需编译 + 变体数上限（如 32）+ 警告用户 |
| **参数名含空格** | 用户输入 `"My Color"` → HLSL `float3 My Color;` → 编译失败 | `Register` 里做 `IsValidParameterName` 校验 |
| **Vec3 在 cbuffer 占 16 字节** | `float3 A; float B;` 中 B 被挤到下一个 16 字节段（HLSL packing rule） | 教学版交给 FXC/DXC 处理；CPU 端写 cbuffer 时按 HLSL layout 算偏移（或用 `Reflect` 查偏移） |
| **重命名参数丢值** | 用户改参数名 → 旧材质实例的值丢失 | UE 给参数加 GUID（标识符不变），教学版简化：重命名 = 新参数 |
| **参数节点参与折叠** | `Add(ScalarParameter, ScalarParameter)` 试图编译期算 → 错（参数是运行期值） | 参数节点的 chunk 必须 `is_constant=false`——`AddInlinedCodeChunk` 默认就是 false，不要误用 `AddConstantChunk` |

#### 集成步骤（按依赖顺序）

1. **UniformTable**：`src/Compiler/Public/UniformTable.h` + `.cpp`，实现 `Register/Find/GenerateHLSL`
2. **MaterialCompiler 持有 table**：加 `UniformTable uniformTable_` 成员 + `GetUniformTable()` + `ReportError()` + `SetStaticOverrides()`/`GetStaticSwitchValue()`
3. **GenerateCode 改造**：`MaterialCompiler::GenerateCode` 末尾调 `uniformTable_.GenerateHLSL()`，把 `cbufferBody` 嵌入 HLSL 模板的 cbuffer 段，`textureDecls` 嵌入纹理声明段
4. **参数节点 Expression 子类**：`ExprScalarParameter` / `ExprVectorParameter` / `ExprTextureParameter` / `ExprStaticSwitch`（每个 ~30-50 行）
5. **注册**：`RegisterAllExpressions()` 里加这 4 个
6. **StaticSwitchContext** + **MaterialResource**：变体 key + 缓存（独立于编译器，在材质资源层）
7. **验证**：写 `parameter_test.cpp`——同名合并、类型冲突、变体 key 一致性、cbuffer 声明正确

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

### 关联课程

原 `lesson06-extension.md` 的块5/块6 已拆分到专门的课里，不在课20 重复：

- **错误诊断**（编译错误：类型不匹配 / 循环依赖 / 未连接必需引脚 / 除零，带**节点 + pin 定位**，编辑器据此高亮出错节点）→ **课19**（对照 UE 的 `HandleMaterialCompilationErrors`）。
- **材质域 / 混合模式**（`EMaterialDomain`：Surface/Unlit/PostProcess/Decal；`EBlendMode`：Opaque/Masked/Translucent/Additive——决定 shader 结构 [课8 模板分支] + blend state）→ **课15**。
- **参数系统深度**（参数节点 / uniform 收集 / `UniformTable` / StaticSwitch / 材质变体）→ **本课第四部分**（参数节点在此第一次实现）。

> **搜索关键词**（UE 源码）：`MaterialExpressionFresnel`、`GetShaderInstructionCount`、`EShaderPlatform`、`EMaterialDomain`、`EBlendMode`、`HandleMaterialCompilationErrors`、`MaterialExpressionScalarParameter`、`MaterialExpressionStaticSwitchParameter`、`FMaterialResource`、`FStaticParameterSet`。

---

## 完成标志

- [ ] TextureSample 可采样纹理（至少代码生成正确）
- [ ] Time/Fresnel/OneMinus/Saturate 工作正确
- [ ] HLSL 导出格式正确（默认格式）
- [ ] 着色器统计面板显示指令数、纹理采样数
- [ ] 所有新表达式注册到调色板和工厂

**Substrate BSDF 节点族（课 6 `MCT_Substrate` 的节点侧消费，桩化实现）**：

- [ ] `SubstrateSlabBSDF` 节点（对照 UE `MaterialExpressionSubstrateSlabBSDF`）：输入 BaseColor/Roughness/Metallic，输出引脚类型 `MCT_Substrate`
- [ ] 编译路径：`Compile()` 生成 `GetInitialisedSubstrateData()` 桩调用 chunk（对齐 UE `HLSLMaterialTranslator.cpp:12927` 的真实调用形态），类型 `MCT_Substrate`、HLSL 类型 `FSubstrateData`（课 6 ToHLSLType 已映射）
- [ ] 桩函数定义进课 8 的 HLSL 模板公共段：`FSubstrateData GetInitialisedSubstrateData() { FSubstrateData D; /* 默认灰 BSDF */ return D; }`——**类型通路全真，BSDF 光学求值桩化**（UE 的 Substrate.ush 运行时 16k 行，教学版到课 17 的 PBR 预览用传统 Lit 路径）
- [ ] 算术拦截测试：`Substrate输出 + Constant` → 编译报错（课 6 `GetArithmeticResultType` 对 `MCT_Substrate` 返回 Unknown 的端到端验证）

**参数系统（第四部分深度）**：

- [ ] `UniformTable` 数据结构实现（`Register` 同名合并 + 类型冲突检测 + `GenerateHLSL`）
- [ ] `MaterialCompiler` 持有 `uniformTable_` + `ReportError` / `SetStaticOverrides` / `GetStaticSwitchValue`
- [ ] `GenerateCode` 把 `UniformTable::GenerateHLSL()` 的输出嵌入 HLSL cbuffer 段 + 纹理声明段
- [ ] `ExprScalarParameter` / `ExprVectorParameter` / `ExprTextureParameter` / `ExprStaticSwitch` 四个节点实现 + 注册
- [ ] 参数节点在属性面板可编辑参数名和默认值，修改后预览实时更新
- [ ] 同名参数合并测试：图里 3 个 `BaseColor` 节点 → cbuffer 只声明 1 次
- [ ] 类型冲突报错测试：同名 `Color` 一个 float3 一个 float1 → 编译错误
- [ ] 纹理参数配 sampler 测试：TextureParameter 自动注册 `NameSampler`
- [ ] `StaticSwitchContext` 变体 key 一致性测试（`(A=true,B=false)` 和 `(B=false,A=true)` 同 key）
- [ ] `MaterialResource::GetOrCompileVariant` 缓存命中测试 + 变体数上限保护
