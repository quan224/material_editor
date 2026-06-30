# 课17：3D 预览集成

## 目标

将 DX12 渲染器集成到 Qt 主窗口中，实现材质的实时 3D 预览：编译材质 -> 生成 HLSL -> 编译为 DX12 字节码 -> 创建 PSO -> 渲染预览物体。

---

## 背景知识

### 渲染流程

```
用户编辑节点图
    ↓
编译器生成 HLSL（课8的 HLSLGenerator）
    ↓
D3DCompile 编译 HLSL 为字节码（Shader::CompileFromSource）
    ↓
创建根签名 + PSO（Shader::CreateRootSignature + CreatePSO）
    ↓
在 DX12Widget 中渲染预览网格（Mesh::Draw + Camera::SetToCommandList）
    ↓
PBR 光照 → 交换链呈现
```

### DX12Widget — Qt 与 DX12 的桥接

DX12 渲染需要输出到一个窗口。Qt 的 QWidget 可以提供窗口句柄（HWND），DX12 通过 `CreateSwapChainForHwnd` 将渲染结果呈现到 Qt 控件上。

关键步骤：
1. DX12Widget 继承 QWidget，重写 `paintEvent` 触发渲染
2. 使用 `winId()` 获取 HWND
3. 创建 DX12 设备、命令队列、交换链
4. 每帧：开始命令列表 → 清除 RTV → 设置 PSO → 绘制 → 呈现

### MaterialPreview — 材质预览管理器

管理预览网格、着色器、光照参数。接收编译器输出的 HLSL 代码，动态创建 PSO 并渲染。

---

## 操作步骤

### 1. 创建文件

```
src/Renderer/Public/DX12Widget.h
src/Renderer/Private/DX12Widget.cpp
src/Renderer/Public/MaterialPreview.h
src/Renderer/Private/MaterialPreview.cpp
src/UI/Private/ViewportPanel.h
src/UI/Private/ViewportPanel.cpp
```

### 2. DX12Widget.h

```cpp
#pragma once
#include <QWidget>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>
#include <functional>  // std::function（RenderCallback）
#include "Core/Public/MathTypes.h"

using Microsoft::WRL::ComPtr;

// DX12 渲染视口 — 封装 DX12 设备和交换链
class DX12Widget : public QWidget {
    Q_OBJECT
public:
    explicit DX12Widget(QWidget* parent = nullptr);
    ~DX12Widget();

    // 初始化 DX12 设备和交换链
    bool Initialize();

    // 设置渲染回调：paintEvent 会调 RenderFrame()，
    // RenderFrame() 默认实现就是调用这个回调（如果设置了）
    // 注意：不要把 RenderFrame 改成 Qt signal——它必须在 paintEvent 内同步执行
    using RenderCallback = std::function<void()>;
    void SetRenderCallback(RenderCallback cb) { renderCallback_ = std::move(cb); }

    // 每帧渲染（paintEvent 调用；子类可重写，或用 SetRenderCallback 注入逻辑）
    virtual void RenderFrame() {
        if (renderCallback_) renderCallback_();
    }

    // 获取当前帧后缓冲（ResourceBarrier 等场景必须传入真实资源指针，
    // nullptr 会让 DX12 调试层报错甚至 GPU 崩溃）
    ID3D12Resource* GetCurrentBackBuffer() const {
        return backBuffers_[currentFrameIndex_].Get();
    }

    // 获取设备（供外部创建资源用）
    ID3D12Device* GetDevice() const { return device_.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return cmdList_.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return cmdQueue_.Get(); }

    // 获取当前 RTV 描述句柄
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const;

    // 获取 DSV 描述句柄
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const;

    // 获取深度缓冲格式
    DXGI_FORMAT GetDSVFormat() const { return DXGI_FORMAT_D24_UNORM_S8_UINT; }
    DXGI_FORMAT GetRTVFormat() const { return DXGI_FORMAT_R8G8B8A8_UNORM; }

    // 命令提交和同步
    void ExecuteCommandList();
    void WaitForGPU();

    // 呈现
    void Present();

    // 是否已初始化
    bool IsInitialized() const { return initialized_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool CreateDevice();
    bool CreateCommandObjects();
    bool CreateSwapChain();
    bool CreateRTV();
    bool CreateDSV();
    void UpdateRenderTargetSize();

    RenderCallback renderCallback_;  // 外部注入的渲染逻辑（ViewPanel 设置）

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> cmdQueue_;
    ComPtr<ID3D12CommandAllocator> cmdAllocator_;
    ComPtr<ID3D12GraphicsCommandList> cmdList_;

    ComPtr<IDXGISwapChain3> swapChain_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    std::vector<ComPtr<ID3D12Resource>> renderTargets_;
    ComPtr<ID3D12Resource> depthStencil_;

    UINT rtvDescriptorSize_ = 0;
    UINT currentFrameIndex_ = 0;
    int width_ = 800;
    int height_ = 600;

    // GPU 同步
    ComPtr<ID3D12Fence> fence_;
    UINT64 fenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;

    bool initialized_ = false;
};
```

