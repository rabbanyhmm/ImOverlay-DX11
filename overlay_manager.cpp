#include "overlay_manager.h"
#include "backends/imgui_impl_dx11.h"
#include <dwmapi.h>
#include <algorithm>
#include <cmath>

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
    case AnchorMode::Screen_TopRight:
        m_target_screen_pos = ImVec2(work_x + work_w - m_window_size.x - margin.x, work_y + margin.y);
        break;
    case AnchorMode::Screen_BottomLeft:
        m_target_screen_pos = ImVec2(work_x + margin.x, work_y + work_h - m_window_size.y - margin.y);
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

    // Direct3D swapchain creation
    if (m_device && factory)
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

        if (SUCCEEDED(factory->CreateSwapChain(m_device, &sd, &m_swap_chain)))
        {
            ID3D11Texture2D* pBackBuffer = nullptr;
            if (SUCCEEDED(m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))))
            {
                m_device->CreateRenderTargetView(pBackBuffer, nullptr, &m_rtv);
                pBackBuffer->Release();
            }
        }
    }

    if (m_config.is_topmost && m_hwnd)
    {
        ::SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    if (!m_config.start_hidden)
    {
        if (m_config.start_minimized)
            ::ShowWindow(m_hwnd, SW_MINIMIZE);
        else
            ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        ::UpdateWindow(m_hwnd);
    }
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
    if (!m_is_alive || !m_swap_chain || !m_rtv || !IsVisible())
        return;

    if (m_render_callback)
    {
        m_render_callback(this, ImGui::GetIO().DeltaTime);
    }
    else
    {
        RenderBuiltinProgress();
    }
}

void Window::RenderBuiltinProgress()
{
    if (!ImGui::GetCurrentContext())
        return;

    ID3D11DeviceContext* context = nullptr;
    m_device->GetImmediateContext(&context);
    if (!context)
        return;

    ImDrawList draw_list(ImGui::GetDrawListSharedData());
    draw_list._ResetForNewFrame();
    draw_list.PushTextureID(ImGui::GetIO().Fonts->TexID);
    draw_list.PushClipRect(ImVec2(0.f, 0.f), m_window_size, false);

    ImVec2 p(m_config.padding.x, m_config.padding.y);
    ImVec2 s = m_config.size;
    ImVec2 max_pt(p.x + s.x, p.y + s.y);

    if (m_config.draw_default_card_bg)
    {
        // bg
        draw_list.AddRectFilled(p, max_pt, m_config.custom_bg_color, m_config.corner_radius);

        // border
        draw_list.AddRect(p, max_pt, m_config.custom_border_color, m_config.corner_radius, 0, m_config.border_thickness);
    }

    // Title label & Icon
    ImVec2 label_pos(p.x + 32.f, p.y + 16.f);
    ImVec2 icon_pos(p.x + 17.f, p.y + 18.f);

    ImFont* font = m_config.custom_font ? m_config.custom_font : ImGui::GetFont();
    if (font)
        draw_list.AddText(font, font->FontSize, label_pos, m_config.custom_text_color, m_title.c_str());
    else
        draw_list.AddText(label_pos, m_config.custom_text_color, m_title.c_str());

    if (m_config.custom_icon_font && !m_icon.empty())
        draw_list.AddText(m_config.custom_icon_font, m_config.custom_icon_font->FontSize, icon_pos, m_config.custom_accent_color, m_icon.c_str());

    // Percentage
    std::string prog = (std::to_string(int(m_progress * 100)) + "%");
    ImVec2 prog_size(40.f, 14.f);
    if (font)
        prog_size = font->CalcTextSizeA(font->FontSize, FLT_MAX, -1.f, prog.c_str(), 0, NULL);
    ImVec2 prog_pos(p.x + s.x - 16.f - prog_size.x, p.y + 16.f);

    if (font)
        draw_list.AddText(font, font->FontSize, prog_pos, m_config.custom_accent_color, prog.c_str());
    else
        draw_list.AddText(prog_pos, m_config.custom_accent_color, prog.c_str());

    // Progress Bar Track & Dynamic Fill
    ImVec2 prog_bar_pos(p.x + 16.f, p.y + 38.f);
    float track_w = s.x - 32.f;
    ImVec2 prog_bar_size(track_w, 4.f);
    ImVec2 prog_bar_size_dynamic(4.f + (track_w - 4.f) * m_progress, 4.f);

    draw_list.AddRectFilled(prog_bar_pos, ImVec2(prog_bar_pos.x + prog_bar_size.x, prog_bar_pos.y + prog_bar_size.y), m_config.custom_track_color, 512.f);
    draw_list.AddRectFilled(prog_bar_pos, ImVec2(prog_bar_pos.x + prog_bar_size_dynamic.x, prog_bar_pos.y + prog_bar_size_dynamic.y), m_config.custom_accent_color, 512.f);

    draw_list.PopClipRect();
    draw_list.PopTextureID();

    // Package into ImDrawData and render via DirectX 11 backend
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

    m_swap_chain->Present(1, 0);
    context->Release();
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
}

void Manager::Init(HWND hwnd, const ImVec2& initial_pos, const ImVec2& menu_size)
{
    m_hwnd = hwnd;
    m_menu_size = menu_size;
    m_target_width = menu_size.x + m_padding.x + m_padding.z;
    m_target_height = menu_size.y + m_padding.y + m_padding.w;
    m_current_width = m_target_width;
    m_current_height = m_target_height;

    // Register the main menu base rectangle
    ImRect menu_rect(
        initial_pos.x + m_padding.x,
        initial_pos.y + m_padding.y,
        initial_pos.x + m_padding.x + menu_size.x,
        initial_pos.y + m_padding.y + menu_size.y
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

    UpdateFloatingOverlays(delta_time);
}

LRESULT Manager::HandleHitTest(LPARAM lParam)
{
    POINT pt = { (short)GET_X_LPARAM(lParam), (short)GET_Y_LPARAM(lParam) };
    ::ScreenToClient(m_hwnd, &pt);

    ImVec2 local_pt((float)pt.x, (float)pt.y);

    for (const auto& pair : m_elements)
    {
        const auto& elem = pair.second;
        if (!elem.is_active)
            continue;

        if (elem.rect.Contains(local_pt))
        {
            if (elem.is_interactive)
            {
                if (ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive())
                {
                    return HTCLIENT;
                }

                if (elem.name == "main_menu")
                {
                    return HTCAPTION;
                }
                return HTCLIENT;
            }
        }
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

} // namespace ImOverlay
