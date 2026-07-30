// UploadRing.cpp
#include "UploadRing.h"
#include <cstring>

void UploadRing::Init(ID3D12Device* device, uint64_t totalCapacity)
{
    totalCapacity_ = totalCapacity;
    sliceSize_ = totalCapacity / kFrameCount;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = totalCapacity;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc = {1, 0};
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES prop{};
    prop.Type = D3D12_HEAP_TYPE_UPLOAD;

    HRESULT hr = device->CreateCommittedResource(
        &prop, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&buffer_));
    if (FAILED(hr)) { buffer_ = nullptr; return; }

    // 持久映射
    D3D12_RANGE readRange{0, 0}; // 不读
    buffer_->Map(0, &readRange, reinterpret_cast<void**>(&cpuBase_));
    gpuBase_ = buffer_->GetGPUVirtualAddress();
    for (uint32_t i = 0; i < kFrameCount; ++i) cursor_[i] = 0;
}

void UploadRing::Destroy()
{
    if (buffer_ && cpuBase_) {
        buffer_->Unmap(0, nullptr);
        cpuBase_ = nullptr;
    }
    buffer_.Reset();
    totalCapacity_ = 0;
    sliceSize_ = 0;
}

void UploadRing::BeginFrame(uint32_t frameIndex)
{
    if (frameIndex >= kFrameCount) return;
    cursor_[frameIndex] = 0; // 该帧 slice 可重写
}

UploadRing::Alloc UploadRing::Allocate(uint32_t frameIndex, uint64_t size, uint64_t align)
{
    Alloc bad{};
    if (!cpuBase_ || frameIndex >= kFrameCount || size == 0) return bad;

    uint64_t sliceStart = (uint64_t)frameIndex * sliceSize_;
    uint64_t sliceEnd   = sliceStart + sliceSize_;

    auto alignUp = [](uint64_t v, uint64_t a){ return (v + a - 1) & ~(a - 1); };
    uint64_t off = alignUp(sliceStart + cursor_[frameIndex], align);
    uint64_t newCursor = off - sliceStart + size;
    if (off + size > sliceEnd) return bad; // 本帧 slice 装不下

    cursor_[frameIndex] = newCursor;
    Alloc a;
    a.cpu = cpuBase_ + off;
    a.gpu = gpuBase_ + off;
    return a;
}
