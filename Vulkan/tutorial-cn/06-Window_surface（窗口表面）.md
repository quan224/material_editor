# 06 窗口表面（Window surface）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Window_surface

## 引言

- 窗口表面的创建
- 查询呈现支持
- 创建呈现队列

由于 Vulkan 是平台无关的 API，它自己无法直接与窗口系统打交道。要在 Vulkan 和窗口系统之间建立连接、把渲染结果呈现到屏幕上，我们需要使用 **WSI（Window System Integration，窗口系统集成）扩展**。本章讨论第一个：`VK_KHR_surface`。它暴露一个 `VkSurfaceKHR` 对象，表示"供呈现渲染图像"的抽象表面类型。我们程序里的表面将由已经用 GLFW 打开的那个窗口支撑。

`VK_KHR_surface` 扩展是**实例级**扩展，而且我们实际上已经启用了它——它包含在 `glfwGetRequiredInstanceExtensions` 返回的列表里。那个列表还包括另外几个 WSI 扩展，后面几章会用到。

窗口表面需要在实例创建之后立刻创建，因为它实际上会影响物理设备的选择。我们之前把它推迟到现在，是因为窗口表面属于"渲染目标与呈现"这个更大的话题，早早解释会让基础搭建过程变得杂乱。还要注意：窗口表面在 Vulkan 里是**完全可选**的组件——如果你只需要离屏渲染，Vulkan 允许你直接这么做，不需要 OpenGL 时代那种"开一个不可见窗口"的 hack。

## 窗口表面的创建（Window surface creation）

先在 debug 回调成员下面加一个 `surface` 类成员：

```cpp
VkSurfaceKHR surface;
```

尽管 `VkSurfaceKHR` 对象和它的用法是平台无关的，但它的**创建**过程不是——创建依赖窗口系统的细节。例如在 Windows 上需要 `HWND` 和 `HMODULE` 句柄。因此这个扩展有一个平台专属的补充扩展，在 Windows 上叫 `VK_KHR_win32_surface`，它同样已自动包含在 `glfwGetRequiredInstanceExtensions` 返回的列表里。

我会演示这个平台专属扩展如何创建 Windows 表面，但本教程并不会真的用它——既然用了 GLFW 这种库，转头又去写平台专属代码就说不过去了。GLFW 实际上提供了 `glfwCreateWindowSurface`，替我们处理了所有平台差异。不过在开始依赖它之前，看看它背后做了什么是有益的。

要访问原生平台函数，需要更新文件顶部的 include：

```cpp
#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
```

窗口表面是 Vulkan 对象，所以伴生一个要填的 `VkWin32SurfaceCreateInfoKHR` 结构体。它有两个重要参数：`hwnd` 和 `hinstance`——窗口和进程的句柄。

```cpp
VkWin32SurfaceCreateInfoKHR createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
createInfo.hwnd = glfwGetWin32Window(window);
createInfo.hinstance = GetModuleHandle(nullptr);
```

`glfwGetWin32Window` 函数用于从 GLFW 窗口对象拿到原生 `HWND`；`GetModuleHandle` 调用返回当前进程的 `HINSTANCE` 句柄。

之后就能用 `vkCreateWin32SurfaceKHR` 创建表面了，参数依次是：实例、表面创建详情、自定义分配器、存储表面句柄的变量。严格说这是一个 WSI 扩展函数，但它太常用了，标准 Vulkan loader 直接包含了它——不像其他扩展函数那样需要显式加载。

```cpp
if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
    throw std::runtime_error("failed to create window surface!");
}
```

其他平台（如 Linux）过程类似，X11 下 `vkCreateXcbSurfaceKHR` 接收一个 XCB 连接和窗口作为创建详情。

`glfwCreateWindowSurface` 函数干的就是这件事，每个平台一份实现。现在把它集成进程序：加一个 `createSurface` 函数，在 `initVulkan` 里紧跟实例创建和 `setupDebugMessenger` 之后调用：

```cpp
void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
}

void createSurface() {

}
```

GLFW 的这个调用用简单参数而不是结构体，所以函数实现非常直白：

```cpp
void createSurface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
}
```

参数依次是 `VkInstance`、GLFW 窗口指针、自定义分配器、指向 `VkSurfaceKHR` 变量的指针。它只是把对应平台调用的 `VkResult` 原样透传。GLFW 没有提供销毁表面的专用函数，直接用原生 API 即可：

```cpp
void cleanup() {
    ...
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    ...
}
```

务必确保表面在实例**之前**销毁。

## 查询呈现支持（Querying for presentation support）

Vulkan 实现可能支持窗口系统集成，但这不代表系统里**每台设备**都支持。因此我们需要扩展 `isDeviceSuitable`，确保设备能把图像呈现到我们创建的表面上。由于"呈现"是**队列专属**的能力，问题实际变成：找到一个支持向我们创建的表面呈现的队列族。

完全存在这种可能：支持绘制命令的队列族与支持呈现的队列族**并不重合**。因此必须考虑"存在一个独立呈现队列"的情况，修改 `QueueFamilyIndices` 结构体：

```cpp
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};
```

接着修改 `findQueueFamilies` 函数，让它找出具备向我们窗口表面呈现能力的队列族。检查函数是 `vkGetPhysicalDeviceSurfaceSupportKHR`，参数为物理设备、队列族索引和表面。在检查 `VK_QUEUE_GRAPHICS_BIT` 的同一个循环里加上对它的调用：

```cpp
VkBool32 presentSupport = false;
vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
```

然后检查这个布尔值，记下呈现族的队列索引：

```cpp
if (presentSupport) {
    indices.presentFamily = i;
}
```

注意：这两个最后很可能就是同一个队列族，但整个程序里我们会把它们当作两个独立队列对待——统一处理方式。话说回来，你也可以加逻辑去**优先选择**绘制与呈现同族支持的物理设备以提升性能。

## 创建呈现队列（Creating the presentation queue）

剩下的事：修改逻辑设备创建流程，把呈现队列也创建出来并取回 `VkQueue` 句柄。先加句柄成员：

```cpp
VkQueue presentQueue;
```

接下来我们需要多个 `VkDeviceQueueCreateInfo` 结构体，从两个族各建一条队列。一个优雅的做法是：把所需队列涉及的全部族去重后放进一个集合（set）：

```cpp
#include <set>

...

QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

float queuePriority = 1.0f;
for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
}
```

再修改 `VkDeviceCreateInfo` 指向这个 vector：

```cpp
createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
createInfo.pQueueCreateInfos = queueCreateInfos.data();
```

如果两个族相同，那只需传一次索引。最后加一行调用取回队列句柄：

```cpp
vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
```

如果两个族恰好相同，两个句柄现在多半是同一个值。下一章我们将研究**交换链（swap chain）**——它让我们有能力把图像呈现到表面上。