### 3. DX12Widget.cpp

```cpp
#include "Renderer/Public/DX12Widget.h"
#include "Core/Public/Logger.h"
#include <d3dx12.h>

DX12Widget::DX12Widget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(300, 200);
    setAttribute(Qt::WA_NativeWindow);         // 确保使用原生窗口
    setAttribute(Qt::WA_PaintOnScreen);         // DX12 自己管理绘制
    setAttribute(Qt::WA_NoSystemBackground);    // 无背景（DX12 负责清除）
}

DX12Widget::~DX12Widget() {
    if (fenceEvent_) CloseHandle(fenceEvent_);

    // 确保 GPU 完成所有工作后再释放资源
    if (device_ && cmdQueue_) {
        WaitForGPU();
    }
}

bool DX12Widget::Initialize() {
    if (initialized_) return true;

    if (!CreateDevice()) return false;
    if (!CreateCommandObjects()) return false;
    if (!CreateSwapChain()) return false;
    if (!CreateRTV()) return false;
    if (!CreateDSV()) return false;

    // 创建 Fence 用于 GPU 同步
    device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                         IID_PPV_ARGS(&fence_));
    fenceEvent_ = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

    initialized_ = true;
    ME_LOG_INFO("DX12Widget initialized successfully");
    return true;
}

bool DX12Widget::CreateDevice() {
    // 启用调试层（Debug 模式）
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
    }
#endif

    // 创建设备
    HRESULT hr = D3D12CreateDevice(
        nullptr, D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device_));

    if (FAILED(hr)) {
        ME_LOG_ERROR("Failed to create D3D12 device");
        return false;
    }
    return true;
}

bool DX12Widget::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue_));

    device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmdAllocator_));

    device_->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        cmdAllocator_.Get(), nullptr,
        IID_PPV_ARGS(&cmdList_));

    // 命令列表创建时处于打开状态，先关闭
    cmdList_->Close();

    return true;
}

bool DX12Widget::CreateSwapChain() {
    ComPtr<IDXGIFactory4> factory;
    CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.BufferCount = 2;
    scDesc.Width = width_;
    scDesc.Height = height_;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc = { 1, 0 };

    ComPtr<IDXGISwapChain1> swapChain1;
    factory->CreateSwapChainForHwnd(
        cmdQueue_.Get(),
        (HWND)winId(),     // Qt 窗口句柄
        &scDesc,
        nullptr, nullptr,
        &swapChain1);

    swapChain1.As(&swapChain_);
    currentFrameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    return true;
}

bool DX12Widget::CreateRTV() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_));

    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    renderTargets_.resize(2);
    for (UINT i = 0; i < 2; i++) {
        swapChain_->GetBuffer(i, IID_PPV_ARGS(&renderTargets_[i]));
        device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                rtvHeap_->GetCPUDescriptorHandleForHeapStart(), i, rtvDescriptorSize_));
    }
    return true;
}

bool DX12Widget::CreateDSV() {
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_));

    auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D24_UNORM_S8_UINT, width_, height_,
        1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearVal.DepthStencil.Depth = 1.0f;
    clearVal.DepthStencil.Stencil = 0;

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    device_->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearVal, IID_PPV_ARGS(&depthStencil_));

    device_->CreateDepthStencilView(depthStencil_.Get(), nullptr,
        dsvHeap_->GetCPUDescriptorHandleForHeapStart());

    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Widget::GetCurrentRTV() const {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart(),
        currentFrameIndex_, rtvDescriptorSize_);
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Widget::GetDSV() const {
    return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
}

void DX12Widget::ExecuteCommandList() {
    cmdAllocator_->Reset();
    cmdList_->Reset(cmdAllocator_.Get(), nullptr);
}

void DX12Widget::Present() {
    // 将渲染目标从渲染状态转为呈现状态
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTargets_[currentFrameIndex_].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    cmdList_->ResourceBarrier(1, &barrier);

    cmdList_->Close();
    ID3D12CommandList* lists[] = { cmdList_.Get() };
    cmdQueue_->ExecuteCommandLists(1, lists);

    swapChain_->Present(1, 0);  // VSync
    WaitForGPU();

    currentFrameIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void DX12Widget::WaitForGPU() {
    fenceValue_++;
    cmdQueue_->Signal(fence_.Get(), fenceValue_);
    if (fence_->GetCompletedValue() < fenceValue_) {
        fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void DX12Widget::paintEvent(QPaintEvent* event) {
    if (!initialized_) return;

    // 子类或外部通过 RenderFrame() 来填充渲染逻辑
    // 这里只是触发渲染的入口
    RenderFrame();

    // 注意：不要在这里调用 update()！
    // paintEvent 内调 update() 会立即再排一次 paintEvent，造成 100% CPU 死循环。
    // 持续重绘的正确做法是用 QTimer（课14 已演示：renderTimer.start(16)），
    // 或者只在场景变化时（相机移动、参数改动）才 update()。
}

void DX12Widget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    if (!initialized_) return;

    // 窗口大小变化时需要重建渲染目标
    WaitForGPU();

    width_ = width();
    height_ = height();

    // 释放旧的渲染目标
    for (auto& rt : renderTargets_) rt.Reset();
    depthStencil_.Reset();

    // 调整交换链大小
    swapChain_->ResizeBuffers(2, width_, height_,
                              DXGI_FORMAT_R8G8B8A8_UNORM, 0);

    CreateRTV();
    CreateDSV();

    currentFrameIndex_ = swapChain_->GetCurrentBackBufferIndex();
}
```

