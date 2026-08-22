# 课14：DirectX 12 基础 — 设备、命令队列与渲染目标

## 目标

理解 DX12 的核心概念，从零搭建最小可运行的 DX12 渲染框架：创建设备、命令队列、交换链，清屏并呈现。

完成本课后，你将拥有一个蓝色窗口——它虽然简单，但包含了一个 DX12 渲染器的完整骨架。后续所有渲染功能（画三角形、加载纹理、PBR 材质）都会在这个骨架上扩展。

---

## 背景知识

### 1. DX12 vs OpenGL 的区别

如果你之前用过 OpenGL，那你习惯了这样的代码：

```cpp
// OpenGL — 简洁，但驱动在背后做了很多事
glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
glClear(GL_COLOR_BUFFER_BIT);
glDrawArrays(GL_TRIANGLES, 0, 36);
```

在 OpenGL 中，你只需告诉驱动"做什么"，驱动会自动帮你管理内存、同步、状态切换。这就像开**自动挡汽车**——踩油门就走，变速箱自动换挡。

DX12 完全不同：

```cpp
// DX12 — 你需要自己管理一切
commandAllocator->Reset();
commandList->Reset(commandAllocator.Get(), nullptr);
commandList->ResourceBarrier(1, &barrier);              // 手动切换资源状态
commandList->OMSetRenderTargets(1, &rtvHandle, ...);    // 手动设置渲染目标
commandList->ClearRenderTargetView(rtvHandle, color, 0, nullptr);
commandList->Close();
commandQueue->ExecuteCommandLists(1, ...);               // 手动提交命令
swapChain->Present(1, 0);                               // 手动翻转
```

DX12 是**显式 API**（Explicit API），几乎所有底层细节都由开发者手动管理。这就像开**手动挡赛车**——需要自己换挡、控制离合、管理转速，但可以榨取每一分性能。

为什么要这么复杂？因为**游戏引擎需要极致性能**。UE5 的渲染线程每帧要处理成千上万个 Draw Call，自动管理的开销会成为瓶颈。DX12 让引擎自己决定何时分配内存、何时提交命令、何时同步，从而实现更好的并行性。

### 2. DX12 核心对象层级

DX12 的对象之间有明确的层级关系，理解这个层级是掌握 DX12 的关键：

```
Device（GPU 设备 — 一切从这里开始）
  |
  +-- Command Queue（命令队列 — CPU 往里扔任务，GPU 从中取任务）
  |     |
  |     +-- Command Allocator（命令内存分配器 — 管理命令的底层内存）
  |           |
  |           +-- Command List（命令列表 — 你在这里"录制"渲染命令）
  |
  +-- Descriptor Heap（描述符堆 — 存放各种"资源描述"的仓库）
  |     |
  |     +-- RTV（Render Target View — 告诉 GPU "往哪个表面画"）
  |     +-- CBV（Constant Buffer View — 告诉 GPU "从哪读 uniform 数据"）
  |     +-- SRV（Shader Resource View — 告诉 GPU "从哪读纹理/缓冲"）
  |
  +-- Fence（栅栏 — CPU/GPU 之间的同步工具）

Swap Chain（交换链 — 独立于 Device，管理窗口的前后缓冲）
  |
  +-- Back Buffer 0（后缓冲 0 — GPU 在上面画）
  +-- Back Buffer 1（后缓冲 1 — 正在展示给用户）
  +-- Back Buffer 2（后缓冲 2 — 三缓冲时使用）
```

### 3. 每个核心对象的作用（通俗比喻）

| 对象 | 比喻 | 解释 |
|------|------|------|
| **Device** | 显卡的"门牌号" | 你通过它跟 GPU 通信。创建任何 GPU 资源都需要先有 Device。 |
| **Command Queue** | GPU 的"任务队列" | CPU 把录制好的 Command List 扔进队列，GPU 按顺序执行。 |
| **Command Allocator** | 命令的"内存池" | Command List 录制的命令需要占用内存，Allocator 负责分配这些内存。 |
| **Command List** | "任务清单" | 你在上面记录一系列 GPU 命令（清屏、画三角形、设置状态等），然后一次性提交。 |
| **Swap Chain** | "双缓冲黑板" | 两块黑板交替使用：一块展示给观众（前台），一块在后台画画。画完交换。 |
| **Descriptor Heap** | "资源索引柜" | GPU 不能直接通过指针访问资源，需要通过"描述符"（Descriptor）来定位。Heap 就是一排格子，每个格子里放一个描述符。 |
| **RTV** | "画板标签" | 告诉 GPU "这块纹理是渲染目标，你可以往上面画东西"。 |
| **Fence** | "栅栏" | CPU 提交命令后需要等 GPU 完成，Fence 就是用来检查"GPU 干完了没有"的工具。 |

### 4. 渲染流程概览

每一帧，你的程序要做以下几件事：

```
初始化阶段（只做一次）：
  创建 Device -> 创建 Command Queue -> 创建 Swap Chain
  -> 创建 RTV -> 创建 Command Allocator + Command List
  -> 创建 Fence

每帧渲染循环：
  ┌─────────────────────────────────────────────────────┐
  │ 1. BeginFrame（开始一帧）                            │
  │    - 等待 GPU 完成上一帧（检查 Fence）                │
  │    - 重置 Command Allocator 和 Command List           │
  │    - 设置后缓冲为"渲染目标"状态                       │
  │                                                      │
  │ 2. ClearColor（清屏）                                │
  │    - 用指定颜色填充后缓冲                             │
  │                                                      │
  │ 3. EndFrame（结束一帧）                              │
  │    - 关闭 Command List                               │
  │    - 提交到 Command Queue 执行                        │
  │    - 设置 Fence 标记（用来追踪 GPU 进度）             │
  │    - Present（交换前后缓冲，显示画面）                │
  └─────────────────────────────────────────────────────┘
```

### 5. 为什么 DX12 初始化代码这么多？

OpenGL 创建窗口并清屏大概 30 行代码。DX12 做同样的事需要 200+ 行。这是因为：

