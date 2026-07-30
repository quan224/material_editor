// DescriptorAllocator.h
// CPU 可见 descriptor heap 的子分配器（staging 用）。
// 渲染器把本堆里建好的 descriptor 每帧 copy 到 GPU 可见堆去绑定。
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

using Microsoft::WRL::ComPtr;

class DescriptorAllocator {
public:
    void Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity);
    void Destroy();

    // 分配 count 个连续句柄；失败返回 {nullptr}
    D3D12_CPU_DESCRIPTOR_HANDLE Allocate(uint32_t count = 1);
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t count = 1);

    uint32_t GetHandleSize() const { return handleSize_; }
    ID3D12DescriptorHeap* GetHeap() const { return heap_.Get(); }
    D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return type_; }

private:
    ComPtr<ID3D12DescriptorHeap> heap_;
    ID3D12Device* device_ = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE type_ = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uint32_t handleSize_ = 0;
    uint32_t capacity_ = 0;

    struct FreeSeg { uint32_t index; uint32_t count; }; // 以"句柄下标"计
    std::vector<FreeSeg> freeList_; // 升序，合并
};
