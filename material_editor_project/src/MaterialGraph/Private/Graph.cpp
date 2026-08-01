#include "MaterialGraph/Public/Graph.h"
#include "MaterialGraph/Public/NodeFactory.h"
#include <nlohmann/json.hpp>
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

    // MaterialOutput 注册在 NodeFactory（hidden=true），统一走 AddNode 构造，不再手搓
    Node* n = AddNode("MaterialOutput", QPointF(600, 300));
    if (n) outputNodeId_ = n->id;
}

Node *Graph::AddNode(const std::string &typeName, const QPointF &position)
{
    // 唯一入口：按 typeName 经 NodeFactory 构造（含引脚 + 默认值），再加入图。
    Ref<Node> node = NodeFactory::GetInstance().Create(typeName, position);
    if (!node) return nullptr;

    // 防御性检查：Create 保证 id 有效，但重复 id 不允许
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