1. **显式资源管理**：OpenGL 驱动在背后帮你创建和管理帧缓冲、命令缓冲等资源。DX12 要求你自己创建每一个。
2. **手动同步**：OpenGL 驱动自动处理 CPU/GPU 同步。DX12 需要你自己用 Fence 来协调。
3. **描述符系统**：OpenGL 直接绑定纹理/缓冲（`glBindTexture`）。DX12 需要先创建描述符，再把描述符放在堆里，然后绑定堆。

好消息是：这些初始化代码写一次就行，后续添加功能（画三角形、加载纹理）时框架不需要大改。

---

## 操作步骤

### 1. 创建文件

```
src/Renderer/Public/DX12Device.h
src/Renderer/Private/DX12Device.cpp
```

### 2. DX12Device.h — 设备封装头文件

```cpp
#pragma once

// DX12 头文件
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

using Microsoft::WRL::ComPtr;

// 后缓冲数量（双缓冲 = 2）
static const uint32_t FRAME_COUNT = 2;

class DX12Device {
public:
    DX12Device() = default;
    ~DX12Device() { Destroy(); }

    // 禁止拷贝（GPU 资源是唯一的）
    DX12Device(const DX12Device&) = delete;
    DX12Device& operator=(const DX12Device&) = delete;

    // ========== 生命周期 ==========

    /// 初始化 DX12 设备
    /// @param hwnd   渲染目标窗口的句柄
    /// @param width  窗口客户区宽度
    /// @param height 窗口客户区高度
    void Init(HWND hwnd, uint32_t width, uint32_t height);

    /// 释放所有 DX12 资源（必须等 GPU 完成后调用）
    void Destroy();

    // ========== 每帧渲染 ==========

    /// 开始一帧：重置命令列表，设置渲染目标
    void BeginFrame();

    /// 用指定颜色清除后缓冲
    /// @param r, g, b, a  RGBA 颜色分量，范围 [0.0, 1.0]
    void ClearColor(float r, float g, float b, float a);

    /// 结束一帧：提交命令，翻转交换链
    void EndFrame();

    // ========== 窗口事件 ==========

    /// 窗口大小改变时调用（释放旧缓冲，创建新缓冲）
    /// @param width  新宽度
    /// @param height 新高度
    void Resize(uint32_t width, uint32_t height);

    // ========== 状态查询 ==========

    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    bool IsInitialized() const { return initialized_; }

    // ========== 底层对象访问（供 DX12Pipeline / DX12Mesh 等子系统使用）==========
    // 这些 getter 在课15 开始会被 Pipeline/Mesh 调用，所以必须暴露

    ID3D12Device* GetDevice() const { return device_.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return commandQueue_.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
    ID3D12CommandAllocator* GetCommandAllocator() const { return commandAllocator_.Get(); }

    // 等待 GPU 完成所有已提交命令（无参版本——常见用法）
    // 实现里会让 fenceValue_ 自增再调用私有的 WaitForGPU(uint64_t)
    void WaitForGPU();

private:
    // ========== 内部初始化方法 ==========

    void EnableDebugLayer();
    void CreateDevice();
    void CreateCommandQueue();
    void CreateSwapChain(HWND hwnd);
    void CreateRenderTargetViews();
    void CreateCommandObjects();
    void CreateFence();

    /// 等待 GPU 完成到指定 Fence 值
    void WaitForGPU(uint64_t fenceValue);

    /// 释放后缓冲相关资源（Resize 和 Destroy 时调用）
    void ReleaseBackBuffers();

    // ========== DX12 核心对象 ==========

    // 设备和命令
    ComPtr<ID3D12Device> device_;                       // GPU 设备
    ComPtr<ID3D12CommandQueue> commandQueue_;           // 命令队列
    ComPtr<ID3D12CommandAllocator> commandAllocator_;   // 命令内存分配器
    ComPtr<ID3D12GraphicsCommandList> commandList_;     // 命令列表

    // 交换链
    ComPtr<IDXGISwapChain3> swapChain_;                 // 交换链

    // 渲染目标
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;              // RTV 描述符堆
    ComPtr<ID3D12Resource> backBuffers_[FRAME_COUNT];   // 后缓冲数组
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[FRAME_COUNT]; // 每个 RTV 的句柄

    // 同步
    ComPtr<ID3D12Fence> fence_;                         // 栅栏
    uint64_t fenceValue_ = 0;                           // 当前 Fence 值
    HANDLE fenceEvent_ = nullptr;                       // Fence 通知事件

    // 状态
    uint32_t currentFrameIndex_ = 0;                    // 当前帧索引（0 或 1）
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t rtvDescriptorSize_ = 0;                    // 一个 RTV 描述符占多大
    bool initialized_ = false;
};
```

**讲解**：

- **ComPtr**：这是微软提供的智能指针（`Microsoft::WRL::ComPtr`），用于管理 COM 对象的生命周期。DX12 的所有接口都是 COM 对象，用 ComPtr 可以自动释放，不需要手动调 `Release()`。
- **FRAME_COUNT = 2**：使用双缓冲。前台缓冲在显示器上展示，后台缓冲供 GPU 画图。画完后交换。也可以设为 3（三缓冲），减少等待但增加延迟。
- **禁止拷贝**：GPU 资源是唯一的，不应该被拷贝。用 `delete` 禁止拷贝构造和赋值。
- **rtvDescriptorSize_**：不同 GPU 上描述符的大小可能不同，需要查询。后面创建 RTV 时要用这个值来计算偏移。

### 3. DX12Device.cpp — 实现

这个文件比较长，但每一步都有详细注释。不要被代码量吓到——大部分是初始化样板代码，理解了原理之后就会觉得很自然。

