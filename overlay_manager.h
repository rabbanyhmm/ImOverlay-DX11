//        ___       ___              _           _____  ___ _ 
//       |_ _|_ __ / _ \__ _____ _ _| |__ _ _  _|   \ \/ / / |
//        | || '  \ (_) \ V / -_) '_| / _` | || | |) >  <| | |
//       |___|_|_|_\___/ \_/\___|_| |_\__,_|\_, |___/_/\_\_|_|
//                                    |__/              
//
//  ImOverlay-DX11: Hardware-Accelerated Desktop Overlay & Multi-Window Framework
//  version 1.0.0 (Release Build: 2026-08-14)
//  https://github.com/rabbanyhmm/ImOverlay-DX11
//
//  SPDX-FileCopyrightText: 2026 rabbanyhmm <https://github.com/rabbanyhmm>
//  SPDX-License-Identifier: MIT

/****************************************************************************\
 *                                                                          *
 *  Note on Documentation and Architecture:                                 *
 *  --------------------------------------                                  *
 *  ImOverlay-DX11 is a standalone, lightweight, multi-window overlay       *
 *  framework written for Direct3D 11 and Dear ImGui on Windows 10/11.      *
 *                                                                          *
 *  Key Capabilities:                                                       *
 *  - 2-File Architecture: Drop 'overlay_manager.h' and '.cpp' into your    *
 *    project with zero third-party dependencies beyond Win32 + ImGui.      *
 *  - Hardware-Accelerated DWM Blur: Real Acrylic and Mica frosted-glass.   *
 *  - Magnetic Window Snapping: Smart edge & inter-window auto-docking.     *
 *  - Streamer Mode (Anti-Capture): Per-window WDA_EXCLUDEFROMCAPTURE to   *
 *    stay completely invisible to OBS, Discord, and screen recording apps. *
 *  - Multi-Toast Queue: Thread-safe, stacked notifications with slide-in.  *
 *  - Global Hotkeys: Non-blocking background listener for gaming hotkeys.  *
 *  - Sub-Window Hierarchy: Cascading close/hide/minimize and drag-follow.  *
 *                                                                          *
 *  Official Repository & Issue Tracker:                                    *
 *  https://github.com/rabbanyhmm/ImOverlay-DX11.git                        *
 *                                                                          *
\****************************************************************************/

#ifndef IMOVERLAY_DX11_HPP_
#define IMOVERLAY_DX11_HPP_

#pragma once

// ============================================================================
// Version Definitions & Metadata
// ============================================================================
#define IMOVERLAY_VERSION_MAJOR 1
#define IMOVERLAY_VERSION_MINOR 0
#define IMOVERLAY_VERSION_PATCH 0
#define IMOVERLAY_VERSION_BUILD 20260814
#define IMOVERLAY_VERSION       "1.0.0"
#define IMOVERLAY_VERSION_NUMBER \
    (IMOVERLAY_VERSION_MAJOR * 10000 + IMOVERLAY_VERSION_MINOR * 100 + IMOVERLAY_VERSION_PATCH)

// ============================================================================
// Compiler & Platform Checks
// ============================================================================
#if !defined(_WIN32) && !defined(_WIN64)
    #error "ImOverlay-DX11 is only supported on Windows platforms (Windows 10 / Windows 11)."
#endif

#if defined(_MSVC_LANG) && _MSVC_LANG < 201703L
    #pragma message("Warning: ImOverlay-DX11 recommends C++17 or higher.")
#elif !defined(_MSVC_LANG) && __cplusplus < 201703L
    #pragma message("Warning: ImOverlay-DX11 recommends C++17 or higher.")
#endif

// Suppress known benign compiler warnings across toolchains
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 4201) // nonstandard extension used: nameless struct/union
    #pragma warning(disable: 4100) // unreferenced formal parameter
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

// Win32 & Direct3D 11 Headers

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include "imgui.h"
#include "imgui_internal.h"

#if defined(_MSC_VER)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#endif

