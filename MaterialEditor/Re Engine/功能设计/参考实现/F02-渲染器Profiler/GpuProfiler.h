// GpuProfiler.h
// DX12 GPU timestamp 计时 + CPU/draw/tri 统计。三缓冲解析，N 帧延迟可读。
// 设计参考：RE Engine 自動計測レポート (CEDEC2018) + REProfiler。
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdint>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

struct FrameStats {
    float   frameCpuMs = 0.f;   // CPU 一帧耗时
    float   frameGpuMs = 0.f;   // GPU 一帧耗时（首 pass begin 到 末 pass end）
    struct Pass { std::string name; float gpuMs = 0.f; };
    std::vector<Pass> passes;   // 本帧各 pass
    uint64_t drawCalls = 0;
    uint64_t triangles = 0;
    uint64_t gpuMemUsed = 0;    // 可选，由 F01 GpuAllocator 填
};

class GpuProfiler {
public:
    static constexpr uint32_t kMaxPasses = 64;      // 每帧最多 pass 数
    static constexpr uint32_t kFrames    = 3;       // 三缓冲（GPU query 延迟）

    void Init(ID3D12Device* device);
    void Destroy();

    // —— 每帧 ——
    void BeginFrame(uint32_t frameIndex);                      // CPU 计时开始 + 计数清零
    uint32_t BeginPass(ID3D12GraphicsCommandList* cmd, const char* name); // 发 begin timestamp，返回 pass id
    void EndPass(ID3D12GraphicsCommandList* cmd, uint32_t passId);        // 发 end timestamp
    void OnDraw(uint64_t triCount) { drawCalls_++; triangles_ += triCount; }
    void EndFrame(ID3D12GraphicsCommandList* cmd, uint32_t frameIndex);   // ResolveQueryData + 结束 CPU 计时

    // 取最新可读帧的统计（frameIndex - (kFrames-1)）。若还没有可读帧返回空。
    const FrameStats& GetStats() const { return latest_; }

private:
    ID3D12Device* device_ = nullptr;
    ComPtr<ID3D12QueryHeap> queryHeap_;
    ComPtr<ID3D12Resource> resolve_[kFrames];       // 每帧一份 readback buffer
    void* mapped_[kFrames] = {};                    // 持久映射

    uint64_t freq_ = 1;                             // GPU timestamp 频率（Hz）

    // 每帧元数据
    struct PassMeta { std::string name; uint32_t beginIdx; uint32_t endIdx; };
    struct FrameMeta {
        std::vector<PassMeta> passes;
        uint32_t passCount = 0;
        uint64_t cpuStart = 0;        // chrono ns
        uint64_t cpuEnd = 0;
        uint64_t drawCalls = 0;
        uint64_t triangles = 0;
    };
    FrameMeta frameMeta_[kFrames] = {};

    uint32_t writingFrame_ = 0;   // BeginPass/EndPass 当前写入的帧槽
    uint32_t totalFrames_  = 0;   // 累计帧数（够 kFrames 才能读）

    uint64_t drawCalls_ = 0;
    uint64_t triangles_ = 0;

    FrameStats latest_;                              // 最近一次解析出的统计（供 UI 读）
};
