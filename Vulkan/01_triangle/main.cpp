// vulkan-tutorial.com 跟写：01_triangle
// 实现在 vulkan.h/.cpp，main 只负责启动
#include "vulkan.h"
#include <iostream>
#include <cstdlib>

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
