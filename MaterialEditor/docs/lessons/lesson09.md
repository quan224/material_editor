# 课9：命令行编译管线验证

## 目标

在进入 Qt UI 开发之前，先用命令行程序验证整个编译管线（Graph → Compiler → HLSL）能端到端工作。

---

## 背景知识

课1-8搭好了所有编译相关的代码，但还没整合测试过。本课写一个完整的测试程序，验证：

1. 创建图 → 添加节点 → 连接 → 拓扑排序
2. 编译器生成 CodeChunk 序列
3. HLSLGenerator 组装成完整 HLSL
4. 输出的 HLSL 可以手动验证正确性

这和 UE5 的 `MaterialEditor` 模块中的 `MaterialTest.cpp` 类似——先确保编译逻辑正确，再做 UI。

---

## 操作步骤

### 1. 创建测试文件

```
tests/test_compiler_pipeline.cpp
```

### 2. 完整测试代码

```cpp
#include <QApplication>
#include <QDebug>
#include "Core/Public/Logger.h"
#include "Core/Public/UUID.h"
#include "Core/Public/RefCounted.h"
#include "MaterialGraph/Public/Graph.h"
#include "MaterialGraph/Public/NodeFactory.h"
#include "MaterialGraph/Public/GraphCompiler.h"
#include "Expression/Public/MaterialCompiler.h"  // 抽象接口在 L4（课6 分层裁决）
#include "Compiler/Public/TypeSystem.h"
#include "Expression/Public/ExpressionRegistry.h"

// 注册所有表达式（课7）
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

// ====== 辅助函数 ======

void SetupNodeFactory(NodeFactory& factory) {
    // 注册表达式类型到工厂（定义引脚布局）
    factory.Register("ExprConstant", "Constant", "Constants", {
        {"Value", EValueType::Float1, EPinDataDirection::Output}
    });
    factory.Register("ExprConstant3Vector", "Constant3Vector", "Constants", {
        {"Output", EValueType::Float3, EPinDataDirection::Output}
    });
    factory.Register("ExprAdd", "Add", "Math", {
        {"A", EValueType::Float1, EPinDataDirection::Input, 0.0f},
        {"B", EValueType::Float1, EPinDataDirection::Input, 0.0f},
        {"Result", EValueType::Float1, EPinDataDirection::Output}
    });
    factory.Register("ExprMultiply", "Multiply", "Math", {
        {"A", EValueType::Float1, EPinDataDirection::Input, 0.0f},
        {"B", EValueType::Float1, EPinDataDirection::Input, 1.0f},
        {"Result", EValueType::Float1, EPinDataDirection::Output}
    });
    factory.Register("ExprClamp", "Clamp", "Math", {
        {"X", EValueType::Float1, EPinDataDirection::Input, 0.0f},
        {"Min", EValueType::Float1, EPinDataDirection::Input, 0.0f},
        {"Max", EValueType::Float1, EPinDataDirection::Input, 1.0f},
        {"Result", EValueType::Float1, EPinDataDirection::Output}
    });
    factory.Register("ExprLerp", "Lerp", "Math", {
        {"A", EValueType::Float1, EPinDataDirection::Input, 0.0f},
        {"B", EValueType::Float1, EPinDataDirection::Input, 1.0f},
        {"Alpha", EValueType::Float1, EPinDataDirection::Input, 0.5f},
        {"Result", EValueType::Float1, EPinDataDirection::Output}
    });
}

// ====== 测试用例 ======

// 测试1：简单常数 → BaseColor
void Test_Constant3ToBaseColor() {
    ME_LOG_INFO("=== 测试1: Constant3(1,0,0) → BaseColor ===");

    auto& factory = NodeFactory::GetInstance();
    SetupNodeFactory(factory);

    Graph graph;
    auto color = factory.Create("ExprConstant3Vector", {0, 0});
    color->parameters["R"] = 1.0f;
    color->parameters["G"] = 0.0f;
    color->parameters["B"] = 0.0f;
    graph.AddNode(color);

    // 连接到输出节点的 BaseColor
    auto* outputNode = graph.GetOutputNode();
    graph.Connect(color->outputPins[0].id,
                  outputNode->FindInputPin("BaseColor")->id);

    // 编译
    MaterialCompiler compiler;
    auto result = compiler.Compile(&graph);

    if (result.success) {
        ME_LOG_INFO("编译成功！HLSL 长度: %zu 字节", result.hlslCode.size());
        ME_LOG_INFO("HLSL 输出:\n%s", result.hlslCode.c_str());
    } else {
        ME_LOG_ERROR("编译失败: %s", result.errorMessage.c_str());
    }
}

// 测试2：常数 → 乘法 → 加法 → BaseColor（多节点链）
void Test_ComplexChain() {
    ME_LOG_INFO("=== 测试2: Constant3(1,0,0) → Multiply(0.5) → Add(0.1) → BaseColor ===");

    auto& factory = NodeFactory::GetInstance();
    SetupNodeFactory(factory);

    Graph graph;

    // 节点：Constant3Vector(1, 0, 0) → 红色
    auto color = factory.Create("ExprConstant3Vector", {0, 0});
    color->parameters["R"] = 1.0f;
    graph.AddNode(color);

    // 节点：Multiply（Float3 * Float1）
    // 注意：这里需要 Float3 版本的 Multiply
    // 先用简单 Float1 测试

    // 节点：Constant(0.5)
    auto factor = factory.Create("ExprConstant", {200, 0});
    factor->parameters["Value"] = 0.5f;
    graph.AddNode(factor);

    // 节点：Constant(0.1)
    auto offset = factory.Create("ExprConstant", {200, 200});
    offset->parameters["Value"] = 0.1f;
    graph.AddNode(offset);

    // 节点：Multiply
    auto mul = factory.Create("ExprMultiply", {400, 0});
    graph.AddNode(mul);

    // 节点：Add
    auto add = factory.Create("ExprAdd", {600, 0});
    graph.AddNode(add);

    // 连接：factor → mul.A
    graph.Connect(factor->outputPins[0].id, mul->FindInputPin("A")->id);
    // 连接：offset → mul.B
    graph.Connect(offset->outputPins[0].id, mul->FindInputPin("B")->id);
    // 连接：mul → add.A
    graph.Connect(mul->outputPins[0].id, add->FindInputPin("A")->id);

    // 给 add.B 一个默认值（不连接，使用引脚默认值 0.0）

    // 连接到输出
    auto* outputNode = graph.GetOutputNode();
    graph.Connect(add->outputPins[0].id,
                  outputNode->FindInputPin("BaseColor")->id);

    // 拓扑排序验证
    GraphCompiler graphComp(&graph);
    auto order = graphComp.TopologicalSort();
    ME_LOG_INFO("拓扑排序结果（%zu 个节点）:", order.size());
    for (auto* n : order) {
        ME_LOG_INFO("  - %s (%s)", n->title.c_str(), n->typeName.c_str());
    }

    // 编译
    MaterialCompiler compiler;
    auto result = compiler.Compile(&graph);

    if (result.success) {
        ME_LOG_INFO("编译成功！");
        ME_LOG_INFO("HLSL 输出:\n%s", result.hlslCode.c_str());
    } else {
        ME_LOG_ERROR("编译失败: %s", result.errorMessage.c_str());
    }
}

// 测试3：常数折叠验证
void Test_ConstantFolding() {
    ME_LOG_INFO("=== 测试3: 常数折叠 Constant(2) + Constant(3) ===");

    auto& factory = NodeFactory::GetInstance();
    SetupNodeFactory(factory);

    Graph graph;

    auto c2 = factory.Create("ExprConstant", {0, 0});
    c2->parameters["Value"] = 2.0f;
    graph.AddNode(c2);

    auto c3 = factory.Create("ExprConstant", {0, 200});
    c3->parameters["Value"] = 3.0f;
    graph.AddNode(c3);

    auto add = factory.Create("ExprAdd", {200, 0});
    graph.AddNode(add);

    graph.Connect(c2->outputPins[0].id, add->FindInputPin("A")->id);
    graph.Connect(c3->outputPins[0].id, add->FindInputPin("B")->id);

    auto* outputNode = graph.GetOutputNode();
    graph.Connect(add->outputPins[0].id,
                  outputNode->FindInputPin("Metallic")->id);

    MaterialCompiler compiler;
    auto result = compiler.Compile(&graph);

    if (result.success) {
        ME_LOG_INFO("编译成功！");
        // 验证：HLSL 中应该直接包含常量 5.0，不生成加法代码
        std::string& code = const_cast<std::string&>(result.hlslCode);
        if (code.find("5.0") != std::string::npos || code.find("5.000000") != std::string::npos) {
            ME_LOG_INFO("常数折叠正确：2+3=5 直接出现在代码中");
        } else {
            ME_LOG_WARNING("常数折叠可能未生效，检查生成的代码");
        }
        ME_LOG_INFO("HLSL:\n%s", code.c_str());
    } else {
        ME_LOG_ERROR("编译失败: %s", result.errorMessage.c_str());
    }
}

// 测试3b：向量常数折叠（扩展版核心能力，直接测编译器 API）
// 验证课6 扩展版的 variant 折叠：Constant3 + Constant3 → 编译期算成 Vec3
void Test_VectorConstantFolding() {
    ME_LOG_INFO("=== 测试3b: 向量折叠 Constant3(1,0,0) + Constant3(0,1,0) ===");
    MaterialCompiler c;
    int32_t a = c.Constant3(1.0f, 0.0f, 0.0f);
    int32_t b = c.Constant3(0.0f, 1.0f, 0.0f);
    int32_t s = c.Add(a, b);

    if (c.IsConstant(s)) {
        // GetConstantValue 返回 variant，取 Vec3
        Vec3 v = std::get<Vec3>(c.GetConstantValue(s));
        bool ok = (v.x == 1.0f && v.y == 1.0f && v.z == 0.0f);
        ME_LOG_INFO("向量折叠: (1,0,0)+(0,1,0) = (%f,%f,%f) %s",
                    v.x, v.y, v.z, ok ? "PASS" : "FAIL");
    } else {
        ME_LOG_ERROR("向量折叠未生效：Add 结果不是常量（检查课6 variant 折叠）");
    }
}

// 测试4：循环检测
void Test_CycleDetection() {
    ME_LOG_INFO("=== 测试4: 循环检测 ===");

    auto& factory = NodeFactory::GetInstance();
    SetupNodeFactory(factory);

    Graph graph;

    auto add1 = factory.Create("ExprAdd", {0, 0});
    auto add2 = factory.Create("ExprAdd", {200, 0});
    graph.AddNode(add1);
    graph.AddNode(add2);

    // 构造环：add1 → add2 → add1
    graph.Connect(add1->outputPins[0].id, add2->FindInputPin("A")->id);
    graph.Connect(add2->outputPins[0].id, add1->FindInputPin("A")->id);

    GraphCompiler graphComp(&graph);
    if (graphComp.HasCycles()) {
        ME_LOG_INFO("循环检测正确：检测到环");
    } else {
        ME_LOG_ERROR("循环检测失败：未检测到环");
    }
}

// 测试5：类型推导
void Test_TypeSystem() {
    ME_LOG_INFO("=== 测试5: 类型推导 ===");

    auto r1 = TypeSystem::GetArithmeticResultType(EValueType::Float1, EValueType::Float3);
    assert(r1 == EValueType::Float3);
    ME_LOG_INFO("Float1 + Float3 = Float3 ✓");

    auto r2 = TypeSystem::GetArithmeticResultType(EValueType::Float2, EValueType::Float2);
    assert(r2 == EValueType::Float2);
    ME_LOG_INFO("Float2 + Float2 = Float2 ✓");

    auto r3 = TypeSystem::GetArithmeticResultType(EValueType::Float3, EValueType::Float3);
    assert(r3 == EValueType::Float3);
    ME_LOG_INFO("Float3 + Float3 = Float3 ✓");

    ME_LOG_INFO("类型推导测试全部通过");
}

// ====== main ======

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 初始化表达式注册表
    RegisterAllExpressions();

    // 运行所有测试
    Test_TypeSystem();
    Test_CycleDetection();
    Test_ConstantFolding();
    Test_VectorConstantFolding();   // 扩展版：向量折叠
    Test_Constant3ToBaseColor();
    Test_ComplexChain();

    ME_LOG_INFO("\n========== 所有测试完成 ==========");

    return 0;  // 不进入事件循环，直接退出
}
```

