# 07 交换链（Swap chains）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Swap_chain

- 检查交换链支持
- 启用设备扩展
- 查询交换链支持详情
- 为交换链选择正确的设置（表面格式 / 呈现模式 / 交换范围）
- 创建交换链
- 取回交换链图像

Vulkan 没有"**默认帧缓冲**"的概念，因此它需要一套基础设施，在图像显示到屏幕之前持有我们要渲染的目标缓冲。这套基础设施就是**交换链（swap chain）**，在 Vulkan 里必须显式创建。交换链本质上是**一队排队等待上屏的图像**：应用程序取一张出来画，画完还回队列。这个队列具体怎么运作、什么条件下允许出队上屏，取决于交换链的配置方式——但它总的目标是**让图像的呈现与屏幕刷新率同步**。

## 检查交换链支持（Checking for swap chain support）

不是所有显卡都能直接把图像呈现到屏幕——比如服务器卡就没有显示输出。其次，图像呈现与窗口系统深度绑定，**并不属于 Vulkan 核心**，你必须在查询支持后启用 `VK_KHR_swapchain` **设备扩展**。

为此我们先扩展 `isDeviceSuitable` 检查这个扩展是否受支持。之前已经见过怎么列出 `VkPhysicalDevice` 支持的扩展，照做即可。头文件里提供了宏 `VK_KHR_SWAPCHAIN_EXTENSION_NAME`（值为 `"VK_KHR_swapchain"`），用它而不是手写字符串，拼错会直接编译报错。

先声明一份必需设备扩展的清单，与验证层清单类似：

```cpp
const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
```

接着新建 `checkDeviceExtensionSupport` 函数，作为 `isDeviceSuitable` 里的附加检查：

```cpp
bool isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    return indices.isComplete() && extensionsSupported;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
    return true;
}
```

填充函数体：枚举扩展，检查所有必需扩展是否都在其中。

```cpp
bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}
```

我这里用字符串 set 表示"尚未确认的必需扩展"——枚举可用扩展时逐个划掉，最后 set 空了就说明全找到了。当然你也可以像 `checkValidationLayerSupport` 那样写嵌套循环，性能差异无关紧要。运行代码，确认你的显卡确实能建交换链。要说的是：上一章查过的呈现队列的存在，其实已经暗示交换链扩展必然受支持——但把事情写明确总是好的，而且这个扩展**必须显式启用**。

## 启用设备扩展（Enabling device extensions）

用交换链前要先启用 `VK_KHR_swapchain` 扩展，只需在逻辑设备创建结构上小改两行：

```cpp
createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
createInfo.ppEnabledExtensionNames = deviceExtensions.data();
```

注意替换掉原来的 `createInfo.enabledExtensionCount = 0;` 那一行。

## 查询交换链支持详情（Querying details of swap chain support）

只检查"交换链可用"还不够——它未必与我们的窗口表面兼容。而且创建交换链的设置项远多于实例和设备的创建，动手前需要再查一些细节。基本上要查三类属性：

- **基本表面能力**（交换链图像数量的最小/最大值、图像宽高的最小/最大值）
- **表面格式**（像素格式、色彩空间）
- **可用的呈现模式**

类似 `findQueueFamilies`，我们用一个结构体把这些查询结果装着传递：

```cpp
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};
```

再新建 `querySwapChainSupport` 函数来填充它：

```cpp
SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    return details;
}
```

先查基本表面能力——单次调用、直接写进一个 `VkSurfaceCapabilitiesKHR` 结构：

```cpp
vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
```

这个函数在判定支持能力时会同时考虑指定的 `VkPhysicalDevice` 和 `VkSurfaceKHR`。**所有支持查询函数的前两个参数都是它俩**——因为它们正是交换链的两大核心组成。

接着查支持的表面格式。因为是一列结构，照例走两段式：

```cpp
uint32_t formatCount;
vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

if (formatCount != 0) {
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
}
```

记得把 vector 的尺寸调到能装下全部格式。最后，查呈现模式完全同理，用 `vkGetPhysicalDeviceSurfacePresentModesKHR`：

