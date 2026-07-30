// DescriptorAllocator.cpp
#include "DescriptorAllocator.h"
#include <algorithm>

void DescriptorAllocator::Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity)
{
    device_ = device;
    type_ = type;
    capacity_ = capacity;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    desc.NumDescriptors = capacity;
    // CPU 可见 staging：采样器堆必须 NON_SHADER_VISIBLE，其它也用 NON_SHADER_VISIBLE 做 staging
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_));
    if (FAILED(hr)) { heap_ = nullptr; return; }

    handleSize_ = (uint32_t)device->GetDescriptorHandleIncrementSize(type);
    freeList_.clear();
    freeList_.push_back({0, capacity});
}

void DescriptorAllocator::Destroy()
{
    heap_.Reset();
    freeList_.clear();
    capacity_ = 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::Allocate(uint32_t count)
{
    D3D12_CPU_DESCRIPTOR_HANDLE bad{}; // ptr=nullptr
    if (!heap_ || count == 0) return bad;

    // first-fit
    for (size_t i = 0; i < freeList_.size(); ++i) {
        FreeSeg& s = freeList_[i];
        if (s.count >= count) {
            uint32_t idx = s.index;
            s.index += count;
            s.count -= count;
            if (s.count == 0) freeList_.erase(freeList_.begin() + i);

            D3D12_CPU_DESCRIPTOR_HANDLE h = heap_->GetCPUDescriptorHandleForHeapStart();
            h.ptr += (size_t)idx * handleSize_;
            return h;
        }
    }
    return bad; // 不够
}

void DescriptorAllocator::Free(D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t count)
{
    if (!heap_ || handle.ptr == 0 || count == 0) return;
    D3D12_CPU_DESCRIPTOR_HANDLE start = heap_->GetCPUDescriptorHandleForHeapStart();
    uint32_t idx = (uint32_t)((handle.ptr - start.ptr) / handleSize_);

    FreeSeg seg{idx, count};
    auto it = std::lower_bound(freeList_.begin(), freeList_.end(), seg,
        [](const FreeSeg& a, const FreeSeg& b){ return a.index < b.index; });
    it = freeList_.insert(it, seg);
    // 与后合并
    if (it + 1 != freeList_.end() && it->index + it->count == (it+1)->index) {
        it->count += (it+1)->count;
        freeList_.erase(it + 1);
    }
    // 与前合并
    if (it != freeList_.begin()) {
        auto prev = it - 1;
        if (prev->index + prev->count == it->index) {
            prev->count += it->count;
            freeList_.erase(it);
        }
    }
}
