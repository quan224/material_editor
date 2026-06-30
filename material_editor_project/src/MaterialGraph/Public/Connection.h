#pragma once
#include "Core/Public/UUID.h"

struct Connection {
    UUID id = UUID::Invalid();
    UUID sourceNodeId;    // 输出端节点
    UUID sourcePinId;     // 输出端引脚
    UUID targetNodeId;    // 输入端节点
    UUID targetPinId;     // 输入端引脚
};