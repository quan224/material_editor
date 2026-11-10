# 11 固定功能（Fixed functions）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Graphics_pipeline_basics/Fixed_functions
> DX12 对照：全部这些结构对应 DX12 的 `D3D12_GRAPHICS_PIPELINE_STATE_DESC` 的各个子结构——Vulkan 拆成 8 个独立 CreateInfo，DX12 塞在一个巨型 desc 里

## 术语表（先认词，正文再认人）

**章级核心**

| 英文 | 中文 | 是什么 |
|---|---|---|
| fixed functions | 固定功能 | 管线里**不可编程、只能配置参数**的阶段（对比着色器这种可编程阶段） |
| pipeline state object | 管线状态对象（PSO） | 把所有配置"烘焙"成的不可变整体，改一个参数就得重建 |
| baked | 烘焙 | 配置在创建时**固化**进对象，之后不可改 |

**动态状态**

| 英文 | 中文 |
|---|---|
| dynamic state | 动态状态——可以绘制时再指定、不用烘焙进管线的少量状态 |
| viewport | 视口 |
| scissor (rect) | 裁剪矩形 |
| blend constant | 混合常量 |

**顶点输入**

| 英文 | 中文 |
|---|---|
| vertex input | 顶点输入 |
| binding | 绑定——一块顶点数据缓冲的"间距/步长"描述（英文原词 stride = 步长） |
| attribute description | 属性描述——每个属性（位置/颜色/UV）的类型、从哪块绑定加载、偏移多少 |
| instancing | 实例化——一次绘制画 N 份相同几何体 |
| vertex buffer | 顶点缓冲 |

**输入装配**

| 英文 | 中文 |
|---|---|
| input assembly | 输入装配——把离散顶点组装成几何图元 |
| topology | 拓扑——顶点怎么连成点/线/三角形 |
| point list | 点列表——每顶点一个点 |
| line list / line strip | 线列表 / 线带——strip = 首尾相连复用端点 |
| triangle list / triangle strip | 三角形列表 / 三角形带——strip = 相邻三角形共用两个顶点 |
| primitive restart | 图元重启——strip 模式里用特殊索引（0xFFFF…）断开重来 |
| element/index buffer | 元素缓冲 / 索引缓冲——自己指定顶点索引顺序，复用顶点 |

**视口与裁剪**

| 英文 | 中文 |
|---|---|
| viewport | 视口——3D 结果映射到帧缓冲的哪个区域（变换） |
| scissor | 裁剪矩形——框外的像素直接丢弃（过滤器，不是变换） |
| minDepth / maxDepth | 深度映射的上下端点（必须在 [0,1]，可反过来做**反向深度** reverse depth） |
| framebuffer | 帧缓冲 |

**光栅化器**

| 英文 | 中文 |
|---|---|
| rasterizer / rasterization | 光栅化器 / 光栅化——把三角形"铺"成像素片元 |
| fragment | 片元——三角形盖住的一个像素候选（还没成为最终像素） |
| depth test | 深度测试——按深度决定前后遮挡 |
| face culling / cull mode | 面剔除 / 剔除模式——背面朝相机的三角形直接不画 |
| front face (clockwise / counter-clockwise) | 正面（顺时针 / 逆时针）——什么绕向的三角形算正面 |
| wireframe | 线框模式 |
| polygon mode | 多边形模式（FILL 填充 / LINE 描边 / POINT 画点） |
| depth clamp | 深度钳位——超近/远的片元"夹"回平面而不是丢弃 |
| depth bias (constant / slope factor) | 深度偏移（常数 / 斜率因子）——给深度加偏移，阴影贴图用 |

**多重采样**

| 英文 | 中文 |
|---|---|
| multisampling (MSAA) | 多重采样——一种抗锯齿手段 |
| anti-aliasing | 抗锯齿——消掉斜边上的"楼梯" |
| sample shading / sample mask | 采样着色 / 采样掩码 |
| alpha to coverage | 用透明度控制采样覆盖（植被边缘常用） |

**深度与模板**

| 英文 | 中文 |
|---|---|
| stencil (test/buffer) | 模板（测试/缓冲）——按像素做"留/丢"标记的测试（做轮廓、镜面反射等） |

**颜色混合**

