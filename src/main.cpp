#include "common/Logger.h"
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
    veil::Logger::Initialize(L"SecureViewer.log");
    veil::LogSystemInfo();
    unsigned int runSeconds = 0;
    bool smokeTest = false;
    bool captureReady = false;
    bool protection = true;
    bool displayOnly = true;
    bool simulateDisplayOnlyFailure = false;
    bool sharedSource = false;
    veil::broker::Handle channelInput, channelOutput;
    int count = 0;
    const std::unique_ptr<wchar_t*, decltype(&LocalFree)> argumentStorage(CommandLineToArgvW(GetCommandLineW(), &count), &LocalFree);
    wchar_t** const args = argumentStorage.get();
    if (!args) { veil::Logger::Win32Error(L"CommandLineToArgvW", GetLastError()); return 1; }
    for (int i = 1; i < count; ++i) {
        if (std::wstring(args[i]) == L"--smoke-test") { runSeconds = 5; smokeTest = true; }
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
            veil::Logger::Info(L"SecureViewer [--smoke-test] [--run-seconds 1..3600] [--capture-ready] [--no-protection] [--disable-display-only] [--simulate-display-only-failure]; shared-source mode is started by CaptureBroker");
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
    veil::D3D11Renderer renderer;
    if (sharedSource) { renderer.SetStats(&stats); }
    renderer.SetProtectionStatus(status.enabled ? L"WDA_EXCLUDEFROMCAPTURE: API enabled" : L"WDA: NOT CONFIRMED / UNPROTECTED");
    if (!renderer.Initialize(window.Handle(), protection && displayOnly, simulateDisplayOnlyFailure, sharedSource)) { return 1; }
    if (sharedSource && !source.Initialize(renderer.Device(), &stats)) { return 1; }
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
    bool stopped = false;
    const int result = window.Run([&] {
        if (sharedSource) {
            DWORD available = 0;
            if (!PeekNamedPipe(channelInput.get(), nullptr, 0, nullptr, &available, nullptr)) {
                veil::Logger::Win32Error(L"Viewer PeekNamedPipe", GetLastError()); return false;
            }
            if (available >= sizeof(veil::broker::Message)) {
                veil::broker::Message message;
                if (!veil::broker::Receive(channelInput.get(), message, 1000)) { return false; }
                if (message.kind == veil::broker::Kind::Stop) { stopped = true; PostQuitMessage(0); return true; }
                if (!sourceLease.granted) {
                    sourceLease.granted = true; sourceLease.generation = message.generation; sourceLease.deadline = message.deadline;
                }
                const auto decision = sourceLease.Check(message, GetTickCount64());
                if (decision != veil::broker::Status::Ok) {
                    veil::broker::Handle rejected(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(message.handle))); return false;
                }
                veil::broker::SharedTexture texture;
                if (!source.OpenFrame(message, texture) || !renderer.SetSourceTexture(texture.Get()) || !texture.Close()) { return false; }
                message.kind = veil::broker::Kind::Ack; message.handle = 0;
                if (!renderer.RenderFrame(window.Dpi()) || !veil::broker::Send(channelOutput.get(), message)) { return false; }
                sourceLease.lastFrame = message.frame;
                return true;
            }
            if (sourceLease.granted && GetTickCount64() >= sourceLease.deadline) { return false; }
        }
        return renderer.RenderFrame(window.Dpi());
    }, runSeconds, smokeTest);
    window.SetResizeHandler({});
    return sharedSource && !stopped ? 1 : result;
}
