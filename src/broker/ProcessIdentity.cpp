#include "broker/ProcessIdentity.h"
#include "broker/Handle.h"
#include "common/Logger.h"
#include <sddl.h>
#include <memory>
#include <vector>

namespace veil::broker {
bool ReadProcessIdentity(HANDLE process, ProcessIdentity& identity) {
    ProcessIdentity result;
    result.pid = GetProcessId(process);
    if (!result.pid) { Logger::Win32Error(L"Identity GetProcessId; Action: deny", GetLastError()); return false; }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        Logger::Win32Error(L"Identity GetProcessTimes; Action: deny", GetLastError()); return false;
    }
    result.creationTime = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    wchar_t image[32768]{}; DWORD characters = static_cast<DWORD>(std::size(image));
    if (!QueryFullProcessImageNameW(process, 0, image, &characters)) {
        Logger::Win32Error(L"Identity QueryFullProcessImageNameW; Action: deny", GetLastError()); return false;
    }
    result.imagePath.assign(image, characters);
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) {
        Logger::Win32Error(L"Identity OpenProcessToken(TOKEN_QUERY); Action: deny", GetLastError()); return false;
    }
    Handle token(raw); DWORD bytes = 0;
    if (!GetTokenInformation(token.get(), TokenSessionId, &result.sessionId, sizeof(result.sessionId), &bytes)) {
        Logger::Win32Error(L"Identity TokenSessionId; Action: deny", GetLastError()); return false;
    }
    const BOOL sized = GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    const DWORD error = GetLastError();
    if (sized || error != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(TOKEN_USER) || bytes > 65536) {
        Logger::Win32Error(L"Identity TokenUser bounded size; Action: deny", error); return false;
    }
    std::vector<BYTE> storage(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, storage.data(), bytes, &bytes)) {
        Logger::Win32Error(L"Identity TokenUser; Action: deny", GetLastError()); return false;
    }
    const auto tokenUser = reinterpret_cast<const TOKEN_USER*>(storage.data());
    wchar_t* sid = nullptr;
    if (!ConvertSidToStringSidW(tokenUser->User.Sid, &sid)) {
        Logger::Win32Error(L"Identity ConvertSidToStringSidW; Action: deny", GetLastError()); return false;
    }
    const std::unique_ptr<wchar_t, decltype(&LocalFree)> cleanup(sid, &LocalFree);
    result.userSid = sid;
    identity = std::move(result); return true;
}
bool SamePrincipal(const ProcessIdentity& a, const ProcessIdentity& b) noexcept {
    return !a.userSid.empty() && a.userSid == b.userSid && a.sessionId != MAXDWORD && a.sessionId == b.sessionId;
}
bool SameImage(const std::wstring& a, const std::wstring& b) noexcept {
    return !a.empty() && !b.empty() && CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
bool MatchesRetainedProcess(HANDLE process, const ProcessIdentity& enrolled) {
    const DWORD wait = WaitForSingleObject(process, 0);
    if (wait != WAIT_TIMEOUT) {
        if (wait == WAIT_FAILED) { Logger::Win32Error(L"Identity process wait; Action: deny", GetLastError()); }
        else { Logger::Error(L"Location: Identity; Cause: enrolled process exited; Action: deny"); }
        return false;
    }
    ProcessIdentity actual;
    if (!ReadProcessIdentity(process, actual)) { return false; }
    const bool matches = enrolled.pid && enrolled.creationTime && actual.pid == enrolled.pid &&
        actual.creationTime == enrolled.creationTime && SamePrincipal(actual, enrolled) && SameImage(actual.imagePath, enrolled.imagePath);
    if (!matches) { Logger::Error(L"Location: Identity; Cause: retained subject mismatch; Action: deny"); }
    return matches;
}
}
