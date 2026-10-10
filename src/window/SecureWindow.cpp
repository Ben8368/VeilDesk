#include "window/SecureWindow.h"
#include "common/Logger.h"
#include "common/ProgressWatchdog.h"
#include "common/PreciseTimer.h"
#include <chrono>
#include <utility>
#include <vector>
#include <algorithm>

namespace {
constexpr wchar_t windowClass[] = L"VeilDesk.SecureViewer";
BOOL CALLBACK EnumerateMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) { veil::Logger::Win32Error(L"GetMonitorInfoW(smoke)", GetLastError()); return FALSE; }
    reinterpret_cast<std::vector<RECT>*>(data)->push_back(info.rcWork);
    return TRUE;
}
bool SetStyle(HWND hwnd, LONG_PTR style) {
    SetLastError(ERROR_SUCCESS);
    if (SetWindowLongPtrW(hwnd, GWL_STYLE, style) == 0 && GetLastError() != ERROR_SUCCESS) {
        veil::Logger::Win32Error(L"SetWindowLongPtrW(style)", GetLastError());
        return false;
    }
    return true;
}
}
namespace veil {
SecureWindow::~SecureWindow() {
    if (hwnd_ && !DestroyWindow(hwnd_)) { Logger::Win32Error(L"DestroyWindow", GetLastError()); }
    if (instance_ && !UnregisterClassW(windowClass, instance_)) { Logger::Win32Error(L"UnregisterClassW", GetLastError()); }
}
bool SecureWindow::Create(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!wc.hCursor) { Logger::Win32Error(L"LoadCursorW", GetLastError()); return false; }
    wc.lpszClassName = windowClass;
    if (!RegisterClassExW(&wc)) { Logger::Win32Error(L"RegisterClassExW", GetLastError()); return false; }
    dpi_ = GetDpiForSystem();
    RECT rect{0, 0, MulDiv(1000, static_cast<int>(dpi_), 96), MulDiv(650, static_cast<int>(dpi_), 96)};
    if (!AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_)) {
        Logger::Win32Error(L"AdjustWindowRectExForDpi", GetLastError()); return false;
    }
    hwnd_ = CreateWindowExW(0, windowClass, L"VeilDesk - SecureViewer", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, this);
    if (!hwnd_) { Logger::Win32Error(L"CreateWindowExW", GetLastError()); return false; }
    dpi_ = GetDpiForWindow(hwnd_);
    Logger::Info(L"Win32 top-level window created; DPI=" + std::to_wstring(dpi_));
    return true;
}
void SecureWindow::Show(int command) { ShowWindow(hwnd_, command); }
void SecureWindow::SetResizeHandler(std::function<bool(UINT, UINT)> callback) { resize_ = std::move(callback); }
int SecureWindow::Run(const std::function<bool()>& render, unsigned int runSeconds, bool smokeTest) {
    const auto start = std::chrono::steady_clock::now();
    unsigned int smokeStep = 0;
    std::vector<RECT> smokeMonitors;
    if (smokeTest) {
        if (!EnumDisplayMonitors(nullptr, nullptr, EnumerateMonitor, reinterpret_cast<LPARAM>(&smokeMonitors))) {
            Logger::Win32Error(L"EnumDisplayMonitors(smoke)", GetLastError()); return 1;
        }
        if (smokeMonitors.size() < 2) { Logger::Info(L"Multi-monitor move test skipped: only one active monitor"); }
    }
    const unsigned int smokeSteps = smokeMonitors.size() > 1 ? 10U : 8U;
    MSG msg{};
    PreciseTimer idle;
    for (;;) {
        if (progress_) { progress_->Checkpoint("peek-window-message"); }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { return failed_ ? 1 : static_cast<int>(msg.wParam); }
            TranslateMessage(&msg);
            if (progress_) { progress_->Checkpoint("dispatch-window-message", UINT64_MAX, msg.message); }
            DispatchMessageW(&msg);
            if (progress_) { progress_->Checkpoint("dispatched-window-message"); }
        }
        if (smokeTest && std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(400 * (smokeStep + 1)) && smokeStep < smokeSteps) {
            bool success = true;
            switch (smokeStep++) {
            case 0:
                ShowWindow(hwnd_, SW_SHOW);
                success = SetWindowPos(hwnd_, nullptr, 100, 100, 760, 520, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
                break;
            case 1: success = SetWindowPos(hwnd_, nullptr, 100, 100, 1280, 800, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE; break;
            case 2: success = SetWindowPos(hwnd_, nullptr, 100, 100, 480, 360, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE; break;
            case 3: ShowWindow(hwnd_, SW_MAXIMIZE); break;
            case 4: ShowWindow(hwnd_, SW_MINIMIZE); break;
            case 5: ShowWindow(hwnd_, SW_RESTORE); break;
            case 6: success = ToggleFullscreen(); break;
            case 7: success = ToggleFullscreen(); break;
            case 8:
            case 9: {
                ShowWindow(hwnd_, SW_RESTORE);
                const RECT& monitor = smokeMonitors[smokeStep == 9 ? 1 : 0];
                success = SetWindowPos(hwnd_, nullptr, monitor.left + 16, monitor.top + 16,
                    std::min(900L, monitor.right - monitor.left - 32), std::min(650L, monitor.bottom - monitor.top - 32),
                    SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
                Logger::Info(L"Smoke moved own Viewer to monitor " + std::to_wstring(smokeStep == 9 ? 2 : 1));
                break;
            }
            }
            if (!success) { Logger::Win32Error(L"Smoke window operation", GetLastError()); failed_ = true; return 1; }
            Logger::Info(L"Smoke window step " + std::to_wstring(smokeStep) + L" completed");
        }
        if (runSeconds && std::chrono::steady_clock::now() - start >= std::chrono::seconds(runSeconds)) {
            Logger::Info(L"Timed smoke run completed");
            SendMessageW(hwnd_, WM_CLOSE, 0, 0);
            continue;
        }
        if (!minimized_ && render && !render()) {
            failed_ = true;
            SendMessageW(hwnd_, WM_CLOSE, 0, 0);
        } else {
            // Throttle idle, minimized and occluded windows without busy spinning.
            const DWORD interval = minimized_ ? 100 : 8;
            if (progress_) { progress_->Checkpoint("idle-arm"); }
            const bool precise = idle.Arm(interval);
            HANDLE timer = idle.Get();
            if (progress_) { progress_->Checkpoint("idle-wait"); }
            const DWORD wait = MsgWaitForMultipleObjectsEx(precise ? 1 : 0, precise ? &timer : nullptr,
                precise ? interval + 100 : interval, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (progress_) { progress_->Checkpoint("idle-complete"); }
            if (wait == WAIT_FAILED) { Logger::Win32Error(L"MsgWaitForMultipleObjectsEx", GetLastError()); return 1; }
        }
    }
}
bool SecureWindow::ToggleFullscreen() {
    if (!fullscreen_) {
        placement_.length = sizeof(placement_);
        MONITORINFO monitor{sizeof(monitor)};
        const HMONITOR handle = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        if (!GetWindowPlacement(hwnd_, &placement_)) { Logger::Win32Error(L"GetWindowPlacement", GetLastError()); return false; }
        if (!GetMonitorInfoW(handle, &monitor)) { Logger::Win32Error(L"GetMonitorInfoW", GetLastError()); return false; }
        if (!SetStyle(hwnd_, WS_POPUP | WS_VISIBLE)) { return false; }
        if (!SetWindowPos(hwnd_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED)) {
            Logger::Win32Error(L"SetWindowPos(fullscreen)", GetLastError());
            SetStyle(hwnd_, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            return false;
        }
    } else {
        if (!SetStyle(hwnd_, WS_OVERLAPPEDWINDOW | WS_VISIBLE)) { return false; }
        if (!SetWindowPlacement(hwnd_, &placement_)) { Logger::Win32Error(L"SetWindowPlacement", GetLastError()); return false; }
        if (!SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED)) {
            Logger::Win32Error(L"SetWindowPos(windowed)", GetLastError()); return false;
        }
    }
    fullscreen_ = !fullscreen_;
    Logger::Info(fullscreen_ ? L"Borderless fullscreen enabled" : L"Windowed mode restored");
    return true;
}
LRESULT CALLBACK SecureWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<SecureWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<SecureWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)) == 0 && GetLastError() != ERROR_SUCCESS) {
            Logger::Win32Error(L"SetWindowLongPtrW(user data)", GetLastError()); return FALSE;
        }
    }
    if (self && self->progress_) { self->progress_->Checkpoint("process-window-message", UINT64_MAX, message); }
    const LRESULT result = self ? self->ProcessMessage(message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
    if (self && self->progress_) { self->progress_->Checkpoint("processed-window-message", UINT64_MAX, message); }
    if (message == WM_NCDESTROY && self) { self->hwnd_ = nullptr; }
    return result;
}
LRESULT SecureWindow::ProcessMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ENTERSIZEMOVE:
        Logger::Info(L"Window entered modal move/resize; frame dispatch can pause");
        break;
    case WM_EXITSIZEMOVE:
        Logger::Info(L"Window exited modal move/resize");
        break;
    case WM_SIZE:
        minimized_ = wparam == SIZE_MINIMIZED;
        if (!minimized_ && resize_ && !resize_(LOWORD(lparam), HIWORD(lparam))) { failed_ = true; PostQuitMessage(1); }
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        Logger::Info(L"WM_DPICHANGED; DPI=" + std::to_wstring(dpi_));
        const auto* rect = reinterpret_cast<const RECT*>(lparam);
        if (!SetWindowPos(hwnd_, nullptr, rect->left, rect->top, rect->right - rect->left,
            rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE)) {
            Logger::Win32Error(L"SetWindowPos(DPI)", GetLastError()); failed_ = true; PostQuitMessage(1);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (fixedWindow_ && (wparam == VK_F11 || wparam == VK_ESCAPE)) { return 0; }
        if (wparam == VK_F11 && (lparam & (1LL << 30)) == 0) { ToggleFullscreen(); return 0; }
        if (wparam == VK_ESCAPE && fullscreen_) { ToggleFullscreen(); return 0; }
        break;
    case WM_SYSCOMMAND:
        if (fixedWindow_ && ((wparam & 0xFFF0) == SC_MOVE || (wparam & 0xFFF0) == SC_SIZE)) { return 0; }
        break;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        if (!BeginPaint(hwnd_, &paint)) { Logger::Win32Error(L"BeginPaint", GetLastError()); }
        if (!EndPaint(hwnd_, &paint)) { Logger::Win32Error(L"EndPaint", GetLastError()); }
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}
}
