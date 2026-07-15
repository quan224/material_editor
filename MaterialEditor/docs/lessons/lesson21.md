# 课21：外部模型与贴图加载 — 拖拽导入、纹理渲染、网络下载

## 目标

为材质编辑器添加外部资源加载能力：用 Assimp 加载任意 3D 模型文件（OBJ/FBX/glTF），用 stb_image 加载纹理图片，支持从文件管理器或浏览器拖拽文件到编辑器窗口，以及从 URL 下载贴图资源。最终实现：拖入一个 glTF 模型 → 自动加载网格和纹理 → 在预览视口中用当前材质渲染。

---

## 背景知识

### UE5 是怎么做的？

UE5 中导入模型和贴图的方式是：
1. 把文件拖进 Content Browser（内容浏览器）
2. UE5 的 `UImporter` 系统根据文件扩展名选择对应的导入器
3. 模型用 `UFbxFactory` / `UObjFactory` 解析，生成 `UStaticMesh` / `USkeletalMesh`
4. 贴图用 `UTextureFactory` 解析，生成 `UTexture2D`
5. 所有导入的资源都序列化为 `.uasset` 二进制格式

我们的流程类似但更简单：
1. 从文件管理器/浏览器拖文件到编辑器窗口
2. 根据扩展名判断类型（模型 or 贴图）
3. 模型 → Assimp 解析为 `Mesh`，替换预览网格
4. 贴图 → stb_image 解码为像素数据，创建 DX12 纹理资源，连到 TextureSample 节点

### 需要的第三方库

| 库 | 用途 | 安装方式 |
|----|------|---------|
| Assimp | 加载 3D 模型文件（OBJ/FBX/glTF/COLLADA 等 100+ 种格式） | `vcpkg install assimp:x64-windows` |
| stb_image | 加载图片文件（PNG/JPG/TGA/BMP/PSD/GIF/HDR 等） | 单头文件，直接放到项目中 |
| Qt Network | HTTP 下载（从 URL 获取模型/贴图） | vcpkg Qt6 已包含，需在 CMake 中添加组件 |

### DX12 纹理资源的创建流程

纹理在 DX12 中比顶点缓冲复杂得多，因为涉及**多行数据对齐**（每行像素数据必须对齐到 256 字节边界）。流程如下：

```
图片文件（PNG/JPG）
    ↓ stb_image 解码
像素数据（RGBA, width × height × 4 字节）
    ↓ 行对齐处理（每行补齐到 256 字节倍数）
上传堆（ID3D12Resource, UPLOAD heap）
    ↓ CopyTextureRegion（GPU 拷贝）
默认堆纹理（ID3D12Resource, DEFAULT heap）
    ↓ 创建 SRV（Shader Resource View）
描述符堆（ID3D12DescriptorHeap, CBV_SRV_UAV 类型）
    ↓ SetDescriptorHeaps + SetGraphicsRootDescriptorTable
像素着色器中通过 register(t0) 采样
```

对比顶点缓冲只需要 `CopyBufferRegion`，纹理需要 `CopyTextureRegion`，并且要用**脚距（Footprint）**来描述子资源的布局。

---

## 操作步骤

### 1. 安装依赖

```bash
# 安装 Assimp
vcpkg install assimp:x64-windows

# stb_image 不需要安装，直接下载头文件
# 从 https://github.com/nothings/stb 下载 stb_image.h
```

### 2. 添加 stb_image 到项目

在项目中创建 `src/ThirdParty/stb_image.h` 和 `src/ThirdParty/stb_image.cpp`：

**stb_image.cpp**（实现文件，整个项目只写一次）：
```cpp
// stb_image 的实现必须且只能在一个 .cpp 文件中
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb_image.h"
```

**说明**：stb 系列库的设计模式是"单头文件库"（STB = Sean's ToolBox）。头文件中同时包含声明和实现，通过在**恰好一个** `.cpp` 文件中定义 `STB_IMAGE_IMPLEMENTATION` 宏来编译实现代码。如果在多个 `.cpp` 中定义，会报重复定义的链接错误。

### 3. 更新 CMakeLists.txt

```cmake
# 在 find_package 中添加 Qt Network 和 Assimp
find_package(Qt6 COMPONENTS Core Widgets Network REQUIRED)
find_package(assimp CONFIG REQUIRED)

# 在 target_link_libraries 中添加
target_link_libraries(MaterialEditor PRIVATE
    Qt6::Core Qt6::Widgets Qt6::Network
    glm::glm nlohmann_json::nlohmann_json
    assimp::assimp
    # DX12 库
    d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib
)
```

### 4. 创建文件

```
src/ThirdParty/stb_image.h            — stb 单头文件库（从 GitHub 下载）
src/ThirdParty/stb_image.cpp          — 实现编译入口（上面写的那个）

src/Renderer/Public/TextureLoader.h   — 贴图加载器（stb_image 封装）
src/Renderer/Private/TextureLoader.cpp
src/Renderer/Public/DX12Texture.h     — DX12 纹理资源封装
src/Renderer/Private/DX12Texture.cpp

src/Renderer/Public/MeshLoader.h      — 模型加载器（Assimp 封装）
src/Renderer/Private/MeshLoader.cpp

src/Core/Public/NetworkDownloader.h   — HTTP 下载器（Qt Network 封装）
src/Core/Private/NetworkDownloader.cpp

src/UI/Private/DropHandler.h          — 拖拽事件处理
src/UI/Private/DropHandler.cpp
```

---

## 第一部分：贴图加载（stb_image + DX12 纹理）

### 5. TextureLoader.h — 贴图文件加载器

```cpp
#pragma once
#include <string>
#include <vector>
#include <cstdint>

// 贴图像素数据（CPU 端，在上传给 GPU 之前暂存）
struct RawTexture {
    int width = 0;
    int height = 0;
    int channels = 0;            // 通常是 4（RGBA）
    std::vector<uint8_t> pixels; // width × height × channels 字节

    bool IsValid() const { return width > 0 && height > 0 && !pixels.empty(); }

    // 计算总字节数
    size_t DataSize() const {
        return (size_t)width * height * channels;
    }
};

class TextureLoader {
public:
    // 从文件加载贴图
    // forceRGBA = true 时强制转换为 RGBA 4 通道（DX12 纹理通常需要）
    static RawTexture LoadFromFile(const std::string& filePath,
                                    bool forceRGBA = true);

    // 从内存数据加载贴图（用于网络下载的数据）
    static RawTexture LoadFromMemory(const uint8_t* data, size_t size,
                                      bool forceRGBA = true);

    // 获取支持的图片格式（用于拖拽时的文件类型判断）
    static bool IsSupportedFormat(const std::string& extension);
};
```

### 6. TextureLoader.cpp

```cpp
#include "Renderer/Public/TextureLoader.h"
#include "ThirdParty/stb_image.h"
#include "Core/Public/Logger.h"
#include <algorithm>
#include <set>

RawTexture TextureLoader::LoadFromFile(const std::string& filePath,
                                         bool forceRGBA) {
    RawTexture tex;

    // stb_image 自动根据文件内容判断格式，不需要我们指定
    int desiredChannels = forceRGBA ? 4 : 0;
    uint8_t* data = stbi_load(filePath.c_str(),
                               &tex.width, &tex.height,
                               &tex.channels, desiredChannels);

    if (!data) {
        ME_LOG_ERROR("Failed to load texture: %s", filePath.c_str());
        ME_LOG_ERROR("  stb_image error: %s", stbi_failure_reason());
        return tex;
    }

    // 如果指定了 desiredChannels = 4，channels 会被强制设为 4
    if (forceRGBA) tex.channels = 4;

    // 把数据拷贝到 vector（stbi_load 返回的是 malloc 分配的内存）
    size_t dataSize = (size_t)tex.width * tex.height * tex.channels;
    tex.pixels.assign(data, data + dataSize);
    stbi_image_free(data);

    ME_LOG_INFO("Texture loaded: %s (%dx%d, %d channels, %zu bytes)",
                filePath.c_str(), tex.width, tex.height,
                tex.channels, tex.pixels.size());

    return tex;
}

RawTexture TextureLoader::LoadFromMemory(const uint8_t* data, size_t size,
                                           bool forceRGBA) {
    RawTexture tex;

    int desiredChannels = forceRGBA ? 4 : 0;
    uint8_t* pixels = stbi_load_from_memory(
        data, (int)size,
        &tex.width, &tex.height,
        &tex.channels, desiredChannels);

    if (!pixels) {
        ME_LOG_ERROR("Failed to load texture from memory (%zu bytes)", size);
        return tex;
    }

    if (forceRGBA) tex.channels = 4;

    size_t dataSize = (size_t)tex.width * tex.height * tex.channels;
    tex.pixels.assign(pixels, pixels + dataSize);
    stbi_image_free(pixels);

    return tex;
}

bool TextureLoader::IsSupportedFormat(const std::string& extension) {
    // 转小写
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static std::set<std::string> supported = {
        "png", "jpg", "jpeg", "tga", "bmp", "psd",
        "gif", "hdr", "pic", "pnm"
    };

    return supported.count(ext) > 0;
}
```

