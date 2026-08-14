//        ___       ___              _           _____  ___ _ 
//       |_ _|_ __ / _ \__ _____ _ _| |__ _ _  _|   \ \/ / / |
//        | || '  \ (_) \ V / -_) '_| / _` | || | |) >  <| | |
//       |___|_|_|_\___/ \_/\___|_| |_\__,_|\_, |___/_/\_\_|_|
//                                    |__/              
//
//  ImOverlay-DX11: Hardware-Accelerated Desktop Overlay & Multi-Window Framework
//  Implementation Source File (Version 1.0.0)
//  https://github.com/rabbanyhmm/ImOverlay-DX11
//
//  SPDX-FileCopyrightText: 2026 rabbanyhmm <https://github.com/rabbanyhmm>
//  SPDX-License-Identifier: MIT

/****************************************************************************\
 *                                                                          *
 *  ImOverlay-DX11 Core Implementation Details:                             *
 *  ------------------------------------------                              *
 *  - DWM Composition & Backdrop: Integrates Windows 11 DWMWA_SYSTEMBACKDROP*
 *    and Windows 10 SetWindowCompositionAttribute for hardware blur.       *
 *  - Screen Capture Exclusion: Applies WDA_EXCLUDEFROMCAPTURE per HWND.    *
 *  - Magnetic Snapping: Intercepts WM_MOVING for smooth border docking.    *
 *  - Multi-Toast Queue: Lock-guarded circular buffer with slide animation. *
 *  - Global Hotkeys: Spawns dedicated message-only HWND pump thread.       *
 *                                                                          *
 *  See overlay_manager.h for the full public API declarations.             *
 *                                                                          *
\****************************************************************************/

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "overlay_manager.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <chrono>

// ============================================================================
// DWM Composition Attribute (Windows 10 Acrylic fallback)
// ============================================================================
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_NONE
#define DWMSBT_NONE             0  // No backdrop
#define DWMSBT_MAINWINDOW       1  // Mica
#define DWMSBT_TRANSIENTWINDOW  3  // Acrylic
#define DWMSBT_TABBEDWINDOW     4  // Mica Alt (tabbed)
#endif

enum ACCENT_STATE
{
    ACCENT_DISABLED                   = 0,
    ACCENT_ENABLE_GRADIENT            = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND          = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND   = 4,
    ACCENT_INVALID_STATE              = 5
};

struct ACCENT_POLICY
{
    ACCENT_STATE AccentState;
    DWORD        AccentFlags;
    DWORD        GradientColor;
    DWORD        AnimationId;
};

enum WINDOWCOMPOSITIONATTRIB
{
    WCA_ACCENT_POLICY = 19
};

struct WINDOWCOMPOSITIONATTRIBDATA
{
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID                   pvData;
    SIZE_T                  cbData;
};

using PFN_SetWindowCompositionAttribute =
    BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

namespace ImOverlay
{

// ============================================================================
// Window Implementation
// ============================================================================

Window::Window(const std::string& id, ID3D11Device* device,
               IDXGIFactory* factory, const Config& config)
    : m_id(id), m_device(device), m_swap_chain_factory(factory), m_config(config)
{
    m_alpha = std::clamp(config.initial_opacity, 0.0f, 1.0f);
    m_window_size = ImVec2(
        m_config.size.x + m_config.padding.x + m_config.padding.z,
        m_config.size.y + m_config.padding.y + m_config.padding.w
    );
    InitWindow(factory);
}

Window::~Window()
{
    if (m_hwnd && m_config.exclude_from_capture)
    {
        ::SetWindowDisplayAffinity(m_hwnd, WDA_NONE);
    }
    if (m_rtv)
    {
        m_rtv->Release();
        m_rtv = nullptr;
    }
    if (m_swap_chain)
    {
        m_swap_chain->Release();
        m_swap_chain = nullptr;
    }
    if (m_hwnd)
    {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void Window::CalculateScreenPosition(const ImVec2& margin)
{
    // Multi-monitor awareness: find the appropriate display monitor work area
    HMONITOR hMon = nullptr;
    if (m_hwnd)
    {
        hMon = ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    }
    else if (!m_config.parent_id.empty() && m_config.parent_id != "main_menu")
    {
        auto* parent = Manager::Get().GetFloatingOverlay(m_config.parent_id);
        if (parent && parent->GetHwnd())
            hMon = ::MonitorFromWindow(parent->GetHwnd(), MONITOR_DEFAULTTONEAREST);
    }

    if (!hMon)
    {
        POINT pt = { 0, 0 };
        hMon = ::MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    }

    MONITORINFO mi = { sizeof(mi) };
    ::GetMonitorInfoW(hMon, &mi);
    RECT work = mi.rcWork;
    float work_x = (float)work.left;
    float work_y = (float)work.top;
    float work_w = (float)(work.right - work.left);
    float work_h = (float)(work.bottom - work.top);

    switch (m_config.anchor)
    {
    case AnchorMode::Screen_TopLeft:
        m_target_screen_pos = ImVec2(work_x + margin.x, work_y + margin.y);
        break;
    case AnchorMode::Screen_TopCenter:
        m_target_screen_pos = ImVec2(work_x + (work_w - m_window_size.x) * 0.5f, work_y + margin.y);
        break;
    case AnchorMode::Screen_TopRight:
        m_target_screen_pos = ImVec2(work_x + work_w - m_window_size.x - margin.x, work_y + margin.y);
        break;
    case AnchorMode::Screen_BottomLeft:
        m_target_screen_pos = ImVec2(work_x + margin.x, work_y + work_h - m_window_size.y - margin.y);
        break;
    case AnchorMode::Screen_BottomCenter:
        m_target_screen_pos = ImVec2(work_x + (work_w - m_window_size.x) * 0.5f, work_y + work_h - m_window_size.y - margin.y);
        break;
    case AnchorMode::Screen_BottomRight:
        m_target_screen_pos = ImVec2(work_x + work_w - m_window_size.x - margin.x,
                                     work_y + work_h - m_window_size.y - margin.y);
        break;
    case AnchorMode::Screen_Center:
        m_target_screen_pos = ImVec2(work_x + (work_w - m_window_size.x) * 0.5f,
                                     work_y + (work_h - m_window_size.y) * 0.5f);
        break;
    case AnchorMode::RelativeToParentWindow:
    {
        auto* parent = Manager::Get().GetFloatingOverlay(m_config.parent_id);
        if (parent && parent->GetHwnd())
        {
            ImVec2 parent_pos = parent->GetPosition();
            m_target_screen_pos = ImVec2(parent_pos.x + m_config.offset_from_parent.x,
                                         parent_pos.y + m_config.offset_from_parent.y);
        }
        else
        {
            m_target_screen_pos = ImVec2(work_x + margin.x, work_y + margin.y);
        }
        break;
    }
    case AnchorMode::Screen_Absolute:
        m_target_screen_pos = m_config.custom_pos;
        break;
    case AnchorMode::Relative:
    default:
        m_target_screen_pos = ImVec2(work_x + work_w - m_window_size.x - margin.x,
                                     work_y + work_h - m_window_size.y - margin.y);
        break;
    }

    m_current_screen_pos = m_target_screen_pos;
}

void Window::InitWindow(IDXGIFactory* factory)
{
    HINSTANCE hInstance = ::GetModuleHandle(nullptr);
    const wchar_t* class_name = L"ImOverlay_FloatingClass";

    WNDCLASSEXW wcCheck = { sizeof(wcCheck) };
    if (!::GetClassInfoExW(hInstance, class_name, &wcCheck))
    {
        WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, Window::WndProc, 0L, 0L,
                           hInstance, nullptr, nullptr, nullptr, nullptr, class_name, nullptr };
        ::RegisterClassExW(&wc);
    }

    CalculateScreenPosition();

    DWORD ex_style = WS_EX_LAYERED;
    if (m_config.is_topmost)
        ex_style |= WS_EX_TOPMOST;

    // Taskbar visibility
    if (m_config.hide_from_taskbar)
        ex_style |= WS_EX_TOOLWINDOW;
    else
        ex_style |= WS_EX_APPWINDOW;

    // Click-through
    if (m_config.is_click_through)
        ex_style |= WS_EX_TRANSPARENT;

    int title_len = MultiByteToWideChar(CP_UTF8, 0, m_config.window_title.c_str(), -1, nullptr, 0);
    std::wstring wtitle(title_len, 0);
    MultiByteToWideChar(CP_UTF8, 0, m_config.window_title.c_str(), -1, &wtitle[0], title_len);

    HWND hwnd_parent = nullptr;
    if (!m_config.parent_id.empty() && m_config.parent_id != "main_menu")
    {
        auto* parent = Manager::Get().GetFloatingOverlay(m_config.parent_id);
        if (parent)
            hwnd_parent = parent->GetHwnd();
    }

    m_hwnd = ::CreateWindowExW(
        ex_style,
        class_name,
        wtitle.c_str(),
        WS_POPUP,
        (int)m_current_screen_pos.x,
        (int)m_current_screen_pos.y,
        (int)m_window_size.x,
        (int)m_window_size.y,
        hwnd_parent, nullptr, hInstance, nullptr
    );

    if (m_hwnd)
    {
        ::SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
    }

    MARGINS margins = { -1 };
    ::DwmExtendFrameIntoClientArea(m_hwnd, &margins);
    BYTE byte_alpha = (BYTE)(m_alpha * 255.f);
    ::SetLayeredWindowAttributes(m_hwnd, 0, byte_alpha, LWA_ALPHA);

    // Screen Capture Exclusion (Streamer Mode)
    if (m_config.exclude_from_capture && m_hwnd)
    {
        ::SetWindowDisplayAffinity(m_hwnd, WDA_EXCLUDEFROMCAPTURE);
    }

    // Direct3D swapchain creation (auto-resolve factory from device if null)
    IDXGIFactory* pFactory = factory;
    bool should_release_factory = false;

    if (!pFactory && m_device)
    {
        IDXGIDevice* pDXGIDevice = nullptr;
        if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&pDXGIDevice))))
        {
            IDXGIAdapter* pDXGIAdapter = nullptr;
            if (SUCCEEDED(pDXGIDevice->GetAdapter(&pDXGIAdapter)))
            {
                if (SUCCEEDED(pDXGIAdapter->GetParent(IID_PPV_ARGS(&pFactory))))
                {
                    should_release_factory = true;
                }
                pDXGIAdapter->Release();
            }
            pDXGIDevice->Release();
        }

        if (!pFactory)
        {
            if (SUCCEEDED(CreateDXGIFactory(IID_PPV_ARGS(&pFactory))))
            {
                should_release_factory = true;
            }
        }
    }

    if (m_device && pFactory)
    {
        DXGI_SWAP_CHAIN_DESC sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 2;
        sd.BufferDesc.Width = (UINT)m_window_size.x;
        sd.BufferDesc.Height = (UINT)m_window_size.y;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = m_hwnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        if (SUCCEEDED(pFactory->CreateSwapChain(m_device, &sd, &m_swap_chain)))
        {
            ID3D11Texture2D* pBackBuffer = nullptr;
            if (SUCCEEDED(m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))))
            {
                m_device->CreateRenderTargetView(pBackBuffer, nullptr, &m_rtv);
                pBackBuffer->Release();
            }
        }

        if (should_release_factory && pFactory)
        {
            pFactory->Release();
            pFactory = nullptr;
        }
    }

    if (m_config.is_topmost && m_hwnd)
    {
        ::SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // Feature 1: Apply Acrylic/Mica blur if requested
    if (m_config.enable_acrylic_blur)
        ApplyAcrylicEffect();


    if (!m_config.start_hidden)
    {
        if (m_config.start_minimized)
            ::ShowWindow(m_hwnd, SW_MINIMIZE);
        else
            ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        ::UpdateWindow(m_hwnd);
    }
}

