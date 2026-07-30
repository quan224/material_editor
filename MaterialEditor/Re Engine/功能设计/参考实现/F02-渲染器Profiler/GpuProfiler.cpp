// GpuProfiler.cpp
#include "GpuProfiler.h"

void GpuProfiler::Init(ID3D12Device* device)
{
    device_ = device;

    // 1) Query heap：每个 timestamp 用 EndQuery 记录，不需要 BeginQuery
    D3D12_QUERY_HEAP_DESC qh{};
    qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qh.Count = kMaxPasses * 2 * kFrames; // 每帧 kMaxPasses*2 条；三帧
    qh.NodeMask = 0;
    device->CreateQueryHeap(&qh, IID_PPV_ARGS(&queryHeap_));

    // 2) 每帧一份 readback buffer（解析结果）
    uint64_t bytes = (uint64_t)(kMaxPasses * 2) * sizeof(uint64_t);
    D3D12_RESOURCE_DESC bdesc{};
    bdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bdesc.Width = bytes; bdesc.Height = 1; bdesc.DepthOrArraySize = 1;
    bdesc.MipLevels = 1; bdesc.SampleDesc = {1,0};
    bdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES prop{}; prop.Type = D3D12_HEAP_TYPE_READBACK;
    for (uint32_t f = 0; f < kFrames; ++f) {
        device->CreateCommittedResource(
            &prop, D3D12_HEAP_FLAG_NONE, &bdesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&resolve_[f]));
        D3D12_RANGE none{0,0};
        resolve_[f]->Map(0, &none, &mapped_[f]);
    }

    // 3) GPU tick 频率（不同 GPU 不同，必须查）
    uint64_t f = 1;
    device->GetTimestampFrequency(&f);
    freq_ = f ? f : 1;
}

void GpuProfiler::Destroy()
{
    for (uint32_t f = 0; f < kFrames; ++f) {
        if (resolve_[f] && mapped_[f]) {
            resolve_[f]->Unmap(0, nullptr);
            mapped_[f] = nullptr;
        }
        resolve_[f].Reset();
    }
    queryHeap_.Reset();
    device_ = nullptr;
}

void GpuProfiler::BeginFrame(uint32_t frameIndex)
{
    writingFrame_ = frameIndex % kFrames;
    FrameMeta& m = frameMeta_[writingFrame_];
    m.passes.clear();
    m.passCount = 0;
    drawCalls_ = 0;
    triangles_ = 0;
    m.cpuStart = std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

uint32_t GpuProfiler::BeginPass(ID3D12GraphicsCommandList* cmd, const char* name)
{
    // NOTE: 调用方需保证 BeginFrame(frameIndex) 已记录当前帧；这里用 frameMeta_ 的"最新在用帧"
    // 为简单起见，要求外部同一帧内连续调用 Begin/End，并用返回的 passId 配对。
    // 当前帧 frameIndex 由调用方在 EndFrame 时传入；这里假设 passMeta 写入 frameMeta_[currentWriting_]。
    // —— 这里把 pass 写进 frameMeta_[writingFrame_]；writingFrame_ 由 BeginFrame 设。
    // （为避免函数签名膨胀，writingFrame_ 用一个成员，BeginFrame 设它）
    FrameMeta& m = frameMeta_[writingFrame_];
    uint32_t passId = m.passCount++;
    uint32_t localBegin = passId * 2;
    m.passes.push_back({ name ? name : "", localBegin, 0 });
    uint32_t base = writingFrame_ * kMaxPasses * 2;
    // TIMESTAMP 只用 EndQuery 记录当前时刻
    cmd->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base + localBegin);
    return passId;
}

void GpuProfiler::EndPass(ID3D12GraphicsCommandList* cmd, uint32_t passId)
{
    FrameMeta& m = frameMeta_[writingFrame_];
    if (passId >= m.passes.size()) return;
    uint32_t localEnd = m.passes[passId].localBegin + 1;
    uint32_t base = writingFrame_ * kMaxPasses * 2;
    cmd->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base + localEnd);
}

void GpuProfiler::EndFrame(ID3D12GraphicsCommandList* cmd, uint32_t frameIndex)
{
    FrameMeta& m = frameMeta_[frameIndex % kFrames];
    m.cpuEnd = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    m.drawCalls = drawCalls_;
    m.triangles = triangles_;

    // 解析本帧的 query 到 resolve_[frameIndex]（GPU 侧命令，本帧提交后某时完成）
    uint32_t base = (frameIndex % kFrames) * kMaxPasses * 2;
    uint32_t count = m.passCount * 2;
    if (count > 0) {
        cmd->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                              base, count, resolve_[frameIndex % kFrames].Get(), 0);
    }

    totalFrames_++;

    // 三帧前的帧 GPU 必已完成（前提：渲染器在 BeginFrame 时等了该帧 fence）
    if (totalFrames_ >= kFrames) {
        uint32_t readable = (frameIndex + 1) % kFrames; // == (frameIndex - (kFrames-1)) mod kFrames
        FrameMeta& rm = frameMeta_[readable];
        const uint64_t* data = reinterpret_cast<const uint64_t*>(mapped_[readable]);

        latest_.passes.clear();
        double frameBegin = 0, frameEnd = 0;
        for (size_t i = 0; i < rm.passes.size(); ++i) {
            uint64_t b = data[rm.passes[i].localBegin];
            uint64_t e = data[rm.passes[i].localBegin + 1];
            double ms = (double)(e - b) / (double)freq_ * 1000.0;
            latest_.passes.push_back({ rm.passes[i].name, (float)ms });
            if (i == 0) frameBegin = (double)b;
            frameEnd = (double)e;
        }
        latest_.frameGpuMs = (float)((frameEnd - frameBegin) / (double)freq_ * 1000.0);
        latest_.frameCpuMs = (float)((double)(rm.cpuEnd - rm.cpuStart) / 1e6);
        latest_.drawCalls = rm.drawCalls;
        latest_.triangles = rm.triangles;
    }
}
