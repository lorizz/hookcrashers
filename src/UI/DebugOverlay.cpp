#include "DebugOverlay.h"

#include "../Core/Logger.h"

#include <windows.h>
#include <d3d9.h>
#include <detours.h>
#include <imgui.h>
#include <backends/imgui_impl_dx9.h>
#include <backends/imgui_impl_win32.h>
#include <string>

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

namespace HookCrashers::UI::DebugOverlay {
namespace {

using EndSceneFn = HRESULT(__stdcall*)(IDirect3DDevice9*);
using PresentFn = HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using ResetFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

EndSceneFn g_OriginalEndScene = nullptr;
PresentFn g_OriginalPresent = nullptr;
ResetFn g_OriginalReset = nullptr;
HWND g_GameWindow = nullptr;
WNDPROC g_OriginalWndProc = nullptr;
bool g_Installed = false;
bool g_ImGuiReady = false;
bool g_Visible = true;
bool g_LastToggleDown = false;
DWORD g_LastFrameTick = 0;

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (g_Visible && g_ImGuiReady) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
    }

    if (g_OriginalWndProc) {
        return CallWindowProc(g_OriginalWndProc, hwnd, msg, wparam, lparam);
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

bool InitializeImGui(IDirect3DDevice9* device) {
    if (g_ImGuiReady) {
        return true;
    }

    D3DDEVICE_CREATION_PARAMETERS creation {};
    if (FAILED(device->GetCreationParameters(&creation)) || !creation.hFocusWindow) {
        Core::Logger::Error("DebugOverlay: failed to read D3D9 creation parameters");
        return false;
    }

    g_GameWindow = creation.hFocusWindow;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_GameWindow);
    ImGui_ImplDX9_Init(device);

    g_OriginalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(g_GameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OverlayWndProc)));

    g_ImGuiReady = true;
    Core::Logger::Info("DebugOverlay: ImGui DX9 console initialized");
    return true;
}

void DrawLogConsole() {
    ImGui::SetNextWindowSize(ImVec2(900.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("HookCrashers Log", &g_Visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("HookCrashers v5");
    ImGui::Text("Log file: %s", Core::Logger::LogPath().c_str());
    ImGui::Separator();

    const auto lines = Core::Logger::SnapshotLines();
    ImGui::BeginChild("LogScroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const std::string& line : lines) {
        ImVec4 color(0.88f, 0.88f, 0.88f, 1.0f);
        if (line.find("[ERROR]") != std::string::npos) {
            color = ImVec4(1.0f, 0.32f, 0.28f, 1.0f);
        } else if (line.find("[WARN]") != std::string::npos) {
            color = ImVec4(1.0f, 0.78f, 0.32f, 1.0f);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

void RunFrame(IDirect3DDevice9* device) {
    if (!g_ImGuiReady && !InitializeImGui(device)) {
        return;
    }

    const bool toggleDown = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (toggleDown && !g_LastToggleDown) {
        g_Visible = !g_Visible;
    }
    g_LastToggleDown = toggleDown;

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (g_Visible) {
        DrawLogConsole();
    }

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    g_LastFrameTick = GetTickCount();
}

HRESULT __stdcall HookedEndScene(IDirect3DDevice9* device) {
    RunFrame(device);
    return g_OriginalEndScene(device);
}

HRESULT __stdcall HookedPresent(IDirect3DDevice9* device,
                                const RECT* sourceRect,
                                const RECT* destRect,
                                HWND destWindowOverride,
                                const RGNDATA* dirtyRegion) {
    if (GetTickCount() - g_LastFrameTick > 8) {
        RunFrame(device);
    }
    return g_OriginalPresent(device, sourceRect, destRect, destWindowOverride, dirtyRegion);
}

HRESULT __stdcall HookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    if (g_ImGuiReady) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    const HRESULT result = g_OriginalReset(device, params);
    if (g_ImGuiReady && SUCCEEDED(result)) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    return result;
}

bool ResolveD3D9VTable(void** endScene, void** present, void** reset) {
    WNDCLASSEXA wc {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "HookCrashersD3D9Dummy";

    if (!RegisterClassExA(&wc)) {
        Core::Logger::Error("DebugOverlay: failed to register dummy D3D9 window class");
        return false;
    }

    HWND hwnd = CreateWindowA(wc.lpszClassName,
                              "HookCrashers",
                              WS_OVERLAPPEDWINDOW,
                              0,
                              0,
                              100,
                              100,
                              nullptr,
                              nullptr,
                              wc.hInstance,
                              nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!hwnd || !d3d) {
        Core::Logger::Error("DebugOverlay: failed to create dummy D3D9 objects");
        if (d3d) {
            d3d->Release();
        }
        if (hwnd) {
            DestroyWindow(hwnd);
        }
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return false;
    }

    D3DPRESENT_PARAMETERS params {};
    params.Windowed = TRUE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow = hwnd;

    IDirect3DDevice9* device = nullptr;
    const HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT,
                                         D3DDEVTYPE_HAL,
                                         hwnd,
                                         D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                         &params,
                                         &device);
    if (FAILED(hr) || !device) {
        Core::Logger::Error("DebugOverlay: failed to create dummy D3D9 device, hr=0x%08X", static_cast<unsigned>(hr));
        d3d->Release();
        DestroyWindow(hwnd);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    *reset = vtable[16];
    *present = vtable[17];
    *endScene = vtable[42];

    device->Release();
    d3d->Release();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return true;
}

} // namespace

bool Install() {
    if (g_Installed) {
        return true;
    }

    void* endScene = nullptr;
    void* present = nullptr;
    void* reset = nullptr;
    if (!ResolveD3D9VTable(&endScene, &present, &reset)) {
        Core::Logger::Error("DebugOverlay: D3D9 vtable resolution failed");
        return false;
    }

    g_OriginalEndScene = reinterpret_cast<EndSceneFn>(endScene);
    g_OriginalPresent = reinterpret_cast<PresentFn>(present);
    g_OriginalReset = reinterpret_cast<ResetFn>(reset);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)g_OriginalEndScene, HookedEndScene);
    DetourAttach(&(PVOID&)g_OriginalPresent, HookedPresent);
    DetourAttach(&(PVOID&)g_OriginalReset, HookedReset);
    const LONG result = DetourTransactionCommit();
    if (result != NO_ERROR) {
        Core::Logger::Error("DebugOverlay: failed to attach D3D9 hooks, detours=%ld", result);
        return false;
    }

    g_Installed = true;
    Core::Logger::Info("DebugOverlay: D3D9 hooks installed; press F10 to toggle log console");
    return true;
}

void Shutdown() {
    if (g_OriginalWndProc && g_GameWindow) {
        SetWindowLongPtr(g_GameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_OriginalWndProc));
        g_OriginalWndProc = nullptr;
    }

    if (g_ImGuiReady) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_ImGuiReady = false;
    }
}

bool IsInstalled() {
    return g_Installed;
}

bool IsVisible() {
    return g_Visible;
}

} // namespace HookCrashers::UI::DebugOverlay
