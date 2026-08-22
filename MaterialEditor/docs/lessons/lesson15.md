# 课15：DirectX 12 渲染管线 — 根签名、PSO 与顶点缓冲

## 目标

理解 DX12 渲染管线的完整流程，编写 HLSL 着色器，创建顶点缓冲，在屏幕上画出一个彩色三角形。

本课是课14的延续。课14已经搭好了 DX12 基础设施（设备、命令队列、交换链、清屏），现在我们要让 GPU 真正"画"出东西来。

---

## 背景知识

### DX12 渲染管线全流程

DX12 的渲染管线是一条流水线，数据从一端进入，经过多个阶段处理，最终变成屏幕上的像素。先看全景图：

```
┌─────────────────────────────────────────────────────────────────┐
│                    DirectX 12 渲染管线                           │
│                                                                 │
│  ┌──────────────┐                                               │
│  │ Input        │ ← 你提供：顶点缓冲（三角形三个顶点的数据）      │
│  │ Assembler    │                                               │
│  │   (IA)       │ → 把顶点数据读进来，组装成图元（三角形）        │
│  └──────┬───────┘                                               │
│         ↓                                                       │
│  ┌──────────────┐                                               │
│  │ Vertex       │ ← 你写的 HLSL 代码：对每个顶点做变换            │
│  │ Shader       │   例如：把 3D 坐标投影到 2D 屏幕               │
│  │   (VS)       │ → 输出变换后的顶点位置 + 你想传递的额外数据     │
│  └──────┬───────┘                                               │
│         ↓                                                       │
│  ┌──────────────┐                                               │
│  │ Rasterizer   │ ← 硬件自动完成                                │
│  │   (RS)       │ → 把三角形内部的像素都"填"出来（光栅化）        │
│  │              │   每个像素都会触发下面的像素着色器              │
│  └──────┬───────┘                                               │
│         ↓                                                       │
│  ┌──────────────┐                                               │
│  │ Pixel        │ ← 你写的 HLSL 代码：决定每个像素是什么颜色     │
│  │ Shader       │   例如：根据传入的顶点颜色做插值                │
│  │   (PS)       │ → 输出这个像素的颜色值                         │
│  └──────┬───────┘                                               │
│         ↓                                                       │
│  ┌──────────────┐                                               │
│  │ Output       │ ← 把像素颜色写入渲染目标（也就是你的窗口）      │
│  │ Merger       │   这里还可以做深度测试、模板测试、混合等        │
│  │   (OM)       │ → 最终显示在屏幕上的画面                       │
│  └──────────────┘                                               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

和 OpenGL 对比一下：
- **OpenGL**：你调 `glDrawArrays()`，OpenGL 驱动在幕后自动帮你配置管线状态。简单，但每次绘制都要验证状态是否兼容，效率低。
- **DX12**：你需要提前把管线所有状态打包成一个"PSO"对象，绘制时直接切换 PSO。繁琐，但 GPU 切换状态的开销几乎为零。

---

### 根签名（Root Signature）

#### 什么是根签名？

根签名是 DX12 中一个独特且非常重要的概念。可以这样理解：

**类比**：根签名 = 函数的参数列表。

你写一个 C++ 函数：
```cpp
void DrawScene(Matrix viewProj, Texture diffuseMap, float time) { ... }
```
参数列表 `(Matrix viewProj, Texture diffuseMap, float time)` 就是这个函数的"签名"——它告诉调用者"我需要哪些数据，以什么方式传入"。

在 DX12 中，着色器就是那个"函数"，而根签名就是着色器的"参数列表"。它告诉 GPU：
- 着色器需要哪些资源（常量缓冲区、纹理、采样器等）
- 这些资源放在哪里（哪个寄存器槽位）
- 如何访问它们（通过描述符表、根常量还是根描述符）

#### 根参数的三种类型

```
根签名
├── Root Parameter 0: Descriptor Table（描述符表）
│   └── 指向一块描述符堆，里面可以放多个 CBV/SRV/UAV
│
├── Root Parameter 1: Root Constant（根常量）
│   └── 直接在根签名中内联存放 32 位常量（最多 64 个 DWORD）
│
└── Root Parameter 2: Root Descriptor（根描述符）
    └── 直接指向一个缓冲区或纹理，不需要描述符堆
```

**本课我们不需要传递任何资源**（画三角形只用硬编码的顶点数据），所以用最简单的**空根签名**——没有任何参数。

你可能会问：空根签名怎么行？其实完全可以。就像一个函数没有参数一样：
```cpp
void DrawTriangle() { ... }  // 没有参数，完全合法
```
我们的着色器只用顶点缓冲中的数据（位置和颜色），不需要额外的常量缓冲区或纹理。

#### 为什么根签名这么重要？

在 OpenGL 中，uniform 变量和纹理绑定是隐式的——你在着色器里声明 `uniform mat4 MVP`，然后调用 `glUniformMatrix4fv()` 设置值。驱动在幕后帮你管理这些绑定关系。

DX12 把这一切都显式化了。根签名让 GPU 提前知道"我需要什么资源"，驱动可以提前做好优化，而不是在每次 draw call 时才去检查。这就是 DX12 更高效的原因之一。

---

### PSO（Pipeline State Object）

#### 什么是 PSO？

PSO = 管线状态对象。它把整个渲染管线的配置打包成一个不可变对象。

**类比**：如果说 OpenGL 的做法是"每次画之前逐一设置各种状态开关"，那 DX12 的做法就是"一次性填好所有设置，打包成 PSO，画的时候一键切换"。

#### PSO 包含什么？

```
PSO（D3D12_GRAPHICS_PIPELINE_STATE_DESC）
├── pRootSignature        ← 根签名（着色器的参数声明）
├── VS                    ← 顶点着色器字节码
├── PS                    ← 像素着色器字节码
├── BlendState            ← 混合状态（半透明？关闭？）
├── SampleMask            ← 采样掩码（通常全部启用）
├── RasterizerState       ← 光栅化状态（背面剔除？线框？）
├── DepthStencilState     ← 深度/模板状态
├── InputLayout           ← 顶点输入布局（顶点数据长什么样？）
├── PrimitiveTopologyType ← 图元类型（三角形？线段？）
├── NumRenderTargets      ← 渲染目标数量
├── RTVFormats[]          ← 渲染目标格式（如 RGBA8）
├── DSVFormat             ← 深度缓冲格式
├── SampleDesc            ← 多重采样设置
└── NodeMask              ← GPU 节点掩码
```

#### PSO vs OpenGL 状态设置的对比

| 操作 | OpenGL | DX12 |
|------|--------|------|
| 设置混合模式 | `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` | 填入 `BlendState` |
| 设置背面剔除 | `glEnable(GL_CULL_FACE); glCullFace(GL_BACK)` | 填入 `RasterizerState` |
| 设置深度测试 | `glEnable(GL_DEPTH_TEST)` | 填入 `DepthStencilState` |
| 设置顶点格式 | `glVertexAttribPointer(...)` × N 次 | 填入 `InputLayout` |
| 切换着色器 | `glUseProgram(id)` | 填入 VS/PS 字节码 |
| **切换时机** | 每次 draw call 前都可以改 | 创建 PSO 时确定，运行时一键切换 |

PSO 一旦创建就**不可修改**。想换一种状态？创建另一个 PSO。这看起来浪费，但其实 PSO 在 GPU 内部做了大量预编译和优化，运行时切换的开销远低于 OpenGL 的状态切换。

#### PSO 创建失败的常见原因

创建 PSO 时如果出错，`CreateGraphicsPipelineState` 会返回 `E_INVALIDARG`。常见原因：

1. **着色器编译失败**：HLSL 语法错误，或入口点函数名不匹配
2. **根签名不匹配**：着色器中声明的资源绑定与根签名描述的不一致
3. **顶点输入布局不匹配**：`InputLayout` 描述的顶点格式与着色器中 `struct VSInput` 的定义对不上
4. **格式不支持**：RTV 格式与交换链格式不匹配（比如交换链用 RGBA8，RTV 却声明为 BGRA8）
5. **着色器字节码为空**：忘记编译着色器，或编译出错但没有检查返回值

**调试建议**：每次创建 PSO 后都检查 `HRESULT`，用我们课14写的 `ME_CHECK_HR` 宏。出错时在 Output 窗口查看 D3D12 的调试信息（需要启用 D3D12 调试层）。

---

### HLSL 着色器基础

HLSL（High-Level Shading Language）是 DirectX 的着色器语言，语法类似 C/C++。如果你学过 GLSL，会发现很多相似之处。

#### HLSL vs GLSL 对比

| 特性 | GLSL | HLSL |
|------|------|------|
| 顶点着色器入口 | `void main()` | 任意函数名（通常是 `main` 或 `VSMain`） |
| 片段/像素着色器 | `void main()` | `float4 main(): SV_TARGET` 或 `void main(out ...)` |
| 4 分量向量 | `vec4` | `float4` |
| 3 分量向量 | `vec3` | `float3` |
| 矩阵 | `mat4` | `float4x4` |
| 纹理采样 | `texture(sampler, uv)` | `texture.Sample(sampler, uv)` |
| 顶点输入 | `layout(location=0) in vec3 aPos` | `float3 position : POSITION` |
| Uniform 变量 | `uniform mat4 MVP;` | `cbuffer { float4x4 MVP; }` |
| 输出位置 | `gl_Position = ...` | `output.position = ... // 用 SV_POSITION 语义` |

#### 语义（Semantic）

HLSL 中每个变量后面跟着一个冒号和名字，这叫"语义"。语义告诉 GPU 这个变量的用途：

| 语义 | 含义 | 用在哪 |
|------|------|--------|
| `POSITION` | 顶点位置（来自顶点缓冲） | VS 输入 |
| `COLOR` | 顶点颜色（来自顶点缓冲） | VS 输入/输出 |
| `SV_POSITION` | 变换后的裁剪空间位置 | VS 输出（系统值，GPU 自动使用） |
| `SV_TARGET` | 像素着色器输出的颜色 | PS 输出（系统值，写到渲染目标） |
| `TEXCOORD` | 纹理坐标 | VS 输入/输出 |
| `NORMAL` | 法线向量 | VS 输入 |

**关键区别**：
- `POSITION` 是自定义语义，用来匹配顶点输入布局中的 `POSITION` 槽位
- `SV_POSITION` 是**系统值**（System Value），以 `SV_` 开头。VS 输出必须用 `SV_POSITION`，GPU 会自动用它来做裁剪和光栅化
- `SV_TARGET` 也是系统值，告诉 GPU 把 PS 输出的颜色写到哪个渲染目标（这里只有一个，所以就是后台缓冲区）

#### 顶点着色器示例

```hlsl
// 顶点着色器的输入：从顶点缓冲读到的数据
struct VSInput {
    float3 position : POSITION;   // 顶点位置，3 个 float
    float4 color    : COLOR;      // 顶点颜色，4 个 float (RGBA)
};

// 顶点着色器的输出：传递给光栅化器的数据
struct VSOutput {
    float4 position : SV_POSITION; // 变换后的位置（必须是 float4）
    float4 color    : COLOR;       // 传递颜色（光栅化器会自动插值）
};

VSOutput main(VSInput input) {
    VSOutput output;
    // 把 float3 position 扩展成 float4（齐次坐标，w=1.0 表示点）
    output.position = float4(input.position, 1.0f);
    output.color = input.color;  // 直接传递颜色
    return output;
}
```

#### 像素着色器示例

```hlsl
// 像素着色器接收的是经过光栅化插值后的数据
struct PSInput {
    float4 position : SV_POSITION; // 像素的位置（GPU 自动填充，一般不用）
    float4 color    : COLOR;       // 插值后的颜色
};

// 输出：写到渲染目标的颜色值
float4 main(PSInput input) : SV_TARGET {
    return input.color;  // 直接返回插值颜色
}
```

**光栅化插值**是什么意思？假设三角形的三个顶点分别是红(1,0,0,1)、绿(0,1,0,1)、蓝(0,0,1,1)。光栅化器会对三角形内部的每个像素，根据它离三个顶点的距离做加权平均，产生平滑的颜色过渡。这就是为什么我们能看到彩色渐变——GPU 自动帮我们做了插值。

---

### 顶点缓冲（Vertex Buffer）