```cpp
#include "Renderer/Public/DX12Device.h"
#include "Core/Public/Logger.h"

#include <dxgi1_6.h>
#include <d3d12.h>

// 链接 DX12 库（也可以在 CMakeLists.txt 中用 target_link_libraries）
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

void DX12Device::Init(HWND hwnd, uint32_t width, uint32_t height) {
    ME_LOG_INFO("DX12Device::Init — 开始初始化 (窗口: %ux%u)", width, height);

    width_ = width;
    height_ = height;

    // 按顺序创建所有 DX12 对象
    // 顺序很重要：后面的对象依赖前面的对象
    EnableDebugLayer();       // 1. 调试层（开发时帮助发现错误）
    CreateDevice();           // 2. 设备（一切的基础）
    CreateCommandQueue();     // 3. 命令队列（需要设备）
    CreateSwapChain(hwnd);    // 4. 交换链（需要命令队列和窗口句柄）
    CreateRenderTargetViews(); // 5. 渲染目标视图（需要设备和交换链）
    CreateCommandObjects();   // 6. 命令对象（需要设备）
    CreateFence();            // 7. 栅栏（需要设备）

    initialized_ = true;
    ME_LOG_INFO("DX12Device::Init — 初始化完成");
}

// ============================================================================
// EnableDebugLayer — 启用调试层
// ============================================================================
// 调试层会在 Visual Studio 的"输出"窗口打印有用的错误信息。
// 比如：资源未释放、命令参数错误、状态不正确等。
// 发布版本应该关闭调试层以提升性能。
void DX12Device::EnableDebugLayer() {
    // 步骤：
    // 1. 创建 Debug 接口
    // 2. 启用调试层
    // 注意：如果显卡驱动不支持调试层，这里的调用会静默失败，不影响运行

#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        ME_LOG_INFO("DX12 调试层已启用");
    }
#endif
}

// ============================================================================
// CreateDevice — 创建 DX12 设备
// ============================================================================
// Device 是你与 GPU 通信的"大门"。创建任何 GPU 资源都需要通过 Device。
//
// 步骤：
// 1. 创建 DXGI Factory（用来枚举显卡适配器）
// 2. 枚举适配器，找到合适的显卡
// 3. 用适配器创建 Device
void DX12Device::CreateDevice() {
    // ---- 步骤 1：创建 DXGI Factory ----
    // DXGI（DirectX Graphics Infrastructure）是 DirectX 的底层基础设施，
    // 负责显卡枚举、窗口呈现（Present）等功能。
    // 参数 CREATE_FACTORY_DEBUG 在调试模式下提供更多诊断信息。
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        ME_LOG_ERROR("创建 DXGI Factory 失败: 0x%08X", hr);
        return;
    }

    // ---- 步骤 2：找到合适的显卡适配器 ----
    // 适配器 = 显卡。系统可能有多个显卡（集成显卡 + 独立显卡）。
    // 我们优先找支持 DX12 的独立显卡。
    ComPtr<IDXGIAdapter1> adapter;
    int adapterIndex = 0;
    bool adapterFound = false;

    while (factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // 检查是否是软件适配器（Basic Render Driver），跳过它
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapterIndex++;
            continue;
        }

        // 检查适配器是否支持 D3D_FEATURE_LEVEL_12_0（DX12 功能级别）
        // 这个检查不会创建 Device，只是测试兼容性
        if (SUCCEEDED(D3D12CreateDevice(
                adapter.Get(),
                D3D_FEATURE_LEVEL_12_0,
                _uuidof(ID3D12Device),
                nullptr))) {
            adapterFound = true;

            // 将适配器描述从 wchar_t 转换为 char 用于日志
            char adapterName[128];
            wcstombs(adapterName, desc.Description, sizeof(adapterName));
            ME_LOG_INFO("选择显卡适配器: %s (显存: %zu MB)",
                adapterName, desc.DedicatedVideoMemory / (1024 * 1024));
            break;
        }

        adapterIndex++;
    }

    if (!adapterFound) {
        ME_LOG_ERROR("未找到支持 DX12 的显卡适配器");
        return;
    }

    // ---- 步骤 3：创建 Device ----
    // D3D12CreateDevice 类似于 OpenGL 的 glGetString(GL_RENDERER)，
    // 但它会真正创建一个与 GPU 通信的接口。
    hr = D3D12CreateDevice(
        adapter.Get(),                  // 使用哪个显卡
        D3D_FEATURE_LEVEL_12_0,         // 请求的功能级别
        IID_PPV_ARGS(&device_)          // 输出 Device 对象
    );

    if (FAILED(hr)) {
        ME_LOG_ERROR("创建 D3D12 Device 失败: 0x%08X", hr);
        return;
    }

    ME_LOG_INFO("D3D12 Device 创建成功");
}

// ============================================================================
// CreateCommandQueue — 创建命令队列
// ============================================================================
// 命令队列是 CPU 和 GPU 之间的"传送带"。
// CPU 把命令列表（Command List）放到队列上，GPU 按顺序从队列取出来执行。
//
// 类比：餐厅的厨房传菜窗口
// - CPU = 服务员（接单、写菜单）
// - Command List = 菜单（写着要做什么菜）
// - Command Queue = 传菜窗口（服务员把菜单放到窗口，厨师从窗口取菜单）
// - GPU = 厨师（按菜单做菜）
void DX12Device::CreateCommandQueue() {
    // 描述命令队列的属性
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;  // 直接命令队列（支持所有 GPU 命令）
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;  // 普通优先级
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;  // 不设置特殊标志
    queueDesc.NodeMask = 0;  // 单 GPU 模式，设为 0

    HRESULT hr = device_->CreateCommandQueue(
        &queueDesc,                 // 队列描述
        IID_PPV_ARGS(&commandQueue_) // 输出 CommandQueue 对象
    );

    if (FAILED(hr)) {
        ME_LOG_ERROR("创建 Command Queue 失败: 0x%08X", hr);
        return;
    }

    ME_LOG_INFO("Command Queue 创建成功");
}

// ============================================================================
// CreateSwapChain — 创建交换链
// ============================================================================
// 交换链管理"双缓冲"机制：
// - Buffer 0：GPU 正在上面画图（后缓冲）
// - Buffer 1：正在显示器上展示（前台缓冲）
// 调用 Present() 时，两者交换身份。
//
// 参数说明：
// - hwnd：Qt 窗口的底层 Win32 句柄
// - BufferCount：缓冲数量（2 = 双缓冲，3 = 三缓冲）
// - Format：像素格式（DXGI_FORMAT_R8G8B8A8_UNORM = 每像素 32 位 RGBA）
// - SwapEffect：DXGI_SWAP_EFFECT_FLIP_DISCARD 是最高效的翻转模式
void DX12Device::CreateSwapChain(HWND hwnd) {
    // 先创建一个 DXGI Factory（用于检查是否支持撕裂同步 Tearing）
    ComPtr<IDXGIFactory4> factory;
    CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

    // 检查是否支持撕裂（Tearing）—— 允许窗口模式下不锁帧率
    BOOL allowTearing = FALSE;
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory.As(&factory5))) {
        factory5->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING,
            &allowTearing,
            sizeof(allowTearing));
    }

    // 描述交换链
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width_;                    // 缓冲宽度（0 = 跟窗口一样）
    swapChainDesc.Height = height_;                  // 缓冲高度
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 32位 RGBA 颜色
    swapChainDesc.Stereo = FALSE;                    // 不使用立体渲染
    swapChainDesc.SampleDesc.Count = 1;              // 不使用多重采样（MSAA）
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 用作渲染目标
    swapChainDesc.BufferCount = FRAME_COUNT;         // 双缓冲
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;    // 缩放模式
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // 翻转+丢弃模式
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; // 不使用透明

    // 创建交换链
    // 注意：CreateSwapChainForHwnd 创建的是 IDXGISwapChain1，
    // 我们用 As() 转换为 IDXGISwapChain3（支持 GetCurrentBackBufferIndex）
    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT hr = factory->CreateSwapChainForHwnd(
        commandQueue_.Get(),  // 命令队列（交换链的 Present 需要队列）
        hwnd,                 // 窗口句柄
        &swapChainDesc,       // 交换链描述
        nullptr,              // 无全屏描述
        nullptr,              // 不限制输出到特定显示器
        &swapChain1           // 输出交换链
    );

    if (FAILED(hr)) {
        ME_LOG_ERROR("创建 Swap Chain 失败: 0x%08X", hr);
        return;
    }

    // 升级到 IDXGISwapChain3（提供 GetCurrentBackBufferIndex 方法）
    hr = swapChain1.As(&swapChain_);
    if (FAILED(hr)) {
        ME_LOG_ERROR("获取 SwapChain3 接口失败: 0x%08X", hr);
        return;
    }

    // 获取当前后缓冲索引（双缓冲时为 0 或 1）
    currentFrameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    ME_LOG_INFO("Swap Chain 创建成功 (缓冲数: %u)", FRAME_COUNT);
}

// ============================================================================
// CreateRenderTargetViews — 创建渲染目标视图（RTV）
// ============================================================================
// 后缓冲是一块 GPU 纹理（ID3D12Resource），GPU 可以在上面画图。
// 但 GPU 需要知道"如何解读这块纹理"，这就是 RTV（Render Target View）的作用。
//
// 类比：
// - 后缓冲 = 一块画布（Canvas）
// - RTV = 画布上的画框（Frame），告诉 GPU "这是画布的可画区域"
// - Descriptor Heap = 放画框的架子（每个格子放一个画框）
//
// 步骤：
// 1. 从交换链获取后缓冲纹理
// 2. 创建 RTV 描述符堆
// 3. 为每个后缓冲创建 RTV
void DX12Device::CreateRenderTargetViews() {
    // ---- 步骤 1：创建 RTV 描述符堆 ----
    // 描述符堆是一块内存，存放多个描述符（这里是 RTV 描述符）
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;   // 类型：渲染目标视图
    rtvHeapDesc.NumDescriptors = FRAME_COUNT;             // 双缓冲需要 2 个 RTV
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;  // 不允许着色器访问（RTV 不需要）
    rtvHeapDesc.NodeMask = 0;

    HRESULT hr = device_->CreateDescriptorHeap(
        &rtvHeapDesc,
        IID_PPV_ARGS(&rtvHeap_)
    );

    if (FAILED(hr)) {
        ME_LOG_ERROR("创建 RTV 描述符堆失败: 0x%08X", hr);
        return;
    }

    // 查询一个 RTV 描述符占多少字节（不同 GPU 可能不同）
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV
    );

    // ---- 步骤 2：为每个后缓冲创建 RTV ----
    // 获取描述符堆的起始句柄
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < FRAME_COUNT; i++) {
        // 从交换链获取第 i 个后缓冲
        hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i]));
        if (FAILED(hr)) {
            ME_LOG_ERROR("获取后缓冲 %u 失败: 0x%08X", i, hr);
            return;
        }

        // 为这个后缓冲创建 RTV
        // CreateRenderTargetView 的第二个参数 nullptr 表示使用默认格式
        // （即交换链创建时指定的 DXGI_FORMAT_R8G8B8A8_UNORM）
        device_->CreateRenderTargetView(
            backBuffers_[i].Get(),  // 目标资源（后缓冲纹理）
            nullptr,                // 描述符（nullptr = 使用资源默认格式）
            rtvHandle               // 描述符句柄（存放 RTV 的位置）
        );

        // 保存句柄，后续渲染时要用
        rtvHandles_[i] = rtvHandle;

        // 移动到描述符堆的下一个位置
        rtvHandle.ptr += rtvDescriptorSize_;
    }

    ME_LOG_INFO("RTV 创建成功 (描述符大小: %u 字节)", rtvDescriptorSize_);
}

// ============================================================================
// CreateCommandObjects — 创建命令分配器和命令列表
// ============================================================================
// Command Allocator（命令分配器）：管理命令列表的底层内存。
//   - 每次 Reset() 会回收上一帧的命令内存
//   - 必须等 GPU 执行完上一帧的命令后才能 Reset
//
// Command List（命令列表）：你在这里"录制"GPU 命令。
//   - 录制完成后 Close()，然后提交给命令队列执行
//   - 每帧开始时 Reset（需要传入 Allocator）
void DX12Device::CreateCommandObjects() {
    // ---- 创建命令分配器 ----
    HRESULT hr = device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,  // 类型：直接命令（支持所有操作）
        IID_PPV_ARGS(&commandAllocator_)
    );

    if (FAILED(hr)) {
        ME_LOG_ERROR("创建 Command Allocator 失败: 0x%08X", hr);
        return;
    }

    // ---- 创建命令列表 ----
    // 参数 pipelineState 设为 nullptr 表示不绑定初始的 PSO（Pipeline State Object）
    // 后续画三角形时才需要 PSO
    hr = device_->CreateCommandList(
        0,                              // 节点掩码（单 GPU = 0）
        D3D12_COMMAND_LIST_TYPE_DIRECT,  // 命令列表类型
        commandAllocator_.Get(),         // 关联的命令分配器
        nullptr,                         // 初始 PSO（无）
        IID_PPV_ARGS(&commandList_)
    );

    if (FAILED(hr)) {
        ME_LOG_ERROR("创建 Command List 失败: 0x%08X", hr);
        return;
    }

    // 命令列表创建后处于"打开"状态，先关闭它。
    // 因为我们还没有开始录制命令。
    // BeginFrame() 时会 Reset 重新打开。
    commandList_->Close();

    ME_LOG_INFO("Command Allocator + Command List 创建成功");
}

// ============================================================================
// CreateFence — 创建同步栅栏
// ============================================================================
// Fence（栅栏）是 CPU/GPU 同步的核心机制。
//
// 工作原理：
// 1. CPU 调用 commandQueue->Signal(fence, value)  —— 在 GPU 命令流中插入一个标记
// 2. GPU 执行到这个标记时，更新 fence 的内部值为 value
// 3. CPU 检查 fence->GetCompletedValue() —— 看看 GPU 执行到哪里了
// 4. 如果 GPU 还没执行到，CPU 可以等待（用 Event）
//
// 类比：在马拉松赛道上设检查点。CPU 是赛事组织者，GPU 是选手。
// Signal = 在赛道上放一个写着号码的牌子。
// GetCompletedValue = 选手跑到哪个牌子了。
void DX12Device::CreateFence() {
    HRESULT hr = device_->CreateFence(
        0,                          // 初始值
        D3D12_FENCE_FLAG_NONE,      // 无特殊标志
        IID_PPV_ARGS(&fence_)
    );

    if (FAILED(hr)) {
        ME_LOG_ERROR("创建 Fence 失败: 0x%08X", hr);
        return;
    }

    // 创建一个 Windows 事件对象，用于等待 Fence
    // 当 GPU 完成 Fence Signal 时，这个事件会被触发
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        ME_LOG_ERROR("创建 Fence Event 失败");
        return;
    }

    fenceValue_ = 0;

    ME_LOG_INFO("Fence 创建成功");
}

// ============================================================================
// WaitForGPU — 等待 GPU 完成
// ============================================================================
void DX12Device::WaitForGPU(uint64_t fenceValue) {
    // 在命令队列中插入一个 Signal：当 GPU 执行到这里时，设置 fence 值
    commandQueue_->Signal(fence_.Get(), fenceValue);

    // 检查 GPU 是否已经完成
    if (fence_->GetCompletedValue() < fenceValue) {
        // GPU 还没完成，设置事件触发条件：
        // 当 fence 值达到 fenceValue 时触发 fenceEvent_
        fence_->SetEventOnCompletion(fenceValue, fenceEvent_);

        // 无限等待事件触发（GPU 完成了）
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

// 无参重载：等待所有已提交命令完成。课15 起的 Pipeline/Mesh 会用它
// 调用时机：上传缓冲复制完成后、销毁资源前
void DX12Device::WaitForGPU() {
    fenceValue_++;
    WaitForGPU(fenceValue_);
}

// ============================================================================
// ReleaseBackBuffers — 释放后缓冲相关资源
// ============================================================================
void DX12Device::ReleaseBackBuffers() {
    // 先等待 GPU 完成所有工作，确保 GPU 不再使用这些缓冲
    if (fence_ && commandQueue_) {
        fenceValue_++;
        WaitForGPU(fenceValue_);
    }

    // 释放后缓冲（ComPtr 会自动 Release）
    for (uint32_t i = 0; i < FRAME_COUNT; i++) {
        backBuffers_[i].Reset();
    }

    // 释放 RTV 描述符堆
    rtvHeap_.Reset();
}

// ============================================================================
// BeginFrame — 开始一帧
// ============================================================================
void DX12Device::BeginFrame() {
    if (!initialized_) return;

    // 更新当前帧索引（交换链在 Present 后会自动更新）
    currentFrameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    // ---- 步骤 1：重置命令分配器 ----
    // 这会回收上一帧的命令内存。必须等 GPU 完成上一帧后才能调用。
    // 我们在 EndFrame() 中做了 Signal + Wait，所以到这里时 GPU 肯定完成了。
    commandAllocator_->Reset();

    // ---- 步骤 2：重置命令列表 ----
    // 重新打开命令列表，准备录制新命令
    commandList_->Reset(commandAllocator_.Get(), nullptr);

    // ---- 步骤 3：资源屏障 — 将后缓冲从"呈现"状态转为"渲染目标"状态 ----
    // DX12 要求显式管理资源状态：
    // - Present 状态：缓冲正在被显示器读取（只能用于 Present）
    // - Render Target 状态：GPU 可以在上面画图
    // 如果不转换状态，GPU 可能会读到错误的数据。
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = backBuffers_[currentFrameIndex_].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList_->ResourceBarrier(1, &barrier);

    // ---- 步骤 4：设置渲染目标 ----
    // 告诉 GPU：接下来的绘制操作都画到这个后缓冲上
    commandList_->OMSetRenderTargets(
        1,                                  // 渲染目标数量
        &rtvHandles_[currentFrameIndex_],   // RTV 句柄
        FALSE,                              // RTV 句柄不是数组（是单个元素指针）
        nullptr                             // 无深度模板缓冲（暂时不需要）
    );
}

// ============================================================================
// ClearColor — 清屏
// ============================================================================
void DX12Device::ClearColor(float r, float g, float b, float a) {
    if (!initialized_) return;

    // 将颜色打包为 float[4] 数组
    float clearColor[] = { r, g, b, a };

    // 用指定颜色填充整个后缓冲
    commandList_->ClearRenderTargetView(
        rtvHandles_[currentFrameIndex_],  // 要清除的 RTV
        clearColor,                         // 颜色
        0,                                  // 矩形数量（0 = 清除整个缓冲）
        nullptr                             // 矩形数组（nullptr = 全部清除）
    );
}

// ============================================================================
// EndFrame — 结束一帧
// ============================================================================
void DX12Device::EndFrame() {
    if (!initialized_) return;

    // ---- 步骤 1：资源屏障 — 将后缓冲从"渲染目标"转回"呈现"状态 ----
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = backBuffers_[currentFrameIndex_].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList_->ResourceBarrier(1, &barrier);

    // ---- 步骤 2：关闭命令列表 ----
    // 命令列表录制完成，必须关闭后才能提交
    commandList_->Close();

    // ---- 步骤 3：提交命令列表到命令队列 ----
    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(
        1,              // 提交的命令列表数量
        commandLists    // 命令列表数组
    );

    // ---- 步骤 4：Signal Fence ----
    // 在 GPU 命令流中插入一个标记，表示"执行到这里了"
    fenceValue_++;
    commandQueue_->Signal(fence_.Get(), fenceValue_);

    // ---- 步骤 5：Present — 翻转交换链 ----
    // 参数 1 = SyncInterval（垂直同步间隔）
    //   0 = 不等垂直同步，立即翻转（帧率不锁）
    //   1 = 等一个垂直同步（锁 60fps，最常用）
    // 参数 2 = Flags
    //   DXGI_PRESENT_ALLOW_TEARING = 允许撕裂（窗口模式下不锁帧率时需要）
    swapChain_->Present(1, 0);
}

// ============================================================================
// Resize — 窗口大小改变
// ============================================================================
void DX12Device::Resize(uint32_t width, uint32_t height) {
    if (!initialized_ || (width == width_ && height == height_)) return;

    ME_LOG_INFO("DX12Device::Resize (%ux%u -> %ux%u)", width_, height_, width, height);

    width_ = width;
    height_ = height;

    // 1. 刷新命令队列（确保 GPU 不再使用旧的缓冲）
    // 先关闭当前命令列表（如果有的话），刷新队列
    commandList_->Close();
    ID3D12CommandList* cmdLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, cmdLists);
    fenceValue_++;
    WaitForGPU(fenceValue_);

    // 2. 释放旧的后缓冲和 RTV 堆
    ReleaseBackBuffers();

    // 3. 调整交换链缓冲大小
    // 参数 0 = 宽度自适应窗口，0 = 高度自适应窗口
    // 参数 DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH 允许切换窗口/全屏模式
    swapChain_->ResizeBuffers(
        FRAME_COUNT,    // 缓冲数量
        width,          // 新宽度
        height,         // 新高度
        DXGI_FORMAT_R8G8B8A8_UNORM, // 像素格式
        0               // 标志
    );

    // 4. 重新创建 RTV
    CreateRenderTargetViews();

    // 5. 更新帧索引
    currentFrameIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

// ============================================================================
// Destroy — 销毁所有资源
// ============================================================================
void DX12Device::Destroy() {
    if (!initialized_) return;

    ME_LOG_INFO("DX12Device::Destroy — 开始清理资源");

    // 1. 等待 GPU 完成所有工作（最重要！）
    // 如果不等 GPU 就释放资源，GPU 可能还在使用它们，会导致崩溃
    fenceValue_++;
    WaitForGPU(fenceValue_);

    // 2. 释放所有资源（ComPtr 的 Reset = Release + 置空）
    // 释放顺序：先释放依赖多的，再释放基础对象
    ReleaseBackBuffers();

    commandList_.Reset();
    commandAllocator_.Reset();
    swapChain_.Reset();
    commandQueue_.Reset();

    // Fence 和 Event
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
    fence_.Reset();

    // Device 最后释放
    device_.Reset();

    initialized_ = false;
    ME_LOG_INFO("DX12Device::Destroy — 资源清理完成");
}
```

