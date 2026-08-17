# 课7：第一批表达式（10个基础表达式）

## 目标

实现 10 个基础表达式（数学 + 常量），并将它们与编译器连接，验证端到端编译管线。

---

## 背景知识

每个表达式遵循**统一的反射驱动模式**：

1. 继承 `Expression`
2. 用 `ME_BEGIN_CLASS / ME_DISPLAY_NAME / ME_CATEGORY / ME_FIELD / ME_END_CLASS` 宏注册元数据
3. 实现引脚布局（`GetInputPins`、`GetOutputPins`）
4. 实现 `Compile(MaterialCompiler*, Node*)` —— 调用编译器 API

子类**不需要**写：
- `GetTypeName / GetDisplayName / GetCategory` —— 都在反射 `ClassDesc` 里
- `GetParameters / SetParameter / GetParameter` —— 基类非虚实现，自动走反射
- `GetCategoryColor` —— UI 层根据 category 自动选色

这个模式对应 UE5 中每个 `MaterialExpression*.cpp` 的 `Compile()` 方法 + UPROPERTY 字段声明。

**对照 UE `UMaterialExpression`**（`Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression.h`）：

| 我们的 | UE | 作用 |
|--------|-----|------|
| `Expression` 基类 | `UMaterialExpression` | 表达式抽象基类 |
| `ME_FIELD` 字段 | `UPROPERTY` 字段 | 暴露给属性面板的可编辑参数 |
| `Compile(MaterialCompiler*, Node*)` | `Compile(FMaterialCompiler*, int32 OutputIndex)` | 编译逻辑：调编译器算子 |
| `ME_DISPLAY_NAME/CATEGORY` | `GetCaption()` + 节点分类 | UI 显示名 + 调色板分组 |
| `ExpressionRegistry` 注册 | UCLASS 自动注册到反射系统 | 按 type_name 创建实例 |

UE 的 `UMaterialExpression` 比 `Expression` 多很多东西（撤销重做、复制粘贴、节点位置、引脚对象 `FExpressionInput` 等），但**核心 Compile 模式一致**：递归编译输入引脚 → 调编译器算子。详见下文"UE5 参考"的逐行对照。

---

## 操作步骤

### 1. 创建文件

```
src/Expression/Public/ExpressionRegistry.h
src/Expression/Private/ExpressionRegistry.cpp
src/Expression/Public/Math/ExprAdd.h
src/Expression/Public/Math/ExprSubtract.h
src/Expression/Public/Math/ExprMultiply.h
src/Expression/Public/Math/ExprDivide.h
src/Expression/Public/Math/ExprPower.h
src/Expression/Public/Math/ExprLerp.h
src/Expression/Public/Math/ExprClamp.h
src/Expression/Public/Math/ExprAbs.h
src/Expression/Public/Constants/ExprConstant.h
src/Expression/Public/Constants/ExprConstant3Vector.h
```

### 2. ExpressionRegistry —— 表达式注册表

把编译器和表达式连接起来的桥梁：

```cpp
// ExpressionRegistry.h
#pragma once
#include "Expression/Public/Expression.h"
#include "Core/Public/RefCounted.h"
#include "Core/Public/Singleton.h"
#include <map>
#include <string>
#include <vector>
#include <functional>

class ExpressionRegistry : public Singleton<ExpressionRegistry> {
    friend Singleton<ExpressionRegistry>;
public:
    using CreatorFn = std::function<Ref<Expression>()>;

    void Register(const std::string& typeName, CreatorFn creator) {
        creators_[typeName] = creator;
    }

    Ref<Expression> Create(const std::string& typeName) const {
        auto it = creators_.find(typeName);
        if (it == creators_.end()) return nullptr;
        return it->second();
    }

    std::vector<std::string> GetAllTypeNames() const {
        std::vector<std::string> names;
        for (const auto& [name, _] : creators_) names.push_back(name);
        return names;
    }

private:
    ExpressionRegistry() = default;
    std::map<std::string, CreatorFn> creators_;
};

void RegisterAllExpressions();
```

