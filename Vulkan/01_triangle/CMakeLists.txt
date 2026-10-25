cmake_minimum_required(VERSION 3.20)
project(vk_triangle CXX)
set(CMAKE_CXX_STANDARD 17)

# Vulkan：LunarG SDK（FindVulkan 在 CMake4.3 + 该 SDK 布局下自动探测失败，显式喂路径最稳）
set(VK_SDK "C:/VulkanSDK/1.4.363.0" CACHE PATH "LunarG Vulkan SDK")
set(Vulkan_LIBRARY "${VK_SDK}/Lib/vulkan-1.lib" CACHE FILEPATH "")
set(Vulkan_INCLUDE_DIR "${VK_SDK}/Include" CACHE PATH "")
find_package(Vulkan REQUIRED)

# GLFW：Vulkan/thirdparty/glfw 预编译包（与主项目 vcpkg 完全无关）
set(GLFW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/glfw)

add_executable(vk_triangle main.cpp)
target_include_directories(vk_triangle PRIVATE ${GLFW_DIR}/include)
target_link_libraries(vk_triangle PRIVATE Vulkan::Vulkan ${GLFW_DIR}/lib-vc2022/glfw3dll.lib)

# 运行期拷 glfw3.dll 到 exe 旁
add_custom_command(TARGET vk_triangle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${GLFW_DIR}/lib-vc2022/glfw3.dll $<TARGET_FILE_DIR:vk_triangle>)
