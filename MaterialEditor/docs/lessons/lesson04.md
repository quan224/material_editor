# 课4：节点工厂 + 图遍历

## 目标

实现 NodeFactory（注册和创建带引脚的节点）和 GraphCompiler（拓扑排序和循环检测）。

---

## 背景知识

### 为什么需要 NodeFactory？
课3中 `Graph::AddNode()` 创建的是空节点（没有引脚）。每个表达式类型的引脚布局不同（Add有2输入1输出，Constant3Vector有0输入1输出）。**NodeFactory** 就是统一管理这些"模板"——注册每个类型怎么创建，需要哪些引脚。

UE5 中每个 `UMaterialExpression` 子类在构造函数中设置自己的输入输出，我们用工厂方法替代。

### 为什么需要拓扑排序？
编译材质时，编译器必须**先编译被依赖的节点，再编译依赖它的节点**。比如 A→B→C，编译顺序必须是 A, B, C。拓扑排序（DFS 后序遍历）给出这个顺序。

---

## 操作步骤

### 1. 创建文件

```
src/MaterialGraph/Public/NodeFactory.h
src/MaterialGraph/Private/NodeFactory.cpp
src/MaterialGraph/Public/GraphCompiler.h
src/MaterialGraph/Private/GraphCompiler.cpp
```

### 2. NodeFactory.h

```cpp
#pragma once
#include "MaterialGraph/Public/Node.h"
#include <functional>
#include <map>
#include <vector>
#include <string>

class NodeFactory {
public:
    // 引脚描述（注册时用）
    struct PinTemplate {
        std::string name;
        EValueType type;
        EPinDataDirection direction;
        std::string defaultValue;
    };

    // 节点类型信息
    struct NodeTypeInfo {
        std::string typeName;      // "ExprAdd"
        std::string displayName;   // "Add"
        std::string category;      // "Math"
        std::vector<PinTemplate> pins;
    };

    // 注册一个表达式类型
    void Register(const std::string& typeName,
                  const std::string& displayName,
                  const std::string& category,
                  const std::vector<PinTemplate>& pins);

    // 创建节点（带引脚）
    Ref<Node> Create(const std::string& typeName, const QPointF& position) const;

    // 获取所有已注册类型
    const std::vector<NodeTypeInfo>& GetAllTypes() const { return registry_; }

    // 按类别获取
    std::vector<const NodeTypeInfo*> GetTypesByCategory(const std::string& category) const;

    // 查找类型信息
    const NodeTypeInfo* FindType(const std::string& typeName) const;

private:
    std::vector<NodeTypeInfo> registry_;
    std::map<std::string, size_t> nameToIndex_;
};
```

### 3. NodeFactory.cpp

```cpp
#include "MaterialGraph/Public/NodeFactory.h"

void NodeFactory::Register(const std::string& typeName,
                            const std::string& displayName,
                            const std::string& category,
                            const std::vector<PinTemplate>& pins) {
    nameToIndex_[typeName] = registry_.size();
    registry_.push_back({typeName, displayName, category, pins});
}

Ref<Node> NodeFactory::Create(const std::string& typeName, const QPointF& position) const {
    auto it = nameToIndex_.find(typeName);
    if (it == nameToIndex_.end()) return nullptr;

    const auto& info = registry_[it->second];
    auto node = MakeRef<Node>();
    node->id = UUID::Generate();
    node->typeName = info.typeName;
    node->title = info.displayName;
    node->position = position;

    for (const auto& pinTpl : info.pins) {
        Pin pin;
        pin.id = UUID::Generate();
        pin.name = pinTpl.name;
        pin.type = pinTpl.type;
        pin.direction = pinTpl.direction;
        pin.ownerNodeId = node->id;
        pin.defaultValue = pinTpl.defaultValue;

        if (pinTpl.direction == EPinDataDirection::Input) {
            node->inputPins.push_back(pin);
        } else {
            node->outputPins.push_back(pin);
        }
    }
    return node;
}

std::vector<const NodeFactory::NodeTypeInfo*> NodeFactory::GetTypesByCategory(
    const std::string& category) const {
    std::vector<const NodeTypeInfo*> result;
    for (const auto& info : registry_) {
        if (info.category == category) result.push_back(&info);
    }
    return result;
}

const NodeFactory::NodeTypeInfo* NodeFactory::FindType(const std::string& typeName) const {
    auto it = nameToIndex_.find(typeName);
    if (it == nameToIndex_.end()) return nullptr;
    return &registry_[it->second];
}
```

**讲解**：
- `Register` 注册类型模板（名称、分类、引脚列表）
- `Create` 根据模板创建完整节点（带引脚），每个引脚分配独立 UUID
- 后续课7中，注册代码会放在一个 `RegisterAllExpressions()` 函数中

### 4. GraphCompiler.h

```cpp
#pragma once
#include "MaterialGraph/Public/Graph.h"
#include <vector>
#include <set>

class GraphCompiler {
public:
    explicit GraphCompiler(Graph* graph);

    // 从输出节点开始反向拓扑排序
    // 返回按编译顺序排列的节点列表（被依赖的在前）
    // 出参 hasCycle：是否检测到循环（循环的节点会被跳过，不会出现在结果中）
    std::vector<Node*> TopologicalSort(bool* hasCycle = nullptr);

    // 检测是否存在循环引用
    bool HasCycles() const;

private:
    void Visit(Node* node,
               std::set<UUID>& visited,
               std::set<UUID>& inStack,
               std::vector<Node*>& result,
               bool& hasCycle);

    Graph* graph_;
};
```

### 5. GraphCompiler.cpp

