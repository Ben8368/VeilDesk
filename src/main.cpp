#include "common/Logger.h"
#include "common/ProgressWatchdog.h"
#include "common/SystemInfo.h"
#include "window/SecureWindow.h"
#include "graphics/D3D11Renderer.h"
#include "security/DisplayProtection.h"
#include <shellapi.h>
#include <string>
#include <memory>
#include "broker/Channel.h"
#include "broker/SharedFrame.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    veil::Logger::Initialize(L"SecureViewer.log", false);
    veil::LogSystemInfo();
    unsigned int runSeconds = 0;
    unsigned benchmarkWidth = 1920, benchmarkHeight = 1080;
    bool smokeTest = false;
    bool captureReady = false;
    bool protection = true;
    bool displayOnly = true;
    bool simulateDisplayOnlyFailure = false;
    bool sharedSource = false, benchmarkDisplay = false;
    veil::broker::Handle channelInput, channelOutput;
    int count = 0;
    const std::unique_ptr<wchar_t*, decltype(&LocalFree)> argumentStorage(CommandLineToArgvW(GetCommandLineW(), &count), &LocalFree);
    wchar_t** const args = argumentStorage.get();
    if (!args) { veil::Logger::Win32Error(L"CommandLineToArgvW", GetLastError()); return 1; }
    for (int i = 1; i < count; ++i) {
        if (std::wstring(args[i]) == L"--smoke-test") { runSeconds = 5; smokeTest = true; }
        else if (std::wstring(args[i]) == L"--benchmark-display") { benchmarkDisplay = true; }
        else if (std::wstring(args[i]) == L"--benchmark-size" && i + 2 < count) {
            wchar_t* end = nullptr;
            const auto pixelWidth = std::wcstoul(args[++i], &end, 10);
            if (!end || *end || pixelWidth < 216 || pixelWidth > 4096) { return 1; }
            const auto pixelHeight = std::wcstoul(args[++i], &end, 10);
            if (!end || *end || pixelHeight < 36 || pixelHeight > 4096) { return 1; }
            benchmarkWidth = pixelWidth; benchmarkHeight = pixelHeight;
        }
        else if (std::wstring(args[i]) == L"--shared-source") { sharedSource = true; }
        else if (std::wstring(args[i]) == L"--channel" && i + 2 < count) {
            try {
                channelInput.reset(reinterpret_cast<HANDLE>(std::stoull(args[++i])));
                channelOutput.reset(reinterpret_cast<HANDLE>(std::stoull(args[++i])));
            } catch (...) { veil::Logger::Error(L"Invalid channel handles"); return 1; }
        }
        else if (std::wstring(args[i]) == L"--no-protection") { protection = false; }
        else if (std::wstring(args[i]) == L"--disable-display-only") { displayOnly = false; }
        else if (std::wstring(args[i]) == L"--simulate-display-only-failure") { simulateDisplayOnlyFailure = true; }
        else if (std::wstring(args[i]) == L"--capture-ready") { captureReady = true; }
        else if (std::wstring(args[i]) == L"--help") {
            veil::Logger::Info(L"SecureViewer [--smoke-test] [--run-seconds 1..7230] [--capture-ready] [--no-protection] [--disable-display-only] [--simulate-display-only-failure]; shared-source mode is started by CaptureBroker");
            return 0;
        }
        else if (std::wstring(args[i]) == L"--run-seconds" && i + 1 < count) {
            wchar_t* end = nullptr;
            const auto seconds = std::wcstoul(args[++i], &end, 10);
            if (!end || *end != L'\0' || seconds == 0 || seconds > 7230) {
                veil::Logger::Error(L"--run-seconds must be 1..7230"); return 1;
            }
            runSeconds = static_cast<unsigned int>(seconds);
        } else { veil::Logger::Error(L"Unknown or incomplete option: " + std::wstring(args[i])); return 1; }
    }
    veil::SecureWindow window;
    veil::broker::SharedFrameConsumer source;
    veil::broker::Lease sourceLease;
    if (sharedSource) {
        if (!protection || GetFileType(channelInput.get()) != FILE_TYPE_PIPE || GetFileType(channelOutput.get()) != FILE_TYPE_PIPE) {
            veil::Logger::Error(L"Shared source requires protected Viewer and private channel"); return 1;
        }
        if (!SetHandleInformation(channelInput.get(), HANDLE_FLAG_INHERIT, 0) ||
            !SetHandleInformation(channelOutput.get(), HANDLE_FLAG_INHERIT, 0)) {
            veil::Logger::Win32Error(L"Viewer disable channel inheritance", GetLastError()); return 1;
        }
    } else if (channelInput || channelOutput) { veil::Logger::Error(L"Channel requires --shared-source"); return 1; }
    if (!window.Create(instance)) { return 1; }
    const auto status = protection ? veil::DisplayProtection::Enable(window.Handle()) : veil::ProtectionResult{};
    if (sharedSource && !status.enabled) { veil::Logger::Error(L"Shared Viewer requires confirmed WDA"); return 1; }
    if (!protection) { veil::Logger::Warn(L"UNPROTECTED diagnostic baseline explicitly selected"); }
    veil::broker::ZeroCopyStats stats;
    struct StatsExit { veil::broker::ZeroCopyStats& stats; bool active; ~StatsExit() { if (active) { stats.Log(); } } } statsExit{stats, sharedSource};
    std::unique_ptr<veil::ProgressWatchdog> progress;
    if (benchmarkDisplay) {
        try { progress = std::make_unique<veil::ProgressWatchdog>(L"Viewer-progress.log"); }
        catch (const std::exception&) { veil::Logger::Error(L"Benchmark progress observer initialization failed"); return 1; }
    }
    window.SetProgress(progress.get());
    veil::D3D11Renderer renderer;
    struct ClearWindowBindings {
        veil::SecureWindow& window;
        ~ClearWindowBindings() { window.SetResizeHandler({}); window.SetProgress(nullptr); }
    } clearBindings{window};
    renderer.SetProgress(progress.get());
    if (sharedSource) { renderer.SetStats(&stats); }
    renderer.SetProtectionStatus(status.enabled ? L"WDA_EXCLUDEFROMCAPTURE: API enabled" : L"WDA: NOT CONFIRMED / UNPROTECTED");
    if (!renderer.Initialize(window.Handle(), protection && displayOnly, simulateDisplayOnlyFailure, sharedSource)) { return 1; }
    if (sharedSource && !source.Initialize(renderer.Device(), &stats)) { return 1; }
    window.SetResizeHandler([&renderer, &window, sharedSource](UINT width, UINT height) {
        return renderer.Resize(width, height) && (!sharedSource || renderer.RenderFrame(window.Dpi()));
    });
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
    if (benchmarkDisplay) {
        if (!sharedSource) { return 1; }
        // Measurement fixture has no non-client menus/drag loops; normal Viewer retains its window controls.
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(window.Handle(), GWL_STYLE, WS_POPUP | WS_VISIBLE) && GetLastError() != ERROR_SUCCESS) {
            veil::Logger::Win32Error(L"Benchmark fixed window style", GetLastError()); return 1;
        }
        RECT rect{0, 0, static_cast<LONG>(benchmarkWidth), static_cast<LONG>(benchmarkHeight)};
        if (!AdjustWindowRectExForDpi(&rect, static_cast<DWORD>(GetWindowLongPtrW(window.Handle(), GWL_STYLE)),
            FALSE, static_cast<DWORD>(GetWindowLongPtrW(window.Handle(), GWL_EXSTYLE)), window.Dpi()) ||
            !SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
            veil::Logger::Win32Error(L"Benchmark Viewer size", GetLastError()); return 1;
        }
        window.Show(SW_SHOWNORMAL);
        veil::Logger::Info(L"Benchmark fixed borderless fixture; ordinary Viewer controls remain in normal mode");
    }
    bool stopped = false;
    const int result = window.Run([&] {
        if (sharedSource) {
            if (progress) { progress->Checkpoint("dispatch-source"); }
            DWORD available = 0;
            if (!PeekNamedPipe(channelInput.get(), nullptr, 0, nullptr, &available, nullptr)) {
                veil::Logger::Win32Error(L"Viewer PeekNamedPipe", GetLastError()); return false;
            }
            if (available >= sizeof(veil::broker::Message)) {
                if (progress) { progress->Checkpoint("receive-source"); }
                veil::broker::Message message;
                if (!veil::broker::Receive(channelInput.get(), message, 1000, L"viewer source message")) { return false; }
                if (message.kind == veil::broker::Kind::Stop) { stopped = true; PostQuitMessage(0); return true; }
                if (!sourceLease.granted) {
                    sourceLease.granted = true; sourceLease.generation = message.generation; sourceLease.deadline = message.deadline;
                    sourceLease.pixelWidth = message.pixelWidth; sourceLease.pixelHeight = message.pixelHeight;
                }
                const auto decision = sourceLease.Check(message, GetTickCount64());
                if (decision != veil::broker::Status::Ok) {
                    veil::broker::Handle rejected(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(message.handle))); return false;
                }
                veil::broker::SharedTexture texture;
                if (progress) { progress->Checkpoint("open-shared", message.frame); }
                if (!source.OpenFrame(message, texture)) { return false; }
                if (progress) { progress->Checkpoint("copy-shared-to-snapshot"); }
                if (!renderer.SetSourceTexture(texture.Get())) { return false; }
                if (progress) { progress->Checkpoint("release-shared"); }
                if (!texture.Close()) { return false; }
                message.kind = veil::broker::Kind::Ack; message.handle = 0;
                const auto previouslyPresented = renderer.FrameCount();
                if (!renderer.RenderFrame(window.Dpi())) { return false; }
                if (renderer.FrameCount() == previouslyPresented) {
                    veil::Logger::Error(L"Location: Viewer Present; Cause: source frame not presented (occluded/recovery); Action: deny ACK"); return false;
                }
                if (progress) { progress->Checkpoint("send-ack"); }
                if (!veil::broker::Send(channelOutput.get(), message)) { return false; }
                sourceLease.lastFrame = message.frame;
                if (progress) { progress->Checkpoint("frame-complete"); }
                return true;
            }
            if (sourceLease.granted && GetTickCount64() >= sourceLease.deadline) { return false; }
        }
        return sharedSource || renderer.RenderFrame(window.Dpi());
    }, runSeconds, smokeTest);
    window.SetResizeHandler({});
    return sharedSource && !stopped ? 1 : result;
}
