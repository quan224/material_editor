#pragma once
#include "MaterialGraph/Public/NodeFactory.h"

void NodeFactory::Register(const std::string &typeName,
                           const std::string &displayName,
                           const std::string &category,
                           const std::vector<PinTemplate> &pins,
                           bool hidden)
{
    nameToIndex_[typeName] = registry_.size();
    registry_.push_back({typeName, displayName, category, pins, hidden});
}

// 内置类型注册：MaterialOutput（每图唯一，由 Graph::EnsureOutputNode 自动创建，hidden 不进调色板）
void NodeFactory::RegisterBuiltins()
{
    using Dir = EPinDataDirection;
    Register("MaterialOutput", "Material Output", "Output",
        {
            {"BaseColor",           MCT_Float3, Dir::Input, nlohmann::json::array({0.0, 0.0, 0.0})},
            {"Metallic",            MCT_Float,  Dir::Input, 0.0},
            {"Specular",            MCT_Float,  Dir::Input, 0.5},
            {"Roughness",           MCT_Float,  Dir::Input, 0.5},
            {"Normal",              MCT_Float3, Dir::Input, nlohmann::json::array({0.0, 0.0, 1.0})},
            {"EmissiveColor",       MCT_Float3, Dir::Input, nlohmann::json::array({0.0, 0.0, 0.0})},
            {"Opacity",             MCT_Float,  Dir::Input, 1.0},
            {"AmbientOcclusion",    MCT_Float,  Dir::Input, 1.0},
            {"WorldPositionOffset", MCT_Float3, Dir::Input, nlohmann::json::array({0.0, 0.0, 0.0})},
        },
        /*hidden=*/true);
}

NodeFactory::NodeFactory()
{
    RegisterBuiltins();
}

Ref<Node> NodeFactory::Create(const std::string &typeName, const QPointF &position) const
{
    auto it = nameToIndex_.find(typeName);
    if (it == nameToIndex_.end())
        return nullptr;

    const NodeFactory::NodeTypeInfo &node_info = registry_[it->second];

    Ref<Node> node = MakeRef<Node>();
    node->id = UUID::Generate();
    node->title = node_info.displayName;
    node->typeName = node_info.typeName;
    node->position = position;

    for (const auto &tml_p : node_info.pins)
    {
        Pin p;
        p.id = UUID::Generate();
        p.name = tml_p.name;
        p.type = tml_p.type;
        p.defaultValue = tml_p.defaultValue;
        p.direction = tml_p.direction;
        p.ownerNodeId = node->id;

        if (p.direction == EPinDataDirection::Input)
        {
            node->inputPins.push_back(p);
        }
        else
        {
            node->outputPins.push_back(p);
        }
    }
    return node;
}

std::vector<const NodeFactory::NodeTypeInfo *> NodeFactory::GetTypesByCategory(const std::string &category) const
{
    std::vector<const NodeFactory::NodeTypeInfo *> node_types;
    for (const auto &tml_node : registry_)
    {
        if (tml_node.category == category)
            node_types.push_back(&tml_node);
    }
    return node_types;
}

const NodeFactory::NodeTypeInfo* NodeFactory::FindType(const std::string& typeName) const {
    auto it = nameToIndex_.find(typeName);
    if (it == nameToIndex_.end()) return nullptr;
    return &registry_[it->second];
}