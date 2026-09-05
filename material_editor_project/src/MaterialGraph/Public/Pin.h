#pragma once
#include "MaterialTypes/Public/ValueType.h"
#include "Core/Public/UUID.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class Pin
{
public:
    UUID id = UUID::Invalid();
    std::string name;                                       // 端口名
    EMaterialValueType type = MCT_Unknown;                          // 端口类型
    EPinDataDirection direction = EPinDataDirection::Input; // 端口方向
    UUID ownerNodeId = UUID::Invalid();                     // 所在节点ID
    nlohmann::json defaultValue;                           // 未连接时的默认值（json：0.5 / [0,0,1] / [1,0,0,1]）

    // 连接信息 —— 这个引脚连到了对端的哪个引脚
    // 注意：字段名叫 otherPinId/otherNodeId（"对端引脚/对端节点"），
    // 不暗示方向。无论这个 Pin 是输入还是输出，"other" 始终指对端。
    // 比如输入引脚 A 持有的 connection 里，otherPinId 是上游输出引脚的 id；
    // 输出引脚 B 持有的 connection 里，otherPinId 是下游输入引脚的 id。
    struct PinConnection
    {
        UUID connectionId;
        UUID otherPinId;
        UUID otherNodeId;
    };
    std::vector<PinConnection> connections;

    // 检查是否可以连接到另一个引脚
    bool CanConnectTo(const Pin &other) const
    {
        // 不能自己连接自己
        if (ownerNodeId == other.ownerNodeId)
            return false;
        // 不能连接同方向
        if (direction == other.direction)
            return false;
        // 类型必须兼容
        EMaterialValueType src_type = (direction == EPinDataDirection::Output) ? type : other.type;
        EMaterialValueType dst_type = (direction == EPinDataDirection::Input) ? type : other.type;
        return CanImplicitConvert(src_type, dst_type);
    }

    bool IsConnected() const { return !connections.empty(); }

    // 输入引脚只能连一个，输出端口可以连多个
    bool IsInput() const { return direction == EPinDataDirection::Input; }
    bool IsOutput() const { return direction == EPinDataDirection::Output; }
};