### 4. MaterialPreview.h

```cpp
#pragma once
#include "Renderer/Public/Shader.h"
#include "Renderer/Public/Mesh.h"
#include "Renderer/Public/Camera.h"
#include "Core/Public/MathTypes.h"
#include <memory>
#include <string>
#include <map>

class DX12Widget;

class MaterialPreview {
public:
    MaterialPreview();
    ~MaterialPreview();

    // 初始化（需要 DX12 设备）
    bool Initialize(ID3D12Device* device,
                    ID3D12GraphicsCommandList* cmdList,
                    ID3D12Resource* uploadBuffer,
                    size_t& uploadOffset);

    // 渲染一帧（由 DX12Widget 调用）
    void Render(ID3D12GraphicsCommandList* cmdList,
                D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                D3D12_CPU_DESCRIPTOR_HANDLE dsv,
                int width, int height);

    // 设置材质着色器（从编译器输出的 HLSL）
    bool SetMaterialShader(ID3D12Device* device,
                           const std::string& vertexShaderSource,
                           const std::string& pixelShaderSource);

    // 切换预览网格
    void SetMesh(ID3D12Device* device,
                 ID3D12GraphicsCommandList* cmdList,
                 ID3D12Resource* uploadBuffer,
                 size_t& uploadOffset,
                 const std::string& type);  // "sphere", "cube", "plane", "cylinder"

    // 相机控制
    Camera& GetCamera() { return camera_; }

    // 设置光源方向
    void SetLightDirection(const Vec3& dir) { lightDir_ = dir; }

    // 错误信息
    const std::string& GetLastError() const { return lastError_; }

private:
    Shader shader_;
    Mesh currentMesh_;
    Camera camera_;
    Vec3 lightDir_ = Vec3(-1, -1, -1);

    // 固定的顶点着色器源码
    std::string vertexShaderSrc_;

    bool initialized_ = false;
    std::string lastError_;
};
```