```cpp
// ExpressionRegistry.cpp
#include "Expression/Public/ExpressionRegistry.h"
#include "Expression/Public/Math/ExprAdd.h"
#include "Expression/Public/Math/ExprSubtract.h"
#include "Expression/Public/Math/ExprMultiply.h"
#include "Expression/Public/Math/ExprDivide.h"
#include "Expression/Public/Math/ExprPower.h"
#include "Expression/Public/Math/ExprLerp.h"
#include "Expression/Public/Math/ExprClamp.h"
#include "Expression/Public/Math/ExprAbs.h"
#include "Expression/Public/Constants/ExprConstant.h"
#include "Expression/Public/Constants/ExprConstant3Vector.h"

void RegisterAllExpressions() {
    static bool done = false;
    if (done) return;
    done = true;

    auto& reg = ExpressionRegistry::GetInstance();
    reg.Register("ExprConstant",        []() -> Ref<Expression> { return MakeRef<ExprConstant>(); });
    reg.Register("ExprConstant3Vector", []() -> Ref<Expression> { return MakeRef<ExprConstant3Vector>(); });
    reg.Register("ExprAdd",             []() -> Ref<Expression> { return MakeRef<ExprAdd>(); });
    reg.Register("ExprSubtract",        []() -> Ref<Expression> { return MakeRef<ExprSubtract>(); });
    reg.Register("ExprMultiply",        []() -> Ref<Expression> { return MakeRef<ExprMultiply>(); });
    reg.Register("ExprDivide",          []() -> Ref<Expression> { return MakeRef<ExprDivide>(); });
    reg.Register("ExprPower",           []() -> Ref<Expression> { return MakeRef<ExprPower>(); });
    reg.Register("ExprLerp",            []() -> Ref<Expression> { return MakeRef<ExprLerp>(); });
    reg.Register("ExprClamp",           []() -> Ref<Expression> { return MakeRef<ExprClamp>(); });
    reg.Register("ExprAbs",             []() -> Ref<Expression> { return MakeRef<ExprAbs>(); });
}
```

### 3. 修改 HLSLTranslator —— 连接 Expression

课 6 定了双层架构：`MaterialCompiler` 是抽象基类（纯虚算子），`HLSLTranslator` 是实现。本课把图遍历的三个入口**追加**到两层上：

- `MaterialCompiler` 基类追加两个纯虚（表达式通过基类指针递归调它们，对照 UE 基类的 `CallExpression`）：

```cpp
// MaterialCompiler.h 追加（对照 UE FMaterialCompiler::CallExpression）
virtual int32_t CompileInputPin(Node* node, const std::string& pinName) = 0;
virtual std::vector<int32_t> CompileExpression(Node* node) = 0;
```

- 在 `HLSLTranslator.cpp` 实现 `CompileExpression`：

```cpp
#include "Expression/Public/ExpressionRegistry.h"

std::vector<int32_t> HLSLTranslator::CompileExpression(Node* node) {
    std::string cacheKey = node->id.ToString();
    auto it = node_cache_.find(cacheKey);
    if (it != node_cache_.end()) return it->second;

    std::vector<int32_t> outputs;

    auto expr = ExpressionRegistry::GetInstance().Create(node->typeName);
    if (expr) {
        // 同步节点参数到 Expression 字段（基于反射自动派发）
        for (const auto& field : expr->GetParameters()) {
            if (node->parameters.contains(field.name)) {
                expr->SetParameter(field.name, node->parameters[field.name]);
            }
        }
        outputs = expr->Compile(this, node);   // this 作为 MaterialCompiler* 抽象接口传入
    } else {
        EmitError("Unknown expression type: " + node->typeName);   // 进 errors_ 列表（课6 的错误收集器）
    }

    node_cache_[cacheKey] = outputs;
    return outputs;
}
```

### 4. 表达式实现 —— 统一反射模式

#### 4.1 数学表达式（无字段，只有 Compile 逻辑）

```cpp
// ExprAdd.h
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Compiler/Public/MaterialCompiler.h"

class ExprAdd : public Expression {
public:
    // 反射注册（无字段，只声明身份元数据）
    ME_BEGIN_CLASS(ExprAdd)
        ME_DISPLAY_NAME("Add")
        ME_CATEGORY("Math")
    ME_END_CLASS(ExprAdd)

    std::vector<ExpressionPinDesc> GetInputPins() const override {
        // 标量引脚类型用 MCT_Float 掩码（不是 MCT_Float1）——UE 语义：
        // "标量表达式类型用 MCT_Float，可复制提升到任意 float 向量"（MaterialValueType.h 注释）
        return {{"A", MCT_Float, "0.0"}, {"B", MCT_Float, "0.0"}};
    }
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {{"Result", MCT_Float, ""}};
    }

    // Compile 签名：(MaterialCompiler*, Node*)
    // - compiler 是课6 的抽象基类指针（实际传入 HLSLTranslator——两层架构，
    //   对照 UE 表达式只认识 FMaterialCompiler*，背后是 FHLSLMaterialTranslator）
    // - node 让表达式能调用 CompileInputPin(node, "A") 递归找上游
    std::vector<int32_t> Compile(MaterialCompiler* c, Node* node) const override {
        int32_t a = c->CompileInputPin(node, "A");
        int32_t b = c->CompileInputPin(node, "B");
        return {c->Add(a, b)};
    }
};
```

