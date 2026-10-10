#include "common/Logger.h"
#include "common/SystemInfo.h"
#include "window/SecureWindow.h"
#include "graphics/D3D11Renderer.h"
#include "security/DisplayProtection.h"
#include <shellapi.h>
#include <string>
#include <memory>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    veil::Logger::Initialize(L"SecureViewer.log");
    veil::LogSystemInfo();
    unsigned int runSeconds = 0;
    bool smokeTest = false;
    bool captureReady = false;
    bool protection = true;
    bool displayOnly = true;
    bool simulateDisplayOnlyFailure = false;
    int count = 0;
    const std::unique_ptr<wchar_t*, decltype(&LocalFree)> argumentStorage(CommandLineToArgvW(GetCommandLineW(), &count), &LocalFree);
    wchar_t** const args = argumentStorage.get();
    if (!args) { veil::Logger::Win32Error(L"CommandLineToArgvW", GetLastError()); return 1; }
    for (int i = 1; i < count; ++i) {
        if (std::wstring(args[i]) == L"--smoke-test") { runSeconds = 5; smokeTest = true; }
        else if (std::wstring(args[i]) == L"--no-protection") { protection = false; }
        else if (std::wstring(args[i]) == L"--disable-display-only") { displayOnly = false; }
        else if (std::wstring(args[i]) == L"--simulate-display-only-failure") { simulateDisplayOnlyFailure = true; }
        else if (std::wstring(args[i]) == L"--capture-ready") { captureReady = true; }
        else if (std::wstring(args[i]) == L"--help") {
            veil::Logger::Info(L"SecureViewer [--smoke-test] [--run-seconds 1..3600] [--capture-ready] [--no-protection] [--disable-display-only] [--simulate-display-only-failure]");
            return 0;
        }
        else if (std::wstring(args[i]) == L"--run-seconds" && i + 1 < count) {
            wchar_t* end = nullptr;
            const auto seconds = std::wcstoul(args[++i], &end, 10);
            if (!end || *end != L'\0' || seconds == 0 || seconds > 3600) {
                veil::Logger::Error(L"--run-seconds must be 1..3600"); return 1;
            }
            runSeconds = static_cast<unsigned int>(seconds);
        } else { veil::Logger::Error(L"Unknown or incomplete option: " + std::wstring(args[i])); return 1; }
    }
    veil::SecureWindow window;
    if (!window.Create(instance)) { return 1; }
    const auto status = protection ? veil::DisplayProtection::Enable(window.Handle()) : veil::ProtectionResult{};
    if (!protection) { veil::Logger::Warn(L"UNPROTECTED diagnostic baseline explicitly selected"); }
    veil::D3D11Renderer renderer;
    renderer.SetProtectionStatus(status.enabled ? L"WDA_EXCLUDEFROMCAPTURE: API enabled" : L"WDA: NOT CONFIRMED / UNPROTECTED");
    if (!renderer.Initialize(window.Handle(), protection && displayOnly, simulateDisplayOnlyFailure)) { return 1; }
    window.SetResizeHandler([&renderer](UINT width, UINT height) { return renderer.Resize(width, height); });
    window.Show(show);
    // The first ShowWindow honors STARTUPINFO even if a different command is
    // supplied. Timed diagnostics explicitly show the window on the second call.
    if (runSeconds) { window.Show(SW_SHOWNORMAL); }
    if (captureReady) {
        window.Show(SW_SHOWNORMAL);
        if (!SetWindowPos(window.Handle(), HWND_TOPMOST, 80, 80, 1000, 700, SWP_NOACTIVATE)) {
            veil::Logger::Win32Error(L"SetWindowPos(capture-ready)", GetLastError()); return 1;
        }
        veil::Logger::Info(L"Diagnostic capture-ready: own Viewer window placed topmost");
    }
    const int result = window.Run([&renderer, &window] { return renderer.RenderFrame(window.Dpi()); }, runSeconds, smokeTest);
    window.SetResizeHandler({});
    return result;
}