**讲解**：
- `stbi_load` 自动识别文件格式（PNG、JPG 等），返回解码后的像素数据
- `desiredChannels = 4` 强制转为 RGBA 四通道，因为 DX12 纹理格式 `DXGI_FORMAT_R8G8B8A8_UNORM` 要求每像素 4 字节
- `stbi_image_free` 释放 stb 内部分配的内存（不能用 `delete`，因为 stb 用的是 `malloc`）
- `stbi_load_from_memory` 从内存中加载，用于网络下载的场景（数据在内存中，没有磁盘文件）

---

### 7. DX12Texture.h — DX12 纹理资源封装

```cpp
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include "Renderer/Public/TextureLoader.h"

using Microsoft::WRL::ComPtr;

class DX12Texture {
public:
    DX12Texture() = default;
    ~DX12Texture();

    // 从 RawTexture 创建 DX12 纹理（完整的上传流程）
    bool Create(ID3D12Device* device,
                ID3D12GraphicsCommandList* cmdList,
                ID3D12Resource* uploadBuffer,
                size_t& uploadOffset,
                const RawTexture& rawTex,
                const std::string& debugName = "");

    // 从文件创建（加载 + 上传一步完成）
    bool CreateFromFile(ID3D12Device* device,
                        ID3D12GraphicsCommandList* cmdList,
                        ID3D12Resource* uploadBuffer,
                        size_t& uploadOffset,
                        const std::string& filePath);

    // 创建 SRV 描述符（必须在纹理上传之后调用）
    bool CreateSRV(ID3D12Device* device,
                   ID3D12DescriptorHeap* srvHeap,
                   int descriptorIndex);

    // 设置到命令列表（绑定到像素着色器）
    void SetToCommandList(ID3D12GraphicsCommandList* cmdList,
                          ID3D12DescriptorHeap* srvHeap,
                          int rootParameterIndex,
                          int descriptorIndex);

    // 资源释放
    void Destroy();

    // 状态查询
    bool IsValid() const { return texture_ != nullptr; }
    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }

private:
    ComPtr<ID3D12Resource> texture_;        // 默认堆上的纹理资源
    ComPtr<ID3D12Resource> uploadResource_; // 上传堆资源（Create 中临时持有）

    int width_ = 0;
    int height_ = 0;

    // 计算对齐后的行字节数（DX12 要求每行对齐到 256 字节边界）
    static UINT AlignRowPitch(UINT width, UINT bytesPerPixel);
};
```

### 8. DX12Texture.cpp

这是本课最复杂的部分——DX12 纹理上传。

```cpp
#include "Renderer/Public/DX12Texture.h"
#include "Core/Public/Logger.h"
#include <d3dx12.h>

DX12Texture::~DX12Texture() {
    Destroy();
}

void DX12Texture::Destroy() {
    texture_.Reset();
    uploadResource_.Reset();
    width_ = 0;
    height_ = 0;
}

UINT DX12Texture::AlignRowPitch(UINT width, UINT bytesPerPixel) {
    // DX12 要求纹理数据的行间距（row pitch）是 256 字节的倍数
    // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT = 256
    UINT rowPitch = width * bytesPerPixel;
    return (rowPitch + 255) & ~255;  // 向上对齐到 256 的倍数
}

bool DX12Texture::Create(ID3D12Device* device,
                           ID3D12GraphicsCommandList* cmdList,
                           ID3D12Resource* uploadBuffer,
                           size_t& uploadOffset,
                           const RawTexture& rawTex,
                           const std::string& debugName) {
    if (!rawTex.IsValid()) {
        ME_LOG_ERROR("Cannot create DX12 texture from invalid RawTexture");
        return false;
    }

    width_ = rawTex.width;
    height_ = rawTex.height;

    // === 第1步：创建目标纹理资源（默认堆）===
    // 纹理资源和缓冲资源不同，用 Tex2D 描述
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width_;
    texDesc.Height = height_;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;       // 暂不生成 mipmap
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // 4 通道 8 位无符号整数
    texDesc.SampleDesc = { 1, 0 };
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;  // GPU 自选最优布局
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,  // 初始状态：等待复制
        nullptr,
        IID_PPV_ARGS(&texture_));

    if (FAILED(hr)) {
        ME_LOG_ERROR("Failed to create texture resource (0x%08X)", hr);
        return false;
    }

    if (!debugName.empty()) {
        texture_->SetName(
            std::wstring(debugName.begin(), debugName.end()).c_str());
    }

    // === 第2步：计算纹理数据的布局（脚距 / Footprint）===
    // DX12 要求我们告诉它上传数据的确切布局
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64_t uploadSize = 0;

    device->GetCopyableFootprints(
        &texDesc,
        0,      // 第一个子资源索引（只有一个 mip level）
        1,      // 子资源数量
        0,      // 起始偏移（由函数返回）
        &footprint,
        &numRows,
        nullptr,
        &uploadSize);

    // === 第3步：准备上传数据（行对齐）===
    // stb_image 返回的像素数据是紧密排列的，每行 width × 4 字节
    // DX12 要求每行是 256 字节的倍数
    UINT srcRowPitch = width_ * 4;                // 原始行宽（每像素 4 字节）
    UINT dstRowPitch = footprint.Footprint.RowPitch;  // 对齐后的行宽

    // 在上传堆中分配空间
    // 注意：这里使用传入的 uploadBuffer，偏移量由外部管理
    // 但纹理上传需要特殊对齐，所以实际上传数据用独立的 uploadResource 更安全
    auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadResource_));

    if (FAILED(hr)) {
        ME_LOG_ERROR("Failed to create texture upload resource");
        return false;
    }

    // 把像素数据复制到上传堆（逐行复制，处理行对齐）
    uint8_t* uploadData = nullptr;
    uploadResource_->Map(0, nullptr, reinterpret_cast<void**>(&uploadData));

    const uint8_t* srcData = rawTex.pixels.data();
    for (UINT row = 0; row < numRows; row++) {
        memcpy(uploadData + row * dstRowPitch,
               srcData + row * srcRowPitch,
               srcRowPitch);  // 只复制有效数据，对齐部分自动是 0
    }

    uploadResource_->Unmap(0, nullptr);

    // === 第4步：执行 GPU 拷贝（上传堆 → 默认堆纹理）===
    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = uploadResource_.Get();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = texture_.Get();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = 0;

    cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

    // === 第5步：转换纹理状态为像素着色器可读（PIXEL_SHADER_RESOURCE）===
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        texture_.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    ME_LOG_INFO("DX12 texture created: %dx%d (upload: %llu bytes)",
                width_, height_, uploadSize);
    return true;
}

bool DX12Texture::CreateFromFile(ID3D12Device* device,
                                   ID3D12GraphicsCommandList* cmdList,
                                   ID3D12Resource* uploadBuffer,
                                   size_t& uploadOffset,
                                   const std::string& filePath) {
    RawTexture rawTex = TextureLoader::LoadFromFile(filePath);
    if (!rawTex.IsValid()) return false;

    return Create(device, cmdList, uploadBuffer, uploadOffset, rawTex, filePath);
}

bool DX12Texture::CreateSRV(ID3D12Device* device,
                              ID3D12DescriptorHeap* srvHeap,
                              int descriptorIndex) {
    if (!texture_) return false;

    // 获取描述符大小
    UINT descriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 计算目标描述符位置
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        srvHeap->GetCPUDescriptorHandleForHeapStart(),
        descriptorIndex, descriptorSize);

    // 填充 SRV 描述
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.ResourceMinLODClamp = 0.0f;

    device->CreateShaderResourceView(texture_.Get(), &srvDesc, srvHandle);

    ME_LOG_INFO("SRV created for texture at descriptor index %d", descriptorIndex);
    return true;
}

void DX12Texture::SetToCommandList(ID3D12GraphicsCommandList* cmdList,
                                     ID3D12DescriptorHeap* srvHeap,
                                     int rootParameterIndex,
                                     int descriptorIndex) {
    if (!texture_) return;

    // 设置描述符堆（告诉 GPU 从哪个堆里找描述符）
    ID3D12DescriptorHeap* heaps[] = { srvHeap };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    // 计算描述符的 GPU 句柄
    UINT descriptorSize = cmdList->GetDevice()->... // 需要从 device 获取
    // 实际上更好的做法是缓存 descriptorSize

    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(
        srvHeap->GetGPUDescriptorHandleForHeapStart(),
        descriptorIndex, descriptorSize);

    // 绑定到根参数（像素着色器中的 t0, t1, ... 寄存器）
    cmdList->SetGraphicsRootDescriptorTable(rootParameterIndex, gpuHandle);
}
```