**关于 Compile 双参数签名**：表达式需要调用 `c->CompileInputPin(node, "A")` 来递归编译上游，必须知道当前节点是谁。把 `node` 作为参数传入比存成成员变量（`Node* ownerNode_`）更安全——后者要求编译器在每次调用前重置成员，多线程/重入时会出问题。

#### 4.2 其余数学表达式（相同模式）

| 文件 | Compile 核心代码 |
|------|-----------------|
| ExprSubtract | `return {c->Subtract(a, b)};` |
| ExprMultiply | `return {c->Multiply(a, b)};` |
| ExprDivide | `return {c->Divide(a, b)};` |
| ExprPower | `return {c->Power(base, exp)};` |
| ExprLerp | `return {c->Lerp(a, b, alpha)};` — 输入: A, B, Alpha |
| ExprClamp | `return {c->Clamp(x, min, max)};` — 输入: X, Min, Max |
| ExprAbs | `return {c->Abs(x)};` — 输入: X |

这些表达式**没有可编辑字段**（所有数据来自上游引脚），所以反射注册里**只有 ME_DISPLAY_NAME 和 ME_CATEGORY**，没有 ME_FIELD。但 ME_BEGIN_CLASS / ME_END_CLASS 仍然必须写——它们生成 `GetClassDesc()` 重写，否则基类的纯虚函数没被重写会编译失败。

#### 4.3 常量表达式（有反射字段）

```cpp
// ExprConstant.h
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Compiler/Public/MaterialCompiler.h"

class ExprConstant : public Expression {
public:
    // 反射要求字段可访问（public 或 friend）
    float value_ = 0.0f;

    // 反射注册：声明字段 + 元数据
    // 字段名通过 #FieldName 字符串化自动得到（无需手写 "value_"）
    ME_BEGIN_CLASS(ExprConstant)
        ME_DISPLAY_NAME("Constant")
        ME_CATEGORY("Constants")
        ME_FIELD(ExprConstant, value_, 0.0f)
    ME_END_CLASS(ExprConstant)

    std::vector<ExpressionPinDesc> GetInputPins() const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {{"Output", MCT_Float, ""}};
    }

    std::vector<int32_t> Compile(MaterialCompiler* c, Node*) const override {
        return {c->Constant(value_)};
    }

    // 注意：不写 GetParameters/SetParameter/GetParameter！
    // 基类 Expression 的非虚实现会自动通过反射 ClassDesc 处理 value_ 字段
};
```

```cpp
// ExprConstant3Vector.h
#pragma once
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Compiler/Public/MaterialCompiler.h"

class ExprConstant3Vector : public Expression {
public:
    float r_ = 0.0f, g_ = 0.0f, b_ = 0.0f;

    ME_BEGIN_CLASS(ExprConstant3Vector)
        ME_DISPLAY_NAME("Constant3Vector")
        ME_CATEGORY("Constants")
        ME_FIELD(ExprConstant3Vector, r_, 0.0f)
        ME_FIELD(ExprConstant3Vector, g_, 0.0f)
        ME_FIELD(ExprConstant3Vector, b_, 0.0f)
    ME_END_CLASS(ExprConstant3Vector)

    std::vector<ExpressionPinDesc> GetInputPins() const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override {
        return {{"Output", MCT_Float3, ""}};
    }

    std::vector<int32_t> Compile(MaterialCompiler* c, Node*) const override {
        return {c->Constant3(r_, g_, b_)};
    }
};
```

### 5. 行数对比（说明反射的价值）

| 项目 | 旧手写方案 | 反射方案 |
|------|-----------|----------|
| 字段声明 | 3 行 | 3 行 |
| 参数 Get/Set 代码 | 19 行（3 个虚函数 if-else） | **0 行**（基类自动） |
| 反射宏 | 0 行 | 5 行（声明式） |
| **参数相关总行数** | **19 行** | **5 行** |
| 类型安全 | 运行时（json 检查） | **编译期**（模板偏特化） |
| 字段名一致性 | 手写 3 次，易写错 | 宏 1 次字符串化 |

反射不是为了"少写代码"，而是为了**把字段元信息（名字、类型、地址）从运行时字符串提升为编译期数据**。

---

## 验证

修改 `main.cpp`，测试完整的编译管线：

