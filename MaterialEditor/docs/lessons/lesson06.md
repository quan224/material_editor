# 课6：编译器核心

## 目标

实现 MaterialCompiler 主类和 CodeChunk 数据结构。这是整个项目**最核心最复杂**的部分，等同于 UE5 的 FHLSLMaterialTranslator。

---

## 背景知识

### 编译器做什么？
编译器接收一个 Graph，从输出节点反向遍历，对每个节点调用其 Expression 的 `Compile()` 方法，生成一系列"代码块"（CodeChunk），最后组装成完整的 GLSL/HLSL 着色器。

### 编译流程详解

```
MaterialCompiler::Compile(Graph*)
  │
  ├─ 1. 获取输出节点
  ├─ 2. 对输出节点的每个输入引脚（BaseColor, Metallic, ...）
  │     调用 CompileInputPin(outputNode, "BaseColor")
  │       │
  │       ├─ 找到该引脚连接的上游节点
  │       ├─ 递归编译上游节点 → CompileExpression(upstreamNode)
  │       │     │
  │       │     ├─ 上游 Expression::Compile(this)
  │       │     │    ├─ 又调用 CompileInputPin() 编译自己的输入 ← 递归
  │       │     │    ├─ 调用 compiler->Add(a, b) 等方法生成代码块
  │       │     │    └─ 返回输出引脚的代码块索引
  │       │     │
  │       │     └─ 缓存结果（同一节点不重复编译）
  │       │
  │       └─ 返回代码块索引
  │
  ├─ 3. 收集所有材质属性的代码块索引
  └─ 4. 调用 HLSLGenerator 组装最终着色器代码
```

### CodeChunk 是什么？
每个代码块对应一行 HLSL 代码：
```hlsl
float Local0 = 1.0;              // CodeChunk 0: 常量
float Local1 = 0.5;              // CodeChunk 1: 常量
float Local2 = Local0 + Local1;  // CodeChunk 2: Add 的结果
```
编译器用 `int32_t` 索引引用代码块（等同 UE5 的做法）。

---

## 操作步骤

### 1. 创建文件

```
src/Compiler/Public/CodeChunk.h
src/Compiler/Public/MaterialCompiler.h
src/Compiler/Private/MaterialCompiler.cpp
src/Compiler/Public/ConstantFolding.h
```

### 2. CodeChunk.h

```cpp
#pragma once
#include "MaterialGraph/Public/Types.h"
#include <string>
#include <vector>
#include <cstdint>

struct CodeChunk {
    uint64_t hash = 0;              // 哈希，用于去重
    std::string code;               // HLSL 代码片段，如 "Local0 + Local1"
    std::string symbolName;         // 变量名，如 "Local0"
    EValueType type = EValueType::Unknown;
    bool isInline = false;          // 短表达式直接嵌入，不声明变量
    bool isConstant = false;        // 是否是编译时常量
    float constantValue = 0.0f;     // 常量值（常数折叠用）
    std::vector<int32_t> references; // 依赖的其他代码块索引
};
```

**讲解**：
- `hash` — 用 `HashString(code)` 计算。相同代码只存储一份（UE5 也是这么做的）
- `isInline` — 如果代码很短（如 `1.0`），直接嵌入使用处，不生成变量声明
- `isConstant` + `constantValue` — 常数折叠标记。如果两个常量做运算，直接算出结果
- `references` — 追踪这个代码块依赖哪些其他代码块（最终代码生成时确定声明顺序）

**UE5 参考**：搜索 `FShaderCodeChunk` 在 `HLSLMaterialTranslator.h` 中

### 3. ConstantFolding.h

```cpp
#pragma once
#include <cmath>
#include <optional>

class ConstantFolding {
public:
    // 尝试二元运算折叠
    static std::optional<float> FoldBinary(const std::string& op, float a, float b) {
        if (op == "+") return a + b;
        if (op == "-") return a - b;
        if (op == "*") return a * b;
        if (op == "/") return (b != 0.0f) ? a / b : std::optional<float>{};
        if (op == "pow") return std::pow(a, b);
        return {};
    }

    // 尝试一元运算折叠
    static std::optional<float> FoldUnary(const std::string& op, float a) {
        if (op == "abs") return std::abs(a);
        if (op == "neg") return -a;
        if (op == "sin") return std::sin(a);
        if (op == "cos") return std::cos(a);
        return {};
    }
};
```

### 4. MaterialCompiler.h

