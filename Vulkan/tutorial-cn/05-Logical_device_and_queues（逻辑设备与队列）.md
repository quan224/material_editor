# 05 逻辑设备与队列（Logical device and queues）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Setup/Logical_device_and_queues

## 引言（Introduction）

选定要用的物理设备之后，我们需要设置一个**逻辑设备（logical device）**来与它对接。逻辑设备的创建过程与实例的创建过程类似，描述我们要使用的功能。既然已经查询过有哪些队列族可用，现在还需要指定要创建哪些队列。如果需求各异，你甚至可以从同一块物理设备创建多个逻辑设备。

先添加一个新类成员来存逻辑设备句柄：

```cpp
VkDevice device;
```

接着添加 `createLogicalDevice` 函数，从 `initVulkan` 调用：

```cpp
void initVulkan() {
    createInstance();
    setupDebugMessenger();
    pickPhysicalDevice();
    createLogicalDevice();
}

void createLogicalDevice() {

}
```

## 指定要创建的队列（Specifying the queues to be created）

创建逻辑设备又要往一堆结构体里填详情，第一个是 `VkDeviceQueueCreateInfo`。这个结构体描述我们想从**单个队列族**要多少条队列。目前我们只对具备图形能力的队列感兴趣。

```cpp
QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

VkDeviceQueueCreateInfo queueCreateInfo{};
queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
queueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
queueCreateInfo.queueCount = 1;
```

当前的驱动只允许你为每个队列族创建少量队列，而且你其实用不着更多条。因为所有命令缓冲都可以在多个线程上创建，然后在主线程用一次低开销调用全部提交。

Vulkan 允许你给队列指定**优先级**，用 0.0 到 1.0 之间的浮点数影响命令缓冲执行的调度。**即使只有一条队列，这个字段也是必填的**：

```cpp
float queuePriority = 1.0f;
queueCreateInfo.pQueuePriorities = &queuePriority;
```

## 指定使用的设备功能（Specifying used device features）

接下来要指定的是我们要用的一组**设备功能**。就是上一章用 `vkGetPhysicalDeviceFeatures` 查询过支持情况的那些功能，比如几何着色器。目前我们不需要任何特殊功能，所以直接定义一个结构体、全部留给 `VK_FALSE` 即可。等我们开始用 Vulkan 做更有意思的事情时再回到这个结构体。

```cpp
VkPhysicalDeviceFeatures deviceFeatures{};
```

## 创建逻辑设备（Creating the logical device）

前两个结构体就位，可以开始填主结构 `VkDeviceCreateInfo` 了：

```cpp
VkDeviceCreateInfo createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
```

首先加上队列创建信息和设备功能结构体的指针：

```cpp
createInfo.pQueueCreateInfos = &queueCreateInfo;
createInfo.queueCreateInfoCount = 1;

createInfo.pEnabledFeatures = &deviceFeatures;
```

其余信息和 `VkInstanceCreateInfo` 结构体长得像，也要求你指定扩展和验证层。区别在于这些这次是**设备特定**的。

设备特定扩展的一个例子是 `VK_KHR_swapchain`，它让你能把该设备渲染出的图像呈现到窗口。系统里完全可能存在不具备这个能力的 Vulkan 设备——比如某些只支持计算操作的设备。我们会在交换链（swap chain）那章回到这个扩展。

以前的 Vulkan 实现区分实例特定和设备特定的验证层，现在不再区分了。也就是说 `VkDeviceCreateInfo` 的 `enabledLayerCount` 和 `ppEnabledLayerNames` 字段会被最新的实现忽略。不过，为了兼容旧实现，还是把它们设置上为好：

```cpp
createInfo.enabledExtensionCount = 0;

if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
} else {
    createInfo.enabledLayerCount = 0;
}
```

我们暂时不需要任何设备特定的扩展。

就这些，现在可以用名字恰如其分的 `vkCreateDevice` 函数实例化逻辑设备了：

```cpp
if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
    throw std::runtime_error("failed to create logical device!");
}
```

参数依次是：要对接的物理设备、我们刚指定的队列与用法信息、可选的分配回调指针、存储逻辑设备句柄的变量指针。和实例创建函数一样，启用了不存在的扩展或指定了不支持的功能用法时，这个调用会返回相应错误。

设备应在 `cleanup` 里用 `vkDestroyDevice` 销毁：

```cpp
void cleanup() {
    vkDestroyDevice(device, nullptr);
    ...
}
```

逻辑设备不与实例直接交互，所以 `vkDestroyDevice` 不含实例参数。

## 取回队列句柄（Retrieving queue handles）

队列随逻辑设备**自动创建**，但我们还没有与它们交互的句柄。先加一个类成员存图形队列的句柄：

```cpp
VkQueue graphicsQueue;
```

设备队列在设备销毁时被隐式清理，所以 `cleanup` 里我们什么都不用做。

可以用 `vkGetDeviceQueue` 函数逐队列族取回队列句柄。参数是：逻辑设备、队列族、队列索引、存储队列句柄的变量指针。因为我们只从这个族创建了一条队列，直接用索引 `0`：

```cpp
vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
```

有了逻辑设备和队列句柄，我们终于可以真正开始用显卡干活了！接下来几章我们将设置把结果呈现给窗口系统的资源。
