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

## UE5 参考

- `Engine/Source/Runtime/Engine/Private/StaticMesh.cpp` — 网格生成
- `Engine/Source/Runtime/Engine/Private/MaterialEditorRender.cpp` — 材质预览渲染
- 搜索 `DrawSphere` / `DrawCube` — 预览几何体
- `Engine/Source/Runtime/Engine/Private/ShaderCompiler/` — 着色器编译
- `Engine/Source/Runtime/RHI/` — RHI 抽象层（DX12/OpenGL/Vulkan）

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
