# 04 物理设备与队列族（Physical devices and queue families）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Setup/Physical_devices_and_queue_families
> 【译注·DX12 对照：`VkPhysicalDevice` ≈ `IDXGIAdapter`（枚举显卡）；队列族 ≈ D3D12 的 direct/compute/copy 队列（但 Vulkan 按"族"查询）】

## 选取物理设备

通过 `VkInstance` 初始化 Vulkan 库之后，需要在系统里寻找并选择一块支持所需功能的显卡。实际上可以选任意多块同时使用，但本教程就用第一块满足需求的。

添加 `pickPhysicalDevice` 函数并在 `initVulkan` 里调用：

```cpp
void initVulkan() {
    createInstance();
    setupDebugMessenger();
    pickPhysicalDevice();
}

void pickPhysicalDevice() {

}
```

选中的显卡存进新成员 `VkPhysicalDevice` 句柄。**这个对象在 `VkInstance` 销毁时被隐式销毁**，所以 cleanup 里不用管它：

```cpp
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
```

列出显卡和列出扩展的套路一模一样，还是两段式，先问数量：

```cpp
uint32_t deviceCount = 0;
vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
```

一块 Vulkan 显卡都没有就没必要继续了：

```cpp
if (deviceCount == 0) {
    throw std::runtime_error("failed to find GPUs with Vulkan support!");
}
```

否则分配数组拿句柄：

```cpp
std::vector<VkPhysicalDevice> devices(deviceCount);
vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
```

现在要逐个评估它们是否满足我们要做的操作——不是所有显卡都一样。为此引入：

```cpp
bool isDeviceSuitable(VkPhysicalDevice device) {
    return true;
}
```

检查是否有任何一块物理设备满足要求：

```cpp
for (const auto& device : devices) {
    if (isDeviceSuitable(device)) {
        physicalDevice = device;
        break;
    }
}

if (physicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("failed to find a suitable GPU!");
}
```

下一节给 `isDeviceSuitable` 加第一批真正的检查；后面章节用到更多 Vulkan 特性时还会继续往里加。

## 基础的设备适合性检查

评估一块设备可以先查一些详情。名字、类型、支持的 Vulkan 版本这类基本属性用 `vkGetPhysicalDeviceProperties`：

```cpp
VkPhysicalDeviceProperties deviceProperties;
vkGetPhysicalDeviceProperties(device, &deviceProperties);
```

可选特性（纹理压缩、64 位浮点、多视口渲染——VR 用）的支持情况用 `vkGetPhysicalDeviceFeatures`：

```cpp
VkPhysicalDeviceFeatures deviceFeatures;
vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
```

设备还有更多可查询的详情，涉及设备内存和队列族的部分后面章节再讨论（见下一节）。

举例：假设我们的应用只支持**独立显卡且必须支持几何着色器**，那么：

```cpp
bool isDeviceSuitable(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    return deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
           deviceFeatures.geometryShader;
}
```

除了"是/否然后拿第一块"，还可以**给每块打分选最高**——独立显卡加分，只有集显时也能兜底：

```cpp
#include <map>

...

void pickPhysicalDevice() {
    ...

    // 用有序 map 自动按分数升序排
    std::multimap<int, VkPhysicalDevice> candidates;

    for (const auto& device : devices) {
        int score = rateDeviceSuitability(device);
        candidates.insert(std::make_pair(score, device));
    }

    // 检查最高分者是否真的可用
    if (candidates.rbegin()->first > 0) {
        physicalDevice = candidates.rbegin()->second;
    } else {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

int rateDeviceSuitability(VkPhysicalDevice device) {
    ...

    int score = 0;

    // 独显性能优势明显
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }

    // 纹理最大尺寸影响画质
    score += deviceProperties.limits.maxImageDimension2D;

    // 没有几何着色器直接一票否决
    if (!deviceFeatures.geometryShader) {
        return 0;
    }

    return score;
}
```

本教程不需要做这么全，只是给你看设备选择可以怎么设计。当然也可以干脆把候选名字列出来让用户选。

我们刚起步，唯一要求就是支持 Vulkan，所以教程就用"任何 GPU 都行"：

```cpp
bool isDeviceSuitable(VkPhysicalDevice device) {
    return true;
}
```

## 队列族

之前提过：Vulkan 里几乎每个操作——从绘制到上传纹理——都要求把**命令提交到队列**。队列有不同类型，来自不同的**队列族（queue family）**，每个族只允许一部分命令。比如可能有一个族只许计算命令、另一个只许内存传输命令。

我们要检查设备支持哪些队列族、其中哪个支持我们想用的命令。为此加一个 `findQueueFamilies` 函数，寻找我们需要的全部队列族。

现在只找支持**图形命令**的队列，函数可以先长这样：

```cpp
uint32_t findQueueFamilies(VkPhysicalDevice device) {
    // Logic to find graphics queue family
}
```

但接下来几章就要找另一个队列了，不如提前把索引打包进结构体：

```cpp
struct QueueFamilyIndices {
    uint32_t graphicsFamily;
};

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    // Logic to find queue family indices to populate struct with
    return indices;
}
```

可是**队列族找不到怎么办**？可以抛异常，但设备是否可用的决策不该在这个函数里做——比如我们可能**偏好**有专用传输队列的设备但不强制。所以需要一种"找到了没"的表示方法。

用魔法值（比如 -1）不行——`uint32_t` 任何值都可能是合法索引，包括 0。好在 C++17 引入了专门区分"值存在与否"的数据结构：

```cpp
#include <optional>

...

std::optional<uint32_t> graphicsFamily;

std::cout << std::boolalpha << graphicsFamily.has_value() << std::endl; // false

graphicsFamily = 0;

std::cout << std::boolalpha << graphicsFamily.has_value() << std::endl; // true
```

`std::optional` 是个包装器：赋值之前里面**没有值**，随时用 `has_value()` 查询。于是改成：

```cpp
#include <optional>

...

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
};

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    // Assign index to queue families that could be found
    return indices;
}
```

现在真正实现 `findQueueFamilies`。取队列族列表的流程你肯定猜到了，还是两段式，用 `vkGetPhysicalDeviceQueueFamilyProperties`：

```cpp
uint32_t queueFamilyCount = 0;
vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
```

`VkQueueFamilyProperties` 含队列族的详情：支持的操作类型、这个族能建的队列数量。我们要找至少一个支持 `VK_QUEUE_GRAPHICS_BIT` 的族：

```cpp
int i = 0;
for (const auto& queueFamily : queueFamilies) {
    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        indices.graphicsFamily = i;
    }

    i++;
}
```

有了这个查找函数，就能在 `isDeviceSuitable` 里用它做检查，确保设备能处理我们要的命令：

```cpp
bool isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);

    return indices.graphicsFamily.has_value();
}
```

顺手给结构体加个通用检查：

```cpp
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;

    bool isComplete() {
        return graphicsFamily.has_value();
    }
};

...

bool isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);

    return indices.isComplete();
}
```

还可以用它提前退出循环：

```cpp
for (const auto& queueFamily : queueFamilies) {
    ...

    if (indices.isComplete()) {
        break;
    }

    i++;
}
```

找物理设备需要的就这么多！下一步是创建**逻辑设备**去跟它对接。
