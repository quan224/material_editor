# 课16：DX12 渲染器封装 — Shader、Mesh、Camera

## 目标

封装 DirectX 12 渲染基础设施：着色器编译与根签名创建、顶点/索引缓冲管理、轨道相机控制，通过常量缓冲将矩阵传递给 GPU。

---

## 背景知识

### UE5 的材质预览

UE5 材质编辑器中的 3D 预览是一个独立的渲染视口，使用简化版渲染管线：
- 预览网格（球体/立方体/平面/圆柱）
- 单方向光 + 简单环境光
- PBR 着色（Cook-Torrance BRDF）
- 轨道相机（旋转/平移/缩放）

在课14/15中你已经掌握了 DX12 基础：设备创建、命令队列、交换链、RTV、上传堆。本课将这些知识封装为可复用的类。

### DX12 渲染管线回顾

```
HLSL 源码 → D3DCompile 编译为字节码
                              ↓
              根签名（Root Signature）描述着色器资源布局
                              ↓
              PSO（Pipeline State Object）打包全部渲染状态
                              ↓
顶点数据 → 上传堆 → 默认堆 → IA（输入装配器）→ VS → 光栅化 → PS → RTV
```

每个关键步骤对应的类：
- `Shader` — 编译 HLSL 字节码、创建根签名、创建 PSO
- `Mesh` — 管理顶点/索引缓冲（基于课15学到的上传堆知识）
- `Camera` — 生成视图/投影矩阵，通过常量缓冲传给 GPU

---

## 操作步骤

### 1. 创建文件

```
src/Renderer/Public/Shader.h
src/Renderer/Private/Shader.cpp
src/Renderer/Public/Mesh.h
src/Renderer/Private/Mesh.cpp
src/Renderer/Public/Camera.h
src/Renderer/Private/Camera.cpp
```

### 2. Shader.h

```cpp
#pragma once
#include <d3d12.h>
#include <d3dcommon.h>   // ID3DBlob（虽然 d3d12.h 间接包含，但显式包含更稳妥）
#include <wrl/client.h>
#include <string>
#include <vector>
#include "Core/Public/MathTypes.h"

using Microsoft::WRL::ComPtr;

// 着色器阶段
enum class EShaderStage {
    Vertex,
    Pixel
};

// 着色器编译结果
struct ShaderBytecode {
    ComPtr<ID3DBlob> bytecode;
    std::string errorMessage;   // 编译错误信息（成功时为空）
    bool IsValid() const { return bytecode != nullptr; }
};

class Shader {
public:
    Shader() = default;
    ~Shader();

    // 从 HLSL 源码编译着色器
    static ShaderBytecode CompileFromSource(
        const std::string& source,
        const std::string& entryPoint,
        const std::string& target,     // "vs_5_0" / "ps_5_0"
        const std::string& debugName = "");

    // 创建根签名（描述常量缓冲/纹理等资源布局）
    bool CreateRootSignature(
        ID3D12Device* device,
        bool useConstantBuffer = true);  // 是否包含 CBV

    // 创建管线状态对象（PSO）
    bool CreatePSO(
        ID3D12Device* device,
        const ShaderBytecode& vs,
        const ShaderBytecode& ps,
        const D3D12_INPUT_LAYOUT_DESC& inputLayout,
        DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
        bool wireframe = false);

    // 设置到命令列表
    void SetToCommandList(ID3D12GraphicsCommandList* cmdList);

    // 资源释放
    void Destroy();

    // 状态查询
    bool IsReady() const { return rootSignature_ != nullptr && pso_ != nullptr; }
    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }
    ID3D12PipelineState* GetPSO() const { return pso_.Get(); }

private:
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pso_;
};
```

### 3. Shader.cpp

```cpp
#include "Renderer/Public/Shader.h"
#include "Core/Public/Logger.h"
#include <d3dcompiler.h>

Shader::~Shader() {
    Destroy();
}

void Shader::Destroy() {
    pso_.Reset();
    rootSignature_.Reset();
}

ShaderBytecode Shader::CompileFromSource(
    const std::string& source,
    const std::string& entryPoint,
    const std::string& target,
    const std::string& debugName)
{
    ShaderBytecode result;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(
        source.c_str(),
        source.size(),
        debugName.empty() ? "shader.hlsl" : debugName.c_str(),
        nullptr,                    // defines
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(),
        target.c_str(),
        flags,
        0,
        &result.bytecode,
        &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob) {
            result.errorMessage = static_cast<char*>(errorBlob->GetBufferPointer());
            ME_LOG_ERROR("Shader compile error: %s", result.errorMessage.c_str());
        }
        return result;
    }

    return result;
}

bool Shader::CreateRootSignature(ID3D12Device* device, bool useConstantBuffer) {
    // 根参数：b0 寄存器的 CBV（用于视图/投影/光照矩阵）
    D3D12_ROOT_PARAMETER rootParams[1] = {};

    if (useConstantBuffer) {
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParams[0].Descriptor.RegisterSpace = 0;
        rootParams[0].Descriptor.ShaderRegister = 0;
    }

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = useConstantBuffer ? 1 : 0;
    rootSigDesc.pParameters = useConstantBuffer ? rootParams : nullptr;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);

    if (FAILED(hr)) {
        if (error) {
            ME_LOG_ERROR("Root signature serialize error: %s",
                         static_cast<char*>(error->GetBufferPointer()));
        }
        return false;
    }

    hr = device->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));

    if (FAILED(hr)) {
        ME_LOG_ERROR("Failed to create root signature");
        return false;
    }

    return true;
}

bool Shader::CreatePSO(
    ID3D12Device* device,
    const ShaderBytecode& vs,
    const ShaderBytecode& ps,
    const D3D12_INPUT_LAYOUT_DESC& inputLayout,
    DXGI_FORMAT rtvFormat,
    bool wireframe)
{
    if (!rootSignature_) {
        ME_LOG_ERROR("Root signature must be created before PSO");
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS = { vs.bytecode->GetBufferPointer(), vs.bytecode->GetBufferSize() };
    psoDesc.PS = { ps.bytecode->GetBufferPointer(), ps.bytecode->GetBufferSize() };
    psoDesc.InputLayout = inputLayout;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.FillMode = wireframe ?
        D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = rtvFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc = { 1, 0 };

    HRESULT hr = device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pso_));

    if (FAILED(hr)) {
        ME_LOG_ERROR("Failed to create PSO");
        return false;
    }

    return true;
}

void Shader::SetToCommandList(ID3D12GraphicsCommandList* cmdList) {
    cmdList->SetGraphicsRootSignature(rootSignature_.Get());
    cmdList->SetPipelineState(pso_.Get());
}
```

### 4. Mesh.h

```cpp
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include "Core/Public/MathTypes.h"

using Microsoft::WRL::ComPtr;

class Mesh {
public:
    struct Vertex {
        Vec3 position;
        Vec3 normal;
        Vec2 texCoord;
    };

    Mesh() = default;
    ~Mesh();

    // 预制几何体
    static Mesh CreateSphere(float radius = 1.0f, int segments = 32, int rings = 16);
    static Mesh CreatePlane(float size = 2.0f);
    static Mesh CreateCube(float size = 1.0f);
    static Mesh CreateCylinder(float radius = 0.5f, float height = 2.0f, int segments = 32);

    // 上传到 GPU（使用上传堆 → 默认堆的方式）
    bool Upload(ID3D12Device* device,
                ID3D12GraphicsCommandList* cmdList,
                ID3D12Resource* uploadBuffer,
                size_t& uploadOffset);

    // 在命令列表中设置顶点/索引缓冲并绘制
    void Draw(ID3D12GraphicsCommandList* cmdList) const;

    // 资源释放
    void Destroy();

    // 状态查询
    bool IsUploaded() const { return vertexBuffer_ != nullptr; }
    uint32_t GetIndexCount() const { return (uint32_t)indices_.size(); }

    // 获取顶点数据（CPU 端，供上传用）
    const std::vector<Vertex>& GetVertices() const { return vertices_; }
    const std::vector<uint32_t>& GetIndices() const { return indices_; }

    // 获取顶点输入布局描述
    static D3D12_INPUT_LAYOUT_DESC GetInputLayout();

private:
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;

    // GPU 资源
    ComPtr<ID3D12Resource> vertexBuffer_;
    ComPtr<ID3D12Resource> indexBuffer_;

    // 缓冲视图
    D3D12_VERTEX_BUFFER_VIEW vbView_ = {};
    D3D12_INDEX_BUFFER_VIEW ibView_ = {};
};
```

### 5. Mesh.cpp

