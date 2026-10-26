// vulkan-tutorial.com 跟写：01_triangle
// 教程入口：Overview → Environment setup → 之后的 Instance/Validation layers/...
// 内容按教程逐节写，每节完成在文件顶部记一行 DX12 对应关系（见 ../学习路线.md）
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>   // 显式包含：GLFW 嵌套包含在 /external:I 路径下未生效
#include <cstdio>
#include <vector>

#include <iostream>
#include <cstdlib>

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

bool checkValidationLayerSupport() {
    // 两段式取值，内存分配由用户控制
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> avaliableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, avaliableLayers.data());
    for (const char* layerName : validationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : avaliableLayers) {
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
}

class HelloTriangleApplication {
public:
    void run() {
        init_window();
        init_vulkan();
        main_loop();
        clean_up();
    }
private:
    void init_window() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);


    }

    void create_instance() {
        // 检查权限
        if (enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("validation layers requested, but not available!");
        }

        // 相当于一个名片
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Hello Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;
        // 相当于一个申请表 夹着名片
        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &appInfo;
        // 申请哪些接口
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        create_info.enabledExtensionCount = glfwExtensionCount;
        create_info.ppEnabledExtensionNames = glfwExtensions;
        // 启用时带上验证层的名字
        if (enableValidationLayers) {
            create_info.enabledLayerCount = validationLayers.size();
            create_info.ppEnabledLayerNames = validationLayers.data();
        }
        else {
            create_info.enabledLayerCount = 0;
        }
        
        // 返回实例
        VkResult result = vkCreateInstance(&create_info, nullptr, &instance);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to create instance");
        }
    }

    void init_vulkan() {
        create_instance();

    }
    void main_loop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }

    }
    void clean_up() {
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();

    }
private:
    GLFWwindow* window;
    VkInstance instance;
};

int main() {

    HelloTriangleApplication app;
    try {
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