### 3. 更新 CMakeLists.txt

在主 `CMakeLists.txt` 中添加测试目标：

```cmake
# 在 CMakeLists.txt 末尾添加

# 编译管线测试
add_executable(TestCompilerPipeline
    tests/test_compiler_pipeline.cpp
    ${SOURCES}
)
target_link_libraries(TestCompilerPipeline
    PRIVATE Qt6::Core Qt6::Widgets
    glm::glm nlohmann_json::nlohmann_json
)
target_include_directories(TestCompilerPipeline PRIVATE src/)
```

### 4. 运行测试

```bash
cd build
cmake --build . --target TestCompilerPipeline
./TestCompilerPipeline
```

**预期输出**（关键行）：

```
[INFO] 类型推导测试全部通过
[INFO] 循环检测正确：检测到环
[INFO] 常数折叠正确：2+3=5 直接出现在代码中
[INFO] === 测试1: Constant3(1,0,0) → BaseColor ===
[INFO] 编译成功！
[INFO] === 测试2: Constant3(1,0,0) → Multiply → Add → BaseColor ===
[INFO] 拓扑排序结果（5 个节点）:
[INFO]   - Constant (ExprConstant)
[INFO]   - Constant (ExprConstant)
[INFO]   - Multiply (ExprMultiply)
[INFO]   - Add (ExprAdd)
[INFO] 编译成功！
```

