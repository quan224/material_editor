# 09 图形管线基础：引言（Graphics pipeline basics: Introduction）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Graphics_pipeline_basics/Introduction
> 【原文此处有一张管线总览图（vulkan_simplified_pipeline.svg），需要时到原网页查看】

接下来几章我们将设置一个图形管线（graphics pipeline），配置它来画出第一个三角形。**图形管线是把网格的顶点和纹理一路变成渲染目标上像素的操作序列**。简化总览如下（原文配图）：

**输入装配（input assembler）** 从你指定的缓冲里收集原始顶点数据，还可以用索引缓冲（index buffer）重复某些元素而不必复制顶点数据本身。

**顶点着色器（vertex shader）** 对每个顶点运行，通常做变换——把顶点位置从模型空间转到屏幕空间。它还把逐顶点数据往下传。

**曲面细分着色器（tessellation shaders）** 允许你按某些规则细分几何体来提升网格质量。常用于让砖墙、楼梯这类表面在近距离时显得不那么平。

**几何着色器（geometry shader）** 对每个图元（三角形/线/点）运行，可以丢弃它或输出比输入更多的图元。和曲面细分类似但灵活得多。不过现在应用里用得不多——大多数显卡上性能不佳（Intel 核显除外）。

**光栅化（rasterization）** 阶段把图元离散成**片元（fragments）**——就是它们在帧缓冲上填充的像素元素。落在屏幕外的片元被丢弃，顶点着色器输出的属性会在片元间**插值**（如图所示）。通常，被其他图元片元挡住的片元也因为深度测试在这里被丢弃。

**片元着色器（fragment shader）** 对每个幸存的片元调用，决定片元写进哪些帧缓冲、用什么颜色和深度值。它可以用顶点着色器传来的插值数据——比如纹理坐标和用于光照的法线。

**颜色混合（color blending）** 阶段对映射到帧缓冲同一像素的不同片元做混合操作。片元可以简单地相互覆盖、相加、或基于透明度混合。

图中**绿色的阶段是固定功能（fixed-function）阶段**——可以用参数调整它们的行为，但工作方式是预定义的。**橙色的阶段是可编程（programmable）的**——你可以把自己的代码上传到显卡，精确执行你想要的操作。比如片元着色器可以实现从纹理采样、光照到光线追踪的任何东西。这些程序在 GPU 的许多核心上同时运行，并行处理大量对象（顶点、片元）。

如果你用过 OpenGL、Direct3D 这些老 API，会习惯随时用 `glBlendFunc`、`OMSetBlendState` 之类的调用改任何管线设置。**Vulkan 的图形管线几乎完全不可变（immutable）**——想换着色器、绑不同的帧缓冲、改混合函数，都必须从头重建管线。缺点是你得为渲染中用到的每种状态组合各建一条管线；好处是管线里的全部操作**提前已知**，驱动可以据此做好得多的优化。

有些可编程阶段是可选的（按需启用）。比如只画简单几何时可以禁掉曲面细分和几何阶段；只关心深度值时可以禁掉片元着色器——阴影贴图生成就这么干。

下一章我们先创建把三角形放上屏幕所需的两个可编程阶段：**顶点着色器和片元着色器**。混合模式、视口、光栅化这些固定功能配置在再下一章设置。设置图形管线的最后一部分是指定输入/输出帧缓冲。

创建 `createGraphicsPipeline` 函数，在 `initVulkan` 里紧跟 `createImageViews` 之后调用。接下来几章都在这个函数上干活：

```cpp
void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createGraphicsPipeline();
}

...

void createGraphicsPipeline() {

}
```
