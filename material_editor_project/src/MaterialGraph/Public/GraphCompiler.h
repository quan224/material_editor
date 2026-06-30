#pragma once
#include "MaterialGraph/Public/Graph.h"
#include <vector>
#include <set>

class GraphCompiler
{
public:
    explicit GraphCompiler(Graph *graph);

    // 从输出节点开始反向拓扑排序
    // 返回按编译顺序排列的节点列表（被依赖的在前）
    // 出参 hasCycle：是否检测到循环（循环的节点会被跳过，不出现在结果中）
    // 调用方应该检查 hasCycle，true 时拒绝编译而不是生成残缺的 HLSL
    std::vector<Node *> TopologicalSort(bool *hasCycle = nullptr);

    // 检测是否存在循环引用
    bool HasCycles() const;

private:
    void Visit(Node *node, std::set<UUID> &visited, std::set<UUID> &inStack, std::vector<Node *> &result, bool &hasCycle);

    Graph *graph_;
};