**讲解**：

纹理上传是 DX12 中最容易出错的环节，核心问题是**行对齐**：

1. **为什么需要对齐？** GPU 的纹理采样硬件要求每行像素数据在内存中对齐到 256 字节边界。一个 100×100 的 RGBA 图片，原始每行 400 字节，但 DX12 要求补齐到 512 字节。

2. **GetCopyableFootprints** — 这个函数替我们计算所有对齐参数：
   - `Footprint.RowPitch` = 对齐后的行宽（512 而不是 400）
   - `uploadSize` = 总上传大小（含对齐填充）

3. **CopyTextureRegion** — 和顶点缓冲的 `CopyBufferRegion` 不同，纹理拷贝需要 `CopyTextureRegion`，因为源数据（上传堆缓冲）和目标（纹理资源）的内存布局不同。

4. **状态转换** — 纹理有三个常见状态：
   - `COPY_DEST` — 正在接收上传数据
   - `PIXEL_SHADER_RESOURCE` — 像素着色器可以读取（采样）
   - `COMMON` — 通用状态

5. **SRV（Shader Resource View）** — 描述符的一种，告诉 GPU "如何解读这个纹理资源"。创建 SRV 后，像素着色器才能通过 `Texture2D tex : register(t0)` 访问纹理。

**UE5 参考**：
- `Engine/Source/Runtime/D3D12RHI/Private/D3D12Texture.cpp` — `CreateTexture2DFromSurface`
- `Engine/Source/Runtime/Engine/Private/TextureDerivedDataTask.cpp` — 纹理压缩和处理管线

---

### 9. 创建 SRV 描述符堆

纹理需要专门的描述符堆。在 `MaterialPreview` 或 `DX12Widget` 中添加：

```cpp
// 在 DX12Widget.h 中添加成员
ComPtr<ID3D12DescriptorHeap> srvHeap_;      // 纹理描述符堆
static const int MAX_TEXTURES = 16;         // 最多同时绑定 16 张纹理
int nextSrvIndex_ = 0;                      // 下一个可用的描述符槽位

// 在 DX12Widget.cpp 的 Initialize() 中添加
bool DX12Widget::CreateSRVHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = MAX_TEXTURES;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    // ↑ 关键：必须设置 SHADER_VISIBLE，着色器才能访问
    // RTV 堆不需要这个标志（RTV 不被着色器直接访问）

    HRESULT hr = device_->CreateDescriptorHeap(
        &srvHeapDesc, IID_PPV_ARGS(&srvHeap_));
    if (FAILED(hr)) {
        ME_LOG_ERROR("Failed to create SRV descriptor heap");
        return false;
    }
    return true;
}
```

**讲解**：
- `D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE` — 这是纹理描述符堆和 RTV 堆的关键区别。RTV 是 CPU 端的概念（GPU 输出到 RTV），不需要着色器可见。SRV 是着色器要读取的，必须在 GPU 可见的堆中。
- `MAX_TEXTURES = 16` — 一个描述符堆中预分配 16 个槽位。每次加载一张纹理占用一个槽位。

---

### 10. 更新根签名以支持纹理

课16 中的根签名只有一个 CBV（常量缓冲）。需要添加 SRV 参数：

```cpp
// 修改 Shader::CreateRootSignature，支持纹理
bool Shader::CreateRootSignature(ID3D12Device* device,
                                  bool useConstantBuffer,
                                  int textureCount) {
    std::vector<D3D12_ROOT_PARAMETER> rootParams;

    // 根参数 0：CBV（b0 寄存器 — 场景常量缓冲）
    if (useConstantBuffer) {
        D3D12_ROOT_PARAMETER cbvParam = {};
        cbvParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        cbvParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        cbvParam.Descriptor.RegisterSpace = 0;
        cbvParam.Descriptor.ShaderRegister = 0;
        rootParams.push_back(cbvParam);
    }

    // 根参数 1：描述符表（t0~tN — 纹理采样器）
    if (textureCount > 0) {
        D3D12_DESCRIPTOR_RANGE texRange = {};
        texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        texRange.NumDescriptors = textureCount;
        texRange.BaseShaderRegister = 0;     // t0 开始
        texRange.RegisterSpace = 0;
        texRange.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER tableParam = {};
        tableParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        tableParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        // ↑ 只在像素着色器中使用纹理
        tableParam.DescriptorTable.NumDescriptorRanges = 1;
        tableParam.DescriptorTable.pDescriptorRanges = &texRange;

        rootParams.push_back(tableParam);
    }

    // 静态采样器（s0 — 线性采样 + Wrap 寻址）
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;  // 线性过滤
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 水平 Wrap
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 垂直 Wrap
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = (UINT)rootParams.size();
    rootSigDesc.pParameters = rootParams.data();
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);

    if (FAILED(hr)) {
        if (error) {
            ME_LOG_ERROR("Root signature error: %s",
                         static_cast<char*>(error->GetBufferPointer()));
        }
        return false;
    }

    hr = device->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));

    return SUCCEEDED(hr);
}
```

**讲解**：
- 根签名现在有 2 个参数：CBV（b0）+ 描述符表（t0~tN）
- `D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE` — 表示"一组描述符"，GPU 通过描述符表来访问多个纹理
- **静态采样器**（Static Sampler）— 采样器描述直接编译进根签名，不需要运行时创建。Wrap 模式表示纹理坐标超出 0~1 范围时循环重复
- `D3D12_SHADER_VISIBILITY_PIXEL` — 纹理只在像素着色器中可见，顶点着色器不需要

---

### 11. 更新像素着色器模板以支持纹理

修改课8的 `HLSLTemplate`，在着色器中添加纹理声明：

```hlsl
// 像素着色器模板（在 HLSLTemplate.cpp 中更新）

// 纹理和采样器声明（由编译器根据是否有纹理节点来决定是否插入）
Texture2D    tex0  : register(t0);
SamplerState sampler0 : register(s0);

// 在 main() 函数中，TextureSample 节点的代码生成变为：
// float4 texSample = tex0.Sample(sampler0, input.uv);
// 而不是之前的占位符
```

---

