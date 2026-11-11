// HelloTriangleApplication：vulkan-tutorial.com 跟写（01_triangle）
// 原实现拆自 main.cpp，函数与教程逐节对应（见 ../tutorial-cn/）
#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>   // 显式包含：GLFW 嵌套包含在 /external:I 路径下未生效
#include <vector>
#include <optional>
#include <limits>
#include <algorithm>


struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capablities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

const std::vector<const char*> device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

class HelloTriangleApplication {
public:
    // 主流程：开窗口 → 初始化 Vulkan → 主循环 → 清理
    void run();

private:
    void init_window();            // 开 GLFW 窗口（禁 OpenGL 上下文、禁拉伸）
    void init_vulkan();            // Vulkan 初始化总调度（顺序即依赖顺序，勿乱动）
    void create_instance();        // 创建实例：与 Vulkan 的会话起点
    void createSurface();          // 创建窗口表面：渲染图像的呈现目标
    void setupDebugMessenger();    // 注册调试信使（仅开发期）
    void pickPhysicalDevice();     // 枚举显卡，挑第一个满足需求的
    void createLogicalDevice();    // 创建逻辑设备（申请图形+呈现队列）并取回队列句柄
    void createSwapChain();  // 创建交换链：显示接力池（三方协商格式/模式/尺寸）
    void createImageViews();  // 每张交换链图配取景器（格式+范围）
    void createGraphicsPipeline();  // 图形管线（着色器+固定功能，10-13 课在此展开）
    bool isDeviceSuitable(VkPhysicalDevice device);  // 设备够不够格：独显+几何着色器+族齐+swapchain 扩展
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);  // 设备扩展在不在（VK_KHR_swapchain）
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);  // 问 surface 能力三筐（六次两段式）
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availavleFormats);  // 三选①格式（B8G8R8A8_SRGB）
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);  // 三选②呈现模式（MAILBOX→FIFO 兜底）
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);  // 三选③尺寸（currentExtent 或 clamp）
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);                // 查图形族/呈现族门牌号
    VkShaderModule createShaderModule(const std::vector<char>& code);
    void main_loop();              // 泵窗口消息直到关窗（以后在这里画帧）
    void clean_up();               // 按依赖逆序销毁（孩子先死，instance 最后）

    GLFWwindow* window;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;

    VkPipelineLayout pipelineLayout;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue graphicsQueue;  // 图形队列
    VkQueue presentQueue;   // 呈现队列
};