**逐方法讲解要点**：

| 方法 | 核心概念 | 常见错误 |
|------|---------|---------|
| `EnableDebugLayer` | 只在 Debug 模式启用，Release 不开 | 忘记 `_DEBUG` 宏保护 |
| `CreateDevice` | 枚举适配器，优先选独立显卡 | 没跳过软件适配器（Basic Render Driver） |
| `CreateCommandQueue` | 类型选 DIRECT，支持所有命令 | 选错类型（BUNDLE 类型不支持完整渲染） |
| `CreateSwapChain` | Flip-Discard 模式最高效 | 旧教程用 BitBlt 模式，性能差 |
| `CreateRenderTargetViews` | 描述符堆 + 句柄偏移 | 忘记乘 rtvDescriptorSize_ |
| `CreateCommandObjects` | 创建后要 Close() | 创建后直接录制，没有 Close 也不 Reset |
| `CreateFence` | 需要一个 Windows Event | 忘记创建 Event，等待时崩溃 |
| `BeginFrame` | 资源屏障 Present->RT | 忘记转换状态，Clear 不生效 |
| `EndFrame` | 关闭列表再提交 | 没关闭就提交，返回错误 |
| `Resize` | 先等 GPU、释放旧缓冲、调整大小、重建 RTV | 直接调 ResizeBuffers 不等待 GPU |
| `Destroy` | 先等 GPU 再释放 | 不等 GPU 就释放，GPU 还在用就崩溃 |

