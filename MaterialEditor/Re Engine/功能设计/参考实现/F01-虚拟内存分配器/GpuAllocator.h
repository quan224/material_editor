// GpuAllocator.h
// GPU 显存子分配器：大块 ID3D12Heap + 放置式资源（CreatePlacedResource）。
// 设计参考：RE Engine「仮想メモリアロケータ」(RE:2023) + offset-allocator 思路。
#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

using Microsoft::WRL::ComPtr;

// D3D12 放置对齐：缓冲/普通纹理 64KB
constexpr uint64_t kPlacementAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; // 65536

// 一次分配的结果
struct GpuAllocation {
    ID3D12Heap* heap = nullptr;   // 所属大堆
    uint64_t    offset = 0;       // 堆内偏移
    uint64_t    size = 0;
    bool IsValid() const { return heap != nullptr; }
};

// 单个大堆内部的子分配器（空闲链表 + 合并，按 offset 排序）
class HeapBlock {
public:
    void Init(ID3D12Device* device, uint64_t size,
              D3D12_HEAP_TYPE type, D3D12_HEAP_FLAGS flags);
    void Destroy();

    // 在本块内分配 size（对齐到 kPlacementAlignment）。成功返回 true 并写 outOffset。
    bool Allocate(uint64_t size, uint64_t alignment, uint64_t& outOffset);
    // 释放 [offset, offset+size)
    void Free(uint64_t offset, uint64_t size);

    ID3D12Heap* GetHeap() const { return heap_.Get(); }
    uint64_t GetCapacity() const { return capacity_; }

private:
    ComPtr<ID3D12Heap> heap_;
    uint64_t capacity_ = 0;

    struct FreeRange { uint64_t offset; uint64_t size; };
    std::vector<FreeRange> freeList_; // 始终按 offset 升序，便于合并
};

// 顶层分配器：管理多个 HeapBlock，按需扩展
class GpuAllocator {
public:
    void Init(ID3D12Device* device,
              uint64_t blockSize = 64ull * 1024 * 1024, // 默认每块 64MB
              D3D12_HEAP_TYPE type = D3D12_HEAP_TYPE_DEFAULT,
              D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES);
    void Destroy();

    // —— 便捷工厂：分配并创建放置式资源 ——
    // 创建 Buffer（顶点/索引/结构化/上传等都走它，调用方在 desc 里指定 MiscFlags 等）
    bool CreateBuffer(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState,
                      ID3D12Resource** outResource, GpuAllocation* outAlloc);
    // 创建 Texture
    bool CreateTexture(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState,
                       ID3D12Resource** outResource, GpuAllocation* outAlloc,
                       const D3D12_CLEAR_VALUE* optimizedClear = nullptr);

    // —— 低级接口：仅分配一段堆内区间（调用方自己 CreatePlacedResource）——
    bool Allocate(uint64_t size, uint64_t alignment, GpuAllocation* outAlloc);
    void Free(const GpuAllocation& alloc);

private:
    ID3D12Device* device_ = nullptr;
    uint64_t blockSize_ = 0;
    D3D12_HEAP_TYPE type_ = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_FLAGS flags_ = D3D12_HEAP_FLAG_NONE;
    std::vector<HeapBlock> blocks_;
};
