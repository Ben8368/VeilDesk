#include "common/PreciseTimer.h"
#include "common/Logger.h"

namespace veil {
PreciseTimer::PreciseTimer() {
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer_) {
        Logger::Win32Error(L"CreateWaitableTimerExW(high resolution); Action: coarse Sleep fallback", GetLastError());
    }
}
PreciseTimer::~PreciseTimer() {
    if (timer_) { CloseHandle(timer_); }
}
bool PreciseTimer::Arm(DWORD milliseconds) {
    if (!timer_) { return false; }
    // Negative due time is relative, in 100ns units.
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10000;
    if (!SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0)) {
        Logger::Win32Error(L"SetWaitableTimerEx; Action: coarse wait fallback", GetLastError());
        CloseHandle(timer_); timer_ = nullptr;
        return false;
    }
    return true;
}
void PreciseTimer::Wait(DWORD milliseconds) {
    if (!Arm(milliseconds) || WaitForSingleObject(timer_, milliseconds + 100) != WAIT_OBJECT_0) { Sleep(milliseconds); }
}
}