// ============================================================================
// Feature 1: Acrylic / Mica DWM Hardware Blur
// ============================================================================

void Window::ApplyAcrylicEffect()
{
    if (!m_hwnd || m_config.acrylic_type == AcrylicType::None)
        return;

    // Windows 11 22H2+ path: DWMWA_SYSTEMBACKDROP_TYPE
    {
        int backdrop = DWMSBT_NONE;
        switch (m_config.acrylic_type)
        {
        case AcrylicType::Mica:    backdrop = DWMSBT_MAINWINDOW;      break;
        case AcrylicType::Acrylic: backdrop = DWMSBT_TRANSIENTWINDOW; break;
        case AcrylicType::MicaAlt: backdrop = DWMSBT_TABBEDWINDOW;    break;
        default: break;
        }
        if (backdrop != DWMSBT_NONE)
        {
            HRESULT hr = ::DwmSetWindowAttribute(
                m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
            if (SUCCEEDED(hr))
                return; // Win11 Mica/Acrylic applied
        }
    }

    // Windows 10 fallback: SetWindowCompositionAttribute
    HMODULE hUser32 = ::GetModuleHandleW(L"user32.dll");
    if (!hUser32) return;
    auto pfnSet = reinterpret_cast<PFN_SetWindowCompositionAttribute>(
        ::GetProcAddress(hUser32, "SetWindowCompositionAttribute"));
    if (!pfnSet) return;

    ACCENT_STATE state = ACCENT_DISABLED;
    switch (m_config.acrylic_type)
    {
    case AcrylicType::Blur:    state = ACCENT_ENABLE_BLURBEHIND;        break;
    case AcrylicType::Acrylic: state = ACCENT_ENABLE_ACRYLICBLURBEHIND; break;
    default:                   state = ACCENT_ENABLE_BLURBEHIND;        break;
    }

    ACCENT_POLICY policy = { state, 0x20, 0x00000000, 0 };
    WINDOWCOMPOSITIONATTRIBDATA data;
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &policy;
    data.cbData = sizeof(policy);
    pfnSet(m_hwnd, &data);
}

void Window::SetAcrylicBlur(bool enable, AcrylicType type)
{
    m_config.enable_acrylic_blur = enable;
    m_config.acrylic_type        = type;
    if (!enable)
    {
        int none = DWMSBT_NONE;
        ::DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &none, sizeof(none));
        HMODULE hUser32 = ::GetModuleHandleW(L"user32.dll");
        if (hUser32)
        {
            auto pfnSet = reinterpret_cast<PFN_SetWindowCompositionAttribute>(
                ::GetProcAddress(hUser32, "SetWindowCompositionAttribute"));
            if (pfnSet)
            {
                ACCENT_POLICY p2 = { ACCENT_DISABLED, 0, 0, 0 };
                WINDOWCOMPOSITIONATTRIBDATA d2{ WCA_ACCENT_POLICY, &p2, sizeof(p2) };
                pfnSet(m_hwnd, &d2);
            }
        }
    }
    else { ApplyAcrylicEffect(); }
}

// ============================================================================
// Feature 2: Magnetic Edge Snapping
// ============================================================================

