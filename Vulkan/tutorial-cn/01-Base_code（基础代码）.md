# 01 基础代码（Base code）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Setup/Base_code
> 代码块保持原样；术语首次出现附英文。

## 总体结构

上一章你已经配置好 Vulkan 项目并用示例代码测试通过。本章从下面这份代码从头开始：

```cpp
#include <vulkan/vulkan.h>

#include <iostream>
#include <stdexcept>
#include <cstdlib>

class HelloTriangleApplication {
public:
    void run() {
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    void initVulkan() {

    }

    void mainLoop() {

    }

    void cleanup() {

    }
};

int main() {
    HelloTriangleApplication app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
```

我们首先 include 来自 LunarG SDK 的 Vulkan 头文件，它提供全部函数、结构和枚举。`stdexcept` 和 `iostream` 用于报告和传播错误，`cstdlib` 提供 `EXIT_SUCCESS` / `EXIT_FAILURE` 宏。

程序本身包在一个类里：Vulkan 对象存为类的**私有成员**，每个对象的初始化函数将从 `initVulkan` 中调用。一切就绪后进入主循环开始渲染帧——稍后我们会在 `mainLoop` 里写一个循环，一直跑到窗口关闭。窗口关闭、`mainLoop` 返回后，在 `cleanup` 里确保释放我们用过的所有资源。

执行中若发生任何致命错误，就抛出带描述信息的 `std::runtime_error` 异常——它会一路传播回 `main` 函数并打印到命令行。为了同时兜住各种标准异常类型，我们 catch 更宽泛的 `std::exception`。马上会遇到的例子：发现某个必需的扩展（extension）不被支持。

本章之后的**每一节**大致都会：往 `initVulkan` 里加一个新函数的调用，往私有成员里加一个或多个新的 Vulkan 对象——它们都要在 `cleanup` 末尾释放。

## 资源管理

就像每块 `malloc` 出来的内存都要配一次 `free`，我们创建的**每个 Vulkan 对象不再需要时都必须显式销毁**。C++ 里可以用 RAII 或 `<memory>` 的智能指针做自动资源管理，但本教程选择对 Vulkan 对象的分配/释放**保持显式**——毕竟 Vulkan 的定位就是把每个操作都摆到明面上来避免错误，学习阶段把对象生命周期写明白，才能真正理解这个 API 的工作方式。

学完本教程后，你可以自己实现自动资源管理：写 C++ 类在构造函数里获取 Vulkan 对象、析构函数里释放，或给 `std::unique_ptr` / `std::shared_ptr` 配自定义删除器（按你的所有权需求选择）。**大型 Vulkan 程序推荐 RAII**，但学习阶段，知道幕后发生了什么永远不亏。

Vulkan 对象要么直接用 `vkCreateXXX` 创建，要么经由另一个对象用 `vkAllocateXXX` 分配。确认对象不再被任何地方使用后，用对应的 `vkDestroyXXX` / `vkFreeXXX` 销毁。这些函数的参数因对象类型而异，但有一个参数全都有：`pAllocator`——可选参数，允许你为自定义内存分配器指定回调。本教程忽略它，一律传 `nullptr`。

## 集成 GLFW

如果只想做离屏渲染（off-screen rendering），Vulkan 不开窗口也完全能跑——但能真的显示点什么才有意思！首先把 `#include <vulkan/vulkan.h>` 那行替换为：

```cpp
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
```

这样 GLFW 会带上它自己的定义，并自动把 Vulkan 头文件一起加载进来。添加一个 `initWindow` 函数，并让它成为 `run` 里的第一个调用：

```cpp
void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

private:
    void initWindow() {

    }
```

我们用这个函数初始化 GLFW 并创建窗口。

`initWindow` 的第一个调用应该是 `glfwInit()`——初始化 GLFW 库。由于 GLFW 当初是为创建 OpenGL 上下文设计的，我们需要告诉它**别**创建 OpenGL 上下文：

```cpp
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
```

窗口缩放（resize）的处理需要特殊关照（后面章节再看），现在先禁掉：

```cpp
glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
```

剩下的就是创建窗口本体。加一个私有成员存它的引用：

```cpp
private:
GLFWwindow* window;
```

初始化窗口：

```cpp
window = glfwCreateWindow(800, 600, "Vulkan", nullptr, nullptr);
```

前三个参数是宽、高、窗口标题。第四个参数可选，指定在哪个显示器上开窗；最后一个参数只对 OpenGL 有意义。

用常量代替硬编码的宽高数字是个好习惯——后面会反复用到这些值。在 `HelloTriangleApplication` 类定义上方加：

```cpp
const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;
```

把创建窗口的调用换成：

```cpp
window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
```

现在你的 `initWindow` 应该长这样：

```cpp
void initWindow() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
}
```

为了让应用一直运行到出错或窗口关闭，需要在 `mainLoop` 里加事件循环：

```cpp
void mainLoop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }
}
```

这段代码不言自明：循环检查事件（比如按 X 按钮）直到用户关掉窗口。**之后我们渲染单帧的函数调用也会放在这个循环里**。

窗口关闭后需要清理资源：销毁窗口、终止 GLFW。这是 `cleanup` 的第一批代码：

```cpp
void cleanup() {
    glfwDestroyWindow(window);

    glfwTerminate();
}
```

现在运行程序，应该能看到一个标题为 `Vulkan` 的窗口，直到你把它关掉。骨架就绪，下一节我们创建第一个真正的 Vulkan 对象！