```cpp
#include "Renderer/Public/Mesh.h"
#include "Core/Public/Logger.h"
#include <d3dx12.h>
#include <cmath>

Mesh::~Mesh() {
    Destroy();
}

// DX12 顶点输入布局（对应 HLSL 中的 VS 输入结构体）
static D3D12_INPUT_ELEMENT_DESC s_InputElements[] = {
    // SemanticName, SemanticIndex, Format, InputSlot, AlignedByteOffset, InputSlotClass, InstanceDataStepRate
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

D3D12_INPUT_LAYOUT_DESC Mesh::GetInputLayout() {
    return { s_InputElements, _countof(s_InputElements) };
}

bool Mesh::Upload(ID3D12Device* device,
                  ID3D12GraphicsCommandList* cmdList,
                  ID3D12Resource* uploadBuffer,
                  size_t& uploadOffset) {
    if (vertexBuffer_) return true;  // 已上传

    UINT vbSize = (UINT)(vertices_.size() * sizeof(Vertex));
    UINT ibSize = (UINT)(indices_.size() * sizeof(uint32_t));

    // --- 顶点缓冲：上传堆 → 默认堆 ---
    auto vbHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
    device->CreateCommittedResource(
        &vbHeapProps, D3D12_HEAP_FLAG_NONE,
        &vbDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&vertexBuffer_));

    // 从上传堆复制顶点数据
    BYTE* uploadData = nullptr;
    uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&uploadData));
    memcpy(uploadData + uploadOffset, vertices_.data(), vbSize);
    uploadBuffer->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(vertexBuffer_.Get(), 0,
                              uploadBuffer, uploadOffset, vbSize);

    // 转换为像素着色器可读状态
    auto vbBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        vertexBuffer_.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    cmdList->ResourceBarrier(1, &vbBarrier);

    uploadOffset += vbSize;

    // --- 索引缓冲 ---
    auto ibHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
    device->CreateCommittedResource(
        &ibHeapProps, D3D12_HEAP_FLAG_NONE,
        &ibDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&indexBuffer_));

    uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&uploadData));
    memcpy(uploadData + uploadOffset, indices_.data(), ibSize);
    uploadBuffer->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(indexBuffer_.Get(), 0,
                              uploadBuffer, uploadOffset, ibSize);

    auto ibBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        indexBuffer_.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_INDEX_BUFFER);
    cmdList->ResourceBarrier(1, &ibBarrier);

    uploadOffset += ibSize;

    // --- 填充视图 ---
    vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbView_.SizeInBytes = vbSize;
    vbView_.StrideInBytes = sizeof(Vertex);

    ibView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    ibView_.SizeInBytes = ibSize;
    ibView_.Format = DXGI_FORMAT_R32_UINT;

    ME_LOG_INFO("Mesh uploaded: %zu vertices, %zu indices",
                vertices_.size(), indices_.size());
    return true;
}

void Mesh::Draw(ID3D12GraphicsCommandList* cmdList) const {
    if (!vertexBuffer_) return;

    cmdList->IASetVertexBuffers(0, 1, &vbView_);
    cmdList->IASetIndexBuffer(&ibView_);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawIndexedInstanced((UINT)indices_.size(), 1, 0, 0, 0);
}

void Mesh::Destroy() {
    vertexBuffer_.Reset();
    indexBuffer_.Reset();
    vbView_ = {};
    ibView_ = {};
}

// === 预制几何体 ===

Mesh Mesh::CreateSphere(float radius, int segments, int rings) {
    Mesh mesh;

    for (int ring = 0; ring <= rings; ring++) {
        float phi = 3.14159265f * ring / rings;
        for (int seg = 0; seg <= segments; seg++) {
            float theta = 2.0f * 3.14159265f * seg / segments;

            float x = radius * std::sin(phi) * std::cos(theta);
            float y = radius * std::cos(phi);
            float z = radius * std::sin(phi) * std::sin(theta);

            Vertex v;
            v.position = Vec3(x, y, z);
            v.normal = Vec3(x, y, z) / radius;  // 单位球法线 = 归一化位置
            v.texCoord = Vec2((float)seg / segments, (float)ring / rings);
            mesh.vertices_.push_back(v);
        }
    }

    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            uint32_t a = ring * (segments + 1) + seg;
            uint32_t b = a + segments + 1;

            mesh.indices_.push_back(a);
            mesh.indices_.push_back(b);
            mesh.indices_.push_back(a + 1);

            mesh.indices_.push_back(a + 1);
            mesh.indices_.push_back(b);
            mesh.indices_.push_back(b + 1);
        }
    }

    return mesh;
}

Mesh Mesh::CreatePlane(float size) {
    Mesh mesh;
    float h = size / 2.0f;

    mesh.vertices_ = {
        {{-h, 0, -h}, {0, 1, 0}, {0, 0}},
        {{ h, 0, -h}, {0, 1, 0}, {1, 0}},
        {{ h, 0,  h}, {0, 1, 0}, {1, 1}},
        {{-h, 0,  h}, {0, 1, 0}, {0, 1}},
    };

    mesh.indices_ = { 0, 1, 2, 0, 2, 3 };
    return mesh;
}

Mesh Mesh::CreateCube(float size) {
    Mesh mesh;
    float h = size / 2.0f;

    struct Face {
        Vec3 positions[4];
        Vec3 normal;
        Vec2 uvs[4];
    };

    Face faces[6] = {
        // 前 (+Z)
        {{{-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}}, {0,0,1}, {{0,0},{1,0},{1,1},{0,1}}},
        // 后 (-Z)
        {{{ h,-h,-h}, {-h,-h,-h}, {-h, h,-h}, { h, h,-h}}, {0,0,-1}, {{0,0},{1,0},{1,1},{0,1}}},
        // 左 (-X)
        {{{-h,-h,-h}, {-h,-h, h}, {-h, h, h}, {-h, h,-h}}, {-1,0,0}, {{0,0},{1,0},{1,1},{0,1}}},
        // 右 (+X)
        {{{ h,-h, h}, { h,-h,-h}, { h, h,-h}, { h, h, h}}, {1,0,0}, {{0,0},{1,0},{1,1},{0,1}}},
        // 上 (+Y)
        {{{-h, h, h}, { h, h, h}, { h, h,-h}, {-h, h,-h}}, {0,1,0}, {{0,0},{1,0},{1,1},{0,1}}},
        // 下 (-Y)
        {{{-h,-h,-h}, { h,-h,-h}, { h,-h, h}, {-h,-h, h}}, {0,-1,0}, {{0,0},{1,0},{1,1},{0,1}}},
    };

    for (int i = 0; i < 6; i++) {
        uint32_t base = (uint32_t)mesh.vertices_.size();
        for (int j = 0; j < 4; j++) {
            mesh.vertices_.push_back({faces[i].positions[j], faces[i].normal, faces[i].uvs[j]});
        }
        mesh.indices_.push_back(base + 0);
        mesh.indices_.push_back(base + 1);
        mesh.indices_.push_back(base + 2);
        mesh.indices_.push_back(base + 0);
        mesh.indices_.push_back(base + 2);
        mesh.indices_.push_back(base + 3);
    }

    return mesh;
}

Mesh Mesh::CreateCylinder(float radius, float height, int segments) {
    Mesh mesh;
    float halfH = height / 2.0f;

    // 侧面
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * 3.14159265f * i / segments;
        float x = radius * std::cos(theta);
        float z = radius * std::sin(theta);
        float u = (float)i / segments;

        mesh.vertices_.push_back({{x, -halfH, z}, {x, 0, z}, {u, 0}});
        mesh.vertices_.push_back({{x,  halfH, z}, {x, 0, z}, {u, 1}});
    }

    for (int i = 0; i < segments; i++) {
        uint32_t bl = i * 2;
        uint32_t br = bl + 2;
        mesh.indices_.push_back(bl);
        mesh.indices_.push_back(br);
        mesh.indices_.push_back(bl + 1);
        mesh.indices_.push_back(bl + 1);
        mesh.indices_.push_back(br);
        mesh.indices_.push_back(br + 1);
    }

    // 顶面和底面（省略，原理相同）
    // ...

    return mesh;
}
```

### 6. Camera.h

```cpp
#pragma once
#include "Core/Public/MathTypes.h"
#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// 传递给 GPU 的常量缓冲结构（必须 16 字节对齐）
struct alignas(16) SceneConstants {
    Mat4 viewProj;       // 视图投影矩阵
    Mat4 model;          // 模型矩阵
    Vec4 cameraPos;      // 相机位置 (w 分量不用)
    Vec4 lightDir;       // 光照方向 (w 分量不用)
    Vec4 lightColor;     // 光照颜色 (w 分量不用)
    float time;          // 时间
    float _padding[3];   // 填充到 16 字节对齐
};

class Camera {
public:
    Camera();

    // 轨道相机操作
    void Orbit(float deltaYaw, float deltaPitch);  // 鼠标左键拖拽
    void Pan(float deltaX, float deltaY);           // 鼠标中键拖拽
    void Zoom(float delta);                         // 滚轮

    // 获取矩阵
    Mat4 GetViewMatrix() const;
    Mat4 GetProjectionMatrix(float aspectRatio) const;

    // 获取相机位置
    Vec3 GetPosition() const;

    // 设置参数
    void SetDistance(float dist) { distance_ = dist; }
    void SetTarget(const Vec3& target) { target_ = target; }

    // 创建常量缓冲（供 DX12 使用）
    bool CreateConstantBuffer(ID3D12Device* device);
    void UpdateConstantBuffer(float aspectRatio);

    // 将常量缓冲设置到命令列表
    void SetToCommandList(ID3D12GraphicsCommandList* cmdList);

private:
    void UpdatePosition();

    Vec3 target_ = Vec3(0, 0, 0);
    float distance_ = 3.0f;
    float yaw_ = 45.0f;
    float pitch_ = 30.0f;
    float fov_ = 45.0f;
    float nearPlane_ = 0.1f;
    float farPlane_ = 100.0f;

    Vec3 position_;

    // 常量缓冲资源
    ComPtr<ID3D12Resource> constantBuffer_;
    SceneConstants* mappedData_ = nullptr;
};
```

### 7. Camera.cpp

