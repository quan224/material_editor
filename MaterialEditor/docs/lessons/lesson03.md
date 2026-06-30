# 课3：图数据模型

## 目标

实现 Pin（引脚）、Node（节点）、Connection（连接）、Graph（图容器）四个核心数据类。这是整个编辑器的数据层，等同于 UE5 的 EdGraph 体系。

---

## 背景知识

UE5 材质编辑器中，所有数据都组织为一个"图"：

```
Graph（图）
 └── Node（节点）── 如 Add、Multiply、TextureSample 等
      ├── Input Pin（输入引脚）── 接收数据的入口
      └── Output Pin（输出引脚）── 输出数据的出口

Connection（连接）── 从一个 Output Pin 连到另一个 Input Pin
```

编译材质时，编译器从**输出节点**（材质属性节点）开始，沿连接反向遍历所有节点，确定编译顺序。

我们的数据模型和 UE5 的对应关系：

| 我们的类 | UE5 对应 | 源码位置 |
|---------|---------|---------|
| `Graph` | `UEdGraph` | `Engine/Classes/EdGraph/EdGraph.h` |
| `Node` | `UEdGraphNode` | `Engine/Classes/EdGraph/EdGraphNode.h` |
| `Pin` | `FEdGraphPin` | `Engine/Classes/EdGraph/EdGraphPin.h` |
| `Connection` | Pin 的 `LinkedTo` 数组 | 同上 |

---

## 操作步骤

### 1. 创建文件

```
src/MaterialGraph/Public/Types.h       — 枚举定义
src/MaterialGraph/Public/Pin.h         — 引脚（仅头文件）
src/MaterialGraph/Public/Node.h        — 节点头文件
src/MaterialGraph/Private/Node.cpp     — 节点实现
src/MaterialGraph/Public/Connection.h  — 连接（仅头文件）
src/MaterialGraph/Public/Graph.h       — 图容器头文件
src/MaterialGraph/Private/Graph.cpp    — 图容器实现
```

### 2. Types.h — 类型定义

```cpp
#pragma once
#include <cstdint>

// 引脚方向
enum class EPinDataDirection {
    Input,
    Output
};

// 引脚值类型（参考 UE5 的 EMaterialValueType，大幅精简）
enum class EValueType : uint32_t {
    Unknown = 0,
    Float1  = 1 << 0,   // 单浮点（标量）
    Float2  = 1 << 1,   // 二维向量
    Float3  = 1 << 2,   // 三维向量 / 颜色
    Float4  = 1 << 3,   // 四维向量
    // 后续可扩展: Int1, Bool, Texture2D 等
};

// 类型是否可以隐式转换（如 Float1 → Float3）
inline bool CanImplicitConvert(EValueType from, EValueType to) {
    if (from == to) return true;
    // Float1 可以扩展到任意 FloatN
    if (from == EValueType::Float1 && to != EValueType::Unknown) return true;
    return false;
}

// 获取类型的分量数
inline int GetComponentCount(EValueType type) {
    switch (type) {
        case EValueType::Float1: return 1;
        case EValueType::Float2: return 2;
        case EValueType::Float3: return 3;
        case EValueType::Float4: return 4;
        default: return 0;
    }
}

// 类型到字符串（HLSL 代码生成用）
inline const char* ValueTypeToString(EValueType type) {
    switch (type) {
        case EValueType::Float1: return "float";
        case EValueType::Float2: return "float2";
        case EValueType::Float3: return "float3";
        case EValueType::Float4: return "float4";
        default: return "unknown";
    }
}
```

**讲解**：
- `EValueType` 用普通递增值（`1 << n` 在历史上设计为"位标志"，但当前代码完全没有用到位运算，**保留 `1 << n` 写法只是为了未来扩展**——比如想表达"接受 Float1 或 Float2"时可以 `Float1 | Float2`。如果你不打算用这个特性，改成 `Float1 = 1, Float2 = 2, ...` 也可以，效果一样）
- `CanImplicitConvert` 定义类型兼容规则，这是连接引脚时的验证依据
- UE5 的规则更复杂（还有 Double、Int 等），我们只保留最常用的 Float1~4

**UE5 参考**：搜索 `EMaterialValueType` 在 `HLSLMaterialTranslator.h` 中

