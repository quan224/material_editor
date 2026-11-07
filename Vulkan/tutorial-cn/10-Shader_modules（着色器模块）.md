# 10 着色器模块（Shader modules）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Graphics_pipeline_basics/Shader_modules

## 着色器与 SPIR-V

与早期 API 不同，Vulkan 里的着色器代码必须用**字节码格式**指定，而不是 GLSL、HLSL 这类人类可读的语法。这个字节码格式叫 **SPIR-V**，为 Vulkan 和 OpenCL（两个 Khronos API）设计，可用于编写图形和计算着色器，本教程聚焦 Vulkan 图形管线里的着色器。

用字节码的好处：GPU 厂商把着色器代码编译成本机代码的编译器**显著更简单**。历史证明，对 GLSL 这类人类可读语法，某些厂商对标准的解读相当随意——如果你在某个厂商的卡上写了不简单的着色器，很可能换个厂商的驱动就因语法错误拒收，更糟的是因为编译器 bug 而运行结果不同。用 SPIR-V 这种直白的字节码有望避免这些。

但这不意味着我们要手写字节码。微软的 **DXC**（DirectXShaderCompiler，为 SM6 重写、基于 LLVM 的开源编译器）能把 HLSL 编译成 SPIR-V——加 `-spirv` 标志即可。它会验证着色器代码、产出一个可以随程序分发的 SPIR-V 二进制。你也可以把它作为库（dxcompiler）嵌入、在运行时产出 SPIR-V，但本教程不这么做。`dxc.exe` 已在 Vulkan SDK 里，无需额外下载。

HLSL 是 C 风格语法的着色器语言。程序有一个入口函数（名字可自定义，编译时用 `-E` 指定，本教程用 `main`）。输入输出走**带语义标记的结构体字段和参数**（如 `SV_POSITION`、`COLOR0`）。语言内置许多图形编程辅助：向量和矩阵原语、叉积/矩阵-向量乘积/向量反射等函数。向量类型是 `float` 加元素数量——3D 位置存 `float3`。可以用 `.x` 这样的成员访问单个分量，也可以 swizzle 一次从多个分量构造新向量：`float3(1.0, 2.0, 3.0).xy` 得到 `float2`。构造器还可以混合向量和标量：`float3(float2(1.0, 2.0), 3.0)`。

如上一章所说，要把三角形放上屏幕需要顶点着色器和片元着色器各一个。接下来两节分别讲它们的 HLSL 代码，然后讲怎么产出两个 SPIR-V 二进制并加载进程序。

## 顶点着色器

顶点着色器处理每个进来的顶点。输入是它的属性（模型空间位置、颜色、法线、纹理坐标），输出是**裁剪坐标（clip coordinates）下的最终位置**，以及需要传给片元着色器的属性（颜色、纹理坐标等）。这些值随后会被光栅器在片元之间**插值**，产生平滑渐变。

裁剪坐标是从顶点着色器出来的四维向量，随后通过**整向量除以最后一个分量**变成**归一化设备坐标（NDC）**。NDC 是把帧缓冲映射到 [-1, 1] × [-1, 1] 的齐次坐标系【原文此处有一张 NDC 坐标系图（normalized_device_coordinates.svg）】。

学过图形学的应该熟悉。用过 OpenGL 的会注意到 **Y 坐标方向翻了**；Z 坐标的范围则和 Direct3D 一致——0 到 1。

第一个三角形我们不做任何变换，直接把三个顶点位置写成 NDC，拼出这样的形状【原文此处有三角形坐标图（triangle_coordinates.svg）】。

把 NDC 当作裁剪坐标输出（最后一个分量设 `1`）就可以直接输出 NDC——除法不会改变任何东西。

这些坐标通常存在顶点缓冲里，但在 Vulkan 里创建顶点缓冲并填充数据并不简单，所以我决定推迟到看到三角形弹出来之后再做。先做点非常规的事：**把坐标直接写死在顶点着色器里**：

```hlsl
struct VSOut {
    float4 position : SV_POSITION;
};

static const float2 positions[3] = {
    float2(0.0, -0.5),
    float2(0.5, 0.5),
    float2(-0.5, 0.5)
};

VSOut main(uint vertexIndex : SV_VertexID) {
    VSOut o;
    o.position = float4(positions[vertexIndex], 0.0, 1.0);
    return o;
}
```

`main` 对每个顶点调用。系统值参数 `SV_VertexID` 含当前顶点的索引——通常是顶点缓冲里的索引，但在我们这里是对着色器内硬编码数组的索引。每个顶点的位置从常量数组取出，配上占位的 `z`、`w` 分量组成裁剪坐标，写进带 `SV_POSITION` 语义的输出字段。

---

## 片元着色器

顶点着色器给出的位置围成的三角形在屏幕上覆盖一片片元。片元着色器在这些片元上调用，为帧缓冲产出颜色和深度。整个三角形输出红色的简单片元着色器：

