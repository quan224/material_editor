# 02 实例（Instance）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Setup/Instance

## 创建实例

要做的第一件事是创建一个**实例（instance）**来初始化 Vulkan 库。实例是你的应用与 Vulkan 库之间的连接，创建它时需要向驱动提供一些关于你应用的信息。

先添加 `createInstance` 函数并在 `initVulkan` 里调用：

```cpp
void initVulkan() {
    createInstance();
}
```

再加一个数据成员持有实例的句柄：

```cpp
private:
VkInstance instance;
```

创建实例前，先填一个描述应用信息的结构体。这些数据严格说是可选的，但可能给驱动提供有用的信息来优化你的应用（比如它用了某个有特殊行为的知名引擎）。这个结构体叫 `VkApplicationInfo`：

```cpp
void createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;
}
```

如前所述，Vulkan 的很多结构体都要求在 `sType` 成员里显式写明类型。它也是众多带 `pNext` 成员的结构体之一——`pNext` 可在未来指向扩展信息，这里我们用值初始化（`{}`）让它保持 `nullptr`。

Vulkan 的大量信息都通过**结构体**而非函数参数传递。还需要再填一个结构体，提供创建实例所需的其余信息。这个结构体**不是可选的**，它告诉 Vulkan 驱动我们要用哪些**全局扩展（global extensions）和验证层（validation layers）**。说"全局"是因为它们作用于整个程序而非某个具体设备——后面几章会看清这一点。

```cpp
VkInstanceCreateInfo createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
createInfo.pApplicationInfo = &appInfo;
```

前两个参数一目了然。接下来两个成员指定所需的全局扩展。正如总览章节所说，Vulkan 是平台无关的 API，意味着你需要一个扩展来对接窗口系统。GLFW 有个内置函数返回它所需的扩展，直接传给结构体即可：

```cpp
uint32_t glfwExtensionCount = 0;
const char** glfwExtensions;

glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

createInfo.enabledExtensionCount = glfwExtensionCount;
createInfo.ppEnabledExtensionNames = glfwExtensions;
```

结构体最后两个成员决定启用的全局验证层——下一章深入讲，现在先留空：

```cpp
createInfo.enabledLayerCount = 0;
```

一切就绪，终于可以发起 `vkCreateInstance` 调用：

```cpp
VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
```

你会看到，Vulkan 对象创建函数的参数遵循固定模式：

- 指向创建信息结构体的指针
- 指向自定义分配器回调的指针（本教程恒为 `nullptr`）
- 指向存储新对象句柄的变量的指针

一切顺利的话，实例句柄已存进 `VkInstance` 成员。几乎所有 Vulkan 函数都返回 `VkResult`——要么是 `VK_SUCCESS` 要么是错误码。检查实例是否创建成功，不必存返回值，直接判断即可：

```cpp
if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
    throw std::runtime_error("failed to create instance!");
}
```

运行程序，确认实例创建成功。

## 遇到 VK_ERROR_INCOMPATIBLE_DRIVER

如果在 MacOS 用最新 MoltenVK SDK，`vkCreateInstance` 可能返回 `VK_ERROR_INCOMPATIBLE_DRIVER`。根据入门说明，从 Vulkan SDK 1.3.216 起，`VK_KHR_PORTABILITY_subset` 扩展是必选的。

解决：先给 `VkInstanceCreateInfo` 的 flags 加上 `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` 位，再把 `VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME` 加进实例的启用扩展列表。典型写法：

```cpp
...

std::vector<const char*> requiredExtensions;

for(uint32_t i = 0; i < glfwExtensionCount; i++) {
    requiredExtensions.emplace_back(glfwExtensions[i]);
}

requiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

createInfo.enabledExtensionCount = (uint32_t) requiredExtensions.size();
createInfo.ppEnabledExtensionNames = requiredExtensions.data();

if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
    throw std::runtime_error("failed to create instance!");
}
```

（Windows + NVIDIA 不涉及此问题。）

## 检查扩展支持

翻 `vkCreateInstance` 文档会发现可能的错误码里有 `VK_ERROR_EXTENSION_NOT_PRESENT`。我们可以直接声明必需的扩展、报错就退出——对窗口系统接口这类关键扩展这没问题；但如果你想检查的是**可选功能**呢？

要在创建实例**之前**获取支持的扩展列表，用 `vkEnumerateInstanceExtensionProperties`。它接收：指向扩展数量的指针、存储扩展详情的 `VkExtensionProperties` 数组，还有一个可选的首参数用于按验证层过滤（暂忽略）。

要分配数组先得知道数量——后一个参数留空就能只查数量：

```cpp
uint32_t extensionCount = 0;
vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
```

分配数组（`#include <vector>`）：

```cpp
std::vector<VkExtensionProperties> extensions(extensionCount);
```

查询扩展详情：

```cpp
vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
```

每个 `VkExtensionProperties` 含扩展的名字和版本，一个 for 循环就能列出（`\t` 是缩进用的制表符）：

```cpp
std::cout << "available extensions:\n";

for (const auto& extension : extensions) {
    std::cout << '\t' << extension.extensionName << '\n';
}
```

这段代码可以加进 `createInstance` 来打印 Vulkan 支持情况。留个挑战：写一个函数，检查 `glfwGetRequiredInstanceExtensions` 返回的扩展是否全部包含在支持列表里。

## 清理

`VkInstance` 应该只在程序退出前销毁，在 `cleanup` 里用 `vkDestroyInstance`：

```cpp
void cleanup() {
    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);

    glfwTerminate();
}
```

`vkDestroyInstance` 的参数一目了然。如上一章所说，Vulkan 的分配/释放函数都带可选的分配器回调，传 `nullptr` 忽略。**之后章节创建的所有其他 Vulkan 资源都必须在实例销毁之前清理**。

在继续实例创建之后的复杂步骤之前，先通过验证层看看我们的调试选项。