#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif
#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace ImOverlay
{

// ============================================================================
// Enums & Structs
// ============================================================================

enum class TransitionMode
{
    Smooth, // Smooth exponential interpolation on window resizing
    Instant // Instantaneous window resize
};

// DWM Backdrop / Acrylic type (requires Windows 10 2004+ for Blur, Windows 11 22H2+ for Mica/Acrylic)
enum class AcrylicType
{
    None,     // No blur
    Blur,     // DWM blur behind (Win10 fallback)
    Acrylic,  // Windows 11 Acrylic backdrop (DWMSBT_TRANSIENTWINDOW)
    Mica,     // Windows 11 Mica (DWMSBT_MAINWINDOW)
    MicaAlt   // Windows 11 Mica Alt / tabbed (DWMSBT_TABBEDWINDOW)
};

// Edge/corner a window has snapped to
enum class SnapEdge
{
    None,
    Left,
    Right,
    Top,
    Bottom,
    Corner_TopLeft,
    Corner_TopRight,
    Corner_BottomLeft,
    Corner_BottomRight
};

// Actions available to global hotkeys
enum class HotkeyAction
{
    ToggleVisibility,  // Show/hide all overlays
    ToggleClickThrough,// Toggle click-through on all overlays
    ToggleCapture,     // Toggle Streamer Mode (WDA_EXCLUDEFROMCAPTURE)
    CollapseAll,       // Minimize all floating windows
    RestoreAll,        // Restore all floating windows
    Custom             // User-supplied callback
};

enum class AnchorMode
{
    Relative,               // Relative to main menu
    RelativeToParentWindow, // Relative to a specified parent window
    Screen_TopLeft,         // Fixed at screen top-left
    Screen_TopRight,        // Fixed at screen top-right
    Screen_BottomLeft,      // Fixed at screen bottom-left
    Screen_BottomRight,     // Fixed at screen bottom-right
    Screen_Center,          // Fixed at screen center
    Screen_Absolute         // Fixed at absolute screen coordinates
};

struct Element
{
    std::string name;
    ImRect rect;                // Screen-space bounding box
    bool is_interactive = true; // True if it receives clicks, false if click-through
    bool is_active = true;      // Current visibility state
};

struct Config
{
    std::string window_title = "ImOverlay Window";     // OS Window Title (shows in Taskbar/Alt+Tab)
    std::string parent_id = "main_menu";               // Parent window ID ("main_menu", overlay ID, or "" for root)
    AnchorMode anchor = AnchorMode::Screen_BottomRight;
    ImVec2 custom_pos = ImVec2(0.f, 0.f);               // Used if AnchorMode::Screen_Absolute
    ImVec2 offset_from_parent = ImVec2(0.f, 0.f);       // Used if AnchorMode::RelativeToParentWindow
    ImVec2 size = ImVec2(340.f, 80.f);                  // Window dimensions
    ImVec4 padding = ImVec4(16.f, 16.f, 16.f, 16.f);    // Transparent margin buffer

    bool is_topmost = true;             // Stay above full-screen games/apps (HWND_TOPMOST)
    bool hide_from_taskbar = true;      // Hide from taskbar and Alt+Tab (WS_EX_TOOLWINDOW)
    bool exclude_from_capture = false;  // Streamer Mode: Hide from OBS/Discord/Screenshots (WDA_EXCLUDEFROMCAPTURE)
    bool is_movable = true;             // Draggable anywhere by user (HTCAPTION)
    bool is_click_through = false;      // Visual-only click-through toggle
    bool start_hidden = false;          // Create window initially hidden
    bool start_minimized = false;       // Create window initially minimized

    // Hierarchy & Cascade Behavior
    bool close_with_parent = true;      // Auto-close when parent window closes
    bool hide_with_parent = true;       // Auto-hide when parent window hides
    bool minimize_with_parent = true;   // Auto-minimize when parent window minimizes
    bool follow_parent_movement = true; // Sub-window follows parent position when parent is dragged

    // Hit-Testing Regions
    std::vector<ImRect> clickable_regions; // Specific clickable hitboxes (e.g. buttons only)
    std::vector<ImRect> drag_regions;      // Specific drag header/caption hitboxes (empty = whole body draggable)

    // Lifetime & Timing
    float duration_seconds = -1.0f;     // -1 = permanent window, > 0 = auto-dismiss after seconds
    bool auto_dismiss_on_finish = true; // Auto-close after task/progress completes
    float finish_dismiss_delay = 2.0f;  // Delay in seconds before closing after hitting 100%
    float initial_opacity = 1.0f;       // Base opacity 0.0f - 1.0f

    // Visual Customization & Styling
    float corner_radius = 16.0f;        // Border radius
    bool draw_default_card_bg = true;   // Draw glassmorphism background rect
    ImU32 custom_bg_color = IM_COL32(18, 18, 20, 240);       // Dark glass background
    ImU32 custom_border_color = IM_COL32(255, 255, 255, 30);  // Subtle glass border
    ImU32 custom_accent_color = IM_COL32(138, 143, 255, 255); // Default accent/primary color
    ImU32 custom_text_color = IM_COL32(255, 255, 255, 255);   // White text
    ImU32 custom_track_color = IM_COL32(255, 255, 255, 12);   // Progress track
    float border_thickness = 1.0f;      // Border outline thickness

    // --- Feature 1: Acrylic / Mica DWM Blur ---
    bool enable_acrylic_blur = false;   // Apply DWM hardware blur behind this window
    AcrylicType acrylic_type = AcrylicType::Acrylic; // Blur style (requires Win10+/Win11+)

    // --- Feature 2: Magnetic Edge Snapping ---
    bool enable_snap = true;            // Enable magnetic edge snapping when dragging
    float snap_threshold = 18.0f;       // Snap activation distance in pixels

    // --- Feature 4: Per-Window ImGui Context ---
    bool enable_imgui_context = false;  // Give this window its own ImGuiContext (for full interactive ImGui UI)

    // Optional Font Pointers (if null, uses default ImGui font)
    ImFont* custom_font = nullptr;
    ImFont* custom_icon_font = nullptr;
};

// ============================================================================
// Window Class (Secondary Windows & Detached Overlays)
// ============================================================================

class Window
{
public:
    using RenderCallback = std::function<void(Window* window, float delta_time)>;
    using EventCallback = std::function<void(Window* window)>;
    using MoveCallback = std::function<void(Window* window, int x, int y)>;
    using ResizeCallback = std::function<void(Window* window, int w, int h)>;

    Window(const std::string& id, ID3D11Device* device, IDXGIFactory* factory,
           const Config& config);
    ~Window();

    // Custom UI Rendering Logic
    void SetRenderCallback(RenderCallback callback) { m_render_callback = callback; }

    // Progress State (for built-in progress card renderer)
    void SetProgressData(const std::string& title, const std::string& icon, float progress)
    {
        m_title = title;
        m_icon = icon;
        m_progress = progress;
    }

    void SetAutoDismissOnFinish(bool enable, float delay_seconds = 2.0f)
    {
        m_config.auto_dismiss_on_finish = enable;
        m_config.finish_dismiss_delay = delay_seconds;
    }
    bool IsFinished() const { return m_progress >= 1.0f; }
    float GetFinishTimer() const { return m_finish_timer; }

    // Frame Lifecycle
    bool Update(float delta_time);
    void Render();

    // --- Window Visibility & State Controls ---
    void Show(bool cascade_to_children = true);
    void Hide(bool cascade_to_children = true);
    void SetVisible(bool visible, bool cascade_to_children = true);
    bool IsVisible() const;

    void Minimize(bool cascade_to_children = true);
    void Maximize();
    void Restore(bool cascade_to_children = true);
    bool IsMinimized() const;
    bool IsMaximized() const;

    // --- Per-Window Configuration & Modifiers ---
    void SetWindowTitle(const std::string& title);
    const std::string& GetWindowTitle() const { return m_config.window_title; }

    void SetTopmost(bool topmost);
    bool IsTopmost() const { return m_config.is_topmost; }

    void SetTaskbarVisible(bool visible);
    bool IsTaskbarVisible() const { return !m_config.hide_from_taskbar; }

    void SetCaptureHidden(bool hide);
    bool IsCaptureHidden() const { return m_config.exclude_from_capture; }

    void SetClickThrough(bool click_through);
    bool IsClickThrough() const { return m_config.is_click_through; }

    void SetMovable(bool movable) { m_config.is_movable = movable; }
    bool IsMovable() const { return m_config.is_movable; }

    void SetPosition(int x, int y);
    ImVec2 GetPosition() const { return m_current_screen_pos; }

    void SetSize(int width, int height);
    ImVec2 GetSize() const { return m_config.size; }

    void SetAnchor(AnchorMode anchor, const ImVec2& margin = ImVec2(24.f, 24.f));
    void SetOpacity(float alpha);
    float GetOpacity() const { return m_alpha; }

    // --- Feature 1: Acrylic / Mica DWM Blur ---
    void SetAcrylicBlur(bool enable, AcrylicType type = AcrylicType::Acrylic);
    bool IsAcrylicBlurEnabled() const { return m_config.enable_acrylic_blur; }
    AcrylicType GetAcrylicType() const { return m_config.acrylic_type; }

    // --- Feature 2: Magnetic Edge Snapping ---
    bool IsSnapped() const { return m_snap_edge != SnapEdge::None; }
    SnapEdge GetSnapEdge() const { return m_snap_edge; }
    void SetSnapEnabled(bool enable) { m_config.enable_snap = enable; }
    void SetSnapThreshold(float px) { m_config.snap_threshold = px; }

    void SetDuration(float seconds) { m_config.duration_seconds = seconds; }
    void SetClickableRegions(const std::vector<ImRect>& regions) { m_config.clickable_regions = regions; }
    void SetDragRegions(const std::vector<ImRect>& regions) { m_config.drag_regions = regions; }
    void SetCornerRadius(float radius) { m_config.corner_radius = radius; }
    void SetCustomColors(ImU32 bg, ImU32 border, ImU32 accent = IM_COL32(138, 143, 255, 255), float thickness = 1.0f)
    {
        m_config.custom_bg_color = bg;
        m_config.custom_border_color = border;
        m_config.custom_accent_color = accent;
        m_config.border_thickness = thickness;
    }
    void SetFonts(ImFont* text_font, ImFont* icon_font = nullptr)
    {
        m_config.custom_font = text_font;
        m_config.custom_icon_font = icon_font;
    }

    // --- Parent / Child Hierarchy Management ---
    void SetParentId(const std::string& parent_id) { m_config.parent_id = parent_id; }
    const std::string& GetParentId() const { return m_config.parent_id; }

    void AddChild(const std::string& child_id);
    void RemoveChild(const std::string& child_id);
    const std::vector<std::string>& GetChildren() const { return m_child_ids; }
    bool HasChildren() const { return !m_child_ids.empty(); }

    void OnParentMoved(int parent_x, int parent_y);

    // --- Event Callbacks ---
    void SetOnCloseCallback(EventCallback cb) { m_on_close_cb = cb; }
    void SetOnMoveCallback(MoveCallback cb) { m_on_move_cb = cb; }
    void SetOnResizeCallback(ResizeCallback cb) { m_on_resize_cb = cb; }

    // Smooth Close (animates out and cascades to children) vs Instant Destroy
    void Close(bool cascade_to_children = true);
    void DestroyNow(bool cascade_to_children = true);

    // Queries
    const std::string& GetId() const { return m_id; }
    bool IsAlive() const { return m_is_alive; }
    bool IsClosing() const { return m_closing; }
    HWND GetHwnd() const { return m_hwnd; }
    ID3D11Device* GetDevice() const { return m_device; }
    ID3D11RenderTargetView* GetRTV() const { return m_rtv; }
    const Config& GetConfig() const { return m_config; }

    // Win32 message handling
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void InitWindow(IDXGIFactory* factory);
    void CalculateScreenPosition(const ImVec2& margin = ImVec2(24.f, 24.f));
    void RenderBuiltinProgress();
    void ResizeBuffers(int width, int height);
    void ApplyAcrylicEffect();          // Feature 1: DWM blur/acrylic setup
    void SnapWindowPosition(RECT& rc);  // Feature 2: magnetic snap, modifies RECT in WM_MOVING
    void SetupImGuiContext();           // Feature 4: create per-window ImGuiContext

    std::string m_id;
    HWND m_hwnd = nullptr;
    ID3D11Device* m_device = nullptr;
    IDXGIFactory* m_swap_chain_factory = nullptr;
    IDXGISwapChain* m_swap_chain = nullptr;
    ID3D11RenderTargetView* m_rtv = nullptr;

    Config m_config;
    ImVec2 m_window_size;
    ImVec2 m_target_screen_pos;
    ImVec2 m_current_screen_pos;

    std::vector<std::string> m_child_ids;

    bool m_is_alive = true;
    bool m_closing = false;

    float m_time_alive = 0.0f;
    float m_alpha = 1.0f;
    float m_anim_progress = 0.0f;

    std::string m_title;
    std::string m_icon;
    float m_progress = 0.0f;
    float m_finish_timer = 0.0f;

    // Feature 2: snap state
    SnapEdge m_snap_edge = SnapEdge::None;

    // Feature 4: per-window ImGui context
    ImGuiContext* m_imgui_context = nullptr;

    RenderCallback m_render_callback = nullptr;
    EventCallback m_on_close_cb = nullptr;
    MoveCallback m_on_move_cb = nullptr;
    ResizeCallback m_on_resize_cb = nullptr;
};

// ============================================================================
// Manager Class (Singleton)
// ============================================================================

class Manager
{
public:
    static Manager& Get()
    {
        static Manager instance;
        return instance;
    }

    // Initialization & Direct3D Binding
    void Init(HWND hwnd, const ImVec2& initial_pos, const ImVec2& menu_size);
    void SetD3DObjects(IDXGISwapChain* swap_chain, ID3D11Device* device, ID3D11RenderTargetView** rtv);
    void SetDXGIFactory(IDXGIFactory* factory) { m_dxgi_factory = factory; }

    // Configuration
    void SetPadding(float uniform_padding)
    {
        SetPadding(uniform_padding, uniform_padding, uniform_padding, uniform_padding);
    }
    void SetPadding(float left, float top, float right, float bottom)
    {
        m_padding = ImVec4(left, top, right, bottom);
        m_target_width = m_menu_size.x + m_padding.x + m_padding.z;
        m_target_height = m_menu_size.y + m_padding.y + m_padding.w;
        ImRect menu_rect(
            m_padding.x,
            m_padding.y,
            m_padding.x + m_menu_size.x,
            m_padding.y + m_menu_size.y
        );
        RegisterElementRect("main_menu", menu_rect, true);
    }
    void SetTransitionMode(TransitionMode mode, float speed = 14.0f)
    {
        m_transition_mode = mode;
        m_transition_speed = speed;
    }

    void SetTopmost(bool topmost);
    bool IsTopmost() const { return m_is_topmost; }
    void SetAutoTopmost(bool enable) { m_auto_topmost = enable; }

    // Screen Capture Protection (Streamer Mode / Anti-Recording)
    // Granular Per-Window Controls:
    void SetMainCaptureHidden(bool hide);
    bool IsMainCaptureHidden() const { return m_main_exclude_from_capture; }
    void SetCaptureHidden(HWND hwnd, bool hide);
    void SetCaptureHidden(const std::string& window_id, bool hide);
    bool IsCaptureHidden(const std::string& window_id) const;
    void SetCaptureHiddenAll(bool hide);
    void StartCaptureMonitor(uint32_t poll_interval_ms = 1000);
    void StopCaptureMonitor();
    bool IsCaptureMonitorRunning() const { return m_monitor_running.load(); }

    // Taskbar & Alt+Tab Controls (Per-Window & Bulk):
    void SetMainTaskbarVisible(bool visible);
    bool IsMainTaskbarVisible() const { return !m_main_hide_from_taskbar; }
    void SetTaskbarVisible(const std::string& window_id, bool visible);
    bool IsTaskbarVisible(const std::string& window_id) const;
    void SetTaskbarVisibleAll(bool visible);

    // =========================================================================
    // Feature 3: Multi-Toast Queue & Stacking Engine
    // Thread-safe: PushToast may be called from any thread.
    // =========================================================================
    void PushToast(const std::string& title, const std::string& message,
                   float duration = 4.0f,
                   ImU32 accent = IM_COL32(138, 143, 255, 255),
                   AnchorMode anchor = AnchorMode::Screen_BottomRight);
    void DismissToast(const std::string& title);
    void DismissAllToasts();
    size_t GetToastCount() const;

    // Legacy — redirects to PushToast
    // =========================================================================
    // Feature 5: Global Hotkey Listener
    // Spawns a message-only HWND on a background thread; thread-safe.
    // =========================================================================
    bool RegisterHotkey(int id, UINT modifiers, UINT vk, HotkeyAction action,
                        std::function<void()> custom_cb = nullptr);
    void UnregisterHotkey(int id);
    void UnregisterAllHotkeys();
    void StartHotkeyListener();
    void StopHotkeyListener();
    bool IsHotkeyListenerRunning() const { return m_hotkey_running.load(); }

    // Registration of expandable UI elements
    void RegisterElement(const std::string& name, const ImVec2& pos, const ImVec2& size, bool interactive = true);
    void RegisterRelativeElement(const std::string& name, const ImVec2& local_pos, const ImVec2& size, bool interactive = true)
    {
        ImVec2 screen_pos(m_padding.x + local_pos.x, m_padding.y + local_pos.y);
        RegisterElement(name, screen_pos, size, interactive);
    }
    void RegisterElementRect(const std::string& name, const ImRect& rect, bool interactive = true);
    void UnregisterElement(const std::string& name);
    void SetElementActive(const std::string& name, bool active);
    void SetElementInteractive(const std::string& name, bool interactive);

    // Frame Lifecycle
    void BeginFrame();
    void EndFrame(float delta_time);

    // Handle Win32 WM_NCHITTEST message
    LRESULT HandleHitTest(LPARAM lParam);

    // --- Detached Floating Overlay Windows & Hierarchy ---
    Window* CreateFloatingOverlay(const std::string& id, const Config& config,
                                  Window::RenderCallback callback = nullptr);

    Window* CreateSubWindow(const std::string& parent_id, const std::string& child_id,
                            const Config& config,
                            Window::RenderCallback callback = nullptr);

    Window* ShowDetachedProgress(const std::string& title, const std::string& icon,
                                 float progress, const Config& config = {});

    void ShowDetachedToast(const std::string& title, const std::string& message,
                           const Config& config = {});

    Window* GetFloatingOverlay(const std::string& id);
    bool HasFloatingOverlay(const std::string& id) const;
    void CloseFloatingOverlay(const std::string& id);
    void DestroyFloatingOverlay(const std::string& id);
    void CloseWindowHierarchy(const std::string& root_id);

    void CloseAllFloatingOverlays();
    void HideAllFloatingOverlays();
    void ShowAllFloatingOverlays();
    void MinimizeAllFloatingOverlays();
    void RestoreAllFloatingOverlays();

    std::vector<std::string> GetFloatingOverlayIds() const;
    std::vector<std::string> GetChildrenOf(const std::string& parent_id) const;
    size_t GetFloatingOverlayCount() const { return m_floating_overlays.size(); }

    // Coordinates & state queries
    ImVec2 GetMenuSize() const { return m_menu_size; }
    ImVec2 GetMenuLocalPos() const { return ImVec2(m_padding.x, m_padding.y); }
    ImVec4 GetPadding() const { return m_padding; }
    bool HasActiveOutsideElements() const { return m_has_outside_elements; }
    ID3D11Device* GetDevice() const { return m_d3d_device; }
    HWND GetMainHwnd() const { return m_hwnd; }

private:
    friend class Window; // Allow Window::SnapWindowPosition to access m_floating_overlays
    Manager() = default;
    ~Manager();

    HWND m_hwnd = nullptr;
    IDXGISwapChain* m_swap_chain = nullptr;
    ID3D11Device* m_d3d_device = nullptr;
    ID3D11RenderTargetView** m_main_rtv = nullptr;
    IDXGIFactory* m_dxgi_factory = nullptr;

    ImVec2 m_menu_size = ImVec2(646.f, 458.f);
    ImVec4 m_padding = ImVec4(26.f, 26.f, 26.f, 26.f);

    std::unordered_map<std::string, Element> m_elements;
    std::unordered_map<std::string, bool> m_click_through_overrides;
    std::vector<std::unique_ptr<Window>> m_floating_overlays;

    float m_current_height = 510.f;
    float m_target_height = 510.f;
    float m_current_width = 698.f;
    float m_target_width = 698.f;

    TransitionMode m_transition_mode = TransitionMode::Smooth;
    float m_transition_speed = 14.0f;
    bool m_is_topmost = false;
    bool m_auto_topmost = false;
    bool m_has_outside_elements = false;
    bool m_main_exclude_from_capture = false;
    bool m_main_hide_from_taskbar = false;

    // Capture monitor background thread state
    std::atomic<bool> m_monitor_running{ false };
    std::thread m_monitor_thread;
    std::mutex m_hidden_windows_mutex;
    std::unordered_set<HWND> m_hidden_capture_windows;

    // -------------------------------------------------------------------------
    // Feature 3: Multi-Toast Queue
    // -------------------------------------------------------------------------
    struct ToastEntry
    {
        std::string  id;             // Unique id (title + index)
        std::string  title;
        std::string  message;
        float        duration;       // Auto-dismiss seconds
        float        age = 0.f;      // Time alive
        float        anim_t = 0.f;   // 0..1 slide-in progress
        bool         dismissing = false;
        ImU32        accent;
        AnchorMode   anchor;
    };
    std::deque<ToastEntry> m_toast_queue;
    std::mutex             m_toast_mutex;
    static constexpr int   k_max_toasts    = 5;
    static constexpr float k_toast_spacing = 12.f;
    static constexpr float k_toast_w       = 320.f;
    static constexpr float k_toast_h       = 68.f;
    static constexpr float k_toast_margin  = 20.f;
    HWND  m_toast_hwnd    = nullptr;
    IDXGISwapChain*         m_toast_swap_chain = nullptr;
    ID3D11RenderTargetView* m_toast_rtv        = nullptr;

    void UpdateToasts(float delta_time);
    void RenderToasts();
    void EnsureToastWindow();

    // -------------------------------------------------------------------------
    // Feature 5: Global Hotkey Listener
    // -------------------------------------------------------------------------
    struct HotkeyEntry
    {
        int         id;
        UINT        modifiers;
        UINT        vk;
        HotkeyAction action;
        std::function<void()> custom_cb;
    };
    std::unordered_map<int, HotkeyEntry> m_hotkeys;
    std::mutex     m_hotkey_mutex;
    std::thread    m_hotkey_thread;
    HWND           m_hotkey_hwnd = nullptr;
    std::atomic<bool> m_hotkey_running{ false };

    void HotkeyMessageLoop();
    void DispatchHotkeyAction(const HotkeyEntry& entry);

    void RecalculateBounds();
    void ApplyWindowResize(int width, int height);
    void UpdateFloatingOverlays(float delta_time);
    void CaptureMonitorLoop(uint32_t poll_interval_ms);
};

} // namespace ImOverlay

// ============================================================================
// Global Backward-Compatible Type Aliases
// ============================================================================
using OverlayManager = ImOverlay::Manager;
using FloatingOverlayWindow = ImOverlay::Window;
using OverlayConfig = ImOverlay::Config;
using OverlayElement = ImOverlay::Element;
using AnchorMode = ImOverlay::AnchorMode;
using TransitionMode = ImOverlay::TransitionMode;
using AcrylicType = ImOverlay::AcrylicType;
using SnapEdge = ImOverlay::SnapEdge;
using HotkeyAction = ImOverlay::HotkeyAction;

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#endif // IMOVERLAY_DX11_HPP_

