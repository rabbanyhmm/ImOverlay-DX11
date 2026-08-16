// =============================================================================
// ImOverlay-DX11 — Minimal Demo
// Demonstrates the minimum code to create a floating overlay with toasts,
// Acrylic blur, magnetic snapping, and a global hotkey.
// Build: link against d3d11.lib, dxgi.lib, dwmapi.lib, ImGui
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "../../overlay_manager.h"  // ImOverlay-DX11

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace ImOverlay;

static IDXGISwapChain*         g_SwapChain = nullptr;
static ID3D11Device*           g_Device    = nullptr;
static ID3D11DeviceContext*    g_Context   = nullptr;
static ID3D11RenderTargetView* g_MainRTV   = nullptr;

bool CreateDeviceD3D(HWND hwnd);
void CleanupDeviceD3D();

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_NCHITTEST: return Manager::Get().HandleHitTest(lParam);
    case WM_DESTROY:   ::PostQuitMessage(0); return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                       hInstance, nullptr, nullptr, nullptr, nullptr,
                       L"ImOverlay_MinimalDemo", nullptr };
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        wc.lpszClassName, L"ImOverlay Demo",
        WS_POPUP, 100, 100, 750, 510,
        nullptr, nullptr, hInstance, nullptr);

    ::SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    if (!CreateDeviceD3D(hwnd)) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_Device, g_Context);

    IDXGIFactory* factory = nullptr;
    CreateDXGIFactory(IID_PPV_ARGS(&factory));

    Manager::Get().Init(hwnd, ImVec2(100.f, 100.f), ImVec2(698.f, 458.f));
    Manager::Get().SetD3DObjects(g_SwapChain, g_Device, &g_MainRTV);
    Manager::Get().SetDXGIFactory(factory);

    // Floating window: Acrylic blur + magnetic snap
    Config floatCfg;
    floatCfg.window_title        = "My Tool";
    floatCfg.size                = ImVec2(300.f, 120.f);
    floatCfg.anchor              = AnchorMode::Screen_BottomRight;
    floatCfg.enable_acrylic_blur = true;
    floatCfg.acrylic_type        = AcrylicType::Acrylic;
    floatCfg.enable_snap         = true;
    floatCfg.snap_threshold      = 18.0f;

    Manager::Get().CreateFloatingOverlay("my_tool", floatCfg,
        [](Window* win, float) {
            ImGui::Text("Hello from ImOverlay-DX11!");
            ImGui::Text("Window: %s", win->GetId().c_str());
        });

    // Global hotkey: Insert = toggle all overlay visibility
    Manager::Get().StartHotkeyListener();
    Manager::Get().RegisterHotkey(1, 0, VK_INSERT, HotkeyAction::ToggleVisibility);

    // Stacking toast notification (thread-safe)
    Manager::Get().PushToast("Welcome!", "ImOverlay-DX11 is running.", 4.0f);

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            continue;
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Manager::Get().BeginFrame();
        // ... your main ImGui UI here ...
        Manager::Get().EndFrame(ImGui::GetIO().DeltaTime);
        ImGui::Render();
        const float clear[4] = { 0, 0, 0, 0 };
        g_Context->OMSetRenderTargets(1, &g_MainRTV, nullptr);
        g_Context->ClearRenderTargetView(g_MainRTV, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_SwapChain->Present(1, 0);
    }

    Manager::Get().StopHotkeyListener();
    Manager::Get().StopCaptureMonitor();
    if (factory) factory->Release();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, hInstance);
    return 0;
}

bool CreateDeviceD3D(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = { 60, 1 };
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc = { 1, 0 };
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_SwapChain, &g_Device, &fl, &g_Context)))
        return false;
    ID3D11Texture2D* bb = nullptr;
    g_SwapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_Device->CreateRenderTargetView(bb, nullptr, &g_MainRTV);
    bb->Release();
    return true;
}

void CleanupDeviceD3D()
{
    if (g_MainRTV)   { g_MainRTV->Release();   g_MainRTV   = nullptr; }
    if (g_SwapChain) { g_SwapChain->Release(); g_SwapChain = nullptr; }
    if (g_Context)   { g_Context->Release();   g_Context   = nullptr; }
    if (g_Device)    { g_Device->Release();    g_Device    = nullptr; }
}
