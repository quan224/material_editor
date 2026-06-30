#include "MaterialGraph/Public/GraphCompiler.h"
#include "Core/Public/Logger.h"
#include <functional>  // std::function（HasCycles 中使用）

GraphCompiler::GraphCompiler(Graph* graph):graph_(graph){};

std::vector<Node *> GraphCompiler::TopologicalSort(bool *hasCycle)
{
    std::vector<Node *> result;
    std::set<UUID> visited;
    std::set<UUID> instack;
    bool cycle = false;
    Node *out_put_node = graph_->GetOutputNode();
    if (!out_put_node)
    {
        ME_LOG_ERROR("No Output Node Found!");
        if (hasCycle) *hasCycle = false;
        return result;
    }

    for (const auto &pin : out_put_node->inputPins)
    {
        if (pin.IsConnected())
        {
            for (const auto &connection : pin.connections)
            {
                // 注意：PinConnection 字段是 otherNodeId（对端节点），
                // 对于输入引脚，"对端" 就是上游输出节点
                Node *up_stream = graph_->FindNode(connection.otherNodeId);
                if (!up_stream) continue;
                Visit(up_stream, visited, instack, result, cycle);
            }
        }
    }
    if (hasCycle) *hasCycle = cycle;
    return result;
}

void GraphCompiler::Visit(Node *node,
                          std::set<UUID> &visited,
                          std::set<UUID> &inStack,
                          std::vector<Node *> &result,
                          bool &hasCycle)
{

    if (visited.count(node->id))
        return;

    if (inStack.count(node->id))
    {
        ME_LOG_ERROR("Cycle detected at node: %s", node->title.c_str());
        hasCycle = true;  // 标记检测到环，但继续处理其他分支
        return;           // 当前节点跳过（避免无限递归）
    }
    inStack.insert(node->id);

    for (const auto &pin : node->inputPins)
    {
        if (pin.IsConnected())
        {
            for (const auto &connection : pin.connections)
            {
                Node *up_stream = graph_->FindNode(connection.otherNodeId);
                if (!up_stream)
                    continue;
                Visit(up_stream, visited, inStack, result, hasCycle);
            }
        }
    }
    visited.insert(node->id);
    inStack.erase(node->id);
    result.push_back(node);
}

bool GraphCompiler::HasCycles() const
{
    std::set<UUID> visited;
    std::set<UUID> inStack;

    Node *output = graph_->GetOutputNode();
    if (!output)
        return false;

    // 简化版：用 const_cast 复用 Visit 逻辑，或单独写检查
    // 这里用一个独立的递归检查
    std::function<bool(const Node *)> checkCycle = [&](const Node *node) -> bool
    {
        if (inStack.count(node->id))
            return true; // 检测到环
        if (visited.count(node->id))
            return false;

        inStack.insert(node->id);
        for (const auto &pin : node->inputPins)
        {
            for (const auto &conn : pin.connections)
            {
                const Node *upstream = graph_->FindNode(conn.otherNodeId);
                if (upstream && checkCycle(upstream))
                    return true;
            }
        }
        inStack.erase(node->id);
        visited.insert(node->id);
        return false;
    };

    return checkCycle(output);
}