void Window::SnapWindowPosition(RECT& rc)
{
    if (!m_config.enable_snap) return;
    const float t = m_config.snap_threshold;
    float w = (float)(rc.right  - rc.left);
    float h = (float)(rc.bottom - rc.top);
    float x = (float)rc.left;
    float y = (float)rc.top;

    HMONITOR hMon = ::MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    ::GetMonitorInfoW(hMon, &mi);
    float sl = (float)mi.rcWork.left, st = (float)mi.rcWork.top;
    float sr = (float)mi.rcWork.right, sb = (float)mi.rcWork.bottom;

    bool sx = false, sy = false;
    if (std::abs(x - sl) < t)           { x = sl;       sx = true; }
    else if (std::abs(x + w - sr) < t)  { x = sr - w;   sx = true; }
    if (std::abs(y - st) < t)           { y = st;       sy = true; }
    else if (std::abs(y + h - sb) < t)  { y = sb - h;   sy = true; }

    // Inter-window snap
    for (auto& overlay : Manager::Get().m_floating_overlays)
    {
        if (!overlay || overlay.get() == this || !overlay->IsAlive() || !overlay->GetHwnd()) continue;
        RECT orc;
        if (!::GetWindowRect(overlay->GetHwnd(), &orc)) continue;
        float ol = (float)orc.left, ot2 = (float)orc.top;
        float or2 = (float)orc.right, ob = (float)orc.bottom;
        if (!sx) {
            if (std::abs(x + w - ol) < t)  { x = ol - w; sx = true; }
            else if (std::abs(x - or2) < t) { x = or2;    sx = true; }
        }
        if (!sy) {
            if (std::abs(y + h - ot2) < t) { y = ot2 - h; sy = true; }
            else if (std::abs(y - ob) < t)  { y = ob;       sy = true; }
        }
    }

    SnapEdge edge = SnapEdge::None;
    if      (sx && sy && x <= sl + 1 && y <= st + 1)     edge = SnapEdge::Corner_TopLeft;
    else if (sx && sy && x >= sr - w - 1 && y <= st + 1) edge = SnapEdge::Corner_TopRight;
    else if (sx && sy && x <= sl + 1)                     edge = SnapEdge::Corner_BottomLeft;
    else if (sx && sy)                                     edge = SnapEdge::Corner_BottomRight;
    else if (sx) edge = (x <= sl + 1) ? SnapEdge::Left : SnapEdge::Right;
    else if (sy) edge = (y <= st + 1) ? SnapEdge::Top  : SnapEdge::Bottom;
    m_snap_edge = edge;

    rc.left   = (LONG)x;  rc.top    = (LONG)y;
    rc.right  = (LONG)(x + w); rc.bottom = (LONG)(y + h);
}