### 5. 手动验证 HLSL

测试2 输出的 HLSL 应该包含：

```hlsl
// 变量声明区
float Local0 = 0.5;
float Local1 = 0.1;
float Local2 = Local0 * Local1;
float Local3 = Local2 + 0.0;

// 材质属性赋值
BaseColor = float3(Local3, Local3, Local3);
```

以及完整的 PBR 光照 main() 函数。

---

## 常见问题

### 问题1：链接错误 — undefined reference to `RegisterAllExpressions`

确保 `ExpressionRegistry.cpp` 和所有 Expr*.h 中定义的函数都在编译列表中。如果使用 `file(GLOB_RECURSE)` 应该自动包含。

### 问题2：HLSL 中变量名不对

检查 `MaterialCompiler::AddCodeChunk` 中 `symbolName` 的生成逻辑。应该是 `Local0`, `Local1`, `Local2`...

### 问题3：拓扑排序为空

检查 `Graph::GetOutputNode()` 是否正确返回了输出节点。检查 `Connection` 中 `targetNodeId` 和 `targetPinId` 是否匹配。

---

## UE5 参考（相对 `Engine/` 路径）

- `Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp`
- 搜索 `TranslateMaterial` — 编译入口（对照我们的 `MaterialCompiler::Compile`）
- 搜索 `GetMaterialShaderCode` — 最终代码输出（对照 `GenerateCode`/`HLSLGenerator::Generate`）
- 搜索 `VerifyShaderIsValid` — 验证逻辑（对照我们的 `result.success` 检查）

**UE 的端到端验证**：UE 有完整的自动化测试（`MaterialTest.cpp`、`HLSLMaterialTranslatorTest.cpp`），覆盖各种节点组合 + 折叠 + 类型推导。我们的 `test_compiler_pipeline.cpp` 就是教学版——同样的思路（构造图 → 编译 → 验证 HLSL），只是测试用例少（UE 有几百个）。

---

## 完成标志

- [ ] 5 个测试全部通过
- [ ] 生成的 HLSL 包含正确的变量声明和 PBR 光照
- [ ] 常数折叠在 HLSL 输出中可见（无多余运算）
- [ ] 循环检测正确工作
- [ ] 类型推导规则验证通过
