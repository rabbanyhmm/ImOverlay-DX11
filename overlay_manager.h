//        ___       ___              _           _____  ___ _ 
//       |_ _|_ __ / _ \__ _____ _ _| |__ _ _  _|   \ \/ / / |
//        | || '  \ (_) \ V / -_) '_| / _` | || | |) >  <| | |
//       |___|_|_|_\___/ \_/\___|_| |_\__,_|\_, |___/_/\_\_|_|
//                                          |__/              
//
//  ImOverlay-DX11: Hardware-Accelerated Desktop Overlay & Multi-Window Framework
//  version 1.0.0 (Release Build: 2026-08-14)
//  https://github.com/rabbanyhmm/ImOverlay-DX11
//
//  SPDX-FileCopyrightText: 2026 rabbanyhmm <https://github.com/rabbanyhmm>
//  SPDX-License-Identifier: MIT


#ifndef IMOVERLAY_DX11_HPP_
#define IMOVERLAY_DX11_HPP_

#pragma once

// Version
#define IMOVERLAY_VERSION_MAJOR 1
#define IMOVERLAY_VERSION_MINOR 0
#define IMOVERLAY_VERSION_PATCH 0
#define IMOVERLAY_VERSION_BUILD 20260814
#define IMOVERLAY_VERSION       "1.0.0"
#define IMOVERLAY_VERSION_NUMBER \
    (IMOVERLAY_VERSION_MAJOR * 10000 + IMOVERLAY_VERSION_MINOR * 100 + IMOVERLAY_VERSION_PATCH)

// Platform checks
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
#pragma comment(lib, "winmm.lib")
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

// Types

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
    Screen_TopCenter,       // Fixed at screen top-center
    Screen_TopRight,        // Fixed at screen top-right
    Screen_BottomLeft,      // Fixed at screen bottom-left
    Screen_BottomCenter,    // Fixed at screen bottom-center
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
    std::string window_title = "ImOverlay Window";
    std::string parent_id = "main_menu";
    AnchorMode anchor = AnchorMode::Screen_BottomRight;
    ImVec2 custom_pos = ImVec2(0.f, 0.f);
    ImVec2 offset_from_parent = ImVec2(0.f, 0.f);
    ImVec2 size = ImVec2(340.f, 80.f);
    ImVec4 padding = ImVec4(16.f, 16.f, 16.f, 16.f);

    bool is_topmost = true;
    bool topmost = true;
    bool hide_from_taskbar = true;
    bool exclude_from_capture = false;
    bool is_movable = true;
    bool is_click_through = false;
    bool start_hidden = false;
    bool start_minimized = false;

    // Hierarchy
    bool close_with_parent = true;
    bool hide_with_parent = true;
    bool minimize_with_parent = true;
    bool follow_parent_movement = true;

    // Hit-testing regions
    std::vector<ImRect> clickable_regions;
    std::vector<ImRect> drag_regions;

    // Lifetime
    float duration_seconds = -1.0f;     // < 0 for permanent, > 0 for auto-dismiss seconds
    bool auto_dismiss_on_finish = true;
    float finish_dismiss_delay = 2.0f;
    float initial_opacity = 1.0f;

    // Styling
    float corner_radius = 16.0f;
    bool draw_default_card_bg = true;
    ImU32 custom_bg_color = IM_COL32(18, 18, 20, 240);
    ImU32 custom_border_color = IM_COL32(255, 255, 255, 25);
    ImU32 custom_accent_color = IM_COL32(138, 143, 255, 255);
    ImU32 custom_text_color = IM_COL32(255, 255, 255, 255);
    ImU32 custom_track_color = IM_COL32(255, 255, 255, 12);
    float border_thickness = 1.0f;
    float accent_alpha = 1.0f;

    // Blur and snapping
    bool enable_acrylic_blur = false;
    AcrylicType acrylic_type = AcrylicType::Acrylic;
    bool enable_snap = false;
    float snap_threshold = 18.0f;
    bool play_snap_sound = false;

    // Typography
    ImFont* custom_font = nullptr;
    ImFont* custom_icon_font = nullptr;
};