#### 什么是顶点？

顶点就是空间中的一个点，加上你需要的附加数据。在本课中，每个顶点包含：

```
┌──────────────────┬──────────────────────┐
│ 位置 (POSITION)  │ 颜色 (COLOR)         │
│ float3: x, y, z  │ float4: r, g, b, a   │
│ 12 字节          │ 16 字节              │
└──────────────────┴──────────────────────┘
每个顶点共 28 字节
```

我们要画一个三角形，需要 3 个顶点：

```
顶点 0: 位置 ( 0.0,  0.5, 0.0)  颜色 (1.0, 0.0, 0.0, 1.0)  ← 红色（顶部）
顶点 1: 位置 ( 0.5, -0.5, 0.0)  颜色 (0.0, 1.0, 0.0, 1.0)  ← 绿色（右下）
顶点 2: 位置 (-0.5, -0.5, 0.0)  颜色 (0.0, 0.0, 1.0, 1.0)  ← 蓝色（左下）
```

坐标系说明：
- X 轴：向右为正
- Y 轴：向上为正
- 坐标范围 [-1, 1]（NDC，Normalized Device Coordinates）
- (0, 0) 在屏幕中心

所以这三个顶点构成的三角形长这样：

```
        ● 红色 (0.0, 0.5)
       / \
      /   \
     /     \
    /  彩色  \
   /   渐变   \
  ●───────────●
蓝色          绿色
(-0.5,-0.5)  (0.5,-0.5)
```

#### 为什么 DX12 创建顶点缓冲这么复杂？

在 OpenGL 中，创建顶点缓冲只需要：
```cpp
GLuint vbo;
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
```

三行代码搞定。DX12 为什么需要这么多步骤？

因为 DX12 要求你明确控制**内存放在哪里**。GPU 有自己的显存，CPU 不能直接往里面写数据。你需要：

```
CPU 内存（你的数组）  →  上传堆（CPU 和 GPU 都能访问）  →  默认堆（只有 GPU 能访问，最快）
                          ↑                                  ↑
                     D3D12_HEAP_TYPE_UPLOAD          D3D12_HEAP_TYPE_DEFAULT
                     CPU 可以 Map() 写入              GPU 访问速度最快
                     GPU 可以读取                     CPU 不能直接写入
```

**为什么不能跳过上传堆？**
因为 CPU 不能直接写 GPU 的默认堆内存。上传堆是唯一一块 CPU 和 GPU 都能访问的共享内存区域。流程是：

1. CPU 把数据写入上传堆（通过 `Map()` / `memcpy()`）
2. GPU 通过命令列表把数据从上传堆复制到默认堆（`CopyBufferRegion`）
3. 之后 GPU 从默认堆读取数据（速度快）

对于小的静态数据（比如我们只有 3 个顶点 = 84 字节），其实可以直接从上传堆读取，不复制到默认堆也可以。但这里我们展示完整流程，因为这在大项目中是标准做法。

---

### 资源屏障（Resource Barrier）

#### 为什么需要资源屏障？

GPU 是高度并行的。当你发出一个命令（比如"把数据从 A 复制到 B"）时，GPU 可能还没执行完，你的下一条命令（比如"从 B 读取数据画三角形"）就已经发出去了。如果 B 还没准备好怎么办？

资源屏障就是用来同步的。它告诉 GPU："在我发出这个屏障之前，这个资源用于复制操作；在屏障之后，它用于读取操作。请确保复制完成后再开始读取。"

```
资源屏障：
复制操作 → 夽复制操作 → 夽复制操作 → 【屏障】→ 读取操作（保证复制已完成）
                                         ↑
                                 GPU 在这里等待直到复制完成
```

#### 常见的屏障类型

本课中我们会遇到两种屏障：

1. **交换链缓冲区状态转换**（课14已经用到）：
   - `PRESENT → RENDER_TARGET`：准备在后台缓冲区上画东西
   - `RENDER_TARGET → PRESENT`：画完了，准备显示

2. **顶点缓冲状态转换**（本课新增）：
   - `COPY_DEST → VERTEX_BUFFER`：数据复制完成，准备作为顶点缓冲使用

---

### 为什么 DX12 画三角形比 OpenGL 多这么多代码？

如果你之前用 OpenGL 画过三角形，大概只需要 50-100 行代码。DX12 可能需要 300+ 行。这不是因为微软喜欢让程序员痛苦，而是设计哲学不同：

| | OpenGL | DX12 |
|---|--------|------|
| **设计目标** | 简单易用 | 高性能、完全控制 |
| **驱动角色** | 驱动帮你做很多事（状态验证、资源管理、同步） | 驱动只做最基本的事，你来控制一切 |
| **错误处理** | 驱动默默处理（可能导致性能下降） | 出错直接崩溃（但你知道哪里错了） |
| **状态管理** | 隐式的，随时可以改 | 显式的，打包成 PSO |
| **内存管理** | 驱动帮你管理 | 你自己管理（上传堆、默认堆） |
| **同步** | 驱动帮你处理 | 你显式管理（围栏、屏障） |

**总结**：OpenGL 像自动挡汽车，DX12 像手动挡赛车。自动挡好开，手动挡能压出更高性能。UE5 使用 DX12（以及 Vulkan，同样是显式 API），正是因为这种显式控制能带来更好的多线程渲染和性能优化。

---

## 操作步骤

### 1. 创建文件

```
src/Renderer/Public/DX12Pipeline.h       ← 管线封装类头文件
src/Renderer/Private/DX12Pipeline.cpp    ← 管线封装类实现
resources/shaders/triangle.vs.hlsl       ← 顶点着色器
resources/shaders/triangle.ps.hlsl       ← 像素着色器
```

---

### 2. 顶点着色器 triangle.vs.hlsl

在 `resources/shaders/` 目录下创建 `triangle.vs.hlsl`：

```hlsl
// =============================================================================
// 顶点着色器：三角形
// =============================================================================
// 顶点着色器对每个顶点执行一次。
// 它的任务是：把顶点位置从模型空间变换到裁剪空间（本课不做什么变换），
//            并把颜色数据传递下去给像素着色器。
// =============================================================================

// 顶点着色器的输入结构 —— 必须与 C++ 端的 InputLayout 描述一致
struct VSInput {
    float3 position : POSITION;   // 顶点位置（来自顶点缓冲）
    float4 color    : COLOR;      // 顶点颜色（来自顶点缓冲）
};

// 顶点着色器的输出结构 —— 会传递给光栅化器，然后插值后传给像素着色器
struct VSOutput {
    float4 position : SV_POSITION; // 裁剪空间位置（系统值，GPU 自动处理）
    float4 color    : COLOR;       // 传递给像素着色器的颜色
};

// 入口函数
VSOutput main(VSInput input) {
    VSOutput output;

    // 本课不做什么矩阵变换，直接把位置扩展成齐次坐标
    // float3 → float4，w 分量 = 1.0 表示这是一个"点"（而非方向向量）
    output.position = float4(input.position, 1.0f);

    // 直接传递颜色，光栅化器会自动在三个顶点之间做插值
    output.color = input.color;

    return output;
}
```

**关键点讲解**：

1. `float3 position : POSITION` — `float3` 是 3 个 float（12 字节），`POSITION` 是语义名。这个语义名要和 C++ 端 `D3D12_INPUT_ELEMENT_DESC` 中定义的一致，GPU 靠这个名字匹配。

2. `float4 position : SV_POSITION` — 输出**必须**用 `SV_POSITION`，这是一个系统值语义。GPU 用它来确定顶点在屏幕上的位置。

3. `float4(input.position, 1.0f)` — 这是一个构造函数调用，把 `float3` 扩展成 `float4`。最后一个参数是 w 分量。

---

### 3. 像素着色器 triangle.ps.hlsl

在 `resources/shaders/` 目录下创建 `triangle.ps.hlsl`：

```hlsl
// =============================================================================
// 像素着色器：三角形
// =============================================================================
// 像素着色器对每个像素执行一次（光栅化后）。
// 它的任务是：决定这个像素最终的颜色。
// =============================================================================

// 像素着色器的输入结构 —— 与顶点着色器的输出对应
// 注意：这里的值是经过光栅化器插值后的！
// 比如三角形中心的像素，颜色大约是三个顶点颜色的平均值
struct PSInput {
    float4 position : SV_POSITION; // 像素的屏幕位置（通常不用，但结构要保持一致）
    float4 color    : COLOR;       // 插值后的颜色
};

// 入口函数
// 返回 float4，语义 SV_TARGET 表示输出到渲染目标（后台缓冲区）
float4 main(PSInput input) : SV_TARGET {
    // 直接返回插值后的颜色
    // 光栅化器已经帮我们把三个顶点的颜色做了平滑插值
    // 所以三角形内部会呈现红-绿-蓝的平滑渐变
    return input.color;
}
```

**关键点讲解**：

1. `PSInput` 结构和 VS 的 `VSOutput` 结构是对应的。光栅化器把 VS 输出的数据插值后填入 PSInput。

2. `SV_TARGET` 表示这个返回值要写到渲染目标。如果有多个渲染目标（MRT），可以用 `SV_TARGET0`、`SV_TARGET1` 等。

3. 本课的 PS 非常简单——直接返回颜色。后面课程会在这里做 PBR 光照计算。

---

### 4. DX12Pipeline.h — 管线封装类

在 `src/Renderer/Public/` 目录下创建 `DX12Pipeline.h`：

```cpp
#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <string>

// 使用 Microsoft::WRL 的 ComPtr 管理 COM 对象的引用计数
// 比裸指针安全，离开作用域自动 Release()
using Microsoft::WRL::ComPtr;

// 前向声明，避免头文件互相包含
class DX12Device;

// =============================================================================
// DX12Pipeline — 封装渲染管线所需的所有资源
// =============================================================================
// 包含：根签名、PSO、顶点缓冲、着色器编译
// 职责：初始化管线、绘制三角形、清理资源
// =============================================================================
class DX12Pipeline {
public:
    DX12Pipeline() = default;
    ~DX12Pipeline() { Destroy(); }

    // 禁止拷贝（COM 对象不能随意拷贝）
    DX12Pipeline(const DX12Pipeline&) = delete;
    DX12Pipeline& operator=(const DX12Pipeline&) = delete;

    // ===== 生命周期 =====

    /// 初始化管线：编译着色器、创建根签名、创建 PSO、创建顶点缓冲
    /// @param device  DX12 设备封装对象（课14创建的）
    void Init(DX12Device& device);

    /// 绘制三角形
    /// @param cmdList  命令列表（从 DX12Device 获取）
    void Draw(ID3D12GraphicsCommandList* cmdList);

    /// 释放所有资源
    void Destroy();

private:
    // ===== 内部辅助方法 =====

    /// 编译 HLSL 着色器文件
    /// @param filePath  HLSL 文件路径（如 "resources/shaders/triangle.vs.hlsl"）
    /// @param entryPoint  入口函数名（如 "main"）
    /// @param target  着色器目标（如 "vs_5_0" 表示顶点着色器 5.0）
    /// @param[out] outBytecode  编译后的字节码
    /// @return true 编译成功
    bool CompileShader(const std::wstring& filePath,
                       const char* entryPoint,
                       const char* target,
                       ComPtr<ID3DBlob>& outBytecode);

    /// 创建顶点缓冲（上传顶点数据到 GPU）
    void CreateVertexBuffer(DX12Device& device);

    // ===== 资源 =====

    // 根签名：描述着色器需要哪些外部资源
    ComPtr<ID3D12RootSignature> rootSignature_;

    // 管线状态对象：打包整个渲染管线的配置
    ComPtr<ID3D12PipelineState> pso_;

    // 顶点缓冲：存储顶点数据（位置+颜色）
    ComPtr<ID3D12Resource> vertexBuffer_;

    // 上传缓冲：CPU 端中转站，用于把数据传到 GPU
    ComPtr<ID3D12Resource> uploadBuffer_;

    // 顶点缓冲视图：描述顶点缓冲的位置和格式
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_ = {};

    // 编译后的着色器字节码（PSO 创建后就不再需要了，但保留以便调试）
    ComPtr<ID3DBlob> vertexShaderBytecode_;
    ComPtr<ID3DBlob> pixelShaderBytecode_;
};
```

**头文件讲解**：