```hlsl
float4 main() : SV_Target {
    return float4(1.0, 0.0, 0.0, 1.0);
}
```

和顶点着色器的 `main` 对每个顶点调用一样，这里对每个片元调用。HLSL 里颜色是 R、G、B、alpha 四分量、各在 [0, 1] 范围的向量。片元输出通过**返回值携带 `SV_Target` 语义**给出——多个渲染目标时用 `SV_Target0`、`SV_Target1` 的索引区分。红色写进链接到第一个（也是唯一一个，索引 0）帧缓冲的目标。

## 逐顶点颜色

整个三角形都是红色没什么意思，下面这样的不是好看得多吗【原文此处有彩色三角形图（triangle_coordinates_colors.png）】？

为此要改两个着色器。首先给三个顶点各指定一种颜色。顶点着色器像位置一样加一个颜色数组：

```hlsl
static const float3 colors[3] = {
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0)
};
```

再把这些逐顶点颜色传给片元着色器，让它输出插值后的值。给顶点着色器的输出结构体加一个带语义的颜色字段，在 `main` 里写它：

```hlsl
struct VSOut {
    float4 position : SV_POSITION;
    float3 color    : COLOR0;
};

VSOut main(uint vertexIndex : SV_VertexID) {
    VSOut o;
    o.position = float4(positions[vertexIndex], 0.0, 1.0);
    o.color = colors[vertexIndex];
    return o;
}
```

接着在片元着色器里加对应的输入：

```hlsl
float4 main(float3 color : COLOR0) : SV_Target {
    return float4(color, 1.0);
}
```

输入变量的名字不必相同——靠**语义名**配对链接（顶点输出的 `COLOR0` 对上片元输入的 `COLOR0`）。`main` 改为输出颜色加 alpha 值。如上图所示，颜色的值会在三个顶点之间的片元上自动插值，产生平滑渐变。

## 编译着色器

在项目根目录建一个 `shaders` 目录，顶点着色器存 `shader.vert.hlsl`、片元着色器存 `shader.frag.hlsl`。

`shader.vert.hlsl` 的内容：

```hlsl
struct VSOut {
    float4 position : SV_POSITION;
    float3 color    : COLOR0;
};

static const float2 positions[3] = {
    float2(0.0, -0.5),
    float2(0.5, 0.5),
    float2(-0.5, 0.5)
};

static const float3 colors[3] = {
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0)
};

VSOut main(uint vertexIndex : SV_VertexID) {
    VSOut o;
    o.position = float4(positions[vertexIndex], 0.0, 1.0);
    o.color = colors[vertexIndex];
    return o;
}
```

`shader.frag.hlsl` 的内容：

```hlsl
float4 main(float3 color : COLOR0) : SV_Target {
    return float4(color, 1.0);
}
```

现在用 `dxc` 把它们编译成 SPIR-V 字节码（`-T` 指定目标阶段，`-E` 指定入口函数，`-spirv` 产出 SPIR-V，`-Fo` 指定输出文件）。

**Windows**

建一个 `compile.bat`：

```bat
C:/VulkanSDK/x.x.x.x/Bin/dxc.exe -T vs_6_0 -E main shader.vert.hlsl -spirv -Fo vert.spv
C:/VulkanSDK/x.x.x.x/Bin/dxc.exe -T ps_6_0 -E main shader.frag.hlsl -spirv -Fo frag.spv
pause
```

把 `dxc.exe` 的路径换成你安装 Vulkan SDK 的实际路径。双击运行。

**Linux**

建一个 `compile.sh`：

```sh
/home/user/VulkanSDK/x.x.x.x/x86_64/bin/dxc -T vs_6_0 -E main shader.vert.hlsl -spirv -Fo vert.spv
/home/user/VulkanSDK/x.x.x.x/x86_64/bin/dxc -T ps_6_0 -E main shader.frag.hlsl -spirv -Fo frag.spv
```

同样替换路径，`chmod +x compile.sh` 后运行。

**平台差异说明结束**

两条命令让编译器读 HLSL 源文件，产出 SPIR-V 字节码文件。

着色器有语法错误时编译器会告诉你行号和问题。试试删掉一个分号再跑。也可以不带参数运行编译器看它支持哪些标志——比如还能把字节码输出成人类可读格式，看清你的着色器到底在干什么、这一阶段做了哪些优化。

命令行编译着色器是最直接的选择，也是本教程用的方式；但也可以在你自己的代码里直接编译——DXC 提供库形式（dxcompiler.dll / IDxcCompiler），可以在程序内把 HLSL 编成 SPIR-V。

两个必知的点：

1. **坐标数值沿用 D3D 习惯即可**——Vulkan 和 D3D 的 NDC 本来就一致（+Y 朝下、z∈[0,1]），教程三角形的数字原样照抄，落点相同
2. **语义名配对代替 location 手写**——顶点输出 `COLOR0`、片元输入 `COLOR0`，DXC 自动分配 location；到有顶点缓冲的章节（顶点输入描述要 location 号）时留意 DXC 的分配规则