```cpp
#include "Renderer/Public/Camera.h"
#include "Core/Public/Logger.h"
#include <d3dx12.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

Camera::Camera() {
    UpdatePosition();
}

void Camera::UpdatePosition() {
    float radYaw = glm::radians(yaw_);
    float radPitch = glm::radians(pitch_);

    position_ = target_ + distance_ * Vec3(
        std::cos(radPitch) * std::sin(radYaw),
        std::sin(radPitch),
        std::cos(radPitch) * std::cos(radYaw)
    );
}

void Camera::Orbit(float deltaYaw, float deltaPitch) {
    yaw_ += deltaYaw;
    pitch_ += deltaPitch;
    pitch_ = glm::clamp(pitch_, -89.0f, 89.0f);
    UpdatePosition();
}

void Camera::Pan(float deltaX, float deltaY) {
    Mat4 view = GetViewMatrix();
    Vec3 right = Vec3(view[0][0], view[1][0], view[2][0]);
    Vec3 up = Vec3(view[0][1], view[1][1], view[2][1]);

    float panSpeed = distance_ * 0.002f;
    target_ -= right * deltaX * panSpeed;
    target_ += up * deltaY * panSpeed;
    UpdatePosition();
}

void Camera::Zoom(float delta) {
    distance_ *= (1.0f - delta * 0.1f);
    distance_ = glm::clamp(distance_, 0.5f, 50.0f);
    UpdatePosition();
}

Mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(position_, target_, Vec3(0, 1, 0));
}

Mat4 Camera::GetProjectionMatrix(float aspectRatio) const {
    // DX 使用 0~1 的深度范围（OpenGL 是 -1~1），glm 默认 OpenGL 行为
    // 使用 glm::perspectiveLH_ZO 获得正确的 DX12 投影矩阵
    return glm::perspectiveLH_ZO(glm::radians(fov_), aspectRatio,
                                  nearPlane_, farPlane_);
}

Vec3 Camera::GetPosition() const {
    return position_;
}

bool Camera::CreateConstantBuffer(ID3D12Device* device) {
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SceneConstants));

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&constantBuffer_));

    if (FAILED(hr)) {
        ME_LOG_ERROR("Failed to create camera constant buffer");
        return false;
    }

    // 持久映射（上传堆支持持久映射）
    hr = constantBuffer_->Map(0, nullptr,
                              reinterpret_cast<void**>(&mappedData_));
    if (FAILED(hr)) {
        ME_LOG_ERROR("Failed to map constant buffer");
        return false;
    }

    return true;
}

void Camera::UpdateConstantBuffer(float aspectRatio) {
    if (!mappedData_) return;

    SceneConstants consts = {};
    consts.viewProj = GetProjectionMatrix(aspectRatio) * GetViewMatrix();
    consts.model = Mat4(1.0f);
    consts.cameraPos = Vec4(position_, 1.0f);
    consts.lightDir = Vec4(glm::normalize(Vec3(-1, -1, -1)), 0.0f);
    consts.lightColor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    consts.time = 0.0f;

    memcpy(mappedData_, &consts, sizeof(SceneConstants));
}

void Camera::SetToCommandList(ID3D12GraphicsCommandList* cmdList) {
    if (!constantBuffer_) return;
    cmdList->SetGraphicsRootConstantBufferView(
        0, constantBuffer_->GetGPUVirtualAddress());
}
```

### 8. CMakeLists.txt 添加 DX12 依赖

确保 `CMakeLists.txt` 包含 DX12 相关库（参考课1的配置）：

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

此时还不能看到渲染效果（需要课17的 ViewportPanel），但可以单独测试各组件：

```cpp
// 临时测试（在 main.cpp 或单独的测试文件中）
#include "Renderer/Public/Shader.h"
#include "Renderer/Public/Mesh.h"
#include "Renderer/Public/Camera.h"

// 测试 Mesh 生成（CPU 端，不需要 DX12 设备）
auto sphere = Mesh::CreateSphere(1.0f);
assert(sphere.GetIndexCount() > 0);
ME_LOG_INFO("Sphere: %zu vertices, %zu indices",
            sphere.GetVertices().size(), sphere.GetIndices().size());

auto cube = Mesh::CreateCube(1.0f);
ME_LOG_INFO("Cube: %zu vertices, %zu indices",
            cube.GetVertices().size(), cube.GetIndices().size());

// 测试 Camera（CPU 端矩阵计算）
Camera cam;
auto view = cam.GetViewMatrix();
auto proj = cam.GetProjectionMatrix(16.0f / 9.0f);
ME_LOG_INFO("Camera position: (%.2f, %.2f, %.2f)",
            cam.GetPosition().x, cam.GetPosition().y, cam.GetPosition().z);

// 测试 Shader 编译（不需要设备）
const char* testVS = R"(
    struct VS_INPUT {
        float3 position : POSITION;
        float3 normal   : NORMAL;
        float2 uv       : TEXCOORD;
    };
    struct VS_OUTPUT {
        float4 pos : SV_POSITION;
        float3 worldPos : TEXCOORD0;
        float3 normal : TEXCOORD1;
        float2 uv : TEXCOORD2;
    };
    cbuffer SceneConstants : register(b0) {
        float4x4 viewProj;
        float4x4 model;
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
auto vsResult = Shader::CompileFromSource(testVS, "main", "vs_5_0", "test.hlsl");
ME_LOG_INFO("VS compile: %s", vsResult.IsValid() ? "OK" : vsResult.errorMessage.c_str());

// GPU 上传测试需要 DX12 设备上下文（在课17中验证）
```

---

## 设计要点：DX12 vs OpenGL 的区别

| 概念 | OpenGL | DX12 |
|------|--------|------|
| 着色器编译 | glCompileShader (运行时) | D3DCompile (运行时) / DXC (离线) |
| 程序对象 | GLuint program | ID3D12PipelineState |
| 资源布局 | uniform (按名字) | 根签名 (按寄存器槽) |
| 缓冲上传 | glBufferData (一步到位) | 上传堆 → 默认堆 (两步) |
| 绘制调用 | glDrawElements | DrawIndexedInstanced |
| 常量传递 | glUniform* | CBV + SetGraphicsRootConstantBufferView |
| 投影矩阵 | Z 范围 [-1, 1] | Z 范围 [0, 1]（使用 LH_ZO 变体） |

这些区别是 DX12 更底层但更灵活的体现。在 UE5 中，这些差异被 RHI（Render Hardware Interface）抽象层隐藏了。我们的项目直接使用 DX12，所以需要自己处理这些细节。

---

## UE5 参考（相对 `Engine/` 路径）

- `Engine/Source/Runtime/Engine/Private/StaticMesh.cpp` — 网格生成（对照我们的 `Mesh`）
- `Engine/Source/Runtime/Engine/Private/MaterialEditorRender.cpp` — 材质预览渲染
- `Engine/Source/Runtime/RHI/` — RHI 抽象层（DX12/Vulkan/Metal 统一接口）

### 对照 UE 渲染抽象（RHI 层）

| 我们的（直接 DX12）| UE（RHI 抽象）| 作用 |
|------------------|--------------|------|
| `Shader`（D3DCompile + 根签名 + PSO）| `FRHIShader` + `FPipelineStateObject` | 着色器 + 管线状态 |
| `Mesh`（顶点/索引缓冲）| `FRHIVertexBuffer` / `FStaticMeshVertexBuffers` | 几何数据 |
| `Camera`（视图/投影矩阵）| `FSceneView` | 相机 |
| `DX12Device`（课14）| `FD3D12Device` / `IRHIDevice` | GPU 设备 |

**三个关键差异**：

1. **UE 有 RHI（Render Hardware Interface）抽象层**——一套 `FRHI*` 接口，底层有 DX12/Vulkan/Metal 各实现，引擎上层不关心用哪个 API。我们的项目**直接用 DX12**（单后端），没有 RHI 层——代价是跨平台难，好处是简单、直接学 DX12 本身。

2. **UE 的网格**（`FStaticMesh`）含 LOD、流式加载（`FStreamableRenderResource`）、显存驻留管理（`FRenderResource`）。我们的 `Mesh` 是简单顶点/索引缓冲，上传一次就用——教学够用，但缺少 LOD 和流式加载。

3. **UE 的相机**（`FSceneView`）集成在场景渲染管线（`FSceneRenderer`），含遮挡剔除、LOD 选择、阴影级联等。我们的 `Camera` 只管生成视图/投影矩阵——纯数学，不含场景管理。

> **搜索关键词**（UE 源码）：`FRHIShader`、`FPipelineStateObject`、`FStaticMeshRenderResources`、`FSceneView`、`FSceneRenderer`、`DrawSphere`。

---

## 深度扩展：多进程 Shader 编译集成

> **来源说明**：课15 末尾的"扩展预告：多进程 shader 编译（块7）"占位——**那块现在落地在课16 这里**。`lesson06-extension.md` 里同一主题的编号是"块5"——同一件事，文档演进过程中序号有调整，**落地页统一指课16**。
>
> **为什么放在课16**：渲染器封装层（`Shader` / `Mesh` / `Camera`）是管 shader 编译管线的天然位置——`Shader` 类本身就是字节码的消费者（`CreatePSO` 需要 `D3D12_SHADER_BYTECODE`），把"编译管线"放在它旁边最有内聚性。本课 `Shader.cpp` 的 `CompileFromSource` 当前用单进程 `D3DCompile`——本节就是它的**工业级升级**。
>
> 这块是项目里**唯一系统学并发编程**的地方。预估代码量 +2000-4000 行，是独立子系统。本节给出**架构骨架与协议设计**（不逐行实现），重点是理解原理与设计取舍——这也是 UE5 `ShaderCompileWorker` / `FShaderCompileManager` 的设计思想。

### 时机与依赖（三个前置条件课16 都满足）

1. **输入就绪**：课8 的 `MaterialCompiler::Compile` 已经能输出完整 HLSL 源码字符串（`CompileResult::hlsl_code`）——这就是多进程编译的输入
2. **消费者就绪**：课14/15 的 DX12 基础已经会用 PSO（`D3D12_GRAPHICS_PIPELINE_STATE_DESC` 需要字节码 `D3D12_SHADER_BYTECODE`）——这就是多进程编译的输出
3. **封装层就绪**：本课的 `Shader` 类已经把"源码 → PSO"封装好——`CompileFromSource` 内部从"调 `D3DCompile`"换成"调 `ShaderCompileManager`"是**最小改动**

> **课15 的 `D3DCompileFromFile` 是学习调试用**（同步、单文件、无变体、无缓存）。生产级 shader 编译是**多进程并发 + 变体 + 缓存**——本节就是它的工业级对标。

---

### 1. 为什么 shader 编译必须并发

**问题规模**：一个材质项目有多少 shader 变体？

```
材质数 × 每材质变体数 = 总编译任务
    ↑              ↑
项目规模        StaticSwitch × 材质域 × 混合模式 × 质量级
```

具体数字感受一下规模：

| 项目规模 | 材质数 | 每材质变体数 | 总变体 | 单条编译耗时 | 串行总时间 |
|---------|-------|------------|--------|-------------|----------|
| 教学版 | 10 | 2 | 20 | 100ms | 2 秒 |
| 小型项目 | 200 | 8 | 1600 | 200ms | 5 分钟 |
| 中型项目（UE5 实际量级）| 5000 | 50 | 250000 | 300ms | ~20 小时 |

> 这就是为什么 UE5 启动编译要等几十分钟到几小时——**串行根本不可用**。8 核 CPU 并发能加速 6-8 倍（不是 8 倍，因为 IPC 和进程管理有开销）。

