#pragma once
#include "Core/Public/UUID.h"
#include "MaterialGraph/Public/Pin.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <QPointF>

class Node
{
public:
    UUID id = UUID::Invalid(); // id
    std::string title;         // 显示名
    std::string typeName;      // 类型标识
    QPointF position;          // 在图中的位置

    std::vector<Pin> inputPins;
    std::vector<Pin> outputPins;

    // 表达式参数
    nlohmann::json parameters;

    // 引脚查找
    Pin *FindPin(const UUID &pinId);
    Pin *FindInputPin(const std::string &name);
    Pin *FindOutputPin(const std::string &name);
    const Pin *FindPin(const UUID &pinId) const;
    const Pin *FindInputPin(const std::string &name) const;
    const Pin *FindOutputPin(const std::string &name) const;

    // 引脚数量
    int InputCount() const { return (int)inputPins.size(); }
    int OutputCount() const { return (int)outputPins.size(); }
};
