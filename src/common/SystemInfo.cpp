#include "common/SystemInfo.h"
#include "common/Logger.h"

namespace veil {
void LogSystemInfo() {
    // Diagnostic only. The Windows 10 compatibility manifest is embedded in both
    // executables. Capability decisions use actual API results, not build numbers.
    OSVERSIONINFOEXW version{};
    version.dwOSVersionInfoSize = sizeof(version);
#pragma warning(push)
#pragma warning(disable: 4996)
    const BOOL found = GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&version));
#pragma warning(pop)
    if (found) {
        Logger::Info(L"Windows version (manifest-aware): " + std::to_wstring(version.dwMajorVersion) + L"." +
            std::to_wstring(version.dwMinorVersion) + L"." + std::to_wstring(version.dwBuildNumber));
    } else { Logger::Win32Error(L"GetVersionExW (diagnostic)", GetLastError()); }
    const bool perMonitorV2 = AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
    if (perMonitorV2) { Logger::Info(L"DPI awareness: Per-Monitor V2"); }
    else { Logger::Warn(L"DPI awareness is not Per-Monitor V2; check embedded manifest"); }
    Logger::Info(L"Process PID=" + std::to_wstring(GetCurrentProcessId()) + L"; active monitors=" + std::to_wstring(GetSystemMetrics(SM_CMONITORS)));
    Logger::Info(L"Windows official capture restrictions PoC; not a security boundary");
}
}