```cpp
uint32_t presentModeCount;
vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

if (presentModeCount != 0) {
    details.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
}
```

现在细节都进结构体了，再扩展一次 `isDeviceSuitable`，用它验证交换链支持是否够用。就本教程而言：**至少有一种支持的图像格式、一种支持的呈现模式**就算够。

```cpp
bool swapChainAdequate = false;
if (extensionsSupported) {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
    swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
}
```

注意：**只有确认扩展可用之后才能去查交换链支持**。函数最后一行变为：

```cpp
return indices.isComplete() && extensionsSupported && swapChainAdequate;
```

## 为交换链选择正确的设置（Choosing the right settings for the swap chain）

`swapChainAdequate` 条件满足就说明支持肯定够，但其中可能混杂着优劣不一的模式。现在写几个函数，为尽可能好的交换链挑出正确设置。要定三类：

- **表面格式**（色深）
- **呈现模式**（图像"换"上屏幕的条件）
- **交换范围**（交换链图像的分辨率）

每项都先想好理想值，可用就用它；不可用就写逻辑找次优。

### 表面格式（Surface format）

```cpp
VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {

}
```

每个 `VkSurfaceFormatKHR` 条目含 `format` 和 `colorSpace` 两个成员。`format` 指明颜色通道和类型，例如 `VK_FORMAT_B8G8R8A8_SRGB` 表示按 B、G、R、alpha 的顺序各存 8 位无符号整数，每像素共 32 位。`colorSpace` 用 `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR` 标志表示是否支持 SRGB 色彩空间（旧版规范里这个标志叫 `VK_COLORSPACE_SRGB_NONLINEAR_KHR`）。

色彩空间我们选 SRGB（如果可用）——它感知上更准确，也基本是图像（包括后面要用的纹理）的标准色彩空间。因此格式也应选 SRGB 的，最常见的就是 `VK_FORMAT_B8G8R8A8_SRGB`。遍历列表看理想组合在不在：

```cpp
for (const auto& availableFormat : availableFormats) {
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return availableFormat;
    }
}
```

如果没中，可以给可用格式按"好坏"排个名，但大多数情况下直接用第一个也行：

```cpp
VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}
```

### 呈现模式（Presentation mode）

呈现模式可以说是交换链**最重要的设置**，它直接决定图像上屏的条件。Vulkan 有四种：

- `VK_PRESENT_MODE_IMMEDIATE_KHR`：你提交的图像立刻传给屏幕，**可能产生撕裂（tearing）**。
- `VK_PRESENT_MODE_FIFO_KHR`：交换链是一个队列——显示器刷新时从队首取一张图像显示，程序把渲染好的图像插入队尾；队列满时程序等待。**最接近现代游戏里的垂直同步**。显示器刷新的时刻称为"垂直空白（vertical blank）"。
- `VK_PRESENT_MODE_FIFO_RELAXED_KHR`：与前者的差别只在程序迟到、上一个垂直空白时队列空着的情况——图像终于就绪后立即上屏，不再等下一个垂直空白，**可能出现可见撕裂**。
- `VK_PRESENT_MODE_MAILBOX_KHR`：第二种模式的又一变体——队列满时**不阻塞程序，而是用新图像替换已排队的图像**。可以在避免撕裂的同时尽快渲染帧，延迟比标准垂直同步更低。俗称"**三重缓冲**"，虽然有三个缓冲本身并不必然意味着帧率解锁。

只有 `VK_PRESENT_MODE_FIFO_KHR` **保证可用**，所以又得写一个函数在可用模式里找最好的：

```cpp
VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    return VK_PRESENT_MODE_FIFO_KHR;
}
```

我个人认为，在不计较能耗时 `VK_PRESENT_MODE_MAILBOX_KHR` 是非常好的折中：避免撕裂、又通过"直到垂直空白前一刻还在渲染最新图像"维持相当低的延迟。在更看重能耗的移动设备上，你会更想用 `VK_PRESENT_MODE_FIFO_KHR`。现在遍历列表看 `VK_PRESENT_MODE_MAILBOX_KHR` 是否可用：

```cpp
VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}
```

