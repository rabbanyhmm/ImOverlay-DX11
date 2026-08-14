# Changelog

All notable changes to **ImOverlay-DX11** are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project adheres to [Semantic Versioning](https://semver.org/).

---

## [1.0.0] — 2026-08-14

### 🎉 Initial Release

#### Added
- **2-file library architecture** (`overlay_manager.h` + `overlay_manager.cpp`)
- **`namespace ImOverlay`** — all types scoped cleanly (`ImOverlay::Manager`, `ImOverlay::Window`, `ImOverlay::Config`, etc.)
- **Backward-compatible type aliases** — `OverlayManager`, `FloatingOverlayWindow`, `OverlayConfig` for drop-in migration
- **MSVC auto-linking pragmas** (`d3d11.lib`, `dxgi.lib`, `d3dcompiler.lib`, `dwmapi.lib`, `uxtheme.lib`)
- **CMakeLists.txt** — static library target `ImOverlay_DX11`
- **MIT LICENSE** — 2026 rabbanyhmm
- **Version macros** — `IMOVERLAY_VERSION`, `IMOVERLAY_VERSION_MAJOR/MINOR/PATCH`
- **ASCII art credit banner** on all source files

#### Core Window System
- `Manager` singleton with `Init()`, `SetD3DObjects()`, `SetDXGIFactory()`
- `Window` class with per-window D3D11 swapchain, `WS_EX_LAYERED` transparency
- `Config` struct — comprehensive per-window configuration
- `AnchorMode` — `Screen_BottomRight/TopLeft/TopRight/BottomLeft/Center/Absolute/Relative/RelativeToParentWindow`
- `TransitionMode` — `Smooth` (exponential interpolation) / `Instant`
- Parent/child window hierarchy with cascade close/hide/minimize/follow

#### Feature 1 — Acrylic / Mica DWM Blur
- `AcrylicType` enum: `None`, `Blur`, `Acrylic`, `Mica`, `MicaAlt`
- Windows 11 22H2+: `DWMWA_SYSTEMBACKDROP_TYPE`
- Windows 10 fallback: `SetWindowCompositionAttribute` with `ACCENT_ENABLE_ACRYLICBLURBEHIND`
- `Config::enable_acrylic_blur`, `Config::acrylic_type`
- `Window::SetAcrylicBlur(bool, AcrylicType)` runtime toggle

#### Feature 2 — Magnetic Window Snapping
- `SnapEdge` enum: `Left`, `Right`, `Top`, `Bottom`, `Corner_TopLeft/TopRight/BottomLeft/BottomRight`
- `Config::enable_snap`, `Config::snap_threshold`
- Snaps to screen edges, corners, and edges of other floating windows during drag
- `Window::IsSnapped()`, `Window::GetSnapEdge()`

#### Feature 3 — Multi-Toast Queue & Stacking Engine
- `Manager::PushToast(title, message, duration, accent, anchor)` — thread-safe from any thread
- Toasts stack from bottom-right, slide in/out with animation, per-toast accent color, progress bar
- `Manager::DismissToast()`, `DismissAllToasts()`, `GetToastCount()`
- Max 5 stacked toasts (configurable via `k_max_toasts`)
- `[[deprecated]] ShowDetachedToast()` — redirects to `PushToast()`

#### Feature 4 — Per-Window ImGui Context
- `Config::enable_imgui_context = true` — spawns own `ImGuiContext*` per window
- Shared font atlas with main context
- Full interactive ImGui controls (sliders, inputs, tables, color pickers) inside secondary windows

#### Feature 5 — Global Hotkey Listener
- `HotkeyAction` enum: `ToggleVisibility`, `ToggleClickThrough`, `ToggleCapture`, `CollapseAll`, `RestoreAll`, `Custom`
- Background message-only `HWND` thread — no main loop blocking
- `Manager::StartHotkeyListener()`, `RegisterHotkey()`, `UnregisterHotkey()`, `StopHotkeyListener()`
- Thread-safe hotkey registration from any thread

#### Feature 6 — Streamer Mode / Anti-Capture (OBS Invisible)
- `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` — invisible to OBS, Discord, Xbox Game Bar
- Per-window: `Config::exclude_from_capture`, `Window::SetCaptureHidden(bool)`, `Window::IsCaptureHidden()`
- By ID: `Manager::SetCaptureHidden(window_id, bool)`, `Manager::IsCaptureHidden(window_id)`
- Global: `Manager::SetCaptureHiddenAll(bool)`, `Manager::SetMainCaptureHidden(bool)`
- Background monitor: `Manager::StartCaptureMonitor()` / `StopCaptureMonitor()`

#### Feature 7 — Taskbar & Alt+Tab Stealth
- Per-window: `Window::SetTaskbarVisible(bool)` / `IsTaskbarVisible()`
- By ID: `Manager::SetTaskbarVisible(window_id, bool)`
- Global: `Manager::SetTaskbarVisibleAll(bool)`, `Manager::SetMainTaskbarVisible(bool)`

#### Feature 8 — Multi-Window Hierarchy & Cascade
- `Manager::CreateFloatingOverlay()`, `CreateSubWindow()`
- Parent/child cascade: `close_with_parent`, `hide_with_parent`, `minimize_with_parent`, `follow_parent_movement`
- `Manager::CloseWindowHierarchy()`, `GetChildrenOf()`

#### Examples
- `examples/minimal_demo/main.cpp` — ~100-line standalone complete demo

---

## [Unreleased]
- [ ] Drag resize handles on floating windows
- [ ] Custom title bar rendering per window
- [ ] DPI scaling awareness (`GetDpiForWindow`)
- [ ] SaveLayout / RestoreLayout (persist window positions to JSON/INI)