**结论**：哪怕教学版只有几十个 shader，也要**一开始就用并发**——不然项目规模稍微上去就完全不可用。这也是项目原则"不做先简化再升级的中间态"的直接体现。

---

### 2. 多线程 vs 多进程（核心选型，选多进程）

| 维度 | 多线程 | 多进程（选定） |
|------|--------|--------------|
| 实现 | `std::thread` 线程池 + 共享内存 | N 个独立 worker 进程（exe） |
| 通信 | 共享变量（最快）| IPC：管道 / 文件 / 共享内存 |
| 崩溃隔离 | 一线程崩 → 整个进程崩，编辑器挂掉 | worker 崩了不影响主，重启即可 |
| 第三方库线程安全 | **`D3DCompile`（fxc）不完全线程安全**；DXC 调用约定更严，并发调用风险大 | 进程天然隔离，无视线程安全 |
| 卡死处理 | 难——`std::thread` 没有 `terminate`，`TerminateThread` 危险（持锁状态下强杀会泄漏）| 进程可强杀（`TerminateProcess`，安全级别高）|
| 资源开销 | 小（线程 ~1MB 栈）| 大（每进程几十 MB）—— 但 shader 编译本身吃几百 MB，相对可忽略 |
| 平台/版本隔离 | 难——一个 DLL 版本绑死 | 每平台一个 worker exe，独立依赖 |
| 跨平台 | 易（`std::thread` 标准）| 难——`CreateProcess`（Win）/ `fork+exec`（POSIX）不同 |

**选多进程的 4 条理由**（同 UE5 `ShaderCompileWorker`）：

1. **fxc / DXC 线程不安全**：`D3DCompile` 内部用全局状态（include handler、错误 buffer），多线程并发调用偶发崩溃，进程隔离彻底规避
2. **非法 HLSL 崩溃隔离**：用户材质图生成的 HLSL 可能非法，fxc 处理某些非法输入会直接 `access violation`——worker 崩了，主进程编辑器继续活着
3. **worker 卡死能杀**：偶尔 fxc 在某输入上死循环（极少，但有），`TerminateProcess` 强杀重启该 worker，主线不卡
4. **平台/版本独立**：以后接 Vulkan（glslang）/ Metal 编译器时，每个后端一个 worker exe，依赖不冲突

**代价**：IPC 开销（每任务几 KB~几 MB 数据拷贝）+ 进程启动开销（每 worker 几十毫秒启动）。但 shader 编译本身几十~几百 ms，IPC 开销占比小。**值得换**。

> **UE 对照**：UE5 的 `ShaderCompileWorker`（`Engine/Source/Programs/ShaderCompileWorker/`）就是这个架构——独立 exe，由 `FShaderCompileManager`（`Engine/Source/Runtime/RenderCore/Private/ShaderCompiler.cpp`）启动管理。

---

### 3. 整体架构（主进程 ↔ N 个 Worker）

```
主进程（MaterialEditor.exe）                 Worker 进程 × N（ShaderCompileWorker.exe）
==========================================   ==========================================

┌──────────────────────────────────────┐    ┌────────────────────────────────────┐
│  UI 线程                              │    │  Worker 0..N-1（N = CPU 核数）      │
│  ┌─────────────────────────┐         │    │  ┌──────────────────────┐          │
│  │ 用户改材质图 → 触发编译 │         │    │  │ main() 循环：        │          │
│  └─────────────────────────┘         │    │  │   while (running):   │          │
│                ↓                     │    │  │     req = read(stdin)│          │
│  ┌─────────────────────────┐         │    │  │     bc  = D3DCompile │          │
│  │ MaterialCompiler::Compile│         │    │  │             (req)    │          │
│  │  → HLSL 源码             │         │    │  │     write(stdout,bc) │          │
│  └─────────────────────────┘         │    │  └──────────────────────┘          │
│                ↓                     │    └────────────────────────────────────┘
│  ┌─────────────────────────┐         │                       ↑↓ IPC（管道）
│  │ ShaderCompileManager    │         │                       │
│  │  ┌──────────────────┐   │ 投递    │                       │
│  │  │ 任务队列          │ ──┼─────────┼──→ 投递到 worker       │
│  │  │ (mutex + cv)     │   │         │     stdin（请求）      │
│  │  └──────────────────┘   │         │                       │
│  │  ┌──────────────────┐   │ 收结果   │                       │
│  │  │ 调度线程          │ ←─┼─────────┼──← worker stdout       │
│  │  │ 找空闲 worker     │   │         │     （字节码/错误）     │
│  │  │  投递 → 收响应    │   │         │                       │
│  │  └──────────────────┘   │         │                       │
│  │  shader 缓存(hash 表)   │ ←─ 命中则跳过编译                │
│  └─────────────────────────┘         │                       │
│                ↓                     │                       │
│  ┌─────────────────────────┐         │                       │
│  │ Shader::CreatePSO(...)  │ ←─ 字节码                        │
│  │  → ID3D12PipelineState  │                                   │
│  └─────────────────────────┘                                   │
└──────────────────────────────────────┘
```

**数据流**（一次编译任务的生命周期）：

1. UI 触发 → `MaterialCompiler::Compile(graph)` 输出 HLSL 源码（课8）
2. `ShaderCompileManager::SubmitAsync(req)` 投递任务到队列
3. **缓存检查**：`hash(HLSL + macros + target + entry + bDebug)` 命中 → 直接返回缓存的字节码
4. **未命中**：调度线程找到一个空闲 worker → 通过 IPC 写到该 worker 的 stdin → 阻塞等 stdout 响应
5. Worker 收到请求 → 调 `D3DCompile` → 得到字节码（或错误文本）
6. 主进程收响应 → 写入缓存 → `promise.set_value` → 调用方的 `future.get()` 返回
7. 主进程拿到字节码 → 创建 `ID3DBlob` → `Shader::CreatePSO` 创建 PSO
8. 失败路径 → 错误文本回传 → 解析（第 10 节）→ 显示在材质编辑器错误面板（课19 接 UI）

---

### 4. Worker 进程内：调用 `D3DCompile`（fxc 路径）

Worker 进程本质就是个"循环读 stdin → 调 `D3DCompile` → 写 stdout"的程序。核心编译函数和主进程的单进程版几乎一样，区别在：**宏定义（变体）从请求里来、错误信息序列化到响应里**。

```cpp
// === ShaderCompileWorker 内部的核心编译函数 ===
// 对比主进程 Shader.cpp 的 CompileFromSource：增加宏定义支持、序列化输出
struct CompileOutput {
    bool success = false;
    std::vector<uint8_t> bytecode;   // 成功：DXBC 字节码
    std::string errorMessage;        // 失败：fxc 的错误文本（透传，主进程解析）
};

CompileOutput CompileHLSL(const ShaderCompileRequest& req) {
    CompileOutput out;

    // --- 1. 把请求里的 define 数组转成 D3D_SHADER_MACRO 数组 ---
    // D3D_SHADER_MACRO 是 {LPCSTR Name, LPCSTR Definition}，末尾要 {nullptr, nullptr} 哨兵
    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(req.defines.size() + 1);
    for (const auto& d : req.defines)
        macros.push_back({d.name.c_str(), d.value.c_str()});
    macros.push_back({nullptr, nullptr});   // ← 末尾哨兵，D3DCompile 要求

    // --- 2. 编译标志 ---
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;        // 默认开优化
    if (req.bDebug)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    // 课15 单进程版只在 _DEBUG 下加调试标志；多进程版让 caller 控制（缓存 key 含 bDebug）

    // --- 3. 调用 D3DCompile ---
    // 关键改动：include handler = nullptr（不是 D3D_COMPILE_STANDARD_FILE_INCLUDE）
    // 原因：worker 进程的工作目录可能不是项目根，#include 解析会失败
    // 教学版的 HLSL 是单文件自洽的（课8 GenerateCode 保证），不需要 include
    ComPtr<ID3DBlob> bytecodeBlob;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(
        req.hlslSource.data(),
        req.hlslSource.size(),
        req.debugName.c_str(),    // 编译错误信息里的"文件名"（任意，仅显示用）
        macros.data(),            // ← 宏定义（变体）从这里来
        nullptr,                  // include handler（教学版不需要）
        req.entryPoint.c_str(),
        req.target.c_str(),       // "vs_5_0" / "ps_5_0" / "cs_5_0" 等
        flags,
        0,
        &bytecodeBlob,
        &errorBlob);

    // --- 4. 处理结果 ---
    if (FAILED(hr)) {
        // fxc 错误格式："filename(line): error Xxxxx: message"，原样透传
        // 主进程解析（第 10 节）—— worker 不解析，保持职责单一
        if (errorBlob)
            out.errorMessage = static_cast<const char*>(errorBlob->GetBufferPointer());
        else
            out.errorMessage = "D3DCompile failed, HRESULT=0x" + std::to_string(hr);
        return out;
    }

    // 成功：拷贝字节码（DXBC 二进制）
    out.success = true;
    out.bytecode.assign(
        static_cast<const uint8_t*>(bytecodeBlob->GetBufferPointer()),
        static_cast<const uint8_t*>(bytecodeBlob->GetBufferPointer()) + bytecodeBlob->GetBufferSize());
    return out;
}
```

**关键点**：

- **include handler = nullptr**：单进程版用 `D3D_COMPILE_STANDARD_FILE_INCLUDE`（允许 `#include "xxx.hlsli"`），多进程版**禁用**——worker 工作目录不确定，include 解析会失败。教学版的 HLSL 是单文件自洽的（课8 `GenerateCode` 保证），不需要 include。如果以后需要，include 文件要随请求一起 IPC 过去（或预编译成字符串嵌入 HLSL）。
- **宏定义（变体）**：`StaticSwitch` / 材质域 / 混合模式都通过宏切换。一个材质图能生成多个变体（不同宏组合），每个变体是一个独立的 `ShaderCompileRequest`。
- **错误透传**：fxc 的错误文本格式固定（见第 10 节），worker 不解析，原样回传——保持 worker 职责单一。

---

### 5. Worker 进程内：DXC（DXIL 路径，可选扩展）

