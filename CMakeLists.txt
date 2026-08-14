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
)

target_link_libraries(ImOverlay-DX11 PUBLIC
    d3d11
    dxgi
    d3dcompiler
    dwmapi
)
