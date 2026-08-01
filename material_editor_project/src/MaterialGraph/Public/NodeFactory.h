#pragma once
#include "MaterialGraph/Public/Node.h"
#include "Core/Public/RefCounted.h"
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "Core/Public/Singleton.h"

class NodeFactory:public Singleton<NodeFactory>
{

friend Singleton<NodeFactory>;

public:
    // 引脚描述
    struct PinTemplate
    {
        std::string name;
        EValueType type;
        EPinDataDirection direction;
        nlohmann::json defaultValue;
    };

    // 节点类型信息
    struct NodeTypeInfo
    {
        std::string typeName;    // ExprAdd
        std::string displayName; // Add
        std::string category;    // Math
        std::vector<PinTemplate> pins;
        bool hidden = false;   // hidden=true 的类型不进调色板（如 MaterialOutput，自动创建、每图唯一）
    };

    // 注册一个表达式类型
    void Register(const std::string &typeName,
                  const std::string &displayName,
                  const std::string &category,
                  const std::vector<PinTemplate> &pins,
                  bool hidden = false);

    // 创建节点
    Ref<Node> Create(const std::string &typeName, const QPointF &position) const;
    // 获取所有已注册的类型
    const std::vector<NodeTypeInfo> &GetAllTypes() const { return registry_; }
    // 按类别获取
    std::vector<const NodeTypeInfo *> GetTypesByCategory(const std::string &category) const;
    // 查找类型信息
    const NodeTypeInfo *FindType(const std::string &typeName) const;

private:
    NodeFactory();              // 构造时 RegisterBuiltins() 注册内置类型
    void RegisterBuiltins();    // 注册 MaterialOutput 等（hidden，自动创建、不进调色板）

    // 直接索引找vector下表，要比找map更快。且内存连续
    std::vector<NodeTypeInfo> registry_;
    std::map<std::string, size_t> nameToIndex_;
};