```cpp
#include "MaterialGraph/Public/GraphCompiler.h"
#include "Core/Public/Logger.h"
#include <functional>  // std::function (HasCycles 中使用)

GraphCompiler::GraphCompiler(Graph* graph) : graph_(graph) {}

std::vector<Node*> GraphCompiler::TopologicalSort(bool* hasCycle) {
    std::vector<Node*> result;
    std::set<UUID> visited;
    std::set<UUID> inStack;
    bool cycle = false;

    Node* output = graph_->GetOutputNode();
    if (!output) {
        ME_LOG_ERROR("No output node found!");
        if (hasCycle) *hasCycle = false;
        return result;
    }

    // 从输出节点的每个输入引脚开始反向遍历
    for (const auto& pin : output->inputPins) {
        if (pin.IsConnected()) {
            // 找到连接的上游节点
            // 注意：PinConnection 里只存 otherNodeId（对端节点），
            // 对于输入引脚，"对端" 就是上游输出节点
            for (const auto& conn : pin.connections) {
                Node* upstream = graph_->FindNode(conn.otherNodeId);
                if (upstream) {
                    Visit(upstream, visited, inStack, result, cycle);
                }
            }
        }
    }

    if (hasCycle) *hasCycle = cycle;
    return result;
}

void GraphCompiler::Visit(Node* node,
                           std::set<UUID>& visited,
                           std::set<UUID>& inStack,
                           std::vector<Node*>& result,
                           bool& hasCycle) {
    if (visited.count(node->id)) return;

    if (inStack.count(node->id)) {
        ME_LOG_ERROR("Cycle detected at node: %s", node->title.c_str());
        hasCycle = true;  // 标记检测到环，但不立即返回——继续处理其他分支
        return;
    }

    inStack.insert(node->id);

    // 遍历该节点的所有输入引脚的连接
    for (const auto& pin : node->inputPins) {
        if (pin.IsConnected()) {
            for (const auto& conn : pin.connections) {
                Node* upstream = graph_->FindNode(conn.otherNodeId);
                if (upstream) {
                    Visit(upstream, visited, inStack, result, hasCycle);
                }
            }
        }
    }

    inStack.erase(node->id);
    visited.insert(node->id);
    result.push_back(node);  // 后序添加：所有依赖都在前面
}

bool GraphCompiler::HasCycles() const {
    std::set<UUID> visited;
    std::set<UUID> inStack;

    Node* output = graph_->GetOutputNode();
    if (!output) return false;

    // 简化版：用 const_cast 复用 Visit 逻辑，或单独写检查
    // 这里用一个独立的递归检查
    std::function<bool(const Node*)> checkCycle = [&](const Node* node) -> bool {
        if (inStack.count(node->id)) return true;  // 检测到环
        if (visited.count(node->id)) return false;

        inStack.insert(node->id);
        for (const auto& pin : node->inputPins) {
            for (const auto& conn : pin.connections) {
                const Node* upstream = graph_->FindNode(conn.otherNodeId);
                if (upstream && checkCycle(upstream)) return true;
            }
        }
        inStack.erase(node->id);
        visited.insert(node->id);
        return false;
    };

    return checkCycle(output);
}
```

**讲解**：
- `TopologicalSort()` 使用 DFS 后序遍历。关键：**先递归访问所有上游节点，最后把自己加入结果**，这样被依赖的节点自然排在前面
- `HasCycles()` 在遍历时维护 `inStack` 集合。如果访问到一个已在栈中的节点，说明存在环
- 从输出节点的输入引脚开始反向遍历，找到所有被连接的上游节点

**UE5 参考**：
- 遍历逻辑参考 `HLSLMaterialTranslator.cpp` 中 `TranslateMaterial()` 的实现
- 搜索 `CompileProperty` 关键词

---

## 验证

```cpp
#include "MaterialGraph/Public/NodeFactory.h"
#include "MaterialGraph/Public/GraphCompiler.h"

// 注册几个测试类型
auto& factory = NodeFactory::GetInstance();
factory.Register("ExprConstant3Vector", "Constant3Vector", "Constants", {
    {"Output", EValueType::Float3, EPinDataDirection::Output}
});
factory.Register("ExprAdd", "Add", "Math", {
    {"A", EValueType::Float3, EPinDataDirection::Input, "0.0"},
    {"B", EValueType::Float3, EPinDataDirection::Input, "0.0"},
    {"Result", EValueType::Float3, EPinDataDirection::Output}
});

// 创建图并添加节点
Graph graph;
auto nodeA = factory.Create("ExprConstant3Vector", {0, 0});
auto nodeB = factory.Create("ExprConstant3Vector", {0, 200});
auto nodeAdd = factory.Create("ExprAdd", {300, 100});

graph.AddNode(nodeA);
graph.AddNode(nodeB);
graph.AddNode(nodeAdd);

// 连接：A → Add.A, Add.Result → Output.BaseColor
graph.Connect(nodeA->outputPins[0].id, nodeAdd->FindInputPin("A")->id);
graph.Connect(nodeAdd->outputPins[0].id,
              graph.GetOutputNode()->FindInputPin("BaseColor")->id);

// 拓扑排序
GraphCompiler compiler(&graph);
bool hasCycle = false;
auto order = compiler.TopologicalSort(&hasCycle);
if (hasCycle) {
    ME_LOG_WARNING("Graph has cycles, some nodes were skipped");
}
for (auto* n : order) {
    ME_LOG_INFO("Compile order: %s (%s)", n->title.c_str(), n->typeName.c_str());
}
// 预期输出顺序: ExprConstant3Vector, ExprAdd
```

---

## 完成标志

- [ ] NodeFactory 可以注册和创建带引脚的节点
- [ ] 创建的节点引脚名称、类型、方向正确
- [ ] 拓扑排序输出正确的编译顺序
- [ ] 循环引用时 HasCycles() 返回 true
