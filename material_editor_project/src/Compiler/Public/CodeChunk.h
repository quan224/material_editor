#pragma once
#include "MaterialGraph/Public/Types.h"
#include <string>
#include <vector>
#include <cstdint>

struct CodeChunk{
    uint64_t hash = 0;  // 哈希，去重用(相同代码只存一份)
    std::string code;  // HLSL片段, 如“Local0+Local1”
    std::string symbol_name;  // 变量名，如"Local2"
    EValueType type = EValueType::Unknown;  // 类型(决定生成"float"/"float3")
    bool is_inline = false;  // 短表达式直接嵌入，不声明变量
    bool is_constant = false;  // 是否为编译时常量
    float constant_value = 0.0f;  // 常量值
    std::vector<int32_t> references;  // 依赖哪些其他chunk(确定声明顺序)
};