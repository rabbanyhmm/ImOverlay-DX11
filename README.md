# ImOverlay-DX11

Direct3D 11 desktop overlay and multi-window framework for Dear ImGui on Windows.

Provides hardware-accelerated floating overlays with DWM Acrylic/Mica blur, magnetic edge snapping, thread-safe toast notifications, global hotkeys, and OBS-invisible capture exclusion. Designed as a lightweight drop-in (`overlay_manager.h` and `overlay_manager.cpp`).

---

## Features

| Feature | Description | Requirement |
|---|---|---|
| **Acrylic / Mica Blur** | Native DWM hardware blur or Windows 11 Mica/Acrylic backdrop | Windows 10 2004+ |
| **Magnetic Snapping** | Snaps to screen edges and adjacent windows within a configurable threshold | Windows 10+ |
| **Toast Notifications** | Thread-safe, stacking, auto-dismissing notifications | Windows 10+ |
| **Per-Window ImGui Context** | Dedicated `ImGuiContext` per secondary window for interactive widgets | Windows 10+ |
| **Global Hotkeys** | Register hotkeys from any thread via a background message loop | Windows 10+ |
| **Streamer Mode** | Per-window `WDA_EXCLUDEFROMCAPTURE` to hide overlays from OBS, Discord, and screen capture | Windows 10 2004+ |
| **Multi-Window Hierarchy** | Parent/child grouping, cascade close/hide, and drag-follow | Windows 10+ |
| **Click-Through** | Per-window `WS_EX_TRANSPARENT` toggle | Windows 10+ |
| **Multi-Monitor Aware** | Correct DPI and monitor work-area positioning across multi-head setups | Windows 10+ |

---

## Repository Structure

```
ImOverlay-DX11/
├── overlay_manager.h          # Public API, types, enums, Manager, and Window class
├── overlay_manager.cpp        # Core implementation
├── CMakeLists.txt             # Static library target
├── LICENSE                    # MIT License
├── README.md
└── examples/
    └── minimal_demo/
        ├── main.cpp           # Complete standalone demo (~150 lines)
        └── README.md
```

---

## Quick Start

```cpp
#include "overlay_manager.h"
using namespace ImOverlay;

// After initializing D3D11 and ImGui:
Manager::Get().Init(hwnd, ImVec2(0, 0), ImVec2(800, 600));
Manager::Get().SetD3DObjects(swapChain, device, &rtv);
Manager::Get().SetDXGIFactory(factory);

// Create a floating overlay with Acrylic blur and magnetic snapping
Config cfg;
cfg.window_title        = "Overlay Panel";
cfg.size                = ImVec2(300, 140);
cfg.anchor              = AnchorMode::Screen_BottomRight;
cfg.enable_acrylic_blur = true;
cfg.acrylic_type        = AcrylicType::Acrylic;
cfg.enable_snap         = true;

Manager::Get().CreateFloatingOverlay("panel_id", cfg, [](Window* win, float dt) {
    ImGui::Text("Overlay content here");
});

// Push a toast from any thread
Manager::Get().PushToast("System", "Initialized successfully", 4.0f);

// Register a global hotkey
Manager::Get().StartHotkeyListener();
Manager::Get().RegisterHotkey(1, 0, VK_INSERT, HotkeyAction::ToggleVisibility);

// In your render loop:
Manager::Get().BeginFrame();
// ... your main UI rendering ...
Manager::Get().EndFrame(deltaTime);
```

---

## Feature Details

### 1. Acrylic and Mica DWM Blur

Hardware blur behind overlay windows via Desktop Window Manager (DWM).

| Type | Effect | OS Support |
|---|---|---|
| `AcrylicType::Blur` | Standard DWM blur behind | Windows 10 2004+ |
| `AcrylicType::Acrylic` | Frosted acrylic backdrop | Windows 11 22H2+ (fallback on Win10) |
| `AcrylicType::Mica` | Material Mica using desktop wallpaper | Windows 11 22H2+ |
| `AcrylicType::MicaAlt` | Tabbed Mica variant | Windows 11 22H2+ |

```cpp
// Set at creation:
Config cfg;
cfg.enable_acrylic_blur  = true;
cfg.acrylic_type         = AcrylicType::Mica;
cfg.draw_default_card_bg = false; // allow DWM backdrop to show through

// Toggle at runtime:
win->SetAcrylicBlur(true, AcrylicType::Acrylic);
win->SetAcrylicBlur(false);
```

> The window clear color must be transparent (`0, 0, 0, 0`) and `WS_EX_LAYERED` enabled. ImOverlay configures both automatically.

---

### 2. Magnetic Window Snapping

Windows snap to display borders and adjacent overlays when dragged within `snap_threshold` pixels.