### 4. 验证代码 — main.cpp

窗口 resize 时要通知 DX12 重建后台缓冲。正确落点是**子类化 QWidget 重写 `resizeEvent`**（Qt 事件系统的标准做法），而不是在 main.cpp 里连信号或装事件过滤器。

在 `main.cpp` 顶部定义验证用的 `RenderWidget`（只重写虚函数、无信号槽，不需要 `Q_OBJECT`，也不必单拆头文件）：

```cpp
#include <QApplication>
#include <QWidget>
#include <QTimer>
#include <QPaintEngine>
#include "Renderer/Public/DX12Device.h"
#include "Core/Public/Logger.h"

// 验证用渲染窗口：把 resize 事件转给 DX12Device
class RenderWidget : public QWidget {
public:
    explicit RenderWidget(QWidget* parent = nullptr)
        : QWidget(parent) {
        setAttribute(Qt::WA_PaintOnScreen, true);  // 告诉 Qt 我们自己画
    }

    void SetDX12Device(DX12Device* device) { device_ = device; }

protected:
    void resizeEvent(QResizeEvent* event) override {
        if (device_ && device_->IsInitialized()) {
            device_->Resize(width(), height());
        }
        QWidget::resizeEvent(event);
    }

    // DX12 自己管理绘制，不需要 Qt 的绘制系统
    QPaintEngine* paintEngine() const override { return nullptr; }

private:
    DX12Device* device_ = nullptr;
};

int main(int argc, char* argv[]) {
    // Qt 应用程序初始化
    QApplication app(argc, argv);

    // ---- 创建渲染窗口 ----
    // DX12 需要一个 HWND（Windows 窗口句柄）。
    // QWidget::winId() 返回的就是底层平台窗口句柄，
    // 在 Windows 上就是 HWND。
    RenderWidget renderWindow;
    renderWindow.setWindowTitle("Material Editor — DX12 Clear Screen Test");
    renderWindow.resize(800, 600);
    renderWindow.show();

    // ---- 初始化 DX12 ----
    DX12Device device;
    HWND hwnd = (HWND)renderWindow.winId();
    device.Init(hwnd, 800, 600);
    renderWindow.SetDX12Device(&device);   // 之后 resize 会自动转发给 device
    ME_LOG_INFO("DX12 device initialized");

    // ---- 渲染循环 ----
    // Qt 的事件循环和 DX12 渲染循环结合：
    // 用 QTimer 每 16ms（~60fps）触发一次渲染。
    // 这种方式比 while(true) 循环更好，因为不会阻塞 Qt 的事件处理。
    QTimer renderTimer;
    QObject::connect(&renderTimer, &QTimer::timeout, [&]() {
        device.BeginFrame();
        // 清屏为深蓝色（暗蓝，类似 UE5 编辑器的默认背景）
        device.ClearColor(0.1f, 0.1f, 0.3f, 1.0f);
        device.EndFrame();
    });
    renderTimer.start(16); // 约 60fps

    // ---- 运行 ----
    // resize 已由 RenderWidget::resizeEvent 处理（见顶部类定义）
    int result = app.exec();

    // 退出前清理 DX12 资源
    device.Destroy();
    ME_LOG_INFO("Application exited");

    return result;
}
```

