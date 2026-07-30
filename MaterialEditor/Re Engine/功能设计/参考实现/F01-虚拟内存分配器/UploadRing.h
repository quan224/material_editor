// UploadRing.h
// 每帧分片的环形上传缓冲（N_FRAMES_IN_FLIGHT）。
// 每帧独占一段 slice，帧首线性 bump 重置；靠帧 fence 保证 GPU 用完才覆盖。
// 持久映射（HEAP_TYPE_UPLOAD），CPU 写 → GPU 读。
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

using Microsoft::WRL::ComPtr;

class UploadRing {
public:
    static constexpr uint32_t kFrameCount = 2; // N_FRAMES_IN_FLIGHT

    struct Alloc {
        void* cpu = nullptr;                 // CPU 写地址
        D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;   // GPU VA（可直接作 CBV/SRV 用）
        bool IsValid() const { return cpu != nullptr; }
    };

    void Init(ID3D12Device* device, uint64_t totalCapacity);
    void Destroy();

    // 帧首调用（确认该帧 GPU 已完成）：重置该帧 bump 游标
    void BeginFrame(uint32_t frameIndex);

    // 在当前帧 slice 内分配 size（对齐 align），返回 cpu/gpu 地址；不够返回 {nullptr}
    Alloc Allocate(uint32_t frameIndex, uint64_t size, uint64_t align = 256);

private:
    ComPtr<ID3D12Resource> buffer_;
    uint8_t* cpuBase_ = nullptr;
    uint64_t gpuBase_ = 0;
    uint64_t totalCapacity_ = 0;
    uint64_t sliceSize_ = 0;
    uint64_t cursor_[kFrameCount] = {}; // 每帧的 bump 游标
};