```cpp
Config cfg;
cfg.enable_snap    = true;
cfg.snap_threshold = 18.0f;

// Runtime adjustments:
win->SetSnapEnabled(true);
win->SetSnapThreshold(24.0f);

if (win->IsSnapped()) {
    SnapEdge edge = win->GetSnapEdge();
    // SnapEdge::Left, Right, Top, Bottom, or Corners
}
```

---

### 3. Toast Notifications

Thread-safe toast queue. Notifications animate into view, stack vertically, and auto-dismiss after their duration expires.

```cpp
// Call from any thread:
Manager::Get().PushToast("Download", "Update ready to install.", 5.0f);
Manager::Get().PushToast("Alert", "High resource usage.", 4.0f, IM_COL32(255, 160, 0, 255));

// Manual dismissal:
Manager::Get().DismissToast("Download");
Manager::Get().DismissAllToasts();

size_t count = Manager::Get().GetToastCount();
```

---

### 4. Per-Window ImGui Context

Secondary windows can optionally own an isolated `ImGuiContext` while sharing the main font atlas, allowing full ImGui widget interaction without interfering with the primary viewport.

```cpp
Config cfg;
cfg.enable_imgui_context = true;

Manager::Get().CreateFloatingOverlay("settings", cfg, [](Window* win, float dt) {
    static float val = 0.5f;
    ImGui::SliderFloat("Scale", &val, 0.1f, 2.0f);
    ImGui::ColorEdit3("Accent", ...);
});
```

---

### 5. Global Hotkeys

Background message-only HWND thread for global hotkeys without polling `GetAsyncKeyState`.

```cpp
Manager::Get().StartHotkeyListener();

Manager::Get().RegisterHotkey(1, 0,        VK_INSERT, HotkeyAction::ToggleVisibility);
Manager::Get().RegisterHotkey(2, MOD_CTRL, VK_F12,    HotkeyAction::ToggleClickThrough);
Manager::Get().RegisterHotkey(3, MOD_ALT,  VK_F1,     HotkeyAction::ToggleCapture);
Manager::Get().RegisterHotkey(4, 0,        VK_F9,     HotkeyAction::CollapseAll);
Manager::Get().RegisterHotkey(5, 0,        VK_F10,    HotkeyAction::RestoreAll);

// Custom callback:
Manager::Get().RegisterHotkey(6, MOD_CONTROL, 'R', HotkeyAction::Custom, []() {
    Manager::Get().PushToast("Hotkey", "Action executed", 2.0f);
});

Manager::Get().UnregisterHotkey(1);
Manager::Get().StopHotkeyListener();
```

---

### 6. Streamer Mode (Anti-Capture)

Hides overlays from capture software (OBS Studio, Discord screen share, Windows game bar, print screen) using `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)`. The window remains fully visible to the user on screen.

```cpp
// On overlay creation:
Config cfg;
cfg.exclude_from_capture = true;

// Runtime toggle:
win->SetCaptureHidden(true);
bool is_hidden = win->IsCaptureHidden();

// Toggle across all managed overlays:
Manager::Get().SetCaptureHiddenAll(true);

// Optional background monitor to enforce affinity against external interference:
Manager::Get().StartCaptureMonitor(1000); // 1-second check interval
Manager::Get().StopCaptureMonitor();
```

---

### 7. Window Hierarchy & Groups

Group windows so that children follow position and visibility changes of the parent.

```cpp
Config parent_cfg;
parent_cfg.size = ImVec2(400, 300);
Window* parent = Manager::Get().CreateFloatingOverlay("main_panel", parent_cfg, ...);

Config child_cfg;
child_cfg.parent_id              = "main_panel";
child_cfg.anchor                 = AnchorMode::RelativeToParentWindow;
child_cfg.offset_from_parent     = ImVec2(410.0f, 0.0f);
child_cfg.close_with_parent      = true;
child_cfg.follow_parent_movement = true;

Manager::Get().CreateSubWindow("main_panel", "side_panel", child_cfg, ...);
```

---

## Build and Integration

### Drop-in (Recommended)

Add `overlay_manager.h` and `overlay_manager.cpp` to your project and link the required Windows system libraries:
- `d3d11.lib`
- `dxgi.lib`
- `dwmapi.lib`
- `winmm.lib`

### CMake

```cmake
add_subdirectory(ImOverlay-DX11)
target_link_libraries(your_project PRIVATE ImOverlay_DX11)
```

### Requirements

- Windows 10 (Build 1903+) or Windows 11
- C++17 compiler (MSVC 2019/2022 recommended)
- Dear ImGui (v1.89+ recommended, requires `imgui_internal.h`)
- ImGui Win32 and DX11 backends (`imgui_impl_win32.h`, `imgui_impl_dx11.h`)

---

## License

MIT License. See [LICENSE](LICENSE) for details.
