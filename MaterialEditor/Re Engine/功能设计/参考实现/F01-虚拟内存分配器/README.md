# F01 — 虚拟内存分配器（完整参考实现）

> DX12 渲染器的 GPU 显存 + descriptor + 上传基建。**完整、可直接整合的参考实现**，不省步骤。
> 设计蓝本：RE Engine「仮想メモリアロケータ導入への道のり」（是松匡亮，RE:2023）。

## 提供的文件

| 文件 | 作用 |
|---|---|
| `GpuAllocator.h/.cpp` | GPU 显存子分配器：大块 `ID3D12Heap` + 放置式资源（`CreatePlacedResource`），空闲链表 + 合并 |
| `DescriptorAllocator.h/.cpp` | CPU 可见 descriptor heap 子分配（CBV/SRV/UAV/Sampler），空闲链表，给 GPU 可见堆做 staging |
| `UploadRing.h/.cpp` | 每帧（N_FRAMES_IN_FLIGHT）的环形上传缓冲，持久映射，帧首重置 |

## 设计要点（为什么这么写）

- **放置式资源**：先建大 `ID3D12Heap`（如 64MB/块），再用 `CreatePlacedResource(heap, offset, ...)` 子分配——比每个资源 `CreateCommittedResource` 省、控碎片、可控驻留。对齐按 D3D12 要求 64KB（`D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT`）。
- **空闲链表 + 合并**：每块维护按 offset 排序的空闲区间，free 时合并相邻区间，避免碎片化。
- **descriptor 分两层**：CPU staging 堆（non-shader-visible，建描述符）+ GPU 可见堆（每帧 copy 进去绑定）。本文件实现 staging 那层；GPU 可见环见集成说明。
- **上传环按帧分片**：N_FRAMES_IN_FLIGHT 帧各占独立 slice，帧首线性 bump 重置，靠帧 fence 保证 GPU 用完才覆盖。最简单且正确。

## 集成步骤

1. `Renderer::Initialize` 里：
   - `gpuAllocator_.Init(device)` —— 之后所有 buffer/texture 走 `CreateBuffer`/`CreateTexture`（内部 `CreatePlacedResource`）。
   - `cpuDescriptors_.Init(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1<<16)`。
   - `samplerDescriptors_.Init(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048)`。
   - `uploadRing_.Init(device, 64*1024*1024 /*64MB*/)`。
2. GPU 可见 descriptor 堆（CBV_SRV_UAV，shader-visible，每帧 copy）单独建，每帧用 `device->CopyDescriptors` 把本帧用到的 staging handle 复制进去——这是渲染器职责，不在 F01 范围。
3. 每帧：`uploadRing_.BeginFrame(frameIndex)`（fence 已确认该帧 GPU 用完）→ 各种 `uploadRing_.Allocate(...)` 上传 → 提交命令 → `fence->Signal`。
4. 资源释放：`gpuAllocator_.Free(alloc)` / `cpuDescriptors_.Free(handle)`。注意 GPU 引用未完成前不能 free（用 fence 追踪释放时机，见下方"延迟释放"提示）。

## 延迟释放（重要，否则 GPU 还在用就被回收）

本实现提供"立即分配/释放"语义。生产级用法需**延迟释放队列**：`Free` 时记录 `(alloc, targetFenceValue)`，等 GPU 走过 targetFenceValue 才真正回收。建议在渲染器里包一层 `DeferredFreeQueue`（每个帧 fence 检查时 flush）。这是与 F02 Profiler 配套的工程项。

## 参考文献

- **RE Engine 自研虚拟内存分配器（是松匡亮，RE:2023）**：https://www.capcom-games.com/coc/2023/en/session/13/
- **offset-allocator（Garlic，开源，参考算法）**：https://github.com/sebbbi/OffsetAllocator （sebbbi 的 GDC 谈 GPU 内存分配）
- **Microsoft Direct3D 12 内存管理官方文档**：https://learn.microsoft.com/en-us/windows/win32/direct3d12/memory-management
- **D3D12 Heap / Placed Resource**：https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createplacedresource
- ** Descriptor Heaps**：https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps-overview
- **GDC 2015, "Parallelizing the Naughty Dog ... " 无关；正确参考：GDC 2016 "Memory Bound? Measuring & Attacking ... "（先列 offset-allocator 即可）**

## 诚实边界

- 不保证首次编译跑通——DX12 细节多，请用编译器+调试层（D3D12 debug layer）验证；报错我跟着修。
- 子分配器是 first-fit + 合并（够用、好懂）；要 RE Engine 级（offset-allocator 的 O(1) 位图法）可替换 `HeapBlock` 内部，对外接口不变。