### 3. Pin.h — 引脚

```cpp
#pragma once
#include "MaterialGraph/Public/Types.h"
#include "Core/Public/UUID.h"
#include <string>
#include <vector>

class Pin {
public:
    UUID id = UUID::Invalid();
    std::string name;           // "A", "B", "Output", "BaseColor" 等
    EValueType type = EValueType::Unknown;
    EPinDataDirection direction = EPinDataDirection::Input;
    UUID ownerNodeId = UUID::Invalid();
    std::string defaultValue;  // 未连接时的默认值，字符串格式如 "0.5", "(0,0,1)", "(1,0,0,1)"

    // 连接信息 —— 这个引脚连到了对端的哪个引脚
    // 注意：字段名叫 otherPinId/otherNodeId（"对端引脚/对端节点"），
    // 不暗示方向。无论这个 Pin 是输入还是输出，"other" 始终指对端。
    // 比如输入引脚 A 持有的 connection 里，otherPinId 是上游输出引脚的 id；
    // 输出引脚 B 持有的 connection 里，otherPinId 是下游输入引脚的 id。
    struct PinConnection {
        UUID connectionId;
        UUID otherPinId;
        UUID otherNodeId;
    };
    std::vector<PinConnection> connections;

    // 检查是否可以连接到另一个引脚
    bool CanConnectTo(const Pin& other) const {
        // 1. 不能自连接（同方向）
        if (direction == other.direction) return false;
        // 2. 不能连接到自己所属的节点
        if (ownerNodeId == other.ownerNodeId) return false;
        // 3. 类型必须兼容
        EValueType srcType = (direction == EPinDataDirection::Output) ? type : other.type;
        EValueType dstType = (direction == EPinDataDirection::Input) ? type : other.type;
        return CanImplicitConvert(srcType, dstType);
    }

    bool IsConnected() const { return !connections.empty(); }

    // 输入引脚最多1个连接，输出引脚可多个
    bool IsInput() const { return direction == EPinDataDirection::Input; }
    bool IsOutput() const { return direction == EPinDataDirection::Output; }
};
```

**讲解**：
- 每个 Pin 有唯一 ID（UUID）
- `connections` 存储连接信息，包含 `connectionId`（方便直接断开连接，不用遍历查找）。输入引脚通常最多1个连接（一条线连进来），输出引脚可以有很多（一条线连出去到多个地方）
- `CanConnectTo` 做三个检查：方向、自身、类型兼容
- `defaultValue` 用字符串存储默认值（模仿 UE5 的 FString DefaultValue），格式如 "0.5"、"(0,0,1)"，编译时由编译器解析

**UE5 参考**：`E:\UE5\Engine\Source\Runtime\Engine\Classes\EdGraph\EdGraphPin.h` — 查看 `FEdGraphPin` 和 `FEdGraphPinType`

### 4. Node.h / Node.cpp — 节点

**Node.h**：
```cpp
#pragma once
#include "Core/Public/UUID.h"
#include "MaterialGraph/Public/Pin.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <QPointF>

class Node {
public:
    UUID id = UUID::Invalid();
    std::string title;           // 显示名称："Add", "Multiply"
    std::string typeName;        // 类型标识："ExprAdd", "ExprMultiply"
    QPointF position;            // 在图中的位置（UI 用）

    std::vector<Pin> inputPins;
    std::vector<Pin> outputPins;

    // 表达式参数（不同表达式有不同参数，用 JSON 存储）
    nlohmann::json parameters;

    // 引脚查找
    Pin* FindPin(const UUID& pinId);
    Pin* FindInputPin(const std::string& name);
    Pin* FindOutputPin(const std::string& name);
    const Pin* FindInputPin(const std::string& name) const;
    const Pin* FindOutputPin(const std::string& name) const;

    // 引脚数量
    int InputCount() const { return (int)inputPins.size(); }
    int OutputCount() const { return (int)outputPins.size(); }
};
```