## 第二部分：模型加载（Assimp）

### 12. MeshLoader.h

```cpp
#pragma once
#include "Renderer/Public/Mesh.h"
#include <string>
#include <vector>

// 模型加载结果
struct LoadedModel {
    std::vector<Mesh> meshes;             // 一个模型可能有多个网格
    std::vector<std::string> materialNames; // 每个网格关联的材质名
    std::string name;                     // 模型名称（文件名）

    // 计算模型的总顶点和三角形数
    size_t TotalVertices() const;
    size_t TotalTriangles() const;

    bool IsValid() const { return !meshes.empty(); }
};

class MeshLoader {
public:
    // 从文件加载模型
    static LoadedModel LoadFromFile(const std::string& filePath);

    // 获取支持的模型格式
    static bool IsSupportedFormat(const std::string& extension);

    // 获取所有支持的格式列表（用于文件对话框过滤）
    static std::string GetSupportedFormatsFilter();
};
```

### 13. MeshLoader.cpp

```cpp
#include "Renderer/Public/MeshLoader.h"
#include "Core/Public/Logger.h"

// Assimp 的头文件
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <set>
#include <algorithm>

size_t LoadedModel::TotalVertices() const {
    size_t total = 0;
    for (const auto& m : meshes) total += m.GetVertices().size();
    return total;
}

size_t LoadedModel::TotalTriangles() const {
    size_t total = 0;
    for (const auto& m : meshes) total += m.GetIndices().size() / 3;
    return total;
}

LoadedModel MeshLoader::LoadFromFile(const std::string& filePath) {
    LoadedModel result;
    result.name = filePath;

    ME_LOG_INFO("Loading model: %s", filePath.c_str());

    // === 第1步：创建 Assimp 导入器 ===
    Assimp::Importer importer;

    // === 第2步：设置后处理标志 ===
    // Assimp 加载模型后会做一系列后处理，生成我们需要的数据
    unsigned int flags =
        aiProcess_Triangulate |           // 把所有面转为三角形（四边形→两个三角形）
        aiProcess_GenNormals |            // 如果模型没有法线，自动生成平滑法线
        aiProcess_CalcTangentSpace |      // 计算切线空间（法线贴图需要）
        aiProcess_JoinIdenticalVertices | // 合并相同顶点（减小模型体积）
        aiProcess_FlipUVs |               // 翻转 UV（OpenGL 和 DX 的 UV 原点不同）
        aiProcess_GenBoundingBoxes;       // 生成包围盒

    // === 第3步：导入文件 ===
    const aiScene* scene = importer.ReadFile(filePath, flags);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        ME_LOG_ERROR("Assimp failed to load: %s", importer.GetErrorString());
        return result;
    }

    ME_LOG_INFO("Scene loaded: %u meshes, %u materials, %u textures",
                scene->mNumMeshes, scene->mNumMaterials, scene->mNumTextures);

    // === 第4步：遍历场景中的所有网格 ===
    // Assimp 的场景结构：Scene → mMeshes[] → 每个 mesh 有 mVertices, mFaces 等
    // 我们递归遍历场景节点树，收集所有引用的网格
    std::set<unsigned int> processedMeshes;
    ProcessNode(scene, scene->mRootNode, scene, result, processedMeshes);

    ME_LOG_INFO("Model loaded: %zu meshes, %zu vertices, %zu triangles",
                result.meshes.size(), result.TotalVertices(), result.TotalTriangles());

    return result;
}

// === 前向声明（static 函数定义在下面，C++ 要求使用前必须声明）===
static void ProcessNode(const aiScene* scene, aiNode* node,
                        const aiScene* rootScene, Model& model,
                        std::set<unsigned int>& processedMeshes);
static Mesh ProcessMesh(const aiScene* scene, aiMesh* aiMesh);

// 递归处理场景节点（Assimp 的场景是树结构）
static void ProcessNode(const aiScene* scene, aiNode* node,
                         LoadedModel& result,
                         std::set<unsigned int>& processed) {
    // 处理当前节点的所有网格
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        unsigned int meshIndex = node->mMeshes[i];
        if (processed.count(meshIndex)) continue;  // 避免重复
        processed.insert(meshIndex);

        aiMesh* aiMesh = scene->mMeshes[meshIndex];
        Mesh mesh = ProcessMesh(aiMesh);
        result.meshes.push_back(std::move(mesh));

        // 记录材质名称
        if (aiMesh->mMaterialIndex < scene->mNumMaterials) {
            aiMaterial* mat = scene->mMaterials[aiMesh->mMaterialIndex];
            aiString matName;
            mat->Get(AI_MATKEY_NAME, matName);
            result.materialNames.push_back(matName.C_Str());
        }
    }

    // 递归处理子节点
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        ProcessNode(scene, node->mChildren[i], result, processed);
    }
}

// 把 Assimp 的 aiMesh 转换为我们的 Mesh
static Mesh ProcessMesh(aiMesh* aiMesh) {
    Mesh mesh;

    // === 提取顶点数据 ===
    for (unsigned int i = 0; i < aiMesh->mNumVertices; i++) {
        Mesh::Vertex vertex;

        // 位置（每个网格必须有的数据）
        vertex.position = Vec3(
            aiMesh->mVertices[i].x,
            aiMesh->mVertices[i].y,
            aiMesh->mVertices[i].z
        );

        // 法线（如果存在）
        if (aiMesh->HasNormals()) {
            vertex.normal = Vec3(
                aiMesh->mNormals[i].x,
                aiMesh->mNormals[i].y,
                aiMesh->mNormals[i].z
            );
        } else {
            vertex.normal = Vec3(0, 1, 0);  // 默认朝上
        }

        // UV 坐标（如果存在，Assimp 支持多套 UV，我们只用第一套）
        if (aiMesh->HasTextureCoords(0)) {
            vertex.texCoord = Vec2(
                aiMesh->mTextureCoords[0][i].x,
                aiMesh->mTextureCoords[0][i].y
            );
        } else {
            vertex.texCoord = Vec2(0, 0);
        }

        // 注意：Mesh 的 vertices_ 是 private 成员，外部不能直接 push_back。
        // 推荐用 MeshBuilder 模式（课16 末尾提到）——它在 lesson21 末尾会介绍。
        // 这里假定 Mesh 暴露了 AddVertex 公开方法（如果你按课16 写的 Mesh 没有，
        // 自己加一个 void AddVertex(const Vertex& v) { vertices_.push_back(v); } 即可）
        mesh.AddVertex(vertex);
    }

    // === 提取索引数据 ===
    // Assimp 的每个 Face 是一个多边形，aiProcess_Triangulate 保证都是三角形
    for (unsigned int i = 0; i < aiMesh->mNumFaces; i++) {
        aiFace& face = aiMesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            mesh.AddIndex(face.mIndices[j]);
        }
    }

    return mesh;
}

bool MeshLoader::IsSupportedFormat(const std::string& extension) {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static std::set<std::string> supported = {
        "obj", "fbx", "gltf", "glb",       // 最常用
        "dae", "3ds", "blend", "ase",       // Blender / 3ds Max
        "ply", "stl", "x", "md5mesh",       // 其他格式
        "md2", "md3", "ms3d", "lwo",        // 更多...
    };
    return supported.count(ext) > 0;
}

std::string MeshLoader::GetSupportedFormatsFilter() {
    return "3D Models (*.obj *.fbx *.gltf *.glb *.dae *.3ds *.ply *.stl);;"
           "OBJ (*.obj);;"
           "FBX (*.fbx);;"
           "glTF (*.gltf *.glb);;"
           "All Files (*.*)";
}
```

**讲解**：
- **Assimp 后处理标志**是最重要的配置：
  - `aiProcess_Triangulate` — GPU 只画三角形，所以要把四边形、N 边形全部转成三角形
  - `aiProcess_GenNormals` — 有些模型（比如 STL 格式）没有法线，Assimp 自动计算
  - `aiProcess_FlipUVs` — OpenGL 的 UV 原点在左下角，DirectX 在左上角。我们用 DX12，但 Assimp 默认按 OpenGL 加载，所以要翻转