1. `ComPtr` 是智能指针，用来自动管理 DirectX COM 对象的生命周期。它会在引用计数归零时自动调用 `Release()`，避免内存泄漏。

2. `D3D12_VERTEX_BUFFER_VIEW` 不是 COM 对象，它只是一个描述结构体（告诉 GPU 顶点缓冲在哪里、有多大、每个顶点的步长），所以不需要用 ComPtr。

3. 我们同时保留了 `vertexBuffer_`（默认堆，GPU 读取用）和 `uploadBuffer_`（上传堆，CPU 写入用）。对于小数据量，可以只用上传堆，但这里展示完整流程。

---

### 5. DX12Pipeline.cpp — 实现文件

在 `src/Renderer/Private/` 目录下创建 `DX12Pipeline.cpp`。

这是本课的核心文件，代码量较大，我们分步骤讲解。

#### 5.1 头文件包含和顶点结构定义

```cpp
#include "Renderer/Public/DX12Pipeline.h"
#include "Renderer/Public/DX12Device.h"
#include "Core/Public/Logger.h"

#include <d3dcompiler.h>   // D3DCompileFromFile
#include <d3d12.h>
#include <dxgi1_6.h>

// 链接 d3dcompiler.lib，这样就不需要手动在 CMakeLists 里添加
#pragma comment(lib, "d3dcompiler.lib")

// =============================================================================
// 顶点结构定义
// =============================================================================
// 这个结构决定了每个顶点有多少数据、如何排列。
// 必须与 HLSL 中 VSInput 的定义一致！
// =============================================================================
struct Vertex {
    float position[3];  // x, y, z  —— 12 字节
    float color[4];     // r, g, b, a  —— 16 字节
    // 总共 28 字节
};

// =============================================================================
// 三个顶点数据：一个彩色三角形
// =============================================================================
static const Vertex triangleVertices[] = {
    // 位置 (x, y, z)              颜色 (r, g, b, a)
    { {  0.0f,  0.5f, 0.0f },    { 1.0f, 0.0f, 0.0f, 1.0f } },  // 顶部 — 红色
    { {  0.5f, -0.5f, 0.0f },    { 0.0f, 1.0f, 0.0f, 1.0f } },  // 右下 — 绿色
    { { -0.5f, -0.5f, 0.0f },    { 0.0f, 0.0f, 1.0f, 1.0f } },  // 左下 — 蓝色
};
static const UINT vertexCount = 3;
```

**为什么用 C 风格数组 `float[3]` 而不是 `float3`？**
为了避免结构体对齐问题。用 `float[3]` 确保数据紧密排列，没有填充字节。如果你用 HLSL 的 `float3` 或者 GLM 的 `vec3`，编译器可能添加填充字节对齐到 16 字节边界，导致数据不匹配。

---

#### 5.2 Init 方法 — 完整的管线初始化

```cpp
void DX12Pipeline::Init(DX12Device& device) {
    ME_LOG_INFO("开始初始化 DX12 渲染管线...");

    ID3D12Device* d3dDevice = device.GetDevice();
    if (!d3dDevice) {
        ME_LOG_ERROR("DX12 设备无效，无法初始化管线");
        return;
    }

    // =====================================================================
    // 第 1 步：编译 HLSL 着色器
    // =====================================================================
    // D3DCompileFromFile 在运行时编译 HLSL 源文件为字节码。
    // 另一种方式是离线编译（用 fxc.exe 或 dxc.exe），生成 .cso 文件，
    // 运行时直接加载，速度更快。本课用运行时编译，方便学习调试。

    // 编译顶点着色器
    // vs_5_0 = Vertex Shader, Shader Model 5.0（DX11 级别，DX12 兼容）
    if (!CompileShader(L"resources/shaders/triangle.vs.hlsl",
                       "main", "vs_5_0",
                       vertexShaderBytecode_)) {
        ME_LOG_ERROR("顶点着色器编译失败！");
        return;
    }
    ME_LOG_INFO("顶点着色器编译成功");

    // 编译像素着色器
    // ps_5_0 = Pixel Shader, Shader Model 5.0
    if (!CompileShader(L"resources/shaders/triangle.ps.hlsl",
                       "main", "ps_5_0",
                       pixelShaderBytecode_)) {
        ME_LOG_ERROR("像素着色器编译失败！");
        return;
    }
    ME_LOG_INFO("像素着色器编译成功");

    // =====================================================================
    // 第 2 步：定义顶点输入布局
    // =====================================================================
    // 告诉 GPU："我的顶点数据长什么样，每个字段在什么偏移量"
    // 这必须与 C++ 端的 Vertex 结构和 HLSL 端的 VSInput 完全对应！

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        // 语义名     语义索引  格式              输入槽  字节偏移  分类
        // --------  -------  ----------------  ------  -------  ----------
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        // 位置：3 个 float，在顶点结构的偏移 0 处
        // D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA = 每个顶点读一次

        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        // 颜色：4 个 float，在顶点结构的偏移 12 处（紧跟 position 的 12 字节之后）
    };

    // =====================================================================
    // 第 3 步：创建根签名
    // =====================================================================
    // 本课用空根签名 — 不需要任何外部资源（常量缓冲区、纹理等）。
    // 空根签名也是合法的！

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 0;          // 没有根参数
    rootSigDesc.pParameters = nullptr;      // 参数数组为空
    rootSigDesc.NumStaticSamplers = 0;      // 没有静态采样器
    rootSigDesc.pStaticSamplers = nullptr;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    // ↑ 这个标志告诉 GPU：我会用输入装配器（IA）读取顶点数据
    // 如果不设这个标志，PSO 创建时会报错

    // 序列化根签名（把描述结构转成二进制格式）
    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,       // 根签名版本 1
        &serializedRootSig,
        &errorBlob                          // 如果出错，错误信息写到这里
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            ME_LOG_ERROR("根签名序列化失败: %s",
                         (const char*)errorBlob->GetBufferPointer());
        }
        return;
    }

    // 用序列化后的数据创建根签名对象
    hr = d3dDevice->CreateRootSignature(
        0,                                              // GPU 节点掩码（单 GPU 用 0）
        serializedRootSig->GetBufferPointer(),          // 序列化数据指针
        serializedRootSig->GetBufferSize(),             // 数据大小
        IID_PPV_ARGS(&rootSignature_)                   // 输出接口
    );
    if (FAILED(hr)) {
        ME_LOG_ERROR("创建根签名失败: hr=0x%08X", hr);
        return;
    }
    ME_LOG_INFO("根签名创建成功");

    // =====================================================================
    // 第 4 步：填充 PSO 描述结构
    // =====================================================================
    // 这是最长的一步，但大部分字段都是默认值。
    // 想象你在填一张表格：每个选项都有明确的含义。

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    // --- 根签名 ---
    psoDesc.pRootSignature = rootSignature_.Get();

    // --- 着色器字节码 ---
    psoDesc.VS = {
        vertexShaderBytecode_->GetBufferPointer(),
        vertexShaderBytecode_->GetBufferSize()
    };
    psoDesc.PS = {
        pixelShaderBytecode_->GetBufferPointer(),
        pixelShaderBytecode_->GetBufferSize()
    };
    // GS、HS、DS、AS、MS 都不使用（置空，默认值）

    // --- 混合状态（Blend State）---
    // 本课不需要透明度混合，关闭混合（每个像素直接覆盖）
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;            // 不用 Alpha-To-Coverage
    blendDesc.IndependentBlendEnable = FALSE;           // 所有渲染目标用同一套混合公式
    blendDesc.RenderTarget[0].BlendEnable = FALSE;      // 关闭混合
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;                   // 写入 RGBA 全部通道
    psoDesc.BlendState = blendDesc;

    // --- 采样掩码 ---
    psoDesc.SampleMask = UINT_MAX;                      // 全部采样通过

    // --- 光栅化状态（Rasterizer State）---
    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;    // 实心填充（不是线框）
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;     // 不做背面剔除（两面都画）
    rasterizerDesc.FrontCounterClockwise = FALSE;       // 顺时针为正面（DX 默认）
    rasterizerDesc.DepthBias = 0;
    rasterizerDesc.DepthBiasClamp = 0.0f;
    rasterizerDesc.SlopeScaledDepthBias = 0.0f;
    rasterizerDesc.DepthClipEnable = TRUE;              // 裁剪超出远平面的像素
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount = 0;
    rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    psoDesc.RasterizerState = rasterizerDesc;

    // --- 深度/模板状态 ---
    // 本课不用深度缓冲，全部关闭
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = FALSE;               // 关闭深度测试
    depthStencilDesc.StencilEnable = FALSE;             // 关闭模板测试
    psoDesc.DepthStencilState = depthStencilDesc;

    // --- 顶点输入布局 ---
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };

    // --- 图元拓扑类型 ---
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // 告诉 GPU 我们要画三角形

    // --- 渲染目标格式 ---
    psoDesc.NumRenderTargets = 1;                       // 只有一个渲染目标
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;// RGBA 8位无符号
    // ↑ 这个格式必须和交换链的格式一致！否则 PSO 创建会失败

    // --- 深度缓冲格式 ---
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;            // 不用深度缓冲

    // --- 多重采样 ---
    psoDesc.SampleDesc.Count = 1;                       // 每像素 1 个采样（不用 MSAA）
    psoDesc.SampleDesc.Quality = 0;

    // =====================================================================
    // 第 5 步：创建 PSO
    // =====================================================================
    hr = d3dDevice->CreateGraphicsPipelineState(&psoDesc,
                                                 IID_PPV_ARGS(&pso_));
    if (FAILED(hr)) {
        ME_LOG_ERROR("创建 PSO 失败: hr=0x%08X", hr);
        ME_LOG_ERROR("常见原因：着色器编译失败、根签名不匹配、格式不匹配");
        return;
    }
    ME_LOG_INFO("PSO 创建成功");

    // =====================================================================
    // 第 6 步：创建顶点缓冲
    // =====================================================================
    CreateVertexBuffer(device);

    ME_LOG_INFO("DX12 渲染管线初始化完成！");
}
```

---

#### 5.3 CreateVertexBuffer — 顶点缓冲创建（重点！）

这是 DX12 中比较繁琐的部分。我们一步步来，每一步都有详细注释。

