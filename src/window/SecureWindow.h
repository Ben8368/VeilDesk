#pragma once
#include <Windows.h>
#include <functional>

namespace veil {
class ProgressWatchdog;
class SecureWindow final {
public:
    SecureWindow() = default;
    ~SecureWindow();
    SecureWindow(const SecureWindow&) = delete;
    SecureWindow& operator=(const SecureWindow&) = delete;
    bool Create(HINSTANCE instance);
    void Show(int command);
    int Run(const std::function<bool()>& render, unsigned int runSeconds = 0, bool smokeTest = false);
    void SetProgress(ProgressWatchdog* observer) noexcept { progress_ = observer; }
    void SetFixedWindow(bool fixed) noexcept { fixedWindow_ = fixed; }
    void SetResizeHandler(std::function<bool(UINT, UINT)> callback);
    HWND Handle() const noexcept { return hwnd_; }
    UINT Dpi() const noexcept { return dpi_; }
    bool ToggleFullscreen();
private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT ProcessMessage(UINT message, WPARAM wparam, LPARAM lparam);
    ProgressWatchdog* progress_ = nullptr;
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    UINT dpi_ = 96;
    bool minimized_ = false;
    bool failed_ = false;
    bool fullscreen_ = false;
    bool fixedWindow_ = false;
    WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};
    std::function<bool(UINT, UINT)> resize_;
};
}