- **场景树结构**：Assimp 把模型组织为一棵节点树（和 UE5 的场景层次一样）。每个节点可以包含网格引用和变换矩阵。我们递归遍历整棵树，收集所有网格。
- **一个模型可以有多个 Mesh**：比如一个人体模型可能有"身体"、"头发"、"衣服"三个网格，每个网格用不同材质。`LoadedModel::meshes` 是一个数组，渲染时逐个绘制。
- **注意**：上面的代码中 `mesh.vertices_` 和 `mesh.indices_` 是私有成员。实际实现时需要给 `Mesh` 类添加 `AddVertex()` 方法，或者把这两个成员改为 public。建议添加 `MeshBuilder` 辅助类：

```cpp
// 在 Mesh.h 中添加
class MeshBuilder {
public:
    void AddVertex(const Mesh::Vertex& v) { vertices_.push_back(v); }
    void AddIndex(uint32_t idx) { indices_.push_back(idx); }
    Mesh Build() const;  // 创建并返回完整的 Mesh

private:
    std::vector<Mesh::Vertex> vertices_;
    std::vector<uint32_t> indices_;
};
```

**UE5 参考**：
- `Engine/Source/Runtime/Engine/Private/Factories/FbxFactory.cpp` — UE5 的 FBX 导入器（几千行）
- `Engine/Source/Runtime/Engine/Private/StaticMesh.cpp` — `UStaticMesh` 的实现

---

## 第三部分：网络下载（Qt Network）

### 14. NetworkDownloader.h

```cpp
#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <functional>
#include <vector>
#include <cstdint>

// 下载结果
struct DownloadResult {
    bool success = false;
    std::vector<uint8_t> data;     // 下载的原始数据
    std::string error;             // 错误信息
    std::string url;               // 请求的 URL
    std::string suggestedFileName; // 从 URL 推断的文件名
};

// 异步下载完成回调
using DownloadCallback = std::function<void(const DownloadResult&)>;

class NetworkDownloader : public QObject {
    Q_OBJECT
public:
    explicit NetworkDownloader(QObject* parent = nullptr);

    // 异步下载（不阻塞 UI）
    void DownloadAsync(const QString& url, DownloadCallback callback);

    // 同步下载（阻塞调用线程，慎用）
    DownloadResult DownloadSync(const QString& url);

    // 取消所有下载
    void CancelAll();

private slots:
    void OnDownloadFinished(QNetworkReply* reply);
    void OnDownloadProgress(qint64 received, qint64 total);

private:
    QNetworkAccessManager* manager_;

    // 正在进行的下载
    struct PendingDownload {
        QNetworkReply* reply;
        DownloadCallback callback;
        std::vector<uint8_t> data;
        QString url;
    };
    std::map<QNetworkReply*, PendingDownload> pending_;

    // 从 URL 提取文件名
    static std::string ExtractFileName(const QString& url);
};
```

### 15. NetworkDownloader.cpp

```cpp
#include "Core/Public/NetworkDownloader.h"
#include "Core/Public/Logger.h"
#include <QUrl>
#include <QEventLoop>
#include <map>

NetworkDownloader::NetworkDownloader(QObject* parent)
    : QObject(parent) {
    manager_ = new QNetworkAccessManager(this);
    connect(manager_, &QNetworkAccessManager::finished,
            this, &NetworkDownloader::OnDownloadFinished);
}

void NetworkDownloader::DownloadAsync(const QString& url,
                                        DownloadCallback callback) {
    ME_LOG_INFO("Starting download: %s", url.toStdString().c_str());

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // ↑ 自动跟随重定向（HTTP 301/302）

    QNetworkReply* reply = manager_->get(request);

    PendingDownload pending;
    pending.reply = reply;
    pending.callback = callback;
    pending.url = url;
    pending_[reply] = pending;

    // 进度信号
    connect(reply, &QNetworkReply::downloadProgress,
            this, &NetworkDownloader::OnDownloadProgress);
}

DownloadResult NetworkDownloader::DownloadSync(const QString& url) {
    DownloadResult result;
    result.url = url.toStdString();

    QEventLoop loop;
    DownloadAsync(url, [&](const DownloadResult& r) {
        result = r;
        loop.quit();
    });
    loop.exec();  // 阻塞直到下载完成

    return result;
}

void NetworkDownloader::OnDownloadFinished(QNetworkReply* reply) {
    auto it = pending_.find(reply);
    if (it == pending_.end()) {
        reply->deleteLater();
        return;
    }

    DownloadResult result;
    result.url = it->second.url.toStdString();
    result.suggestedFileName = ExtractFileName(it->second.url);

    if (reply->error() == QNetworkReply::NoError) {
        // 读取全部数据
        QByteArray data = reply->readAll();
        result.data.assign(data.begin(), data.end());
        result.success = true;
        ME_LOG_INFO("Download complete: %s (%zu bytes)",
                     result.url.c_str(), result.data.size());
    } else {
        result.error = reply->errorString().toStdString();
        result.success = false;
        ME_LOG_ERROR("Download failed: %s — %s",
                      result.url.c_str(), result.error.c_str());
    }

    // 调用回调
    if (it->second.callback) {
        it->second.callback(result);
    }

    pending_.erase(it);
    reply->deleteLater();
}

void NetworkDownloader::OnDownloadProgress(qint64 received, qint64 total) {
    if (total > 0) {
        int percent = (int)(received * 100 / total);
        ME_LOG_INFO("Download progress: %d%% (%lld / %lld bytes)",
                     percent, received, total);
    }
}

void NetworkDownloader::CancelAll() {
    for (auto& [reply, pending] : pending_) {
        reply->abort();
    }
    pending_.clear();
}

std::string NetworkDownloader::ExtractFileName(const QString& url) {
    // 从 URL 中提取文件名
    // https://example.com/models/character.glb → character.glb
    QString path = QUrl(url).path();
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash >= 0 && lastSlash < path.size() - 1) {
        return path.mid(lastSlash + 1).toStdString();
    }
    return "downloaded_file";
}
```

**讲解**：
- `QNetworkAccessManager` — Qt 的 HTTP 客户端，支持 GET/POST 等请求
- **异步下载**（`DownloadAsync`）— 发起请求后立即返回，下载完成时通过回调通知。不阻塞 UI 线程。
- **同步下载**（`DownloadSync`）— 用 `QEventLoop` 阻塞等待下载完成。适合非 UI 场景，但不要在主线程中使用（会冻住窗口）。
- `QNetworkRequest::NoLessSafeRedirectPolicy` — 自动跟随 HTTP 重定向（很多 CDN 下载链接会重定向）
- `reply->deleteLater()` — Qt 的延迟删除机制，确保在事件处理完毕后再释放 reply 对象

---

## 第四部分：拖拽处理（Qt Drag & Drop）

### 16. DropHandler.h

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <string>

class Graph;
class DX12Widget;
class MaterialPreview;

// 拖拽处理 — 判断拖入的文件类型并分发到对应的加载器
class DropHandler : public QObject {
    Q_OBJECT
public:
    explicit DropHandler(QObject* parent = nullptr);

    // 设置依赖（由 MainWindow 在初始化时注入）
    void SetGraph(Graph* graph) { graph_ = graph; }
    void SetDX12Widget(DX12Widget* widget) { dx12Widget_ = widget; }
    void SetMaterialPreview(MaterialPreview* preview) { preview_ = preview; }

    // 处理拖拽进入事件（返回 true 表示我们接受这个拖拽）
    bool HandleDragEnter(class QDragEnterEvent* event);

    // 处理拖拽移动事件
    bool HandleDragMove(class QDragMoveEvent* event);