```cpp
void DX12Pipeline::CreateVertexBuffer(DX12Device& device) {
    ID3D12Device* d3dDevice = device.GetDevice();

    // 顶点数据总大小
    const UINT vertexBufferSize = sizeof(triangleVertices);
    // 3 个顶点 × 28 字节 = 84 字节

    // =================================================================
    // 步骤 1：创建上传堆缓冲
    // =================================================================
    // 上传堆是 CPU 可写的内存区域，用于把数据从 CPU 传到 GPU。
    // 它的位置可能在系统内存（DDR）或 GPU 显存的特殊区域。
    // 关键是：CPU 可以通过 Map() 函数获得这块内存的指针，然后写入数据。

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;      // 上传堆
    uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeapProps.CreationNodeMask = 0;
    uploadHeapProps.VisibleNodeMask = 0;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;  // 缓冲类型资源
    bufferDesc.Alignment = 0;
    bufferDesc.Width = vertexBufferSize;                      // 缓冲大小
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;                  // 缓冲没有像素格式
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;      // 行主序排列
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = d3dDevice->CreateCommittedResource(
        &uploadHeapProps,                              // 堆属性
        D3D12_HEAP_FLAG_NONE,                          // 堆标志
        &bufferDesc,                                   // 资源描述
        D3D12_RESOURCE_STATE_GENERIC_READ,             // 初始状态：通用读取
        // ↑ 上传堆必须用 GENERIC_READ 状态，因为 GPU 会从中读取数据
        nullptr,                                       // 清除值（缓冲不需要）
        IID_PPV_ARGS(&uploadBuffer_)                   // 输出
    );
    if (FAILED(hr)) {
        ME_LOG_ERROR("创建上传堆缓冲失败: hr=0x%08X", hr);
        return;
    }

    // =================================================================
    // 步骤 2：映射上传堆、拷贝顶点数据
    // =================================================================
    // Map() 函数获取上传堆内存的 CPU 端指针。
    // 之后我们可以像普通内存一样写入数据。

    void* pData = nullptr;
    hr = uploadBuffer_->Map(
        0,                    // 子资源索引（缓冲只有一个）
        nullptr,              // 映射范围（nullptr = 映射整个缓冲）
        &pData                // 输出：CPU 端指针
    );
    if (FAILED(hr)) {
        ME_LOG_ERROR("映射上传堆失败: hr=0x%08X", hr);
        return;
    }

    // 把顶点数据拷贝到上传堆
    memcpy(pData, triangleVertices, vertexBufferSize);

    // Unmap() 告诉 GPU "我已经写完了，你可以读取了"
    // 参数 nullptr 表示整个缓冲都已写入
    uploadBuffer_->Unmap(0, nullptr);

    ME_LOG_INFO("顶点数据已上传到上传堆（%u 字节）", vertexBufferSize);

    // =================================================================
    // 步骤 3：创建默认堆缓冲（GPU 高速访问）
    // =================================================================
    // 对于静态数据（不频繁更新的顶点缓冲），最佳实践是把数据复制到默认堆。
    // 默认堆在 GPU 显存中，GPU 访问速度最快。
    // 注意：CPU 不能直接写默认堆，所以需要从上传堆复制过去。

    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;    // 默认堆
    defaultHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeapProps.CreationNodeMask = 0;
    defaultHeapProps.VisibleNodeMask = 0;

    hr = d3dDevice->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,                // 初始状态：复制目标
        // ↑ 因为接下来我们要从上传堆复制数据到这里
        nullptr,
        IID_PPV_ARGS(&vertexBuffer_)
    );
    if (FAILED(hr)) {
        ME_LOG_ERROR("创建默认堆缓冲失败: hr=0x%08X", hr);
        return;
    }

    // =================================================================
    // 步骤 4：用命令列表把数据从上传堆复制到默认堆
    // =================================================================
    // 注意：我们不能直接复制！必须通过 GPU 的命令队列来执行复制。
    // 流程：创建临时命令列表 → 记录复制命令 → 执行 → 等待完成

    // 创建临时命令分配器（用于这次复制操作）
    ComPtr<ID3D12CommandAllocator> copyCmdAlloc;
    hr = d3dDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&copyCmdAlloc)
    );

    // 创建临时命令列表
    ComPtr<ID3D12GraphicsCommandList> copyCmdList;
    hr = d3dDevice->CreateCommandList(
        0,                                              // GPU 节点掩码
        D3D12_COMMAND_LIST_TYPE_DIRECT,                 // 直接命令列表（可执行所有操作）
        copyCmdAlloc.Get(),                             // 命令分配器
        nullptr,                                        // 初始 PSO（不需要）
        IID_PPV_ARGS(&copyCmdList)
    );

    // 记录复制命令
    copyCmdList->CopyBufferRegion(
        vertexBuffer_.Get(), 0,                         // 目标：默认堆缓冲，偏移 0
        uploadBuffer_.Get(), 0,                         // 来源：上传堆缓冲，偏移 0
        vertexBufferSize                                // 复制大小
    );

    // 添加资源屏障：把顶点缓冲从 COPY_DEST 转为 VERTEX_BUFFER 状态
    // 这告诉 GPU："复制完成了，接下来把它当顶点缓冲用"
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = vertexBuffer_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    copyCmdList->ResourceBarrier(1, &barrier);

    // 关闭命令列表（必须关闭才能执行）
    copyCmdList->Close();

    // 执行命令列表
    ID3D12CommandList* cmdLists[] = { copyCmdList.Get() };
    device.GetCommandQueue()->ExecuteCommandLists(
        _countof(cmdLists), cmdLists
    );

    // 等待 GPU 执行完毕
    // 这里用简单的围栏同步。在实际项目中，你可能不会为了 84 字节等待 GPU，
    // 但在初始化阶段等一下是完全可接受的。
    device.WaitForGPU();

    ME_LOG_INFO("顶点缓冲已创建并上传到默认堆");

    // =================================================================
    // 步骤 5：创建顶点缓冲视图（VBV）
    // =================================================================
    // 顶点缓冲视图是一个描述结构体，告诉 GPU 顶点缓冲在哪里、有多大。
    // 绘制时通过 IASetVertexBuffers() 设置到管线。

    vertexBufferView_.BufferLocation =
        vertexBuffer_->GetGPUVirtualAddress();          // 缓冲在 GPU 内存的地址
    vertexBufferView_.SizeInBytes = vertexBufferSize;   // 缓冲总大小
    vertexBufferView_.StrideInBytes = sizeof(Vertex);   // 每个顶点的步长（28 字节）
    // GPU 用步长来计算第 N 个顶点在哪：
    // 顶点 N 的地址 = BufferLocation + N * StrideInBytes

    ME_LOG_INFO("顶点缓冲视图创建完成（步长: %u 字节）", sizeof(Vertex));
}
```

**创建顶点缓冲的完整流程回顾**：

```
CPU 内存              上传堆                    默认堆
┌─────────┐          ┌─────────┐              ┌─────────┐
│ 三角形   │ ─────→  │  上传堆   │ ─────GPU──→ │  默认堆   │
│ 顶点数据  │ memcpy  │  缓冲    │  CopyBuffer  │  缓冲    │
│ (84字节) │          │ (84字节) │  Region     │ (84字节) │
└─────────┘          └─────────┘              └─────────┘
  C++ 数组            Map/Unmap              最终 GPU 读取
                      CPU 可写               GPU 可高速读取
```

---

#### 5.4 Draw 方法 — 绘制三角形

```cpp
void DX12Pipeline::Draw(ID3D12GraphicsCommandList* cmdList) {
    if (!cmdList || !pso_ || !rootSignature_.Get()) {
        return;  // 安全检查
    }

    // =================================================================
    // 设置渲染管线状态
    // =================================================================

    // 1. 设置根签名
    // 告诉命令列表"后续的绘制操作使用这个根签名"
    cmdList->SetGraphicsRootSignature(rootSignature_.Get());

    // 2. 设置管线状态
    // 这一步切换整个渲染管线的配置（着色器、混合、光栅化等）
    cmdList->SetPipelineState(pso_.Get());

    // 3. 设置图元拓扑
    // 告诉输入装配器如何把顶点组装成图元
    // TRIANGLELIST = 每 3 个顶点组成一个三角形
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 4. 设置顶点缓冲
    // 告诉输入装配器从哪个缓冲读取顶点数据
    cmdList->IASetVertexBuffers(
        0,                              // 输入槽索引（从 0 开始）
        1,                              // 绑定几个顶点缓冲
        &vertexBufferView_              // 顶点缓冲视图数组
    );

    // 5. 发出绘制命令！
    // DrawInstanced = 绘制实例化的几何体
    cmdList->DrawInstanced(
        vertexCount,                    // 每个实例的顶点数（3 个）
        1,                              // 实例数量（1 个，不使用实例化）
        0,                              // 起始顶点索引（从第 0 个开始）
        0                               // 起始实例索引（从第 0 个开始）
    );

    // GPU 收到 DrawInstanced 后的执行流程：
    // 1. IA 从顶点缓冲读取 3 个顶点
    // 2. VS 对每个顶点执行一次（共 3 次）
    // 3. RS 把三角形光栅化为像素
    // 4. PS 对每个像素执行一次（可能几千次）
    // 5. OM 把像素颜色写入后台缓冲区
}
```

---

#### 5.5 CompileShader — 着色器编译辅助函数

```cpp
bool DX12Pipeline::CompileShader(const std::wstring& filePath,
                                  const char* entryPoint,
                                  const char* target,
                                  ComPtr<ID3DBlob>& outBytecode) {
    // =================================================================
    // D3DCompileFromFile 参数说明
    // =================================================================
    // pFileName:    HLSL 文件路径（宽字符）
    // pDefines:     预处理器宏定义（本课不用）
    // pInclude:     include 处理器（本课不用）
    // pEntrypoint:  入口函数名（"main"）
    // pTarget:      着色器目标（"vs_5_0" = 顶点着色器 SM5.0）
    // Flags1:       编译选项
    // Flags2:       效果编译选项（不用）
    // ppCode:       输出编译后的字节码
    // ppErrorMsgs:  输出错误信息

    ComPtr<ID3DBlob> errorBlob;

    UINT compileFlags = 0;
#ifdef _DEBUG
    // 调试模式下启用着色器调试信息
    compileFlags |= D3DCOMPILE_DEBUG;
    // 跳过优化，方便调试（发布版不要加这个）
    compileFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompileFromFile(
        filePath.c_str(),
        nullptr,                // 没有预处理器宏
        nullptr,                // 默认 include 处理
        entryPoint,
        target,
        compileFlags,
        0,
        &outBytecode,
        &errorBlob
    );

    if (FAILED(hr)) {
        // 编译失败，输出错误信息
        if (errorBlob) {
            const char* errorMsg =
                static_cast<const char*>(errorBlob->GetBufferPointer());
            ME_LOG_ERROR("着色器编译错误 [%ls]: %s",
                         filePath.c_str(), errorMsg);
        } else {
            ME_LOG_ERROR("着色器编译失败 [%ls]: hr=0x%08X",
                         filePath.c_str(), hr);
        }
        return false;
    }

    return true;
}
```

**着色器编译讲解**：

1. **运行时编译 vs 离线编译**：
   - 运行时编译（`D3DCompileFromFile`）：游戏运行时编译 HLSL。优点是方便调试和热重载，缺点是启动慢、需要分发 HLSL 源码。
   - 离线编译（`fxc.exe` 或 `dxc.exe`）：构建时把 HLSL 编译成 `.cso`（Compiled Shader Object）文件，运行时直接加载。优点是启动快、源码保护，缺点是调试不便。
   - UE5 使用离线编译，通过 Unreal Build Tool 自动调用 fxc/dxc。

2. **着色器目标字符串**：
   - `"vs_5_0"` = Vertex Shader, Shader Model 5.0
   - `"ps_5_0"` = Pixel Shader, Shader Model 5.0
   - `"vs_6_0"` = 使用 DXC 编译器的 Shader Model 6.0（需要 DXC，不支持 D3DCompileFromFile）
   - 本课用 SM 5.0，兼容性最好。

3. **调试标志**：
   - `D3DCOMPILE_DEBUG`：在字节码中嵌入调试信息，可以用图形调试工具（如 PIX）逐行调试着色器
   - `D3DCOMPILE_SKIP_OPTIMIZATION`：禁止优化，确保代码结构不被打乱，方便调试

---

#### 5.6 Destroy — 清理资源

```cpp
void DX12Pipeline::Destroy() {
    // ComPtr 会自动调用 Release()，我们只需要把指针置空
    // 但为了清晰，显式释放

    // 注意释放顺序：先释放引用 GPU 资源的对象，再释放资源本身
    // ComPtr 的 Reset() 会减少引用计数，归零时调用 Release()

    vertexBufferView_ = {};             // 清空视图（不是 COM 对象，直接重置）

    vertexBuffer_.Reset();              // 释放默认堆缓冲
    uploadBuffer_.Reset();              // 释放上传堆缓冲
    pso_.Reset();                       // 释放管线状态对象
    rootSignature_.Reset();             // 释放根签名
    vertexShaderBytecode_.Reset();      // 释放着色器字节码
    pixelShaderBytecode_.Reset();

    ME_LOG_INFO("DX12 渲染管线资源已释放");
}
```

---

### 6. 修改 CMakeLists.txt

在项目的 `CMakeLists.txt` 中，需要添加 `d3d12.lib` 和 `d3dcompiler.lib` 的链接。

```cmake
# 在 target_link_libraries 中添加 DX12 相关库
if(WIN32)
    target_link_libraries(MaterialEditor PRIVATE
        d3d12
        d3dcompiler
        dxgi
        # ... 其他已有的库
    )
endif()
```

注意：虽然代码中已经用 `#pragma comment(lib, "d3dcompiler.lib")` 链接了 d3dcompiler，但最好在 CMakeLists.txt 中也显式链接，这样更清晰。

---

### 7. 验证代码 — 修改 main.cpp

在课14的基础上，加入 `DX12Pipeline` 来绘制三角形。

