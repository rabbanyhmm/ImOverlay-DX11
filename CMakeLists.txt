cmake_minimum_required(VERSION 3.15)
project(ImOverlay-DX11 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ImOverlay-DX11 STATIC
    overlay_manager.h
    overlay_manager.cpp
)

target_include_directories(ImOverlay-DX11 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(ImOverlay-DX11 PUBLIC
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    uxtheme
    winmm
)

option(IMOVERLAY_BUILD_EXAMPLES "Build example applications" ON)

if(IMOVERLAY_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/minimal_demo/main.cpp")
    add_executable(minimal_demo WIN32
        examples/minimal_demo/main.cpp
        overlay_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../imgui.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../imgui_draw.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../imgui_tables.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../imgui_widgets.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../backends/imgui_impl_win32.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../backends/imgui_impl_dx11.cpp
    )
    target_include_directories(minimal_demo PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../backends
    )
    target_link_libraries(minimal_demo PRIVATE
        d3d11
        dxgi
        d3dcompiler
        dwmapi
        uxtheme
        winmm
    )
endif()