    // 处理放下事件（执行实际的加载操作）
    bool HandleDrop(class QDropEvent* event);

signals:
    // 通知 UI 资源已加载
    void ModelLoaded(const std::string& filePath, int meshCount, int vertexCount);
    void TextureLoaded(const std::string& filePath, int width, int height);
    void DownloadStarted(const std::string& url);
    void DownloadCompleted(const std::string& filePath);
    void Error(const std::string& message);

private:
    Graph* graph_ = nullptr;
    DX12Widget* dx12Widget_ = nullptr;
    MaterialPreview* preview_ = nullptr;

    // 处理本地文件
    bool HandleLocalFile(const QString& filePath);

    // 处理网络 URL
    bool HandleURL(const QString& url);

    // 判断文件类型
    enum class FileType { Model, Texture, Unknown };
    static FileType GetFileType(const QString& filePath);
};
```

### 17. DropHandler.cpp

```cpp
#include "UI/Private/DropHandler.h"
#include "Renderer/Public/MeshLoader.h"
#include "Renderer/Public/TextureLoader.h"
#include "Renderer/Public/DX12Texture.h"
#include "Core/Public/NetworkDownloader.h"
#include "Core/Public/Logger.h"
#include "MaterialGraph/Public/Graph.h"
#include "Renderer/Public/MaterialPreview.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>

bool DropHandler::HandleDragEnter(QDragEnterEvent* event) {
    // 检查拖拽的内容是否包含 URL（文件路径也算 URL）
    if (event->mimeData()->hasUrls()) {
        // 检查至少有一个 URL 是我们支持的格式
        for (const QUrl& url : event->mimeData()->urls()) {
            QString path = url.toLocalFile().isEmpty()
                            ? url.toString()
                            : url.toLocalFile();

            if (GetFileType(path) != FileType::Unknown) {
                event->acceptProposedAction();
                return true;
            }
        }
    }
    return false;
}

bool DropHandler::HandleDragMove(QDragMoveEvent* event) {
    event->acceptProposedAction();
    return true;
}

bool DropHandler::HandleDrop(QDropEvent* event) {
    if (!event->mimeData()->hasUrls()) return false;

    bool handled = false;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            // 本地文件（从文件管理器拖入）
            handled |= HandleLocalFile(url.toLocalFile());
        } else if (url.scheme() == "http" || url.scheme() == "https") {
            // 网络 URL（从浏览器拖入）
            handled |= HandleURL(url.toString());
        }
    }
    return handled;
}

bool DropHandler::HandleLocalFile(const QString& filePath) {
    FileType type = GetFileType(filePath);
    QFileInfo fi(filePath);
    std::string ext = fi.suffix().toLower().toStdString();
    std::string path = filePath.toStdString();

    if (type == FileType::Model) {
        ME_LOG_INFO("Dropped model: %s", path.c_str());

        // 加载模型
        LoadedModel model = MeshLoader::LoadFromFile(path);
        if (!model.IsValid()) {
            emit Error("Failed to load model: " + path);
            return false;
        }

        // 替换预览网格（取第一个 mesh）
        // 实际实现中需要上传到 GPU
        if (preview_ && dx12Widget_ && !model.meshes.empty()) {
            // 需要通过 MaterialPreview 的接口设置外部模型
            // 这里触发 ModelLoaded 信号，由 MainWindow 处理上传和替换
            emit ModelLoaded(path, (int)model.meshes.size(),
                            (int)model.TotalVertices());
        }

        return true;

    } else if (type == FileType::Texture) {
        ME_LOG_INFO("Dropped texture: %s", path.c_str());

        // 加载贴图
        RawTexture rawTex = TextureLoader::LoadFromFile(path);
        if (!rawTex.IsValid()) {
            emit Error("Failed to load texture: " + path);
            return false;
        }

        // 创建 DX12 纹理资源并绑定到材质
        // 同样通过信号通知 MainWindow 处理
        emit TextureLoaded(path, rawTex.width, rawTex.height);

        return true;
    }

    return false;
}

bool DropHandler::HandleURL(const QString& url) {
    ME_LOG_INFO("Dropped URL: %s", url.toStdString().c_str());
    emit DownloadStarted(url.toStdString());

    // 异步下载
    auto* downloader = new NetworkDownloader(this);
    // 注意：lambda 体内会调 downloader->deleteLater()，必须捕获 downloader 指针
    downloader->DownloadAsync(url, [this, url, downloader](const DownloadResult& result) {
        if (!result.success) {
            emit Error("Download failed: " + result.error);
            return;
        }

        // 下载完成后保存到临时文件并加载
        // 判断类型
        QString fileName = QString::fromStdString(result.suggestedFileName);
        FileType type = GetFileType(fileName);

        // 保存到临时目录
        QString tempPath = QDir::tempPath() + "/" + fileName;
        QFile file(tempPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reinterpret_cast<const char*>(result.data.data()),
                       result.data.size());
            file.close();
        }

        // 如果是图片，也可以直接从内存加载
        if (type == FileType::Texture) {
            RawTexture rawTex = TextureLoader::LoadFromMemory(
                result.data.data(), result.data.size());
            if (rawTex.IsValid()) {
                emit TextureLoaded(result.suggestedFileName,
                                   rawTex.width, rawTex.height);
            }
        } else if (type == FileType::Model) {
            HandleLocalFile(tempPath);
        }

        emit DownloadCompleted(tempPath.toStdString());

        // 下载完成后自动删除下载器
        downloader->deleteLater();
    });

    return true;
}

DropHandler::FileType DropHandler::GetFileType(const QString& filePath) {
    QString ext = QFileInfo(filePath).suffix().toLower();

    if (MeshLoader::IsSupportedFormat(ext.toStdString())) {
        return FileType::Model;
    }
    if (TextureLoader::IsSupportedFormat(ext.toStdString())) {
        return FileType::Texture;
    }
    return FileType::Unknown;
}
```

**讲解**：
- Qt 的拖拽事件分三步：`dragEnterEvent`（判断能否接受）→ `dragMoveEvent`（持续跟踪）→ `dropEvent`（实际操作）
- `mimeData()->hasUrls()` — 文件管理器拖入文件时，Qt 会把文件路径作为 URL 传递；浏览器拖入链接时，传递的是 http/https URL
- `url.isLocalFile()` — 区分本地文件和网络 URL
- 网络 URL 下载是异步的，下载完成后通过信号通知 UI

---

### 18. 集成到 ViewportPanel（支持拖拽）

在 `ViewportPanel` 中重写拖拽事件，委托给 `DropHandler`：

```cpp
// ViewportPanel.h 中添加
#include "UI/Private/DropHandler.h"

class ViewportPanel : public QDockWidget {
    Q_OBJECT
    // ... 原有代码 ...

protected:
    // 添加拖拽事件
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    DropHandler* dropHandler_;
};
```

```cpp
// ViewportPanel.cpp 中添加
#include <QDragEnterEvent>
#include <QDropEvent>

ViewportPanel::ViewportPanel(const QString& title, QWidget* parent)
    : QDockWidget(title, parent) {
    // ... 原有代码 ...

    // 启用拖拽
    setAcceptDrops(true);
    dropHandler_ = new DropHandler(this);

    // 连接信号
    connect(dropHandler_, &DropHandler::ModelLoaded,
            this, [this](const std::string& path, int meshes, int verts) {
        ME_LOG_INFO("Model loaded into viewport: %s (%d meshes, %d verts)",
                     path.c_str(), meshes, verts);
        // 触发重绘
        if (dx12Widget_) dx12Widget_->update();
    });

    connect(dropHandler_, &DropHandler::TextureLoaded,
            this, [this](const std::string& path, int w, int h) {
        ME_LOG_INFO("Texture loaded: %s (%dx%d)", path.c_str(), w, h);
    });

    connect(dropHandler_, &DropHandler::Error,
            this, [this](const std::string& msg) {
        ME_LOG_ERROR("Drop error: %s", msg.c_str());
    });
}

void ViewportPanel::dragEnterEvent(QDragEnterEvent* event) {
    if (dropHandler_->HandleDragEnter(event)) {
        // 高亮边框提示用户可以放下
        setStyleSheet("QDockWidget { border: 2px solid #4CAF50; }");
    }
}