### 5. MaterialPreview.cpp

```cpp
#include "Renderer/Public/MaterialPreview.h"
#include "Core/Public/Logger.h"

MaterialPreview::MaterialPreview() {
    // 固定的顶点着色器（HLSL）
    vertexShaderSrc_ = R"(
struct VS_INPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

struct VS_OUTPUT {
    float4 pos       : SV_POSITION;
    float3 worldPos  : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 uv        : TEXCOORD2;
};

cbuffer SceneConstants : register(b0) {
    float4x4 viewProj;
    float4x4 model;
    float4 cameraPos;
    float4 lightDir;
    float4 lightColor;
    float  time;
    float3 _pad;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.worldPos = worldPos.xyz;
    output.normal = mul((float3x3)model, input.normal);
    output.uv = input.uv;
    output.pos = mul(viewProj, worldPos);
    return output;
}
)";

    // 默认球体
    currentMesh_ = Mesh::CreateSphere();
}

MaterialPreview::~MaterialPreview() {
    currentMesh_.Destroy();
    shader_.Destroy();
}

bool MaterialPreview::Initialize(ID3D12Device* device,
                                  ID3D12GraphicsCommandList* cmdList,
                                  ID3D12Resource* uploadBuffer,
                                  size_t& uploadOffset) {
    if (initialized_) return true;

    // 创建相机常量缓冲
    camera_.CreateConstantBuffer(device);

    // 上传网格数据到 GPU
    currentMesh_.Upload(device, cmdList, uploadBuffer, uploadOffset);

    initialized_ = true;
    return true;
}

void MaterialPreview::Render(ID3D12GraphicsCommandList* cmdList,
                              D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                              D3D12_CPU_DESCRIPTOR_HANDLE dsv,
                              int width, int height) {
    if (!initialized_ || width <= 0 || height <= 0) return;

    // 清除渲染目标（深灰色背景）
    float clearColor[] = { 0.15f, 0.15f, 0.15f, 1.0f };
    cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // 设置渲染目标
    cmdList->OMSetRenderTargets(1, &rtv, TRUE, &dsv);

    // 设置视口和裁剪矩形
    D3D12_VIEWPORT viewport = { 0, 0, (float)width, (float)height, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, width, height };
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissorRect);

    // 使用材质着色器
    if (!shader_.IsReady()) return;

    shader_.SetToCommandList(cmdList);

    // 更新相机常量缓冲
    float aspect = (float)width / (float)height;
    camera_.UpdateConstantBuffer(aspect);
    camera_.SetToCommandList(cmdList);

    // 绘制网格
    currentMesh_.Draw(cmdList);
}

bool MaterialPreview::SetMaterialShader(ID3D12Device* device,
                                         const std::string& vertexShaderSource,
                                         const std::string& pixelShaderSource) {
    // 编译顶点着色器
    auto vs = Shader::CompileFromSource(
        vertexShaderSource.empty() ? vertexShaderSrc_ : vertexShaderSource,
        "main", "vs_5_0", "preview_vs.hlsl");
    if (!vs.IsValid()) {
        lastError_ = "VS compile error: " + vs.errorMessage;
        ME_LOG_ERROR("%s", lastError_.c_str());
        return false;
    }

    // 编译像素着色器
    auto ps = Shader::CompileFromSource(
        pixelShaderSource,
        "main", "ps_5_0", "preview_ps.hlsl");
    if (!ps.IsValid()) {
        lastError_ = "PS compile error: " + ps.errorMessage;
        ME_LOG_ERROR("%s", lastError_.c_str());
        return false;
    }

    // 销毁旧 PSO
    shader_.Destroy();

    // 创建根签名
    if (!shader_.CreateRootSignature(device, true)) {
        lastError_ = "Failed to create root signature";
        return false;
    }

    // 创建 PSO
    auto inputLayout = Mesh::GetInputLayout();
    if (!shader_.CreatePSO(device, vs, ps, inputLayout)) {
        lastError_ = "Failed to create PSO";
        return false;
    }

    ME_LOG_INFO("Material shader compiled and PSO created successfully");
    return true;
}

void MaterialPreview::SetMesh(ID3D12Device* device,
                               ID3D12GraphicsCommandList* cmdList,
                               ID3D12Resource* uploadBuffer,
                               size_t& uploadOffset,
                               const std::string& type) {
    // 销毁旧网格
    currentMesh_.Destroy();

    if (type == "sphere") {
        currentMesh_ = Mesh::CreateSphere();
    } else if (type == "cube") {
        currentMesh_ = Mesh::CreateCube();
    } else if (type == "plane") {
        currentMesh_ = Mesh::CreatePlane();
    } else if (type == "cylinder") {
        currentMesh_ = Mesh::CreateCylinder();
    } else {
        currentMesh_ = Mesh::CreateSphere();
    }

    currentMesh_.Upload(device, cmdList, uploadBuffer, uploadOffset);
}
```