## 加载着色器

有了产出 SPIR-V 着色器的办法，接下来把它们加载进程序，好插进图形管线。先写一个简单的辅助函数从文件读二进制数据：

```cpp
#include <fstream>

...

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }
}
```

`readFile` 函数会读指定文件的全部字节，装进 `std::vector` 管理的字节数组返回。打开文件用两个标志：

- `ate`：从文件**末尾**开始读
- `binary`：按二进制读（避免文本转换）

从末尾开始的好处：可以用读位置确定文件大小、据此分配缓冲：

```cpp
size_t fileSize = (size_t) file.tellg();
std::vector<char> buffer(fileSize);
```

然后 seek 回文件开头，一次性读完所有字节：

```cpp
file.seekg(0);
file.read(buffer.data(), fileSize);
```

最后关闭文件并返回字节：

```cpp
file.close();

return buffer;
```

现在在 `createGraphicsPipeline` 里调用它加载两个着色器的字节码：

```cpp
void createGraphicsPipeline() {
    auto vertShaderCode = readFile("shaders/vert.spv");
    auto fragShaderCode = readFile("shaders/frag.spv");
}
```

打印缓冲大小、对照实际文件字节数来确认加载正确。注意代码不需要以空字符结尾——它是二进制代码，稍后我们会显式传它的大小。

## 创建着色器模块

把代码交给管线之前，得先把它包进一个 `VkShaderModule` 对象。建辅助函数 `createShaderModule`：

```cpp
VkShaderModule createShaderModule(const std::vector<char>& code) {

}
```

函数接收字节码缓冲，从中创建 `VkShaderModule`。

创建着色器模块很简单，只需指定字节码缓冲的指针和长度，装进 `VkShaderModuleCreateInfo` 结构。唯一的坑：大小以**字节**计，但字节码指针是 `uint32_t*` 而不是 `char*`，所以要用 `reinterpret_cast` 转换指针。做这种转换时还得确保数据满足 `uint32_t` 的**对齐**要求——幸好数据存在 `std::vector` 里，默认分配器已保证数据满足最坏情况的对齐要求。

```cpp
VkShaderModuleCreateInfo createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
createInfo.codeSize = code.size();
createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
```

然后调用 `vkCreateShaderModule` 创建：

```cpp
VkShaderModule shaderModule;
if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
}
```

参数和之前的对象创建函数一样：逻辑设备、create info 指针、可选的自定义分配器指针、句柄输出变量。创建模块后代码缓冲可以立即释放。别忘了返回创建的模块：

```cpp
return shaderModule;
```

着色器模块只是之前从文件加载的字节码及其函数定义的**薄包装**。SPIR-V 字节码到 GPU 机器码的编译和链接**直到图形管线创建时才发生**。这意味着管线创建完成后就可以销毁着色器模块——所以它们是 `createGraphicsPipeline` 里的**局部变量**而不是类成员：

```cpp
void createGraphicsPipeline() {
    auto vertShaderCode = readFile("shaders/vert.spv");
    auto fragShaderCode = readFile("shaders/frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
```

清理放在函数末尾，加两个 `vkDestroyShaderModule` 调用。本章余下的代码都插在这两行之前：

```cpp
    ...
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}
```

## 着色器阶段创建

要真正使用着色器，得通过 `VkPipelineShaderStageCreateInfo` 结构把它们分配给管线的特定阶段——这是实际创建管线过程的一部分。

先在 `createGraphicsPipeline` 里填顶点着色器的结构：

```cpp
VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
```

除了必填的 `sType`，第一步是告诉 Vulkan 着色器用在管线的**哪个阶段**——上一章描述的每个可编程阶段都有对应的枚举值。

```cpp
vertShaderStageInfo.module = vertShaderModule;
vertShaderStageInfo.pName = "main";
```

接着两个成员指定含代码的着色器模块，和要调用的函数——即**入口点（entrypoint）**。这意味着可以把多个片元着色器合进一个着色器模块，用不同入口点区分行为。不过我们用标准的 `main`。

还有一个（可选）成员 `pSpecializationInfo`，这里不用但值得一提。它允许你为**着色器常量**指定值——同一个着色器模块，在管线创建时通过指定不同的常量值来配置行为。这比运行时用变量配置更高效，因为编译器可以做诸如消除依赖这些值的 `if` 语句之类的优化。没有这类常量就设 `nullptr`，结构体初始化已自动处理。

改成片元着色器的版本很容易：

```cpp
VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
fragShaderStageInfo.module = fragShaderModule;
fragShaderStageInfo.pName = "main";
```

最后定义一个装这两个结构的数组，稍后在实际创建管线的步骤里引用它：

```cpp
VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
```

管线可编程阶段的描述就这么多。下一章看固定功能阶段。