```cpp
#include <QApplication>
#include <QWidget>
#include <QTimer>
#include "Renderer/Public/DX12Device.h"
#include "Renderer/Public/DX12Pipeline.h"
#include "Core/Public/Logger.h"

int main(int argc, char* argv[]) {
    // 初始化日志系统
    ME::Logger::Init();
    ME_LOG_INFO("材质编辑器启动...");

    // 创建 Qt 应用
    QApplication app(argc, argv);

    // 创建一个 Qt 窗口作为 DX12 的渲染目标
    QWidget renderWindow;
    renderWindow.setWindowTitle("DX12 Test - Colorful Triangle");
    renderWindow.resize(800, 600);
    renderWindow.show();

    // 初始化 DX12 设备（课14的代码）
    DX12Device device;
    HWND hwnd = reinterpret_cast<HWND>(renderWindow.winId());
    device.Init(hwnd, 800, 600);
    ME_LOG_INFO("DX12 设备初始化完成");

    // 初始化渲染管线（本课新增）
    DX12Pipeline pipeline;
    pipeline.Init(device);
    ME_LOG_INFO("DX12 渲染管线初始化完成，准备绘制三角形");

    // 渲染循环
    // QTimer 每 16ms 触发一次（约 60 FPS）
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        // 开始新的一帧
        device.BeginFrame();

        // 获取命令列表（BeginFrame 已经重置了命令列表）
        ID3D12GraphicsCommandList* cmdList = device.GetCommandList();

        // ========================================
        // 在这里插入我们的绘制代码！
        // ========================================

        // 绘制彩色三角形
        pipeline.Draw(cmdList);

        // 结束这一帧（执行命令、呈现）
        device.EndFrame();
    });
    timer.start(16);  // ~60 FPS

    ME_LOG_INFO("进入主循环...");

    // 进入 Qt 事件循环
    int result = app.exec();

    // 清理资源（先管线后设备）
    pipeline.Destroy();
    device.Destroy();

    ME_LOG_INFO("材质编辑器关闭");
    return result;
}
```

**关于渲染循环的说明**：

注意 `BeginFrame()` 和 `EndFrame()` 之间我们调用 `pipeline.Draw()`。这意味着每一帧我们都在重新设置管线状态并绘制三角形。虽然三角形是静态的，但 DX12 的命令列表每次都需要重新记录命令（不像 OpenGL 可以"设置一次，一直有效"）。

这是正常的设计。在实际项目中，你会把 PSO 和根签名的设置放在需要切换的时候才调用，而不是每帧都设置。但对于一个简单的三角形，每帧设置一次也没问题。

---

## 验证

### 运行步骤

1. 确保文件结构正确：
   ```
   MaterialEditor/
   ├── src/
   │   └── Renderer/
   │       ├── Public/
   │       │   ├── DX12Device.h      （课14已创建）
   │       │   └── DX12Pipeline.h    （本课新增）
   │       └── Private/
   │           ├── DX12Device.cpp     （课14已创建）
   │           └── DX12Pipeline.cpp   （本课新增）
   ├── resources/
   │   └── shaders/
   │       ├── triangle.vs.hlsl      （本课新增）
   │       └── triangle.ps.hlsl      （本课新增）
   └── CMakeLists.txt
   ```

2. 确保 CMakeLists.txt 链接了 `d3d12`、`d3dcompiler`、`dxgi`

3. 用 CMake 构建：
   ```bash
   cd build
   cmake --build . --config Debug
   ```

4. 运行程序

### 预期结果

你会看到一个 800×600 的窗口，里面有一个彩色三角形：
- 顶部顶点是**红色**
- 右下顶点是**绿色**
- 左下顶点是**蓝色**
- 三角形内部是红-绿-蓝的**平滑渐变**（光栅化器自动插值）
- 背景色是课14中设置的清屏颜色（深灰色或黑色）

### 验证清单

1. 控制台输出着色器编译成功的日志
2. 控制台输出根签名和 PSO 创建成功的日志
3. 窗口中显示彩色三角形
4. 三角形三个顶点颜色分别是红、绿、蓝
5. 颜色在三角形内部平滑过渡
6. 窗口可以正常关闭（不崩溃）

### 常见问题排查

**问题：着色器编译失败，提示找不到文件**
- 检查 HLSL 文件路径是否正确
- 确保 `resources/shaders/` 目录存在且文件名拼写正确
- 运行时工作目录可能不是项目根目录，试试用绝对路径

**问题：PSO 创建失败（hr=0x80070057 = E_INVALIDARG）**
- 检查 RTV 格式是否与交换链格式一致（都用 `DXGI_FORMAT_R8G8B8A8_UNORM`）
- 检查根签名是否设置了 `ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT` 标志
- 检查着色器字节码是否有效（不为空）
- 启用 D3D12 调试层查看详细错误信息

**问题：三角形不显示**
- 检查顶点缓冲是否创建成功
- 检查 `DrawInstanced` 的参数是否正确
- 检查清屏颜色是否与三角形颜色冲突
- 检查视口和裁剪矩形是否设置正确（课14的 `BeginFrame` 中应已设置）

**问题：三角形颜色错误（全是黑色）**
- 检查像素着色器是否正确编译和链接
- 检查顶点输入布局的颜色偏移量是否正确（应该是 12，不是 16）
- 检查 HLSL 中的语义名是否与 InputLayout 中的一致

**问题：窗口 resize 后三角形消失或变形**
- 课14的 DX12Device 需要处理窗口大小变化（重建交换链和 RTV）
- 如果还没实现 resize 处理，先固定窗口大小

---

## 深度扩展：材质域（Material Domain）与混合模式（Blend Mode）

> 本节是 PSO/BlendState 概念的进阶。基础三角形只用了 Opaque 一种 blend 模式，
> 实际材质编辑器要支持玻璃（半透明）、火焰（加色）、UI（覆盖）、屏幕后处理等完全不同的渲染行为。
> 这一块讲清楚：**domain + blendMode 决定了 shader 怎么生成 + PSO 怎么配**。

### 为什么需要这两个枚举？

一个材质编辑器要支持很多种"材质类型"：

| 用途 | 例子 | 渲染行为 |
|------|------|----------|
| 普通不透明表面 | 墙壁、地板 | 走完整 PBR 光照，写入渲染目标 |
| 自发光或调试 | 调试线、纯色物体 | 不参与光照，直接输出 Emissive |
| 半透明表面 | 玻璃、水、烟 | 走 alpha 混合，需要排序 |
| 屏幕后处理 | bloom、tone map | 输入是场景色，PS 改写整个画面 |
| 贴花 | 弹孔、血迹 | 投影到现有几何体上 |
| UI | 文字、按钮 | 屏幕空间，alpha 混合，无光照 |

这 6 种行为在 shader 结构、blend state、光照计算上都不同。我们用两个**正交**的枚举描述：

- **EMaterialDomain（材质域）** —— 决定 shader **结构**（PS 输入输出长什么样、是否走光照）
- **EBlendMode（混合模式）** —— 决定 OM 阶段如何把 PS 输出与渲染目标**混合**

UE5 在材质编辑器里**第一件事**就是让你选这两个：Domain 下拉框 + Blend Mode 下拉框。我们也照这个分工。

---

### 1. 枚举定义

放在 `Types.h`（L2 数据模型层，零编译器/HLSL 知识，符合分层铁律）：

```cpp
// =============================================================================
// 材质域：决定 shader 结构和光照行为
// =============================================================================
enum class EMaterialDomain : uint8_t {
    Surface,            // 表面（默认）—— 走完整 PBR 光照，输出 BaseColor/Metallic/Roughness/Normal/...
    Unlit,              // 不参与光照 —— PS 直接输出 Emissive 颜色（调试可视化、HUD）
    PostProcess,        // 屏幕后处理 —— 输入是场景色纹理，PS 改写整个画面（bloom、tone map）
    Decal,              // 贴花 —— 投影到现有几何体（本课程只讲概念，不实现）
    UserInterface,      // UI —— 屏幕空间，无透视，alpha 混合
};

// =============================================================================
// 混合模式：决定 OM 阶段如何把 PS 输出与渲染目标混合
// =============================================================================
enum class EBlendMode : uint8_t {
    Opaque,             // 不透明（默认）—— PS 输出直接覆盖渲染目标，不走混合
    Masked,             // 遮罩 —— 二值透明（要么完全画要么完全不画），用 clip() 在 PS 里剔除
    Translucent,        // 半透明 —— SrcAlpha/InvSrcAlpha 混合（标准玻璃/水/烟）
    Additive,           // 加色 —— One/One 混合（火焰、光效、激光）
    Modulate,           // 调制 —— DestColor/Zero 混合（玻璃染色、彩色阴影）
};

// =============================================================================
// 材质开关（独立的 bool 标志，影响 PSO 的其他字段）
// =============================================================================
struct MaterialFlags {
    bool twoSided            = false;  // 双面渲染（关闭背面剔除 → CullMode=None）
    bool castShadow          = true;   // 投射阴影（影响 shadow map pass，不是本课 PSO）
    bool useTessellation     = false;  // 开启曲面细分（HS/DS 阶段，本课程不实现）
    bool wireframe           = false;  // 线框模式（FillMode=WIREFRAME）
    bool separateAlphaBlend  = false;  // alpha 通道用独立混合公式（IndependentBlendEnable=TRUE）
};
```

**为什么 enum 放 Types.h 而不放在编译器层？**
- `EBlendMode` 本身只是**数据模型的描述**，没有 HLSL 字符串
- 真正的"HLSL 字符串生成"和"D3D12_BLEND_DESC 映射"才属于 L5 编译器/渲染层
- 这是课 5 期间确立的分层铁律：**HLSL 字符串和 D3D12 类型不能出现在 Types.h**

---

### 2. domain + blendMode 影响的三层（核心原理）

材质域和混合模式不只是一个"配置"，它会同时影响**三个独立层面**：

```
┌─────────────────────────────────────────────────────────────┐
│  影响 1：shader 结构（课 8 HLSL 生成的核心输入）              │
│  ─────────────────────────────────────────                  │
│  - Surface domain：PS 输出 FMaterialAttributes（BaseColor/  │
│    Metallic/Roughness/Normal/Emissive/...），走 PBR BRDF    │
│  - Unlit domain：PS 输出 float3 Emissive，直接 return       │
│  - PostProcess domain：PS 输入 SceneColorTexture，输出      │
│    float3 改写后颜色（全屏三角形，不需要模型几何）           │
│  - UserInterface domain：PS 输出 float4(RGBA)，alpha 用于混合│
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  影响 2：blend state（本课 PSO 的 D3D12_BLEND_DESC）          │
│  ─────────────────────────────────                          │
│  - Opaque   → BlendEnable=FALSE                            │
│  - Masked   → BlendEnable=FALSE，但 PS 里 clip(opacity-cut) │
│  - Translucent → SrcAlpha/InvSrcAlpha, BlendEnable=TRUE    │
│  - Additive → One/One, BlendEnable=TRUE                    │
│  - Modulate → DestColor/Zero, BlendEnable=TRUE             │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  影响 3：光照（编译器决定是否生成光照代码）                   │
│  ─────────────────────────────────                          │
│  - Surface    → 调用 CalcPBRLighting()，受多盏灯影响         │
│  - Unlit      → 直接 Emissive * 1，不调用光照                │
│  - PostProcess → 完全没有光照概念                            │
└─────────────────────────────────────────────────────────────┘
```

**关键认知**：domain 决定 **"shader 长什么样"**，blendMode 决定 **"PS 输出后 OM 阶段做什么"**。两者**正交**但都有约束（见第 6 节合法组合表）。

---

### 3. 对 shader 结构的影响（GenerateCode 分支）

课 8 的 `GenerateCode()` 是把材质节点图翻译成 HLSL。**第一步就要看 domain** 来决定生成什么模板的 PS。下面是概念性参考代码（实际实现见课 8）：

