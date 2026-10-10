#pragma once
#include <Windows.h>

namespace veil {
struct ProtectionResult {
    bool enabled = false;
    DWORD error = ERROR_SUCCESS;
    DWORD observedAffinity = WDA_NONE;
};
class DisplayProtection final {
public:
    // Windows capture restriction for our own top-level HWND; NOT a security
    // boundary against administrators, kernel/driver/hypervisor control or cameras.
    static ProtectionResult Enable(HWND hwnd);
};
}
