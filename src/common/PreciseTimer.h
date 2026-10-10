#pragma once
#include <Windows.h>

namespace veil {
// Per-wait high-resolution relative timer. Avoids process-wide timeBeginPeriod;
// Sleep/MsgWait timeouts otherwise round up to the ~15.6ms system tick.
class PreciseTimer final {
public:
    PreciseTimer();
    ~PreciseTimer();
    PreciseTimer(const PreciseTimer&) = delete;
    PreciseTimer& operator=(const PreciseTimer&) = delete;
    // False when the high-resolution timer is unavailable; caller keeps a coarse timeout.
    bool Arm(DWORD milliseconds);
    HANDLE Get() const noexcept { return timer_; }
    // Bounded wait; falls back to Sleep when arming fails.
    void Wait(DWORD milliseconds);
private:
    HANDLE timer_ = nullptr;
};
}