### 6. ViewportPanel.h

```cpp
#pragma once
#include <QDockWidget>
#include <QPoint>
#include "Renderer/Public/DX12Widget.h"
#include "Renderer/Public/MaterialPreview.h"
#include "Compiler/Public/MaterialCompiler.h"

// ViewportPanel — QDockWidget 包装 DX12Widget + 鼠标事件 → 相机控制
class ViewportPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit ViewportPanel(const QString& title, QWidget* parent = nullptr);
    ~ViewportPanel();

    // 初始化 DX12 渲染（需要 Qt 窗口已创建）
    bool InitializeDX12();

    // 设置材质编译结果
    void SetMaterialResult(const MaterialCompiler::CompileResult& result);

    // 切换预览网格
    void SetPreviewMesh(const std::string& type);

    // 获取内部 DX12Widget（供布局用）
    DX12Widget* GetDX12Widget() const { return dx12Widget_; }

private slots:
    void OnRenderFrame();

protected:
    // 鼠标事件 → 相机控制
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    DX12Widget* dx12Widget_;
    MaterialPreview preview_;
    QPoint lastMousePos_;
    bool dx12Initialized_ = false;

    // 上传堆（用于网格上传）
    ComPtr<ID3D12Resource> uploadBuffer_;
    size_t uploadOffset_ = 0;
};
```

### 7. ViewportPanel.cpp