### 交换范围（Swap extent）

只剩最后一个大属性，为它加最后一个函数：

```cpp
VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {

}
```

交换范围就是交换链图像的分辨率，**几乎总是恰好等于目标窗口的分辨率，单位是像素**（稍后细说）。允许的分辨率范围定义在 `VkSurfaceCapabilitiesKHR` 里。Vulkan 让我们通过 `currentExtent` 成员去匹配窗口分辨率。但有些窗口系统允许我们偏离，做法是把 `currentExtent` 的宽高设成一个特殊值——`uint32_t` 的最大值。遇到这种情况，就在 `minImageExtent` 和 `maxImageExtent` 的边界内挑最贴近窗口的分辨率。但必须用**正确的单位**来指定。

GLFW 度量尺寸用两种单位：**像素（pixels）**和**屏幕坐标（screen coordinates）**。例如前面建窗口时的 `{WIDTH, HEIGHT}` 用的是屏幕坐标。但 Vulkan 用像素工作，交换范围必须以像素指定。麻烦在于高 DPI 显示器（如 Apple Retina）上**屏幕坐标不等于像素**——像素密度更高，窗口的像素分辨率会大于屏幕坐标分辨率。所以当 Vulkan 没有替我们定死交换范围时，不能直接用原来的 `{WIDTH, HEIGHT}`，而要先用 `glfwGetFramebufferSize` 查出窗口的**像素**分辨率，再去与最小/最大图像范围做匹配：

```cpp
#include <cstdint> // Necessary for uint32_t
#include <limits> // Necessary for std::numeric_limits
#include <algorithm> // Necessary for std::clamp

...

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}
```

`clamp` 在这里用于把宽高夹在实现支持的最小/最大范围内。

## 创建交换链（Creating the swap chain）

这些辅助函数备齐了运行时要做出的选择，我们终于凑齐了创建可用交换链所需的全部信息。创建 `createSwapChain` 函数，开头先取这三个选择的结果，并在 `initVulkan` 里逻辑设备创建之后调用：

```cpp
void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
}

void createSwapChain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
}
```

除这些属性外，还要决定交换链里放**多少张图像**。实现规定了它正常运转所需的最小数量：

```cpp
uint32_t imageCount = swapChainSupport.capabilities.minImageCount;
```

但只按最小值来，意味着有时得等驱动完成内部操作才能取到下一张可画的图像。因此建议**至少多要一张**：

```cpp
uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
```

同时别超过最大数量——`0` 是特殊值，表示没有上限：

```cpp
if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
    imageCount = swapChainSupport.capabilities.maxImageCount;
}
```

按 Vulkan 的传统，创建交换链对象又要填一个大结构。开头很眼熟：

```cpp
VkSwapchainCreateInfoKHR createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
createInfo.surface = surface;
```

指定交换链绑到哪个表面之后，是交换链图像的细节：

```cpp
createInfo.minImageCount = imageCount;
createInfo.imageFormat = surfaceFormat.format;
createInfo.imageColorSpace = surfaceFormat.colorSpace;
createInfo.imageExtent = extent;
createInfo.imageArrayLayers = 1;
createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
```

`imageArrayLayers` 指每张图像由几层组成，除立体 3D 应用外**恒为 1**。`imageUsage` 位域指定我们拿交换链图像做什么操作——本教程直接往上面渲染，即用作**颜色附件（color attachment）**。也可以先渲染到单独的图像再做后处理，那就用 `VK_IMAGE_USAGE_TRANSFER_DST_BIT` 之类的值，再用内存传输操作把渲染结果搬进交换链图像。

接下来要指定：**跨多个队列族使用的交换链图像怎么处理**。如果图形族与呈现族不同，我们的程序就会遇到这种情况——图像在图形队列上画，再提交给呈现队列。两种处理方式：

- `VK_SHARING_MODE_EXCLUSIVE`：一张图像同一时刻只归一个队列族所有，换族使用前必须**显式转移所有权**。性能最好。
- `VK_SHARING_MODE_CONCURRENT`：图像可在多个队列族间使用，无需显式所有权转移。