```cpp
// =============================================================================
// MaterialCodeGenerator::GeneratePixelShader
// =============================================================================
// 根据 domain 生成完全不同的 PS 模板
// 注意：本课程只完整实现 Surface 和 Unlit；PostProcess/Decal/UI 只讲结构
// =============================================================================
std::string MaterialCodeGenerator::GeneratePixelShader(const Material& mat) {
    std::string out;

    // 重要：blendMode 不是运行时 if，而是编译期宏
    // GenerateCode 必须把它"烤"进 shader 文本（#define），否则 PS 性能崩塌
    out += fmt::format("#define BLEND_MODE_MASKED {}\n",
                       mat.blendMode == EBlendMode::Masked ? 1 : 0);
    out += fmt::format("#define OPACITY_MASK_CLIP_VALUE {:.2f}\n",
                       mat.opacityMaskClipValue);

    switch (mat.domain) {
        case EMaterialDomain::Surface: {
            // Surface 域：PS 输出 FMaterialAttributes 结构体
            // 走完整 PBR 光照（见课 17）
            out += R"hlsl(
                struct FMaterialAttributes {
                    float3 BaseColor;
                    float  Metallic;
                    float  Roughness;
                    float3 Normal;
                    float3 Emissive;
                    float  Opacity;
                    float  AO;
                };

                float4 main(VSOutput In) : SV_TARGET {
                    FMaterialAttributes M = CalcMaterialAttributes(In);  // 采样节点图
                    ApplyWorldPositionOffset(M, In);

                    // Masked 模式：二值透明（要么画要么不画）
                    // 关键：必须在光照计算之前 clip，否则被剔除的像素白跑 PBR
                    #if BLEND_MODE_MASKED
                        clip(M.Opacity - OPACITY_MASK_CLIP_VALUE);  // 默认 0.5
                    #endif

                    // 走 PBR 光照（Opaque/Masked 走完整光照；
                    //               Translucent 也走光照但 alpha 通道输出）
                    float3 litColor = CalcPBRLighting(M, In);

                    // Opaque/Masked 输出 alpha=1.0；Translucent 输出材质 Opacity
                    #if BLEND_MODE_MASKED
                        float alpha = 1.0;  // Masked 已经 clip 了，剩下都是不透明的
                    #else
                        float alpha = M.Opacity;
                    #endif

                    return float4(litColor + M.Emissive, alpha);
                }
            )hlsl";
            break;
        }

        case EMaterialDomain::Unlit: {
            // Unlit 域：直接输出 Emissive，不走光照
            // 用于调试可视化、HUD 元素、纯色物体
            out += R"hlsl(
                float4 main(VSOutput In) : SV_TARGET {
                    float3 emissive = CalcEmissive(In);  // 用户连的 Emissive 输入
                    return float4(emissive, 1.0);
                }
            )hlsl";
            break;
        }

        case EMaterialDomain::PostProcess: {
            // PostProcess 域：输入 SceneColor，输出改写后的颜色
            // 关键：全屏三角形（VS 里硬编码 NDC 顶点），不需要模型几何
            out += R"hlsl(
                Texture2D    SceneColorTex;   // 由渲染器绑定（课 17 的场景色 RTV）
                SamplerState LinearSampler;

                float4 main(float2 uv : TEXCOORD0) : SV_TARGET {
                    float3 sceneColor = SceneColorTex.Sample(LinearSampler, uv).rgb;
                    float3 result = ApplyPostProcess(sceneColor, uv);  // bloom/tonemap/...
                    return float4(result, 1.0);
                }
            )hlsl";
            break;
        }

        case EMaterialDomain::UserInterface: {
            // UI 域：屏幕空间，alpha 用于 SrcAlpha 混合
            out += R"hlsl(
                float4 main(VSOutput In) : SV_TARGET {
                    float4 color = CalcUIColor(In);  // 通常采样 UI 纹理
                    return color;  // alpha 走 OM 阶段的 SrcAlpha 混合
                }
            )hlsl";
            break;
        }

        case EMaterialDomain::Decal: {
            // Decal 域：本课程不实现（需要第二个 GBuffer 子集 + 深度 bias）
            // UE5 用DeferredDecalDrawingPolicy + 特殊几何体投影
            ME_LOG_WARNING("Decal domain 暂未实现，回退到 Surface");
            return GeneratePixelShader(/* domain=Surface 同样的 mat */);
        }
    }

    return out;
}
```

**已踩坑**：
- **blendMode 必须烤成 #define 宏，不能在 HLSL 里运行时判断**：`if (blendMode == Masked)` 这种代码无效，因为 shader 里没有 blendMode 变量（blendMode 是 C++ enum）。所有"模式相关"的分支都用 `#if BLEND_MODE_MASKED` 编译期宏。
- **Masked 的 `clip()` 必须在光照之前**：被剔除的像素不应该再跑昂贵的 PBR，否则浪费 GPU。
- **PostProcess 域不依赖模型顶点**：通常画一个全屏三角形（VS 硬编码 NDC 坐标，覆盖整个屏幕），完全不走模型 vertex buffer。
- **Unlit 在 UE5 里不是 domain**：UE5 的 Unlit 是 ShadingModel（ shading model = Unlit），不是 MaterialDomain。我们简化为单独的 domain，教学上更清晰，但**实现上与 UE5 不对应**——这点要在文档里说清楚。

---

### 4. 对 DX12 BlendState 的影响（本课核心）

这是 BlendState 的进阶版。基础三角形的 BlendState 是 Opaque（关闭混合）。下面给出**每种 blendMode 的完整 D3D12 配置表**。

#### 4.1 D3D12_RENDER_TARGET_BLEND_DESC 配置表

`D3D12_BLEND_DESC.RenderTarget[0]` 是 `D3D12_RENDER_TARGET_BLEND_DESC`，11 个字段。下表给出每种 blendMode 的标准配置：

| 字段 | Opaque | Masked | Translucent | Additive | Modulate |
|------|--------|--------|-------------|----------|----------|
| `BlendEnable` | FALSE | FALSE | TRUE | TRUE | TRUE |
| `LogicOpEnable` | FALSE | FALSE | FALSE | FALSE | FALSE |
| `SrcBlend` | ONE | ONE | SRC_ALPHA | ONE | DEST_COLOR |
| `DestBlend` | ZERO | ZERO | INV_SRC_ALPHA | ONE | ZERO |
| `BlendOp` | ADD | ADD | ADD | ADD | ADD |
| `SrcBlendAlpha` | ONE | ONE | ONE | ONE | ONE |
| `DestBlendAlpha` | ZERO | ZERO | INV_SRC_ALPHA | ONE | ZERO |
| `BlendOpAlpha` | ADD | ADD | ADD | ADD | ADD |
| `RenderTargetWriteMask` | ALL | ALL | ALL | ALL | ALL |
| `LogicOp` | NOOP | NOOP | NOOP | NOOP | NOOP |

**说明（为什么这么配）**：

- **Opaque**：`BlendEnable=FALSE`，PS 输出直接覆盖渲染目标。`SrcBlend/DestBlend` 没用（因为 BlendEnable 关了），但 DX12 要求**仍然填合理值**，否则某些驱动会报警告。约定填 `ONE/ZERO`。
- **Masked**：本质上是不透明的（不混合），但 PS 里用 `clip(opacity - cutoff)` 二值剔除。`BlendEnable=FALSE`，blend 公式同 Opaque。**关键区别在 PS 不在 blend state**。
- **Translucent**：经典的 alpha 混合。`FinalColor = SrcColor * SrcAlpha + DestColor * (1 - SrcAlpha)`。alpha 通道用 `ONE/INV_SRC_ALPHA`（把源的 alpha 累加到目标的 alpha）。
- **Additive**：相加。`FinalColor = SrcColor + DestColor`。用于发光体（火焰、激光、光晕），永远不会变暗。
- **Modulate**：乘法。`FinalColor = SrcColor * DestColor`。用于彩色玻璃、太阳镜、染色阴影。

#### 4.2 参考实现：MakeBlendDesc(EBlendMode)

放在 `Renderer/Public/BlendStateHelpers.h`（L5 渲染层，因为依赖 D3D12 头文件）：

```cpp
#pragma once
#include <d3d12.h>
#include "MaterialGraph/Types.h"  // EBlendMode / EMaterialDomain / MaterialFlags 在这里

// =============================================================================
// MakeBlendDesc —— 根据材质的 EBlendMode 生成 D3D12_BLEND_DESC
// =============================================================================
// 调用时机：创建 PSO 时（PSO 不可变，blendMode 一旦确定就编译进 PSO）
// 注意：PSO 一旦创建就不能改 blendMode，要换就得新建 PSO
// =============================================================================
inline D3D12_BLEND_DESC MakeBlendDesc(EBlendMode mode) {
    D3D12_BLEND_DESC desc = {};
    desc.AlphaToCoverageEnable = FALSE;        // 不用 MSAA alpha-to-coverage
                                                // (Masked + MSAA 时可以开，本课程不用)
    desc.IndependentBlendEnable = FALSE;       // 所有 RTV 共用一套混合公式
                                                // (MRT 各自配 blend 时设 TRUE)

    D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];
    rt.LogicOpEnable      = FALSE;             // 不用逻辑运算（与 Blend 互斥）
    rt.BlendOp            = D3D12_BLEND_OP_ADD;
    rt.BlendOpAlpha       = D3D12_BLEND_OP_ADD;
    rt.LogicOp            = D3D12_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    switch (mode) {
        case EBlendMode::Opaque:
            // 不混合：PS 输出直接覆盖渲染目标
            rt.BlendEnable    = FALSE;
            rt.SrcBlend       = D3D12_BLEND_ONE;
            rt.DestBlend      = D3D12_BLEND_ZERO;
            rt.SrcBlendAlpha  = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            break;

        case EBlendMode::Masked:
            // 不混合（PS 内部 clip() 决定画不画），配置同 Opaque
            rt.BlendEnable    = FALSE;
            rt.SrcBlend       = D3D12_BLEND_ONE;
            rt.DestBlend      = D3D12_BLEND_ZERO;
            rt.SrcBlendAlpha  = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            break;

        case EBlendMode::Translucent:
            // 经典 alpha 混合：FinalColor = Src*SrcAlpha + Dst*(1-SrcAlpha)
            rt.BlendEnable    = TRUE;
            rt.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
            rt.SrcBlendAlpha  = D3D12_BLEND_ONE;          // alpha 通道：Src.a + Dst.a*(1-Src.a)
            rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            break;

        case EBlendMode::Additive:
            // 加色混合：FinalColor = Src + Dst（火焰、激光、光效）
            // 注意：SrcBlend=ONE 而非 SRC_ALPHA，这样不带 alpha 也加得很强
            rt.BlendEnable    = TRUE;
            rt.SrcBlend       = D3D12_BLEND_ONE;
            rt.DestBlend      = D3D12_BLEND_ONE;
            rt.SrcBlendAlpha  = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ONE;
            break;

        case EBlendMode::Modulate:
            // 乘法混合：FinalColor = Src * Dst（染色玻璃、彩色阴影）
            // DX12 公式：FinalColor = SrcBlend * Src + DestBlend * Dst
            //          = DEST_COLOR * Src + ZERO * Dst = Dst * Src ✓
            rt.BlendEnable    = TRUE;
            rt.SrcBlend       = D3D12_BLEND_DEST_COLOR;
            rt.DestBlend      = D3D12_BLEND_ZERO;
            rt.SrcBlendAlpha  = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            break;
    }

    return desc;
}

// =============================================================================
// 辅助：根据材质 flags 调整 D3D12_RASTERIZER_DESC
// =============================================================================
inline D3D12_CULL_MODE MakeCullMode(bool twoSided) {
    // twoSided=true → 两面都画（关闭背面剔除）
    // twoSided=false → 默认行为（剔除背面，DX 顺时针为正面）
    return twoSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
}

inline D3D12_FILL_MODE MakeFillMode(bool wireframe) {
    return wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
}

// =============================================================================
// 辅助：半透明模式必须改深度状态（不写深度，否则挡住后面的半透明物体）
// =============================================================================
inline D3D12_DEPTH_WRITE_MASK MakeDepthWriteMask(EBlendMode mode) {
    // Translucent/Additive/Modulate 都不写深度（需要排序）
    // Opaque/Masked 正常写深度
    if (mode == EBlendMode::Opaque || mode == EBlendMode::Masked) {
        return D3D12_DEPTH_WRITE_MASK_ALL;
    }
    return D3D12_DEPTH_WRITE_MASK_ZERO;
}
```

#### 4.3 在 PSO 创建时使用

把课 15 的 `Init()` 第 4 步替换为：

