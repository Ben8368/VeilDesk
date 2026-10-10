#include "security/DisplayProtection.h"
#include "common/Logger.h"
#include <dwmapi.h>

namespace veil {
ProtectionResult DisplayProtection::Enable(HWND hwnd) {
    ProtectionResult result{};
    DWORD owner = 0;
    if (!GetWindowThreadProcessId(hwnd, &owner)) {
        result.error = GetLastError();
        Logger::Win32Error(L"GetWindowThreadProcessId", result.error); return result;
    }
    if (owner != GetCurrentProcessId() || GetAncestor(hwnd, GA_ROOT) != hwnd) {
        result.error = ERROR_INVALID_WINDOW_HANDLE;
        Logger::Error(L"DisplayProtection requires this process's top-level window"); return result;
    }
    BOOL composition = FALSE;
    if (!Logger::CheckHR(DwmIsCompositionEnabled(&composition), L"DwmIsCompositionEnabled")) { return result; }
    Logger::Info(composition ? L"DWM composition enabled" : L"DWM composition disabled");
    if (!SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)) {
        result.error = GetLastError();
        Logger::Win32Error(L"SetWindowDisplayAffinity", result.error); return result;
    }
    if (!GetWindowDisplayAffinity(hwnd, &result.observedAffinity)) {
        result.error = GetLastError();
        Logger::Win32Error(L"GetWindowDisplayAffinity", result.error); return result;
    }
    Logger::Info(L"GetWindowDisplayAffinity=" + std::to_wstring(result.observedAffinity));
    result.enabled = composition && result.observedAffinity == WDA_EXCLUDEFROMCAPTURE;
    if (result.enabled) {
        Logger::Info(L"WDA_EXCLUDEFROMCAPTURE enabled (API set/readback; capture behavior requires testing)");
    } else {
        Logger::Warn(L"WDA request accepted but effective exclusion was not confirmed");
    }
    return result;
}
}