```cpp
QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

if (indices.graphicsFamily != indices.presentFamily) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
} else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0; // Optional
    createInfo.pQueueFamilyIndices = nullptr; // Optional
}
```

两个族不同时，本教程用 concurrent 模式——**避开所有权转移的章节**，那些概念放到以后讲更好。concurrent 要求用 `queueFamilyIndexCount` 和 `pQueueFamilyIndices` 预先声明在哪些族之间共享。两个族相同（多数硬件如此）时，则用 exclusive——因为 concurrent 要求至少两个不同的族。

```cpp
createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
```

可以指定对交换链图像施加某种变换（如果 `capabilities` 的 `supportedTransforms` 支持），比如顺时针转 90 度或水平翻转。**不想变换，就填当前变换**。

```cpp
createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
```

`compositeAlpha` 指定是否用 alpha 通道与窗口系统中的其他窗口混合。几乎总是想直接忽略 alpha 通道，即 `VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR`。

```cpp
createInfo.presentMode = presentMode;
createInfo.clipped = VK_TRUE;
```

`presentMode` 不用多说。`clipped` 设为 `VK_TRUE` 表示**不关心被遮挡像素的颜色**（比如被别的窗口挡住的地方）——除非你真需要把这些像素读回来并获得可预测的结果，开启裁剪能拿到最好的性能。

```cpp
createInfo.oldSwapchain = VK_NULL_HANDLE;
```

最后一个字段 `oldSwapchain`。程序运行期间交换链可能失效或变得不优（比如窗口大小变了），那时需要**从头重建**交换链，并在这个字段里引用旧的。这是个复杂话题，以后有专门章节。现在先假设只会创建一个交换链。

加一个类成员存 `VkSwapchainKHR` 对象：

```cpp
VkSwapchainKHR swapChain;
```

创建交换链只需调 `vkCreateSwapchainKHR`：

```cpp
if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
    throw std::runtime_error("failed to create swap chain!");
}
```

参数依次是逻辑设备、创建信息、可选的自定义分配器、存句柄的变量指针，毫无意外。它要用 `vkDestroySwapchainKHR` 在销毁**设备之前**清理：

```cpp
void cleanup() {
    vkDestroySwapchainKHR(device, swapChain, nullptr);
    ...
}
```

现在跑一下程序，确认交换链创建成功！如果此时 `vkCreateSwapchainKHR` 报访问违例，或看到 `Failed to find 'vkGetInstanceProcAddress' in layer SteamOverlayVulkanLayer.dll` 之类的消息，参见 FAQ 里关于 Steam 覆盖层的条目。

试试在开着验证层时删掉 `createInfo.imageExtent = extent;` 这一行——验证层立刻逮住错误并打印有用的消息。

## 取回交换链图像（Retrieving the swap chain images）

交换链已创建，剩下的就是把里面 `VkImage` 的句柄取出来——后续章节的渲染操作会引用它们。加一个类成员：

```cpp
std::vector<VkImage> swapChainImages;
```

这些图像是**实现为交换链创建的**，交换链销毁时它们会被自动清理，因此不需要写清理代码。

取句柄的代码加在 `createSwapChain` 函数末尾、`vkCreateSwapchainKHR` 调用之后。过程与之前从 Vulkan 取对象数组非常类似。记住：我们只指定了图像的**最小**数量，实现允许创建更多的图像——所以先用 `vkGetSwapchainImagesKHR` 查实际数量，调整容器大小，再调一次取回句柄：

```cpp
vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
swapChainImages.resize(imageCount);
vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());
```

最后一件事：把我们选定的格式和范围存进成员变量，后面章节会用到。

```cpp
VkSwapchainKHR swapChain;
std::vector<VkImage> swapChainImages;
VkFormat swapChainImageFormat;
VkExtent2D swapChainExtent;

...

swapChainImageFormat = surfaceFormat.format;
swapChainExtent = extent;
```

现在我们有一组**既能画上去、又能呈现到窗口**的图像了。下一章开始讲怎么把这些图像配置成渲染目标，然后就要接触真正的图形管线与绘制命令了！
