#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

#include "switch_auto/application.hpp"

namespace {

constexpr wchar_t kWindowClass[] = L"SwitchAutoSimulatorWindow";
std::array<std::uint32_t, switch_auto::kLogicalWidth * switch_auto::kLogicalHeight> gPixels{};
std::array<std::uint32_t, switch_auto::kLogicalWidth * switch_auto::kLogicalHeight> gBgraPixels{};
switch_auto::MutableRuntimeService gRuntime;
switch_auto::Application gApplication(gRuntime);

void renderFrame() {
    switch_auto::Canvas canvas(gPixels.data(), switch_auto::kLogicalWidth,
                               switch_auto::kLogicalHeight, switch_auto::kLogicalWidth);
    gApplication.update(1.0f / 60.0f);
    gApplication.render(canvas);
    for (std::size_t i = 0; i < gPixels.size(); ++i) {
        const std::uint32_t rgba = gPixels[i];
        const std::uint32_t r = rgba & 0xffU;
        const std::uint32_t g = (rgba >> 8U) & 0xffU;
        const std::uint32_t b = (rgba >> 16U) & 0xffU;
        gBgraPixels[i] = b | (g << 8U) | (r << 16U);
    }
}

void updateWindowTitle(HWND window) {
    const auto& state = gRuntime.current();
    wchar_t title[256];
    swprintf(title, 256,
             L"Switch Auto Simulator | arrows: speed | B: battery | C: companion | %d km/h",
             state.speedKph);
    SetWindowTextW(window, title);
}

bool writeSnapshot(const wchar_t* path) {
    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER imageHeader{};
    imageHeader.biSize = sizeof(BITMAPINFOHEADER);
    imageHeader.biWidth = switch_auto::kLogicalWidth;
    imageHeader.biHeight = -switch_auto::kLogicalHeight;
    imageHeader.biPlanes = 1;
    imageHeader.biBitCount = 32;
    imageHeader.biCompression = BI_RGB;
    imageHeader.biSizeImage = static_cast<DWORD>(gBgraPixels.size() * sizeof(std::uint32_t));

    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + imageHeader.biSizeImage;

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    const bool success =
        WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr) &&
        WriteFile(file, &imageHeader, sizeof(imageHeader), &written, nullptr) &&
        WriteFile(file, gBgraPixels.data(), imageHeader.biSizeImage, &written, nullptr);
    CloseHandle(file);
    return success;
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            SetTimer(window, 1, 16, nullptr);
            return 0;
        case WM_TIMER:
            renderFrame();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_KEYDOWN: {
            auto& state = gRuntime.mutableState();
            if (wParam == VK_UP) state.speedKph += 5;
            if (wParam == VK_DOWN) state.speedKph = state.speedKph >= 5 ? state.speedKph - 5 : 0;
            if (wParam == 'B') state.batteryPercent = state.batteryPercent > 10 ? state.batteryPercent - 10 : 100;
            if (wParam == 'C') {
                state.companionConnected = !state.companionConnected;
                state.track = state.companionConnected ? "MIDNIGHT DRIVE" : "NO MEDIA";
                state.artist = state.companionConnected ? "SWITCH AUTO DEMO" : "ANDROID COMPANION OFFLINE";
            }
            updateWindowTitle(window);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC device = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = switch_auto::kLogicalWidth;
            info.bmiHeader.biHeight = -switch_auto::kLogicalHeight;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(device, COLORONCOLOR);
            StretchDIBits(device, 0, 0, client.right, client.bottom,
                          0, 0, switch_auto::kLogicalWidth, switch_auto::kLogicalHeight,
                          gBgraPixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(window, 1);
            gApplication.shutdown();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    if (!gApplication.initialize()) return 1;

    if (commandLine != nullptr && std::wstring_view(commandLine).find(L"--snapshot") !=
                                      std::wstring_view::npos) {
        renderFrame();
        const bool success = writeSnapshot(L"switch-auto-snapshot.bmp");
        gApplication.shutdown();
        return success ? 0 : 4;
    }

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&windowClass)) return 2;

    RECT size{0, 0, switch_auto::kLogicalWidth, switch_auto::kLogicalHeight};
    AdjustWindowRect(&size, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExW(0, kWindowClass, L"Switch Auto Simulator",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        size.right - size.left, size.bottom - size.top,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 3;

    renderFrame();
    updateWindowTitle(window);
    ShowWindow(window, showCommand);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
