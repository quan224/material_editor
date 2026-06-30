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
    // 两种 AddNode 方式：
    //   1. 传入 typeName + position：内部通过 NodeFactory::Create 构造带引脚的节点
    //   2. 传入 Ref<Node>：接收外部已构造好的节点（用于测试 / 反射版 Expression
    //      创建的节点 / EnsureOutputNode 这种不走工厂的特例），不依赖工厂单例
    Node *AddNode(const std::string &typeName, const QPointF &position);
    Node *AddNode(const Ref<Node> &node);
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