```cpp
#pragma once
#include "Compiler/Public/CodeChunk.h"
#include "Compiler/Public/ConstantFolding.h"
#include "MaterialGraph/Public/Types.h"
#include <vector>
#include <map>
#include <string>
#include <set>

class Graph;
class Node;
class Expression;

class MaterialCompiler {
public:
    struct CompileResult {
        bool success = false;
        std::string hlslCode;        // 生成的 HLSL 代码（DX12 直接使用）
        std::string errorMessage;
    };

    MaterialCompiler();

    // 主编译入口
    CompileResult Compile(Graph* graph);

    // === 表达式调用的编译 API ===

    // 编译一个输入引脚（递归编译上游节点）
    int32_t CompileInputPin(Node* node, const std::string& pinName);

    // 算术运算
    int32_t Add(int32_t a, int32_t b);
    int32_t Subtract(int32_t a, int32_t b);
    int32_t Multiply(int32_t a, int32_t b);
    int32_t Divide(int32_t a, int32_t b);
    int32_t Power(int32_t base, int32_t exp);
    int32_t Lerp(int32_t a, int32_t b, int32_t alpha);
    int32_t Clamp(int32_t x, int32_t minVal, int32_t maxVal);
    int32_t Abs(int32_t x);
    int32_t Negate(int32_t x);

    // 三角函数
    int32_t Sine(int32_t x);
    int32_t Cosine(int32_t x);

    // 向量运算
    int32_t Dot(int32_t a, int32_t b);
    int32_t Cross(int32_t a, int32_t b);
    int32_t Normalize(int32_t x);
    int32_t Length(int32_t x);

    // 常量
    int32_t Constant(float value);
    int32_t Constant2(float x, float y);
    int32_t Constant3(float x, float y, float z);
    int32_t Constant4(float x, float y, float z, float w);

    // 向量操作
    int32_t ComponentMask(int32_t input, bool r, bool g, bool b, bool a);
    int32_t AppendVector(int32_t a, int32_t b);

    // 纹理（课7/8 扩展）
    int32_t TextureCoordinate();
    int32_t TextureSample(int32_t texture, int32_t coordinate);

    // 控制
    int32_t If(int32_t condition, int32_t trueVal, int32_t falseVal);

    // 类型转换
    int32_t Cast(int32_t code, EValueType targetType);

    // 查询代码块信息
    std::string GetParameterCode(int32_t index) const;
    EValueType GetType(int32_t index) const;
    bool IsConstant(int32_t index) const;
    float GetConstantValue(int32_t index) const;

private:
    // 代码块管理
    int32_t AddCodeChunk(EValueType type, const std::string& code, bool isInline = false);
    int32_t AddConstantChunk(EValueType type, float value);
    std::string MakeSymbolName();

    // 解析字符串形式的默认值（如 "0.5" 或 "(1,0,0)"）为代码块索引
    int32_t ParseDefaultValue(const std::string& val, EValueType type);

    // 编译单个表达式节点
    std::vector<int32_t> CompileExpression(Node* node);

    // 最终代码生成（课8 完善）
    std::string GenerateCode(const std::map<std::string, int32_t>& outputs);

    // 状态
    std::vector<CodeChunk> chunks_;
    std::map<uint64_t, int32_t> hashToChunk_;  // 哈希去重
    std::map<std::string, std::vector<int32_t>> nodeCache_;  // nodeId → 输出索引
    int32_t nextSymbolIndex_ = 0;
    Graph* currentGraph_ = nullptr;
    std::string errorMessage_;
};
```

### 5. MaterialCompiler.cpp — 核心实现