### 5. CMakeLists.txt 更新

需要在 `src/Renderer/CMakeLists.txt`（或主 CMakeLists.txt）中链接 DX12 相关库：

```cmake
# DX12 相关头文件路径（Windows SDK 通常已包含，不需要额外设置）
# 但如果编译器找不到头文件，可以手动添加：
# include_directories(${Windows_INCLUDE_DIRS})

# 链接 DX12 库
target_link_libraries(MaterialEditor
    PRIVATE
        d3d12.lib      # DX12 核心 API
        dxgi.lib        # DXGI（交换链、适配器枚举）
        dxguid.lib      # GUID 定义（IID_PPV_ARGS 需要）
)
```

或者如果你使用 `find_package`：

```cmake
# CMake 3.20+ 可以用 find_package 查找 Windows SDK
# 但 DX12 库通常直接链接即可
if(WIN32)
    target_link_libraries(MaterialEditor PRIVATE d3d12 dxgi dxguid)
endif()
```

### 6. 编译和运行

```bash
cd material_editor_project
cmake --preset mingw            # 配置（MinGW preset，见课5 环境搭建）
cmake --build build-mingw       # 编译
```

运行后应该看到一个蓝色窗口，标题为 "Material Editor -- DX12 Clear Screen Test"。

---

## 讲解要点

