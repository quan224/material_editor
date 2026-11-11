# 12 渲染通道（Render passes）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Graphics_pipeline_basics/Render_passes
> DX12 对照：DX12 没有 render pass 概念——直接 `OMSetRenderTargets` 设目标开画。后来 DX12 加了可选的 RenderPass 扩展（极少用）；Vulkan 把它作为管线的必需前置

## 设置

创建管线之前，需要告诉 Vulkan 渲染期间会用到哪些**帧缓冲附件（framebuffer attachments）**。要指定有多少颜色缓冲和深度缓冲、各用多少采样、渲染过程中内容怎么处理。这些信息全部包在一个**渲染通道（render pass）**对象里——为它新建 `createRenderPass` 函数，在 `initVulkan` 里 `createGraphicsPipeline` **之前**调用：

```cpp
void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
}

...

void createRenderPass() {

}
```

## 附件描述（Attachment description）

我们只有一个颜色缓冲附件，由交换链的一张图像代表：

```cpp
void createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
}
```

颜色附件的 `format` 要和交换链图像格式一致；暂时不做多重采样所以 1 个采样。

```cpp
colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
```

`loadOp` 和 `storeOp` 决定渲染**前**和渲染**后**怎么处理附件里的数据。`loadOp` 可选：

- `VK_ATTACHMENT_LOAD_OP_LOAD`：保留附件的现有内容
- `VK_ATTACHMENT_LOAD_OP_CLEAR`：渲染开始时清为常量值
- `VK_ATTACHMENT_LOAD_OP_DONT_CARE`：现有内容未定义，我们不在乎

我们用 clear——画新帧前把帧缓冲清成黑色。`storeOp` 只有两个选项：

- `VK_ATTACHMENT_STORE_OP_STORE`：渲染结果存入内存，之后可读
- `VK_ATTACHMENT_STORE_OP_DONT_CARE`：渲染后帧缓冲内容未定义

我们想看到三角形出现在屏幕上，所以用 store。

```cpp
colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
```

`loadOp`/`storeOp` 作用于颜色和深度数据，`stencilLoadOp`/`stencilStoreOp` 作用于模板数据。我们不用模板缓冲，所以无所谓。

```cpp
colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
```

Vulkan 里的纹理和帧缓冲都用 `VkImage` 对象（特定像素格式）表示，但**像素在内存中的布局可以随用途改变**。常见布局：

- `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`：图像用作颜色附件
- `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`：图像用来在交换链中呈现
- `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`：图像用作拷贝目标

纹理那章会更深入，现在只需要知道：**图像需要转换到适合下一步操作的布局**。

`initialLayout` 指定渲染通道开始前图像处于什么布局；`finalLayout` 指定渲染通道结束后自动转换到什么布局。用 `VK_IMAGE_LAYOUT_UNDEFINED` 做 initialLayout 表示"不在乎之前是什么布局"——注意这个特殊值**不保证保留图像内容**，但我们反正要 clear，无所谓。我们希望渲染后图像准备好被交换链呈现，所以 finalLayout 用 `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`。

## 子通道与附件引用（Subpasses and attachment references）

一个渲染通道可以包含多个**子通道（subpass）**。子通道是后续的渲染操作，依赖之前通道的帧缓冲内容——比如一串依次应用的后处理效果。把这些操作组成一个渲染通道，Vulkan 能重排操作、节省显存带宽来获得更好的性能。不过我们的第一个三角形只用一个子通道。

每个子通道引用前面描述的一个或多个附件。引用本身是 `VkAttachmentReference` 结构：

```cpp
VkAttachmentReference colorAttachmentRef{};
colorAttachmentRef.attachment = 0;
colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
```

`attachment` 指定引用附件描述数组中第几个附件（索引）。我们只有一个 `VkAttachmentDescription`，所以索引是 0。`layout` 指定子通道使用此引用时我们希望附件处于什么布局——Vulkan 会在子通道开始时自动转换。我们把它当颜色缓冲用，`VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` 性能最好。

子通道用 `VkSubpassDescription` 描述：

```cpp
VkSubpassDescription subpass{};
subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
```

Vulkan 将来可能支持计算子通道，所以必须显式声明这是图形子通道。接着指定颜色附件的引用：

```cpp
subpass.colorAttachmentCount = 1;
subpass.pColorAttachments = &colorAttachmentRef;
```

**这个数组里的附件索引直接被片元着色器的输出引用**——HLSL 里的 `SV_Target0` 对应 `pColorAttachments[0]`、`SV_Target1` 对应 `[1]`……！

子通道还可以引用以下其他类型的附件：

- `pInputAttachments`：从 shader 里读的附件
- `pResolveAttachments`：多重采样颜色附件的 resolve 目标
- `pDepthStencilAttachment`：深度和模板数据附件
- `pPreserveAttachments`：本子通道不用但数据必须保留的附件

## 渲染通道（Render pass）

附件和引用它的子通道都描述好了，可以创建渲染通道本身。在 `pipelineLayout` 成员上面加一个类成员：

```cpp
VkRenderPass renderPass;
VkPipelineLayout pipelineLayout;
```

填 `VkRenderPassCreateInfo`（含附件数组和子通道数组，`VkAttachmentReference` 用索引引用附件）：

```cpp
VkRenderPassCreateInfo renderPassInfo{};
renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
renderPassInfo.attachmentCount = 1;
renderPassInfo.pAttachments = &colorAttachment;
renderPassInfo.subpassCount = 1;
renderPassInfo.pSubpasses = &subpass;

if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
    throw std::runtime_error("failed to create render pass!");
}
```

和管线布局一样，渲染通道在整个程序中被引用，只在最后清理：

```cpp
void cleanup() {
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    ...
}
```

工作量大，但下一章一切汇合——终于可以创建图形管线对象了！
