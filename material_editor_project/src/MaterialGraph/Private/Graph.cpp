#include "MaterialGraph/Public/Graph.h"
#include "MaterialGraph/Public/NodeFactory.h"
#include <algorithm>  // std::remove_if（Disconnect 中使用 erase-remove 惯用法）
#include <vector>

Graph::Graph(QObject* parent):QObject(parent){
    EnsureOutputNode();
}

void Graph::EnsureOutputNode()
{
    // 不重复设置
    if (outputNodeId_.IsValid() && FindNode(outputNodeId_))
        return;

    auto node = MakeRef<Node>();
    node->id = UUID::Generate();
    node->typeName = "MaterialOutput";
    node->title = "Material Output";
    node->position = QPointF(600, 300);

    // 材质输出节点只有输入引脚
    auto makeInput = [&](const char *name, EValueType type, const std::string &def = "0.0")
    {
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
        makeInput("BaseColor", EValueType::Float3, "(0,0,0)"),
        makeInput("Metallic", EValueType::Float1, "0.0"),
        makeInput("Specular", EValueType::Float1, "0.5"),
        makeInput("Roughness", EValueType::Float1, "0.5"),
        makeInput("Normal", EValueType::Float3, "(0,0,1)"),
        makeInput("EmissiveColor", EValueType::Float3, "(0,0,0)"),
        makeInput("Opacity", EValueType::Float1, "1.0"),
        makeInput("AmbientOcclusion", EValueType::Float1, "1.0"),
        makeInput("WorldPositionOffset", EValueType::Float3, "(0,0,0)"),
    };

    outputNodeId_ = node->id;
    nodes_[node->id] = node;
}

Node *Graph::AddNode(const std::string &typeName, const QPointF &position)
{

    Ref<Node> node = NodeFactory::GetInstance().Create(typeName, position);
    if (!node) return nullptr;
    return AddNode(node);  // 复用下面的重载
}

Node *Graph::AddNode(const Ref<Node> &node)
{
    // 防御性检查：空指针 / 无效 id / 已存在都不允许
    if (!node || !node->id.IsValid()) return nullptr;
    if (nodes_.count(node->id)) return nullptr;

    nodes_[node->id] = node;
    emit NodeAdded(node.get());
    emit GraphChanged();
    return node.get();
}

void Graph::RemoveNode(const UUID &nodeId)
{

    // 不允许删除输出节点
    if (nodeId == outputNodeId_)
        return;

    // 节点不存在直接返回
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end())
        return;

    // 删除与该节点相关的所有连接
    std::vector<UUID> connsToRemove;
    for (auto &[cid, conn] : connections_)
    {
        if (conn.sourceNodeId == nodeId || conn.targetNodeId == nodeId)
        {
            connsToRemove.push_back(cid);
        }
    }

    for (auto &cid : connsToRemove)
    {
        Disconnect(cid);
    }
    nodes_.erase(it);
    emit NodeRemoved(nodeId);
    emit GraphChanged();
}

Node *Graph::FindNode(const UUID &nodeId)
{
    auto it = nodes_.find(nodeId);
    return it != nodes_.end() ? it->second.get() : nullptr;
}

const Node *Graph::FindNode(const UUID &nodeId) const
{
    auto it = nodes_.find(nodeId);
    return it != nodes_.end() ? it->second.get() : nullptr;
}

Node *Graph::GetOutputNode()
{
    return FindNode(outputNodeId_);
}

const Node *Graph::GetOutputNode() const
{
    return FindNode(outputNodeId_);
}

bool Graph::Connect(const UUID &sourcePinId, const UUID &targetPinId)
{
    // 查找两个引脚分别属于哪个节点
    Pin *sourcePin = nullptr;
    Pin *targetPin = nullptr;
    Node *sourceNode = nullptr;
    Node *targetNode = nullptr;

    for (auto &[nid, node] : nodes_)
    {
        for (auto &p : node->outputPins)
        {
            if (p.id == sourcePinId)
            {
                sourcePin = &p;
                sourceNode = node.get();
            }
        }
        for (auto &p : node->inputPins)
        {
            if (p.id == targetPinId)
            {
                targetPin = &p;
                targetNode = node.get();
            }
        }
        if (sourcePin && targetPin)
            break;
    }
    if (!sourcePin || !targetPin || !sourceNode || !targetNode)
        return false;

    if (!sourcePin->CanConnectTo(*targetPin))
        return false;

    if (targetPin->IsConnected())
    {
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

    // 更新引脚的连接信息（字段名 otherPinId/otherNodeId 是中性命名，对两端都用）
    sourcePin->connections.push_back({conn.id, targetPinId, targetNode->id});
    targetPin->connections.push_back({conn.id, sourcePinId, sourceNode->id});
    // 注意：sourcePin 的 other 是 target，targetPin 的 other 是 source

    emit ConnectionAdded(conn);
    emit GraphChanged();
    return true;
}

void Graph::Disconnect(const UUID& connectionId) {
    auto it = connections_.find(connectionId);
    if (it == connections_.end()) return;

    Connection& conn = it->second;

    // 清理引脚中的连接信息
    // 注意：PinConnection 里只存了 otherPinId（对端引脚），所以要从当前节点视角判断对端
    // ——源节点的对端是 targetPin，目标节点的对端是 sourcePin
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