```cpp
#include <QApplication>
#include <QWidget>
#include "Core/Public/Logger.h"
#include "MaterialGraph/Public/Graph.h"
#include "MaterialGraph/Public/NodeFactory.h"
#include "Compiler/Public/HLSLTranslator.h"   // 两层架构：实现类（基类 MaterialCompiler 是抽象接口）
#include "Expression/Public/ExpressionRegistry.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 1. 注册表达式
    RegisterAllExpressions();

    // 2. 设置工厂（引脚类型用课6 的 MCT_* bitmask）
    NodeFactory::GetInstance().Register("ExprConstant3Vector", "Constant3Vector", "Constants", {
        {"Output", MCT_Float3, EPinDataDirection::Output}
    });
    NodeFactory::GetInstance().Register("ExprAdd", "Add", "Math", {
        {"A", MCT_Float, EPinDataDirection::Input},
        {"B", MCT_Float, EPinDataDirection::Input},
        {"Result", MCT_Float, EPinDataDirection::Output}
    });

    // 3. 创建图
    Graph graph;
    auto nodeColor = NodeFactory::GetInstance().Create("ExprConstant3Vector", {0, 0});
    nodeColor->parameters["r_"] = 1.0f;  // 红色（注意字段名 r_，和反射里一致）
    nodeColor->parameters["g_"] = 0.0f;
    nodeColor->parameters["b_"] = 0.0f;
    graph.AddNode(nodeColor);

    auto nodeAdd = NodeFactory::GetInstance().Create("ExprAdd", {300, 0});
    graph.AddNode(nodeAdd);

    // 连接
    graph.Connect(nodeColor->outputPins[0].id, nodeAdd->FindInputPin("A")->id);
    graph.Connect(nodeAdd->outputPins[0].id,
                  graph.GetOutputNode()->FindInputPin("BaseColor")->id);

    // 4. 编译（HLSLTranslator 是 MaterialCompiler 的实现）
    HLSLTranslator compiler;
    auto result = compiler.Compile(&graph);

    if (result.success) {
        ME_LOG_INFO("=== 编译成功 ===");
        ME_LOG_INFO("%s", result.hlsl_code.c_str());
    } else {
        ME_LOG_ERROR("编译失败: %s", result.error_message.c_str());
    }

    QWidget window;
    window.setWindowTitle("Material Editor - Compile Test");
    window.resize(800, 600);
    window.show();
    return app.exec();
}
```

**预期输出**（HLSL）：
```
[INFO] === 编译成功 ===
    // Material outputs
    // BaseColor = float3(1.000000, 0.000000, 0.000000)
```

B 引脚未连接，`CompileInputPin` 走默认值 `"0.0"`（`ParseDefaultValue`）得到常量表达式；`Add` 发现两边表达式都 `IsConstant()` → 立即折叠成新的常量块（课 6 的 `ConstResultValue` 路径），**没有任何加法指令**。

---

## 扩展验证：向量折叠（课6 表达式树的核心能力）

把上面的测试改成"**两个 Constant3Vector 相加**"，验证课6 表达式树的向量常数折叠：

```cpp
// 两个常量向量节点
auto nodeC1 = NodeFactory::GetInstance().Create("ExprConstant3Vector", {0, 0});
nodeC1->parameters["r_"] = 1.0f; nodeC1->parameters["g_"] = 0.0f; nodeC1->parameters["b_"] = 0.0f;  // 红 (1,0,0)

auto nodeC2 = NodeFactory::GetInstance().Create("ExprConstant3Vector", {0, 100});
nodeC2->parameters["r_"] = 0.0f; nodeC2->parameters["g_"] = 1.0f; nodeC2->parameters["b_"] = 0.0f;  // 绿 (0,1,0)

auto nodeAdd = NodeFactory::GetInstance().Create("ExprAdd", {300, 0});
// 连接：C1 → Add.A，C2 → Add.B，Add → Output.BaseColor
graph.Connect(nodeC1->outputPins[0].id, nodeAdd->FindInputPin("A")->id);
graph.Connect(nodeC2->outputPins[0].id, nodeAdd->FindInputPin("B")->id);
graph.Connect(nodeAdd->outputPins[0].id, graph.GetOutputNode()->FindInputPin("BaseColor")->id);

auto result = compiler.Compile(&graph);
```

**预期：`Add((1,0,0), (0,1,0))` 编译期折叠成 `(1,1,0)`**——因为课6 的 `Constant3` 建的是 `UniformConstant(Vec4(1,0,0,0), MCT_Float3)` 表达式叶子，`Add` 发现两边表达式的树都 `IsConstant()`，就对两棵树 `GetNumberValue` 求值、逐分量相加，经 `ConstResultValue` 折成一个新的常量叶子。生成的 HLSL 只有一个常量、**没有加法指令**：