**fxc 编译的是 DXBC（Shader Model 5.0 及以下）**，DX12 全功能用。**DXC 编译的是 DXIL（SM 6.0+）**，支持 wave intrinsics、raytracing、mesh shader 等新特性。

教学版用 fxc 够了（DX12 SM 5.0 足够画材质预览）。但架构上要**预留 DXC 路径**——以后接 SM 6.x 时不用大改：

```cpp
// DXC 调用骨架（伪代码）—— DXC 是 COM 组件，调用约定比 D3DCompile 啰嗦
CompileOutput CompileHLSL_DXC(const ShaderCompileRequest& req) {
    CompileOutput out;

    // 1. 创建 DXC 编译器对象（COM）
    ComPtr<IDxcCompiler3> compiler;
    ComPtr<IDxcUtils>     utils;
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
    DxcCreateInstance(CLSID_DxcUtils,    IID_PPV_ARGS(&utils));

    // 2. 把 HLSL 源码包成 IDxcBlobEncoding
    ComPtr<IDxcBlobEncoding> sourceBlob;
    utils->CreateBlobFromPinned(req.hlslSource.data(), (UINT)req.hlslSource.size(),
                                 DXC_CP_UTF8, &sourceBlob);

    // 3. 构造编译参数（DXC 用 wchar_t* 数组传参）
    std::vector<LPCWSTR> args = {
        L"-E", widen(req.entryPoint).c_str(),   // 入口
        L"-T", widen(req.target).c_str(),        // 目标 ps_6_0 / vs_6_0
        L"-O3",                                   // 优化级别
    };
    if (req.bDebug) args.push_back(L"-Zi");

    // 4. 宏定义通过 DxcBuffer / 参数传（DXC 宏传递方式略不同，参考 DXC 文档）

    // 5. 编译
    DxcBuffer dxcBuffer { sourceBlob->GetBufferPointer(),
                          sourceBlob->GetBufferSize(),
                          DXC_CP_UTF8 };
    ComPtr<IDxcResult> result;
    HRESULT hr = compiler->Compile(
        &dxcBuffer,
        args.data(), (UINT)args.size(),
        nullptr,             // include handler
        IID_PPV_ARGS(&result));

    // 6. 取结果（DXC 错误在 IDxcResultUtf8 里，需要单独取）
    if (SUCCEEDED(hr)) {
        ComPtr<IDxcBlob> code;
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&code), nullptr);
        out.success = true;
        out.bytecode.assign(...);
    } else {
        ComPtr<IDxcBlobUtf8> errors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        out.errorMessage = errors ? errors->GetStringPointer() : "DXC failed";
    }

    return out;
}
```

> **教学版优先 fxc**（`D3DCompile`），简单稳定，DX12 SM 5.0 够用。DXC 路径**留作扩展**，不在课16 实现。架构上预留即可——worker main 里加分支 `if (target 以 "ps_6"/"vs_6" 开头) → CompileHLSL_DXC else CompileHLSL`。

---

### 6. IPC 协议设计

#### 选型对比

| IPC 方式 | 实现 | 优点 | 缺点 | 适合场景 |
|---------|------|------|------|---------|
| **匿名管道**（`CreatePipe` + 句柄继承）| 主进程创建一对管道，子进程继承作为 stdin/stdout | **简单**、和 `CreateProcess` 天然配合、UE5 也用这招 | 单向（要 2 根）、字节流（要自带帧边界）| **推荐（教学版用这个）** |
| 命名管道（`CreateNamedPipe`）| 主进程创建命名管道，子进程 `CreateFile` 连接 | 双向、可多客户端、可跨网络 | API 啰嗦、要管管道名冲突 | 多客户端、跨机器 |
| 临时文件 | 主进程把请求写到文件，子进程读 | 最简单、跨平台 | 慢、要管清理、磁盘 IO | 调试期或极端大字节码 |
| 共享内存（`CreateFileMapping`）| 双方映射同一块内存 | 最快（零拷贝）| 同步复杂（要 mutex）、生命周期管理难 | 大数据量（数百 MB+）|

**教学版选匿名管道**：和 `CreateProcess` 的 stdin/stdout 重定向天然配合，UE5 也是这套。字节码虽然可能几 MB，但管道缓冲区调大 + 分块传输足够。

#### 协议数据结构（请求 / 响应）

**关键设计**：用**长度前缀 + 二进制负载**的帧格式（不像 HTTP 头那么复杂，但保证消息边界——管道是字节流，必须自带边界）。

```cpp
// === 协议放在共享头文件：ShaderCompileProtocol.h（主进程和 worker 都包含）===

namespace ShaderCompileProtocol {

constexpr uint32_t MAGIC   = 0x4D455343;   // 'MESC' = Material Editor Shader Compile
constexpr uint32_t VERSION = 1;

// 单个宏定义（定长数组，便于协议序列化）
struct ShaderMacro {
    char name[64];     // 宏名（如 "QUALITY_HIGH"）
    char value[64];    // 宏值（如 "1"，空字符串表示无值 define）
};

// 请求头（定长，便于 worker 解析）
struct RequestHeader {
    uint32_t magic;          // = MAGIC，协议校验
    uint32_t version;        // = VERSION
    uint64_t jobId;          // 主进程分配的任务 ID（响应用它对回来）
    char     entryPoint[64]; // "main" / "VSMain" / ...
    char     target[16];     // "vs_5_0" / "ps_5_0" / ...
    uint32_t defineCount;    // 宏定义数量
    uint32_t sourceSize;     // HLSL 源码字节数
    uint32_t bDebug : 1;     // 是否带调试信息（影响缓存 key）
    uint32_t reserved : 31;
};
// 帧格式：[RequestHeader] [ShaderMacro × defineCount] [HLSL source (sourceSize bytes)]
// worker 读：先固定读 sizeof(RequestHeader)，再按字段长度读后续

// 响应头（定长）
struct ResponseHeader {
    uint32_t magic;          // = MAGIC
    uint64_t jobId;          // 对应请求的 jobId
    int32_t  success;        // 1=成功，0=编译错误，-1=worker 内部错误（如 OOM）
    uint32_t bytecodeSize;   // 字节码字节数（成功时）
    uint32_t errorSize;      // 错误文本字节数（失败时）
};
// 帧格式：[ResponseHeader] [bytecode (bytecodeSize bytes)] [error text (errorSize bytes)]

} // namespace ShaderCompileProtocol
```

**帧格式（二进制流）**：

```
请求帧：  [Header (96B)] [Macro × N (128B × N)] [HLSL source]
响应帧：  [Header (24B)] [DXBC bytecode]         [Error text]
```

**为什么用二进制而非 JSON**：

- **字节码是二进制**：DXBC 不是文本，JSON base64 编码膨胀 33%
- **解析快**：定长头 + 长度前缀，无文本解析开销
- **简单**：内存拷贝即序列化（POD 结构 + memcpy）

**为什么不传文件路径**：worker 跨进程文件可能不在同一磁盘位置（不同机器、不同挂载点），传源码内容更稳。临时文件路径作为兜底（极端大 HLSL 才用）。

#### 大字节码传输

DXBC 字节码通常几 KB~几十 KB（VS/PS），偶尔上百 KB（复杂材质）。**匿名管道默认缓冲区 4 KB**，传输大块要：

1. **调大管道缓冲区**：`CreatePipe(..., bufferSize)` 传 1 MB 或更大
2. **分块写**：单次 `WriteFile` > 缓冲区时 OS 自动分块，主进程 `ReadFile` 循环读到预期字节数即可
3. **用阻塞 IO**：worker 不多任务，主进程调度线程里一个 worker 一个调用栈阻塞读，简单可靠

> 不要用非阻塞 IO + select——那是网络编程思路。本机管道阻塞读最简单。

---

### 7. Worker main 循环

Worker 是个独立 exe（`ShaderCompileWorker.exe`），main 函数就是一个循环：

```cpp
// === ShaderCompileWorker.cpp（worker 进程主程序）===

#include "ShaderCompileProtocol.h"
#include <d3dcompiler.h>
#include <io.h>       // _setmode
#include <fcntl.h>    // _O_BINARY
#include <cstdio>

// 从 stdin 读 N 字节（阻塞，直到读够）
bool ReadExact(HANDLE hIn, void* buf, DWORD size) {
    DWORD totalRead = 0;
    while (totalRead < size) {
        DWORD read = 0;
        if (!ReadFile(hIn, (char*)buf + totalRead, size - totalRead, &read, nullptr))
            return false;   // 管道关了，主进程让 worker 退出
        if (read == 0) return false;
        totalRead += read;
    }
    return true;
}

// 往 stdout 写 N 字节
bool WriteExact(HANDLE hOut, const void* buf, DWORD size) {
    DWORD totalWritten = 0;
    while (totalWritten < size) {
        DWORD written = 0;
        if (!WriteFile(hOut, (const char*)buf + totalWritten,
                       size - totalWritten, &written, nullptr))
            return false;
        totalWritten += written;
    }
    return true;
}

int main() {
    // ★ Windows 关键：stdin/stdout 默认文本模式会改 \n → \r\n，破坏字节码
    // 必须切到二进制模式（这是经典坑，见第 13 节）
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    ME_LOG_INFO("ShaderCompileWorker started, pid=%u", GetCurrentProcessId());

    while (true) {
        // --- 1. 读请求头 ---
        ShaderCompileProtocol::RequestHeader header;
        if (!ReadExact(hIn, &header, sizeof(header)))
            break;   // 主进程关管道 = 让 worker 退出

        if (header.magic != ShaderCompileProtocol::MAGIC) {
            ME_LOG_ERROR("Bad magic in request: %u", header.magic);
            break;
        }

        // --- 2. 读宏定义 ---
        std::vector<ShaderCompileProtocol::ShaderMacro> macros(header.defineCount);
        if (header.defineCount > 0)
            ReadExact(hIn, macros.data(),
                      header.defineCount * sizeof(ShaderCompileProtocol::ShaderMacro));

        // --- 3. 读 HLSL 源码 ---
        std::string hlslSource(header.sourceSize, '\0');
        if (header.sourceSize > 0)
            ReadExact(hIn, hlslSource.data(), header.sourceSize);

        // --- 4. 编译 ---
        ShaderCompileRequest req;
        req.entryPoint = header.entryPoint;
        req.target     = header.target;
        req.bDebug     = header.bDebug;
        req.hlslSource = std::move(hlslSource);
        for (auto& m : macros) req.defines.push_back({m.name, m.value});

        CompileOutput output = CompileHLSL(req);   // 第 4 节的实现

        // --- 5. 写响应头 ---
        ShaderCompileProtocol::ResponseHeader resp {};
        resp.magic        = ShaderCompileProtocol::MAGIC;
        resp.jobId        = header.jobId;
        resp.success      = output.success ? 1 : 0;
        resp.bytecodeSize = (uint32_t)output.bytecode.size();
        resp.errorSize    = (uint32_t)output.errorMessage.size();
        WriteExact(hOut, &resp, sizeof(resp));

        // --- 6. 写字节码和错误文本 ---
        if (resp.bytecodeSize > 0)
            WriteExact(hOut, output.bytecode.data(), resp.bytecodeSize);
        if (resp.errorSize > 0)
            WriteExact(hOut, output.errorMessage.data(), resp.errorSize);
    }

    ME_LOG_INFO("ShaderCompileWorker exit, pid=%u", GetCurrentProcessId());
    return 0;   // ★ 永远返回 0：编译错误用响应通道（success=0）表达，不用 exit code
}
```

