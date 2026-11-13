# 14 帧缓冲（Framebuffers）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Drawing/Framebuffers

前几章我们反复提到帧缓冲，渲染通道也已设置好预期接收一个与交换链图像同格式的帧缓冲，但还没真正创建过任何帧缓冲。

渲染通道创建时指定的那些**附件**，通过包进一个 `VkFramebuffer` 对象来**绑定**。帧缓冲对象引用代表各附件的全部 `VkImageView`。我们的例子只有一个：颜色附件。不过，附件该用哪张图像，取决于我们取图像来呈现时**交换链返回哪一张**——所以必须**为交换链里的每张图像各建一个帧缓冲**，绘制时用与取回图像对应的那个。

为此再加一个 `std::vector` 类成员存这些帧缓冲：

```cpp
std::vector<VkFramebuffer> swapChainFramebuffers;
```

在新函数 `createFramebuffers` 里创建它们，在 `initVulkan` 中紧跟图形管线创建之后调用：

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
    createFramebuffers();
}

...

void createFramebuffers() {

}
```

先把容器尺寸调到能装下所有帧缓冲：

```cpp
void createFramebuffers() {
    swapChainFramebuffers.resize(swapChainImageViews.size());
}
```

然后遍历图像视图，逐个创建帧缓冲：

```cpp
for (size_t i = 0; i < swapChainImageViews.size(); i++) {
    VkImageView attachments[] = {
        swapChainImageViews[i]
    };

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
        throw std::runtime_error("failed to create framebuffer!");
    }
}
```

如你所见，帧缓冲的创建相当直白。首先要指定帧缓冲需要与哪个 `renderPass` **兼容**——帧缓冲只能用于与之兼容的渲染通道，兼容大致意味着附件的数量和类型相同。

`attachmentCount` 和 `pAttachments` 指定要绑定到渲染通道 `pAttachment` 数组中各附件描述上的那些 `VkImageView`。

`width` 和 `height` 不言自明；`layers` 指图像数组的层数，交换链图像是单层图像，所以层数是 `1`。

帧缓冲的销毁要在它所依赖的图像视图和渲染通道**之前**、且在渲染全部结束之后：

```cpp
void cleanup() {
    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    ...
}
```

现在我们到达了一个里程碑：**渲染所需的全部对象都已就绪**。下一章开始写第一批真正的绘制命令。
