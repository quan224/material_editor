# 03 验证层（Validation layers）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Setup/Validation_layers

## 验证层是什么？

Vulkan API 的设计围绕"最小化驱动开销"的理念，其表现之一就是**默认情况下 API 几乎不做错误检查**。哪怕是把枚举设成错误值、给必填参数传空指针这种低级错误，通常也不会被显式处理——直接崩溃或未定义行为。Vulkan 要求你对每件事都显式声明，也就容易犯各种小错，比如用了某个新 GPU 特性却忘了在创建逻辑设备时请求它。

但这不代表这些检查没法加进 API。Vulkan 为此引入了一套优雅的系统——**验证层（validation layers）**。验证层是可选组件，挂接（hook）进 Vulkan 函数调用以执行额外操作。常见操作有：

- 对照规范检查参数值，检测误用
- 跟踪对象的创建与销毁，发现资源泄漏
- 通过跟踪调用来源线程检查线程安全
- 把每次调用及其参数打印到标准输出
- 跟踪 Vulkan 调用，用于性能分析与重放

诊断验证层里一个函数的实现大概长这样：

```cpp
VkResult vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* instance) {

    if (pCreateInfo == nullptr || instance == nullptr) {
        log("Null pointer passed to required parameter!");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    return real_vkCreateInstance(pCreateInfo, pAllocator, instance);
}
```

验证层可以自由堆叠，把你想要的调试功能全开上。你可以 debug 构建开验证层、release 构建完全禁用——两全其美！

Vulkan 本身不内置任何验证层，但 **LunarG Vulkan SDK 提供了一整套检查常见错误的层**，且完全开源——你可以看它们查哪些错、甚至贡献代码。使用验证层是避免你的应用"不小心依赖未定义行为而在不同驱动上跑挂"的最好办法。

验证层只有装在系统上才能用——比如 LunarG 验证层只在装了 Vulkan SDK 的机器上可用。

Vulkan 以前有两种验证层：实例级和设备级。设计意图是实例层只查实例这类全局对象相关的调用、设备层只查特定 GPU 相关的调用。**设备级验证层现在已废弃**——实例验证层会应用于所有 Vulkan 调用。规范文档仍建议在设备级也启用相同层以兼容（某些实现要求），我们到逻辑设备那节会照做。

本节看如何启用 Vulkan SDK 提供的标准诊断层。和扩展一样，验证层靠名字启用。所有有用的标准验证被打包在 SDK 的一个层里：**`VK_LAYER_KHRONOS_validation`**。

先给程序加两个配置变量：要启用的层、是否启用。我选择以是否 debug 编译为准——`NDEBUG` 宏是 C++ 标准的一部分，意为"not debug"：

```cpp
const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
```

再写一个 `checkValidationLayerSupport` 检查请求的层是否全部可用。先用 `vkEnumerateInstanceLayerProperties` 列出所有可用层——用法和实例那章讲过的 `vkEnumerateInstanceExtensionProperties` 完全一致：

```cpp
bool checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    return false;
}
```

接着检查 `validationLayers` 里的每个名字是否存在于 `availableLayers`（`strcmp` 需要 `<cstring>`）：

```cpp
for (const char* layerName : validationLayers) {
    bool layerFound = false;

    for (const auto& layerProperties : availableLayers) {
        if (strcmp(layerName, layerProperties.layerName) == 0) {
            layerFound = true;
            break;
        }
    }

    if (!layerFound) {
        return false;
    }
}

return true;
```

在 `createInstance` 里用上：

```cpp
void createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    ...
}
```

用 debug 模式运行，确认不报错。报了就查 FAQ（通常是 SDK 没装好）。

最后修改 `VkInstanceCreateInfo` 的填写，启用时带上验证层名字：

```cpp
if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
} else {
    createInfo.enabledLayerCount = 0;
}
```

检查通过的话 `vkCreateInstance` 不该再返回 `VK_ERROR_LAYER_NOT_PRESENT`，但跑一下确认最稳。

## 消息回调

验证层默认把调试消息打到标准输出，但我们也可以在程序里提供显式**回调**自己处理——这样还能筛选想看哪些消息（不是所有消息都是致命错误）。不想现在做可以跳到本章最后一节。

要在程序里建立处理消息的回调，需要通过 `VK_EXT_debug_utils` 扩展设置一个带回调的 **debug messenger**。

先写 `getRequiredExtensions`：按是否启用验证层返回所需扩展列表：

```cpp
std::vector<const char*> getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}
```

GLFW 指定的扩展永远必需，debug messenger 扩展按条件追加。注意用了 `VK_EXT_DEBUG_UTILS_EXTENSION_NAME` 宏（等于字符串字面量 `"VK_EXT_debug_utils"`）——用它避免手拼出错。

在 `createInstance` 里替换原来的写法：

```cpp
auto extensions = getRequiredExtensions();
createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
createInfo.ppEnabledExtensionNames = extensions.data();
```

跑一下确保没有 `VK_ERROR_EXTENSION_NOT_PRESENT`。这个扩展的存在性其实不必专门查——验证层可用就隐含它可用。

现在看回调函数长什么样。添加静态成员函数 `debugCallback`，原型是 `PFN_vkDebugUtilsMessengerCallbackEXT`。`VKAPI_ATTR` 和 `VKAPI_CALL` 保证函数签名正确、Vulkan 能调用它：

```cpp
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}
```

第一个参数是消息严重度（severity），取值之一：

- `VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT`：诊断信息
- `VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT`：一般信息，如资源创建
- `VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT`：不一定是错误、但很可能是你程序 bug 的行为
- `VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT`：无效且可能导致崩溃的行为