```cpp
#include "Compiler/Public/MaterialCompiler.h"
#include "Compiler/Public/TypeSystem.h"
#include "MaterialGraph/Public/Graph.h"
#include "MaterialGraph/Public/NodeFactory.h"
#include "Expression/Public/Expression.h"
#include "Core/Public/Hash.h"
#include "Core/Public/Logger.h"
#include <sstream>
#include <cstdarg>

// ===================== 主编译入口 =====================

MaterialCompiler::MaterialCompiler() {}

MaterialCompiler::CompileResult MaterialCompiler::Compile(Graph* graph) {
    CompileResult result;
    currentGraph_ = graph;
    chunks_.clear();
    hashToChunk_.clear();
    nodeCache_.clear();
    nextSymbolIndex_ = 0;
    errorMessage_.clear();

    Node* output = graph->GetOutputNode();
    if (!output) {
        result.success = false;
        result.errorMessage = "No output node found";
        return result;
    }

    // 收集每个材质属性的编译结果
    std::map<std::string, int32_t> materialOutputs;
    for (const auto& pin : output->inputPins) {
        if (pin.IsConnected()) {
            int32_t codeIdx = CompileInputPin(output, pin.name);
            if (codeIdx < 0) {
                result.success = false;
                result.errorMessage = errorMessage_;
                return result;
            }
            materialOutputs[pin.name] = codeIdx;
        }
    }

    // 生成最终代码
    result.hlslCode = GenerateCode(materialOutputs);
    result.success = true;
    return result;
}

// ===================== 辅助函数 =====================

// 解析字符串默认值，返回对应的代码块索引
// 注意：必须是 MaterialCompiler 的成员函数，因为内部要调用 Constant/Constant3/Constant4
// 这些是成员方法（依赖 chunks_ 等状态）。返回 int32_t（代码块索引）而非 CodeChunk，
// 以匹配 CompileInputPin 的返回类型。
int32_t MaterialCompiler::ParseDefaultValue(const std::string& val, EValueType type) {
    if (type == EValueType::Float1) {
        return Constant(std::stof(val));
    }
    if (type == EValueType::Float2) {
        float x = 0, y = 0;
        sscanf(val.c_str(), "(%f,%f)", &x, &y);
        return Constant2(x, y);
    }
    if (type == EValueType::Float3) {
        // 格式: "(x,y,z)"
        float x = 0, y = 0, z = 0;
        sscanf(val.c_str(), "(%f,%f,%f)", &x, &y, &z);
        return Constant3(x, y, z);
    }
    if (type == EValueType::Float4) {
        float x = 0, y = 0, z = 0, w = 0;
        sscanf(val.c_str(), "(%f,%f,%f,%f)", &x, &y, &z, &w);
        return Constant4(x, y, z, w);
    }
    return Constant(0.0f);
}

// ===================== 递归编译 =====================

int32_t MaterialCompiler::CompileInputPin(Node* node, const std::string& pinName) {
    const Pin* pin = node->FindInputPin(pinName);
    if (!pin) return Constant(0.0f);  // 引脚不存在，返回默认值

    if (!pin->IsConnected()) {
        // 未连接：使用默认值
        return ParseDefaultValue(pin->defaultValue, pin->type);
    }

    // 找到连接的上游节点
    // 注意：PinConnection 字段名是 otherNodeId / otherPinId（"对端"），
    // 不暗示方向。从输入引脚的视角看，"对端" 就是上游输出节点。
    const auto& conn = pin->connections[0];  // 输入引脚最多一个连接
    Node* upstream = currentGraph_->FindNode(conn.otherNodeId);
    if (!upstream) return Constant(0.0f);

    // 编译上游节点
    auto outputs = CompileExpression(upstream);

    // 找到上游连接的输出引脚索引
    const Pin* srcPin = upstream->FindPin(conn.otherPinId);
    if (!srcPin) return Constant(0.0f);

    int pinIndex = 0;
    for (int i = 0; i < (int)upstream->outputPins.size(); i++) {
        if (upstream->outputPins[i].id == srcPin->id) { pinIndex = i; break; }
    }

    if (pinIndex < (int)outputs.size()) return outputs[pinIndex];
    return Constant(0.0f);
}

std::vector<int32_t> MaterialCompiler::CompileExpression(Node* node) {
    // 检查缓存（同一节点不重复编译）
    std::string cacheKey = node->id.ToString();
    auto it = nodeCache_.find(cacheKey);
    if (it != nodeCache_.end()) return it->second;

    // 获取 Expression 实例并编译
    // 注意：Expression 实例的获取方式在课7中完善
    // 这里预留接口
    std::vector<int32_t> outputs;

    // TODO: 课7中通过 ExpressionRegistry 获取 Expression 并调用 Compile()
    // 临时：如果节点有编译缓存直接返回
    errorMessage_ = "Expression not registered: " + node->typeName;

    nodeCache_[cacheKey] = outputs;
    return outputs;
}

// ===================== 代码块管理 =====================

std::string MaterialCompiler::MakeSymbolName() {
    return "Local" + std::to_string(nextSymbolIndex_++);
}

int32_t MaterialCompiler::AddCodeChunk(EValueType type, const std::string& code, bool isInline) {
    uint64_t hash = HashString(code);
    auto it = hashToChunk_.find(hash);
    if (it != hashToChunk_.end()) return it->second;  // 去重

    CodeChunk chunk;
    chunk.hash = hash;
    chunk.code = code;
    chunk.type = type;
    chunk.isInline = isInline;
    if (!isInline) {
        chunk.symbolName = MakeSymbolName();
    }
    chunks_.push_back(chunk);
    int32_t index = (int32_t)chunks_.size() - 1;
    hashToChunk_[hash] = index;
    return index;
}

int32_t MaterialCompiler::AddConstantChunk(EValueType type, float value) {
    std::string code;
    if (value == 0.0f) code = "0.0";
    else if (value == 1.0f) code = "1.0";
    else code = std::to_string(value);

    uint64_t hash = HashString("const_" + code);
    auto it = hashToChunk_.find(hash);
    if (it != hashToChunk_.end()) return it->second;

    CodeChunk chunk;
    chunk.hash = hash;
    chunk.code = code;
    chunk.type = type;
    chunk.isInline = true;
    chunk.isConstant = true;
    chunk.constantValue = value;
    chunks_.push_back(chunk);
    int32_t index = (int32_t)chunks_.size() - 1;
    hashToChunk_[hash] = index;
    return index;
}

// ===================== 算术运算 =====================

int32_t MaterialCompiler::Add(int32_t a, int32_t b) {
    // 常数折叠
    if (IsConstant(a) && IsConstant(b)) {
        return AddConstantChunk(TypeSystem::GetArithmeticResultType(GetType(a), GetType(b)),
                                GetConstantValue(a) + GetConstantValue(b));
    }
    auto type = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    std::string code = GetParameterCode(a) + " + " + GetParameterCode(b);
    return AddCodeChunk(type, code, true);
}

int32_t MaterialCompiler::Subtract(int32_t a, int32_t b) {
    if (IsConstant(a) && IsConstant(b)) {
        return AddConstantChunk(TypeSystem::GetArithmeticResultType(GetType(a), GetType(b)),
                                GetConstantValue(a) - GetConstantValue(b));
    }
    auto type = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    return AddCodeChunk(type, GetParameterCode(a) + " - " + GetParameterCode(b), true);
}

int32_t MaterialCompiler::Multiply(int32_t a, int32_t b) {
    if (IsConstant(a) && IsConstant(b)) {
        return AddConstantChunk(TypeSystem::GetArithmeticResultType(GetType(a), GetType(b)),
                                GetConstantValue(a) * GetConstantValue(b));
    }
    // 乘以 1 → 直通
    if (IsConstant(a) && GetConstantValue(a) == 1.0f) return b;
    if (IsConstant(b) && GetConstantValue(b) == 1.0f) return a;
    // 乘以 0 → 零
    if (IsConstant(a) && GetConstantValue(a) == 0.0f) return Constant(0.0f);
    if (IsConstant(b) && GetConstantValue(b) == 0.0f) return Constant(0.0f);

    auto type = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    return AddCodeChunk(type, GetParameterCode(a) + " * " + GetParameterCode(b), true);
}

int32_t MaterialCompiler::Divide(int32_t a, int32_t b) {
    if (IsConstant(b) && GetConstantValue(b) == 0.0f) {
        ME_LOG_WARNING("Division by zero in material compile");
        return Constant(0.0f);
    }
    if (IsConstant(a) && IsConstant(b)) {
        return AddConstantChunk(GetType(a), GetConstantValue(a) / GetConstantValue(b));
    }
    return AddCodeChunk(GetType(a), GetParameterCode(a) + " / " + GetParameterCode(b), true);
}

int32_t MaterialCompiler::Power(int32_t base, int32_t exp) {
    if (IsConstant(base) && IsConstant(exp)) {
        return AddConstantChunk(GetType(base), std::pow(GetConstantValue(base), GetConstantValue(exp)));
    }
    return AddCodeChunk(GetType(base), "pow(" + GetParameterCode(base) + ", " + GetParameterCode(exp) + ")", false);
}

int32_t MaterialCompiler::Lerp(int32_t a, int32_t b, int32_t alpha) {
    // HLSL 用 lerp（GLSL 用 mix，名字不同语义相同）
    return AddCodeChunk(GetType(a),
        "lerp(" + GetParameterCode(a) + ", " + GetParameterCode(b) + ", " + GetParameterCode(alpha) + ")",
        false);
}

int32_t MaterialCompiler::Clamp(int32_t x, int32_t minVal, int32_t maxVal) {
    return AddCodeChunk(GetType(x),
        "clamp(" + GetParameterCode(x) + ", " + GetParameterCode(minVal) + ", " + GetParameterCode(maxVal) + ")",
        false);
}

int32_t MaterialCompiler::Abs(int32_t x) {
    if (IsConstant(x)) return AddConstantChunk(GetType(x), std::abs(GetConstantValue(x)));
    return AddCodeChunk(GetType(x), "abs(" + GetParameterCode(x) + ")", false);
}

int32_t MaterialCompiler::Negate(int32_t x) {
    if (IsConstant(x)) return AddConstantChunk(GetType(x), -GetConstantValue(x));
    return AddCodeChunk(GetType(x), "(-" + GetParameterCode(x) + ")", true);
}

int32_t MaterialCompiler::Sine(int32_t x) {
    if (IsConstant(x)) return AddConstantChunk(GetType(x), std::sin(GetConstantValue(x)));
    return AddCodeChunk(GetType(x), "sin(" + GetParameterCode(x) + ")", false);
}

int32_t MaterialCompiler::Cosine(int32_t x) {
    if (IsConstant(x)) return AddConstantChunk(GetType(x), std::cos(GetConstantValue(x)));
    return AddCodeChunk(GetType(x), "cos(" + GetParameterCode(x) + ")", false);
}

int32_t MaterialCompiler::Dot(int32_t a, int32_t b) {
    return AddCodeChunk(EValueType::Float1, "dot(" + GetParameterCode(a) + ", " + GetParameterCode(b) + ")", false);
}

int32_t MaterialCompiler::Cross(int32_t a, int32_t b) {
    return AddCodeChunk(EValueType::Float3, "cross(" + GetParameterCode(a) + ", " + GetParameterCode(b) + ")", false);
}

int32_t MaterialCompiler::Normalize(int32_t x) {
    return AddCodeChunk(GetType(x), "normalize(" + GetParameterCode(x) + ")", false);
}

int32_t MaterialCompiler::Length(int32_t x) {
    return AddCodeChunk(EValueType::Float1, "length(" + GetParameterCode(x) + ")", false);
}

// ===================== 常量 =====================

int32_t MaterialCompiler::Constant(float value) { return AddConstantChunk(EValueType::Float1, value); }
int32_t MaterialCompiler::Constant2(float x, float y) {
    return AddCodeChunk(EValueType::Float2, "float2(" + std::to_string(x) + ", " + std::to_string(y) + ")", false);
}
int32_t MaterialCompiler::Constant3(float x, float y, float z) {
    return AddCodeChunk(EValueType::Float3, "float3(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")", false);
}
int32_t MaterialCompiler::Constant4(float x, float y, float z, float w) {
    return AddCodeChunk(EValueType::Float4, "float4(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ", " + std::to_string(w) + ")", false);
}

// ===================== 向量操作 =====================

int32_t MaterialCompiler::ComponentMask(int32_t input, bool r, bool g, bool b, bool a) {
    std::string mask;
    if (r) mask += "r";
    if (g) mask += "g";
    if (b) mask += "b";
    if (a) mask += "a";
    int components = (r?1:0) + (g?1:0) + (b?1:0) + (a?1:0);
    EValueType type = (components == 1) ? EValueType::Float1 :
                      (components == 2) ? EValueType::Float2 :
                      (components == 3) ? EValueType::Float3 : EValueType::Float4;
    return AddCodeChunk(type, GetParameterCode(input) + "." + mask, true);
}

int32_t MaterialCompiler::AppendVector(int32_t a, int32_t b) {
    int total = TypeSystem::GetComponentCount(GetType(a)) + TypeSystem::GetComponentCount(GetType(b));
    EValueType type = (total == 2) ? EValueType::Float2 :
                      (total == 3) ? EValueType::Float3 :
                      (total == 4) ? EValueType::Float4 : EValueType::Unknown;
    // 注意：HLSL 拼接两个向量要用 float3(a, b.z) 这种形式，但如果 a/b 都是标量，
    // float2(a, b) 也是合法的。这里简化处理，假定输入是标量或编译器已保证维度
    return AddCodeChunk(type, "float" + std::to_string(total) + "(" + GetParameterCode(a) + ", " + GetParameterCode(b) + ")", false);
}

// ===================== 纹理（占位，课15 DX12 阶段会重写）=====================
// 注意：占位实现假设 HLSL 中已存在 SamplerState 和 Texture2D 全局变量，
// 真正的实现在课15 DX12 集成时会替换为基于 Srv/Sampler 索引的版本

int32_t MaterialCompiler::TextureCoordinate() {
    // 占位：从顶点着色器传入的 UV（在课15 真正接入 VertexBuffer 后会有真实数据）
    return AddCodeChunk(EValueType::Float2, "input.UV", true);
}
int32_t MaterialCompiler::TextureSample(int32_t texture, int32_t coordinate) {
    // HLSL 纹理采样：Texture2D.Sample(SamplerState, float2)
    // 与 GLSL 的 texture(tex, uv) 不同，HLSL 把 sampler 作为独立对象传入
    return AddCodeChunk(EValueType::Float4,
        GetParameterCode(texture) + ".Sample(Sampler0, " + GetParameterCode(coordinate) + ")", false);
}

// ===================== 控制 =====================

int32_t MaterialCompiler::If(int32_t condition, int32_t trueVal, int32_t falseVal) {
    // HLSL 用 lerp(falseVal, trueVal, condition) 实现，
    // 但 lerp 的第三个参数是 [0,1] 浮点插值，所以 condition 需要先 step 一下
    // HLSL 内置 step(edge, x) = (x < edge) ? 0 : 1
    return AddCodeChunk(GetType(trueVal),
        "lerp(" + GetParameterCode(falseVal) + ", " + GetParameterCode(trueVal) + ", step(0.5, " + GetParameterCode(condition) + "))",
        false);
}

int32_t MaterialCompiler::Cast(int32_t code, EValueType targetType) {
    if (GetType(code) == targetType) return code;
    int cc = TypeSystem::GetComponentCount(targetType);
    return AddCodeChunk(targetType, TypeSystem::ToHLSLType(targetType) + "(" + GetParameterCode(code) + ")", false);
}

// ===================== 查询 =====================

std::string MaterialCompiler::GetParameterCode(int32_t index) const {
    if (index < 0 || index >= (int32_t)chunks_.size()) return "0.0";
    const auto& chunk = chunks_[index];
    return chunk.isInline ? chunk.code : chunk.symbolName;
}

EValueType MaterialCompiler::GetType(int32_t index) const {
    if (index < 0 || index >= (int32_t)chunks_.size()) return EValueType::Unknown;
    return chunks_[index].type;
}

bool MaterialCompiler::IsConstant(int32_t index) const {
    if (index < 0 || index >= (int32_t)chunks_.size()) return false;
    return chunks_[index].isConstant;
}

float MaterialCompiler::GetConstantValue(int32_t index) const {
    if (index < 0 || index >= (int32_t)chunks_.size()) return 0.0f;
    return chunks_[index].constantValue;
}

// ===================== 代码生成（简化版，课8完善）=====================

std::string MaterialCompiler::GenerateCode(const std::map<std::string, int32_t>& outputs) {
    std::stringstream ss;

    // 生成变量声明
    for (size_t i = 0; i < chunks_.size(); i++) {
        const auto& chunk = chunks_[i];
        if (chunk.isInline) continue;  // 内联的不需要声明
        ss << "    " << TypeSystem::ToHLSLType(chunk.type) << " " << chunk.symbolName
           << " = " << chunk.code << ";\n";
    }

    // 生成材质输出赋值
    ss << "\n    // Material outputs\n";
    for (const auto& [name, idx] : outputs) {
        ss << "    // " << name << " = " << GetParameterCode(idx) << "\n";
    }

    return ss.str();
}
```

**讲解**：
- `Add()` 等方法先检查常数折叠，再生成代码。这是 UE5 的 `FHLSLMaterialTranslator::Add()` 的简化版
- `Multiply` 有额外优化：乘1直通、乘0归零
- `GetParameterCode()` 根据是否内联返回不同内容——内联返回代码本身，非内联返回变量名
- `CompileInputPin()` 是递归的核心：编译一个引脚 → 找上游节点 → 编译上游 → 返回结果
- 代码生成先用简化版（课8会完善为完整着色器）

**UE5 参考**：
- `E:\UE5\Engine\Source\Runtime\Engine\Private\Materials\HLSLMaterialTranslator.h` — 类定义
- `HLSLMaterialTranslator.cpp` 搜索 `FHLSLMaterialTranslator::Add` — 算术编译
- 搜索 `AddCodeChunkInner` — 代码块添加逻辑

---

## 完成标志

- [ ] CodeChunk 结构定义完成
- [ ] MaterialCompiler 所有算术方法实现
- [ ] 常数折叠在 Add/Multiply 等中工作
- [ ] 代码生成可以输出变量声明
- [ ] 编译通过（注意 CompileExpression 中的 TODO 需要课7完成才能端到端验证）