```cpp
// === 从材质资源读取元数据（材质编辑器的核心连接点）===
// 假设 DX12Pipeline 持有当前材质资源的指针（由 UI 层传入）
const Material* mat = graph_->GetMaterial();
if (!mat) {
    ME_LOG_ERROR("材质资源为空，无法创建 PSO");
    return;
}

// --- 混合状态（按 blendMode 配置）---
psoDesc.BlendState = MakeBlendDesc(mat->blendMode);

// --- 光栅化状态（按 flags 配置）---
D3D12_RASTERIZER_DESC rasterizerDesc    = {};
rasterizerDesc.FillMode                 = MakeFillMode(mat->flags.wireframe);
rasterizerDesc.CullMode                 = MakeCullMode(mat->flags.twoSided);
rasterizerDesc.FrontCounterClockwise    = FALSE;
rasterizerDesc.DepthBias                = 0;
rasterizerDesc.DepthBiasClamp           = 0.0f;
rasterizerDesc.SlopeScaledDepthBias     = 0.0f;
rasterizerDesc.DepthClipEnable          = TRUE;
rasterizerDesc.MultisampleEnable        = FALSE;
rasterizerDesc.AntialiasedLineEnable    = FALSE;
rasterizerDesc.ForcedSampleCount        = 0;
rasterizerDesc.ConservativeRaster       = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
psoDesc.RasterizerState = rasterizerDesc;

// --- 深度状态（半透明时不写深度）---
D3D12_DEPTH_STENCIL_DESC depthStencilDesc   = {};
depthStencilDesc.DepthEnable                = TRUE;   // 假设课 16 已开深度缓冲
depthStencilDesc.DepthWriteMask             = MakeDepthWriteMask(mat->blendMode);
depthStencilDesc.DepthFunc                  = D3D12_COMPARISON_FUNC_LESS;
depthStencilDesc.StencilEnable              = FALSE;
psoDesc.DepthStencilState = depthStencilDesc;
```

**重要**：PSO 是**不可变**的。每种 (domain, blendMode) 组合需要独立的 PSO。材质编辑器需要维护一个 PSO 缓存，按 `(shaderHash, blendMode, flags)` 三元组键值缓存。这是课 16+ PSO 缓存的工作，本课先打通"一个材质 → 一个 PSO"的基础流程。

---

### 5. 材质属性的存储与传递

domain/blendMode 是材质级别的元数据，不是节点。放在哪里？

#### 5.1 选择：Material 类（Graph 的元数据包装）

```cpp
// =============================================================================
// Material —— 材质资源（Graph 的元数据包装）
// =============================================================================
// 这个类是"材质资产"的概念，对应 UE5 的 UMaterial。
// 它持有节点图（Graph）和材质级元数据（domain/blendMode/flags）。
// 编译时（GenerateCode）和 PSO 创建时（MakeBlendDesc）都读这些字段。
// =============================================================================
class Material {
public:
    // === 节点图 ===
    Graph* graph = nullptr;     // 课 4 的节点图数据模型

    // === 材质级元数据（编译 + PSO 都读）===
    EMaterialDomain  domain    = EMaterialDomain::Surface;
    EBlendMode       blendMode = EBlendMode::Opaque;
    MaterialFlags    flags;

    // === Masked 模式的 alpha cutoff 阈值 ===
    float opacityMaskClipValue = 0.5f;   // UE5 默认也是 0.5

    // === 序列化（JSON）===
    // 注意：domain/blendMode 是 enum class，JSON 要先转 int
    nlohmann::json toJson() const {
        return {
            {"domain",               static_cast<int>(domain)},
            {"blendMode",            static_cast<int>(blendMode)},
            {"twoSided",             flags.twoSided},
            {"castShadow",           flags.castShadow},
            {"useTessellation",      flags.useTessellation},
            {"wireframe",            flags.wireframe},
            {"separateAlphaBlend",   flags.separateAlphaBlend},
            {"opacityMaskClipValue", opacityMaskClipValue},
        };
    }

    void fromJson(const nlohmann::json& j) {
        // 范围检查！防止用户手改 JSON 写了个 99 进来
        int d = j.value("domain", 0);
        int b = j.value("blendMode", 0);
        if (d < 0 || d > 4) {
            ME_LOG_WARNING("domain 值 %d 越界，回退到 Surface", d);
            d = 0;
        }
        if (b < 0 || b > 4) {
            ME_LOG_WARNING("blendMode 值 %d 越界，回退到 Opaque", b);
            b = 0;
        }
        domain    = static_cast<EMaterialDomain>(d);
        blendMode = static_cast<EBlendMode>(b);
        // ... 其他字段
    }
};
```

#### 5.2 编译时的调用链

```
用户在 UI 里设置 domain=Surface, blendMode=Translucent
        ↓
存进 Material 资源（toJson 持久化到 .mat 文件）
        ↓
点击"编译"按钮
        ↓
GraphCompiler::Compile(Material&) 同时把 domain/blendMode 喂给两条管线：
        ├── → MaterialCodeGenerator::GeneratePixelShader(material)
        │       根据 domain 选 PS 模板，根据 blendMode 决定 clip() 分支
        │       产出 HLSL 字符串
        │
        └── （等运行时）PSO 创建调用 MakeBlendDesc(material.blendMode)
                根据 blendMode 选 D3D12_BLEND_DESC 配置
                产出 D3D12 PSO
```

**关键点**：HLSL 生成时和 PSO 创建时**都要**读 domain/blendMode，但读不同的字段：
- **HLSL 生成**：关心 **domain**（决定 PS 结构）+ **blendMode**（决定是否生成 `clip()` 代码）
- **PSO 创建**：关心 **blendMode**（决定 D3D12_BLEND_DESC）+ **flags.twoSided**（决定 cull mode）+ **flags.wireframe**（决定 fill mode）

---

### 6. domain × blendMode 的合法组合

**不是所有组合都合法**。下表对标 UE5：

| domain ＼ blendMode | Opaque | Masked | Translucent | Additive | Modulate |
|---------------------|--------|--------|-------------|----------|----------|
| **Surface**         | OK     | OK     | OK          | OK       | OK       |
| **Unlit**           | OK     | -      | OK          | OK       | OK       |
| **PostProcess**     | -      | -      | OK          | OK       | OK       |
| **Decal**           | -      | OK     | OK          | -        | OK       |
| **UserInterface**   | -      | -      | OK          | OK       | -        |

**典型用例**：
- `Surface + Opaque` —— 默认组合（墙壁、地板）
- `Surface + Masked` —— 树叶、铁丝网（透明部分完全不画）
- `Surface + Translucent` —— 玻璃、水（标准 alpha 混合）
- `Unlit + Translucent` —— 火焰的烟（颜色直接来自纹理，但需要 alpha 混合）
- `Unlit + Additive` —— 激光束、能量场（颜色叠加，无光照）
- `PostProcess + Translucent` —— bloom 的最终合成
- `Decal + Masked` —— 不透明的贴花（弹孔）
- `UserInterface + Translucent` —— UI 标准组合

**编译期校验**：`GraphCompiler::Validate()` 应在编译前检查这些组合，遇到非法组合报错（"PostProcess 域不支持 Opaque blend mode"）。

```cpp
bool ValidateDomainBlendCombo(EMaterialDomain d, EBlendMode b) {
    switch (d) {
        case EMaterialDomain::Surface:
            return true;  // Surface 支持所有 blendMode
        case EMaterialDomain::Unlit:
            return b != EBlendMode::Masked;  // Unlit 没有 opacity 概念
        case EMaterialDomain::PostProcess:
            return b == EBlendMode::Translucent ||
                   b == EBlendMode::Additive  ||
                   b == EBlendMode::Modulate;
        case EMaterialDomain::Decal:
            return b == EBlendMode::Masked    ||
                   b == EBlendMode::Translucent ||
                   b == EBlendMode::Modulate;
        case EMaterialDomain::UserInterface:
            return b == EBlendMode::Translucent ||
                   b == EBlendMode::Additive;
    }
    return false;
}
```

---

### 7. UE5 对照

UE5 的材质系统就是这套设计（我们的目标就是理解它）。

#### 7.1 枚举定义位置

- **EMaterialDomain**：`Engine/Source/Runtime/Engine/Public/Engine/EngineTypes.h`
  - 搜索 `enum class EMaterialDomain` —— UE5 有 9 个值（比我们多：`DeferredDecal`、`Volume`、`LightFunction`、`VolumetricHeightFog` 等）
  - 我们的 `Decal` ≈ UE5 的 `DeferredDecal`
  - 我们的 `Unlit` 在 UE5 **不是 domain**，而是 ShadingModel（这点要注意，前面已说明）
- **EBlendMode**：同文件，搜索 `enum class EBlendMode`
  - UE5 也是 5 个值，与我们的完全对齐：`Opaque`、`Masked`、`Translucent`、`Additive`、`Modulate`
  - 新版 UE5 还加了 `AlphaComposite`（用于 D3D11-only 的特殊混合），可忽略

#### 7.2 关键 API（FMaterial）

UE5 的材质 C++ 类 `FMaterial`（编辑器侧）和 `FMaterialResource`（编译后）提供查询接口，对应 `Engine/Source/Runtime/Engine/Public/MaterialShared.h`：

- `FMaterial::GetBlendMode()` —— 返回 EBlendMode
- `FMaterial::GetMaterialDomain()` —— 返回 EMaterialDomain
- `FMaterial::IsTranslucentBlendMode()` —— 便利查询（决定是否走透明 pass、是否需要排序）
- `FMaterial::IsMasked()` —— 便利查询（决定是否生成 clip 代码）
- `FMaterial::NeedsGBuffer()` —— 决定是否写入 GBuffer（延迟渲染）
- `FMaterial::NeedsUnlitViewMode()` —— 调试用（unlit 着色器替换）
- `FMaterial::WritesEveryPixel()` —— 是否覆盖每个像素（Opaque=true，Masked/Translucent=false）

**重点搜索**：`MaterialShared.cpp` 搜索 `IsTranslucentBlendMode` 实现，能看到 UE5 怎么判断（mask Out Opaque 和 Masked）。

#### 7.3 shader 模板中的分支

- **MaterialTemplate.ush**：`Engine/Shaders/Private/MaterialTemplate.ush`
  - 顶部 `#define MATERIAL_DOMAIN_SURFACE` 等宏（编译时由材质资源决定值）
  - 搜索 `MATERIAL_DOMAIN_POSTPROCESS` —— 大量 `#if` 分支
  - 关键结构 `FMaterialPixelShaderParams`、`GetMaterialEmissive`、`GetMaterialBaseColor` 等
  - **Masked 的 clip 代码**：搜索 `GetOpacityMask` + `clip`，UE5 在 `MaterialTemplate.ush` 里硬编码 `clip(MaterialExpressionOpacityMask - OpacityMaskClipValue)`

- **HLSLMaterialTranslator.cpp**：`Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp`
  - `FHLSLMaterialTranslator::GetMaterialDomain()` —— 决定生成哪个 domain 的代码
  - 搜索 `MaterialDomain == MD_PostProcess` 看分支
  - 这是课 6-8 编译器的核心参考

#### 7.4 D3D12 BlendState 创建

- **D3D12PipelineState.cpp**：`Engine/Source/Runtime/D3D12RHI/Private/D3D12PipelineState.cpp`
  - 搜索 `FD3D12PipelineState::Init`、`BlendDesc`、`RenderTargetsBlendState`
  - UE5 用 `FD3D12BlendState` RHI 层封装，由 RHI 转换 EBlendMode 到 D3D12_BLEND_DESC
- 实际转换函数：`FD3D12DynamicRHI::TranslateBlendState` 或类似（不同 UE 版本位置略异）
- **关键观察**：UE5 把 BlendState 也做成 RHI 资源（`FRHIBlendState`），缓存复用，避免每次创建 PSO 都重新填一遍——这就是我们 MakeBlendDesc 函数的"工程化升级方向"

#### 7.5 对照表