这个枚举的值设计成可以用比较运算——比如判断"是否达到 Warning 级及以上"：

```cpp
if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    // Message is important enough to show
}
```

`messageType` 参数取值：

- `VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT`：与规范或性能无关的杂项事件
- `VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT`：违反规范或可能是错误的行为
- `VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT`：Vulkan 的潜在非最优用法

`pCallbackData` 指向含消息详情的 `VkDebugUtilsMessengerCallbackDataEXT`，最重要的成员：

- `pMessage`：以空字符结尾的调试消息字符串
- `pObjects`：与消息相关的 Vulkan 对象句柄数组
- `objectCount`：数组长度

最后 `pUserData` 是设置回调时你指定的指针，用来传自己的数据。

回调返回布尔值：是否中止触发这条验证消息的 Vulkan 调用。返回 true 则调用以 `VK_ERROR_VALIDATION_FAILED_EXT` 错误中止——这通常只用于测试验证层自身，**你应该永远返回 `VK_FALSE`**。

剩下的就是告诉 Vulkan 这个回调。有点意外的是，Vulkan 里连 debug 回调都是用句柄管理的、要显式创建和销毁。这种回调属于 **debug messenger**，想建多少个都行。在 `instance` 成员下面加：

```cpp
VkDebugUtilsMessengerEXT debugMessenger;
```

添加 `setupDebugMessenger`，在 `initVulkan` 里紧跟 `createInstance` 之后调用：

```cpp
void initVulkan() {
    createInstance();
    setupDebugMessenger();
}

void setupDebugMessenger() {
    if (!enableValidationLayers) return;

}
```

填 messenger 和回调的详情结构体：

```cpp
VkDebugUtilsMessengerCreateInfoEXT createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
createInfo.pfnUserCallback = debugCallback;
createInfo.pUserData = nullptr; // Optional
```

`messageSeverity` 指定哪些严重度会触发回调——这里我除了 `INFO` 全开了（要可能的问题通知、不要啰嗦的一般调试信息）。`messageType` 过滤消息类型——全开，没用到的以后再关。`pfnUserCallback` 是回调函数指针；`pUserData` 可选，比如可以传 `HelloTriangleApplication` 类的指针进回调。

配置验证层消息和回调的方式还有很多，这套对本教程够用了——详见扩展规范。

这个结构体要传给 `vkCreateDebugUtilsMessengerEXT` 来创建 `VkDebugUtilsMessengerEXT` 对象。麻烦的是：**这是扩展函数，不会自动加载**，得自己用 `vkGetInstanceProcAddr` 查地址。我们写一个代理函数包掉这件事（放在 `HelloTriangleApplication` 类定义上方）：

```cpp
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}
```

`vkGetInstanceProcAddr` 加载失败返回 `nullptr`。现在可以调用它创建扩展对象：

```cpp
if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
    throw std::runtime_error("failed to set up debug messenger!");
}
```

倒数第二个参数照例是分配器回调传 `nullptr`。debug messenger 隶属于我们的 Vulkan 实例及其层，所以**第一个参数必须显式传 instance**——后面其他"子对象"也会反复见到这个模式。

`VkDebugUtilsMessengerEXT` 对象要用 `vkDestroyDebugUtilsMessengerEXT` 清理——同样需要显式加载。再写一个代理函数：

```cpp
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}
```

确保它是静态成员函数或类外函数。在 `cleanup` 里调用：

```cpp
void cleanup() {
    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }

    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);

    glfwTerminate();
}
```

## 调试实例的创建与销毁本身

虽然已经加上了验证层，但还有覆盖不到的地方：`vkCreateDebugUtilsMessengerEXT` 要求实例已创建、`vkDestroyDebugUtilsMessengerEXT` 必须在实例销毁前调用——这俩调用本身出问题就没法调试了。

细读扩展文档会发现：可以专门为 `vkCreateInstance` / `vkDestroyInstance` 这两次调用**另建一个 debug messenger**——只需把 `VkDebugUtilsMessengerCreateInfoEXT` 结构体的指针传给 `VkInstanceCreateInfo` 的 `pNext` 字段。先把填写逻辑抽成独立函数：

```cpp
void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

...

void setupDebugMessenger() {
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}
```

在 `createInstance` 里复用：

```cpp
void createInstance() {
    ...

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    ...

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;

        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance!");
    }
}
```

注意 `debugCreateInfo` 变量放在 if 外面——**确保它在 `vkCreateInstance` 调用前不被销毁**。用这种方式附加的 debug messenger 会在 `vkCreateInstance` / `vkDestroyInstance` 期间自动生效，之后自动清理。

## 测试

故意犯个错看验证层的表现：临时删掉 `cleanup` 里的 `DestroyDebugUtilsMessengerEXT` 调用再运行，程序退出时应该看到类似下图的东西【原文此处有一张截图（validation_layer_test.png），md 中未嵌入，需要时到原网页查看】。没消息就检查 SDK 安装。想知道是哪次调用触发的，可以在消息回调里打断点看调用栈。

## 配置

除了 `VkDebugUtilsMessengerCreateInfoEXT` 里的那些 flag，验证层还有更多设置。到 Vulkan SDK 的 `Config` 目录看 `vk_layer_settings.txt`，里面有层的配置说明——把该文件拷到项目的 `Debug` / `Release` 目录即可为你的应用定制行为。本教程后续假设你用默认配置。

接下来本教程还会故意犯几次错，展示验证层抓它们有多好用——让你体会"在 Vulkan 里精确知道自己每步在干什么"有多重要。下一节看系统里的 Vulkan 设备。