```cpp
#include "UI/Private/ViewportPanel.h"
#include "Core/Public/Logger.h"
#include <d3dx12.h>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>

ViewportPanel::ViewportPanel(const QString& title, QWidget* parent)
    : QDockWidget(title, parent) {
    dx12Widget_ = new DX12Widget(this);
    setWidget(dx12Widget_);
    setMinimumSize(300, 200);
}

ViewportPanel::~ViewportPanel() {
    if (dx12Initialized_) {
        dx12Widget_->WaitForGPU();
    }
}

bool ViewportPanel::InitializeDX12() {
    if (dx12Initialized_) return true;

    if (!dx12Widget_->Initialize()) {
        ME_LOG_ERROR("Failed to initialize DX12Widget");
        return false;
    }

    // 创建上传堆（256MB，用于网格数据上传）
    auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(256 * 1024 * 1024);
    dx12Widget_->GetDevice()->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE,
        &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&uploadBuffer_));

    // 初始化预览管理器
    uploadOffset_ = 0;
    preview_.Initialize(dx12Widget_->GetDevice(),
                        dx12Widget_->GetCommandList(),
                        uploadBuffer_.Get(), uploadOffset_);

    dx12Initialized_ = true;

    // 注意：RenderFrame 是 DX12Widget 的虚函数（不是 Qt 信号），
    // 不能用 connect。改为：让 DX12Widget::paintEvent 直接调用
    // OnRenderCallback_（std::function 回调），由 ViewportPanel 设置。
    // 这里通过 SetRenderCallback 注入：
    dx12Widget_->SetRenderCallback([this]() { OnRenderFrame(); });

    return true;
}

void ViewportPanel::OnRenderFrame() {
    if (!dx12Initialized_) return;

    auto* device = dx12Widget_->GetDevice();

    // 开始记录命令
    dx12Widget_->ExecuteCommandList();

    ID3D12GraphicsCommandList* cmdList = dx12Widget_->GetCommandList();

    // 将渲染目标转为可写状态
    // 注意：必须传入真实的后缓冲资源指针——nullptr 会让 DX12 调试层报错，
    // 甚至 GPU 崩溃。DX12Widget 提供 GetCurrentBackBuffer() 获取当前帧资源
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        dx12Widget_->GetCurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    // 渲染预览
    preview_.Render(cmdList,
                    dx12Widget_->GetCurrentRTV(),
                    dx12Widget_->GetDSV(),
                    dx12Widget_->width(),
                    dx12Widget_->height());

    // 提交并呈现
    dx12Widget_->Present();
}

void ViewportPanel::SetMaterialResult(
    const MaterialCompiler::CompileResult& result) {
    if (!dx12Initialized_ || !result.success) return;

    // result.hlslCode 包含像素着色器（课8生成的完整 HLSL）
    preview_.SetMaterialShader(dx12Widget_->GetDevice(),
                               "",  // 使用默认顶点着色器
                               result.hlslCode);
    dx12Widget_->update();
}

void ViewportPanel::SetPreviewMesh(const std::string& type) {
    if (!dx12Initialized_) return;

    // 需要命令列表来上传新网格
    dx12Widget_->WaitForGPU();
    dx12Widget_->ExecuteCommandList();

    uploadOffset_ = 0;
    preview_.SetMesh(dx12Widget_->GetDevice(),
                     dx12Widget_->GetCommandList(),
                     uploadBuffer_.Get(), uploadOffset_,
                     type);

    dx12Widget_->Present();
    dx12Widget_->update();
}

// === 鼠标事件 ===

void ViewportPanel::mousePressEvent(QMouseEvent* event) {
    lastMousePos_ = event->pos();
}

void ViewportPanel::mouseMoveEvent(QMouseEvent* event) {
    QPoint delta = event->pos() - lastMousePos_;
    lastMousePos_ = event->pos();

    if (event->buttons() & Qt::LeftButton) {
        // 左键拖拽 → 轨道旋转
        preview_.GetCamera().Orbit(delta.x() * 0.5f, delta.y() * 0.5f);
        dx12Widget_->update();
    } else if (event->buttons() & Qt::MiddleButton) {
        // 中键拖拽 → 平移
        preview_.GetCamera().Pan(delta.x(), delta.y());
        dx12Widget_->update();
    } else if (event->buttons() & Qt::RightButton) {
        // 右键拖拽 → 缩放
        preview_.GetCamera().Zoom(-delta.y() * 0.05f);
        dx12Widget_->update();
    }
}

void ViewportPanel::mouseReleaseEvent(QMouseEvent* event) {
    // 无特殊处理
}

void ViewportPanel::wheelEvent(QWheelEvent* event) {
    float delta = event->angleDelta().y() / 120.0f;
    preview_.GetCamera().Zoom(delta);
    dx12Widget_->update();
}
```

### 8. 集成到 MainWindow

修改 `MainWindow.cpp`：

