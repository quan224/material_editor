# 08 图像视图（Image views）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Image_views

要在渲染管线里使用任何 `VkImage`——包括交换链里的那些——我们必须先为它创建一个 `VkImageView` 对象。图像视图顾名思义，就是**"进入图像的一个视角"**：它描述如何访问这张图像、访问图像的哪一部分，比如"把它当成一张没有 mip 层级的 2D 纹理"还是"当成深度纹理"。

本章编写 `createImageViews` 函数，为交换链里的每张图像创建一个基础视图，之后就能把它们当颜色目标用。

先加一个类成员存这些视图：

```cpp
std::vector<VkImageView> swapChainImageViews;
```

创建 `createImageViews` 函数，在交换链创建之后立刻调用：

```cpp
void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
}

void createImageViews() {

}
```

第一件事是把列表尺寸调到能装下所有要建的视图：

```cpp
void createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());

}
```

接着搭一个遍历所有交换链图像的循环：

```cpp
for (size_t i = 0; i < swapChainImages.size(); i++) {

}
```

图像视图的创建参数装在 `VkImageViewCreateInfo` 结构里，前几个参数很直白：

```cpp
VkImageViewCreateInfo createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
createInfo.image = swapChainImages[i];
```

`viewType` 和 `format` 字段指定图像数据**该怎么解释**。`viewType` 允许把同一张图像当作 1D 纹理、2D 纹理、3D 纹理或立方体贴图来用：

```cpp
createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
createInfo.format = swapChainImageFormat;
```

`components` 字段允许你**重排（swizzle）颜色通道**。比如可以把所有通道都映射到红色通道，做单色纹理；也可以往某个通道里映射常量 `0` 或 `1`。我们这里用默认映射就好：

```cpp
createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
```

`subresourceRange` 字段描述图像的用途、以及访问图像的哪一部分。我们的图像将用作**颜色目标**，没有 mip 层级、没有多层：

```cpp
createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
createInfo.subresourceRange.baseMipLevel = 0;
createInfo.subresourceRange.levelCount = 1;
createInfo.subresourceRange.baseArrayLayer = 0;
createInfo.subresourceRange.layerCount = 1;
```

如果你在做立体 3D（左右眼）应用，就会创建带多层的交换链，然后为每张图像建多个视图，各访问不同的层，代表左右眼的视角。

创建图像视图只需调用 `vkCreateImageView`：

```cpp
if (vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
    throw std::runtime_error("failed to create image views!");
}
```

和图像不同——图像视图是**我们显式创建的**——所以程序结尾要加一个类似的循环把它们逐个销毁：

```cpp
void cleanup() {
    for (auto imageView : swapChainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }

    ...
}
```

有了图像视图，把图像**当纹理**用就够了；但要把它当**渲染目标**还差一步间接层，那叫**帧缓冲（framebuffer）**。不过在碰它之前，得先把图形管线搭起来。