void Window::Show(bool cascade_to_children)
{
    if (m_hwnd)
    {
        ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        if (m_config.is_topmost)
        {
            ::SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        ::UpdateWindow(m_hwnd);
    }

    if (cascade_to_children)
    {
        for (const auto& child_id : m_child_ids)
        {
            auto* child = Manager::Get().GetFloatingOverlay(child_id);
            if (child && child->GetConfig().hide_with_parent)
                child->Show(true);
        }
    }
}

void Window::Hide(bool cascade_to_children)
{
    if (m_hwnd)
    {
        ::ShowWindow(m_hwnd, SW_HIDE);
    }

    if (cascade_to_children)
    {
        for (const auto& child_id : m_child_ids)
        {
            auto* child = Manager::Get().GetFloatingOverlay(child_id);
            if (child && child->GetConfig().hide_with_parent)
                child->Hide(true);
        }
    }
}

void Window::SetVisible(bool visible, bool cascade_to_children)
{
    if (visible)
        Show(cascade_to_children);
    else
        Hide(cascade_to_children);
}

bool Window::IsVisible() const
{
    return m_hwnd ? (::IsWindowVisible(m_hwnd) != FALSE) : false;
}

void Window::Minimize(bool cascade_to_children)
{
    if (m_hwnd)
        ::ShowWindow(m_hwnd, SW_MINIMIZE);

    if (cascade_to_children)
    {
        for (const auto& child_id : m_child_ids)
        {
            auto* child = Manager::Get().GetFloatingOverlay(child_id);
            if (child && child->GetConfig().minimize_with_parent)
                child->Minimize(true);
        }
    }
}

void Window::Maximize()
{
    if (m_hwnd)
        ::ShowWindow(m_hwnd, SW_MAXIMIZE);
}

void Window::Restore(bool cascade_to_children)
{
    if (m_hwnd)
    {
        ::ShowWindow(m_hwnd, SW_RESTORE);
        if (m_config.is_topmost)
        {
            ::SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }

    if (cascade_to_children)
    {
        for (const auto& child_id : m_child_ids)
        {
            auto* child = Manager::Get().GetFloatingOverlay(child_id);
            if (child && child->GetConfig().minimize_with_parent)
                child->Restore(true);
        }
    }
}

bool Window::IsMinimized() const
{
    return m_hwnd ? (::IsIconic(m_hwnd) != FALSE) : false;
}

bool Window::IsMaximized() const
{
    return m_hwnd ? (::IsZoomed(m_hwnd) != FALSE) : false;
}

void Window::SetWindowTitle(const std::string& title)
{
    m_config.window_title = title;
    if (m_hwnd)
    {
        int title_len = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
        std::wstring wtitle(title_len, 0);
        MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, &wtitle[0], title_len);
        ::SetWindowTextW(m_hwnd, wtitle.c_str());
    }
}

void Window::SetTopmost(bool topmost)
{
    m_config.is_topmost = topmost;
    if (m_hwnd)
    {
        HWND insert_after = m_config.is_topmost ? HWND_TOPMOST : HWND_NOTOPMOST;
        ::SetWindowPos(m_hwnd, insert_after, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void Window::SetTaskbarVisible(bool visible)
{
    m_config.hide_from_taskbar = !visible;
    if (m_hwnd)
    {
        LONG_PTR ex = ::GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);
        if (visible)
            ex = (ex & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW;
        else
            ex = (ex & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW;
        ::SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, ex);
        ::SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
}

void Window::SetCaptureHidden(bool hide)
{
    m_config.exclude_from_capture = hide;
    if (m_hwnd && ::IsWindow(m_hwnd))
    {
        ::SetWindowDisplayAffinity(m_hwnd, hide ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
    }
}

void Window::SetClickThrough(bool click_through)
{
    m_config.is_click_through = click_through;
    if (m_hwnd)
    {
        LONG_PTR ex = ::GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);
        if (click_through)
            ex |= WS_EX_TRANSPARENT;
        else
            ex &= ~WS_EX_TRANSPARENT;
        ::SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, ex);
    }
}

void Window::SetPosition(int x, int y)
{
    m_current_screen_pos = ImVec2((float)x, (float)y);
    m_target_screen_pos = m_current_screen_pos;
    if (m_hwnd)
    {
        ::SetWindowPos(m_hwnd, nullptr, x, y, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_on_move_cb)
        m_on_move_cb(this, x, y);

    // Notify all child sub-windows that parent moved
    for (const auto& child_id : m_child_ids)
    {
        auto* child = Manager::Get().GetFloatingOverlay(child_id);
        if (child && child->GetConfig().follow_parent_movement)
        {
            child->OnParentMoved(x, y);
        }
    }
}

void Window::OnParentMoved(int parent_x, int parent_y)
{
    if (m_config.anchor == AnchorMode::RelativeToParentWindow)
    {
        int new_x = parent_x + (int)m_config.offset_from_parent.x;
        int new_y = parent_y + (int)m_config.offset_from_parent.y;
        SetPosition(new_x, new_y);
    }
}

void Window::SetSize(int width, int height)
{
    m_config.size = ImVec2((float)width, (float)height);
    m_window_size = ImVec2(
        (float)width + m_config.padding.x + m_config.padding.z,
        (float)height + m_config.padding.y + m_config.padding.w
    );

    if (m_hwnd)
    {
        ::SetWindowPos(m_hwnd, nullptr, 0, 0,
                       (int)m_window_size.x, (int)m_window_size.y,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        ResizeBuffers((int)m_window_size.x, (int)m_window_size.y);
    }
    if (m_on_resize_cb)
        m_on_resize_cb(this, width, height);
}

void Window::SetAnchor(AnchorMode anchor, const ImVec2& margin)
{
    m_config.anchor = anchor;
    CalculateScreenPosition(margin);
    SetPosition((int)m_target_screen_pos.x, (int)m_target_screen_pos.y);
}

void Window::SetOpacity(float alpha)
{
    m_alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (m_hwnd)
    {
        BYTE byte_alpha = (BYTE)(m_alpha * 255.f);
        ::SetLayeredWindowAttributes(m_hwnd, 0, byte_alpha, LWA_ALPHA);
    }
}

void Window::AddChild(const std::string& child_id)
{
    if (std::find(m_child_ids.begin(), m_child_ids.end(), child_id) == m_child_ids.end())
    {
        m_child_ids.push_back(child_id);
    }
}

void Window::RemoveChild(const std::string& child_id)
{
    auto it = std::find(m_child_ids.begin(), m_child_ids.end(), child_id);
    if (it != m_child_ids.end())
    {
        m_child_ids.erase(it);
    }
}

void Window::Close(bool cascade_to_children)
{
    m_closing = true;

    if (cascade_to_children)
    {
        for (const auto& child_id : m_child_ids)
        {
            auto* child = Manager::Get().GetFloatingOverlay(child_id);
            if (child && child->GetConfig().close_with_parent)
                child->Close(true);
        }
    }

    if (m_on_close_cb)
        m_on_close_cb(this);
}

void Window::DestroyNow(bool cascade_to_children)
{
    m_is_alive = false;

    if (cascade_to_children)
    {
        for (const auto& child_id : m_child_ids)
        {
            auto* child = Manager::Get().GetFloatingOverlay(child_id);
            if (child && child->GetConfig().close_with_parent)
                child->DestroyNow(true);
        }
    }

    if (m_on_close_cb)
        m_on_close_cb(this);
}

void Window::ResizeBuffers(int width, int height)
{
    if (width <= 0 || height <= 0 || !m_swap_chain || !m_device)
        return;

    if (m_rtv)
    {
        m_rtv->Release();
        m_rtv = nullptr;
    }

    m_swap_chain->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))))
    {
        m_device->CreateRenderTargetView(pBackBuffer, nullptr, &m_rtv);
        pBackBuffer->Release();
    }
}

bool Window::Update(float delta_time)
{
    if (!m_is_alive)
        return false;

    // 1. Auto-close when progress finishes (progress >= 1.0) with configurable delay (e.g. 2.0s)
    if (m_config.auto_dismiss_on_finish && m_progress >= 1.0f)
    {
        m_finish_timer += delta_time;
        if (m_finish_timer >= m_config.finish_dismiss_delay)
        {
            Close(true);
        }
    }

    // 2. Max overall lifetime duration (if specified > 0)
    if (m_config.duration_seconds > 0.0f)
    {
        m_time_alive += delta_time;
        if (m_time_alive >= m_config.duration_seconds)
        {
            Close(true);
        }
    }

    if (m_closing)
    {
        m_anim_progress -= delta_time * 6.0f;
        if (m_anim_progress <= 0.0f)
        {
            m_anim_progress = 0.0f;
            m_is_alive = false;
            return false;
        }
    }
    else
    {
        if (m_anim_progress < 1.0f)
        {
            m_anim_progress += delta_time * 6.0f;
            if (m_anim_progress > 1.0f)
                m_anim_progress = 1.0f;
        }
    }

    if (m_config.duration_seconds > 0.0f || m_closing)
    {
        m_alpha = std::clamp(m_anim_progress, 0.0f, 1.0f);
        if (m_hwnd)
        {
            BYTE byte_alpha = (BYTE)(m_alpha * 255.f);
            ::SetLayeredWindowAttributes(m_hwnd, 0, byte_alpha, LWA_ALPHA);
        }
    }

    return m_is_alive;
}


void Window::Render()
{
    if (!m_is_alive || !m_swap_chain || !m_rtv || !IsVisible() || !m_device)
        return;

    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx)
        return;

    ImDrawListSharedData* shared_data = ImGui::GetDrawListSharedData();
    if (!shared_data)
        return;

    // Ensure circle tessellation table is initialized
    if (shared_data->CircleSegmentMaxError <= 0.0f)
        shared_data->SetCircleTessellationMaxError(0.30f);

    ImDrawList draw_list(shared_data);
    draw_list._ResetForNewFrame();
    draw_list.Flags = shared_data->InitialFlags;
    draw_list.PushTextureID(ImGui::GetIO().Fonts->TexID);
    draw_list.PushClipRect(ImVec2(0.f, 0.f), m_window_size, false);

    m_current_draw_list = &draw_list;
    if (m_render_callback)
    {
        m_render_callback(this, ctx->IO.DeltaTime);
    }
    else
    {
        RenderBuiltinProgress(&draw_list);
    }
    m_current_draw_list = nullptr;

    draw_list.PopClipRect();
    draw_list.PopTextureID();

    if (draw_list.VtxBuffer.Size == 0 || draw_list.CmdBuffer.Size == 0)
        return;

    ImDrawData draw_data;
    draw_data.Valid = true;
    draw_data.CmdLists.push_back(&draw_list);
    draw_data.CmdListsCount = 1;
    draw_data.TotalVtxCount = draw_list.VtxBuffer.Size;
    draw_data.TotalIdxCount = draw_list.IdxBuffer.Size;
    draw_data.DisplayPos = ImVec2(0.f, 0.f);
    draw_data.DisplaySize = m_window_size;
    draw_data.FramebufferScale = ImVec2(1.f, 1.f);
    draw_data.OwnerViewport = nullptr;

    ID3D11DeviceContext* context = nullptr;
    m_device->GetImmediateContext(&context);
    if (context)
    {
        // Backup active render target & viewport so main window is 100% untouched
        ID3D11RenderTargetView* prev_rtv = nullptr;
        ID3D11DepthStencilView* prev_dsv = nullptr;
        context->OMGetRenderTargets(1, &prev_rtv, &prev_dsv);

        UINT prev_vp_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        D3D11_VIEWPORT prev_vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        context->RSGetViewports(&prev_vp_count, prev_vps);

        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        context->OMSetRenderTargets(1, &m_rtv, nullptr);
        context->ClearRenderTargetView(m_rtv, clear_color);

        D3D11_VIEWPORT vp;
        vp.Width = m_window_size.x;
        vp.Height = m_window_size.y;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;
        context->RSSetViewports(1, &vp);

        ImGui_ImplDX11_RenderDrawData(&draw_data);
        m_swap_chain->Present(0, 0);

        // Restore previous render target & viewport
        context->OMSetRenderTargets(1, &prev_rtv, prev_dsv);
        if (prev_vp_count > 0)
            context->RSSetViewports(prev_vp_count, prev_vps);

        if (prev_rtv) prev_rtv->Release();
        if (prev_dsv) prev_dsv->Release();
        context->Release();
    }
}

void Window::RenderBuiltinProgress(ImDrawList* draw_list)
{
    if (!draw_list)
        return;

    ImVec2 p(m_config.padding.x, m_config.padding.y);
    ImVec2 s = m_config.size;
    ImVec2 max_pt(p.x + s.x, p.y + s.y);

    float card_radius = (std::clamp)(m_config.corner_radius, 0.0f, (std::min)(s.x, s.y) * 0.5f);

    if (m_config.draw_default_card_bg)
    {
        // bg
        draw_list->AddRectFilled(p, max_pt, m_config.custom_bg_color, card_radius);

        // border
        draw_list->AddRect(p, max_pt, m_config.custom_border_color, card_radius, 0, m_config.border_thickness);
    }

    ImFont* font = m_config.custom_font ? m_config.custom_font : ImGui::GetFont();

    if (!m_message.empty())
    {
        // --- Standalone Toast Notification Rendering ---
        // Left accent bar
        ImVec2 bar_max(p.x + 4.f, max_pt.y);
        draw_list->AddRectFilled(p, bar_max, m_config.custom_accent_color, card_radius, ImDrawFlags_RoundCornersLeft);

        // Title
        ImVec2 label_pos(p.x + 16.f, p.y + 12.f);
        if (font)
            draw_list->AddText(font, font->FontSize, label_pos, m_config.custom_text_color, m_title.c_str());
        else
            draw_list->AddText(label_pos, m_config.custom_text_color, m_title.c_str());

        // Message
        ImVec2 msg_pos(p.x + 16.f, p.y + 34.f);
        ImU32 msg_color = IM_COL32(185, 185, 195, 230);
        if (font)
            draw_list->AddText(font, font->FontSize * 0.9f, msg_pos, msg_color, m_message.c_str());
        else
            draw_list->AddText(msg_pos, msg_color, m_message.c_str());

        // Progress countdown bar (if duration > 0)
        if (m_config.duration_seconds > 0.0f)
        {
            float countdown = std::clamp(1.0f - (m_time_alive / m_config.duration_seconds), 0.0f, 1.0f);
            float track_w = s.x - 28.f;
            ImVec2 bar_pos(p.x + 14.f, max_pt.y - 5.f);
            draw_list->AddRectFilled(bar_pos, ImVec2(bar_pos.x + track_w, bar_pos.y + 2.f), m_config.custom_track_color, 1.f);
            draw_list->AddRectFilled(bar_pos, ImVec2(bar_pos.x + track_w * countdown, bar_pos.y + 2.f), m_config.custom_accent_color, 1.f);
        }
    }
    else
    {
        // --- Built-in Progress Card Rendering ---
        ImVec2 label_pos(p.x + 32.f, p.y + 16.f);
        ImVec2 icon_pos(p.x + 17.f, p.y + 18.f);

        if (font)
            draw_list->AddText(font, font->FontSize, label_pos, m_config.custom_text_color, m_title.c_str());
        else
            draw_list->AddText(label_pos, m_config.custom_text_color, m_title.c_str());

        if (m_config.custom_icon_font && !m_icon.empty())
            draw_list->AddText(m_config.custom_icon_font, m_config.custom_icon_font->FontSize, icon_pos, m_config.custom_accent_color, m_icon.c_str());

        // Percentage
        std::string prog = (std::to_string(int(m_progress * 100)) + "%");
        ImVec2 prog_size(40.f, 14.f);
        if (font)
            prog_size = font->CalcTextSizeA(font->FontSize, FLT_MAX, -1.f, prog.c_str(), 0, NULL);
        ImVec2 prog_pos(p.x + s.x - 16.f - prog_size.x, p.y + 16.f);

        if (font)
            draw_list->AddText(font, font->FontSize, prog_pos, m_config.custom_accent_color, prog.c_str());
        else
            draw_list->AddText(prog_pos, m_config.custom_accent_color, prog.c_str());

        // Progress Bar Track & Dynamic Fill (height = 4px, rounding = 2px)
        ImVec2 prog_bar_pos(p.x + 16.f, p.y + 38.f);
        float track_w = s.x - 32.f;
        ImVec2 prog_bar_size(track_w, 4.f);
        ImVec2 prog_bar_size_dynamic(4.f + (track_w - 4.f) * m_progress, 4.f);

        draw_list->AddRectFilled(prog_bar_pos, ImVec2(prog_bar_pos.x + prog_bar_size.x, prog_bar_pos.y + prog_bar_size.y), m_config.custom_track_color, 2.0f);
        draw_list->AddRectFilled(prog_bar_pos, ImVec2(prog_bar_pos.x + prog_bar_size_dynamic.x, prog_bar_pos.y + prog_bar_size_dynamic.y), m_config.custom_accent_color, 2.0f);
    }
}

LRESULT CALLBACK Window::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Window* self = (Window*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_NCHITTEST:
    {
        if (!self || self->m_config.is_click_through)
            return HTTRANSPARENT;

        POINT pt = { (short)GET_X_LPARAM(lParam), (short)GET_Y_LPARAM(lParam) };
        ::ScreenToClient(hWnd, &pt);
        ImVec2 local_pt((float)pt.x, (float)pt.y);

        ImRect content_rect(
            self->m_config.padding.x,
            self->m_config.padding.y,
            self->m_window_size.x - self->m_config.padding.z,
            self->m_window_size.y - self->m_config.padding.w
        );

        if (!content_rect.Contains(local_pt))
        {
            return HTTRANSPARENT;
        }

        // Check if cursor is on a clickable interactive region (buttons, inputs)
        if (!self->m_config.clickable_regions.empty())
        {
            for (const auto& r : self->m_config.clickable_regions)
            {
                if (r.Contains(local_pt))
                    return HTCLIENT;
            }
        }

        // Check if cursor is on designated drag header regions
        if (!self->m_config.drag_regions.empty())
        {
            for (const auto& r : self->m_config.drag_regions)
            {
                if (r.Contains(local_pt))
                    return self->m_config.is_movable ? HTCAPTION : HTCLIENT;
            }
            return HTCLIENT;
        }

        if (self->m_config.is_movable)
        {
            return HTCAPTION;
        }

        return HTCLIENT;
    }

    case WM_ENTERSIZEMOVE:
        ::SetTimer(hWnd, 1002, 15, nullptr); // Render continuously during window drag
        return 0;

    case WM_TIMER:
        if (wParam == 1002 && self)
        {
            self->Render();
            return 0;
        }
        break;

    case WM_MOVING:
        // Feature 2: Magnetic snapping - lParam is LPRECT, modify in-place
        if (self && self->m_config.enable_snap)
        {
            RECT* prc = reinterpret_cast<RECT*>(lParam);
            if (prc) self->SnapWindowPosition(*prc);
        }
        // fall-through to WM_MOVE to update position state
    case WM_MOVE:
        if (self)
        {
            RECT rc;
            ::GetWindowRect(hWnd, &rc);
            self->m_current_screen_pos = ImVec2((float)rc.left, (float)rc.top);
            if (self->m_on_move_cb)
                self->m_on_move_cb(self, rc.left, rc.top);

            // Notify attached sub-windows
            for (const auto& child_id : self->m_child_ids)
            {
                auto* child = Manager::Get().GetFloatingOverlay(child_id);
                if (child && child->GetConfig().follow_parent_movement)
                    child->OnParentMoved(rc.left, rc.top);
            }
            self->Render();
        }
        return 0;

    case WM_EXITSIZEMOVE:
        ::KillTimer(hWnd, 1002);
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================================
// Manager Implementation
// ============================================================================

Manager::~Manager()
{
    StopCaptureMonitor();
    StopHotkeyListener();
}

void Manager::Init(HWND hwnd, const ImVec2& initial_pos, const ImVec2& menu_size)
{
    (void)initial_pos;
    m_hwnd = hwnd;
    m_menu_size = menu_size;
    m_target_width = menu_size.x + m_padding.x + m_padding.z;
    m_target_height = menu_size.y + m_padding.y + m_padding.w;
    m_current_width = m_target_width;
    m_current_height = m_target_height;

    // Register the main menu base rectangle in local window client coordinates
    ImRect menu_rect(
        m_padding.x,
        m_padding.y,
        m_padding.x + menu_size.x,
        m_padding.y + menu_size.y
    );
    RegisterElementRect("main_menu", menu_rect, true);
}

void Manager::SetD3DObjects(IDXGISwapChain* swap_chain, ID3D11Device* device, ID3D11RenderTargetView** rtv)
{
    m_swap_chain = swap_chain;
    m_d3d_device = device;
    m_main_rtv = rtv;
}

void Manager::SetTopmost(bool topmost)
{
    m_is_topmost = topmost;
    if (m_hwnd)
    {
        HWND insert_after = topmost ? HWND_TOPMOST : HWND_NOTOPMOST;
        ::SetWindowPos(m_hwnd, insert_after, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// --- Screen Capture Protection & Background Monitor ---

void Manager::SetMainCaptureHidden(bool hide)
{
    m_main_exclude_from_capture = hide;
    if (m_hwnd && ::IsWindow(m_hwnd))
    {
        SetCaptureHidden(m_hwnd, hide);
    }
}

void Manager::SetCaptureHidden(HWND hwnd, bool hide)
{
    if (hwnd && ::IsWindow(hwnd))
    {
        ::SetWindowDisplayAffinity(hwnd, hide ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
        std::lock_guard<std::mutex> lock(m_hidden_windows_mutex);
        if (hide)
            m_hidden_capture_windows.insert(hwnd);
        else
            m_hidden_capture_windows.erase(hwnd);
    }
}

void Manager::SetCaptureHidden(const std::string& window_id, bool hide)
{
    if (window_id == "main_menu")
    {
        SetMainCaptureHidden(hide);
        return;
    }

    Window* win = GetFloatingOverlay(window_id);
    if (win)
    {
        win->SetCaptureHidden(hide);
        if (win->GetHwnd())
        {
            std::lock_guard<std::mutex> lock(m_hidden_windows_mutex);
            if (hide)
                m_hidden_capture_windows.insert(win->GetHwnd());
            else
                m_hidden_capture_windows.erase(win->GetHwnd());
        }
    }
}

bool Manager::IsCaptureHidden(const std::string& window_id) const
{
    if (window_id == "main_menu")
        return m_main_exclude_from_capture;

    for (const auto& overlay : m_floating_overlays)
    {
        if (overlay && overlay->GetId() == window_id && overlay->IsAlive())
        {
            return overlay->IsCaptureHidden();
        }
    }
    return false;
}

void Manager::SetCaptureHiddenAll(bool hide)
{
    SetMainCaptureHidden(hide);

    for (auto& overlay : m_floating_overlays)
    {
        if (overlay)
        {
            overlay->SetCaptureHidden(hide);
            if (overlay->GetHwnd())
            {
                std::lock_guard<std::mutex> lock(m_hidden_windows_mutex);
                if (hide)
                    m_hidden_capture_windows.insert(overlay->GetHwnd());
                else
                    m_hidden_capture_windows.erase(overlay->GetHwnd());
            }
        }
    }
}

void Manager::StartCaptureMonitor(uint32_t poll_interval_ms)
{
    if (m_monitor_running.load())
        return;

    m_monitor_running = true;
    m_monitor_thread = std::thread(&Manager::CaptureMonitorLoop, this, poll_interval_ms);
}

void Manager::StopCaptureMonitor()
{
    m_monitor_running = false;
    if (m_monitor_thread.joinable())
    {
        m_monitor_thread.join();
    }
}

void Manager::CaptureMonitorLoop(uint32_t poll_interval_ms)
{
    while (m_monitor_running.load())
    {
        // Enforce capture exclusion on main menu only if configured
        if (m_hwnd && ::IsWindow(m_hwnd))
        {
            ::SetWindowDisplayAffinity(m_hwnd, m_main_exclude_from_capture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
        }

        // Enforce capture exclusion individually per floating overlay
        for (auto& overlay : m_floating_overlays)
        {
            if (overlay && overlay->GetHwnd() && ::IsWindow(overlay->GetHwnd()))
            {
                ::SetWindowDisplayAffinity(
                    overlay->GetHwnd(),
                    overlay->IsCaptureHidden() ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE
                );
            }
        }

        // Clean up any stale destroyed HWNDs
        {
            std::lock_guard<std::mutex> lock(m_hidden_windows_mutex);
            for (auto it = m_hidden_capture_windows.begin(); it != m_hidden_capture_windows.end();)
            {
                if (!::IsWindow(*it))
                    it = m_hidden_capture_windows.erase(it);
                else
                    ++it;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    }
}

void Manager::SetMainTaskbarVisible(bool visible)
{
    m_main_hide_from_taskbar = !visible;
    if (m_hwnd && ::IsWindow(m_hwnd))
    {
        LONG_PTR ex = ::GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);
        if (visible)
            ex = (ex & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW;
        else
            ex = (ex & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW;
        ::SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, ex);
        ::SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
}

void Manager::SetTaskbarVisible(const std::string& window_id, bool visible)
{
    if (window_id == "main_menu")
    {
        SetMainTaskbarVisible(visible);
        return;
    }

    Window* win = GetFloatingOverlay(window_id);
    if (win)
    {
        win->SetTaskbarVisible(visible);
    }
}

bool Manager::IsTaskbarVisible(const std::string& window_id) const
{
    if (window_id == "main_menu")
        return !m_main_hide_from_taskbar;

    for (const auto& overlay : m_floating_overlays)
    {
        if (overlay && overlay->GetId() == window_id && overlay->IsAlive())
        {
            return overlay->IsTaskbarVisible();
        }
    }
    return false;
}

void Manager::SetTaskbarVisibleAll(bool visible)
{
    SetMainTaskbarVisible(visible);

    for (auto& overlay : m_floating_overlays)
    {
        if (overlay)
            overlay->SetTaskbarVisible(visible);
    }
}

void Manager::RegisterElement(const std::string& name, const ImVec2& pos, const ImVec2& size, bool interactive)
{
    ImRect rect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);
    RegisterElementRect(name, rect, interactive);
}

void Manager::RegisterElementRect(const std::string& name, const ImRect& rect, bool interactive)
{
    Element elem;
    elem.name = name;
    elem.rect = rect;
    elem.is_interactive = interactive;
    elem.is_active = true;
    m_elements[name] = elem;
}

void Manager::UnregisterElement(const std::string& name)
{
    m_elements.erase(name);
}

void Manager::SetElementActive(const std::string& name, bool active)
{
    auto it = m_elements.find(name);
    if (it != m_elements.end())
    {
        it->second.is_active = active;
    }
}

void Manager::SetElementInteractive(const std::string& name, bool interactive)
{
    auto it = m_elements.find(name);
    if (it != m_elements.end())
    {
        it->second.is_interactive = interactive;
    }
}

void Manager::RecalculateBounds()
{
    float min_x = m_padding.x;
    float min_y = m_padding.y;
    float max_x = m_padding.x + m_menu_size.x;
    float max_y = m_padding.y + m_menu_size.y;

    bool outside = false;

    for (const auto& pair : m_elements)
    {
        const auto& elem = pair.second;
        if (!elem.is_active)
            continue;

        if (elem.name == "main_menu")
            continue;

        outside = true;
        if (elem.rect.Min.x < min_x) min_x = elem.rect.Min.x;
        if (elem.rect.Min.y < min_y) min_y = elem.rect.Min.y;
        if (elem.rect.Max.x > max_x) max_x = elem.rect.Max.x;
        if (elem.rect.Max.y > max_y) max_y = elem.rect.Max.y;
    }

    m_has_outside_elements = outside;
    m_target_width = max_x + m_padding.z;
    m_target_height = max_y + m_padding.w;

    if (m_auto_topmost)
    {
        SetTopmost(outside);
    }
}

void Manager::ApplyWindowResize(int width, int height)
{
    if (!m_hwnd || width <= 0 || height <= 0)
        return;

    // Expand the physical Win32 OS window
    ::SetWindowPos(
        m_hwnd,
        nullptr,
        0, 0,
        width, height,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
    );

    // Resize DirectX 11 SwapChain BackBuffers
    if (m_swap_chain && m_d3d_device && m_main_rtv)
    {
        if (*m_main_rtv)
        {
            (*m_main_rtv)->Release();
            *m_main_rtv = nullptr;
        }

        m_swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

        ID3D11Texture2D* pBackBuffer = nullptr;
        if (SUCCEEDED(m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))))
        {
            m_d3d_device->CreateRenderTargetView(pBackBuffer, nullptr, m_main_rtv);
            pBackBuffer->Release();
        }
    }
}

void Manager::BeginFrame()
{
    RecalculateBounds();
}

void Manager::EndFrame(float delta_time)
{
    if (m_transition_mode == TransitionMode::Instant)
    {
        if (std::abs(m_current_width - m_target_width) > 0.5f ||
            std::abs(m_current_height - m_target_height) > 0.5f)
        {
            m_current_width = m_target_width;
            m_current_height = m_target_height;
            ApplyWindowResize((int)m_current_width, (int)m_current_height);
        }
    }
    else
    {
        // Smooth exponential interpolation
        float rate = 1.0f - std::exp(-m_transition_speed * delta_time);
        m_current_width += (m_target_width - m_current_width) * rate;
        m_current_height += (m_target_height - m_current_height) * rate;

        if (std::abs(m_current_width - m_target_width) > 1.0f ||
            std::abs(m_current_height - m_target_height) > 1.0f)
        {
            ApplyWindowResize((int)m_current_width, (int)m_current_height);
        }
    }

    UpdateToasts(delta_time);
    RenderToasts();
    UpdateFloatingOverlays(delta_time);
}

LRESULT Manager::HandleHitTest(LPARAM lParam)
{
    POINT pt = { (short)GET_X_LPARAM(lParam), (short)GET_Y_LPARAM(lParam) };
    ::ScreenToClient(m_hwnd, &pt);

    ImVec2 local_pt((float)pt.x, (float)pt.y);

    // 1. Check specific interactive UI sub-elements (like modal dialogs, buttons) before background window
    for (const auto& pair : m_elements)
    {
        if (pair.first == "main_menu")
            continue;

        const auto& elem = pair.second;
        if (elem.is_active && elem.is_interactive && elem.rect.Contains(local_pt))
        {
            return HTCLIENT;
        }
    }

    // 2. Check main window drag surface
    auto it = m_elements.find("main_menu");
    if (it != m_elements.end() && it->second.is_active && it->second.rect.Contains(local_pt))
    {
        if (ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive())
        {
            return HTCLIENT;
        }

        return HTCAPTION;
    }

    return HTTRANSPARENT;
}

// --- Detached Floating Overlay Windows & Hierarchy ---

Window* Manager::CreateFloatingOverlay(const std::string& id,
                                       const Config& config,
                                       Window::RenderCallback callback)
{
    for (auto& overlay : m_floating_overlays)
    {
        if (overlay->GetId() == id)
        {
            overlay->SetRenderCallback(callback);
            return overlay.get();
        }
    }

    auto new_overlay = std::make_unique<Window>(id, m_d3d_device, m_dxgi_factory, config);
    new_overlay->SetRenderCallback(callback);
    Window* ptr = new_overlay.get();

    if (!config.parent_id.empty() && config.parent_id != "main_menu")
    {
        Window* parent = GetFloatingOverlay(config.parent_id);
        if (parent)
        {
            parent->AddChild(id);
        }
    }

    m_floating_overlays.push_back(std::move(new_overlay));
    return ptr;
}

Window* Manager::CreateSubWindow(const std::string& parent_id,
                                 const std::string& child_id,
                                 const Config& config,
                                 Window::RenderCallback callback)
{
    Config cfg = config;
    cfg.parent_id = parent_id;

    Window* child = CreateFloatingOverlay(child_id, cfg, callback);
    if (child)
    {
        Window* parent = GetFloatingOverlay(parent_id);
        if (parent)
        {
            parent->AddChild(child_id);
        }
    }
    return child;
}

std::vector<std::string> Manager::GetChildrenOf(const std::string& parent_id) const
{
    std::vector<std::string> result;
    for (const auto& overlay : m_floating_overlays)
    {
        if (overlay->IsAlive() && overlay->GetParentId() == parent_id)
        {
            result.push_back(overlay->GetId());
        }
    }
    return result;
}

void Manager::CloseWindowHierarchy(const std::string& root_id)
{
    Window* root = GetFloatingOverlay(root_id);
    if (root)
    {
        root->Close(true);
    }
}

Window* Manager::ShowDetachedProgress(const std::string& title,
                                      const std::string& icon,
                                      float progress,
                                      const Config& config)
{
    std::string id = "detached_progress";
    Window* overlay = GetFloatingOverlay(id);
    if (!overlay)
    {
        Config cfg = config;
        cfg.is_topmost = true;
        cfg.size = ImVec2(340.f, 80.f);
        overlay = CreateFloatingOverlay(id, cfg, nullptr);
    }
    if (overlay)
    {
        overlay->SetTopmost(true);
        overlay->SetProgressData(title, icon, progress);
    }
    return overlay;
}

void Manager::ShowDetachedToast(const std::string& title, const std::string& message,
                                const Config& config)
{
    (void)message;
    std::string id = "toast_" + title;
    Config cfg = config;
    if (cfg.duration_seconds < 0.0f)
        cfg.duration_seconds = 4.0f;

    CreateFloatingOverlay(id, cfg, nullptr);
}

void Manager::CloseFloatingOverlay(const std::string& id)
{
    for (auto& overlay : m_floating_overlays)
    {
        if (overlay->GetId() == id)
        {
            overlay->Close();
            break;
        }
    }
}

Window* Manager::GetFloatingOverlay(const std::string& id)
{
    for (auto& overlay : m_floating_overlays)
    {
        if (overlay->GetId() == id && overlay->IsAlive())
        {
            return overlay.get();
        }
    }
    return nullptr;
}

bool Manager::HasFloatingOverlay(const std::string& id) const
{
    for (const auto& overlay : m_floating_overlays)
    {
        if (overlay->GetId() == id && overlay->IsAlive())
            return true;
    }
    return false;
}

void Manager::DestroyFloatingOverlay(const std::string& id)
{
    for (auto it = m_floating_overlays.begin(); it != m_floating_overlays.end(); ++it)
    {
        if ((*it)->GetId() == id)
        {
            (*it)->DestroyNow();
            m_floating_overlays.erase(it);
            break;
        }
    }
}

void Manager::CloseAllFloatingOverlays()
{
    for (auto& overlay : m_floating_overlays)
    {
        overlay->Close();
    }
}

void Manager::HideAllFloatingOverlays()
{
    for (auto& overlay : m_floating_overlays)
    {
        overlay->Hide();
    }
}

void Manager::ShowAllFloatingOverlays()
{
    for (auto& overlay : m_floating_overlays)
    {
        overlay->Show();
    }
}

void Manager::MinimizeAllFloatingOverlays()
{
    for (auto& overlay : m_floating_overlays)
    {
        overlay->Minimize();
    }
}

void Manager::RestoreAllFloatingOverlays()
{
    for (auto& overlay : m_floating_overlays)
    {
        overlay->Restore();
    }
}

std::vector<std::string> Manager::GetFloatingOverlayIds() const
{
    std::vector<std::string> ids;
    ids.reserve(m_floating_overlays.size());
    for (const auto& overlay : m_floating_overlays)
    {
        if (overlay->IsAlive())
            ids.push_back(overlay->GetId());
    }
    return ids;
}

void Manager::UpdateFloatingOverlays(float delta_time)
{
    // Collect all active toast windows in creation order (oldest -> newest)
    std::vector<Window*> active_toasts;
    for (auto& overlay : m_floating_overlays)
    {
        if (overlay && overlay->IsAlive() && overlay->GetId().rfind("toast_", 0) == 0)
        {
            active_toasts.push_back(overlay.get());
        }
    }

    // Dynamic upward stacking:
    // Newest toast (last in active_toasts) gets slot 0 (at the bottom).
    // Older active toasts get slot 1, 2, 3... and are shifted UPWARDS above newer ones!
    int count = (int)active_toasts.size();
    for (int i = 0; i < count; ++i)
    {
        int slot = (count - 1) - i;
        Window* toast = active_toasts[i];
        if (toast && !toast->IsClosing())
        {
            float target_offset_y = 20.f + (float)slot * (toast->GetConfig().size.y + toast->GetConfig().padding.y + toast->GetConfig().padding.w + 8.f);
            toast->SetAnchor(toast->GetConfig().anchor, ImVec2(20.f, target_offset_y));
        }
    }

    for (auto it = m_floating_overlays.begin(); it != m_floating_overlays.end();)
    {
        if (!(*it)->Update(delta_time))
        {
            it = m_floating_overlays.erase(it);
        }
        else
        {
            (*it)->Render();
            ++it;
        }
    }
}


// ============================================================================
// Feature 3: Multi-Toast Queue & Stacking Engine
// ============================================================================

void Manager::PushToast(const std::string& title, const std::string& message,
                        float duration, ImU32 accent, AnchorMode anchor)
{
    static int toast_counter = 0;
    std::string id = "toast_" + std::to_string(++toast_counter);

    // Count currently active toast windows to calculate vertical stack offset on the desktop screen
    int active_toasts = 0;
    for (const auto& ov : m_floating_overlays)
    {
        if (ov && ov->GetId().rfind("toast_", 0) == 0 && ov->IsAlive())
            active_toasts++;
    }

    Config cfg;
    cfg.window_title = "Notification";
    cfg.anchor = anchor;
    cfg.size = ImVec2(340.f, 76.f);
    cfg.padding = ImVec4(12.f, 12.f, 12.f, 12.f);
    cfg.duration_seconds = duration > 0.f ? duration : 4.0f;
    cfg.is_topmost = true;
    cfg.hide_from_taskbar = true;
    cfg.is_click_through = false;
    cfg.is_movable = true;
    cfg.draw_default_card_bg = true;
    cfg.custom_accent_color = accent;

    // Stack vertically from the desktop monitor bottom-right corner
    float stack_offset_y = 24.f + (float)(active_toasts % 6) * (cfg.size.y + cfg.padding.y + cfg.padding.w + 10.f);

    Window* toast_win = CreateFloatingOverlay(id, cfg, nullptr);
    if (toast_win)
    {
        toast_win->SetToastData(title, message, accent);
        toast_win->SetAnchor(anchor, ImVec2(24.f, stack_offset_y));
        toast_win->Show();
    }
}

void Manager::DismissToast(const std::string& title)
{
    for (auto& ov : m_floating_overlays)
    {
        if (ov && ov->IsAlive() && ov->GetTitle() == title)
            ov->Close();
    }
}

void Manager::DismissAllToasts()
{
    for (auto& ov : m_floating_overlays)
    {
        if (ov && ov->IsAlive() && ov->GetId().rfind("toast_", 0) == 0)
            ov->Close();
    }
}

size_t Manager::GetToastCount() const
{
    size_t count = 0;
    for (const auto& ov : m_floating_overlays)
    {
        if (ov && ov->IsAlive() && ov->GetId().rfind("toast_", 0) == 0)
            count++;
    }
    return count;
}

void Manager::UpdateToasts(float delta_time)
{
    (void)delta_time;
}

void Manager::RenderToasts()
{
}

// ============================================================================
// Feature 5: Global Hotkey Listener
// ============================================================================

bool Manager::RegisterHotkey(int id, UINT modifiers, UINT vk,
                              HotkeyAction action, std::function<void()> custom_cb)
{
    std::lock_guard<std::mutex> lock(m_hotkey_mutex);
    HotkeyEntry entry{ id, modifiers, vk, action, custom_cb };
    m_hotkeys[id] = std::move(entry);

    // If listener is already running, register immediately on the message loop
    if (m_hotkey_running.load() && m_hotkey_hwnd)
    {
        ::PostMessage(m_hotkey_hwnd, WM_APP + 1, (WPARAM)id, 0);
    }
    return true;
}

void Manager::UnregisterHotkey(int id)
{
    std::lock_guard<std::mutex> lock(m_hotkey_mutex);
    m_hotkeys.erase(id);
    if (m_hotkey_running.load() && m_hotkey_hwnd)
    {
        ::PostMessage(m_hotkey_hwnd, WM_APP + 2, (WPARAM)id, 0);
    }
}

void Manager::UnregisterAllHotkeys()
{
    std::lock_guard<std::mutex> lock(m_hotkey_mutex);
    for (auto& [id, _] : m_hotkeys)
    {
        if (m_hotkey_running.load() && m_hotkey_hwnd)
            ::PostMessage(m_hotkey_hwnd, WM_APP + 2, (WPARAM)id, 0);
    }
    m_hotkeys.clear();
}

void Manager::StartHotkeyListener()
{
    if (m_hotkey_running.load()) return;
    m_hotkey_running = true;
    m_hotkey_thread = std::thread(&Manager::HotkeyMessageLoop, this);
}

void Manager::StopHotkeyListener()
{
    if (!m_hotkey_running.load()) return;
    m_hotkey_running = false;
    if (m_hotkey_hwnd)
        ::PostMessage(m_hotkey_hwnd, WM_QUIT, 0, 0);
    if (m_hotkey_thread.joinable())
        m_hotkey_thread.join();
    m_hotkey_hwnd = nullptr;
}

void Manager::HotkeyMessageLoop()
{
    // Create a message-only window to receive WM_HOTKEY messages
    HINSTANCE hInst = ::GetModuleHandle(nullptr);
    const wchar_t* cls = L"ImOverlay_HotkeyMsgWnd";
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = ::DefWindowProcW;
    wc.hInstance   = hInst;
    wc.lpszClassName = cls;
    ::RegisterClassExW(&wc);

    m_hotkey_hwnd = ::CreateWindowExW(0, cls, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);

    // Register all pending hotkeys
    {
        std::lock_guard<std::mutex> lock(m_hotkey_mutex);
        for (auto& [id, entry] : m_hotkeys)
            ::RegisterHotKey(m_hotkey_hwnd, id, entry.modifiers, entry.vk);
    }

    MSG msg = {};
    while (::GetMessage(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_HOTKEY)
        {
            int id = (int)msg.wParam;
            std::lock_guard<std::mutex> lock(m_hotkey_mutex);
            auto it = m_hotkeys.find(id);
            if (it != m_hotkeys.end())
                DispatchHotkeyAction(it->second);
        }
        else if (msg.message == WM_APP + 1)
        {
            // Register newly added hotkey
            int id = (int)msg.wParam;
            std::lock_guard<std::mutex> lock(m_hotkey_mutex);
            auto it = m_hotkeys.find(id);
            if (it != m_hotkeys.end())
                ::RegisterHotKey(m_hotkey_hwnd, id, it->second.modifiers, it->second.vk);
        }
        else if (msg.message == WM_APP + 2)
        {
            // Unregister removed hotkey
            ::UnregisterHotKey(m_hotkey_hwnd, (int)msg.wParam);
        }
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    // Cleanup hotkeys before exit
    {
        std::lock_guard<std::mutex> lock(m_hotkey_mutex);
        for (auto& [id, _] : m_hotkeys)
            ::UnregisterHotKey(m_hotkey_hwnd, id);
    }
    if (m_hotkey_hwnd)
    {
        ::DestroyWindow(m_hotkey_hwnd);
        m_hotkey_hwnd = nullptr;
    }
    ::UnregisterClassW(cls, hInst);
}

void Manager::DispatchHotkeyAction(const HotkeyEntry& entry)
{
    // Hotkey dispatched from background thread — post actions to be applied on next frame
    // where possible; visibility toggles are safe to do immediately via Win32 API.
    switch (entry.action)
    {
    case HotkeyAction::ToggleVisibility:
    {
        bool any_visible = false;
        for (auto& ov : m_floating_overlays)
            if (ov && ov->IsVisible()) { any_visible = true; break; }
        if (any_visible) HideAllFloatingOverlays();
        else             ShowAllFloatingOverlays();
        break;
    }
    case HotkeyAction::ToggleClickThrough:
    {
        bool any_ct = false;
        for (auto& ov : m_floating_overlays)
            if (ov && ov->IsClickThrough()) { any_ct = true; break; }
        for (auto& ov : m_floating_overlays)
            if (ov) ov->SetClickThrough(!any_ct);
        break;
    }
    case HotkeyAction::ToggleCapture:
        SetCaptureHiddenAll(!IsMainCaptureHidden());
        break;
    case HotkeyAction::CollapseAll:
        MinimizeAllFloatingOverlays();
        break;
    case HotkeyAction::RestoreAll:
        RestoreAllFloatingOverlays();
        break;
    case HotkeyAction::Custom:
        if (entry.custom_cb) entry.custom_cb();
        break;
    }
}

} // namespace ImOverlay