**Node.cpp**：
```cpp
#include "MaterialGraph/Public/Node.h"

Pin* Node::FindPin(const UUID& pinId) {
    for (auto& p : inputPins) if (p.id == pinId) return &p;
    for (auto& p : outputPins) if (p.id == pinId) return &p;
    return nullptr;
}

Pin* Node::FindInputPin(const std::string& name) {
    for (auto& p : inputPins) if (p.name == name) return &p;
    return nullptr;
}

Pin* Node::FindOutputPin(const std::string& name) {
    for (auto& p : outputPins) if (p.name == name) return &p;
    return nullptr;
}

const Pin* Node::FindInputPin(const std::string& name) const {
    for (const auto& p : inputPins) if (p.name == name) return &p;
    return nullptr;
}

const Pin* Node::FindOutputPin(const std::string& name) const {
    for (const auto& p : outputPins) if (p.name == name) return &p;
    return nullptr;
}
```

**讲解**：
- Node 存储位置（`QPointF`）和引脚列表，但不存 Expression 的编译逻辑——编译逻辑在 Expression 类中（课5）
- `parameters` 是 JSON 对象，如 `{"value": 0.5}` 或 `{"x": 1.0, "y": 0.0, "z": 0.0}`
- 引脚查找通过名称，这在编译时很常用：`node->FindInputPin("BaseColor")`

**UE5 参考**：
- `E:\UE5\Engine\Source\Runtime\Engine\Classes\EdGraph\EdGraphNode.h` — `NodePosX`, `NodePosY` 等属性
- `E:\UE5\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpression.h` — `GetInput()`, `GetOutput()` 方法

### 5. Connection.h — 连接

```cpp
#pragma once
#include "Core/Public/UUID.h"

struct Connection {
    UUID id = UUID::Invalid();
    UUID sourceNodeId;    // 输出端节点
    UUID sourcePinId;     // 输出端引脚
    UUID targetNodeId;    // 输入端节点
    UUID targetPinId;     // 输入端引脚
};
```

**讲解**：
- 连接就是"哪条线的哪引脚"到"哪条线的哪引脚"
- UE5 中连接信息存在 Pin 的 `LinkedTo` 数组中，我们单独用结构体，方便管理

### 6. Graph.h / Graph.cpp — 图容器

**Graph.h**：
```cpp
#pragma once
#include "Core/Public/UUID.h"
#include "Core/Public/RefCounted.h"
#include "MaterialGraph/Public/Node.h"
#include "MaterialGraph/Public/Connection.h"
#include <QObject>
#include <map>
#include <vector>

class Graph : public QObject {
    Q_OBJECT
public:
    explicit Graph(QObject* parent = nullptr);

    // 节点管理
    // 两种 AddNode 方式：
    //   1. 传入 typeName + position：内部创建空节点（无引脚），用于课3的简易测试
    //   2. 传入 Ref<Node>：接收外部已构造好的节点（通常是 NodeFactory::Create 的产物），
    //      这种节点已经带有完整引脚，是课4以后的主流用法
    Node* AddNode(const std::string& typeName, const QPointF& position);
    Node* AddNode(const Ref<Node>& node);
    void RemoveNode(const UUID& nodeId);
    Node* FindNode(const UUID& nodeId);
    const Node* FindNode(const UUID& nodeId) const;
    const std::map<UUID, Ref<Node>>& GetNodes() const { return nodes_; }

    // 连接管理
    bool Connect(const UUID& sourcePinId, const UUID& targetPinId);
    void Disconnect(const UUID& connectionId);
    const std::map<UUID, Connection>& GetConnections() const { return connections_; }

    // 获取材质输出节点（编译起点）
    Node* GetOutputNode();
    void EnsureOutputNode();  // 确保输出节点存在

signals:
    void NodeAdded(Node* node);
    void NodeRemoved(const UUID& nodeId);
    void ConnectionAdded(const Connection& conn);
    void ConnectionRemoved(const UUID& connId);
    void GraphChanged();

private:
    std::map<UUID, Ref<Node>> nodes_;
    std::map<UUID, Connection> connections_;
    UUID outputNodeId_;  // 材质输出节点的 ID
};
```

