#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

namespace veil::broker {
enum class ChildRole { None, Producer, View, Record };
enum class PublisherState { NotVerified };
struct ProcessIdentity {
    DWORD pid = 0, sessionId = MAXDWORD;
    std::uint64_t creationTime = 0;
    std::wstring imagePath, userSid, publisher;
    PublisherState publisherState = PublisherState::NotVerified;
};
bool ReadProcessIdentity(HANDLE retainedProcess, ProcessIdentity& identity);
bool SamePrincipal(const ProcessIdentity& a, const ProcessIdentity& b) noexcept;
bool SameImage(const std::wstring& a, const std::wstring& b) noexcept;
bool MatchesRetainedProcess(HANDLE retainedProcess, const ProcessIdentity& enrolled);
}
