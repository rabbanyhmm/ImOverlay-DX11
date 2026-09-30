# ImOverlay-DX11

Direct3D 11 multi-window overlay framework for Dear ImGui on Windows.

Drop `overlay_manager.h` and `overlay_manager.cpp` into your project to create hardware-accelerated floating windows with native DWM blur (Acrylic and Mica), magnetic edge snapping, toast notifications, global hotkeys, and OBS capture bypass.

## Requirements

- Windows 10 (1903+) or Windows 11
- C++17
- Dear ImGui (with Win32 and DX11 backends)
- System libraries: `d3d11.lib`, `dxgi.lib`, `dwmapi.lib`, `winmm.lib`

## Integration

### Option 1: Drop-in files
Add `overlay_manager.h` and `overlay_manager.cpp` to your source tree and link the required system libraries.

### Option 2: CMake
```cmake
add_subdirectory(ImOverlay-DX11)
target_link_libraries(your_project PRIVATE ImOverlay_DX11)
```

## Quick Start

Initialize the manager after creating your D3D11 device and ImGui context:

```cpp
#include "overlay_manager.h"
using namespace ImOverlay;

// After D3D11 and ImGui init:
Manager::Get().Init(hwnd, ImVec2(0, 0), ImVec2(800, 600));
Manager::Get().SetD3DObjects(swapChain, device, &rtv);
Manager::Get().SetDXGIFactory(factory);

// Create a floating window
Config cfg;
cfg.window_title        = "Overlay Panel";
cfg.size                = ImVec2(300, 140);
cfg.anchor              = AnchorMode::Screen_BottomRight;
cfg.enable_acrylic_blur = true;
cfg.acrylic_type        = AcrylicType::Acrylic;
cfg.enable_snap         = true;

Manager::Get().CreateFloatingOverlay("panel_id", cfg, [](Window* win, float dt) {
    ImGui::Text("Overlay content");
});

// Push a toast notification from any thread
Manager::Get().PushToast("System", "Initialized", 4.0f);

// Global hotkey (e.g. Insert toggles all overlays)
Manager::Get().StartHotkeyListener();
Manager::Get().RegisterHotkey(1, 0, VK_INSERT, HotkeyAction::ToggleVisibility);

// In your main loop:
Manager::Get().BeginFrame();
// ... render main ImGui UI ...
Manager::Get().EndFrame(deltaTime);
```

## Common Usage

### DWM Blur (Acrylic and Mica)
Supports native hardware blur behind transparent overlay windows.

```cpp
Config cfg;
cfg.enable_acrylic_blur  = true;
cfg.acrylic_type         = AcrylicType::Mica; // Blur, Acrylic, Mica, MicaAlt
cfg.draw_default_card_bg = false;             // let the blur show through

// Or toggle at runtime:
win->SetAcrylicBlur(true, AcrylicType::Acrylic);
win->SetAcrylicBlur(false);
```

Overlay windows must use a clear color with alpha 0 (`0, 0, 0, 0`). The framework handles `WS_EX_LAYERED` automatically.

### Magnetic Snapping
Windows snap to display borders and adjacent overlays when moved close to edges.

```cpp
Config cfg;
cfg.enable_snap    = true;
cfg.snap_threshold = 18.0f; // pixel distance

// Adjust at runtime
win->SetSnapEnabled(true);
win->SetSnapThreshold(24.0f);
```

### Stacking Toasts
Thread-safe notification queue. Notifications slide in, stack vertically, and auto-dismiss.

```cpp
// Safe to call from worker threads
Manager::Get().PushToast("Download", "Complete", 5.0f);
Manager::Get().PushToast("Warning", "High memory usage", 4.0f, IM_COL32(255, 160, 0, 255));

// Manual dismiss
Manager::Get().DismissToast("Download");
Manager::Get().DismissAllToasts();
```

### Streamer Mode (Anti-Capture)
Uses `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` so the window remains visible on your monitor but stays invisible in OBS Studio, Discord screen sharing, and screenshots.

```cpp
// At window creation:
Config cfg;
cfg.exclude_from_capture = true;

// Or toggle per window at runtime:
win->SetCaptureHidden(true);

// Toggle all overlays at once:
Manager::Get().SetCaptureHiddenAll(true);
```

Requires Windows 10 2004 (build 19041) or higher.

### Window Groups and Hierarchy
Attach child windows to a parent so they move and close together.

```cpp
Config child_cfg;
child_cfg.parent_id              = "main_panel";
child_cfg.anchor                 = AnchorMode::RelativeToParentWindow;
child_cfg.offset_from_parent     = ImVec2(410.0f, 0.0f);
child_cfg.close_with_parent      = true;
child_cfg.follow_parent_movement = true;

Manager::Get().CreateSubWindow("main_panel", "sub_panel", child_cfg, ...);
```

### Isolated ImGui Context
Secondary windows can run their own `ImGuiContext` while sharing the main font atlas, avoiding state conflicts with the primary window.

```cpp
Config cfg;
cfg.enable_imgui_context = true;

Manager::Get().CreateFloatingOverlay("tool_window", cfg, [](Window* win, float dt) {
    static float val = 0.5f;
    ImGui::SliderFloat("Scale", &val, 0.1f, 2.0f);
});
```

## Example
A standalone demo is available in [`examples/minimal_demo/`](examples/minimal_demo/).

## License
MIT License. See [LICENSE](LICENSE) for details.
