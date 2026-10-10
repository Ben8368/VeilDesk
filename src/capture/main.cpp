#include "capture/CaptureEngine.h"
#include "common/Logger.h"
#include "common/SystemInfo.h"
#include <objbase.h>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
struct WindowEntry { HWND hwnd; DWORD pid; std::wstring title; };
BOOL CALLBACK Enumerate(HWND hwnd, LPARAM data) {
    wchar_t className[128]{};
    if (!GetClassNameW(hwnd, className, static_cast<int>(std::size(className)))) { return TRUE; }
    if (std::wstring(className) != L"VeilDesk.SecureViewer") { return TRUE; }
    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    DWORD pid = 0;
    if (!GetWindowThreadProcessId(hwnd, &pid)) { return TRUE; }
    reinterpret_cast<std::vector<WindowEntry>*>(data)->push_back({hwnd, pid, title});
    return TRUE;
}
class ComApartment final {
public:
    ComApartment() : result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComApartment() { if (SUCCEEDED(result)) { CoUninitialize(); } }
    const HRESULT result;
};
}
int wmain(int argc, wchar_t** argv) {
    veil::Logger::Initialize(L"CaptureTest.log");
    veil::LogSystemInfo();
    std::wstring output = L"capture.png";
    DWORD selectedPid = 0;
    bool listOnly = false;
    bool expectVisible = false;
    bool expectExcluded = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring option = argv[i];
        if (option == L"--output" && i + 1 < argc) { output = argv[++i]; }
        else if (option == L"--pid" && i + 1 < argc) {
            wchar_t* end = nullptr;
            const auto number = std::wcstoul(argv[++i], &end, 10);
            if (!end || *end != L'\0' || number == 0 || number == ULONG_MAX) { veil::Logger::Error(L"Invalid --pid"); return 1; }
            selectedPid = static_cast<DWORD>(number);
        }
        else if (option == L"--list") { listOnly = true; }
        else if (option == L"--expect-visible") { expectVisible = true; }
        else if (option == L"--expect-excluded") { expectExcluded = true; }
        else if (option == L"--help") {
            std::wcout << L"CaptureTest [--pid PID] [--output capture.png] [--list]\n"
                L"Tests: --expect-visible / --expect-excluded (marker heuristics; manual baseline required)\n"; return 0;
        } else { veil::Logger::Error(L"Unknown or incomplete option: " + option); return 1; }
    }
    if (expectVisible && expectExcluded) { veil::Logger::Error(L"Conflicting capture expectations"); return 1; }
    ComApartment apartment;
    if (!veil::Logger::CheckHR(apartment.result, L"CoInitializeEx")) { return 1; }
    std::vector<WindowEntry> windows;
    if (!EnumWindows(Enumerate, reinterpret_cast<LPARAM>(&windows))) {
        veil::Logger::Win32Error(L"EnumWindows", GetLastError()); return 1;
    }
    if (windows.empty()) { veil::Logger::Error(L"No SecureViewer windows found on this desktop. Start SecureViewer first."); return 1; }
    for (size_t i = 0; i < windows.size(); ++i) {
        std::wcout << L"[" << i + 1 << L"] PID=" << windows[i].pid << L" HWND=" << windows[i].hwnd << L" " << windows[i].title << L'\n';
    }
    if (listOnly) { return 0; }
    HWND selected = nullptr;
    if (selectedPid) {
        for (const auto& entry : windows) { if (entry.pid == selectedPid) { selected = entry.hwnd; break; } }
    } else {
        std::wcout << L"Select SecureViewer number: " << std::flush;
        size_t index = 0;
        if (!(std::wcin >> index) || index == 0 || index > windows.size()) { veil::Logger::Error(L"Invalid window selection"); return 1; }
        selected = windows[index - 1].hwnd;
    }
    if (!selected) { veil::Logger::Error(L"Selected PID has no SecureViewer window on this desktop"); return 1; }
    veil::CapturedImage image;
    if (!veil::CaptureEngine::CaptureClient(selected, image)) {
        veil::Logger::Warn(L"Capture failed / inconclusive (API or window state failure; not proof of protection)"); return 1;
    }
    if (!veil::CaptureEngine::SavePng(image, output)) { return 1; }
    const bool marker = veil::CaptureEngine::HasViewerPattern(image);
    const bool black = veil::CaptureEngine::IsNearlyBlack(image);
    if (marker) {
        veil::Logger::Warn(L"Capture succeeded: Viewer diagnostic pattern detected");
    } else {
        veil::Logger::Info(L"Capture blocked / blank / excluded (candidate: Viewer pattern absent)");
        veil::Logger::Info(black ? L"Pixel observation: nearly black" : L"Pixel observation: nonblank; may be underlying desktop content or occlusion");
        veil::Logger::Warn(L"Pattern absence is inconclusive without a visible unprotected baseline, an unobscured window and manual PNG inspection");
    }
    if ((expectVisible && !marker) || (expectExcluded && marker)) { veil::Logger::Error(L"Capture expectation mismatch"); return 2; }
    return 0;
}