| 英文 | 中文 |
|---|---|
| color blending | 颜色混合——片元新颜色与帧缓冲旧颜色怎么合并 |
| blend factor | 混合因子——新旧颜色各乘个系数（src = 源/新的，dst = 目标/旧的） |
| blend op | 混合操作——乘完之后加减还是别的（ADD 最常用） |
| alpha | 透明度（颜色的第 4 分量） |
| alpha blending | 透明混合：`新α·新色 + (1−新α)·旧色` |
| logic op | 逻辑操作——按位运算合并（与/或/异或） |
| bitwise | 按位（的运算） |
| color write mask | 颜色写入掩码——允许 R/G/B/A 哪些通道写入 |
| channel | 通道（R/G/B/A 各一个） |

**管线布局**

| 英文 | 中文 |
|---|---|
| pipeline layout | 管线布局——声明这条管线用哪些 uniform 资源 |
| uniform | 统一值——着色器里的全局变量，绘制时可换值不重建（传矩阵/纹理） |
| descriptor set layout | 描述符集布局——uniform 的正式声明结构（后面章节主角） |
| push constant | 推送常量——传小量动态值给着色器的另一通道（不用建缓冲） |

**下一章预告**

| 英文 | 中文 |
|---|---|
| render pass | 渲染通道——声明这次渲染读写哪些附件、什么格式（创建管线前最后一个缺口） |


老 API 给管线的多数阶段提供了默认状态。**Vulkan 里你必须显式指定所有管线状态**——它们会被烘焙进一个不可变的管线状态对象。本章填完所有配置这些固定功能操作的结构。

## 动态状态（Dynamic state）

虽然多数管线状态需要烘焙进管线，**少量状态可以在绘制时改而不重建管线**。比如视口大小、线宽、混合常量。要用动态状态，填一个 `VkPipelineDynamicStateCreateInfo`：

```cpp
std::vector<VkDynamicState> dynamicStates = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR
};

VkPipelineDynamicStateCreateInfo dynamicState{};
dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
dynamicState.pDynamicStates = dynamicStates.data();
```

这会让管线**忽略这些值的配置**，你可以在（也必须在）**绘制时**再指定实际数据。更灵活，视口和裁剪走动态状态是常见做法——烘焙进管线反而在窗口缩放时要重建整条管线。

## 顶点输入（Vertex input）

`VkPipelineVertexInputStateCreateInfo` 描述传给顶点着色器的顶点数据格式。两种方式描述：

- **绑定（Bindings）**：数据间距，是逐顶点还是逐实例（见实例化）
- **属性描述（Attribute descriptions）**：属性的类型、从哪个绑定加载、偏移多少

我们的顶点数据直接写死在着色器里，所以暂时声明"没有顶点数据要加载"。顶点缓冲那章再回来。

```cpp
VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
vertexInputInfo.vertexBindingDescriptionCount = 0;
vertexInputInfo.pVertexBindingDescriptions = nullptr; // Optional
vertexInputInfo.vertexAttributeDescriptionCount = 0;
vertexInputInfo.pVertexAttributeDescriptions = nullptr; // Optional
```

把这个结构加到 `createGraphicsPipeline` 函数里 `shaderStages` 数组之后。

## 输入装配（Input assembly）

`VkPipelineInputAssemblyStateCreateInfo` 描述两件事：**画什么类型的几何体**（topology）和**是否启用图元重启**。topology 可选：

- `VK_PRIMITIVE_TOPOLOGY_POINT_LIST`：每个顶点画一个点
- `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`：每 2 个顶点画一条线，不复用
- `VK_PRIMITIVE_TOPOLOGY_LINE_STRIP`：上一条线的终点是下一条线的起点
- `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`：每 3 个顶点画一个三角形，不复用
- `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP`：上一个三角形的第 2、3 顶点是下一个三角形的第 1、2 顶点

通常顶点按索引顺序从顶点缓冲加载，但用**元素缓冲（element buffer）**可以自己指定索引——实现顶点复用等优化。`primitiveRestartEnable` 设 `VK_TRUE` 时，可以在 `_STRIP` 模式下用特殊索引 `0xFFFF` 或 `0xFFFFFFFF` 断开线条和三角形。

我们画三角形：

```cpp
VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
inputAssembly.primitiveRestartEnable = VK_FALSE;
```

## 视口和裁剪（Viewports and scissors）

视口描述**帧缓冲上渲染输出映射到哪个区域**。几乎总是 `(0, 0)` 到 `(width, height)`：

```cpp
VkViewport viewport{};
viewport.x = 0.0f;
viewport.y = 0.0f;
viewport.width = (float) swapChainExtent.width;
viewport.height = (float) swapChainExtent.height;
viewport.minDepth = 0.0f;
viewport.maxDepth = 1.0f;
```

注意交换链图像的尺寸可能和窗口的 `WIDTH`/`HEIGHT` 不同——交换链图像将来被用作帧缓冲，所以要用它的大小。