**关键点**：

- **stdin/stdout 切二进制模式**：Windows 默认文本模式会把 `\n` 改成 `\r\n`，破坏字节码——这是**最经典的坑**。
- **管道关闭 = 退出信号**：主进程关掉写端，worker 的 `ReadFile` 返回 0 或失败，worker 干净退出。不需要额外的退出协议消息。
- **不返回错误码**：worker 自己永远别 `exit(1)`——编译错误是正常业务结果（`success=0`），通过响应回传；只有 worker 自己内部异常（OOM 等）才考虑退出。`exit code` 永远是 0，让主进程通过"响应读 EOF"判断 worker 崩了。
- **每个 worker 一次只处理一个任务**：worker 不并发——主进程的调度线程保证 N 个 worker 处理 N 个并发任务，单 worker 内串行。这样 worker 内的 `D3DCompile` 不用考虑线程安全。

---

### 8. 主进程：进程池 + 任务队列（`ShaderCompileManager`）

这是整个子系统的**核心类**。职责：管理 N 个 worker 进程、维护任务队列、缓存、给上层暴露异步接口（`future`）。

```cpp
// === ShaderCompileManager.h（主进程侧）===

class ShaderCompileManager {
public:
    static ShaderCompileManager& Get();   // 单例（项目里就一个编译管线）

    // 初始化：启动 N 个 worker（N = CPU 核数，默认 = std::thread::hardware_concurrency()）
    bool Initialize(uint32_t workerCount = 0);

    // 销毁：关掉所有 worker 管道，等待退出
    void Shutdown();

    // === 主接口：提交编译任务，返回 future（异步）===
    // 调用方（如 Shader::CompileFromSource）：
    //   auto future = mgr.SubmitAsync(req);
    //   auto bytecode = future.get();   // 阻塞等结果
    std::future<ShaderBytecode> SubmitAsync(ShaderCompileRequest req);

    // 同步版（包装 async，等结果再返回）—— 给不需要并发的调用点用
    ShaderBytecode SubmitSync(ShaderCompileRequest req);

private:
    // 一个 worker = 一个进程 + 管道对 + 状态标志
    struct Worker {
        HANDLE hProcess = nullptr;
        HANDLE hPipeIn  = nullptr;   // 主进程 → worker（worker 的 stdin）
        HANDLE hPipeOut = nullptr;   // worker → 主进程（worker 的 stdout）
        std::atomic<bool> busy{false};
        std::atomic<bool> alive{false};
    };

    bool LaunchWorker(Worker& w);
    ShaderBytecode DispatchToWorker(Worker& w, const ShaderCompileRequest& req);

    // === 任务队列（生产者-消费者）===
    struct Job {
        ShaderCompileRequest req;
        std::promise<ShaderBytecode> promise;
    };
    std::queue<Job> jobs_;
    std::mutex jobsMutex_;
    std::condition_variable jobsCv_;

    // === 调度线程（从队列取任务，分配给空闲 worker）===
    std::thread schedulerThread_;
    std::atomic<bool> running_{false};

    // === Worker 池 ===
    std::vector<Worker> workers_;
    std::mutex workersMutex_;
    std::condition_variable workerFreeCv_;   // 等待某 worker 变空闲

    // === Shader 缓存（见第 9 节）===
    ShaderCache cache_;
};
```

#### 主流程：调度线程 + worker 池

```cpp
// === ShaderCompileManager.cpp ===

bool ShaderCompileManager::Initialize(uint32_t workerCount) {
    if (workerCount == 0)
        workerCount = std::thread::hardware_concurrency();
    ME_LOG_INFO("Starting %u shader compile workers", workerCount);

    workers_.resize(workerCount);
    for (auto& w : workers_) {
        if (!LaunchWorker(w)) {
            ME_LOG_ERROR("Failed to launch worker");
            return false;
        }
    }

    running_ = true;
    schedulerThread_ = std::thread([this] { SchedulerLoop(); });
    return true;
}

void ShaderCompileManager::SchedulerLoop() {
    while (running_) {
        // 1. 等任务进来（生产者-消费者的"消费者"端）
        Job job;
        {
            std::unique_lock lock(jobsMutex_);
            jobsCv_.wait(lock, [&] { return !jobs_.empty() || !running_; });
            //                        ↑ lambda 形式防虚假唤醒（经典坑）
            if (!running_) break;
            job = std::move(jobs_.front());
            jobs_.pop();
        }

        // 2. 找一个空闲 worker（没有就等）
        Worker* w = nullptr;
        {
            std::unique_lock lock(workersMutex_);
            workerFreeCv_.wait(lock, [&] {
                return std::any_of(workers_.begin(), workers_.end(),
                                   [](auto& x){ return x.alive && !x.busy; });
            });
            for (auto& x : workers_) {
                if (x.alive && !x.busy) { w = &x; x.busy = true; break; }
            }
        }

        // 3. 在调度线程里直接 dispatch（简化版：单 dispatcher）
        //    task 多时可改成"每 worker 一个专责通信 thread"（更并发）
        ShaderBytecode result = DispatchToWorker(*w, job.req);
        job.promise.set_value(std::move(result));

        // 4. worker 标记为空闲，唤醒下一个等空闲的任务
        w->busy = false;
        workerFreeCv_.notify_one();
    }
}

std::future<ShaderBytecode> ShaderCompileManager::SubmitAsync(ShaderCompileRequest req) {
    Job job;
    job.req = std::move(req);
    auto fut = job.promise.get_future();
    {
        std::lock_guard lock(jobsMutex_);
        jobs_.push(std::move(job));
    }
    jobsCv_.notify_one();
    return fut;
}
```

#### 启动 worker：`CreateProcess` + 管道重定向

```cpp
bool ShaderCompileManager::LaunchWorker(Worker& w) {
    SECURITY_ATTRIBUTES sa { sizeof(sa), nullptr, TRUE };   // 句柄可继承（关键！）

    // 1. 创建两对管道（一对给 worker stdin，一对给 stdout）
    HANDLE childStdinRead = nullptr, childStdinWrite = nullptr;
    HANDLE childStdoutRead = nullptr, childStdoutWrite = nullptr;
    CreatePipe(&childStdinRead,  &childStdinWrite,  &sa, 1 << 20);   // 1 MB 缓冲
    CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 1 << 20);

    // 主进程端的句柄要清掉继承标志（否则主进程意外退出时句柄泄漏）
    SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0);

    // 2. 构造 CreateProcess 的 STARTUPINFO，重定向 stdin/stdout
    STARTUPINFO si {}; si.cb = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = childStdinRead;     // worker 的 stdin = 主进程写的端
    si.hStdOutput = childStdoutWrite;   // worker 的 stdout = 主进程读的端
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);  // stderr 直通，调试日志看得到

    PROCESS_INFORMATION pi {};
    std::string cmd = "ShaderCompileWorker.exe";
    BOOL ok = CreateProcessA(
        nullptr,                       // 应用名（用命令行）
        cmd.data(),                    // 命令行
        nullptr, nullptr,              // 进程/线程属性
        TRUE,                          // ★ 继承句柄（必须 TRUE，否则管道句柄进不去子进程）
        CREATE_NO_WINDOW,              // 不弹黑窗口
        nullptr, nullptr,              // 环境、工作目录
        &si, &pi);

    if (!ok) {
        ME_LOG_ERROR("CreateProcess failed: %u", GetLastError());
        return false;
    }

    // 3. 主进程关闭自己不需要的 worker 端句柄（否则僵尸句柄泄漏）
    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);
    CloseHandle(pi.hThread);   // worker 主线程 handle 不用，但必须 Close

    // 4. 记到 Worker 结构
    w.hProcess = pi.hProcess;
    w.hPipeIn  = childStdinWrite;    // 主进程写 → worker stdin
    w.hPipeOut = childStdoutRead;    // worker stdout → 主进程读
    w.busy = false;
    w.alive = true;
    return true;
}
```

#### Dispatch：发任务给 worker

