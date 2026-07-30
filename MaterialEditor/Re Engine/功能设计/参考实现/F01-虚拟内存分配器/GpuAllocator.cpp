// GpuAllocator.cpp
#include "GpuAllocator.h"
#include <algorithm>
#include <cstring>

// ===================== HeapBlock =====================

void HeapBlock::Init(ID3D12Device* device, uint64_t size,
                     D3D12_HEAP_TYPE type, D3D12_HEAP_FLAGS flags)
{
    capacity_ = size;
    D3D12_HEAP_DESC desc{};
    desc.SizeInBytes        = size;
    desc.Properties.Type    = type;
    desc.Alignment          = kPlacementAlignment;
    desc.Flags              = flags;
    // NOTE: 不同 heap type 不能混；本块只服务创建时指定的 type/flags
    HRESULT hr = device->CreateHeap(&desc, IID_PPV_ARGS(&heap_));
    if (FAILED(hr)) { heap_ = nullptr; capacity_ = 0; return; }

    // 整块初始就是一个空闲区间
    freeList_.clear();
    freeList_.push_back({0, size});
}

void HeapBlock::Destroy()
{
    heap_.Reset();
    freeList_.clear();
    capacity_ = 0;
}

bool HeapBlock::Allocate(uint64_t size, uint64_t alignment, uint64_t& outOffset)
{
    if (!heap_ || size == 0) return false;
    // 对齐向上取整（对齐默认 64KB）
    auto alignUp = [](uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); };

    // first-fit：找第一个能放下、且对齐后仍在区间内的空闲块
    for (size_t i = 0; i < freeList_.size(); ++i) {
        FreeRange& r = freeList_[i];
        uint64_t aligned = alignUp(r.offset, alignment);
        uint64_t end     = r.offset + r.size;
        if (aligned >= end) continue;
        uint64_t needEnd = aligned + size;
        if (needEnd > end) continue;

        // 命中：切出 [aligned, aligned+size)
        uint64_t leftLen  = aligned - r.offset;     // 对齐产生的左残量
        uint64_t rightLen = end - needEnd;          // 右残量
        r.offset = needEnd;
        r.size   = rightLen;                        // 收缩本区间为右残量
        // 左残量作为一个新空闲区间插入（>0 才插）
        if (leftLen > 0)
            freeList_.insert(freeList_.begin() + i, FreeRange{ aligned - leftLen, leftLen });
        // 删掉空区间（右侧收缩为 0 的情况）
        if (r.size == 0)
            freeList_.erase(freeList_.begin() + i + (leftLen > 0 ? 1 : 0) - (leftLen>0?0:0));
        // ↑ 上面这行有点绕；为清晰起见下面统一做一次清理
        // 清理 size==0 的区间
        freeList_.erase(std::remove_if(freeList_.begin(), freeList_.end(),
                        [](const FreeRange& f){ return f.size == 0; }), freeList_.end());
        outOffset = aligned;
        return true;
    }
    return false;
}

void HeapBlock::Free(uint64_t offset, uint64_t size)
{
    if (!heap_ || size == 0) return;
    // 插入并合并相邻区间（list 保持按 offset 升序）
    FreeRange newRange{ offset, size };
    auto it = std::lower_bound(freeList_.begin(), freeList_.end(), newRange,
        [](const FreeRange& a, const FreeRange& b){ return a.offset < b.offset; });
    it = freeList_.insert(it, newRange);

    // 与后一个合并
    if (it + 1 != freeList_.end() && it->offset + it->size == (it+1)->offset) {
        it->size += (it+1)->size;
        freeList_.erase(it + 1);
    }
    // 与前一个合并
    if (it != freeList_.begin()) {
        auto prev = it - 1;
        if (prev->offset + prev->size == it->offset) {
            prev->size += it->size;
            freeList_.erase(it);
        }
    }
}

// ===================== GpuAllocator =====================

void GpuAllocator::Init(ID3D12Device* device, uint64_t blockSize,
                        D3D12_HEAP_TYPE type, D3D12_HEAP_FLAGS flags)
{
    device_ = device;
    blockSize_ = blockSize;
    type_ = type;
    flags_ = flags;
    // 首块直接建好
    blocks_.emplace_back();
    blocks_.back().Init(device, blockSize_, type_, flags_);
}

void GpuAllocator::Destroy()
{
    for (auto& b : blocks_) b.Destroy();
    blocks_.clear();
    device_ = nullptr;
}

bool GpuAllocator::Allocate(uint64_t size, uint64_t alignment, GpuAllocation* outAlloc)
{
    if (!device_ || size == 0) return false;
    // 先试现有块
    for (auto& b : blocks_) {
        uint64_t off = 0;
        if (b.Allocate(size, alignment, off)) {
            outAlloc->heap = b.GetHeap();
            outAlloc->offset = off;
            outAlloc->size = size;
            return true;
        }
    }
    // 不够：块不够大就放大到 size（对齐到 blockSize_ 上取整）
    uint64_t newSize = blockSize_;
    if (size > newSize) newSize = ((size + blockSize_ - 1) / blockSize_) * blockSize_;
    blocks_.emplace_back();
    blocks_.back().Init(device_, newSize, type_, flags_);
    uint64_t off = 0;
    bool ok = blocks_.back().Allocate(size, alignment, off);
    if (ok) {
        outAlloc->heap = blocks_.back().GetHeap();
        outAlloc->offset = off;
        outAlloc->size = size;
    }
    return ok;
}

void GpuAllocator::Free(const GpuAllocation& alloc)
{
    if (!alloc.IsValid()) return;
    for (auto& b : blocks_) {
        if (b.GetHeap() == alloc.heap) {
            b.Free(alloc.offset, alloc.size);
            return;
        }
    }
    // 找不到：可能是重复释放，忽略
}

bool GpuAllocator::CreateBuffer(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState,
                                ID3D12Resource** outResource, GpuAllocation* outAlloc)
{
    uint64_t required = desc.Width; // buffer 的占用 = Width（D3D12 对 buffer 用 Width 表示字节数）
    required = (required + kPlacementAlignment - 1) & ~(kPlacementAlignment - 1);
    GpuAllocation a;
    if (!Allocate(required, kPlacementAlignment, &a)) return false;
    HRESULT hr = device_->CreatePlacedResource(
        a.heap, a.offset, &desc, initialState, nullptr,
        IID_PPV_ARGS(outResource));
    if (FAILED(hr)) { Free(a); return false; }
    if (outAlloc) *outAlloc = a;
    return true;
}

bool GpuAllocator::CreateTexture(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState,
                                 ID3D12Resource** outResource, GpuAllocation* outAlloc,
                                 const D3D12_CLEAR_VALUE* optimizedClear)
{
    // 纹理所需大小由 GetResourceAllocationInfo 决定（含对齐/padding），不能用 Width×Height×...
    D3D12_RESOURCE_ALLOCATION_INFO info =
        device_->GetResourceAllocationInfo(0, 1, &desc);
    GpuAllocation a;
    if (!Allocate(info.SizeInBytes, info.Alignment, &a)) return false;
    HRESULT hr = device_->CreatePlacedResource(
        a.heap, a.offset, &desc, initialState, optimizedClear,
        IID_PPV_ARGS(outResource));
    if (FAILED(hr)) { Free(a); return false; }
    if (outAlloc) *outAlloc = a;
    return true;
}
