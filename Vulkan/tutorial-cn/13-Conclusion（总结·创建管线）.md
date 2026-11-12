# 13 总结（Conclusion）——创建图形管线

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Graphics_pipeline_basics/Conclusion

现在可以把前几章的所有结构和对象**组合起来创建图形管线**了！先快速盘点手上的东西：

- **着色器阶段（Shader stages）**：着色器模块，定义图形管线可编程阶段的功能
- **固定功能状态（Fixed-function state）**：定义管线固定功能阶段的全部结构——输入装配、光栅化器、视口、颜色混合等
- **管线布局（Pipeline layout）**：着色器引用的 uniform 和推送值，绘制时可更新
- **渲染通道（Render pass）**：管线各阶段引用的附件及其用法

这些合起来完整定义了图形管线的功能。现在可以在 `createGraphicsPipeline` 函数末尾填 `VkGraphicsPipelineCreateInfo` 了——位置在 `vkDestroyShaderModule` 调用**之前**，因为创建过程中着色器模块还要用。

```cpp
VkGraphicsPipelineCreateInfo pipelineInfo{};
pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
pipelineInfo.stageCount = 2;
pipelineInfo.pStages = shaderStages;
```

先引用 `VkPipelineShaderStageCreateInfo` 结构数组。

```cpp
pipelineInfo.pVertexInputState = &vertexInputInfo;
pipelineInfo.pInputAssemblyState = &inputAssembly;
pipelineInfo.pViewportState = &viewportState;
pipelineInfo.pRasterizationState = &rasterizer;
pipelineInfo.pMultisampleState = &multisampling;
pipelineInfo.pDepthStencilState = nullptr; // Optional
pipelineInfo.pColorBlendState = &colorBlending;
pipelineInfo.pDynamicState = &dynamicState;
```

然后引用所有描述固定功能阶段的结构。

```cpp
pipelineInfo.layout = pipelineLayout;
```

接下来是管线布局——注意它是 Vulkan 句柄而不是结构指针。

```cpp
pipelineInfo.renderPass = renderPass;
pipelineInfo.subpass = 0;
```

最后是渲染通道的引用，以及这条图形管线将在哪个**子通道（subpass）**里使用的索引。这条管线也可以配其他渲染通道用而不限于这一个实例，但它们必须与 `renderPass` **兼容（compatible）**——兼容的必要条件见规范原文。本教程不用这个特性。

```cpp
pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
pipelineInfo.basePipelineIndex = -1; // Optional
```

其实还有两个参数：`basePipelineHandle` 和 `basePipelineIndex`。Vulkan 允许**从现有管线派生**新管线。管线派生（pipeline derivatives）的思路：新管线与现有管线有大量功能共同时，创建开销更低；同父管线之间切换也更快。可以 用 `basePipelineHandle` 指定现有管线的句柄，或用 `basePipelineIndex` 按索引引用即将创建的另一条管线。现在只有一条管线，给空句柄和无效索引即可。这些值只有在 `VkGraphicsPipelineCreateInfo` 的 `flags` 字段同时指定了 `VK_PIPELINE_CREATE_DERIVATIVE_BIT` 标志时才会被使用。

最后一步：加一个类成员存 `VkPipeline` 对象：

```cpp
VkPipeline graphicsPipeline;
```

创建图形管线：

```cpp
if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline!");
}
```

`vkCreateGraphicsPipelines` 的参数比 Vulkan 常见的对象创建函数多。它设计为接收**多个** `VkGraphicsPipelineCreateInfo`、一次调用创建**多条** `VkPipeline`。

第二个参数我们传了 `VK_NULL_HANDLE`，它引用一个可选的 `VkPipelineCache`（管线缓存）对象。管线缓存可以存储并复用管线创建相关的数据——跨多次 `vkCreateGraphicsPipelines` 调用，甚至把缓存存成文件后**跨程序运行**复用——能显著加速之后的管线创建。管线缓存章节再讲。

图形管线是所有常见绘制操作都需要的，程序结束时才销毁：

```cpp
void cleanup() {
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    ...
}
```

运行程序，确认这一番辛苦换来管线的成功创建！离看到画面已经不远了。接下来两章：从交换链图像创建实际的帧缓冲、准备绘制命令。
