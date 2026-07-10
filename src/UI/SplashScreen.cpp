#include "SplashScreen.h"

#include "resource.h"
#include "../Core/Logger.h"

#include <gdiplus.h>
#include <objidl.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

namespace HookCrashers::UI::SplashScreen {
namespace {

constexpr const char* kWindowClassName = "HookCrashersSplashWindow";

struct GdiPlusSession {
    ULONG_PTR token = 0;

    GdiPlusSession() {
        Gdiplus::GdiplusStartupInput input {};
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
            token = 0;
        }
    }

    ~GdiPlusSession() {
        if (token != 0) {
            Gdiplus::GdiplusShutdown(token);
        }
    }

    bool IsReady() const {
        return token != 0;
    }
};

struct LoadedImage {
    IStream* stream = nullptr;
    std::unique_ptr<Gdiplus::Image> image;

    LoadedImage() = default;
    LoadedImage(const LoadedImage&) = delete;
    LoadedImage& operator=(const LoadedImage&) = delete;

    LoadedImage(LoadedImage&& other) noexcept
        : stream(std::exchange(other.stream, nullptr)),
          image(std::move(other.image)) {}

    LoadedImage& operator=(LoadedImage&& other) noexcept {
        if (this != &other) {
            image.reset();
            if (stream) {
                stream->Release();
            }
            stream = std::exchange(other.stream, nullptr);
            image = std::move(other.image);
        }
        return *this;
    }

    ~LoadedImage() {
        image.reset();
        if (stream) {
            stream->Release();
            stream = nullptr;
        }
    }
};

LoadedImage LoadSplashImage(HMODULE moduleHandle) {
    LoadedImage result {};

    HRSRC resource = FindResourceA(moduleHandle, MAKEINTRESOURCEA(IDR_HOOKCRASHERS_SPLASH), MAKEINTRESOURCEA(10));
    if (!resource) {
        Core::Logger::Error("SplashScreen: splash resource was not found");
        return result;
    }

    const DWORD size = SizeofResource(moduleHandle, resource);
    HGLOBAL loaded = LoadResource(moduleHandle, resource);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    if (!bytes || size == 0) {
        Core::Logger::Error("SplashScreen: splash resource is empty");
        return result;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) {
        Core::Logger::Error("SplashScreen: failed to allocate splash image memory");
        return result;
    }

    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        Core::Logger::Error("SplashScreen: failed to lock splash image memory");
        return result;
    }

    std::memcpy(target, bytes, size);
    GlobalUnlock(memory);

    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &result.stream))) {
        GlobalFree(memory);
        Core::Logger::Error("SplashScreen: failed to create stream for splash image");
        return result;
    }

    result.image.reset(Gdiplus::Image::FromStream(result.stream));
    if (!result.image || result.image->GetLastStatus() != Gdiplus::Ok) {
        Core::Logger::Error("SplashScreen: failed to decode splash PNG");
        result.image.reset();
    }

    return result;
}

LRESULT CALLBACK SplashWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, message, wparam, lparam);
}

void PaintSplash(HWND hwnd, Gdiplus::Image& image, int width, int height) {
    HDC dc = GetDC(hwnd);
    if (dc) {
        Gdiplus::Graphics graphics(dc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.Clear(Gdiplus::Color(255, 0, 0, 0));
        graphics.DrawImage(&image, 0, 0, width, height);
        ReleaseDC(hwnd, dc);
    }
}

} // namespace

void Show(HMODULE moduleHandle, DWORD durationMs) {
    GdiPlusSession gdiplus;
    if (!gdiplus.IsReady()) {
        Core::Logger::Error("SplashScreen: GDI+ failed to start");
        return;
    }

    LoadedImage loaded = LoadSplashImage(moduleHandle);
    if (!loaded.image) {
        return;
    }

    const int imageWidth = static_cast<int>(loaded.image->GetWidth());
    const int imageHeight = static_cast<int>(loaded.image->GetHeight());
    if (imageWidth <= 0 || imageHeight <= 0) {
        Core::Logger::Error("SplashScreen: invalid image size");
        return;
    }

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const double maxWidth = static_cast<double>(screenWidth) * 0.72;
    const double maxHeight = static_cast<double>(screenHeight) * 0.32;
    const double scale = (std::min)({1.0, maxWidth / imageWidth, maxHeight / imageHeight});
    const int windowWidth = (std::max)(1, static_cast<int>(imageWidth * scale));
    const int windowHeight = (std::max)(1, static_cast<int>(imageHeight * scale));
    const int x = (screenWidth - windowWidth) / 2;
    const int y = (screenHeight - windowHeight) / 2;

    WNDCLASSEXA wc {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SplashWndProc;
    wc.hInstance = moduleHandle;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                kWindowClassName,
                                "HookCrashers",
                                WS_POPUP,
                                x,
                                y,
                                windowWidth,
                                windowHeight,
                                nullptr,
                                nullptr,
                                moduleHandle,
                                nullptr);
    if (!hwnd) {
        Core::Logger::Error("SplashScreen: failed to create splash window");
        UnregisterClassA(kWindowClassName, moduleHandle);
        return;
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    PaintSplash(hwnd, *loaded.image, windowWidth, windowHeight);
    Core::Logger::Info("SplashScreen: shown for %u ms", static_cast<unsigned>(durationMs));

    const DWORD start = GetTickCount();
    MSG message {};
    while (GetTickCount() - start < durationMs) {
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        Sleep(16);
    }

    DestroyWindow(hwnd);
    UnregisterClassA(kWindowClassName, moduleHandle);
}

} // namespace HookCrashers::UI::SplashScreen