**Graph.cpp**：
```cpp
#include "MaterialGraph/Public/Graph.h"
#include <algorithm>  // std::remove_if (Disconnect 中使用 erase-remove 惯用法)

Graph::Graph(QObject* parent) : QObject(parent) {
    EnsureOutputNode();
}

void Graph::EnsureOutputNode() {
    if (outputNodeId_.IsValid() && FindNode(outputNodeId_)) return;

    auto node = MakeRef<Node>();
    node->id = UUID::Generate();
    node->typeName = "MaterialOutput";
    node->title = "Material Output";
    node->position = QPointF(600, 300);

    // 材质输出节点只有输入引脚（各材质属性）
    auto makeInput = [&](const char* name, EValueType type, const std::string& def = "0.0") {
        Pin p;
        p.id = UUID::Generate();
        p.name = name;
        p.type = type;
        p.direction = EPinDataDirection::Input;
        p.ownerNodeId = node->id;
        p.defaultValue = def;
        return p;
    };

    node->inputPins = {
        makeInput("BaseColor",       EValueType::Float3, "(0,0,0)"),
        makeInput("Metallic",        EValueType::Float1, "0.0"),
        makeInput("Specular",        EValueType::Float1, "0.5"),
        makeInput("Roughness",       EValueType::Float1, "0.5"),
        makeInput("Normal",          EValueType::Float3, "(0,0,1)"),
        makeInput("EmissiveColor",   EValueType::Float3, "(0,0,0)"),
        makeInput("Opacity",         EValueType::Float1, "1.0"),
        makeInput("AmbientOcclusion",EValueType::Float1, "1.0"),
        makeInput("WorldPositionOffset", EValueType::Float3, "(0,0,0)"),
    };

    outputNodeId_ = node->id;
    nodes_[node->id] = node;
}

Node* Graph::AddNode(const std::string& typeName, const QPointF& position) {
    auto node = MakeRef<Node>();
    node->id = UUID::Generate();
    node->typeName = typeName;
    node->title = typeName;  // 临时用 typeName，后续从 Expression 获取 displayName
    node->position = position;
    // 注意：引脚创建在 NodeFactory 中（课4），这里先创建空节点

    nodes_[node->id] = node;
    emit NodeAdded(node.get());
    emit GraphChanged();
    return node.get();
}

Node* Graph::AddNode(const Ref<Node>& node) {
    if (!node || !node->id.IsValid()) return nullptr;
    if (nodes_.count(node->id)) return nullptr;  // 已存在，避免重复添加
    nodes_[node->id] = node;
    emit NodeAdded(node.get());
    emit GraphChanged();
    return node.get();
}

void Graph::RemoveNode(const UUID& nodeId) {
    if (nodeId == outputNodeId_) return;  // 不允许删除输出节点

    auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) return;

    // 删除与该节点相关的所有连接
    std::vector<UUID> connsToRemove;
    for (auto& [cid, conn] : connections_) {
        if (conn.sourceNodeId == nodeId || conn.targetNodeId == nodeId) {
            connsToRemove.push_back(cid);
        }
    }
    for (auto& cid : connsToRemove) {
        Disconnect(cid);
    }

    nodes_.erase(it);
    emit NodeRemoved(nodeId);
    emit GraphChanged();
}

Node* Graph::FindNode(const UUID& nodeId) {
    auto it = nodes_.find(nodeId);
    return it != nodes_.end() ? it->second.get() : nullptr;
}

const Node* Graph::FindNode(const UUID& nodeId) const {
    auto it = nodes_.find(nodeId);
    return it != nodes_.end() ? it->second.get() : nullptr;
}

Node* Graph::GetOutputNode() {
    return FindNode(outputNodeId_);
}

bool Graph::Connect(const UUID& sourcePinId, const UUID& targetPinId) {
    // 查找两个引脚分别属于哪个节点
    Pin* sourcePin = nullptr;
    Pin* targetPin = nullptr;
    Node* sourceNode = nullptr;
    Node* targetNode = nullptr;

    for (auto& [nid, node] : nodes_) {
        for (auto& pin : node->outputPins) {
            if (pin.id == sourcePinId) { sourcePin = &pin; sourceNode = node.get(); }
        }
        for (auto& pin : node->inputPins) {
            if (pin.id == targetPinId) { targetPin = &pin; targetNode = node.get(); }
        }
    }

    if (!sourcePin || !targetPin || !sourceNode || !targetNode) return false;

    // 检查连接合法性
    if (!sourcePin->CanConnectTo(*targetPin)) return false;

    // 如果输入引脚已有连接，先断开（直接从 Pin 拿 connectionId）
    if (targetPin->IsConnected()) {
        Disconnect(targetPin->connections[0].connectionId);
    }

    // 创建连接
    Connection conn;
    conn.id = UUID::Generate();
    conn.sourceNodeId = sourceNode->id;
    conn.sourcePinId = sourcePinId;
    conn.targetNodeId = targetNode->id;
    conn.targetPinId = targetPinId;
    connections_[conn.id] = conn;

    // 更新引脚的连接信息
    sourcePin->connections.push_back({conn.id, targetPinId, targetNode->id});
    targetPin->connections.push_back({conn.id, sourcePinId, sourceNode->id});

    emit ConnectionAdded(conn);
    emit GraphChanged();
    return true;
}

void Graph::Disconnect(const UUID& connectionId) {
    auto it = connections_.find(connectionId);
    if (it == connections_.end()) return;

    Connection& conn = it->second;

    // 清理引脚中的连接信息
    // 注意：PinConnection 里只存了 otherPinId（对端引脚），所以这里要从
    // 当前节点视角判断对端——源节点的对端是 targetPin，目标节点的对端是 sourcePin。
    if (auto* srcNode = FindNode(conn.sourceNodeId)) {
        if (auto* pin = srcNode->FindPin(conn.sourcePinId)) {
            pin->connections.erase(
                std::remove_if(pin->connections.begin(), pin->connections.end(),
                    [&](const Pin::PinConnection& pc) { return pc.otherPinId == conn.targetPinId; }),
                pin->connections.end());
        }
    }
    if (auto* dstNode = FindNode(conn.targetNodeId)) {
        if (auto* pin = dstNode->FindPin(conn.targetPinId)) {
            pin->connections.erase(
                std::remove_if(pin->connections.begin(), pin->connections.end(),
                    [&](const Pin::PinConnection& pc) { return pc.otherPinId == conn.sourcePinId; }),
                pin->connections.end());
        }
    }

    connections_.erase(it);
    emit ConnectionRemoved(connectionId);
    emit GraphChanged();
}
```

