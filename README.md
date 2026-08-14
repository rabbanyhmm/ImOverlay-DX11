<div align="center">

# 🎯 ImOverlay-DX11

**A professional, hardware-accelerated Desktop Overlay & Multi-Window Framework**
**built on Dear ImGui + Direct3D 11 for Windows**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Windows](https://img.shields.io/badge/Platform-Windows%2010%2B-lightblue.svg)](https://microsoft.com/windows)
[![DirectX 11](https://img.shields.io/badge/DirectX-11-green.svg)](https://docs.microsoft.com/en-us/windows/win32/direct3d11/atoc-dx-graphics-direct3d-11)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-orange.svg)](https://en.cppreference.com/w/cpp/17)
[![GitHub](https://img.shields.io/badge/Repo-GitHub-black.svg)](https://github.com/rabbanyhmm/ImOverlay-DX11)

> **Create beautiful, hardware-accelerated overlay windows with Acrylic blur, magnetic snapping, stacking toasts, global hotkeys, and OBS-invisible Streamer Mode — in a single header+source pair.**

</div>

---

## ✨ Feature Matrix

| Feature | Description | Min Windows |
|---------|-------------|-------------|
| 🎨 **Acrylic / Mica Blur** | Native DWM hardware blur or Win11 Mica/Acrylic backdrop | Win10 2004+ |
| 🧲 **Magnetic Edge Snapping** | Windows snap to screen edges and to each other | Win10+ |
| 🍞 **Multi-Toast Queue** | Stacking, auto-dismissing, thread-safe toast notifications | Win10+ |
| 🕹️ **Full ImGui Context / Window** | Per-window ImGuiContext for interactive controls | Win10+ |
| ⌨️ **Global Hotkeys** | Register hotkeys from any thread; background message loop | Win10+ |
| 🛡️ **Streamer Mode (Anti-Capture)** | Per-window `WDA_EXCLUDEFROMCAPTURE` — invisible to OBS/Discord | Win10 2004+ |
| 📌 **Taskbar & Alt+Tab Stealth** | Per-window and bulk taskbar/Alt+Tab hide | Win10+ |
| 🪟 **Multi-Window Hierarchy** | Parent/child cascade, follow-on-drag, close/hide with parent | Win10+ |
| 🎯 **HWND_TOPMOST** | Always-on-top overlay windows | Win10+ |
| 🖱️ **Click-Through** | `WS_EX_TRANSPARENT` per-window toggle | Win10+ |
| 🔁 **Smooth Transitions** | Exponential window resize interpolation | Win10+ |
| 📐 **Multi-Monitor Aware** | Correct anchor placement on any monitor layout | Win10+ |

---

## 📁 File Structure

```
overlay_framework/
├── overlay_manager.h          # Single header — all types, enums, Manager, Window
├── overlay_manager.cpp        # Full implementation (~2000 lines)
├── CMakeLists.txt             # CMake static library target
├── LICENSE                    # MIT License (2026 rabbanyhmm)
├── README.md                  # This file
└── examples/
    └── minimal_demo/
        ├── main.cpp           # ~100 line complete working demo
        └── README.md
```

---

## 🚀 Quick Start

```cpp
#include "overlay_manager.h"
using namespace ImOverlay;

// In WinMain after D3D11 + ImGui init:
Manager::Get().Init(hwnd, ImVec2(0, 0), ImVec2(800, 600));
Manager::Get().SetD3DObjects(swapChain, device, &rtv);
Manager::Get().SetDXGIFactory(factory);

// Create a floating window with Acrylic blur + magnetic snapping
Config cfg;
cfg.window_title        = "My Overlay Tool";
cfg.size                = ImVec2(300, 140);
cfg.anchor              = AnchorMode::Screen_BottomRight;
cfg.enable_acrylic_blur = true;
cfg.acrylic_type        = AcrylicType::Acrylic;
cfg.enable_snap         = true;

Manager::Get().CreateFloatingOverlay("my_tool", cfg, [](Window* win, float dt) {
    ImGui::Text("Hello from ImOverlay-DX11!");
});

// Push a toast from any thread
Manager::Get().PushToast("Update", "Initialization complete!", 4.0f);

// Register a global hotkey
Manager::Get().StartHotkeyListener();
Manager::Get().RegisterHotkey(1, 0, VK_INSERT, HotkeyAction::ToggleVisibility);

// In render loop:
Manager::Get().BeginFrame();
// ... your ImGui rendering ...
Manager::Get().EndFrame(deltaTime);
```

---

## 🎨 Feature 1 — Acrylic / Mica DWM Blur

Real hardware-accelerated blur effects behind overlay windows.

| AcrylicType | Effect | Min OS |
|-------------|--------|--------|
| `Blur` | Standard DWM blur behind | Win10 2004+ |
| `Acrylic` | Frosted glass Acrylic backdrop | Win11 22H2+ (Win10 fallback) |
| `Mica` | Material Mica (integrates desktop wallpaper) | Win11 22H2+ |
| `MicaAlt` | Mica Alt / tabbed variant | Win11 22H2+ |

```cpp
// At creation time:
Config cfg;
cfg.enable_acrylic_blur = true;
cfg.acrylic_type        = AcrylicType::Mica;     // or Acrylic, Blur, MicaAlt
cfg.draw_default_card_bg = false;                 // Let DWM show through instead of card bg

// At runtime:
win->SetAcrylicBlur(true, AcrylicType::Acrylic);
win->SetAcrylicBlur(false);                       // Remove blur
```

> **Note:** For blur to be visible, your window background must be transparent (clear color `0,0,0,0`) and `WS_EX_LAYERED` must be set — ImOverlay-DX11 handles both automatically.

---

## 🧲 Feature 2 — Magnetic Window Snapping

Floating windows snap to screen edges and to each other when dragged within `snap_threshold` pixels.

```cpp
Config cfg;
cfg.enable_snap     = true;   // Default: true
cfg.snap_threshold  = 18.0f;  // Activation distance in pixels (default: 18)

// Runtime control:
win->SetSnapEnabled(true);
win->SetSnapThreshold(24.0f);

// Query snap state:
if (win->IsSnapped())
{
    SnapEdge edge = win->GetSnapEdge();
    // SnapEdge::Left, Right, Top, Bottom
    // SnapEdge::Corner_TopLeft/TopRight/BottomLeft/BottomRight
}
```

---

## 🍞 Feature 3 — Multi-Toast Queue & Stacking Engine

Thread-safe stacking toast notifications. Toasts slide in from the bottom-right, stack upward, and auto-dismiss with a progress bar.

```cpp
// From any thread (main or background):
Manager::Get().PushToast("Download Complete", "v2.1.0 is ready to install.", 5.0f);
Manager::Get().PushToast("Warning", "CPU usage is high.", 4.0f,
                         IM_COL32(255, 165, 0, 255)); // Orange accent

// Dismiss programmatically:
Manager::Get().DismissToast("Download Complete");
Manager::Get().DismissAllToasts();

// Query count:
size_t count = Manager::Get().GetToastCount();
```

**Toast caps:** Max 5 stacked toasts by default (`k_max_toasts`). Oldest is evicted when new ones arrive over the cap.

---

## 🕹️ Feature 4 — Per-Window ImGui Context

Enable full interactive Dear ImGui rendering (sliders, inputs, tables, color pickers) inside secondary windows.

```cpp
Config cfg;
cfg.enable_imgui_context = true;   // Give this window its own ImGuiContext

Manager::Get().CreateFloatingOverlay("settings", cfg, [](Window* win, float dt) {
    // Full ImGui controls work here because this window has its own context
    static float val = 0.5f;
    ImGui::SliderFloat("Volume", &val, 0.f, 1.f);
    ImGui::ColorEdit3("Theme", ...);
    ImGui::InputText("Search", ...);
});
```

> **Note:** Font atlas is shared with the main context. Each per-context window gets its own `ImGui_ImplDX11` and `ImGui_ImplWin32` backend state.

---

## ⌨️ Feature 5 — Global Hotkeys

Built-in global hotkey listener using a background message-only HWND thread. Thread-safe registration from any thread.

```cpp
// Start the listener (spawns background thread once)
Manager::Get().StartHotkeyListener();

// Built-in actions:
Manager::Get().RegisterHotkey(1, 0,        VK_INSERT, HotkeyAction::ToggleVisibility);
Manager::Get().RegisterHotkey(2, MOD_CTRL, VK_F12,    HotkeyAction::ToggleClickThrough);
Manager::Get().RegisterHotkey(3, MOD_ALT,  VK_F1,     HotkeyAction::ToggleCapture);
Manager::Get().RegisterHotkey(4, 0,        VK_F9,     HotkeyAction::CollapseAll);
Manager::Get().RegisterHotkey(5, 0,        VK_F10,    HotkeyAction::RestoreAll);

// Custom callback:
Manager::Get().RegisterHotkey(6, MOD_CTRL, 'R', HotkeyAction::Custom, []() {
    // your custom action here
    Manager::Get().PushToast("Hotkey", "Custom action triggered!", 3.f);
});

// Unregister:
Manager::Get().UnregisterHotkey(1);
Manager::Get().UnregisterAllHotkeys();
Manager::Get().StopHotkeyListener();
```

| HotkeyAction | Effect |
|--------------|--------|
| `ToggleVisibility` | Show/hide all floating overlays |
| `ToggleClickThrough` | Toggle click-through on all windows |
| `ToggleCapture` | Toggle Streamer Mode (OBS invisible) |
| `CollapseAll` | Minimize all floating windows |
| `RestoreAll` | Restore all floating windows |
| `Custom` | Your own `std::function<void()>` callback |

---

## 🛡️ Feature 6 — Streamer Mode (Anti-Capture / OBS Invisible)

Hides individual windows from OBS Studio, Discord screen share, Xbox Game Bar, and screenshots — **while remaining 100% visible on the physical display.**

```cpp
// Per-window at creation:
Config cfg;
cfg.exclude_from_capture = true;

// Per-window runtime toggle:
win->SetCaptureHidden(true);
win->SetCaptureHidden(false);
bool hidden = win->IsCaptureHidden();

// By window ID:
Manager::Get().SetCaptureHidden("my_tool", true);
Manager::Get().SetCaptureHidden("stream_chat", false);
bool hidden = Manager::Get().IsCaptureHidden("my_tool");

// Main menu window:
Manager::Get().SetMainCaptureHidden(true);

// All windows at once:
Manager::Get().SetCaptureHiddenAll(true);

// Background monitor (keeps protection alive even if another process tries to disable it):
Manager::Get().StartCaptureMonitor(1000); // poll every 1000ms
Manager::Get().StopCaptureMonitor();
```

> **Requires:** Windows 10 Version 2004 (Build 19041+) for `WDA_EXCLUDEFROMCAPTURE`.

---

## 📐 Window Hierarchy & Cascade Behavior

```cpp
// Create a main floating window
Config parent_cfg;
parent_cfg.size = ImVec2(400, 300);
Window* parent = Manager::Get().CreateFloatingOverlay("main_panel", parent_cfg, ...);

// Create a child that follows the parent
Config child_cfg;
child_cfg.parent_id            = "main_panel";
child_cfg.anchor               = AnchorMode::RelativeToParentWindow;
child_cfg.offset_from_parent   = ImVec2(410.f, 0.f); // Right of parent
child_cfg.close_with_parent    = true;
child_cfg.follow_parent_movement = true;
Manager::Get().CreateSubWindow("main_panel", "side_panel", child_cfg, ...);
```

---

## 🛠️ Build & Integration

### Option A: Copy 2 Files
Drop `overlay_manager.h` and `overlay_manager.cpp` directly into your project. Link: `d3d11.lib`, `dxgi.lib`, `dwmapi.lib`.

### Option B: CMake
```cmake
add_subdirectory(overlay_framework)
target_link_libraries(your_app PRIVATE ImOverlay_DX11)
```

### Requirements
- Windows 10 (1903+) or Windows 11
- MSVC 2019/2022, C++17
- Dear ImGui (any recent version with `imgui_internal.h`)
- `backends/imgui_impl_dx11.h`, `backends/imgui_impl_win32.h`

---

## 📦 Examples

See [`examples/minimal_demo/`](examples/minimal_demo/) for a complete ~100-line standalone demo using all major features.

---

## 📜 License

MIT License — © 2026 [rabbanyhmm](https://github.com/rabbanyhmm)