```cpp
ShaderBytecode ShaderCompileManager::DispatchToWorker(
    Worker& w, const ShaderCompileRequest& req)
{
    // 1. 序列化请求（按协议帧格式）
    ShaderCompileProtocol::RequestHeader header {};
    header.magic       = ShaderCompileProtocol::MAGIC;
    header.version     = ShaderCompileProtocol::VERSION;
    header.jobId       = GenerateJobId();   // 简单用全局自增
    std::strncpy(header.entryPoint, req.entryPoint.c_str(), 63);
    std::strncpy(header.target,     req.target.c_str(),     15);
    header.defineCount = (uint32_t)req.defines.size();
    header.sourceSize  = (uint32_t)req.hlslSource.size();
    header.bDebug      = req.bDebug ? 1 : 0;

    // 2. 写请求（头 + 宏 + 源码）
    WriteExact(w.hPipeIn, &header, sizeof(header));
    if (header.defineCount > 0)
        WriteExact(w.hPipeIn, req.defines.data(),
                   header.defineCount * sizeof(ShaderCompileProtocol::ShaderMacro));
    WriteExact(w.hPipeIn, req.hlslSource.data(), header.sourceSize);

    // 3. 读响应（阻塞，等 worker 编完）
    ShaderCompileProtocol::ResponseHeader resp {};
    if (!ReadExact(w.hPipeOut, &resp, sizeof(resp))) {
        ME_LOG_ERROR("Worker died mid-task, pid=%u", GetProcessId(w.hProcess));
        w.alive = false;
        ShaderBytecode fail;
        fail.errorMessage = "Worker 进程崩溃（HLSL 可能非法导致 fxc AV）";
        return fail;
    }

    // 4. 读字节码 / 错误文本
    ShaderBytecode result;
    if (resp.bytecodeSize > 0) {
        std::vector<uint8_t> bc(resp.bytecodeSize);
        ReadExact(w.hPipeOut, bc.data(), resp.bytecodeSize);
        D3DCreateBlob(resp.bytecodeSize, &result.bytecode);
        memcpy(result.bytecode->GetBufferPointer(), bc.data(), resp.bytecodeSize);
    }
    if (resp.errorSize > 0) {
        result.errorMessage.resize(resp.errorSize);
        ReadExact(w.hPipeOut, result.errorMessage.data(), resp.errorSize);
    }
    return result;
}
```

> **worker 崩溃恢复**：上面 `w.alive = false` 后，调度线程的"找空闲 worker"会跳过它。需要单独的**看门狗线程**定期检查 worker 是否还活着（`WaitForSingleObject(w.hProcess, 0) == WAIT_OBJECT_0` 表示已退出），重启新的——否则一个 worker 崩了，可用 worker 数就少 1。

---

### 9. Shader 缓存

**目的**：HLSL 没变就别再编译——开发迭代时这是**最大的性能优化**。改一个节点重编译，99% 的 shader 没变，命中缓存秒级完成。

#### 缓存 key 设计（关键，**不能漏字段**）

```cpp
// 缓存 key：HLSL + 宏 + target + entry + debug 标志的 hash
// 任何一个变了就要重编——漏一个会导致用错字节码（典型坑）
uint64_t ShaderCache::MakeKey(const ShaderCompileRequest& req) {
    // 用 FNV-1a（简单稳定）；UE 用 CityHash64
    uint64_t h = 14695981039346656037ULL;   // FNV-1a 偏移基

    auto mix = [&](const void* data, size_t size) {
        auto bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            h ^= bytes[i];
            h *= 1099511628211ULL;   // FNV-1a 质数
        }
    };

    // 顺序无关紧要，但每个字段都必须 mix 进去
    mix(req.hlslSource.data(), req.hlslSource.size());
    mix(req.entryPoint.data(), req.entryPoint.size());
    mix(req.target.data(),     req.target.size());
    for (const auto& d : req.defines) {
        mix(d.name.data(),  d.name.size());
        mix(d.value.data(), d.value.size());
    }
    mix(&req.bDebug, sizeof(req.bDebug));
    return h;
}
```

**哪些字段进 key**：

| 字段 | 进 key？ | 为什么 |
|------|---------|--------|
| HLSL 源码 | ✅ | 内容变了就要重编 |
| entry point | ✅ | 同一 HLSL 可能多个入口（虽然教学版没有）|
| target (`vs_5_0` 等) | ✅ | VS 和 PS 字节码完全不同 |
| 宏定义 | ✅ | **变体核心**——同 HLSL 不同宏 → 不同字节码 |
| bDebug | ✅ | 调试版带符号信息，字节码不同 |
| debugName | ❌ | 只影响错误信息显示，不影响字节码 |
| jobId | ❌ | 主进程内部分配，不影响内容 |

**典型坑**：漏了 `target` 进 key → 第一次编 PS 缓存了，第二次编 VS 命中 PS 缓存 → 渲染崩。**key 必须包含所有影响字节码的字段**。

#### 缓存存储：内存 + 磁盘

```cpp
class ShaderCache {
public:
    // 查询：命中返回字节码，未命中返回 nullptr
    std::shared_ptr<ID3DBlob> Find(uint64_t key);

    // 插入：编译成功后调
    void Insert(uint64_t key, std::shared_ptr<ID3DBlob> bytecode);

    // 持久化：跨编辑器重启有效
    bool SaveToDisk(const std::string& path);   // 一个文件，存 {key, bytecode} 列表
    bool LoadFromDisk(const std::string& path);

private:
    std::mutex mutex_;   // 多线程访问要加锁
    std::unordered_map<uint64_t, std::shared_ptr<ID3DBlob>> entries_;
};
```

**磁盘持久化格式**：简单——一个文件头 + 一堆 `{uint64_t key, uint32_t size, uint8_t data[size]}` 条目。UE5 用 `FShaderCodeArchive`（`Engine/Source/Runtime/RenderCore/Private/ShaderCodeArchive.cpp`），支持增量更新、版本控制、压缩。教学版用简单的全量覆盖。

**缓存失效**：

- **HLSL 改了**：自然失效（hash 变了，找不到 → 重编）
- **shader 模型升级**（SM 5.0 → 6.0）：target 字段变了 → hash 变了 → 失效
- **fxc/DXC 版本变了**：**这个不会自动失效**——如果项目升级了 Windows SDK，fxc 编出来的字节码可能不兼容，但 hash 不变。**对策**：把 SDK 版本号 mix 进 key，或检测 SDK 升级时清缓存。
- **手动清**：调试期可以删磁盘文件强制全重编。

---

### 10. 错误回传 + 关联课19

#### fxc / DXC 错误格式

```
shader.hlsl(45,1): error X3004: undeclared identifier 'foo'
shader.hlsl(50,5): warning X3206: implicit truncation of vector type
```

格式：`<filename>(<line>,<col>): <severity> X<code>: <message>`

- `<filename>`：编译时传的 `debugName`（不一定是真文件——教学版传 "shader.hlsl"）
- `<line>,<col>`：HLSL 源码里的位置（**不是材质图里的节点位置**）
- `<severity>`：`error` / `warning`
- `X<code>`：错误码（X3004 = undeclared identifier，X3000 = syntax error，X3013 = 函数不存在等）

#### 主进程解析（关联课19）

```cpp
struct ShaderError {
    int line;
    int column;
    enum class Severity { Error, Warning } severity;
    int code;             // X3004 → 3004
    std::string message;
};

std::vector<ShaderError> ParseFXCErrors(const std::string& text) {
    std::vector<ShaderError> errors;
    // 正则匹配 fxc 错误格式
    std::regex re(R"(([\w.]+)\((\d+),(\d+)\):\s*(error|warning)\s+X(\d+):\s*(.+))");
    std::smatch m;
    std::string s = text;
    while (std::regex_search(s, m, re)) {
        ShaderError e;
        e.line     = std::stoi(m[2]);
        e.column   = std::stoi(m[3]);
        e.severity = (m[4] == "error") ? ShaderError::Severity::Error
                                        : ShaderError::Severity::Warning;
        e.code     = std::stoi(m[5]);
        e.message  = m[6];
        errors.push_back(e);
        s = m.suffix();
    }
    return errors;
}
```

#### HLSL 行号 → 材质图节点定位（关联课19，接口对齐）

**核心难点**：fxc 报的行号是 HLSL 文件里的，但用户看的是材质图。要把 HLSL 行号反查到**哪个节点生成了这行**——这就要求 `MaterialCompiler::GenerateCode`（课8）在生成 HLSL 时**留 mapping**：

```cpp
// 课8 的 MaterialCompiler 扩展：HLSL 行号 ↔ 节点 ID 映射
struct SourceMap {
    struct Entry {
        int hlslLine;             // HLSL 文件中的行号
        std::string nodeId;       // 生成这行的节点 UUID
        std::string nodeTypeName; // 节点类型（如 "Add"）
    };
    std::vector<Entry> entries;
};

// 课8 CompileResult 要加 SourceMap 字段：
// struct CompileResult {
//     bool success;
//     std::string hlsl_code;
//     std::string error_message;
//     SourceMap sourceMap;    // ← 新增，关联课16 的错误回传 + 课19 的错误诊断
// };

// GenerateCode 时每写一行就 push 一条 Entry
// 编译失败时，根据 fxc 报的行号反查节点 ID → 在 UI 高亮该节点（课19 错误诊断）
```

> **课16 ↔ 课8 ↔ 课19 三方接口对齐**：`MaterialCompiler::Compile`（课8）返回的 `CompileResult` 要加 `SourceMap` 字段；`ShaderCompileManager`（课16）拿到 fxc 错误后用 `SourceMap` 反查节点 ID；UI 错误面板（课19）消费 `SourceMap` 高亮节点。**三个地方接口要对齐**——任一方改了字段另外两处要跟着改。

#### 错误显示在 UI（课19 实现）

UI 错误面板（课19）的最终形态：

```
[Shader 编译错误]
  → 材质节点「Add」(id=abc-123)
    shader.hlsl(45): error X3004: undeclared identifier 'foo'
    提示：检查 Add 节点的输入引脚是否连接正确
```

> **UE 对照**：UE 的 `HandleMaterialCompilationErrors`（`Engine/Source/Runtime/Engine/Private/Materials/Material.cpp`）干这事，把 fxc 错误反查到 `UMaterialExpression`，编辑器高亮出错节点。

---

### 11. 学习点（C++ 并发编程，通用技能）

这块是项目里**唯一系统学并发编程**的地方。每个组件对应一个标准库特性：

| 组件 | 学到 | 标准/平台 API |
|------|------|--------------|
| 任务队列 | 生产者-消费者模式 | `std::queue` + `std::mutex` + `std::condition_variable` |
| 调度线程 | 后台 daemon thread | `std::thread` + `std::atomic<bool>` 退出标志 |
| 异步接口 | 阻塞取结果 | `std::future` / `std::promise` |
| Worker 池 | 资源池管理 | `std::vector` + `std::mutex` 保护共享状态 |
| 启动 worker | 进程创建 | Windows: `CreateProcess` / POSIX: `fork`+`exec` |
| IPC | 管道通信 | Windows: `CreatePipe` + `ReadFile`/`WriteFile` / POSIX: `pipe()` + `read`/`write` |
| 缓存 | 线程安全容器 | `std::unordered_map` + `std::mutex`（或 `std::shared_mutex` 读写锁）|

