#pragma once
#include "Core/Public/UUID.h"
#include "Core/Public/RefCounted.h"
#include "MaterialGraph/Public/Node.h"
#include "MaterialGraph/Public/Connection.h"
#include <QObject>
#include <map>
#include <vector>

class Graph : public QObject
{
    Q_OBJECT

public:
    explicit Graph(QObject *parent = nullptr);

    // 节点管理
    // 唯一的添加节点入口：按 typeName 经 NodeFactory 构造（含引脚 + 默认值）再加入图。
    // 所有节点（含 MaterialOutput）都注册在工厂里，统一走这条路径，不手动构造 Node。
    Node *AddNode(const std::string &typeName, const QPointF &position);
    void RemoveNode(const UUID &nodeId);
    Node *FindNode(const UUID &nodeId);
    const Node *FindNode(const UUID &nodeId) const;
    const std::map<UUID, Ref<Node>> &GetNodes() const { return nodes_; }

    // 连接管理
    bool Connect(const UUID &sourcePinId, const UUID &targetPinId);
    void Disconnect(const UUID &connectionId);
    const std::map<UUID, Connection> &GetConnections() const { return connections_; }

    // 获取材质输出节点(编译起点)
    Node *GetOutputNode();
    const Node *GetOutputNode() const;
    void EnsureOutputNode(); // 确保输出节点存在

signals:
    void NodeAdded(Node *node);
    void NodeRemoved(const UUID &nodeId);
    void ConnectionAdded(const Connection &conn);
    void ConnectionRemoved(const UUID &connId);
    void GraphChanged();

private:
    std::map<UUID, Ref<Node>> nodes_;
    std::map<UUID, Connection> connections_;
    UUID outputNodeId_; // 材质输出节点的ID
};