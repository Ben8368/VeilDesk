#include "broker/Protocol.h"
#include "common/Logger.h"
#include <Windows.h>
#include <bcrypt.h>

namespace veil::broker {
bool CreateSessionGeneration(std::uint64_t& generation) {
    generation = 0;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        std::uint64_t random = 0;
        const NTSTATUS status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&random), sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status != 0) {
            Logger::Error(L"Location: BCryptGenRandom; Cause: NTSTATUS=" + std::to_wstring(status) + L"; Action: deny session"); return false;
        }
        if (random && random != (std::numeric_limits<std::uint64_t>::max)()) { generation = random; return true; }
    }
    Logger::Error(L"Location: session generation; Cause: invalid random epochs; Action: deny"); return false;
}
}