// Window

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

    ImDrawList* GetDrawList() const { return m_current_draw_list; }

    // Progress State (for built-in progress card renderer)
    void SetProgressData(const std::string& title, const std::string& icon, float progress)
    {
        m_title = title;
        m_icon = icon;
        m_progress = progress;
        m_message.clear();
    }

    // Toast State (for built-in toast notification card renderer)
    void SetToastData(const std::string& title, const std::string& message, ImU32 accent = IM_COL32(138, 143, 255, 255))
    {
        m_title = title;
        m_message = message;
        m_toast_accent = accent;
        m_config.custom_accent_color = accent;
    }

    const std::string& GetTitle() const { return m_title; }
    const std::string& GetMessage() const { return m_message; }

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

    // Visibility and state
    void Show(bool cascade_to_children = true);
    void Hide(bool cascade_to_children = true);
    void SetVisible(bool visible, bool cascade_to_children = true);
    bool IsVisible() const;

    void Minimize(bool cascade_to_children = true);
    void Maximize();
    void Restore(bool cascade_to_children = true);
    bool IsMinimized() const;
    bool IsMaximized() const;

    // Configuration
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
    ImVec2 GetWindowSize() const { return m_window_size; }

    void SetAnchor(AnchorMode anchor, const ImVec2& margin = ImVec2(24.f, 24.f));
    void SetOpacity(float alpha);
    float GetOpacity() const { return m_alpha; }

    // DWM blur
    void SetAcrylicBlur(bool enable, AcrylicType type = AcrylicType::Acrylic);
    bool IsAcrylicBlurEnabled() const { return m_config.enable_acrylic_blur; }
    AcrylicType GetAcrylicType() const { return m_config.acrylic_type; }

    // Snapping
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

    // Hierarchy
    void SetParentId(const std::string& parent_id) { m_config.parent_id = parent_id; }
    const std::string& GetParentId() const { return m_config.parent_id; }

    void AddChild(const std::string& child_id);
    void RemoveChild(const std::string& child_id);
    const std::vector<std::string>& GetChildren() const { return m_child_ids; }
    bool HasChildren() const { return !m_child_ids.empty(); }

    void OnParentMoved(int parent_x, int parent_y);

    // Callbacks
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
    void RenderBuiltinProgress(ImDrawList* draw_list);
    void ResizeBuffers(int width, int height);
    void ApplyAcrylicEffect();
    void SnapWindowPosition(RECT& rc);

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
    std::string m_message;
    std::string m_icon;
    ImU32 m_toast_accent = IM_COL32(138, 143, 255, 255);
    float m_progress = 0.0f;
    float m_finish_timer = 0.0f;

    SnapEdge m_snap_edge = SnapEdge::None;
    ImGuiContext* m_imgui_context = nullptr;
    ImDrawList* m_current_draw_list = nullptr;

    RenderCallback m_render_callback = nullptr;
    EventCallback m_on_close_cb = nullptr;
    MoveCallback m_on_move_cb = nullptr;
    ResizeCallback m_on_resize_cb = nullptr;
};

// Manager

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

    using GlobalFrameCallback = std::function<void()>;
    void SetGlobalFrameCallback(GlobalFrameCallback cb) { m_global_frame_cb = cb; }
    void TickGlobalFrame()
    {
        if (m_global_frame_cb)
            m_global_frame_cb();
    }

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

    // Screen capture exclusion (WDA_EXCLUDEFROMCAPTURE)
    void SetMainCaptureHidden(bool hide);
    bool IsMainCaptureHidden() const { return m_main_exclude_from_capture; }
    void SetCaptureHidden(HWND hwnd, bool hide);
    void SetCaptureHidden(const std::string& window_id, bool hide);
    bool IsCaptureHidden(const std::string& window_id) const;
    void SetCaptureHiddenAll(bool hide);
    void StartCaptureMonitor(uint32_t poll_interval_ms = 1000);
    void StopCaptureMonitor();
    bool IsCaptureMonitorRunning() const { return m_monitor_running.load(); }

    // Taskbar visibility
    void SetMainTaskbarVisible(bool visible);
    bool IsMainTaskbarVisible() const { return !m_main_hide_from_taskbar; }
    void SetTaskbarVisible(const std::string& window_id, bool visible);
    bool IsTaskbarVisible(const std::string& window_id) const;
    void SetTaskbarVisibleAll(bool visible);

    // Toast notifications (thread-safe)
    void PushToast(const std::string& title, const std::string& message,
                   float duration = 4.0f,
                   ImU32 accent = IM_COL32(138, 143, 255, 255),
                   AnchorMode anchor = AnchorMode::Screen_BottomRight);
    void DismissToast(const std::string& title);
    void DismissAllToasts();
    size_t GetToastCount() const;

    // Hotkeys (thread-safe)
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

    // Floating overlays
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
    float GetTargetWidth() const { return m_target_width; }
    float GetTargetHeight() const { return m_target_height; }
    ImVec2 GetTargetSize() const { return ImVec2(m_target_width, m_target_height); }
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

    // Toast notifications
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
    struct ToastItem
    {
        std::string id;
        std::string title;
        std::string message;
        float duration = 4.0f;
        float time_alive = 0.0f;
        float alpha = 0.0f;
        bool closing = false;
        ImU32 accent = IM_COL32(138, 143, 255, 255);
        AnchorMode anchor = AnchorMode::Screen_BottomRight;
    };
    std::vector<ToastItem> m_toasts;
    std::mutex m_toasts_mutex;
    uint32_t m_toast_counter = 0;

    void UpdateToasts(float delta_time);
    void RenderToasts();

    // Hotkey listener
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
    GlobalFrameCallback m_global_frame_cb = nullptr;

    void HotkeyMessageLoop();
    void DispatchHotkeyAction(const HotkeyEntry& entry);

    void RecalculateBounds();
    void ApplyWindowResize(int width, int height);
    void UpdateFloatingOverlays(float delta_time);
    void CaptureMonitorLoop(uint32_t poll_interval_ms);
};

} // namespace ImOverlay

// Compatibility aliases
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

#if defined(IMOVERLAY_IMPLEMENTATION) || defined(IMOVERLAY_HEADER_ONLY)
#include "overlay_manager.cpp"
#endif

#endif // IMOVERLAY_DX11_HPP_

