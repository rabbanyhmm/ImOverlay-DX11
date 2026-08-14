<div align="center">

# 🪟 ImOverlay-DX11

**A modern, lightweight 2-file transparent desktop overlay & multi-window framework for Win32 and DirectX 11.**

[![GitHub](https://img.shields.io/badge/GitHub-rabbanyhmm%2FImOverlay--DX11-blue?logo=github)](https://github.com/rabbanyhmm/ImOverlay-DX11.git)
[![C++20](https://img.shields.io/badge/Language-C%2B%2B20-00599C?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/20)
[![DirectX 11](https://img.shields.io/badge/Graphics-DirectX%2011-green?logo=directx)](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-graphics)
[![Dear ImGui](https://img.shields.io/badge/UI-Dear%20ImGui-orange)](https://github.com/ocornut/imgui)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D6?logo=windows)](https://microsoft.com)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

<p align="center">
  <a href="#-key-features">Key Features</a> •
  <a href="#-quick-start">Quick Start</a> •
  <a href="#-multi-window--hierarchy-system">Multi-Window & Hierarchy</a> •
  <a href="#-api-reference">API Reference</a> •
  <a href="#-architecture">Architecture</a> •
  <a href="#-license">License</a>
</p>

</div>

---

## 📖 Overview

**ImOverlay-DX11** is a high-performance C++20 framework for building **hardware-accelerated, transparent desktop overlays and multi-window systems** using Win32 and DirectX 11 with Dear ImGui.

Everything is packed into **two clean, standalone files** (`overlay_manager.h` and `overlay_manager.cpp`), allowing you to drop it into any project in seconds.

---

## ✨ Key Features

| Feature | Description |
| :--- | :--- |
| 🏎️ **Dynamic Physical Auto-Expansion** | Automatically expands the physical Win32 window (`SetWindowPos`) when dropdowns, toasts, or menus extend beyond the main bounds, and smoothly shrinks back on completion. |
| 🎯 **Smart Click-Through** | Transparent background margins allow mouse clicks to pass straight through (`HTTRANSPARENT`) to underlying games, desktop icons, and browser windows. |
| 🖱️ **Zero-Lag Native Window Dragging** | Native OS dragging (`HTCAPTION`) on empty backgrounds and headers with a high-resolution modal render pump for **continuous 60 FPS** without freezing animations. |
| 👨‍👧 **Parent-Child Sub-Window Hierarchy** | Attach sub-toolbars and child overlays with automatic cascading **Close, Destroy, Minimize, Restore, Show, and Hide**. |
| 🔗 **Real-Time Follow Drag** | Sub-windows anchored to a parent window smoothly follow the parent's movement in real time (`OnParentMoved`). |
| 🏝️ **Independent Multi-Window System** | Spawn unlimited secondary windows with dedicated DirectX 11 swapchains, custom ImGui render callbacks, and independent `HWND_TOPMOST` z-orders. |
| 🖥️ **Multi-Monitor Display Snapping** | Automatically detects active display monitor work areas (`MonitorFromWindow` / `rcWork`) across multi-screen gaming setups. |
| ⏱️ **Auto-Dismiss On Finish** | Progress windows can remain visible for a configurable delay (e.g. 2.0s) after reaching 100% before smoothly closing. |
| 🔒 **Antivirus Hardened** | WIC image loading, Control Flow Guard (`/guard:cf`), ASLR, and DEP enabled (0 detections on VirusTotal). |
| 📦 **2-File Drop-In Architecture** | Zero complex dependencies—just drop `overlay_manager.h` and `overlay_manager.cpp` into your project! |

---

## 📦 Project Structure

```
ImOverlay-DX11/
├── overlay_manager.h      # Single unified header (Config, Hierarchy, FloatingOverlayWindow, OverlayManager)
├── overlay_manager.cpp    # Single unified implementation (Win32, DX11 Swapchains, Modal Drag Pump)
└── README.md              # Full documentation & examples
```

---

## 🚀 Quick Start

### 1. Clone the Repository

```bash
git clone https://github.com/rabbanyhmm/ImOverlay-DX11.git
```

### 2. Add to Your Project

Include the header wherever needed:

```cpp
#include "overlay_manager.h"
```

### 3. Initialization in `WinMain`

```cpp
// In your application startup:
OverlayManager::Get().Init(hwnd, initial_screen_pos, main_menu_size);
OverlayManager::Get().SetPadding(26.f, 26.f, 26.f, 26.f); // Transparent padding
OverlayManager::Get().SetTransitionMode(TransitionMode::Smooth, 14.0f);
OverlayManager::Get().SetTopmost(false); // Main menu normal Z-order

// Bind Direct3D swapchain:
OverlayManager::Get().SetD3DObjects(g_pSwapChain, g_pd3dDevice, &g_mainRenderTargetView);
OverlayManager::Get().SetDXGIFactory(g_pDXGIFactory);
```

### 4. Message Loop (`WndProc`)

Add smooth modal drag rendering and hit-testing in your `WndProc`:

```cpp
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_NCHITTEST:
        return OverlayManager::Get().HandleHitTest(lParam);

    case WM_ENTERSIZEMOVE:
        ::SetTimer(hWnd, 1001, 15, nullptr); // ~60 FPS modal drag timer
        return 0;

    case WM_TIMER:
        if (wParam == 1001)
        {
            RenderAppFrame();
            return 0;
        }
        break;

    case WM_MOVING:
    case WM_MOVE:
        RenderAppFrame();
        return 0;

    case WM_EXITSIZEMOVE:
        ::KillTimer(hWnd, 1001);
        return 0;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            OverlayManager::Get().MinimizeAllFloatingOverlays();
        else if (wParam == SIZE_RESTORED)
            OverlayManager::Get().RestoreAllFloatingOverlays();
        return 0;

    case WM_DESTROY:
        OverlayManager::Get().CloseAllFloatingOverlays();
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
```

### 5. Main Render Loop

```cpp
// Inside your render frame:
OverlayManager::Get().BeginFrame();

// Draw your main ImGui menu
menu->draw();

// Update auto-expansion and process all secondary windows
OverlayManager::Get().EndFrame(io.DeltaTime);
```

---

## 👨‍👧 Multi-Window & Hierarchy System

### 1. Creating Secondary Floating Windows

```cpp
OverlayConfig config;
config.window_title = "Secondary Tools Menu";
config.size = ImVec2(400.f, 300.f);
config.anchor = AnchorMode::Screen_BottomRight;
config.is_topmost = true;             // Stay on top of fullscreen games!
config.hide_from_taskbar = false;     // Show in Windows Taskbar
config.is_movable = true;             // User can drag it across monitors
config.duration_seconds = -1.0f;      // Permanent standalone window

auto* sec_window = OverlayManager::Get().CreateFloatingOverlay(
    "secondary_tools",
    config,
    [](FloatingOverlayWindow* win, float delta_time) {
        // Render any custom ImGui widgets or draw lists here!
    }
);
```

### 2. Creating Child Sub-Windows Attached to a Parent

```cpp
OverlayConfig sub_cfg;
sub_cfg.window_title = "Color Picker Sub-Window";
sub_cfg.size = ImVec2(240.f, 280.f);
sub_cfg.close_with_parent = true;      // Auto-close when parent closes!
sub_cfg.hide_with_parent = true;       // Auto-hide when parent hides!
sub_cfg.minimize_with_parent = true;   // Auto-minimize when parent minimizes!
sub_cfg.follow_parent_movement = true; // Auto-follow when parent is dragged!

auto* sub_win = OverlayManager::Get().CreateSubWindow(
    "secondary_tools",  // Parent ID
    "color_picker",     // Child ID
    sub_cfg,
    [](FloatingOverlayWindow* win, float delta_time) {
        // Child UI
    }
);
```

### 3. Displaying Detached Progress Cards (with Auto-Dismiss Delay)

```cpp
OverlayConfig cfg;
cfg.anchor = AnchorMode::Screen_BottomRight;
cfg.is_topmost = true;
cfg.auto_dismiss_on_finish = true;
cfg.finish_dismiss_delay = 2.0f; // Stays on screen 2.0s at 100% before auto-closing

OverlayManager::Get().ShowDetachedProgress(
    "Downloading Update",
    ICON_FA_DOWNLOAD,
    download_progress, // 0.0f to 1.0f
    cfg
);
```

---

## 🎛️ API Reference

### Window Lifecycle & State (`FloatingOverlayWindow`)

```cpp
auto* win = OverlayManager::Get().GetFloatingOverlay("my_window");
if (win)
{
    // Visibility
    win->Show();
    win->Hide();
    win->SetVisible(true);

    // Minimize / Maximize / Restore
    win->Minimize();
    win->Maximize();
    win->Restore();
    bool is_min = win->IsMinimized();
    bool is_max = win->IsMaximized();

    // Modifiers
    win->SetWindowTitle("Custom Window Title");
    win->SetTopmost(true);
    win->SetTaskbarVisible(true);
    win->SetClickThrough(false);
    win->SetMovable(true);
    win->SetOpacity(0.95f);
    win->SetPosition(100, 100);
    win->SetSize(500, 400);
    win->SetCornerRadius(16.0f);
    win->SetCustomColors(IM_COL32(20, 20, 20, 240), IM_COL32(255, 255, 255, 30));

    // Event Callbacks
    win->SetOnCloseCallback([](FloatingOverlayWindow* w) { /* On Close */ });
    win->SetOnMoveCallback([](FloatingOverlayWindow* w, int x, int y) { /* On Move */ });
    win->SetOnResizeCallback([](FloatingOverlayWindow* w, int w, int h) { /* On Resize */ });

    // Closing
    win->Close();       // Smooth animated fade out (cascades to children)
    win->DestroyNow();  // Instant destroy (cascades to children)
}
```

### Batch Operations (`OverlayManager`)

```cpp
OverlayManager::Get().ShowAllFloatingOverlays();
OverlayManager::Get().HideAllFloatingOverlays();
OverlayManager::Get().MinimizeAllFloatingOverlays();
OverlayManager::Get().RestoreAllFloatingOverlays();
OverlayManager::Get().CloseAllFloatingOverlays();

// Queries
std::vector<std::string> ids = OverlayManager::Get().GetFloatingOverlayIds();
std::vector<std::string> children = OverlayManager::Get().GetChildrenOf("parent_id");
size_t count = OverlayManager::Get().GetFloatingOverlayCount();
```

---

## 📐 Architecture

```
                                  ┌────────────────────────┐
                                  │     OverlayManager     │
                                  │      (Singleton)       │
                                  └───────────┬────────────┘
                                              │
                     ┌────────────────────────┴────────────────────────┐
                     ▼                                                 ▼
        ┌─────────────────────────┐                       ┌─────────────────────────┐
        │       Main Menu         │                       │  FloatingOverlayWindow  │
        │   (Dynamic Expansion)   │                       │  (Multi-Window Pool)    │
        └────────────┬────────────┘                       └────────────┬────────────┘
                     │                                                 │
          ┌──────────┴──────────┐                            ┌─────────┴─────────┐
          ▼                     ▼                            ▼                   ▼
    Smart Click-Thru     60 FPS Modal Drag            Parent Window        Child Sub-Window
     (HTTRANSPARENT)        (HTCAPTION)               (Independent Z)     (Cascading Events)
```

---

## 🛠️ Building & Requirements

- **Operating System:** Windows 10 / 11 (64-bit)
- **Compiler:** MSVC with C++20 support (Visual Studio 2022 recommended)
- **DirectX:** DirectX 11 SDK (`d3d11.lib`, `dxgi.lib`, `d3dcompiler.lib`, `dwmapi.lib`)
- **UI:** [Dear ImGui](https://github.com/ocornut/imgui)

---

## 📄 License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

---

<div align="center">
  <b>Developed with ❤️ for high-performance desktop tools & game overlays.</b>
</div>