```
    // Material outputs
    // BaseColor = float3(1.000000, 1.000000, 0.000000)
```

> 如果折叠没生效，HLSL 会是 `float3 Local0 = float3(1,0,0); float3 Local1 = float3(0,1,0); float3 Local2 = Local0 + Local1;`——说明 `Constant3` 没走表达式路径（检查课6 的 `Constant3` 是否建 `UniformConstant` 叶子并挂到 chunk 上），或 `Add` 没走"两边 `IsConstant()` → 求值折叠"的轨道（错走了 `UniformFoldedMath` 建树轨道或纯 HLSL 轨道）。这正是课6 表达式树要验证的。

---

## UE5 参考（相对 `Engine/` 路径）

每个表达式对照 UE5 真实实现——UE 的 `UMaterialExpression` 子类结构和我们一样：UPROPERTY 字段 + `Compile(FMaterialCompiler*)` 方法。

| 我们的表达式 | UE 对应 | UE 源码位置（相对 `Engine/`）|
|-------------|---------|------------------------------|
| `ExprAdd` | `UMaterialExpressionAdd` | `Source/Runtime/Engine/Private/Materials/MaterialExpressionAdd.cpp` |
| `ExprSubtract` | `UMaterialExpressionSubtract` | 同目录 `MaterialExpressionSubtract.cpp` |
| `ExprMultiply` | `UMaterialExpressionMultiply` | `MaterialExpressionMultiply.cpp` |
| `ExprDivide` | `UMaterialExpressionDivide` | `MaterialExpressionDivide.cpp` |
| `ExprPower` | `UMaterialExpressionPower` | `MaterialExpressionPower.cpp` |
| `ExprLerp` | `UMaterialExpressionLinearInterpolate` | `MaterialExpressionLinearInterpolate.cpp` |
| `ExprClamp` | `UMaterialExpressionClamp` | `MaterialExpressionClamp.cpp` |
| `ExprAbs` | `UMaterialExpressionAbs` | `MaterialExpressionAbs.cpp` |
| `ExprConstant` | `UMaterialExpressionConstant` | `MaterialExpressionConstant.cpp` |
| `ExprConstant3Vector` | `UMaterialExpressionConstant3Vector` | `MaterialExpressionConstant3Vector.cpp` |

**UE 的 `UMaterialExpressionAdd::Compile` 真实长什么样**（对照我们的 `ExprAdd::Compile`）：

```cpp
// UE: Engine/Source/Runtime/Engine/Private/Materials/MaterialExpressionAdd.cpp
int32 UMaterialExpressionAdd::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    int32 Result = INDEX_NONE;
    int32 A = Compiler->CompileInput(A_input);   // ← 等同我们的 c->CompileInputPin(node, "A")
    int32 B = Compiler->CompileInput(B_input);
    if (A != INDEX_NONE && B != INDEX_NONE)
    {
        Result = Compiler->Add(A, B);             // ← 等同我们的 c->Add(a, b)
    }
    return Result;
}
```

**逐行对照**：
- UE `Compiler->CompileInput(A_input)` ≈ 我们 `c->CompileInputPin(node, "A")`——递归编译上游引脚
- UE `Compiler->Add(A, B)` ≈ 我们 `c->Add(a, b)`——调编译器算子生成代码块
- UE 返回 `int32`（chunk 索引）≈ 我们返回 `int32_t`

**几乎一模一样**——我们的表达式设计就是 UE 的教学复刻。区别只在：UE 的 `CompileInput` 接收 `FExpressionInput`（带引脚对象引用），我们接收引脚名字字符串；UE 用 `INDEX_NONE`(-1) 表错误，我们也用负数哨兵。

**搜索关键词**（在 UE 源码）：`UMaterialExpressionAdd::Compile`、`UMaterialExpressionConstant::Compile`、`Compiler->Add`、`Compiler->Constant`。

---

## 完成标志

- [ ] 10 个表达式全部实现（统一反射驱动模式）
- [ ] ExpressionRegistry 注册和创建正常（继承 Singleton）
- [ ] 端到端编译：Graph → Compiler → HLSL 代码输出
- [ ] 常数折叠工作：Constant(2)+Constant(3) 不生成加法代码
- [ ] 所有表达式都通过 ME_BEGIN_CLASS/ME_END_CLASS 注册反射元数据，没有手写参数虚函数