void ViewportPanel::dragMoveEvent(QDragMoveEvent* event) {
    dropHandler_->HandleDragMove(event);
}

void ViewportPanel::dropEvent(QDropEvent* event) {
    setStyleSheet("");  // 移除高亮
    dropHandler_->HandleDrop(event);
}
```

---

### 19. 集成到 MainWindow

在 `MainWindow` 中也支持拖拽（整个窗口都可以接受文件），并处理纹理加载到材质图：

```cpp
// MainWindow.cpp

#include "UI/Private/DropHandler.h"
#include "Renderer/Public/MeshLoader.h"
#include "Renderer/Public/DX12Texture.h"

void MainWindow::SetupDropHandling() {
    // 主窗口也接受拖拽
    setAcceptDrops(true);

    dropHandler_ = new DropHandler(this);
    dropHandler_->SetGraph(graph_);
    dropHandler_->SetDX12Widget(viewportPanel_->GetDX12Widget());

    // 模型加载 → 替换预览网格
    connect(dropHandler_, &DropHandler::ModelLoaded,
            this, &MainWindow::OnModelDropped);

    // 贴图加载 → 创建 TextureSample 节点
    connect(dropHandler_, &DropHandler::TextureLoaded,
            this, &MainWindow::OnTextureDropped);

    // 错误显示
    connect(dropHandler_, &DropHandler::Error,
            this, [this](const std::string& msg) {
        statusBar()->showMessage(QString::fromStdString(msg), 5000);
    });
}

void MainWindow::OnModelDropped(const std::string& filePath,
                                   int meshCount, int vertexCount) {
    // 加载模型并替换预览网格
    LoadedModel model = MeshLoader::LoadFromFile(filePath);
    if (!model.IsValid() || model.meshes.empty()) return;

    // 取第一个网格作为预览（多网格模型后续可支持选择）
    auto& mesh = model.meshes[0];

    // 上传到 GPU 并替换
    if (viewportPanel_) {
        viewportPanel_->SetPreviewMesh(mesh);  // 需要添加这个重载
    }

    statusBar()->showMessage(
        QString("Loaded: %1 (%2 meshes, %3 vertices)")
            .arg(QString::fromStdString(filePath))
            .arg(meshCount).arg(vertexCount));
}

void MainWindow::OnTextureDropped(const std::string& filePath,
                                     int width, int height) {
    // 在材质图中自动创建一个 TextureSample 节点
    // 位置放在当前视口中心附近

    // 1. 创建节点
    auto* node = graph_->AddNode("ExprTextureSample", QPointF(200, 200));
    node->parameters["textureName_"] = filePath;  // 字段名和反射注册一致（课5）

    // 2. 创建 DX12 纹理资源（用于预览渲染）
    // 需要在 ViewportPanel 中执行 GPU 操作
    if (viewportPanel_) {
        viewportPanel_->LoadTexture(filePath);
    }

    statusBar()->showMessage(
        QString("Texture loaded: %1 (%2x%3)")
            .arg(QString::fromStdString(filePath))
            .arg(width).arg(height));
}

// 拖拽事件转发
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    dropHandler_->HandleDragEnter(event);
}

void MainWindow::dropEvent(QDropEvent* event) {
    dropHandler_->HandleDrop(event);
}
```

---

### 20. ViewportPanel 添加外部模型和纹理支持

```cpp
// ViewportPanel.h 添加

// 设置外部模型（从 MeshLoader 加载的）
void SetPreviewMesh(const Mesh& externalMesh);

// 加载纹理到 GPU
void LoadTexture(const std::string& filePath);

private:
    // 纹理资源管理
    std::map<std::string, DX12Texture> loadedTextures_;
    int textureSlotIndex_ = 0;  // 当前使用的纹理描述符槽位
```

```cpp
// ViewportPanel.cpp

void ViewportPanel::SetPreviewMesh(const Mesh& externalMesh) {
    if (!dx12Initialized_) return;

    dx12Widget_->WaitForGPU();
    dx12Widget_->ExecuteCommandList();

    // 销毁旧网格
    preview_.DestroyMesh();  // 需要在 MaterialPreview 中添加此方法

    // 拷贝外部网格数据
    Mesh mesh = externalMesh;

    // 上传到 GPU
    uploadOffset_ = 0;
    mesh.Upload(dx12Widget_->GetDevice(),
                dx12Widget_->GetCommandList(),
                uploadBuffer_.Get(), uploadOffset_);

    // 设置为当前预览网格
    preview_.SetMesh(mesh);  // 需要添加接受 Mesh 对象的重载

    dx12Widget_->Present();
    dx12Widget_->update();
}

void ViewportPanel::LoadTexture(const std::string& filePath) {
    if (!dx12Initialized_) return;
    if (loadedTextures_.count(filePath)) return;  // 已加载

    // 检查槽位
    if (textureSlotIndex_ >= DX12Widget::MAX_TEXTURES) {
        ME_LOG_ERROR("Max texture count reached (%d)", DX12Widget::MAX_TEXTURES);
        return;
    }

    dx12Widget_->WaitForGPU();
    dx12Widget_->ExecuteCommandList();

    // 创建 DX12 纹理
    DX12Texture tex;
    tex.CreateFromFile(dx12Widget_->GetDevice(),
                       dx12Widget_->GetCommandList(),
                       uploadBuffer_.Get(), uploadOffset_,
                       filePath);

    // 创建 SRV
    tex.CreateSRV(dx12Widget_->GetDevice(),
                  dx12Widget_->GetSRVHeap(),
                  textureSlotIndex_);

    loadedTextures_[filePath] = std::move(tex);
    textureSlotIndex_++;

    dx12Widget_->Present();
}
```

---

## 第五部分：添加"打开文件"菜单

除了拖拽，还应该在菜单栏中添加"文件 → 导入模型/贴图"的操作。

### 21. MainWindow 中添加文件导入菜单

```cpp
// 在 MainWindow::SetupMenuBar() 中添加

// 文件菜单
QMenu* fileMenu = menuBar()->addMenu("&File");

QAction* importModelAct = fileMenu->addAction("Import &Model...");
QAction* importTextureAct = fileMenu->addAction("Import &Texture...");

connect(importModelAct, &QAction::triggered, this, [this]() {
    QString filter = QString::fromStdString(MeshLoader::GetSupportedFormatsFilter());
    QString path = QFileDialog::getOpenFileName(
        this, "Import 3D Model", QString(), filter);

    if (!path.isEmpty()) {
        LoadedModel model = MeshLoader::LoadFromFile(path.toStdString());
        if (model.IsValid()) {
            OnModelDropped(path.toStdString(),
                          (int)model.meshes.size(),
                          (int)model.TotalVertices());
        }
    }
});

connect(importTextureAct, &QAction::triggered, this, [this]() {
    QString path = QFileDialog::getOpenFileName(
        this, "Import Texture", QString(),
        "Images (*.png *.jpg *.jpeg *.tga *.bmp *.hdr);;All Files (*.*)");

    if (!path.isEmpty()) {
        RawTexture tex = TextureLoader::LoadFromFile(path.toStdString());
        if (tex.IsValid()) {
            OnTextureDropped(path.toStdString(), tex.width, tex.height);
        }
    }
});
```

---

## 验证

### 测试1：加载 OBJ 模型

用任何 3D 软件（Blender 等）导出一个简单的 OBJ 模型，或者从网上下载一个免费的：

```cpp
// 在 main.cpp 或测试函数中
#include "Renderer/Public/MeshLoader.h"