`minDepth` 和 `maxDepth` 指定帧缓冲的深度值范围。必须在 `[0.0f, 1.0f]` 内，但 minDepth 可以大于 maxDepth（反向深度）。没特殊需求就用标准值 0.0f 和 1.0f。

视口定义从**图像到帧缓冲的变换**，而**裁剪矩形**定义**哪些区域的像素实际会被存储**。裁剪矩形之外的像素被光栅化器丢弃。它是**过滤器**而不是变换【原文此处有一张对比图（viewports_scissors.png），需要时到原网页查看】。

画到整个帧缓冲的话，裁剪矩形覆盖全部：

```cpp
VkRect2D scissor{};
scissor.offset = {0, 0};
scissor.extent = swapChainExtent;
```

视口和裁剪矩形要么作为**管线静态部分**指定，要么作为**动态状态**在命令缓冲里设。前者跟其他状态一致，但动态方式更灵活（窗口缩放不用重建管线），且所有实现都能免性能代价地处理。

选动态方式时，先启用动态状态（本章开头那段代码），管线创建时只需要指定数量：

```cpp
VkPipelineViewportStateCreateInfo viewportState{};
viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
viewportState.viewportCount = 1;
viewportState.scissorCount = 1;
```

实际的视口和裁剪矩形在绘制时设置。动态状态甚至在单个命令缓冲里可以指定不同的视口和裁剪。

不用动态状态时，视口和裁剪要设进管线的 `VkPipelineViewportStateCreateInfo`——对这个管线不可变。改这些值需要建新管线：

```cpp
VkPipelineViewportStateCreateInfo viewportState{};
viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
viewportState.viewportCount = 1;
viewportState.pViewports = &viewport;
viewportState.scissorCount = 1;
viewportState.pScissors = &scissor;
```

无论怎么设，某些显卡支持多视口和多裁剪矩形（结构成员引用数组）。使用多个需要启用 GPU 特性（见逻辑设备创建）。

**教程的选择**：本教程使用**动态状态**方式（更常见更灵活）。

## 光栅化器（Rasterizer）

光栅化器把顶点着色器塑形的几何体变成片元交给片元着色器上色。它还做深度测试、面剔除、裁剪测试，可以配置为填充整个多边形或只画边（线框模式）。全用 `VkPipelineRasterizationStateCreateInfo` 配置：

```cpp
VkPipelineRasterizationStateCreateInfo rasterizer{};
rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
rasterizer.depthClampEnable = VK_FALSE;
```

`depthClampEnable` 设 `VK_TRUE` 时，超出近平面和远平面的片元**被钳位**到平面而不是丢弃。阴影贴图等特殊场景有用。需要启用 GPU 特能。

```cpp
rasterizer.rasterizerDiscardEnable = VK_FALSE;
```

设 `VK_TRUE` 时几何体永远不经过光栅化阶段——基本上禁用所有到帧缓冲的输出。

```cpp
rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
```

`polygonMode` 决定片元怎么从几何体生成：

- `VK_POLYGON_MODE_FILL`：填充多边形区域
- `VK_POLYGON_MODE_LINE`：多边形边画成线（线框）
- `VK_POLYGON_MODE_POINT`：多边形顶点画成点

非 FILL 模式需要启用 GPU 特性。

```cpp
rasterizer.lineWidth = 1.0f;
```

线宽（片元数）。最大线宽取决于硬件，超过 `1.0f` 需启用 `wideLines` 特性。

```cpp
rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
```

`cullMode` 决定面剔除类型：不剔除、剔正面、剔背面、全剔。`frontFace` 指定什么顶点顺序算正面——顺时针或逆时针。

```cpp
rasterizer.depthBiasEnable = VK_FALSE;
rasterizer.depthBiasConstantFactor = 0.0f; // Optional
rasterizer.depthBiasClamp = 0.0f; // Optional
rasterizer.depthBiasSlopeFactor = 0.0f; // Optional
```

光栅化器可以给深度值加常数或按片元斜率偏移——阴影贴图偶尔用。我们不用，`depthBiasEnable = VK_FALSE`。

## 多重采样（Multisampling）

`VkPipelineMultisampleStateCreateInfo` 配置多重采样——一种**抗锯齿**方式。它把光栅化到同一像素的多个多边形的片元着色器结果合并。主要发生在边缘——也是锯齿最明显的地方。**一个像素只被一个多边形覆盖时不需要多次跑片元着色器**，所以比渲染到高分辨率再缩小便宜得多。需要启用 GPU 特性。

