#pragma once
#include "broker/AuthorizationEnvelope.h"
#include <Windows.h>
#include <string>
#include <vector>

namespace veil::broker {
class AuthorizationClient final {
public:
    AuthorizationClient(std::wstring pipeName, bool allowConsolePeer, DWORD consolePeerPid);
    bool Issue(std::uint32_t scope, std::uint32_t durationMs, std::uint32_t maxFrames,
        std::uint32_t maxBytes, AuthorizationEnvelope& envelope);
    bool ConsumeFrame(const std::vector<std::pair<AuthorizationEnvelope, std::uint32_t>>& leases,
        std::uint32_t frame);
    bool RevokeAll();
private:
    bool Exchange(std::uint16_t kind, const std::vector<std::uint8_t>& request,
        std::vector<std::uint8_t>& body);
    bool VerifyServer(HANDLE pipe);
    std::wstring pipeName_;
    bool allowConsolePeer_ = false;
    DWORD consolePeerPid_ = 0;
    std::uint32_t requestId_ = 0;
};
}
