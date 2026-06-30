#include "MaterialGraph/Public/Node.h"

Pin *Node::FindPin(const UUID &pinId)
{
    for (auto &p : inputPins)
        if (p.id == pinId)
            return &p;
    for (auto &p : outputPins)
        if (p.id == pinId)
            return &p;
    return nullptr;
}

Pin *Node::FindInputPin(const std::string &name)
{
    for (auto &p : inputPins)
        if (p.name == name)
            return &p;
    return nullptr;
}

Pin *Node::FindOutputPin(const std::string &name)
{
    for (auto &p : outputPins)
        if (p.name == name)
            return &p;
    return nullptr;
}

const Pin *Node::FindPin(const UUID &pinId) const
{
    for (const auto &p : inputPins)
        if (p.id == pinId)
            return &p;
    for (const auto &p : outputPins)
        if (p.id == pinId)
            return &p;
    return nullptr;
}

const Pin *Node::FindInputPin(const std::string &name) const
{
    for (const auto &p : inputPins)
        if (p.name == name)
            return &p;
    return nullptr;
}

const Pin *Node::FindOutputPin(const std::string &name) const
{
    for (const auto &p : outputPins)
        if (p.name == name)
            return &p;
    return nullptr;
}

