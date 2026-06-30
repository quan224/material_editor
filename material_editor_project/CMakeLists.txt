cmake_minimum_required(VERSION 3.20)
project(MaterialEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

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

    # vcpkg 的 Qt 没有自带 windeployqt，手动复制平台插件
    set(PLATFORM_PLUGIN_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>/platforms")
    add_custom_command(TARGET MaterialEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLATFORM_PLUGIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/vcpkg/installed/x64-windows/debug/Qt6/plugins/platforms/qwindowsd.dll"
            "${PLATFORM_PLUGIN_DIR}/"
        COMMENT "Copying Qt platform plugin..."
    )
endif()

file(COPY resources/ DESTINATION ${CMAKE_BINARY_DIR}/resources)