**讲解**：
- Graph 继承 `QObject`，使用 `Q_OBJECT` 宏，这样才能用信号槽（signals/slots）
- `EnsureOutputNode()` 在构造时自动创建材质输出节点，带9个输入引脚（BaseColor 等）
- `Connect()` 先做验证（类型兼容、方向正确），再创建连接并更新两端引脚
- 输入引脚只允许一个连接（新连接替换旧连接），输出引脚允许多个
- 所有修改操作都发射信号（`NodeAdded` 等），UI 层监听这些信号来更新显示

**UE5 参考**：
- `E:\UE5\Engine\Source\Runtime\Engine\Classes\EdGraph\EdGraph.h`
- `E:\UE5\Engine\Source\Runtime\Engine\Classes\Materials\Material.h` — 查看 FExpressionInput 列表

---

## 验证

修改 `main.cpp`：

```cpp
#include <QApplication>
#include <QWidget>
#include "MaterialGraph/Public/Graph.h"
#include "Core/Public/Logger.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    Graph graph;
    auto* output = graph.GetOutputNode();
    ME_LOG_INFO("Output node: %s (id: %s)",
        output->title.c_str(), output->id.ToString().c_str());
    ME_LOG_INFO("Output node has %d input pins", output->InputCount());

    // 添加两个测试节点
    auto* nodeA = graph.AddNode("ExprConstant3Vector", QPointF(100, 100));
    auto* nodeB = graph.AddNode("ExprAdd", QPointF(300, 100));
    ME_LOG_INFO("Added nodes: %s, %s", nodeA->id.ToString().c_str(),
        nodeB->id.ToString().c_str());

    // 测试连接（手动给测试节点加引脚来测试）
    // 注：完整的节点创建（含引脚）在课4的 NodeFactory 中实现

    QWidget window;
    window.setWindowTitle("Material Editor v0.1 - Graph OK");
    window.resize(1280, 720);
    window.show();
    return app.exec();
}
```

---

## 完成标志

- [ ] Graph 创建时自动包含输出节点
- [ ] 输出节点有 9 个输入引脚（BaseColor ~ WorldPositionOffset）
- [ ] AddNode / RemoveNode 正常工作
- [ ] Connect 验证方向和类型，非法连接返回 false
- [ ] 编译运行无错误
