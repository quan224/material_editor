#pragma once
#include "MaterialGraph/Public/NodeFactory.h"

void NodeFactory::Register(const std::string &typeName,
                           const std::string &displayName,
                           const std::string &category,
                           const std::vector<PinTemplate> &pins)
{
    nameToIndex_[typeName] = registry_.size();
    registry_.push_back({typeName, displayName, category, pins});
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