**关键概念**：

- **数据竞争**：两个 thread 同时写 `jobs_` 队列会崩——必须 `std::lock_guard` 加锁
- **死锁**：两个 mutex 互相等——锁的获取顺序必须全局一致
- **虚假唤醒**：`condition_variable::wait` 可能没有原因醒来——必须用 lambda 形式 `cv.wait(lock, predicate)` 检查条件
- **句柄继承**（Windows 特有）：`CreateProcess` 不开 `bInheritHandles`，子进程拿不到管道句柄——经典坑
- **二进制模式**（Windows 特有）：stdin/stdout 默认文本模式，会改 `\n` → `\r\n`，传输字节码必须 `_setmode(_O_BINARY)`

**这些技能迁移性极强**——以后写"加载大量资源"、"网络 IO 并发"、"后台任务并行"都用得到同样的模式。

---

### 12. UE5 对照（相对 `Engine/` 路径）

| 本节概念 | UE5 对应 | 位置 |
|---------|---------|------|
| `ShaderCompileWorker.exe` | 同名（独立 program）| `Engine/Source/Programs/ShaderCompileWorker/` |
| `ShaderCompileManager` | `FShaderCompileManager` | `Engine/Source/Runtime/RenderCore/Private/ShaderCompiler.cpp` |
| Worker 进程管理 | `FShaderCompileThreadRunnable` | 同上 |
| `ShaderCompileRequest` | `FShaderCompileJob` + `FShaderCompilerEnvironment` | `Engine/Source/Runtime/RenderCore/Public/ShaderCompilerCore.h` |
| 宏定义（变体）| `FShaderCompilerEnvironment::GetDefinitions()` 返回 `TMap<FString, FString>` | 同上 |
| Shader 缓存 | `FShaderCodeArchive` | `Engine/Source/Runtime/RenderCore/Private/ShaderCodeArchive.cpp` |
| 错误回传 | `FShaderCompilerOutput::GetErrors()` 返回 `TArray<FShaderCompilerError>` | 同 ShaderCompilerCore |
| HLSL → 字节码 | `CompileD3D11Shader_DXCompiler` / `CompileShader_DX` | `Engine/Source/Runtime/RHI/Private/D3D11ShaderCompiler.cpp` 等 |
| 任务分发 | `FShaderCompilationTask` | `Engine/Source/Runtime/RenderCore/Private/ShaderCompiler.cpp` |

**UE5 vs 教学版的 3 个关键差异**：

1. **UE5 跨平台 + 多后端**：一个 shader 可能编到 DX12 / Vulkan / Metal / 各主机。worker 进程按"编译目标"分组（如 `ShaderCompileWorker.exe` 处理 DX，另一个 worker 处理 Vulkan）。教学版**只编 DX12 + fxc**，单一 worker 类型，简单。
2. **UE5 异步编译管线复杂**：shader 编译完还要做"合并到 shader pipeline"、"上传到 GPU"、"PSO 缓存"等步骤。UE5 的 `FShaderCompilationTask` 是异步的，主线程不阻塞。教学版的 `SubmitAsync` 返回 `future`，调用者自己控制等不等。
3. **UE5 shader 缓存（`FShaderCodeArchive`）支持增量更新**：只重编变化的 shader，添加到现有存档。教学版用全量覆盖（每次保存重写整个缓存文件），简单。

**搜索关键词**（UE 源码）：`FShaderCompileManager`、`FShaderCompileJob`、`FShaderCompilerEnvironment`、`ShaderCompileWorker`、`CompileD3D11Shader_DXCompiler`、`FShaderCodeArchive`。

---

### 13. 已踩坑 / 注意

| 坑 | 现象 | 对策 |
|----|------|------|
| stdin/stdout 文本模式 | 写字节码后读出来 `\n` 变 `\r\n`，DXBC 损坏，PSO 创建失败 | worker main 开头：`_setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);` |
| `CreateProcess` 没开 `bInheritHandles` | worker 的 stdin/stdout 句柄无效，读 0 字节立即 EOF | 第 5 参数传 `TRUE`（继承）|
| 管道缓冲区不够 | `WriteFile` 大字节码时阻塞，主进程读不到完整数据 | `CreatePipe` 第 4 参数传 1 MB+ |
| 缓存 key 漏字段 | 改了宏没重编，用错字节码渲染乱 | key 必须含 HLSL + entry + target + 宏 + bDebug（见第 9 节表）|
| Worker 卡死 | 偶发 fxc 死循环，调度线程永远等不到响应 | 实现看门狗：`WaitForSingleObject` 超时后 `TerminateProcess` 杀掉重启 |
| Worker 崩溃 | 非法 HLSL 导致 fxc AV | worker 永远不 `exit(1)`，崩溃由主进程检测（响应读 EOF）后重启该 worker |
| 进程退出码混用 | 编译错误用 exit code 表达 → 主进程无法区分"编译错误"和"worker 崩溃"| 编译结果只走响应通道（`success=0`），exit code 永远是 0（worker 干净退出）|
| 大宏数量爆栈 | 一个 shader 数百宏 → `ShaderMacro[N]` 数组栈分配 | 用堆分配（`std::vector`）|
| 调度单 dispatcher 瓶颈 | 任务多时调度线程成为瓶颈 | 改为线程池：每 worker 一个专责通信 thread |
| 关闭顺序错 | 主进程先退出，worker 永远不退（管道泄漏）| Shutdown 先关管道写端 → 等 worker 自然退出 → CloseHandle |
| Shader cache 文件版本 | 项目升级后老缓存格式读不了 | 缓存文件加 magic + version，version 不匹配就忽略重建 |
| fxc 错误的列号偶尔为 0 | 正则解析失败 | 列号字段 optional，匹配不到按 0 处理 |

---

### 14. 集成步骤（按依赖顺序）

1. **协议头文件**（`src/ShaderCompile/Public/ShaderCompileProtocol.h`）：定义 `RequestHeader` / `ResponseHeader` / `ShaderMacro`，主进程和 worker 共享
2. **Worker exe**（`src/ShaderCompileWorker/main.cpp`）：实现第 4 节的 `CompileHLSL` + 第 7 节的 main 循环；先用 main 单独跑通"读 stdin → 编译 → 写 stdout"
3. **CMake 加子项目**：把 `ShaderCompileWorker` 加成独立 target，输出 `ShaderCompileWorker.exe`；放在主 exe 同目录（`CreateProcess` 用相对路径找得到）
4. **`ShaderCompileManager`**（主进程）：实现第 8 节——`Initialize`（启动 N worker）、`SubmitAsync`（投递任务）、`DispatchToWorker`（IPC 通信）；用单元测试验证"提交 N 任务，全部完成"
5. **`ShaderCache`**：实现第 9 节——内存 map + 磁盘持久化；测试"同 HLSL 二次提交命中缓存"
6. **替换 `Shader::CompileFromSource`**：把课16 单进程 `D3DCompile` 调用替换成 `ShaderCompileManager::SubmitSync(req)`，输出仍是 `ShaderBytecode`（结构不变，调用方零修改）
7. **错误解析**：实现第 10 节的 `ParseFXCErrors`，把错误文本解析成结构化 `ShaderError` 列表（先输出到 logger，课19 接 UI 面板）
8. **SourceMap 收集**：在课8 的 `MaterialCompiler::GenerateCode` 加行号映射（每行 HLSL 记录生成它的节点 ID），加到 `CompileResult` 字段——课19 反查节点用
9. **看门狗**：实现 worker 心跳/超时检测，崩溃自动重启（第 13 节"worker 卡死"）
10. **DXC 路径**（可选）：第 5 节的 DXC 集成，留作 SM 6.0+ 扩展

---

## 完成标志

- [ ] Shader::CompileFromSource 能编译 HLSL 顶点/像素着色器
- [ ] 根签名创建成功（包含 CBV）
- [ ] PSO 创建成功（绑定 VS/PS/输入布局）
- [ ] 球体/立方体/平面/圆柱网格生成正确
- [ ] Mesh::Upload 正确上传数据到 GPU 默认堆
- [ ] Camera 轨道控制正确（旋转/平移/缩放）
- [ ] Camera 常量缓冲创建和更新正确
- [ ] 投影矩阵使用 LH_ZO 正确（DX12 要求）

### 多进程 Shader 编译集成（深度扩展）

- [ ] ShaderCompileProtocol.h 定义 RequestHeader / ResponseHeader / ShaderMacro（主进程 worker 共享）
- [ ] ShaderCompileWorker.exe 独立 target 能跑通"读 stdin → D3DCompile → 写 stdout"
- [ ] Worker main 切到二进制模式（`_setmode(_O_BINARY)`）防 `\r\n` 损坏字节码
- [ ] ShaderCompileManager 启动 N 个 worker（`CreateProcess` + 管道重定向）
- [ ] SubmitAsync / SubmitSync 接口正确（`future`/`promise` 跨线程传结果）
- [ ] 调度线程 + 任务队列（mutex + condition_variable + 防虚假唤醒 lambda）
- [ ] ShaderCache key 含 HLSL + entry + target + 宏 + bDebug（任一字段变就失效）
- [ ] ShaderCache 内存命中跳过编译 + 磁盘持久化跨编辑器重启有效
- [ ] Shader::CompileFromSource 内部从单进程 D3DCompile 替换为 ShaderCompileManager::SubmitSync（ShaderBytecode 结构不变）
- [ ] ParseFXCErrors 解析 fxc 错误文本为结构化 ShaderError 列表
- [ ] 课8 的 MaterialCompiler::Compile 在 CompileResult 加 SourceMap（HLSL 行号 ↔ 节点 ID），供课19 反查
- [ ] Worker 崩溃检测（响应读 EOF → 标记 alive=false）+ 看门狗超时重启
- [ ] 课15 "扩展预告：多进程(块7)" 占位的落地到此节（落地页统一指课16）
