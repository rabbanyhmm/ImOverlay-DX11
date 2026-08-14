# Minimal Demo — ImOverlay-DX11

A minimal (~100 lines) complete example showing how to use **ImOverlay-DX11** from scratch.

## What It Demonstrates

| Feature | Code |
|---------|------|
| Transparent Win32 window | `WS_EX_LAYERED \| WS_EX_TOPMOST` |
| D3D11 swapchain setup | `CreateDeviceD3D()` |
| ImOverlay Manager init | `Manager::Get().Init(...)` |
| Floating overlay with Acrylic blur | `Config::enable_acrylic_blur = true` |
| Magnetic edge snapping | `Config::enable_snap = true` |
| Multi-toast notification | `Manager::Get().PushToast(...)` |
| Global hotkey (Insert) | `RegisterHotkey(1, 0, VK_INSERT, HotkeyAction::ToggleVisibility)` |

## Build Requirements

- Windows 10 (1903+) or Windows 11
- Visual Studio 2019/2022 with MSVC C++17
- Link: `d3d11.lib`, `dxgi.lib`, `dwmapi.lib`
- Include path must resolve to `imgui.h` and `overlay_manager.h`

## Quick Start

```cpp
// In main.cpp:
Manager::Get().Init(hwnd, ImVec2(0,0), ImVec2(800,600));
Manager::Get().PushToast("Hello", "ImOverlay-DX11!", 4.0f);
```
