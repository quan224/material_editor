#pragma once
#include <cstdint>

// 注意：故意用弱类型 enum（不带 class）——对齐 UE EShaderFrequency 的用法，
// SF_Vertex 等成员裸名直接用、可做数组索引（SF_NumFrequencies 当尺寸）
// 对照 UE RHIDefinitions.h:188，值原样
enum EShaderFrequency : uint8_t
{
    SF_Vertex              = 0,
    SF_Mesh                = 1,
    SF_Amplification       = 2,
    SF_Pixel               = 3,
    SF_Geometry            = 4,
    SF_Compute             = 5,
    SF_RayGen              = 6,
    SF_RayMiss             = 7,
    SF_RayHitGroup         = 8,
    SF_RayCallable         = 9,
    SF_WorkGraphRoot       = 10,
    SF_WorkGraphComputeNode= 11,

    SF_NumFrequencies      = 12,

    // 图形管线标准频率数（不含 compute）
    SF_NumGraphicsFrequencies = 5,

    // 标准频率数（含 compute）
    SF_NumStandardFrequencies = 6,

    SF_NumBits             = 4,
};
static_assert(SF_NumFrequencies <= (1 << SF_NumBits), "SF_NumFrequencies will not fit on SF_NumBits");