void Test_LoadOBJ() {
    LoadedModel model = MeshLoader::LoadFromFile("test_model.obj");

    ME_LOG_INFO("Model loaded: %s", model.name.c_str());
    ME_LOG_INFO("  Meshes: %zu", model.meshes.size());
    ME_LOG_INFO("  Vertices: %zu", model.TotalVertices());
    ME_LOG_INFO("  Triangles: %zu", model.TotalTriangles());

    for (size_t i = 0; i < model.meshes.size(); i++) {
        ME_LOG_INFO("  Mesh[%zu]: %zu vertices, %zu indices",
                     i,
                     model.meshes[i].GetVertices().size(),
                     model.meshes[i].GetIndices().size());
    }
}
```

### 测试2：加载纹理

```cpp
#include "Renderer/Public/TextureLoader.h"

void Test_LoadTexture() {
    RawTexture tex = TextureLoader::LoadFromFile("test_texture.png");
    ME_LOG_INFO("Texture: %dx%d, %d channels, %zu bytes",
                tex.width, tex.height, tex.channels, tex.pixels.size());

    // 验证像素数据
    if (tex.IsValid()) {
        // 检查第一个像素的 RGBA 值
        ME_LOG_INFO("First pixel: R=%d, G=%d, B=%d, A=%d",
                    tex.pixels[0], tex.pixels[1],
                    tex.pixels[2], tex.pixels[3]);
    }
}
```

### 测试3：拖拽

1. 编译运行编辑器
2. 从文件管理器拖一个 `.obj` 文件到 ViewportPanel
3. 预览网格应该从球体变成加载的模型
4. 从文件管理器拖一个 `.png` 文件到编辑器
5. 材质图中应该自动创建一个 TextureSample 节点

---

## 常见问题

### 问题1：Assimp 编译错误 — 找不到头文件

确保 vcpkg 正确安装了 assimp，并且 CMakePresets.json 中的 vcpkg toolchain 路径正确。检查 `find_package(assimp CONFIG REQUIRED)` 是否成功。

### 问题2：stb_image 链接错误 — 重复定义

确保 `#define STB_IMAGE_IMPLEMENTATION` 只出现在**一个** `.cpp` 文件中。如果出现在多个文件中，会报 `LNK2005` 重复定义错误。

### 问题3：纹理显示全黑

可能的原因：
1. **纹理未上传** — 确保 `Create` 之后执行了命令列表并等待 GPU 完成
2. **SRV 未创建** — `CreateSRV` 必须在纹理上传后调用
3. **根签名不匹配** — 像素着色器中的 `register(t0)` 必须对应根签名中的描述符表
4. **状态未转换** — 纹理必须是 `PIXEL_SHADER_RESOURCE` 状态才能在着色器中采样
5. **行对齐错误** — 检查 `GetCopyableFootprints` 返回的 RowPitch 是否正确

### 问题4：模型加载后显示错乱

可能的原因：
1. **未翻转 UV** — 确保使用了 `aiProcess_FlipUVs` 标志
2. **未三角化** — 确保使用了 `aiProcess_Triangulate` 标志
3. **坐标系不同** — Assimp 默认使用右手坐标系（OpenGL），DX12 使用左手坐标系。可以通过 `aiProcess_MakeLeftHanded` 转换
4. **模型太大或太小** — 加载后需要计算包围盒并缩放到合适大小

### 问题5：拖拽不生效

1. 确保调用了 `setAcceptDrops(true)`
2. 确保 `dragEnterEvent` 中调用了 `event->acceptProposedAction()`
3. 如果是 DX12Widget，可能需要 `setAttribute(Qt::WA_AcceptDrops, true)`

### 问题6：网络下载失败

1. 检查 URL 是否有效（浏览器中能否直接打开）
2. 有些网站禁止外部程序下载（需要设置 User-Agent 头）
3. HTTPS 证书问题 — Qt 可能需要 OpenSSL 库

```cpp
// 设置自定义 User-Agent
QNetworkRequest request(url);
request.setRawHeader("User-Agent",
    "MaterialEditor/1.0 (Educational Project)");
```

---

## UE5 参考（相对 `Engine/` 路径）

| 功能 | UE5 源码位置 |
|------|-------------|
| 纹理加载 | `Engine/Source/Runtime/Engine/Private/Texture2D.cpp` |
| 纹理平台实现 | `Engine/Source/Runtime/D3D12RHI/Private/D3D12Texture.cpp` |
| FBX 导入 | `Engine/Source/Runtime/FbxMeshBuilder/Private/FbxMeshBuilder.cpp` |
| OBJ 导入 | `Engine/Source/Runtime/GeometryCollection/Factories/GeometryCollectionOBJFactory.cpp` |
| 资源导入框架 | `Engine/Source/Runtime/Engine/Classes/Factories/Factory.h` |
| 材质贴图绑定 | `Engine/Source/Runtime/Engine/Private/Materials/MaterialShader.cpp` — `FMaterialShaderParameters` |
| 描述符管理 | `Engine/Source/Runtime/D3D12RHI/Private/D3D12DescriptorCache.cpp` |
| 异步资源加载 | `Engine/Source/Runtime/Engine/Private/AsyncLoading.cpp` |

### 对照 UE 资源加载

| 我们的 | UE | 作用 |
|--------|-----|------|
| stb_image（单文件库）| `Texture2D` + UAsset 导入管线 | 纹理加载 |
| Assimp（开源）| FBX SDK（Autodesk 授权）+ 自研 OBJ | 模型导入 |
| 同步加载 | 异步加载（`AsyncLoading` + 流式）| 加载策略 |
| 手动 SRV 描述符 | `FD3D12DescriptorCache`（描述符池）| 描述符管理 |

**四个关键差异**：

1. **纹理格式**：UE 用自研压缩格式（BC/ASTC/ETC，平台相关，GPU 直接解压）。我们用 stb_image 解码成 **RGBA 未压缩**——简单但占显存大（一张 4K 贴图 64MB）。生产要用压缩纹理（块1 纹理类型，`lesson06-extension.md`）。

2. **模型导入**：UE 用 FBX SDK（Autodesk 授权，闭源，质量高）+ 自研 OBJ/USD。我们用 **Assimp**（开源，支持 40+ 格式）——免费，但复杂 FBX 的导入质量不如 FBX SDK。

3. **异步加载**：UE 的资源加载是**异步的**（`AsyncLoading` + 后台线程 + 流式 + 引用计数卸载），不卡帧。我们是**同步加载**（加载时卡）——简单，大模型会卡。

4. **描述符管理**：UE 用 `FD3D12DescriptorCache`（描述符池 + 复用，避免每帧重建）。我们手动创建 SRV（每个纹理一个描述符，不复用）——简单，但纹理多时描述符堆吃紧。

### 扩展预告：纹理类型（块1，见 `lesson06-extension.md`）

块1（类型系统扩展）规划 `EValueType::Texture2D` / `SamplerState`——让纹理成为**编译器层一等类型**（不是参数层）。`TextureSample` 节点接收 `Texture2D` + `SamplerState`，编译期类型检查，采样返回 `Float4`。当前 lesson21 的纹理加载（stb_image + DX12 上传）是基础——块1 让纹理能进材质图编译（传给 TextureSample）。

> **搜索关键词**（UE 源码）：`Texture2D`、`FTexture2DMipMap`、`UAssetManager`、`FStreamableManager`、`FMaterialShaderParameters`、`FD3D12DescriptorCache`。

---

## 完成标志

- [ ] stb_image 可以加载 PNG/JPG 文件为 RawTexture
- [ ] DX12Texture 可以创建 GPU 纹理并在像素着色器中采样
- [ ] Assimp 可以加载 OBJ/FBX/glTF 模型
- [ ] 加载的模型可以替换预览网格并正确显示
- [ ] 从文件管理器拖拽模型文件到编辑器可以替换预览网格
- [ ] 从文件管理器拖拽贴图文件到编辑器可以创建 TextureSample 节点
- [ ] 从浏览器拖拽 URL 可以异步下载并加载资源
- [ ] "File → Import Model" 菜单可以打开文件对话框选择模型
- [ ] 纹理在 3D 预览中正确显示（不是全黑或全白）
- [ ] 模型的 UV 坐标正确（纹理没有镜像或旋转问题）
- [ ] 程序关闭时所有 GPU 资源正确释放，无内存泄漏