### 为什么 DX12 需要这么多初始化代码？

对比例子：

| 功能 | OpenGL 代码行数 | DX12 代码行数 | 原因 |
|------|----------------|--------------|------|
| 创建设备 | 0（自动） | ~40 行 | 手动枚举适配器、选择功能级别 |
| 创建命令队列 | 0（自动） | ~15 行 | 手动创建队列并设置属性 |
| 创建交换链 | ~10 行（GLFW 帮你做） | ~40 行 | 手动设置缓冲格式、翻转模式 |
| 创建渲染目标 | 0（自动） | ~30 行 | 手动创建描述符堆和 RTV |
| 创建命令列表 | 0（自动） | ~20 行 | 手动创建分配器和命令列表 |
| 同步 | 0（自动） | ~20 行 | 手动创建 Fence 和 Event |

**总计**：OpenGL ~10 行 vs DX12 ~165 行。但 DX12 的优势在于：

1. **可预测的性能**：没有驱动的"隐式优化"导致帧时间不稳定
2. **多线程录制**：多个线程可以并行录制不同的 Command List
3. **精确控制**：你决定何时分配、何时提交、何时同步

### 双缓冲/三缓冲的意义

- **单缓冲**（不用）：GPU 和显示器共享一个缓冲，画面会出现撕裂（上半帧和下半帧内容不同步）
- **双缓冲**：前台展示、后台画图，画完再交换。消除撕裂，但 GPU 可能需要等垂直同步
- **三缓冲**：增加一个缓冲，GPU 不用等垂直同步就可以开始画下一帧。更流畅但多占一帧的延迟

### 为什么需要 Fence 做同步？

CPU 和 GPU 是**异步**工作的：
- CPU 提交命令后立即返回，不等待 GPU 执行
- GPU 按自己的节奏执行命令