```cpp
VkPipelineMultisampleStateCreateInfo multisampling{};
multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
multisampling.sampleShadingEnable = VK_FALSE;
multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
multisampling.minSampleShading = 1.0f; // Optional
multisampling.pSampleMask = nullptr; // Optional
multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
multisampling.alphaToOneEnable = VK_FALSE; // Optional
```

后面章节再回来，暂时禁用。

## 深度和模板测试（Depth and stencil testing）

如果用深度和/或模板缓冲，还需要用 `VkPipelineDepthStencilStateCreateInfo` 配置测试。现在没有，直接传 `nullptr`。深度缓冲那章再回来。

## 颜色混合（Color blending）

片元着色器返回颜色后，需要和帧缓冲里已有的颜色合并。这就是**颜色混合**，两种方式：

- 混合旧值和新值产生最终颜色
- 用位运算合并旧值和新值

两种结构配置颜色混合。第一个 `VkPipelineColorBlendAttachmentState` 是**逐帧缓冲**的配置，第二个 `VkPipelineColorBlendStateCreateInfo` 是**全局**设置。我们只有一个帧缓冲：

```cpp
VkPipelineColorBlendAttachmentState colorBlendAttachment{};
colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
colorBlendAttachment.blendEnable = VK_FALSE;
colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional
```

这个逐帧缓冲结构配置第一种混合方式。操作用伪代码演示最清楚：

```
if (blendEnable) {
    finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor * oldColor.rgb);
    finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor * oldColor.a);
} else {
    finalColor = newColor;
}

finalColor = finalColor & colorWriteMask;
```

`blendEnable` 为 `VK_FALSE` 时，片元着色器的新颜色直接通过。否则执行两个混合操作。结果和 `colorWriteMask` 按位与，决定哪些通道实际通过。

最常用的混合是** alpha 混合**——根据透明度混合新旧颜色：

```
finalColor.rgb = newAlpha * newColor + (1 - newAlpha) * oldColor;
finalColor.a = newAlpha.a;
```

参数这样配：

```cpp
colorBlendAttachment.blendEnable = VK_TRUE;
colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
```

所有可用的操作在规范的 `VkBlendFactor` 和 `VkBlendOp` 枚举里。

第二个结构引用所有帧缓冲的结构数组，还可以设混合常量：

```cpp
VkPipelineColorBlendStateCreateInfo colorBlending{};
colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
colorBlending.logicOpEnable = VK_FALSE;
colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
colorBlending.attachmentCount = 1;
colorBlending.pAttachments = &colorBlendAttachment;
colorBlending.blendConstants[0] = 0.0f; // Optional
colorBlending.blendConstants[1] = 0.0f; // Optional
colorBlending.blendConstants[2] = 0.0f; // Optional
colorBlending.blendConstants[3] = 0.0f; // Optional
```

要用第二种方式（位运算合并），`logicOpEnable` 设 `VK_TRUE`，位运算在 `logicOp` 里指定。**这会自动禁用第一种方式**（等同于每个帧缓冲的 `blendEnable` 都是 `VK_FALSE`）。`colorWriteMask` 在此模式下也生效。也可以两种都关——那片元颜色原样写入帧缓冲（我们现在就这么干）。

## 管线布局（Pipeline layout）

着色器里可以用 `uniform` 值——类似动态状态的全局变量，可以在绘制时改变着色器行为而不用重建。常用于传变换矩阵给顶点着色器，或在片元着色器里创建纹理采样器。

这些 uniform 需要在**管线创建时**用 `VkPipelineLayout` 对象指定。虽然我们到后面的章节才会用到，但现在就必须创建一个空的管线布局。

加一个类成员存这个对象（其他函数后面会用到它）：

```cpp
VkPipelineLayout pipelineLayout;
```

在 `createGraphicsPipeline` 里创建：

```cpp
VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
pipelineLayoutInfo.setLayoutCount = 0; // Optional
pipelineLayoutInfo.pSetLayouts = nullptr; // Optional
pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
}
```

结构还指定了**推送常量（push constants）**——另一种传动态值给着色器的方式，后面可能涉及。管线布局在程序整个生命周期被引用，所以要在 cleanup 里销毁：

```cpp
void cleanup() {
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    ...
}
```

## 结语

固定功能状态就这么多！从头设置这些很费劲，但好处是**我们现在几乎完全知道图形管线里在发生什么**——因为某些组件的默认状态不符合预期而碰到意外行为的可能性降低了。

不过还差一个对象才能创建图形管线——**渲染通道（render pass）**。