| 概念 | UE5 | 我们 |
|------|-----|------|
| 材质域枚举 | `EMaterialDomain`（9 值） | `EMaterialDomain`（5 值，简化） |
| 混合模式枚举 | `EBlendMode`（5 值） | `EBlendMode`（5 值，完全对齐） |
| Unlit 表达 | ShadingModel=Unlit（不是 domain） | 单独的 domain（教学简化） |
| 材质元数据存储 | `UMaterial::BlendMode` / `MaterialDomain` | `Material::blendMode` / `domain` |
| 编译期查询 | `FMaterial::IsMasked()` 等 | 直接读 `mat.blendMode` |
| shader 模板 | `MaterialTemplate.ush` 用 `#if MATERIAL_DOMAIN_*` | `GeneratePixelShader` 用 switch |
| D3D12 blend 转换 | `FD3D12BlendState` RHI 资源 | `MakeBlendDesc` inline 函数 |
| PSO 缓存 | `FD3D12PipelineStateCache` | （课 16+ 实现） |

---

### 8. 集成步骤（落地清单）

按这个顺序加进项目：

1. **L2 数据模型**（`Types.h`）：加 `EMaterialDomain`、`EBlendMode`、`MaterialFlags` 枚举/结构。**不引入任何 HLSL/D3D12 类型**。
2. **L2 数据模型**（新 `Material.h` 或 `Graph.h`）：加 `Material` 类，持有 `Graph*` 和材质元数据。实现 `toJson/fromJson` 序列化（注意 enum class 范围检查）。
3. **L5 编译器**（`GraphCompiler`）：在 `Compile(Material&)` 入口处读取 domain/blendMode，传给 code generator。加 `ValidateDomainBlendCombo()` 检查合法组合（见第 6 节）。
4. **L5 编译器**（`MaterialCodeGenerator`，课 8）：`GeneratePixelShader` 按 domain switch，按 blendMode 决定是否生成 `clip()` 代码。把 blendMode 烤成 `#define` 宏。
5. **L5 渲染**（新 `BlendStateHelpers.h`）：加 `MakeBlendDesc(EBlendMode)`、`MakeCullMode(bool)`、`MakeFillMode(bool)`、`MakeDepthWriteMask(EBlendMode)` 内联函数。
6. **L5 渲染**（`DX12Pipeline::Init`）：把硬编码的 `blendDesc` 替换为 `MakeBlendDesc(mat->blendMode)`，cull mode 用 `MakeCullMode(mat->flags.twoSided)`，深度写掩码用 `MakeDepthWriteMask(mat->blendMode)`。
7. **UI**（课 10-13）：材质编辑器属性面板加 Domain 下拉框、Blend Mode 下拉框、Two Sided 复选框、Opacity Mask Clip Value 滑块。改值后 dirty 标记，触发重编译。
8. **PSO 缓存**（课 16+）：按 `(shaderHash, blendMode, flags)` 三元组缓存 PSO，避免重复创建。

---

### 9. 已踩坑与注意事项

#### 9.1 Masked 的 opacity clip 阈值

- **默认 0.5**：UE5 也是 0.5。材质编辑器允许用户调整（0~1 之间）。
- **必须在光照前 clip**：否则被剔除的像素仍然跑完了昂贵的 PBR，浪费 GPU。
- **不能在 PS 末尾才 clip**：要在采样材质属性之后立即 clip（在调用 `CalcPBRLighting` 之前）。
- **clip 不是 discard**：DX12 HLSL 的 `clip(x)` 等价于 `if (x < 0) discard;`，是硬件指令，不会有性能损失。注意 `discard` 在 DX12 里其实叫 `clip`（GLSL 才叫 `discard`）。
- **depth buffer 写入**：Masked 模式仍然写入深度（与 Opaque 一样），所以深度测试正常工作。**Translucent 不写深度**，需要排序。

#### 9.2 Translucent 的排序问题

- **半透明物体必须从后向前画**，否则深度测试会拒绝被前面物体挡住的像素。
- UE5 的 `TranslucencyDrawingPolicy` 按（距相机距离）排序。
- **不要在半透明物体上写深度**（`DepthWriteMask = ZERO`），否则它会挡住后面的半透明物体。
- DX12 配置：`DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO`（半透明时）。这就是 `MakeDepthWriteMask()` 函数存在的理由。
- **不透明物体先画**，画完关深度写，再画半透明（这是渲染管线标准顺序）。

#### 9.3 Decal 需要额外的 RTV 和深度 bias

- 我们的课程**不实现 Decal**，因为它需要：
  - 第二个 RTV（GBuffer 的子集）
  - 特殊的几何体（贴花的 AABB 盒子）
  - 深度 bias（避免 z-fighting）
- UE5 的 `DeferredDecal` 是个内部 hack，超出我们课程范围。如果你只是想"看上去像贴花"，可以用 `Surface + Masked` + 特殊纹理代替。

#### 9.4 PSO 创建失败的 blend 相关原因

- **LogicOpEnable 和 BlendEnable 互斥**：不能同时为 TRUE。
- **IndependentBlendEnable=FALSE 时**：只读 `RenderTarget[0]`，其他 7 个被忽略。但如果你后续开 MRT，要全部正确填充。
- **BlendOp 和 SrcBlend/DestBlend 不匹配**：某些组合虽然合法但毫无意义（如 SrcBlend=INV_SRC_ALPHA 配 BlendOp=REV_SUBTRACT），驱动会接受但效果奇怪。
- **PostProcess 域 + MSAA**：通常关闭 MSAA（全屏三角形不需要），否则 DepthClipEnable 和 SampleDesc.Count 要对应。

#### 9.5 enum class 转 JSON 的坑

- `nlohmann::json` 不能直接序列化 `enum class`（不是 int 也不是 string）。
- 必须显式 `static_cast<int>(mode)` 或写 `NLOHMANN_JSON_SERIALIZE_ENUM` 宏。
- **反序列化时要做范围检查**（防止用户手改 JSON 写了个 99 进去）—— 见第 5.1 节 `fromJson` 的实现。

#### 9.6 domain 改变 = 完全重编译

- domain 是 shader 结构的根本决定因素，不能像 blendMode 那样只改 PSO。
- 用户改 domain 时，**必须**触发完全重编译（重新跑 GraphCompiler）。
- UI 应在改 domain 时弹"这将清空所有节点连接"的确认框（因为不同 domain 需要的输出节点不同：Surface 需要 BaseColor/Metallic 等，Unlit 只需要 Emissive，PostProcess 只需要 PostProcessInput）。

#### 9.7 Additive 模式的"颜色越叠越白"

- `FinalColor = Src + Dst`，多次叠加会很快饱和到 (1,1,1) 白色。
- 这是物理正确的（光叠加就是能量叠加），但美术常抱怨"调不出柔和的发光"。
- 解决：让 PS 输出的颜色乘以一个小系数（如 0.3），或用 HDR 渲染目标（R16G16B16A16_FLOAT）+ 后处理 tonemap。

#### 9.8 blendMode 的宏定义命名冲突

- 不要在 HLSL 里直接用 `MASKED`、`OPAQUE` 这种短名字做宏，会和引擎其它定义冲突。
- 推荐 `BLEND_MODE_MASKED`、`BLEND_MODE_TRANSLUCENT` 这种带前缀的命名。

---

### 10. 自测问题

回答这几个问题来检查理解：

1. **为什么 Opaque 和 Masked 的 `D3D12_BLEND_DESC` 完全一样？** 它们的区别在哪？
   <details><summary>提示</summary>区别在 PS（Masked 在 PS 里 clip），不在 OM 阶段。两者都 BlendEnable=FALSE。</details>

2. **Additive 模式画一片黑色像素（PS 输出 (0,0,0)），渲染目标颜色会变吗？** 为什么？
   <details><summary>提示</summary>不会。FinalColor = Src + Dst = (0,0,0) + Dst = Dst。黑色加任何颜色都不变。</details>

3. **`Surface + Masked` 和 `Surface + Translucent` 都能让部分像素不显示，区别是什么？**
   <details><summary>提示</summary>Masked 是二值（全画/全不画），有硬边；Translucent 是连续 alpha（部分混合），有柔边。Masked 写深度，Translucent 不写。</details>

4. **如果一个材质的 `domain=PostProcess`，但用户连了 BaseColor 节点，应该怎么处理？**
   <details><summary>提示</summary>编译期警告或忽略。PostProcess 域只认 PostProcessInput（场景色），BaseColor/Metallic 等是无意义的。</details>

5. **UE5 的 `IsTranslucentBlendMode()` 函数会返回 true 的 blendMode 有哪些？** 为什么 Masked 不算 translucent？
   <details><summary>提示</summary>Translucent/Additive/Modulate。Masked 本质是不透明的（写深度、不混合），只是用 clip 剔除部分像素。</details>

6. **为什么 Translucent 模式必须 `DepthWriteMask=ZERO`，但 Masked 必须保持深度写入？**
   <details><summary>提示</summary>Translucent 写深度会挡住后面的半透明物体；Masked 像素要么画要么不画，画的部分应该正常参与深度测试。</details>

---

## UE5 参考

UE5 的 DX12 实现分散在 `D3D12RHI` 模块中。以下文件展示了 UE5 如何处理本课涉及的概念：

### 根签名
- `Engine/Source/Runtime/D3D12RHI/Private/D3D12RootSignature.cpp`
- UE5 根据着色器的资源绑定自动生成根签名，而不是手写空签名
- 搜索 `FD3D12RootSignature`、`D3D12SerializeRootSignature`

### PSO
- `Engine/Source/Runtime/D3D12RHI/Private/D3D12PipelineState.cpp`
- UE5 维护一个 PSO 缓存，避免重复创建相同的 PSO
- 搜索 `FD3D12PipelineState`、`CreateGraphicsPipelineState`

### 顶点缓冲
- `Engine/Source/Runtime/D3D12RHI/Private/D3D12VertexBuffer.cpp`
- UE5 封装了 `FRHIVertexBuffer`，内部管理上传堆和默认堆
- 搜索 `FD3D12VertexBuffer`、`CreateCommittedResource`

### 着色器编译
- `Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp`
- 这是 UE5 材质编译的核心：把材质节点图翻译成 HLSL 代码
- UE5 不会像我们这样调用 `D3DCompileFromFile`，而是用自研的着色器编译管线（Shader Compiler）
- 搜索 `FHLSLMaterialTranslator`、`Compile`、`GenerateCode`

### 多进程 shader 编译（见课16）

本课的 `CompileShader` 是**单进程运行时编译**（`D3DCompileFromFile`），适合学习调试。但一个材质项目有**上万 shader 变体**，单进程串行编译会很慢。

**多进程 shader 编译集成**（对标 UE 的 `ShaderCompileWorker`）已在**课16（渲染器封装）**深度展开——主进程线程池调度 + N 个 worker 进程 + IPC + shader 缓存。课15 先用单进程 `D3DCompileFromFile` 打通基础管线，多进程升级见课16。

> **UE 对照**：`Engine/Source/Runtime/RenderCore/Private/ShaderCompiler*` + `Engine/Source/Programs/ShaderCompileWorker`（worker 进程源码）。

### UE5 vs 我们的教学版本

| 概念 | UE5 的做法 | 我们的做法 |
|------|-----------|-----------|
| 根签名 | 自动从着色器反射生成 | 手动创建空签名 |
| PSO | 有缓存系统，延迟创建 | 直接创建 |
| 顶点缓冲 | 封装在 RHI 层，支持多种布局 | 简单的结构体 |
| 着色器编译 | 多线程离线编译，SM6.0+ | 运行时编译，SM5.0 |
| 资源上传 | 有专门的上传队列和分配器 | 临时命令列表 |

---

## 完成标志

- [ ] HLSL 顶点着色器编译成功（无错误日志）
- [ ] HLSL 像素着色器编译成功（无错误日志）
- [ ] 根签名创建成功
- [ ] PSO 创建成功
- [ ] 顶点缓冲创建成功，数据上传到 GPU
- [ ] 彩色三角形正确显示在窗口中
- [ ] 三角形三个顶点颜色不同：红（顶部）、绿（右下）、蓝（左下）
- [ ] 三角形内部颜色平滑渐变（光栅化插值正确）
- [ ] 窗口 resize 后三角形正常显示（如果实现了 resize 处理）
- [ ] 程序关闭时不崩溃，资源正确释放
- [ ] 理解 DX12 渲染管线的 5 个阶段（IA → VS → RS → PS → OM）
- [ ] 理解根签名的作用和为什么本课可以用空签名
- [ ] 理解 PSO 与 OpenGL 状态设置的区别
- [ ] 理解上传堆和默认堆的区别以及数据上传流程
- [ ] 理解资源屏障在顶点缓冲上传中的作用
