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

# === 保留源码目录结构到 IDE 视图 ===
# VS 默认会把所有 .h 塞进 "Header Files"、.cpp 塞进 "Source Files"，丢失模块划分。
# source_group(TREE ...) 让 IDE 解决方案按真实目录树展示（Core/MaterialGraph/Reflection/...）。
# 跨 IDE 通用：VS / VSCode CMake Tools / Xcode 都吃这个。
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "" FILES ${SOURCES} ${HEADERS})
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
if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    file(COPY resources/ DESTINATION ${CMAKE_BINARY_DIR}/resources)
endif()

# === 部署 Qt 平台插件（开发期）===
# Qt 程序运行时必须能找到 platforms/qwindows[d].dll，否则启动直接报错。
# vcpkg 装的 Qt6 不带 windeployqt，没法自动收集依赖，所以这里手动拷一个最关键的。
# 跨机器通用：Qt6::QWindowsIntegrationPlugin 这个 target 由 Qt6Gui 包暴露，
#             vcpkg / MSYS2 / 官方 Qt 安装包都有，路径自动适配。
if(TARGET Qt6::QWindowsIntegrationPlugin)
    add_custom_command(TARGET MaterialEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:MaterialEditor>/platforms
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>
                $<TARGET_FILE_DIR:MaterialEditor>/platforms/
        COMMENT "Copying Qt Windows platform plugin"
    )
else()
    message(WARNING "Qt6::QWindowsIntegrationPlugin target not found — exe will need QT_QPA_PLATFORM_PLUGIN_PATH set to run")
endif()

# === 打包目标：用 windeployqt 收集 Qt 运行时依赖到构建目录 ===
# 日常开发：cmake --build --preset debug                  （快，不跑 windeployqt）
# 发布打包：cmake --build --preset debug --target deploy  （先 build，再自动收集 Qt dll + 平台插件）
#
# 跨机器兼容：
#   MSYS2 Qt6：windeployqt.exe 在 mingw64/bin，find_program 能从 PATH 找到
#   vcpkg Qt6：根本不带 windeployqt.exe，find_program 返回空 → 不创建 deploy target
find_program(WINDEPLOYQT_EXE windeployqt)
if(WINDEPLOYQT_EXE)
    add_custom_target(deploy
        COMMAND ${WINDEPLOYQT_EXE}
                $<TARGET_FILE:MaterialEditor>
                --no-translations
        DEPENDS MaterialEditor
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Collecting Qt runtime dependencies (windeployqt)"
    )
else()
    message(STATUS "windeployqt.exe not found — 'deploy' target disabled (vcpkg Qt6 doesn't ship it)")
endif()
