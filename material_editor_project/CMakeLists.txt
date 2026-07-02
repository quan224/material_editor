cmake_minimum_required(VERSION 3.20)
project(MaterialEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# === 工具链区分 ===
# MSVC 路线走 vcpkg；MinGW 走 MSYS2（不需要 vcpkg，依赖装在 mingw64 标准位置）
if(MSVC)
    set(VCPKG_TARGET_TRIPLET x64-windows CACHE STRING "vcpkg triplet")
endif()

find_package(Qt6 COMPONENTS Core Widgets REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS src/*.h)

add_executable(MaterialEditor ${SOURCES} ${HEADERS})
target_link_libraries(MaterialEditor PRIVATE
    Qt6::Core Qt6::Widgets
    glm::glm nlohmann_json::nlohmann_json
)
target_include_directories(MaterialEditor PRIVATE src/)

if(WIN32)
    target_compile_definitions(MaterialEditor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(MaterialEditor PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

# === 编译器特定标志 ===
if(MSVC)
    target_compile_options(MaterialEditor PRIVATE /utf-8 /W4)
elseif(MINGW)
    target_compile_options(MaterialEditor PRIVATE -Wall -Wextra -Wno-unused-parameter)
    # 静态链接 MinGW 运行时（libgcc/libstdc++/libwinpthread），
    # 让 exe 不依赖这几个 dll，方便分发到没装 MSYS2 的机器
    target_link_options(MaterialEditor PRIVATE -static)
endif()

# === 复制资源（resources 目录存在时才复制，避免 configure 失败）===
# 注意：Qt 平台插件（platforms/qwindows.dll）暂不处理——当前 main.cpp
# 不创建 QApplication，无需插件即可运行。等课10 做 Qt 窗口时再用 windeployqt。
if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    file(COPY resources/ DESTINATION ${CMAKE_BINARY_DIR}/resources)
endif()