```cpp
#include "UI/Private/ViewportPanel.h"

// 在 SetupDockWidgets() 中
viewportPanel_ = new ViewportPanel("Viewport", this);
addDockWidget(Qt::BottomDockWidgetArea, viewportPanel_);

// 窗口显示后初始化 DX12（需要 HWND 已创建）
// 使用 QTimer::singleShot 在事件循环开始后初始化
QTimer::singleShot(0, this, [this]() {
    if (viewportPanel_) {
        viewportPanel_->InitializeDX12();
    }
});

// 编译成功后更新 3D 预览
void MainWindow::OnCompile() {
    auto result = compiler_->Compile(graph_);

    if (result.success) {
        compileStatus_->setText("Compiled OK");
        compileStatus_->setStyleSheet("color: green;");
        statusLabel_->setText(QString("Compiled: %1 bytes HLSL")
            .arg(result.hlslCode.size()));

        // 更新代码预览
        if (codePanel_) codePanel_->SetCode(result.hlslCode);

        // 更新 3D 预览
        if (viewportPanel_) viewportPanel_->SetMaterialResult(result);
    } else {
        compileStatus_->setText("Compile Error");
        compileStatus_->setStyleSheet("color: red;");
        statusLabel_->setText(QString("Error: %1").arg(
            QString::fromStdString(result.errorMessage)));

        if (codePanel_) codePanel_->SetError(result.errorMessage);
    }
}

// 预览网格切换
connect(previewMeshCombo_, &QComboBox::currentTextChanged,
        this, [this](const QString& text) {
    if (viewportPanel_) {
        viewportPanel_->SetPreviewMesh(text.toLower().toStdString());
    }
});
```

### 9. 更新 CMakeLists.txt

```cmake
if(WIN32)
    target_link_libraries(MaterialEditor PRIVATE
        d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib
    )
    target_compile_definitions(MaterialEditor PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN
    )
endif()
```

---

## 验证

1. 运行程序
2. 右键添加 Constant3Vector 节点，设置 R=1, G=0, B=0（红色）
3. 连接到输出节点的 BaseColor
4. 自动编译
5. 底部 Viewport 显示红色球体
6. 左键拖拽 → 旋转相机
7. 滚轮 → 缩放
8. 切换预览网格为 Cube → 显示红色立方体
9. 修改参数 → 实时更新预览

### PBR 验证

创建一个金属材质测试：
1. Constant3Vector(0.8, 0.6, 0.2) → BaseColor（金色）
2. Constant(0.9) → Metallic
3. Constant(0.3) → Roughness
4. 预览应该显示一个有金属光泽的球体

---

## 常见问题

### 问题1：着色器编译失败 — HLSL 语法错误

检查课8的 HLSLGenerator 输出。DX12 的 HLSL 与 GLSL 差异：
- `float3` 而非 `vec3`
- `lerp` 而非 `mix`
- `cbuffer` 而非 `uniform`
- `SV_POSITION` / `SV_Target` 语义
- 入口函数签名：`float4 main(PS_INPUT input) : SV_Target`

### 问题2：DX12Widget 黑屏

- 确认 `InitializeDX12()` 在窗口显示后被调用（HWND 需要有效）
- 确认 DX12 调试层没有报错（检查 Visual Studio 输出窗口）
- 确认 PSO 创建成功

### 问题3：预览不更新

- 确认 `dx12Widget_->update()` 被调用
- 确认命令列表正确提交和呈现
- 检查 GPU 同步（fence）是否正常工作

### 问题4：PSO 创建失败

- 确认顶点着色器的输入布局与 Mesh 的顶点格式匹配
- 确认 RTV 格式与交换链格式一致
- 确认根签名在 PSO 之前创建

---

## UE5 参考

- `E:\UE5\Engine\Source\Editor\MaterialEditor\Private\MaterialEditorPreview.cpp` — 预览渲染
- 搜索 `UpdateMaterialPreview` — 预览更新逻辑
- `E:\UE5\Engine\Source\Runtime\Engine\Private\MaterialEditorRender.cpp` — 预览渲染实现
- `E:\UE5\Engine\Source\Runtime\Engine\Shaders\Private\BasePassPixelShader.ush` — PBR 着色
- `E:\UE5\Engine\Source\Runtime\RHI\D3D12\` — UE5 的 DX12 RHI 实现

---

## 完成标志

- [ ] DX12Widget 成功创建 DX12 设备和交换链
- [ ] ViewportPanel 显示预览球体
- [ ] 编译材质后预览实时更新（PSO 重建）
- [ ] 轨道相机控制（旋转/平移/缩放）
- [ ] PBR 光照效果正确（金属/非金属区分明显）
- [ ] 预览网格切换（球体/立方体/平面/圆柱）
- [ ] 深度测试正确（物体遮挡关系正确）
- [ ] 窗口大小变化时正确重建渲染目标