如果没有 Fence，可能出现这样的问题：
1. CPU 在第 1 帧提交了命令
2. CPU 立刻开始准备第 2 帧，调 `commandAllocator->Reset()`
3. 但 GPU 还在执行第 1 帧的命令！
4. `Reset()` 回收了 GPU 正在使用的内存 -> 崩溃

Fence 的作用就是让 CPU 知道"GPU 执行到哪里了"，避免这种冲突。

### RTV（Render Target View）是什么？

在 DX12 中，资源（纹理、缓冲等）和"如何看待这个资源"是分开的：

- **ID3D12Resource** = 实际的 GPU 内存（画布本身）
- **RTV** = 告诉 GPU "这块内存是一个渲染目标，你可以往上面画东西"

为什么要这样设计？因为同一块 GPU 内存可能有多种用途：
- 作为渲染目标（画图）
- 作为着色器资源（采样纹理）
- 作为无序访问（Compute Shader 读写）

不同的用途需要不同的"视图"（View），这就是 DX12 的描述符系统。

---

## UE5 参考

UE5 的 DX12 封装比我们的复杂得多，但核心结构是一样的：

| 我们的代码 | UE5 对应 | 文件位置 |
|-----------|---------|---------|
| `D3D12CreateDevice()` | `FD3D12Device::Initialize()` | `Engine/Source/Runtime/D3D12RHI/Private/D3D12Device.cpp` |
| `CreateCommandQueue()` | `FD3D12CommandQueue::FD3D12CommandQueue()` | `Engine/Source/Runtime/D3D12RHI/Private/D3D12CommandQueue.cpp` |
| `CreateSwapChainForHwnd()` | `FD3D12Viewport::CreateSwapChain()` | `Engine/Source/Runtime/Windows/D3D12/D3D12Viewport.cpp` |
| `CreateRenderTargetView()` | `FD3D12RenderTargetView::Create()` | `Engine/Source/Runtime/D3D12RHI/Private/D3D12RenderTargetView.cpp` |
| `Fence` 机制 | `FD3D12Fence` | `Engine/Source/Runtime/D3D12RHI/Private/D3D12Fence.cpp` |

**UE5 额外做了什么**：

1. **多设备支持**：UE5 支持多 GPU（`GPUIndex`），每个 GPU 一个 `FD3D12Device`
2. **命令队列分类**：UE5 区分 Graphics / Compute / Copy / Async Compute 四种队列
3. **资源跟踪**：UE5 用 `FD3D12ResidencyManager` 管理显存，自动将不常用的资源移到系统内存
4. **描述符管理**：UE5 用 `FD3D12DescriptorCache` 缓存描述符，避免每帧重新创建

建议你在 UE5 源码中搜索以下关键词来理解专业引擎的 DX12 封装：

- `FD3D12Adapter` — 显卡适配器的封装
- `FD3D12Device` — GPU 设备的封装（对应我们的 DX12Device）
- `FD3D12CommandContext` — 命令上下文（封装了 Allocator + CommandList）
- `FD3D12DynamicRHI` — RHI（Render Hardware Interface）层，所有图形 API 的统一接口

---

## 前向链接：纹理类型 → SRV 视图维度（课 6 类型位的渲染侧消费）

课 6 的 `EValueType` 纹理族（`MCT_Texture2D/2DArray/Volume/...`）在渲染侧的消费就在本课的描述符系统：**每种纹理类型对应一种 SRV 的 ViewDimension**。本课先立映射表（描述符骨架代码里按它分派），课 15 的纹理 SRV 实际创建时直接查表：

| EValueType（课 6）| D3D12 SRV ViewDimension | HLSL 声明（课 6 ToHLSLType）| 采样坐标 |
|---|---|---|---|
| `MCT_Texture2D` | `D3D12_SRV_DIMENSION_TEXTURE2D` | `Texture2D` | float2 uv |
| `MCT_TextureCube` | `D3D12_SRV_DIMENSION_TEXTURECUBE` | `TextureCube` | float3 方向 |
| `MCT_Texture2DArray` | `D3D12_SRV_DIMENSION_TEXTURE2DARRAY` | `Texture2DArray` | float2 uv + layer |
| `MCT_VolumeTexture` | `D3D12_SRV_DIMENSION_TEXTURE3D` | `Texture3D` | float3 uvw |
| `MCT_TextureExternal` / `MCT_TextureVirtual` | 同 Texture2D 路径 | `Texture2D` | float2 uv（教学版简化路径，类型位对齐 UE）|

```cpp
// DX12Device/描述符辅助：由编译器类型位直接决定 SRV 形状（课 15 CreateShaderResourceView 用）
inline D3D12_SRV_DIMENSION GetSRVDimension(EValueType t) {
    switch (t) {
        case MCT_TextureCube:    return D3D12_SRV_DIMENSION_TEXTURECUBE;
        case MCT_Texture2DArray: return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        case MCT_VolumeTexture:  return D3D12_SRV_DIMENSION_TEXTURE3D;
        default:                 return D3D12_SRV_DIMENSION_TEXTURE2D;  // 2D/External/Virtual
    }
}
```

这就是"类型位有消费方"的完整闭环在渲染侧的一段：**编译器用类型位生成采样代码（课 6 ToHLSLType + 课 8 HLSL），渲染器用同一个类型位决定资源视图形状（本课表 + 课 15 SRV）**——两端读的是同一个标签，代码不会对不上。

---

## 完成标志

- [ ] DX12 设备创建成功，日志输出 "DX12 device initialized"
- [ ] 日志输出选择的显卡名称和显存大小
- [ ] 窗口显示蓝色背景（深蓝 0.1, 0.1, 0.3）
- [ ] 窗口 resize 后画面正常（不崩溃、不黑屏）
- [ ] 关闭程序时日志输出 "DX12Device::Destroy — 资源清理完成"
- [ ] 关闭程序不崩溃（资源正确释放）
- [ ] Debug 模式下，Visual Studio 输出窗口没有 D3D12 错误信息
- [ ] `GetSRVDimension` 辅助函数就位（EValueType 纹理族 → ViewDimension 映射，课 15 SRV 创建直接调用）
